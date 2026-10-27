#include "cinematic.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "garage.h"

static const float BARS_S = 2.6f;    // tempo das barras pretas entrando/saindo
static const float FLIGHT_S = 2.4f;  // voo da câmera até o menu, depois que as barras saem
static const float STEER_SHOW = 0.28f;  // rodas da frente viradas durante o filme

Cinematic::Cinematic(Garage& g) : garage(g) {}

void Cinematic::start(const CarEntry& entry, const std::shared_ptr<LoadedCar>& l) {
  Garage& g = garage;
  loaded = l;
  saved = {g.camera.position, g.controls.target, g.camera.fov, g.controls.autoRotate};
  g.controls.enabled = false;
  g.controls.autoRotate = false;
  // profundidade de campo só durante a cinemática
  g.fx.bokeh = true;
  g.fx.focus = 5;
  g.fx.aperture = 0.004f;
  g.fx.maxblur = 0.008f;
  // rodas da frente viradas para mostrar o aro
  for (const char* id : {"FL", "FR"}) {
    auto it = loaded->pivots.find(id);
    if (it != loaded->pivots.end()) it->second.steer->setRotationY(STEER_SHOW);
  }
  flying = false;
  shots = buildShots(*loaded);
  index = 0;
  t = 0;
  loops = 0;
  maker = entry.maker;
  name = entry.name;
  closing = false;
  bars.snap(1);  // a cena já abre com as faixas; elas só recolhem (devagar) ao sair
  hint.delay = 1.2f;
  hint.set(1);
  hintTimer = 0;
  beginShot();
  active = true;
  sinceStart = 0;
}

bool Cinematic::requestExit() {
  if (!active || closing || sinceStart < 0.4f) return false;
  closing = true;
  bars.set(0);
  title.set(0);
  hint.delay = 0;
  hint.set(0);
  closeTimer = BARS_S;
  return true;
}

// as barras saíram: a câmera parte de onde o filme parou e voa, em arco em volta do carro
// (coordenadas esféricas: não atravessa a carroceria), até o enquadramento do menu
void Cinematic::startFlight() {
  Garage& g = garage;
  flying = true;
  flightT = 0;
  Flight& f = flight;
  f.pos0 = g.camera.position;
  f.look0 = lastLook;
  f.look1 = saved.target;
  f.fov0 = g.camera.fov;
  f.fov1 = saved.fov;
  f.ap0 = g.fx.aperture;
  // destino: a mesma direção de antes do filme, na distância do menu (a entrada da garagem termina aqui)
  vec3 dir1 = normalize(saved.pos - saved.target);
  vec3 o0 = f.pos0 - f.look1, o1 = dir1 * g.baseDist;
  f.r0 = length(o0);
  f.r1 = length(o1);
  f.az0 = std::atan2(o0.x, o0.z);
  f.az1 = std::atan2(o1.x, o1.z);
  // gira pelo caminho mais curto
  while (f.az1 - f.az0 > (float)M_PI) f.az1 -= 2 * (float)M_PI;
  while (f.az1 - f.az0 < -(float)M_PI) f.az1 += 2 * (float)M_PI;
  f.el0 = std::asin(std::clamp(o0.y / std::max(f.r0, 1e-4f), -1.f, 1.f));
  f.el1 = std::asin(std::clamp(o1.y / std::max(f.r1, 1e-4f), -1.f, 1.f));
}

void Cinematic::updateFlight(float dt) {
  Garage& g = garage;
  flightT = std::min(1.f, flightT + dt / FLIGHT_S);
  float k = flightT;
  float e = k < 0.5f ? 4 * k * k * k : 1 - std::pow(-2 * k + 2, 3.f) / 2;  // easeInOutCubic
  const Flight& f = flight;
  // posição em órbita em volta do carro (em e = 0 é exatamente onde o filme parou)
  float az = f.az0 + (f.az1 - f.az0) * e, el = f.el0 + (f.el1 - f.el0) * e, r = f.r0 + (f.r1 - f.r0) * e;
  vec3 pos = f.look1 + vec3(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el)) * r;
  vec3 look = lerp(f.look0, f.look1, e);
  Camera& cam = g.camera;
  cam.position = pos;
  cam.up = vec3(0, 1, 0);
  cam.lookAt(look);
  cam.fov = f.fov0 + (f.fov1 - f.fov0) * e;
  // a profundidade de campo de cinema some aos poucos
  g.fx.focus = distance(pos, look);
  g.fx.aperture = f.ap0 * (1 - e);
  // rodas da frente voltam a ficar retas
  for (const char* id : {"FL", "FR"}) {
    auto it = loaded->pivots.find(id);
    if (it != loaded->pivots.end()) it->second.steer->setRotationY(STEER_SHOW * (1 - e));
  }
  if (k >= 1) finish();
}

