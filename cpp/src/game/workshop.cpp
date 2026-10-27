#include "workshop.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../core/assets.h"
#include "../physics/vehicle.h"
#include "../../third_party/stb_image.h"

namespace fs = std::filesystem;

static vec3 hslColor(float h, float sat, float l) {
  auto f = [&](float n) {
    float k = std::fmod(n + h / 30.f, 12.f), a = sat * std::min(l, 1 - l);
    return l - a * std::max(-1.f, std::min({k - 3, 9 - k, 1.f}));
  };
  // a grade é em sRGB (como o usuário vê); o material trabalha em linear
  auto lin = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
  return vec3(lin(f(0)), lin(f(8)), lin(f(4)));
}

std::vector<vec3> colorGrid(int& cols) {
  cols = 12;
  std::vector<vec3> g;
  for (int i = 0; i < 12; i++) g.push_back(hslColor(0, 0, 0.97f - i * 0.085f));  // cinzas
  const float L[6] = {0.82f, 0.68f, 0.55f, 0.43f, 0.31f, 0.2f};
  for (float l : L)
    for (int i = 0; i < 12; i++) g.push_back(hslColor(i * 30.f, 0.85f, l));
  return g;
}

// faixas e nomes dos ajustes (o efeito de cada nível está em tunedSpec)
const std::vector<TuneItem> TUNE_ITEMS = {
    {0, "Mapa do motor", "Reprogramação da central: mais torque em toda a faixa de giro (+6% por nível).",
     &CarTune::power, 0, 5, {"Original", "Nível 1", "Nível 2", "Nível 3", "Nível 4", "Nível 5"}},
    {0, "Pressão do turbo", "Mais pressão no turbo: mais torque e o turbo enche mais cedo. Não muda nada em motor aspirado.",
     &CarTune::turbo, 0, 3, {"Original", "Nível 1", "Nível 2", "Nível 3"}},
    {0, "Limite de giro", "Sobe o corte de giro (+250 rpm por nível): dá para esticar mais cada marcha.",
     &CarTune::limiter, 0, 4, {}},
    {0, "Relação final", "Relação do diferencial. Mais curta acelera mais forte; mais longa dá mais velocidade máxima.",
     &CarTune::finalDrive, -3, 3, {}},
    {0, "Redução de peso", "Peças mais leves (−2% de massa por nível): acelera, freia e contorna melhor.",
     &CarTune::weight, 0, 4, {"Original", "Nível 1", "Nível 2", "Nível 3", "Nível 4"}},
    {1, "Molas dianteiras", "Molas mais duras deixam a frente mais precisa e com menos rolagem; mais macias dão mais aderência em ondulações.",
     &CarTune::springF, -3, 3, {}},
    {1, "Molas traseiras", "Traseira mais dura deixa o carro mais solto na saída das curvas; mais macia dá mais tração.",
     &CarTune::springR, -3, 3, {}},
    {1, "Barra estabilizadora dianteira", "Mais dura: o carro sai de frente (subesterço). Mais macia: a frente vira mais.",
     &CarTune::arbF, -3, 3, {}},
    {1, "Barra estabilizadora traseira", "Mais dura: a traseira escorrega mais fácil e o carro gira mais rápido. Mais macia: mais estável.",
     &CarTune::arbR, -3, 3, {}},
    {1, "Amortecedores", "Mais firmes: respostas rápidas e menos balanço. Mais macios: o carro absorve melhor zebras.",
     &CarTune::damper, -3, 3, {}},
    {1, "Altura do carro", "Mais baixo: centro de gravidade menor, menos rolagem; cuidado com zebras altas.",
     &CarTune::rideHeight, -3, 3, {}},
    {1, "Composto dos pneus", "Aderência do pneu: rua escorrega mais (bom para drift), corrida gruda no asfalto.",
     &CarTune::tire, -1, 2, {"Rua", "Original", "Esporte+", "Corrida"}},
    {1, "Força dos freios", "Pressão máxima dos freios. Mais força para mais cedo, mas trava as rodas com mais facilidade.",
     &CarTune::brakeForce, -2, 2, {}},
    {1, "Balanço dos freios", "Mais na frente: estável na frenagem. Mais atrás: o carro gira ao frear (pode rodar).",
     &CarTune::brakeBias, -3, 3, {}},
    {1, "Diferencial (aceleração)", "Mais travado: tração forte e tendência a sair de traseira acelerando. Menos: gira mais fácil.",
     &CarTune::diffPower, -3, 3, {}},
    {1, "Diferencial (desaceleração)", "Mais travado: estável ao tirar o pé. Menos: a traseira ajuda a contornar na entrada da curva.",
     &CarTune::diffCoast, -3, 3, {}},
    {1, "Ângulo de esterço", "Quanto as rodas viram. Mais ângulo ajuda em curvas fechadas e no drift.",
     &CarTune::steerLock, -2, 3, {}},
    {1, "Pressão aerodinâmica", "Mais asa: cola na pista em curvas rápidas, mas perde velocidade de reta.",
     &CarTune::aero, -3, 3, {}},
    {1, "Balanço aerodinâmico", "Mais na frente: a frente vira mais em alta. Mais atrás: estável em curvas rápidas.",
     &CarTune::aeroBal, -3, 3, {}},
};

