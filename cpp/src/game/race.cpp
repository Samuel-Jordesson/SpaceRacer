// Corrida: contra o tempo e contra a IA. O traçado da pista (TrackMap::line) dá o progresso de
// cada carro, as voltas e a posição. Os adversários usam outros carros do catálogo, pilotados
// pela mesma física: a IA calcula volante, acelerador e freio a cada quadro, enxergando os
// carros em volta (inclusive o jogador), o limite de aderência das curvas e as derrapagens.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "game.h"

using phys::V3;

namespace {
const char* BOT_NAMES[] = {"R. Alves", "M. Bianchi", "L. Vogt", "T. Hayes", "K. Sato", "L. Moreau", "A. Ruiz"};
const vec3 BOT_COLORS[] = {{0.6f, 0.02f, 0.02f}, {0.02f, 0.12f, 0.55f}, {0.02f, 0.3f, 0.1f}, {0.95f, 0.7f, 0.02f},
                           {0.02f, 0.02f, 0.025f}, {0.9f, 0.3f, 0.02f}, {0.3f, 0.05f, 0.5f}};
// cor do ponto de cada adversário no minimapa
const Color MAP_COLORS[] = {Color::hex(0xff5a4e), Color::hex(0x4ea0ff), Color::hex(0x5fd37a), Color::hex(0xffd23f),
                            Color::hex(0xc48bff), Color::hex(0xff9d3c), Color::hex(0x6fe3e3)};
const Color WHITE_C(1, 1, 1, 1);
vec3 toV(const V3& v) { return vec3((float)v.x, (float)v.y, (float)v.z); }
std::string lapStr(double t) {
  if (t <= 0) return "--:--.---";
  int m = (int)(t / 60);
  double s = t - m * 60;
  char b[32];
  std::snprintf(b, sizeof b, "%02d:%06.3f", m, s);
  return b;
}
}  // namespace

bool Game::raceActive() const { return phase != Phase::Free; }

// coloca um carro no traçado a uma distância s, deslocado lat metros para a direita
void Game::placeOnLine(phys::Vehicle& v, float s, float lat) {
  TrackMap* t = world->currentTrack();
  vec3 d = t->lineDir(s), right(-d.z, 0, d.x);
  vec3 p = t->lineAt(s) + right * lat;
  phys::GroundQuery h = ground->heightNear(p.x, p.z, p.y + 3);
  v.reset(V3(p.x, h.h > -1e8 ? h.h : p.y, p.z), std::atan2(-d.x, -d.z));
}

// velocidade máxima em cada ponto do traçado para um carro: aderência do pneu + pressão
// aerodinâmica na curva, e depois a frenagem para chegar na próxima curva a tempo
static std::vector<float> speedPlan(const TrackMap& t, const phys::CarSpec& sp) {
  size_t n = t.line.size();
  float mu = (float)sp.tire.mu * 0.8f, g = 9.81f;
  float aero = mu * 0.5f * (float)(sp.aero.rho * sp.aero.ClA / sp.mass);
  std::vector<float> curv(n, 0), plan(n, 90);
  for (size_t i = 0; i < n; i++) {
    vec3 a = t.line[(i + n - 3) % n], b = t.line[i], c = t.line[(i + 3) % n];
    vec3 d1 = b - a, d2 = c - b;
    d1.y = d2.y = 0;
    float ang = std::acos(std::clamp(dot(normalize(d1), normalize(d2)), -1.f, 1.f));
    curv[i] = ang / std::max(1.f, (length(d1) + length(d2)) / 2);
  }
  for (size_t i = 0; i < n; i++) {
    float k = (curv[(i + n - 1) % n] + curv[i] + curv[(i + 1) % n]) / 3;
    plan[i] = k > aero + 1e-4f ? std::min(90.f, std::sqrt(mu * g / (k - aero))) : 90.f;
  }
  float aBrake = mu * g * 0.8f;
  for (int pass = 0; pass < 3; pass++)
    for (size_t k = 0; k < n; k++) {
      size_t i = n - 1 - k, j = (i + 1) % n;
      float ds = distance(t.line[i], t.line[j]);
      plan[i] = std::min(plan[i], std::sqrt(plan[j] * plan[j] + 2 * aBrake * ds));
    }
  return plan;
}