void Cinematic::finish() {
  active = false;
  closing = false;
  Garage& g = garage;
  g.fx.bokeh = false;
  flying = false;
  // a câmera já está na pose do menu (o voo terminou nela)
  g.controls.target = saved.target;
  g.camera.fov = saved.fov;
  g.intro = 1;
  g.controls.enabled = true;
  g.controls.autoRotate = saved.autoRotate;
  for (const char* id : {"FL", "FR"}) {
    auto it = loaded->pivots.find(id);
    if (it != loaded->pivots.end()) it->second.steer->quaternion = quat();
  }
  bars.snap(0);
  if (onExit) onExit();
}

void Cinematic::beginShot() {
  const Shot& shot = shots[index];
  t = 0;
  // corte seco na maioria; "dip" rápido para preto em algumas viradas de bloco
  if (shot.dip) dipT = 0;
  title.set(shot.title ? 1.f : 0.f);
}

void Cinematic::update(float dt) {
  if (!active) return;
  sinceStart += dt;
  if (flying) {
    updateFlight(dt);
    return;
  }
  if (closing) {
    closeTimer -= dt;
    if (closeTimer <= 0) {
      startFlight();
      return;
    }
  }
  if (hintTimer > 0) {
    hintTimer -= dt;
    if (hintTimer <= 0) hint.set(0);
  }
  t += dt;
  if (t >= shots[index].dur) {
    index = (index + 1) % (int)shots.size();
    if (index == 0) loops++;
    beginShot();
  }
  const Shot& shot = shots[index];
  float k = std::min(1.f, t / shot.dur);  // velocidade constante: o plano é cortado em movimento
  Camera& cam = garage.camera;
  vec3 pos, look;
  if (shot.orbit) {
    const Orbit& o = *shot.orbit;
    float a = o.a0 + (o.a1 - o.a0) * k;
    float r = o.r0 + (o.r1 - o.r0) * k;
    pos = vec3(o.c.x + std::sin(a) * r, o.h0 + (o.h1 - o.h0) * k, o.c.z + std::cos(a) * r);
    look = o.c;
  } else {
    pos = lerp(shot.from, shot.to, k);
    look = lerp(shot.lookFrom, shot.lookTo ? *shot.lookTo : shot.lookFrom, k);
  }
  // micro-flutuação de câmera em estabilizador (deixa o movimento "orgânico")
  float tt = SDL_GetTicks() / 1000.f;
  pos.x += std::sin(tt * 0.9f) * 0.006f;
  pos.y += std::sin(tt * 1.3f + 1.7f) * 0.005f;
  cam.position = pos;
  cam.up = vec3(0, 1, 0);
  cam.lookAt(look);
  lastLook = look;
  cam.fov = shot.fov0 + ((shot.fov1 >= 0 ? shot.fov1 : shot.fov0) - shot.fov0) * k;
  // foco no assunto do plano
  vec3 focusOn = shot.orbit ? shot.orbit->c : look;
  garage.fx.focus = distance(cam.position, focusOn);
  garage.fx.aperture = shot.aperture ? *shot.aperture : 0.0025f;
  garage.fx.maxblur = 0.009f;
}