static std::string prefDir() {
  char* d = SDL_GetPrefPath("SpaceRacer", "SpaceRacer");
  std::string p = d ? std::string(d) : "";
  SDL_free(d);
  return p + "oficina/";
}

CarShop loadShop(const std::string& carId) {
  CarShop s;
  std::ifstream in(prefDir() + carId + ".ini");
  if (!in) return s;
  std::string line;
  Livery* cur = nullptr;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (line == "[pintura]") {
      s.liveries.emplace_back();
      cur = &s.liveries.back();
      continue;
    }
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    int iv = std::atoi(v.c_str());
    if (cur) {
      auto v3 = [](const std::string& t) {
        vec3 r;
        std::sscanf(t.c_str(), "%f,%f,%f", &r.x, &r.y, &r.z);
        return r;
      };
      if (k == "name") cur->name = v;
      else if (k == "repaint") cur->repaint = iv != 0;
      else if (k == "paint") cur->paint = v3(v);
      else if (k == "finish") cur->finish = std::clamp(iv, 0, 2);
      else if (k == "keepArt") cur->keepArt = iv != 0;
      else if (k == "rimCustom") cur->rimCustom = iv != 0;
      else if (k == "rim") cur->rim = v3(v);
      else if (k == "rimFinish") cur->rimFinish = std::clamp(iv, 0, 3);
      else if (k == "sticker") {
        // arquivo|cx,cy,cz|nx,ny,nz|ux,uy,uz|tamanho|rotação|espelho
        std::vector<std::string> f;
        std::stringstream ss(v);
        std::string part;
        while (std::getline(ss, part, '|')) f.push_back(part);
        if (f.size() >= 7) {
          Sticker st;
          st.file = f[0];
          st.c = v3(f[1]);
          st.n = v3(f[2]);
          st.up = v3(f[3]);
          st.size = std::clamp((float)std::atof(f[4].c_str()), 0.1f, 3.f);
          st.rot = (float)std::atof(f[5].c_str());
          st.mirror = std::atoi(f[6].c_str()) != 0;
          cur->stickers.push_back(st);
        }
      }
      continue;
    }
    if (k == "livery") s.livery = iv;
    for (auto& it : TUNE_ITEMS)
      if (k == it.label) s.tune.*it.field = std::clamp(iv, it.lo, it.hi);
  }
  if (s.livery >= (int)s.liveries.size()) s.livery = -1;
  return s;
}

void saveShop(const std::string& carId, const CarShop& s) {
  std::error_code ec;
  fs::create_directories(prefDir(), ec);
  std::ofstream out(prefDir() + carId + ".ini");
  out << "livery=" << s.livery << "\n";
  for (auto& it : TUNE_ITEMS) out << it.label << "=" << s.tune.*it.field << "\n";
  auto v3 = [](const vec3& c) {
    char b[64];
    std::snprintf(b, sizeof b, "%.4f,%.4f,%.4f", c.x, c.y, c.z);
    return std::string(b);
  };
  for (auto& l : s.liveries) {
    out << "[pintura]\nname=" << l.name << "\nrepaint=" << l.repaint << "\npaint=" << v3(l.paint) << "\nfinish=" << l.finish
        << "\nkeepArt=" << l.keepArt << "\nrimCustom=" << l.rimCustom << "\nrim=" << v3(l.rim) << "\nrimFinish=" << l.rimFinish << "\n";
    for (auto& st : l.stickers)
      out << "sticker=" << st.file << "|" << v3(st.c) << "|" << v3(st.n) << "|" << v3(st.up) << "|" << st.size << "|" << st.rot << "|"
          << st.mirror << "\n";
  }
}

