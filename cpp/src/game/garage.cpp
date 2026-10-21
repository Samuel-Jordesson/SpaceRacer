#include "garage.h"

#include <cmath>
#include <random>

#include "cinematic.h"

static const uint32_t BG = 0xf3f3f2;

// ---------------------------------------------------------------- espelho (Reflector)
namespace {
const char* MIRROR_VS = R"(
layout(location = 0) in vec3 aPos;
uniform mat4 uView, uProj;
void main() { gl_Position = uProj * uView * vec4(aPos, 1.0); }
)";
const char* MIRROR_FS = R"(
uniform sampler2D tDiffuse;
uniform vec3 uColor;
uniform vec2 uViewport;
out vec4 fragColor;
float blendOverlay(float base, float blend) { return base < 0.5 ? (2.0 * base * blend) : (1.0 - 2.0 * (1.0 - base) * (1.0 - blend)); }
vec3 blendOverlay(vec3 base, vec3 blend) { return vec3(blendOverlay(base.r, blend.r), blendOverlay(base.g, blend.g), blendOverlay(base.b, blend.b)); }
void main() {
  // pontos do plano projetam no mesmo lugar nas duas câmeras: amostra na posição da tela
  vec4 base = texture(tDiffuse, gl_FragCoord.xy / uViewport);
  fragColor = vec4(blendOverlay(base.rgb, uColor), 1.0);
}
)";
}  // namespace

Mirror::Mirror(float s, float yy, vec3 c) : size(s), y(yy), color(c) {
  shader = std::make_unique<Shader>(MIRROR_VS, MIRROR_FS);
  float e = size / 2;
  float v[18] = {-e, y, e, e, y, e, e, y, -e, -e, y, e, e, y, -e, -e, y, -e};
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
  glBindVertexArray(0);
}

Mirror::~Mirror() {
  resize(0, 0);
  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
}