void Game::raceStart() {
  raceStop();
  phase = Phase::Free;
  TrackMap* t = world->currentTrack();
  if (race.mode == 0 || !t || !t->hasLine()) return;
  int nBots = race.mode == 2 ? (int)oppPlan.size() : 0;
  // grid: pole na frente; o jogador larga em último (na corrida contra o tempo, sozinho)
  racers.clear();
  auto gridS = [&](int slot) { return t->lineLen - 12.f - slot * 9.f; };
  auto gridLat = [](int slot) { return slot % 2 == 0 ? -2.6f : 2.6f; };
  std::map<std::string, int> used;
  for (int i = 0; i < nBots; i++) {
    Bot b;
    b.entry = oppPlan[i];
    auto it = oppModels.find(b.entry->id);
    if (it == oppModels.end()) continue;
    b.model = cloneCar(*it->second);
    // o mesmo modelo repetido ganha outra cor
    if (used[b.entry->id]++ > 0)
      for (auto& m : b.model->paintMats) {
        m->map = {};
        m->color = BOT_COLORS[i % 7];
        m->metalness = 0;
        m->roughness = 0.32f;
        m->clearcoat = 1;
        m->clearcoatRoughness = 0.03f;
        m->physical = true;
      }
    const phys::CarSpec& sp = *b.entry->spec;
    b.car = std::make_unique<phys::Vehicle>(sp, ground, &b.model->geometry);
    b.obj = makeNode("bot");
    b.visual = makeNode("bot-visual");
    b.obj->add(b.visual);
    b.visual->position = vec3(0, (float)-sp.cgHeight, (float)(b.model->geometry.wheelbase / 2 - b.car->a));
    b.visual->add(b.model->root);
    world->scene.root->add(b.obj);
    static const float SKILL[7] = {0.97f, 0.93f, 0.95f, 0.9f, 0.92f, 0.88f, 0.91f};
    b.skill = SKILL[i % 7];
    b.plan = speedPlan(*t, sp);
    b.lane = gridLat(i);
    placeOnLine(*b.car, gridS(i), gridLat(i));
    bots.push_back(std::move(b));
  }
  nBots = (int)bots.size();
  int playerSlot = nBots;
  float ps = nBots ? gridS(playerSlot) : gridS(0);
  placeOnLine(*car, ps, nBots ? gridLat(playerSlot) : 0.f);
  rig.yaw = (float)std::atan2(-t->lineDir(ps).x, -t->lineDir(ps).z);
  racers.push_back({"Você", car.get()});
  for (int i = 0; i < nBots; i++) racers.push_back({BOT_NAMES[i % 7], bots[i].car.get()});
  for (auto& r : racers) {
    r.seg = t->nearestOnLine(toV(r.car->pos), -1, &r.s);
    r.laps = -1;  // atrás da linha: a primeira passagem começa a volta 1
  }
  phase = Phase::Countdown;
  countdown = 5.5f;
  raceClock = 0;
  resultsOpen = false;
}

void Game::raceStop() {
  for (auto& b : bots) world->scene.root->remove(b.obj.get());
  bots.clear();
  racers.clear();
  phase = Phase::Free;
}