phys::CarSpec tunedSpec(const phys::CarSpec& base, const CarTune& t) {
  phys::CarSpec s = base;
  // motor
  double tq = 1 + 0.06 * t.power;
  bool turbo = s.engine.turboShare > 0.01;
  double boost = turbo ? 0.05 * t.turbo : 0;
  for (auto& [rpm, nm] : s.engine.torqueCurve) {
    // o turbo rende mais a partir de onde a pressão começa a subir
    double b = rpm >= s.engine.boostRpmStart ? boost : boost * 0.3;
    nm *= tq + b;
  }
  if (turbo && t.turbo > 0) {
    s.engine.boostRpmStart = std::max(1000.0, s.engine.boostRpmStart - 120.0 * t.turbo);
    s.engine.boostRpmFull = std::max(s.engine.boostRpmStart + 400, s.engine.boostRpmFull - 180.0 * t.turbo);
  }
  if (t.limiter > 0) {
    double add = 250.0 * t.limiter;
    s.engine.limiterRpm += add;
    // estende a curva: o torque cai depois do pico original
    if (!s.engine.torqueCurve.empty()) {
      auto last = s.engine.torqueCurve.back();
      s.engine.torqueCurve.push_back({last.first + add, last.second * (1 - 0.00008 * add)});
    }
  }
  s.gearbox.finalDrive *= 1 + 0.045 * t.finalDrive;
  double massK = 1 - 0.02 * t.weight;
  s.mass *= massK;
  s.inertia.pitch *= massK;
  s.inertia.yaw *= massK;
  s.inertia.roll *= massK;
  // suspensão
  s.suspension.front.k *= 1 + 0.1 * t.springF;
  s.suspension.rear.k *= 1 + 0.1 * t.springR;
  s.suspension.front.arb *= 1 + 0.15 * t.arbF;
  s.suspension.rear.arb *= 1 + 0.15 * t.arbR;
  double d = 1 + 0.12 * t.damper;
  for (auto* su : {&s.suspension.front, &s.suspension.rear}) {
    su->bump *= d;
    su->rebound *= d;
  }
  s.cgHeight += 0.008 * t.rideHeight;
  // pneus, freios, diferencial, direção, aerodinâmica
  static const double TIRE[4] = {0.86, 1.0, 1.05, 1.1};
  s.tire.mu *= TIRE[std::clamp(t.tire + 1, 0, 3)];
  double bf = 1 + 0.08 * t.brakeForce;
  double total = s.brakes.maxTorqueFront + s.brakes.maxTorqueRear;
  double front = s.brakes.maxTorqueFront / total + 0.02 * t.brakeBias;
  s.brakes.maxTorqueFront = total * front * bf;
  s.brakes.maxTorqueRear = total * (1 - front) * bf;
  s.differential.powerLock = std::clamp(s.differential.powerLock + 0.08 * t.diffPower, 0.0, 1.0);
  s.differential.coastLock = std::clamp(s.differential.coastLock + 0.08 * t.diffCoast, 0.0, 1.0);
  s.steering.maxAngleDeg += 3.0 * t.steerLock;
  s.aero.ClA *= 1 + 0.12 * t.aero;
  s.aero.CdA *= 1 + 0.03 * t.aero;
  s.aero.frontBalance = std::clamp(s.aero.frontBalance + 0.02 * t.aeroBal, 0.2, 0.7);
  return s;
}

static double torqueAt(const phys::CarSpec& s, double rpm) {
  auto& c = s.engine.torqueCurve;
  if (c.empty()) return 0;
  if (rpm <= c.front().first) return c.front().second;
  for (size_t i = 1; i < c.size(); i++)
    if (rpm <= c[i].first) {
      double t = (rpm - c[i - 1].first) / (c[i].first - c[i - 1].first);
      return c[i - 1].second + (c[i].second - c[i - 1].second) * t;
    }
  return c.back().second;
}