void Cinematic::drawOverlay(UI& ui, float dt) {
  bars.update(dt);
  title.update(dt);
  hint.update(dt);
  dipT += dt;
  if (!active && bars.value <= 0) return;
  float W = ui.width(), H = ui.height();
  float barH = std::max((H - W / 2.39f) / 2, H * 0.06f);
  float off = (1 - bars.value) * barH;
  ui.rect({0, -off, W, barH}, Color(0, 0, 0, 1));
  ui.rect({0, H - barH + off, W, barH}, Color(0, 0, 0, 1));

  // título: fabricante, modelo e o traço vermelho
  if (title.value > 0.001f) {
    ui.pushOpacity(title.value);
    bool narrow = W <= 900;
    TextStyle mk{Font::BC500, 14, 14 * 0.5f, Color::hex(0x5d6066)};
    float nameSize = narrow ? 40.f : 64.f;
    TextStyle nm{Font::BC600, nameSize, nameSize * 0.02f, Color::hex(0x111214)};
    float blockH = 14 + 6 + nameSize + 14 + 2;
    float bottom = H - (std::max(H * 0.06f, (H - W / 2.39f) / 2) + H * 0.05f);
    float y = bottom - blockH + (1 - title.value) * 10;
    float x = W * 0.07f;
    ui.text(upper(maker), x, y, mk, 14);
    ui.text(name, x, y + 14 + 6, nm, nameSize);
    ui.rect({x, y + 14 + 6 + nameSize + 14, 48, 2}, Color::hex(0xd4001a));
    ui.popOpacity();
  }
  // dip para preto (0,7 s, ease-out)
  if (dipT < 0.7f) {
    float a = 1 - EASE_OUT(dipT / 0.7f);
    ui.rect({0, 0, W, H}, Color(0, 0, 0, a));
  }
  // "Aperte [espaço] para continuar": lado direito, dentro da faixa preta de baixo
  if (hint.value > 0.001f) {
    pulse += dt;
    float k = hint.value * (0.8f + 0.2f * std::sin(pulse * 2.4f));
    const Color fg(1, 1, 1, 0.7f);
    TextStyle st{Font::BC500, 11, 11 * 0.3f, fg};
    std::string a = "APERTE", b = "PARA CONTINUAR";
    float keyH = 18, gap = 10;
    float keyW = promptWidth(ui, prompt, PadButton::Confirm, keyH, "space");
    float total = ui.measure(a, st) + gap + keyW + gap + ui.measure(b, st);
    // centro vertical da faixa (acompanha as barras entrando/saindo)
    float cy = H - barH + off + barH / 2;
    float x = W - W * 0.07f - total;
    ui.pushOpacity(k);
    x += ui.text(a, x, cy - 5.5f, st, 11) + gap;
    // ícone: barra de espaço no teclado, botão de confirmar no controle
    drawPrompt(ui, prompt, PadButton::Confirm, x, cy, keyH, "space", {true});
    x += keyW + gap;
    ui.text(b, x, cy - 5.5f, st, 11);
    ui.popOpacity();
  }
}