// Piloto da IA.
//  1. vê os carros em volta (inclusive o jogador): posição ao longo da pista e de lado;
//  2. com alguém lento à frente na mesma faixa: ultrapassa pelo lado livre se houver espaço,
//     senão acompanha com distância de segurança (nunca "empurra" o da frente);
//  3. carro lado a lado: abre espaço e não fecha a porta;
//  4. velocidade de cada trecho pela aderência (plano do traçado) e frenagem antecipada;
//  5. derrapagem: contraesterça e alivia o acelerador até o carro voltar a apontar para onde vai.
void Game::botDrive(Bot& b, Racer& r) {
  TrackMap* t = world->currentTrack();
  phys::Vehicle& v = *b.car;
  float speed = (float)v.forwardSpeed(), L = t->lineLen;
  const phys::Mat3 R = v.quat.toMat3();
  vec3 fwd = toV(R.col(2)) * -1.f, right = toV(R.col(0)), pos = toV(v.pos), vel = toV(v.vel);
  int n = (int)t->line.size();
  float halfW = t->lineHalfW.empty() ? 5 : t->lineHalfW[std::clamp(r.seg, 0, n - 1)];
  float laneMax = std::max(0.f, halfW - 2.0f);
  // lateral (metros à direita do traçado) de qualquer carro
  auto latOf = [&](const Racer& o) {
    vec3 d = t->lineDir(o.s), lr(-d.z, 0, d.x);
    vec3 off = toV(o.car->pos) - t->lineAt(o.s);
    return dot(off, lr);
  };
  float myLat = latOf(r);
  // --- 1/2/3: tráfego
  float desired = b.lane, follow = 1e9f;
  float safeGap = 7 + std::max(0.f, speed) * 0.25f;
  bool sideBlockL = false, sideBlockR = false;
  for (auto& o : racers) {
    if (o.car == &v) continue;
    float ds = o.s - r.s;
    if (ds < -L / 2) ds += L;
    if (ds > L / 2) ds -= L;
    float olat = latOf(o), dl = olat - myLat;
    float ov = (float)o.car->forwardSpeed();
    // lado a lado: marca o lado ocupado e mantém pelo menos 2,6 m de distância lateral
    if (std::fabs(ds) < 6) {
      if (dl > 0 && dl < 3.2f) sideBlockR = true;
      if (dl < 0 && dl > -3.2f) sideBlockL = true;
      if (std::fabs(dl) < 2.6f) desired = myLat - (dl > 0 ? 1.f : -1.f) * (2.6f - std::fabs(dl)) * 1.2f;
    }
    // à frente na mesma faixa
    if (ds > 0.5f && ds < 45 && std::fabs(dl) < 2.4f) {
      float closing = speed - ov;
      bool wantPass = closing > 1.5f || b.plan[((r.seg + 10) % n + n) % n] * b.skill > ov + 3;
      if (wantPass && halfW > 4.8f && ds < 30) {
        // escolhe o lado com mais pista e sem ninguém ao lado
        float spaceL = olat + laneMax, spaceR = laneMax - olat;
        bool goRight = spaceR > spaceL;
        if (goRight && sideBlockR) goRight = false;
        if (!goRight && sideBlockL) goRight = true;
        float target = olat + (goRight ? 3.0f : -3.0f);
        if (std::fabs(target) <= laneMax + 0.3f && !(goRight ? sideBlockR : sideBlockL)) desired = target;
      }
      // sem espaço para passar (ou ainda longe): mantém distância segura
      if (std::fabs(desired - olat) < 2.4f) follow = std::min(follow, ov + (ds - safeGap) * 0.7f);
    }
  }
  b.lane += std::clamp(desired - b.lane, -0.06f, 0.06f);
  // sem tráfego, volta devagar para o traçado
  if (std::fabs(desired - b.lane) < 0.05f) b.lane *= 0.995f;
  b.lane = std::clamp(b.lane, -laneMax, laneMax);
  // --- 4: direção (pure pursuit) para a faixa escolhida
  float ld = std::max(8.f, speed * 0.5f + 5);
  vec3 dir = t->lineDir(r.s + ld), lr(-dir.z, 0, dir.x);
  vec3 target = t->lineAt(r.s + ld) + lr * b.lane;
  vec3 to = target - pos;
  float alpha = std::atan2(dot(to, right), std::max(0.1f, dot(to, fwd)));
  float wb = (float)v.geometry.wheelbase;
  float delta = std::atan2(2 * wb * std::sin(alpha), ld);
  float lock = (float)(v.spec.steering.maxAngleDeg * M_PI / 180);
  float steer = delta / lock;
  // --- 5: derrapagem (ângulo entre para onde o carro aponta e para onde ele vai)
  float vF = dot(vel, fwd), vL = dot(vel, right);
  float beta = std::fabs(vF) > 3 ? std::atan2(vL, std::fabs(vF)) : 0.f;
  float slide = std::max(0.f, std::fabs(beta) - 0.07f);
  steer += beta * 1.1f / std::max(0.2f, lock);  // contraesterço: aponta as rodas para onde o carro desliza
  b.ctl.steer = std::clamp(steer, -1.f, 1.f);
  // --- velocidade: plano um pouco à frente (tempo de reação/frenagem), tráfego e derrapagem
  int ahead = r.seg + std::max(1, (int)(speed * 0.3f / 4));
  float vt = std::min(b.plan[((ahead % n) + n) % n] * b.skill, std::max(0.f, follow));
  if (phase != Phase::Running) vt = 0;
  float err = vt - speed;
  float thr = err > -0.5f ? std::clamp(err * 0.35f + 0.25f, 0.f, 1.f) : 0.f;
  thr *= std::clamp(1 - slide * 6, 0.15f, 1.f);
  b.ctl.throttle = thr;
  b.ctl.brake = err < -1 ? std::clamp(-err * 0.15f, 0.f, 1.f) : 0.f;
  if (slide > 0.15f) b.ctl.brake *= 0.4f;  // freio forte no meio da rodada piora tudo
  b.ctl.handbrake = phase == Phase::Countdown ? 1 : 0;
  if (phase == Phase::Countdown) b.ctl.brake = 1;
}

