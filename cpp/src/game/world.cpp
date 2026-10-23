#include "world.h"

#include <cmath>
#include <stdexcept>
#include <thread>

#include "../../third_party/stb_image.h"
#include "../core/assets.h"

static const char* HDRI_DAY = "assets/hdri/dayskyhdri012b_2k.hdr";
static const char* HDRI_SUNSET = "assets/hdri/eveningskyhdri046b_2k.hdr";
static const float ASPHALT_TILE = 3.2f;  // metros cobertos por uma repetição da textura

void World::loadCPU(const std::function<void(float)>& onProgress) {
  onProgress(0.02f);
  // --- céu HDRI (linhas de cima para baixo, como o RGBELoader antes do flipY)
  int W, H, comp;
  const char* HDRI_URL = timeOfDay == 1 ? HDRI_SUNSET : HDRI_DAY;
  float* rgb = stbi_loadf(assetPath(HDRI_URL).c_str(), &W, &H, &comp, 3);
  if (!rgb) throw std::runtime_error("não foi possível ler " + std::string(HDRI_URL));
  onProgress(0.55f);

  // Encontra o sol (pixel mais brilhante), a cor do horizonte e uma cópia com o sol limitado.
  float best = 0;
  size_t bi = 0;
  for (size_t i = 0; i < (size_t)W * H; i++) {
    float l = rgb[i * 3] * 0.2126f + rgb[i * 3 + 1] * 0.7152f + rgb[i * 3 + 2] * 0.0722f;
    if (l > best) { best = l; bi = i; }
  }
  int px = (int)(bi % W), py = (int)(bi / W);
  float u = (px + 0.5f) / W, v = 1 - (py + 0.5f) / H;
  float lat = (v - 0.5f) * (float)M_PI, phi = (u - 0.5f) * 2 * (float)M_PI;
  sunDir = normalize(vec3(std::cos(phi) * std::cos(lat), std::sin(lat), std::sin(phi) * std::cos(lat)));
  // de dia o sol não desce demais (sombras curtas); no pôr do sol fica rasante, com sombras longas
  float minSunY = timeOfDay == 1 ? 0.06f : 0.15f;
  if (sunDir.y < minSunY) { sunDir.y = minSunY; sunDir = normalize(sunDir); }

  // cor média logo acima do horizonte (para a neblina)
  int row = (int)std::floor(H * 0.47);
  double c[3] = {0, 0, 0};
  for (int x = 0; x < W; x++)
    for (int k = 0; k < 3; k++) c[k] += rgb[((size_t)row * W + x) * 3 + k];
  horizon = vec3((float)(c[0] / W), (float)(c[1] / W), (float)(c[2] / W));
  float m = std::max({horizon.x, horizon.y, horizon.z});
  if (m > 1) horizon *= 1 / m;

  // cópia em meia resolução (a iluminação ambiente é borrada de qualquer jeito)
  int w2 = W >> 1, h2 = H >> 1;
  std::vector<float> clamped((size_t)w2 * h2 * 4);
  for (int y = 0; y < h2; y++)
    for (int x = 0; x < w2; x++) {
      size_t o = ((size_t)y * w2 + x) * 4, a = ((size_t)(y * 2) * W + x * 2) * 3, b = a + (size_t)W * 3;
      for (int k = 0; k < 3; k++) clamped[o + k] = (rgb[a + k] + rgb[a + 3 + k] + rgb[b + k] + rgb[b + 3 + k]) / 4;
      clamped[o + 3] = 1;
    }
  const float LIMIT = 12;
  for (size_t i = 0; i < (size_t)w2 * h2; i++) {
    size_t o = i * 4;
    float l = clamped[o] * 0.2126f + clamped[o + 1] * 0.7152f + clamped[o + 2] * 0.0722f;
    if (l > LIMIT) { float s = LIMIT / l; clamped[o] *= s; clamped[o + 1] *= s; clamped[o + 2] *= s; }
  }
  // metade inferior do HDRI "puresky" não tem chão: preenche com tom de asfalto iluminado
  for (int y = (int)std::floor(h2 * 0.5); y < h2; y++) {
    float t = std::min(1.0f, (y - h2 * 0.5f) / (h2 * 0.03f));
    for (int x = 0; x < w2; x++) {
      size_t o = ((size_t)y * w2 + x) * 4;
      clamped[o] = clamped[o] * (1 - t) + 0.16f * t;
      clamped[o + 1] = clamped[o + 1] * (1 - t) + 0.155f * t;
      clamped[o + 2] = clamped[o + 2] * (1 - t) + 0.15f * t;
    }
  }
  // na GPU a linha 0 fica embaixo (v = 0): inverte as linhas
  envTex = std::make_shared<Texture>();
  envTex->width = w2;
  envTex->height = h2;
  envTex->mipmaps = false;
  envTex->floatPixels.resize(clamped.size());
  for (int y = 0; y < h2; y++)
    std::copy(clamped.begin() + (size_t)(h2 - 1 - y) * w2 * 4, clamped.begin() + (size_t)(h2 - y) * w2 * 4,
              envTex->floatPixels.begin() + (size_t)y * w2 * 4);

  // o fundo vai para a GPU em meia precisão (metade da memória de vídeo)
  background = std::make_shared<Texture>();
  background->width = W;
  background->height = H;
  background->mipmaps = true;  // o shader do céu escolhe o mip pelo tamanho do pixel
  background->halfPixels.resize((size_t)W * H * 4);
  {
    unsigned nT = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < nT; t++)
      pool.emplace_back([&, t] {
        for (int y = t; y < H; y += nT) {
          const float* src = rgb + (size_t)(H - 1 - y) * W * 3;
          uint16_t* dst = background->halfPixels.data() + (size_t)y * W * 4;
          for (int x = 0; x < W; x++) {
            for (int k = 0; k < 3; k++) dst[x * 4 + k] = floatToHalf(std::min(src[x * 3 + k], 400.f));
            dst[x * 4 + 3] = 0x3c00;
          }
        }
      });
    for (auto& th : pool) th.join();
  }
  stbi_image_free(rgb);
  onProgress(0.7f);

  // --- asfalto
  asphaltMap = loadImage("assets/asphalt/asphalt_02_diff.jpg", true);
  onProgress(0.8f);
  asphaltNormal = loadImage("assets/asphalt/asphalt_02_nor_gl.jpg", false);
  onProgress(0.9f);
  asphaltRough = loadImage("assets/asphalt/asphalt_02_rough.jpg", false);
  if (!asphaltMap || !asphaltNormal || !asphaltRough) throw std::runtime_error("texturas do asfalto ausentes");
  onProgress(0.95f);
}