Perf estimatePerf(const phys::CarSpec& spec, const phys::Geometry* geo) {
  Perf p{};
  // potência e torque máximos pela curva
  double bestW = 0, bestT = 0;
  for (double rpm = spec.engine.idleRpm; rpm <= spec.engine.limiterRpm; rpm += 50) {
    double t = torqueAt(spec, rpm);
    bestT = std::max(bestT, t);
    bestW = std::max(bestW, t * rpm * 2 * M_PI / 60);
  }
  if (spec.electricRear.present) bestW += spec.electricRear.power;
  if (spec.electricFront.present) bestW += 2 * spec.electricFront.powerPerWheel;
  p.powerCv = (float)(bestW / 735.5);
  p.torqueNm = (float)bestT;
  p.massKg = (float)spec.mass;
  // 0–100: simulação curta em chão plano (câmbio automático, controle de tração)
  {
    phys::FlatGround ground;
    phys::Vehicle car(spec, &ground, geo);
    phys::Controls idle;
    for (int i = 0; i < 1500; i++) car.step(0.001, idle);
    phys::Controls in{1, 0, 0, 0};
    double t = 0;
    while (t < 15 && car.forwardSpeed() * 3.6 < 100) {
      car.step(0.001, in);
      t += 0.001;
    }
    p.zeroTo100 = (float)t;
  }
  // velocidade máxima: maior velocidade em que alguma marcha ainda vence o arrasto
  {
    double r = geo && geo->radiusRear > 0 ? geo->radiusRear : spec.geometry.radiusRear;
    if (r <= 0) r = 0.34;
    double top = 0;
    for (double v = 5; v < 140; v += 0.25) {
      double drag = 0.5 * spec.aero.rho * spec.aero.CdA * v * v + spec.mass * 9.81 * spec.tire.rollingResistance;
      double best = 0;
      for (double ratio : spec.gearbox.ratios) {
        double k = ratio * spec.gearbox.finalDrive;
        double rpm = v / r * k * 60 / (2 * M_PI);
        if (rpm > spec.engine.limiterRpm) continue;
        best = std::max(best, torqueAt(spec, std::max(rpm, spec.engine.idleRpm)) * k * spec.gearbox.efficiency / r);
      }
      if (best < drag) break;
      top = v;
    }
    p.topKmh = (float)(top * 3.6);
  }
  // índice de desempenho: relação peso/potência, aderência e pressão aerodinâmica
  double cvt = p.powerCv / (p.massKg / 1000);
  p.pi = (int)std::clamp(100 + cvt + 200 * (spec.tire.mu - 1.0) + 20 * spec.aero.ClA, 100.0, 999.0);
  return p;
}

std::vector<std::string> decalFiles() {
  std::vector<std::string> out;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(assetPath("decals"), ec)) {
    std::string ext = e.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") out.push_back(e.path().filename().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string importDecal(const std::string& path) {
  std::error_code ec;
  fs::path src(path), dir(assetPath("decals"));
  fs::create_directories(dir, ec);
  std::string stem = src.stem().string(), ext = src.extension().string();
  fs::path dst = dir / src.filename();
  for (int i = 2; fs::exists(dst, ec) && !fs::equivalent(dst, src, ec); i++) dst = dir / (stem + "-" + std::to_string(i) + ext);
  if (!fs::equivalent(dst, src, ec)) fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
  return ec ? "" : dst.filename().string();
}

mat4 stickerMatrix(const Sticker& st, float aspect) {
  vec3 f = normalize(st.n);
  vec3 u0 = st.up - f * dot(st.up, f);
  if (length(u0) < 1e-4f) u0 = std::fabs(f.y) < 0.9f ? vec3(0, 1, 0) : vec3(0, 0, -1);
  u0 = normalize(u0);
  vec3 r0 = cross(f, u0);  // direita de quem olha ao longo da projeção
  float a = st.rot * (float)M_PI / 180, ca = std::cos(a), sa = std::sin(a);
  vec3 R = r0 * ca + u0 * sa, U = u0 * ca - r0 * sa;
  float w = st.size, h = st.size / std::max(0.1f, aspect), depth = 0.35f;
  mat4 m;
  auto row = [&](int r, const vec3& v, float s, float off) {
    m.e[0 * 4 + r] = v.x * s;
    m.e[1 * 4 + r] = v.y * s;
    m.e[2 * 4 + r] = v.z * s;
    m.e[3 * 4 + r] = -dot(v, st.c) * s + off;
  };
  row(0, R, 1 / w, 0.5f);
  row(1, U, 1 / h, 0.5f);
  row(2, f, 1 / depth, 0);
  m.e[3] = m.e[7] = m.e[11] = 0;
  m.e[15] = 1;
  return m;
}