void Game::raceUpdate(float dt) {
  if (phase == Phase::Free) return;
  if (phase == Phase::Countdown) {
    countdown -= dt;
    if (countdown <= 0) {
      phase = Phase::Running;
      raceClock = 0;
      for (auto& r : racers) r.lapStart = 0;
    }
  } else {
    raceClock += dt;
  }
  for (size_t i = 0; i < bots.size(); i++) botDrive(bots[i], racers[i + 1]);
}

// depois da física: colisões, progresso no traçado, voltas, carros presos e fim da corrida
void Game::raceAfterPhysics(float dt) {
  if (phase == Phase::Free) return;
  TrackMap* t = world->currentTrack();
  collideCars();
  float L = t->lineLen;
  for (size_t i = 0; i < racers.size(); i++) {
    Racer& r = racers[i];
    float prev = r.s;
    r.seg = t->nearestOnLine(toV(r.car->pos), r.seg, &r.s);
    if (prev > 0.75f * L && r.s < 0.25f * L) {
      r.laps++;
      if (phase == Phase::Running) {
        if (r.laps >= 1) {
          r.lastLap = raceClock - r.lapStart;
          if (r.bestLap <= 0 || r.lastLap < r.bestLap) r.bestLap = r.lastLap;
          if (i == 0) hud->flash("Volta " + std::to_string(r.laps) + ": " + lapStr(r.lastLap), 2.5f);
        }
        r.lapStart = raceClock;
        if (r.laps >= race.laps && r.finish < 0) r.finish = raceClock;
      }
    } else if (prev < 0.25f * L && r.s > 0.75f * L) {
      r.laps--;
    }
    // adversário preso ou fora da pista: volta para o traçado num ponto livre
    if (i > 0 && phase == Phase::Running) {
      vec3 off = toV(r.car->pos) - t->lineAt(r.s);
      off.y = 0;
      bool bad = r.car->speed() < 1 || length(off) > 25;
      r.stuck = bad ? r.stuck + dt : 0;
      if (r.stuck > 4) {
        float sp = r.s;
        for (int tries = 0; tries < 12; tries++) {
          bool clear = true;
          vec3 cand = t->lineAt(sp);
          for (size_t j = 0; j < racers.size(); j++)
            if (j != i && distance(toV(racers[j].car->pos), cand) < 9) clear = false;
          if (clear) break;
          sp -= 10;
        }
        placeOnLine(*r.car, sp, 0);
        r.seg = t->nearestOnLine(t->lineAt(sp), -1, &r.s);
        bots[i - 1].lane = 0;
        r.stuck = 0;
      }
    }
  }
  if (std::getenv("SR_RACELOG")) {
    static double next = 0;
    if (raceClock >= next) {
      next = raceClock + 1;
      std::printf("[corrida] t=%.0f", raceClock);
      for (auto& r : racers) std::printf(" | %s v=%.0f s=%.0f volta=%d", r.name.c_str(), r.car->speed() * 3.6, r.s, r.laps);
      std::printf("\n");
    }
  }
  if (phase == Phase::Running && racers[0].finish >= 0) {
    phase = Phase::Finished;
    resultsOpen = true;
  }
}