void World::finishGPU() {
  background->upload();
  scene.backgroundTex = background.get();
  scene.backgroundIntensity = 1;

  // environment map sem o disco do sol (o sol vem da luz direcional, com sombra)
  env = Environment::fromEquirect(*envTex, envTex->width / 4);
  envTex.reset();
  scene.env = env.get();
  scene.envIntensity = 1;

  // neblina atmosférica: ~30% de véu a 1,5 km e horizonte quase todo na cor do céu; a densidade
  // cai pela metade a cada ~170 m de altura (morros e céu mais limpos que os vales)
  scene.fog.type = Fog::Atmosphere;
  scene.fog.color = horizon;
  scene.fog.sunColor = horizon * 0.4f + vec3(1.0f, 0.86f, 0.66f) * 1.1f;
  scene.fog.density = 0.00040f;
  if (timeOfDay == 1) {
    // fim de tarde: ar mais carregado e véu alaranjado contra o sol
    scene.fog.sunColor = horizon * 0.3f + vec3(1.0f, 0.58f, 0.28f) * 1.2f;
    scene.fog.density = 0.00055f;
  }
  scene.fog.falloff = 0.005f;
  scene.fog.base = 0;

  // --- sol
  sun.color = hexColor(timeOfDay == 1 ? 0xffb066 : 0xfff1dc);
  sun.intensity = timeOfDay == 1 ? 4.8f : 6.0f;
  sun.castShadow = true;
  sun.mapSize = 4096;
  sun.left = -12; sun.right = 12; sun.top = 12; sun.bottom = -12; sun.near = 1; sun.far = 200;
  sun.bias = -0.00015f;
  sun.normalBias = 0.025f;
  scene.light = &sun;
  follow(vec3());

  // --- chão
  for (auto& t : {asphaltMap, asphaltNormal, asphaltRough}) {
    t->repeat = true;
    t->anisotropy = 16;
    t->upload();
  }
  const float extent = 12000, e = extent / 2;
  auto geo = std::make_shared<Geometry>();
  // PlaneGeometry girada −90° em X; UV em metros (a escala é aplicada no shader)
  geo->pos = {{-e, 0, e}, {e, 0, e}, {-e, 0, -e}, {e, 0, -e}};
  geo->nrm.assign(4, vec3(0, 1, 0));
  geo->uv0 = {{-e, -e}, {e, -e}, {-e, e}, {e, e}};
  geo->idx = {0, 1, 2, 2, 1, 3};
  geo->computeBounds();
  auto mat = std::make_shared<Material>();
  mat->map = {asphaltMap, 0};
  mat->normalMap = {asphaltNormal, 0};
  mat->roughnessMap = {asphaltRough, 0};
  mat->normalScale = vec2(1.2f, 1.2f);
  mat->roughness = 1;
  mat->metalness = 0;
  mat->color = hexColor(0xb4b4b4);
  mat->asphaltTile = 1 / ASPHALT_TILE;
  flatGround = makeNode("ground");
  flatGround->geometry = geo;
  flatGround->material = mat;
  flatGround->receiveShadow = true;
  scene.root->add(flatGround);
}

void World::setTrack(TrackMap* t) {
  if (track) scene.root->remove(track->root.get());
  track = t;
  flatGround->visible = t == nullptr;
  scene.fog.base = t ? t->spawnPos.y : 0.f;  // a névoa assenta no nível da pista
  if (t) scene.root->add(t->root);
}

void World::setShadowDistance(float meters) {
  float h = meters / 2;
  sun.left = -h; sun.right = h; sun.top = h; sun.bottom = -h;
  // a "câmera" do sol fica longe o bastante para pegar morros e prédios fora do quadrado
  shadowReach = h + 120;
  sun.near = 1;
  sun.far = shadowReach * 2;
  // texel maior com alcance maior: o desvio pela normal cresce junto (sem "acne" na pista)
  sun.normalBias = 0.025f * std::max(1.f, meters / 24);
}

void World::follow(const vec3& p) {
  sun.target = p;
  sun.position = p + sunDir * shadowReach;
}