// monta a textura em camadas com as imagens dos adesivos (512×512 cada, proporção guardada)
static void buildDecalArray(LoadedCar& car, const std::vector<std::string>& files) {
  auto ds = std::make_shared<DecalSet>();
  ds->space = car.root.get();
  const int S = 512;
  std::vector<uint8_t> layers;
  for (auto& f : files) {
    int w, h, n;
    uint8_t* px = stbi_load(assetPath("decals/" + f).c_str(), &w, &h, &n, 4);
    std::vector<uint8_t> out((size_t)S * S * 4, 0);
    float aspect = 1;
    if (px) {
      aspect = (float)w / h;
      // reamostragem bilinear para o quadrado (a proporção volta na matriz do adesivo)
      for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
          float sx = (x + 0.5f) * w / S - 0.5f, sy = (y + 0.5f) * h / S - 0.5f;
          int x0 = std::clamp((int)std::floor(sx), 0, w - 1), y0 = std::clamp((int)std::floor(sy), 0, h - 1);
          int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
          float fx = std::clamp(sx - x0, 0.f, 1.f), fy = std::clamp(sy - y0, 0.f, 1.f);
          for (int c = 0; c < 4; c++) {
            float a = px[(y0 * w + x0) * 4 + c] * (1 - fx) + px[(y0 * w + x1) * 4 + c] * fx;
            float b = px[(y1 * w + x0) * 4 + c] * (1 - fx) + px[(y1 * w + x1) * 4 + c] * fx;
            out[((size_t)y * S + x) * 4 + c] = (uint8_t)(a * (1 - fy) + b * fy + 0.5f);
          }
        }
      stbi_image_free(px);
    }
    ds->files.push_back(f);
    ds->aspects.push_back(aspect);
    layers.insert(layers.end(), out.begin(), out.end());
  }
  if (!files.empty()) {
    glGenTextures(1, &ds->array);
    glBindTexture(GL_TEXTURE_2D_ARRAY, ds->array);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_SRGB8_ALPHA8, S, S, (GLsizei)files.size(), 0, GL_RGBA, GL_UNSIGNED_BYTE, layers.data());
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, std::min(8.f, gl::maxAnisotropy()));
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  }
  car.decals = ds;
}

static void finishMat(Material& m, int finish) {
  if (finish == 0) { m.metalness = 0; m.roughness = 0.32f; m.clearcoat = 1; m.clearcoatRoughness = 0.03f; }
  else if (finish == 1) { m.metalness = 0.65f; m.roughness = 0.3f; m.clearcoat = 1; m.clearcoatRoughness = 0.04f; }
  else if (finish == 2) { m.metalness = 0; m.roughness = 0.78f; m.clearcoat = 0; }
  else { m.metalness = 1; m.roughness = 0.08f; m.clearcoat = 0.5f; m.clearcoatRoughness = 0.02f; }  // cromado
  if (m.clearcoat > 0) m.physical = true;
}

void applyLivery(LoadedCar& car, const CarShop& shop) {
  const Livery* l = shop.livery >= 0 && shop.livery < (int)shop.liveries.size() ? &shop.liveries[shop.livery] : nullptr;
  // adesivos: uma camada por imagem diferente
  std::vector<std::string> files;
  if (l)
    for (auto& st : l->stickers)
      if (std::find(files.begin(), files.end(), st.file) == files.end() && (int)files.size() < DecalSet::MAX) files.push_back(st.file);
  std::string key;
  for (auto& f : files) key += f + "|";
  if (key != car.decalKey || !car.decals) {
    buildDecalArray(car, files);
    car.decalKey = key;
  }
  DecalSet& ds = *car.decals;
  ds.count = 0;
  if (l)
    for (auto& st : l->stickers) {
      if (ds.count >= DecalSet::MAX) break;
      int layer = (int)(std::find(ds.files.begin(), ds.files.end(), st.file) - ds.files.begin());
      if (layer >= (int)ds.files.size()) continue;
      ds.mats[ds.count] = stickerMatrix(st, ds.aspects[layer]);
      ds.info[ds.count] = vec4((float)layer, st.mirror ? 1.f : 0.f, 0, 0);
      ds.count++;
    }
  // carroceria
  for (size_t i = 0; i < car.paintMats.size(); i++) {
    Material& m = *car.paintMats[i];
    const LoadedCar::PaintOrig& o = car.paintOrig[i];
    m.map = o.map;
    m.color = o.color;
    m.metalness = o.metalness;
    m.roughness = o.roughness;
    m.clearcoat = o.clearcoat;
    m.clearcoatRoughness = o.clearcoatRoughness;
    m.decals = ds.count > 0 ? car.decals : nullptr;
    if (!l || !l->repaint) continue;
    if (l->keepArt) {
      m.color = l->paint * 1.6f;  // tinge a decoração original
    } else {
      m.map = {};
      m.color = l->paint;
    }
    finishMat(m, l->finish);
  }
  // aros
  for (size_t i = 0; i < car.rimMats.size(); i++) {
    Material& m = *car.rimMats[i];
    const LoadedCar::PaintOrig& o = car.rimOrig[i];
    m.map = o.map;
    m.color = o.color;
    m.metalness = o.metalness;
    m.roughness = o.roughness;
    m.clearcoat = o.clearcoat;
    m.clearcoatRoughness = o.clearcoatRoughness;
    if (!l || !l->rimCustom) continue;
    m.map = {};
    m.color = l->rim;
    finishMat(m, l->rimFinish);
  }
}