// carros como dois círculos (eixos dianteiro e traseiro): separa e troca a velocidade na normal
void Game::collideCars() {
  std::vector<phys::Vehicle*> cars;
  for (auto& r : racers) cars.push_back(r.car);
  const double R = 1.05;
  auto circles = [](phys::Vehicle& v, V3 out[2]) {
    V3 f = v.quat.toMat3().col(2) * -1.0;
    out[0] = v.pos + f * 1.2;
    out[1] = v.pos - f * 1.2;
  };
  for (size_t i = 0; i < cars.size(); i++)
    for (size_t j = i + 1; j < cars.size(); j++) {
      phys::Vehicle &a = *cars[i], &b = *cars[j];
      if ((a.pos - b.pos).length() > 7) continue;
      V3 ca[2], cb[2];
      circles(a, ca);
      circles(b, cb);
      for (auto& p : ca)
        for (auto& q : cb) {
          V3 d = p - q;
          d.y = 0;
          double dl = d.length();
          if (dl >= 2 * R || dl < 1e-6) continue;
          V3 nrm = d * (1 / dl);
          double pen = 2 * R - dl;
          a.pos = a.pos + nrm * (pen / 2);
          b.pos = b.pos - nrm * (pen / 2);
          V3 rel = a.vel - b.vel;
          double vn = rel.dot(nrm);
          if (vn < 0) {
            double jimp = -(1 + 0.3) * vn / 2;
            a.vel = a.vel + nrm * jimp;
            b.vel = b.vel - nrm * jimp;
          }
        }
    }
}

void Game::syncBots() {
  float ahead = paused ? 0.f : (float)acc;
  for (auto& b : bots) {
    phys::Vehicle& v = *b.car;
    b.obj->position = vec3((float)(v.pos.x + v.vel.x * ahead), (float)(v.pos.y + v.vel.y * ahead), (float)(v.pos.z + v.vel.z * ahead));
    b.obj->quaternion = quat((float)v.quat.x, (float)v.quat.y, (float)v.quat.z, (float)v.quat.w);
    for (const auto& w : v.wheels) {
      auto it = b.model->pivots.find(w.id);
      if (it == b.model->pivots.end()) continue;
      WheelPivot& pv = it->second;
      pv.steer->position.y = pv.base.y - (float)(w.x0 - w.compression);
      pv.steer->setRotationY((float)w.steer);
      pv.spin->setRotationX((float)-w.spin);
      float blur = std::min(0.85f, std::max(0.f, (float)(std::fabs(w.omega) - 25) / 45));
      for (auto& m : pv.blur) m->opacity = blur;
    }
    float brake = b.ctl.brake > 0.05f ? 1.f : 0.f;
    for (auto& m : b.model->tailLights) m->emissiveIntensity = 0.8f + brake * 14;
  }
}

