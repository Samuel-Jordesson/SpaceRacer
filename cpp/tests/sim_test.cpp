// Testes da física sem janela (port de test/sim-test.js).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "physics/carspec.h"
#include "physics/vehicle.h"

using namespace phys;

static const double DT = 1.0 / 1000;
static double kmh(double v) { return v * 3.6; }
static int failures = 0;
static void check(const char* name, bool ok, const std::string& info) {
  std::printf("%s %s: %s\n", ok ? "OK  " : "FAIL", name, info.c_str());
  if (!ok) failures++;
}
static bool finite(const Vehicle& c) {
  for (double v : {c.pos.x, c.pos.y, c.pos.z, c.vel.x, c.vel.y, c.vel.z, c.engine.omega})
    if (!std::isfinite(v)) return false;
  return true;
}
template <class... A>
static std::string fmt(const char* f, A... a) {
  char b[512];
  std::snprintf(b, sizeof b, f, a...);
  return b;
}

struct Range { double a, b; };
struct Expect { Range t100, t200, vmax, brake, lat; };
static bool inRange(double v, Range r, bool valid = true) { return valid && v > r.a && v < r.b; }

int main() {
  FlatGround ground;
  // faixas esperadas de cada carro (números públicos / típicos de GT3)
  const std::pair<const CarSpec*, Expect> suites[] = {
      {&FERRARI_296_GT3, {{2.6, 4.2}, {7, 12}, {265, 310}, {75, 150}, {1.4, 2.6}}},
      {&PORSCHE_992_GT3_R, {{2.8, 4.3}, {7.5, 12.5}, {270, 300}, {75, 150}, {1.4, 2.6}}},
      // oficial: 0–100 em 2,4 s, 0–200 em 6,7 s, > 350 km/h; pneu de rua = menos g lateral
      {&LAMBORGHINI_FENOMENO, {{2.1, 2.9}, {5.9, 7.6}, {345, 365}, {90, 175}, {1.05, 1.7}}},
      // GT3 de motor dianteiro: ~280–290 km/h de máxima em Le Mans/Monza
      {&BMW_M4_GT3_EVO, {{2.8, 4.2}, {7, 12}, {270, 300}, {75, 150}, {1.4, 2.6}}},
      // oficial: 0–100 em ~5,4 s, 275 km/h; carro de rua com pneu de rua e tração dianteira
      {&HONDA_CIVIC_TYPE_R, {{4.8, 6.3}, {14, 24}, {260, 290}, {110, 200}, {0.9, 1.35}}},
  };

  for (const auto& [spec, expect] : suites) {
    auto make = [&] { return Vehicle(*spec, &ground); };
    std::printf("\n=== %s ===\n", spec->name.c_str());

    // 1) Repouso: assentar e não derivar
    {
      Vehicle car = make();
      Controls inp;
      for (int i = 0; i < 4000; i++) car.step(DT, inp);
      double spd = car.speed();
      double drift = std::hypot(car.pos.x, car.pos.z);
      check("repouso", spd < 0.01 && drift < 0.02 && finite(car),
            fmt("vel=%.2e m/s, deriva=%.4f m, CG y=%.3f, rpm=%.0f", spd, drift, car.pos.y, car.engine.rpm));
      double sum = 0;
      for (auto& w : car.wheels) sum += w.fz;
      check("cargas estáticas", std::fabs(sum - car.mass * 9.81) < 50,
            fmt("Fz FL/FR/RL/RR = %.0f/%.0f/%.0f/%.0f N", car.wheels[0].fz, car.wheels[1].fz, car.wheels[2].fz, car.wheels[3].fz));
    }

    // 2) Aceleração (câmbio automático, TC ligado)
    {
      Vehicle car = make();
      Controls inp{1, 0, 0, 0};
      double t100 = -1, t200 = -1, t = 0, maxV = 0;
      std::string shifts;
      int lastGear = 0;
      for (; t < 70; t += DT) {
        car.step(DT, inp);
        double v = kmh(car.forwardSpeed());
        if (car.gear != lastGear) { shifts += fmt(shifts.empty() ? "%d@%.0f" : " %d@%.0f", car.gear, v); lastGear = car.gear; }
        if (t100 < 0 && v >= 100) t100 = t;
        if (t200 < 0 && v >= 200) t200 = t;
        maxV = std::fmax(maxV, v);
        if (!finite(car)) break;
      }
      check("0-100 km/h", inRange(t100, expect.t100, t100 >= 0), fmt("%.2f s", t100));
      check("0-200 km/h", inRange(t200, expect.t200, t200 >= 0), fmt("%.2f s", t200));
      check("velocidade máxima", inRange(maxV, expect.vmax), fmt("%.1f km/h, marchas: %s", maxV, shifts.c_str()));
      check("sem desvio lateral", std::fabs(car.pos.x) < 1, fmt("x=%.3f m após %.0f m", car.pos.x, car.pos.z));
    }

    // 3) Frenagem 200 → 0 com ABS
    {
      Vehicle car = make();
      Controls inp{1, 0, 0, 0};
      while (kmh(car.forwardSpeed()) < 200) car.step(DT, inp);
      inp = {0, 1, 0, 0};
      double z0 = car.pos.z, t = 0, peakG = 0;
      while (car.forwardSpeed() > 0.05 && t < 15) { car.step(DT, inp); t += DT; peakG = std::fmax(peakG, car.accel.z / 9.81); }
      double dist = std::fabs(car.pos.z - z0);
      check("frenagem 200-0", inRange(dist, expect.brake), fmt("%.1f m em %.2f s, pico %.2f g", dist, t, peakG));
      for (int i = 0; i < 3000; i++) car.step(DT, inp);
      check("parado após frear", car.speed() < 0.02 && finite(car),
            fmt("vel=%.2e, rpm=%.0f, marcha=%d", car.speed(), car.engine.rpm, car.gear));
    }

    // 4) Curva em regime: aumenta esterço a velocidade constante e mede g lateral máximo
    for (double target : {80.0, 160.0}) {
      Vehicle car = make();
      Controls inp;
      double maxLat = 0, t = 0;
      while (t < 40) {
        double v = kmh(car.forwardSpeed());
        inp.throttle = std::fmin(1, std::fmax(0, (target - v) * 0.15 + 0.25));
        if (t > 12) inp.steer = std::fmin(0.6, (t - 12) * (target > 100 ? 0.012 : 0.03));
        car.step(DT, inp);
        t += DT;
        if (t > 12) maxLat = std::fmax(maxLat, std::fabs(car.accel.x) / 9.81);
        if (!finite(car)) break;
      }
      std::string name = fmt("g lateral a %.0f km/h", target);
      check(name.c_str(), inRange(maxLat, expect.lat) && finite(car), fmt("%.2f g", maxLat));
    }

    // 5) Ré
    {
      Vehicle car = make();
      Controls idle;
      car.assists.autoGearbox = false;
      car.shiftDown();
      for (int i = 0; i < 200; i++) car.step(DT, idle);
      Controls inp{0.5, 0, 0, 0};
      for (int i = 0; i < 3000; i++) car.step(DT, inp);
      check("marcha à ré", car.gear == -1 && car.forwardSpeed() < -2, fmt("marcha=%d, v=%.1f km/h", car.gear, kmh(car.forwardSpeed())));
    }

    // 6) Freio de mão + esterço em baixa: deve rodar (sobre-esterço) sem explodir
    {
      Vehicle car = make();
      Controls inp{1, 0, 0, 0};
      while (kmh(car.forwardSpeed()) < 90) car.step(DT, inp);
      inp = {0, 0, 1, 1};
      double maxYaw = 0;
      for (int i = 0; i < 3000; i++) { car.step(DT, inp); maxYaw = std::fmax(maxYaw, std::fabs(car.angVel.y)); }
      check("freio de mão", finite(car) && maxYaw > 0.8 && car.pos.y > 0.2 && car.pos.y < 0.6,
            fmt("taxa de guinada máx %.2f rad/s, CG y=%.3f", maxYaw, car.pos.y));
    }

    // 7) Estresse: entradas aleatórias, sem auxílios, trocas de marcha aleatórias
    {
      Vehicle car = make();
      car.assists = {false, false, false};
      double s = 12345;
      auto rnd = [&] { s = std::fmod(s * 16807, 2147483647); return s / 2147483647; };
      Controls inp;
      bool ok = true;
      double maxY = 0;
      for (int i = 0; i < 120000 && ok; i++) {
        if (i % 400 == 0) {
          inp.throttle = rnd() < 0.6 ? rnd() : 0;
          inp.brake = rnd() < 0.25 ? rnd() : 0;
          inp.steer = rnd() * 2 - 1;
          inp.handbrake = rnd() < 0.05 ? 1 : 0;
          double r = rnd();
          if (r < 0.15) car.shiftUp();
          else if (r < 0.25) car.shiftDown();
        }
        car.step(DT, inp);
        ok = finite(car);
        maxY = std::fmax(maxY, car.pos.y);
      }
      check("estresse 120 s", ok && maxY < 1.5, fmt("finito=%s, CG y máx=%.3f m, v final=%.0f km/h", ok ? "true" : "false", maxY, kmh(car.speed())));
    }
  }

  // 8) Desempenho
  {
    Vehicle car(FERRARI_296_GT3, &ground);
    Controls inp{1, 0, 0.2, 0};
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20000; i++) car.step(DT, inp);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20;
    check("custo por segundo simulado", ms < 30, fmt("%.2f ms de CPU para 1 s (1000 passos)", ms));
  }

  std::printf(failures ? "\n%d falha(s)\n" : "\nTodos os testes passaram\n", failures);
  return failures ? 1 : 0;
}