void Mirror::resize(int nw, int nh) {
  if (nw == w && nh == h) return;
  if (fbo) {
    glDeleteFramebuffers(1, &fbo);
    glDeleteFramebuffers(1, &msFbo);
    glDeleteTextures(1, &tex);
    glDeleteRenderbuffers(1, &msColor);
    glDeleteRenderbuffers(1, &msDepth);
    fbo = msFbo = tex = msColor = msDepth = 0;
  }
  w = nw;
  h = nh;
  if (w <= 0 || h <= 0) return;
  // alvo em meia precisão com MSAA 4 (como o Reflector do three r170)
  glGenFramebuffers(1, &msFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
  glGenRenderbuffers(1, &msColor);
  glBindRenderbuffer(GL_RENDERBUFFER, msColor);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA16F, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
  glGenRenderbuffers(1, &msDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Mirror::renderReflection(Renderer& r, Scene& scene, const Camera& cam, const std::function<bool(const Node&)>& exclude) {
  if (!fbo || cam.position.y < y) return;  // câmera atrás do espelho: não renderiza
  glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
  glViewport(0, 0, w, h);
  RenderOptions o;
  o.clip = true;
  o.clipPlane = vec4(0, 1, 0, -y);
  o.mirrored = true;
  o.exclude = exclude;
  o.customs = false;
  r.render(scene, cam, w, h, o);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
  glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Mirror::draw(const RenderContext& ctx) {
  Shader& s = *shader;
  s.use();
  s.set("uView", ctx.view);
  s.set("uProj", ctx.proj);
  s.set("tDiffuse", 0);
  s.set("uColor", color);
  s.set("uViewport", vec2((float)ctx.viewportW, (float)ctx.viewportH));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
}

// ---------------------------------------------------------------- placa de porcelanato
// branco com veios muito sutis e rejunte fino (mesmo desenho do canvas 2D da versão web)
static TexturePtr porcelainTexture() {
  const int s = 1024;
  auto t = std::make_shared<Texture>();
  t->width = t->height = s;
  t->pixels.resize((size_t)s * s * 4);
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> u(0, 1);
  for (int i = 0; i < s * s; i++) {
    int x = i % s, y = i / s;
    float vein = std::sin(x * 0.011f + std::sin(y * 0.007f) * 3 + std::sin((x + y) * 0.003f) * 4);
    float v = (std::fabs(vein) < 0.03f ? -6 : 0) + (u(rng) - 0.5f) * 3;
    int c[3] = {0xfb, 0xfb, 0xfa};
    for (int k = 0; k < 3; k++) t->pixels[i * 4 + k] = (uint8_t)std::clamp((int)std::lround(c[k] + v), 0, 255);
    t->pixels[i * 4 + 3] = 255;
  }
  // strokeRect com linha de 3 px centrada na borda: 1,5 px visível em cada lado
  for (int y = 0; y < s; y++)
    for (int x = 0; x < s; x++) {
      float d = (float)std::min({x, y, s - 1 - x, s - 1 - y}) + 0.5f;
      float cov = std::clamp(1.5f - d + 0.5f, 0.f, 1.f);
      if (cov <= 0) continue;
      uint8_t* p = &t->pixels[((size_t)y * s + x) * 4];
      const int line[3] = {190, 190, 188};
      float a = 0.9f * cov;
      for (int k = 0; k < 3; k++) p[k] = (uint8_t)std::lround(line[k] * a + p[k] * (1 - a));
    }
  t->srgb = true;
  t->repeat = true;
  t->anisotropy = 16;
  return t;
}

void resetPose(LoadedCar& loaded) {
  loaded.root->position = vec3();
  loaded.root->quaternion = quat();
  for (auto& [id, p] : loaded.pivots) {
    p.steer->position = p.base;
    p.steer->quaternion = quat();
    p.spin->quaternion = quat();
    for (auto& m : p.glow) {
      if (m->glowQuad) m->opacity = 0;
      m->emissiveIntensity = 0;
    }
    for (auto& m : p.blur) m->opacity = 0;
  }
  if (loaded.steeringWheel) loaded.steeringWheel->pivot->quaternion = quat();
  for (auto& m : loaded.tailLights) m->emissiveIntensity = 0.8f;
}

// ---------------------------------------------------------------- garagem
Garage::Garage(Renderer& r) : renderer(r), controls(&camera) {
  scene.background = hexColor(BG);
  scene.fog.type = Fog::Linear;
  scene.fog.color = hexColor(BG);
  scene.fog.near = 14;
  scene.fog.far = 38;
  env = Environment::room();
  scene.env = env.get();
  scene.envIntensity = 1;

  // luz principal de cima (sombra suave) + preenchimento
  key.color = vec3(1);
  key.intensity = 2.2f;
  key.position = vec3(2, 12, 3);
  key.target = vec3();
  key.castShadow = true;
  key.mapSize = 2048;
  key.left = -5; key.right = 5; key.top = 5; key.bottom = -5; key.near = 1; key.far = 30;
  key.bias = -0.0003f;
  scene.light = &key;
  scene.hemi = true;
  scene.hemiSky = vec3(1);
  scene.hemiGround = hexColor(0xe8e8e8);
  scene.hemiIntensity = 0.6f;

  // piso: espelho (reflexo planar) + camada de porcelanato branco semitransparente por cima
  const float size = 80;
  mirror = std::make_unique<Mirror>(size, -0.001f, hexColor(0xd0d0d0));
  scene.opaqueCustom.push_back(mirror.get());
  {
    auto geo = std::make_shared<Geometry>();
    float e = size / 2;
    geo->pos = {{-e, 0, e}, {e, 0, e}, {-e, 0, -e}, {e, 0, -e}};
    geo->nrm.assign(4, vec3(0, 1, 0));
    geo->uv0 = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    geo->idx = {0, 1, 2, 2, 1, 3};
    geo->computeBounds();
    auto m = std::make_shared<Material>();
    m->physical = true;
    m->color = vec3(1);
    m->map = {porcelainTexture(), 0};
    m->mapRepeat = vec2(size / 1.2f, size / 1.2f);  // placas de 120 × 120 cm
    m->roughness = 0.18f;
    m->metalness = 0;
    m->clearcoat = 1;
    m->clearcoatRoughness = 0.05f;
    m->transparent = true;
    m->opacity = 0.62f;
    floor = makeNode("floor");
    floor->geometry = geo;
    floor->material = m;
    floor->receiveShadow = true;
    scene.root->add(floor);
  }
  // base giratória sutil (anel)
  {
    auto geo = std::make_shared<Geometry>();
    const int seg = 128;
    const float r0 = 3.15f, r1 = 3.2f;
    for (int i = 0; i <= seg; i++) {
      float a = (float)i / seg * 2 * (float)M_PI;
      for (float r : {r0, r1}) {
        geo->pos.push_back(vec3(std::cos(a) * r, 0, -std::sin(a) * r));
        geo->nrm.push_back(vec3(0, 1, 0));
      }
    }
    for (int i = 0; i < seg; i++) {
      uint32_t a = i * 2, b = a + 1, c = a + 2, d = a + 3;
      for (uint32_t k : {a, b, d, a, d, c}) geo->idx.push_back(k);
    }
    geo->computeBounds();
    auto m = std::make_shared<Material>();
    m->kind = Material::Kind::Basic;
    m->color = hexColor(0xd9d9d9);
    m->transparent = true;
    m->opacity = 0.8f;
    disc = makeNode("disc");
    disc->geometry = geo;
    disc->material = m;
    disc->position.y = 0.002f;
    scene.root->add(disc);
  }

  turntable = makeNode("turntable");
  scene.root->add(turntable);

  camera.fov = 32;
  camera.near = 0.1f;
  camera.far = 200;
  // ¾ frontal pela direita, mais rente ao chão
  camera.position = vec3(5.2f, 1.2f, -5.0f);
  controls.target = vec3(0, 0.55f, 0);
  controls.enableDamping = true;
  controls.dampingFactor = 0.06f;
  controls.enablePan = false;
  controls.minDistance = 5.2f;
  controls.maxDistance = 10.5f;
  controls.minPolarAngle = (float)M_PI * 0.28f;
  controls.maxPolarAngle = (float)M_PI * 0.485f;
  controls.autoRotate = true;
  controls.autoRotateSpeed = 0.6f;
  controls.onStart = [this] { controls.autoRotate = false; resumeTimer = -1; };
  controls.onEnd = [this] { resumeTimer = 4; };

  fx.vignette = 0.1f;
  fx.tint = 0;
  fx.contrast = 1.03f;
  fx.saturation = 1.05f;
  fx.toneMapping = PostFX::ToneMapping::Neutral;
  fx.exposure = 1;
}

Garage::~Garage() = default;

void Garage::setActive(bool on) {
  active = on;
  controls.enabled = on;
  if (on) {
    // tone mapping "PBR Neutral": brancos ficam brancos, cores fiéis (fotografia de produto)
    fx.toneMapping = PostFX::ToneMapping::Neutral;
    fx.exposure = 1;
    resize(winW, winH, dpr);
  }
}

// coloca o carro (root já normalizado: chão em y = 0, centro do entre-eixos na origem)
void Garage::showCar(const std::shared_ptr<LoadedCar>& loaded) {
  if (car) turntable->remove(car->root.get());
  car = loaded;
  resetPose(*loaded);
  turntable->add(loaded->root);
  intro = 0;  // animação de entrada
}

// devolve o carro para a garagem depois de pilotar
void Garage::reclaimCar() {
  if (!car) return;
  resetPose(*car);
  turntable->add(car->root);
}

void Garage::resize(int w, int h, float d) {
  winW = w;
  winH = h;
  dpr = d;
  float aspect = camera.aspect = (float)w / std::max(1, h);
  // telas estreitas (celular em pé) precisam de mais distância para o carro caber
  float dist = baseDist = 7.2f * std::max(1.0f, 1.2f / aspect);
  controls.minDistance = dist * 0.72f;
  controls.maxDistance = dist * 1.45f;
  scene.fog.near = dist + 7;
  scene.fog.far = dist + 31;
  fx.resize(w, h, d);
  mirror->resize((int)std::floor(w * 0.75f), (int)std::floor(h * 0.75f));
}

void Garage::applySettings(const Settings& st) {
  fx.setQuality(st.quality(), st.preset);
  fx.lensFx = st.lensFx;
  fx.speedBlur = st.speedBlur;
  fx.resize(winW, winH, dpr);
  // a luz do estúdio usa no máximo 2048 (como na versão web)
  key.mapSize = std::min(2048, st.shadow);
}

void Garage::mouseDown(int button, float x, float y) { controls.pointerDown(button, x, y); }
void Garage::mouseMove(float x, float y) { controls.pointerMove(x, y, (float)winH); }
void Garage::mouseUp() { controls.pointerUp(); }
void Garage::wheel(float dy) { controls.wheel(dy); }

void Garage::render(float dt) {
  camera.update();
  scene.root->updateMatrixWorld();
  renderer.renderShadows(scene);
  // o piso e o anel ficam acima do espelho: somem enquanto o reflexo é renderizado
  Node* f = floor.get();
  Node* d = disc.get();
  mirror->renderReflection(renderer, scene, camera, [f, d](const Node& n) { return &n == f || &n == d; });
  fx.beginScene();
  renderer.render(scene, camera, fx.width, fx.height);
  fx.endScene(dt, 0, camera);
}

void Garage::frame(float dt) {
  if (resumeTimer > 0) {
    resumeTimer -= dt;
    if (resumeTimer <= 0) controls.autoRotate = true;
  }
  if (cine && cine->active) {
    cine->update(dt);
    render(dt);
    return;
  }
  // entrada do carro: câmera se aproxima suavemente
  if (intro < 1) {
    intro = std::min(1.f, intro + dt / 1.6f);
    float e = 1 - std::pow(1 - intro, 3.f);
    vec3 dir = normalize(camera.position - controls.target);
    camera.position = controls.target + dir * (baseDist + 5.8f * (1 - e));
  }
  controls.update(dt);
  render(dt);
}