// HUD da corrida (estilo limpo, texto branco sem painéis): posição, volta e tempos, minimapa,
// semáforo e o resultado no fim
void Game::drawRace(UI& ui) {
  TrackMap* t = world ? world->currentTrack() : nullptr;
  float W = ui.width(), H = ui.height();
  auto shadowText = [&](const std::string& s, float x, float y, TextStyle st) {
    TextStyle sh = st;
    sh.color = Color(0, 0, 0, 0.35f * st.color.a);
    ui.text(s, x + 1, y + 1.5f, sh);
    return ui.text(s, x, y, st);
  };

  // --- minimapa (canto inferior esquerdo): traçado, adversários e você
  if (t && t->hasLine()) {
    float R = W <= 700 ? 70.f : 100.f, cx = 30 + R, cy = H - 34 - R;
    ui.circle(cx, cy, R, Color(0.02f, 0.025f, 0.03f, 0.28f));
    ui.ring(cx, cy, R, 1.5f, Color(1, 1, 1, 0.35f));
    float mnx = 1e30f, mxx = -1e30f, mnz = 1e30f, mxz = -1e30f;
    for (auto& p : t->line) { mnx = std::min(mnx, p.x); mxx = std::max(mxx, p.x); mnz = std::min(mnz, p.z); mxz = std::max(mxz, p.z); }
    float sc = R * 1.55f / std::max(mxx - mnx, mxz - mnz), mx = (mnx + mxx) / 2, mz = (mnz + mxz) / 2;
    auto mp = [&](const vec3& p) { return vec2(cx + (p.x - mx) * sc, cy + (p.z - mz) * sc); };
    size_t n = t->line.size();
    for (int pass = 0; pass < 2; pass++)
      for (size_t i = 0; i < n; i += 2) {
        vec2 a = mp(t->line[i]), b = mp(t->line[(i + 2) % n]);
        if (pass == 0) ui.line(a.x, a.y, b.x, b.y, 5, Color(0, 0, 0, 0.3f));
        else ui.line(a.x, a.y, b.x, b.y, 2.6f, Color(1, 1, 1, 0.85f));
      }
    // linha de largada
    {
      vec2 a = mp(t->line[0]);
      vec3 d = t->lineDir(0);
      ui.line(a.x - d.z * 6, a.y + d.x * 6, a.x + d.z * 6, a.y - d.x * 6, 2, Color::hex(0xe8102a));
    }
    for (size_t i = 0; i < bots.size(); i++) {
      vec2 p = mp(toV(bots[i].car->pos));
      ui.circle(p.x, p.y, 4.5f, Color(0, 0, 0, 0.5f));
      ui.circle(p.x, p.y, 3.5f, MAP_COLORS[i % 7]);
    }
    // você: seta branca apontando para onde o carro vai
    {
      vec2 p = mp(toV(car->pos));
      vec3 f = toV(car->quat.toMat3().col(2)) * -1.f;
      float fl = std::max(1e-4f, std::sqrt(f.x * f.x + f.z * f.z));
      vec2 d(f.x / fl, f.z / fl), q(-d.y, d.x);
      vec2 tip = p + d * 8.f, l = p - d * 5.f + q * 5.f, rr = p - d * 5.f - q * 5.f;
      ui.triangle({tip.x + 1, tip.y + 1}, {l.x + 1, l.y + 1}, {rr.x + 1, rr.y + 1}, Color(0, 0, 0, 0.5f));
      ui.triangle({tip.x, tip.y}, {l.x, l.y}, {rr.x, rr.y}, WHITE_C);
    }
  }
  if (phase == Phase::Free || racers.empty()) return;
  float L = t->lineLen;
  const Racer& me = racers[0];
  // ordem da corrida: quem terminou primeiro, depois quem andou mais
  std::vector<int> order(racers.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    const Racer &A = racers[a], &B = racers[b];
    if ((A.finish >= 0) != (B.finish >= 0)) return A.finish >= 0;
    if (A.finish >= 0) return A.finish < B.finish;
    return A.laps * L + A.s > B.laps * L + B.s;
  });

  // --- posição (canto superior esquerdo): número grande | POSIÇÃO / total
  if (race.mode == 2) {
    int pos = (int)(std::find(order.begin(), order.end(), 0) - order.begin()) + 1;
    TextStyle big{Font::Barlow300, 64, 0, WHITE_C, true};
    float x = 56, y = 26;
    float w = shadowText(std::to_string(pos), x, y, big);
    ui.rect({x + w + 16, y + 14, 1, 52}, Color(1, 1, 1, 0.5f));
    TextStyle lb{Font::Barlow500, 13, 1.2f, Color(1, 1, 1, 0.9f)};
    TextStyle tot{Font::Barlow400, 20, 0.5f, Color(1, 1, 1, 0.9f), true};
    shadowText("POSIÇÃO", x + w + 32, y + 16, lb);
    shadowText("/ " + std::to_string(racers.size()), x + w + 32, y + 38, tot);
    ui.rect({x - 4, y + 82, w + 140, 1}, Color(1, 1, 1, 0.25f));
  }

  // --- volta e tempos (canto superior direito, alinhado à direita)
  {
    int lap = std::clamp(me.laps + 1, 1, race.laps);
    float rx = W - 64, y = 30;
    TextStyle lb{Font::Barlow500, 13, 1.2f, Color(1, 1, 1, 0.9f)};
    TextStyle lp{Font::Barlow300, 36, 0.3f, WHITE_C, true};
    TextStyle lt{Font::Barlow400, 20, 0.3f, Color(1, 1, 1, 0.75f), true};
    shadowText("VOLTA", rx - ui.measure("VOLTA", lb), y, lb);
    std::string tot = " / " + std::to_string(race.laps);
    float tw = ui.measure(tot, lt);
    shadowText(tot, rx - tw, y + 30, lt);
    std::string ls = std::to_string(lap);
    shadowText(ls, rx - tw - ui.measure(ls, lp), y + 18, lp);
    ui.rect({rx - 96, y + 66, 96, 1}, Color(1, 1, 1, 0.3f));
    double cur = phase == Phase::Running ? raceClock - me.lapStart : phase == Phase::Finished ? me.lastLap : 0;
    TextStyle tm{Font::Barlow400, 20, 0.6f, WHITE_C, true};
    std::string cs = lapStr(cur);
    shadowText(cs, rx - ui.measure(cs, tm), y + 76, tm);
    TextStyle bl{Font::Barlow500, 10.5f, 1, Color(1, 1, 1, 0.65f)};
    TextStyle bv{Font::Barlow400, 15, 0.4f, Color(1, 1, 1, 0.9f), true};
    std::string bs = lapStr(me.bestLap);
    float bw = ui.measure(bs, bv);
    shadowText(bs, rx - bw, y + 110, bv);
    shadowText("MELHOR", rx - bw - 30 - ui.measure("MELHOR", bl), y + 113, bl);
  }

  // --- semáforo: 5 luzes acendem uma por segundo e apagam juntas na largada
  if (phase == Phase::Countdown || (phase == Phase::Running && raceClock < 1.2)) {
    float r = 18, gap = 14, total = 5 * r * 2 + 4 * gap, x0 = (W - total) / 2, y = 150;
    ui.rect({x0 - 18, y - r - 14, total + 36, r * 2 + 28}, Color(0.03f, 0.03f, 0.035f, 0.7f), 10);
    int lit = phase == Phase::Countdown ? std::clamp((int)std::floor(5.5f - countdown + 0.0001f), 0, 5) : 0;
    for (int i = 0; i < 5; i++) {
      float cxl = x0 + r + i * (r * 2 + gap);
      ui.circle(cxl, y, r, i < lit ? Color::hex(0xe8102a) : Color(1, 1, 1, 0.12f));
    }
    if (phase == Phase::Running) {
      TextStyle go{Font::Barlow600, 46, 2, Color::hex(0x2ecc55)};
      std::string g = "VAI!";
      shadowText(g, (W - ui.measure(g, go)) / 2, y + 40, go);
    }
  }

  // --- resultado
  if (phase == Phase::Finished && resultsOpen) {
    float rw = 560, rh = 120 + order.size() * 36, rx = (W - rw) / 2, ry = (H - rh) / 2;
    ui.rect({rx, ry, rw, rh}, Color(0.04f, 0.045f, 0.05f, 0.9f), 6);
    TextStyle ttl{Font::Barlow500, 24, 3, WHITE_C};
    ui.text(race.mode == 2 ? "RESULTADO" : "FIM DA SESSÃO", rx + 28, ry + 24, ttl);
    TextStyle row{Font::Barlow400, 16, 0.4f, WHITE_C, true};
    float yy = ry + 74;
    for (size_t k = 0; k < order.size(); k++) {
      const Racer& Rr = racers[order[k]];
      row.color = order[k] == 0 ? Color::hex(0xffd23f) : WHITE_C;
      ui.text(std::to_string(k + 1) + ".", rx + 28, yy, row);
      std::string nm = Rr.name;
      if (order[k] > 0 && order[k] - 1 < (int)bots.size() && bots[order[k] - 1].entry) nm += "  (" + bots[order[k] - 1].entry->name + ")";
      ui.text(nm, rx + 66, yy, row);
      std::string tm = Rr.finish >= 0 ? lapStr(Rr.finish) : "em corrida";
      ui.text(tm, rx + rw - 28 - ui.measure(tm, row), yy, row);
      yy += 36;
    }
    TextStyle hint{Font::Barlow400, 13, 0.3f, Color(1, 1, 1, 0.6f)};
    std::string h = "Enter: voltar à garagem   ·   Espaço: continuar dirigindo";
    ui.text(h, rx + (rw - ui.measure(h, hint)) / 2, ry + rh - 34, hint);
  }
}