// Monta os planos a partir das medidas do carro (frente = −z, chão em y = 0).
std::vector<Cinematic::Shot> Cinematic::buildShots(LoadedCar& l) {
  l.root->updateMatrixWorld();
  Box3 box = worldBox(*l.root);
  vec3 size = box.size();
  float W = size.x, H = size.y;
  float zf = box.min.z, zr = box.max.z;  // frente e traseira
  auto wheel = [&](const char* id) {
    auto it = l.pivots.find(id);
    return it != l.pivots.end() ? it->second.base : vec3(0, 0.35f, 0);
  };
  vec3 FL = wheel("FL"), RR = wheel("RR");
  vec3 eye = l.cockpit ? l.cockpit->eye : vec3(-0.3f, 0.95f, 0);
  using V = vec3;
  std::vector<Shot> s;
  auto lin = [&](float dur, float fov0, V from, V to, V lookFrom, std::optional<V> lookTo, float ap, bool title = false, bool dip = false, float fov1 = -1) {
    Shot sh;
    sh.dur = dur; sh.fov0 = fov0; sh.fov1 = fov1; sh.from = from; sh.to = to; sh.lookFrom = lookFrom; sh.lookTo = lookTo;
    sh.aperture = ap; sh.title = title; sh.dip = dip;
    s.push_back(sh);
  };
  auto orb = [&](float dur, float fov0, Orbit o, bool title, bool dip) {
    Shot sh;
    sh.dur = dur; sh.fov0 = fov0; sh.orbit = o; sh.title = title; sh.dip = dip;
    s.push_back(sh);
  };
  // 1. ¾ frontal, altura da cintura, push-in lento (hero shot de abertura) + título
  lin(6.5f, 30, V(-W * 2.9f, 0.75f, zf - 4.6f), V(-W * 2.2f, 0.65f, zf - 3.4f), V(0, H * 0.42f, zf * 0.25f), V(0, H * 0.4f, zf * 0.15f), 0.0012f, true, true, 28);
  // 2. close na roda dianteira (aro + pinça), arco curto, foco raso
  lin(5, 26, V(FL.x - 1.25f, FL.y + 0.05f, FL.z - 0.95f), V(FL.x - 1.35f, FL.y + 0.12f, FL.z + 0.55f), FL, std::nullopt, 0.006f);
  // 3. detalhe do farol: slider lateral rente à frente
  lin(4.5f, 24, V(-W * 0.75f, H * 0.52f, zf - 1.15f), V(-W * 0.12f, H * 0.56f, zf - 1.3f), V(-W * 0.34f, H * 0.48f, zf + 0.3f), std::nullopt, 0.005f);
  // 4. lateral rente ao chão percorrendo o carro inteiro (slider baixo)
  lin(6, 34, V(-W / 2 - 1.5f, 0.22f, zf - 0.4f), V(-W / 2 - 1.5f, 0.26f, zr + 0.4f), V(-W / 2, 0.45f, zf + 0.6f), V(-W / 2, 0.45f, zr - 0.6f), 0.002f);
  // 5. ¾ traseira com grua subindo (revela a asa)
  lin(6, 30, V(W * 2.1f, 0.25f, zr + 3.1f), V(W * 2.3f, 1.85f, zr + 3.4f), V(0, H * 0.45f, zr * 0.35f), V(0, H * 0.5f, zr * 0.3f), 0.0012f, false, true);
  // 6. asa e difusor: travelling por trás
  lin(5, 27, V(-1.7f, H * 0.95f, zr + 1.7f), V(1.7f, H * 0.85f, zr + 1.55f), V(0, H * 0.8f, zr - 0.25f), std::nullopt, 0.004f);
  // 7. escapamento / difusor de baixo
  lin(4, 28, V(0.9f, 0.12f, zr + 1.6f), V(0.4f, 0.18f, zr + 1.05f), V(0, 0.3f, zr - 0.1f), std::nullopt, 0.006f);
  // 8. roda traseira de baixo, girando em volta
  lin(4.5f, 26, V(RR.x + 1.15f, 0.18f, RR.z + 1.0f), V(RR.x + 1.3f, 0.3f, RR.z - 0.55f), RR, std::nullopt, 0.006f);
  // 9. plano alto girando devagar (grua alta, ~50° de cima)
  orb(6, 36, Orbit{V(0, 0.3f, 0), 0.35f, 1.15f, 6.2f, 5.8f, 4.6f, 4.1f}, false, true);
  // 10. espiando o cockpit pela janela do piloto
  lin(4.5f, 30, V(eye.x - 1.25f, eye.y + 0.08f, eye.z + 0.7f), V(eye.x - 1.3f, eye.y + 0.12f, eye.z - 0.35f), V(eye.x + 0.2f, eye.y - 0.15f, eye.z - 0.2f), std::nullopt, 0.004f);
  // 11. perfil direto com lente longa (compressão), truck lento
  lin(5.5f, 17, V(W / 2 + 10.5f, 0.7f, -1.4f), V(W / 2 + 10.5f, 0.7f, 1.4f), V(0, H * 0.42f, -1.2f), V(0, H * 0.42f, 1.2f), 0.0008f);
  // 12. frente reta, câmera baixa recuando (revelação)
  lin(5.5f, 30, V(0, 0.32f, zf - 1.5f), V(0, 0.5f, zf - 4.6f), V(0, H * 0.4f, zf + 0.2f), V(0, H * 0.42f, zf * 0.4f), 0.002f);
  // 13. ⅞ traseira em órbita larga e alta, título de novo (fechamento)
  orb(7.5f, 32, Orbit{V(0, H * 0.35f, 0), 2.35f, 1.55f, 7.6f, 6.8f, 1.9f, 1.2f}, true, true);
  return s;
}
