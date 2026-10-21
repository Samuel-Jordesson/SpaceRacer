#include "vehicle.h"

#include <algorithm>

namespace phys {

static const double G = 9.81;
static const double RPM = 60 / (2 * M_PI);  // rad/s → rpm

Vehicle::Vehicle(const CarSpec& s, const Ground* gr, const Geometry* geo)
    : spec(s), ground(gr), tire(s.tire), assists(s.assists) {
  geometry = geo ? *geo : spec.geometry;
  const Geometry& g = geometry;
  mass = spec.mass;
  inertia = V3(spec.inertia.pitch, spec.inertia.yaw, spec.inertia.roll);
  a = (1 - spec.frontWeight) * g.wheelbase;
  b = spec.frontWeight * g.wheelbase;
  double h = spec.cgHeight;

  struct Corner { const char* id; bool front, left; };
  const Corner corners[4] = {{"FL", true, true}, {"FR", true, false}, {"RL", false, true}, {"RR", false, false}};
  for (int i = 0; i < 4; i++) {
    const Corner& c = corners[i];
    Wheel& w = wheels[i];
    w.susp = c.front ? spec.suspension.front : spec.suspension.rear;
    w.radius = c.front ? g.radiusFront : g.radiusRear;
    double track = c.front ? g.trackFront : g.trackRear;
    double axleLoad = mass * G * (c.front ? spec.frontWeight : 1 - spec.frontWeight) / 2;
    w.id = c.id;
    w.front = c.front;
    w.left = c.left;
    w.driven = spec.frontDrive ? c.front : !c.front;
    w.motor = c.front && spec.electricFront.present;  // motor elétrico no eixo dianteiro (híbridos AWD)
    w.hardpoint = V3((c.left ? -1 : 1) * track / 2, w.radius - h, c.front ? -a : b);
    w.x0 = axleLoad / w.susp.k;
    w.inertia = c.front ? spec.tire.inertiaFront : spec.tire.inertiaRear;
    w.maxBrake = c.front ? spec.brakes.maxTorqueFront : spec.brakes.maxTorqueRear;
  }

  // Pontos de colisão da carroceria (corpo), para o caso de capotar/bater no chão.
  double hx = 0.98, zf = -a - 1.12, zr = b + 1.05;
  double yb = 0.09 - h, yt = 1.18 - h;
  for (double x : {-hx, hx})
    for (double z : {zf, zr})
      for (double y : {yb, yt}) hull.emplace_back(x, y, z);
  for (double x : {-hx * 0.8, hx * 0.8}) hull.emplace_back(x, yt, 0);
  // meia altura nas laterais (bate em muro/barreira mais baixa que a carroceria)
  for (double x : {-hx, hx})
    for (double z : {zf, 0.0, zr}) hull.emplace_back(x, 0.5 - h, z);

  reset();
}

void Vehicle::reset(const V3& position, double heading) {
  pos = V3(position.x, position.y + spec.cgHeight + 0.01, position.z);
  quat = Quat::fromAxisAngle(V3(0, 1, 0), heading);
  vel = V3();
  angVel = V3();
  for (Wheel& w : wheels) {
    w.omega = 0;
    w.compression = w.x0;
    w.absFactor = 1;
    w.Tbrake = 0;
    w.brakeTemp = spec.brakes.ambientTemp + 60;
  }
  const Engine& e = spec.engine;
  engine = EngineState{};
  engine.omega = e.idleRpm / RPM;
  engine.rpm = e.idleRpm;
  gear = 0;
  shift.reset();
  clutch = 0;
  clutchLocked = false;
  clutchTorque = 0;
  tcCut = 1;
  tcFront = 1;
  frontMotorTorque = 0;
  tcActive = false;
  absActive = false;
  steerAngle = 0;
  accel = V3();  // aceleração no referencial do carro (para o HUD)
  time = 0;
  overrevBlocked = 0;
}

// ------------------------------------------------------------------ câmbio
double Vehicle::ratio(int g) const {
  const Gearbox& gb = spec.gearbox;
  if (g == 0) return 0;
  return (g < 0 ? gb.reverse : gb.ratios[g - 1]) * gb.finalDrive;
}

// engatar ou desengatar do neutro (parado, antes de largar) não é uma troca em movimento
double Vehicle::shiftDuration(int from, int to) const {
  return from == 0 || to == 0 ? std::min(spec.gearbox.shiftTime, 0.05) : spec.gearbox.shiftTime;
}

// rodas ligadas ao motor: FL/FR (0, 1) na tração dianteira, RL/RR (2, 3) na traseira
int Vehicle::drivenLeft() const { return spec.frontDrive ? 0 : 2; }
double Vehicle::drivenOmega() const { return (wheels[drivenLeft()].omega + wheels[drivenLeft() + 1].omega) / 2; }

bool Vehicle::shiftUp() {
  if (shift) return false;
  if (gear >= (int)spec.gearbox.ratios.size()) return false;
  shift = Shift{gear + 1, 0, true, shiftDuration(gear, gear + 1)};
  return true;
}

bool Vehicle::shiftDown() {
  if (shift) return false;
  int target = gear - 1;
  if (target < -1) return false;
  if (target == -1 && std::fabs(forwardSpeed()) > 1.5) return false;
  if (target >= 1) {
    double rpmAfter = std::fabs(drivenOmega() * ratio(target)) * RPM;
    if (rpmAfter > spec.engine.limiterRpm + 300) { overrevBlocked = 0.6; return false; }
  }
  shift = Shift{target, 0, false, shiftDuration(gear, target)};
  return true;
}

// ------------------------------------------------------------------ passo de física
void Vehicle::step(double dt, const Controls& input) {
  time += dt;
  const Mat3 R = quat.toMat3();
  const V3 up = R.col(1), fwd = R.col(2) * -1;
  V3 force(0, -mass * G, 0);
  V3 torque;
  auto addForceAt = [&](const V3& f, const V3& p) {
    force.addScaled(f, 1);
    V3 r = p - pos;
    torque.addScaled(r.cross(f), 1);
  };
  auto pointVel = [&](const V3& p) { return vel + angVel.cross(p - pos); };

  // ---------------- direção (Ackermann)
  double maxSteer = spec.steering.maxAngleDeg * M_PI / 180;
  double delta = -clamp(input.steer, -1, 1) * maxSteer;  // positivo = esquerda
  steerAngle = delta;
  double L = geometry.wheelbase, tF = geometry.trackFront;
  for (Wheel& w : wheels) {
    if (!w.front) { w.steer = 0; continue; }
    if (std::fabs(delta) < 1e-5) { w.steer = delta; continue; }
    double rT = L / std::tan(std::fabs(delta));
    double inner = std::atan(L / (rT - tF / 2)), outer = std::atan(L / (rT + tF / 2));
    bool isInner = (delta > 0) == w.left;
    double ack = isInner ? inner : outer;
    w.steer = sign(delta) * (std::fabs(delta) + spec.steering.ackermann * (ack - std::fabs(delta)));
  }

  // ---------------- aerodinâmica
  const Aero& ae = spec.aero;
  double vF = vel.dot(fwd);
  double spd = vel.length();
  force.addScaled(vel, -0.5 * ae.rho * ae.CdA * spd);
  double down = 0.5 * ae.rho * ae.ClA * vF * vF;
  addForceAt(up * (-down * ae.frontBalance), pos + R.mulV(V3(0, 0, -a)));
  addForceAt(up * (-down * (1 - ae.frontBalance)), pos + R.mulV(V3(0, 0, b)));

  // ---------------- suspensão
  for (Wheel& w : wheels) {
    V3 O = pos + R.mulV(w.hardpoint);
    w.origin = O;
    const double lift = 0.6;
    auto hit = ground->raycast(O + up * lift, up * -1, lift + w.x0 + w.radius);
    if (hit) {
      double t = hit->dist - lift;
      double x = w.x0 + w.radius - t;
      w.contact = x > 0;
      w.contactNormal.set(hit->nx, hit->ny, hit->nz);
      w.surfaceMu = hit->mu;
      w.surfaceDrag = hit->drag;
      w.compression = std::max(0.0, x);
    } else {
      w.contact = false;
      w.compression = 0;
    }
    w.fsusp = 0;
    if (w.contact) {
      const V3& n = w.contactNormal;
      double xdot = -pointVel(O).dot(n) / std::max(0.3, up.dot(n));
      double xmax = w.x0 + w.susp.travel;
      double f = w.susp.k * w.compression + (w.compression > xmax ? spec.suspension.bumpStopK * (w.compression - xmax) : 0);
      f += (xdot > 0 ? w.susp.bump : w.susp.rebound) * xdot;
      w.fsusp = f;
    }
  }
  // barras estabilizadoras
  for (auto [l, r] : {std::pair{0, 1}, std::pair{2, 3}}) {
    Wheel &wl = wheels[l], &wr = wheels[r];
    double d = (wl.contact ? wl.compression : 0) - (wr.contact ? wr.compression : 0);
    double f = wl.susp.arb * d;
    if (wl.contact) wl.fsusp += f;
    if (wr.contact) wr.fsusp -= f;
  }
  for (Wheel& w : wheels) {
    w.fsusp = std::max(0.0, w.fsusp);
    if (!w.contact) { w.fz = 0; continue; }
    addForceAt(up * w.fsusp, w.origin);
    w.fz = w.fsusp * std::max(0.0, up.dot(w.contactNormal));
  }

  // ---------------- pedais e auxílios
  double brake = clamp(input.brake, 0, 1);
  double handbrake = clamp(input.handbrake, 0, 1);

  // ---------------- pneus
  for (Wheel& w : wheels) {
    const V3 n = w.contactNormal;
    V3 center = w.origin - up * (w.x0 - w.compression);
    w.center = center;
    if (!w.contact) { w.fx = w.fy = w.dfx = 0; w.slip = 0; w.slipRatio = 0; w.slipAngle = 0; continue; }
    V3 P = center - n * w.radius;
    w.contactPoint = P;
    // direção da roda no plano do chão
    double cs = std::cos(w.steer), sn = std::sin(w.steer);
    V3 right = R.col(0);
    V3 wf = fwd * cs + right * -sn;
    wf = (wf - n * wf.dot(n)).normalize();
    V3 wl = wf.cross(n);  // lateral (direita)
    V3 vP = pointVel(P);
    double vx = vP.dot(wf), vy = vP.dot(wl);
    double denomX = std::max(std::fabs(vx), 0.6);
    double denomY = std::max(std::fabs(vx), 1.2);
    w.denomX = denomX;
    double kappa = (w.omega * w.radius - vx) / denomX;
    double tanA = vy / denomY;
    tire.forces(w.fz, kappa, tanA, w.surfaceMu, tireOut);
    w.fx = tireOut.fx; w.fy = tireOut.fy; w.dfx = tireOut.dfx; w.slip = tireOut.s;
    w.slipRatio = kappa; w.slipAngle = std::atan(tanA);
    addForceAt(wf * w.fx + wl * w.fy, P);
  }

  // ABS: modula pressão por roda para manter κ próximo do pico em frenagem
  absActive = false;
  for (Wheel& w : wheels) {
    double target = 1;
    if (assists.abs && w.contact && spd > 2 && brake > 0.05) {
      double lim = tire.kPeak * 1.05;
      if (-w.slipRatio > lim) target = clamp(1 - (-w.slipRatio - lim) / (tire.kPeak * 0.6), 0, 1);
    }
    w.absFactor += (target - w.absFactor) * clamp(dt / (target < w.absFactor ? 0.004 : 0.03), 0, 1);
    if (w.absFactor < 0.97) absActive = true;
  }

  // ---------------- motor, embreagem, câmbio e rodas
  drivetrain(dt, input, brake, handbrake);

  // ---------------- colisão da carroceria com o chão
  for (const V3& hp : hull) {
    V3 P = pos + R.mulV(hp);
    GroundQuery gq = ground->heightNear(P.x, P.z, P.y);
    double d = gq.h - P.y;
    if (d <= 0) continue;
    V3 n(gq.nx, gq.ny, gq.nz);
    V3 v = pointVel(P);
    double vn = v.dot(n);
    double fn = std::max(0.0, 250000 * d - 12000 * vn);
    V3 vt = v - n * vn;
    double vtl = vt.length();
    V3 ft = vtl > 1e-4 ? vt * (-std::min(0.55 * fn, 4000 * vtl) / vtl) : V3();
    addForceAt(n * fn + ft, P);
  }
  // ---------------- colisão com muros e barreiras (mesma mola/amortecedor, atrito menor)
  for (const V3& hp : hull) {
    V3 P = pos + R.mulV(hp), n;
    double depth;
    if (!ground->wallContact(P, 0.12, n, depth)) continue;
    V3 v = pointVel(P);
    double vn = v.dot(n);
    double fn = std::max(0.0, 300000 * depth - 15000 * vn);
    V3 vt = v - n * vn;
    double vtl = vt.length();
    V3 ft = vtl > 1e-4 ? vt * (-std::min(0.35 * fn, 4000 * vtl) / vtl) : V3();
    addForceAt(n * fn + ft, P);
  }

  // ---------------- integração do corpo rígido
  vel.addScaled(force, dt / mass);
  V3 wb = R.mulTV(angVel), tb = R.mulTV(torque);
  const V3& I = inertia;
  V3 Iw(I.x * wb.x, I.y * wb.y, I.z * wb.z);
  V3 gyro = wb.cross(Iw);
  wb.x += (tb.x - gyro.x) / I.x * dt;
  wb.y += (tb.y - gyro.y) / I.y * dt;
  wb.z += (tb.z - gyro.z) / I.z * dt;
  angVel = R.mulV(wb);
  pos.addScaled(vel, dt);
  quat.integrate(angVel, dt);

  // aceleração no referencial do carro (sem gravidade): para o medidor de G
  V3 aW = (force + V3(0, mass * G, 0)) * (1 / mass);
  V3 aB = R.mulTV(aW);
  accel.set(accel.x + (aB.x - accel.x) * 0.02, accel.y + (aB.y - accel.y) * 0.02, accel.z + (aB.z - accel.z) * 0.02);

  // temperatura do disco: calor = torque de freio × rotação; resfria com o fluxo de ar
  const Brakes& bk = spec.brakes;
  for (Wheel& w : wheels) {
    w.spin = std::fmod(w.spin + w.omega * dt, M_PI * 2);
    double heat = w.Tbrake * std::fabs(w.omega);
    w.brakeTemp += dt * (heat / bk.heatCapacity - (w.brakeTemp - bk.ambientTemp) * (bk.coolingBase + bk.coolingPerMs * spd));
  }
}

void Vehicle::drivetrain(double dt, const Controls& input, double brake, double handbrake) {
  const Engine& es = spec.engine;
  const Gearbox& gb = spec.gearbox;
  const Differential& df = spec.differential;
  EngineState& eng = engine;
  const double rpm = eng.omega * RPM;

  // --- troca de marcha sequencial (corte de ignição / blip automático)
  double throttle = clamp(input.throttle, 0, 1);
  double clutchTarget = 0;
  if (shift) {
    Shift& s = *shift;
    if (s.t == 0) gear = s.target;
    s.t += dt;
    clutchTarget = 0;
    if (s.up) throttle = 0;
    else if (gear != 0) {
      double targetRpm = std::fabs(drivenOmega() * ratio()) * RPM;
      throttle = clamp((targetRpm - rpm) / 600, 0, 1);
    }
    if (s.t >= s.duration) { shift.reset(); clutchLocked = gear != 0 && rpm > 1500; }
  }

  // --- embreagem automática (anti-stall / largada)
  if (!shift) {
    if (gear == 0) clutchTarget = 0;
    else {
      // a embreagem morde entre a marcha lenta e uma rotação que sobe com o acelerador
      // com mais acelerador o motorista deixa o giro subir antes de a embreagem morder (evita afogar)
      double lo = es.idleRpm + 100 + throttle * (es.clutchFullRpm - es.idleRpm) * 0.2;
      double hi = lo + 300 + throttle * (es.clutchFullRpm - lo - 300);
      double win = clamp((rpm - lo) / (hi - lo), 0, 1);
      clutchTarget = clutchLocked && rpm > 1100 ? 1 : win;
    }
  }
  double rate = clutchTarget > clutch ? (clutchLocked ? 25 : 15) : 40;
  clutch += clamp(clutchTarget - clutch, -rate * dt, rate * dt);

  // --- controle de tração
  Wheel& wl = wheels[drivenLeft()];
  Wheel& wr = wheels[drivenLeft() + 1];
  double tcTarget = 1;
  if (assists.tc && gear != 0) {
    double k = std::max(wl.contact ? wl.slipRatio * sign(gear) : 0, wr.contact ? wr.slipRatio * sign(gear) : 0);
    double lim = tire.kPeak * 1.15;
    if (k > lim) tcTarget = clamp(1 - (k - lim) / (tire.kPeak * 0.9), 0, 1);
  }
  tcCut += (tcTarget - tcCut) * clamp(dt / (tcTarget < tcCut ? 0.01 : 0.08), 0, 1);
  tcActive = tcCut < 0.97 && throttle > 0.05;
  throttle *= tcCut;

  // --- motores elétricos dianteiros (um por roda, redução fixa, sem câmbio):
  // torque constante até a potência máxima, depois potência constante; desligam na vel. máx. do e-eixo
  const ElectricFront& ef = spec.electricFront;
  frontMotorTorque = 0;
  if (ef.present && gear != 0) {
    double pedal = clamp(input.throttle, 0, 1);
    Wheel& f0 = wheels[0];
    Wheel& f1 = wheels[1];
    double fTarget = 1;
    if (assists.tc) {
      double k = std::max(f0.contact ? f0.slipRatio * sign(gear) : 0, f1.contact ? f1.slipRatio * sign(gear) : 0);
      double lim = tire.kPeak * 1.15;
      if (k > lim) fTarget = clamp(1 - (k - lim) / (tire.kPeak * 0.9), 0, 1);
    }
    tcFront += (fTarget - tcFront) * clamp(dt / (fTarget < tcFront ? 0.01 : 0.08), 0, 1);
    if (tcFront < 0.97 && pedal > 0.05) tcActive = true;
    double om = std::max(1.0, std::fabs((f0.omega + f1.omega) / 2));
    double v = std::fabs(vel.dot(quat.toMat3().col(2) * -1));
    // e-eixo desacopla perto da vel. máx.; em ré, limita a ~25 km/h
    double fade = gear < 0 ? clamp((25 / 3.6 - v) / 3, 0, 1) : clamp((ef.cutoffKmh / 3.6 - v) / 8, 0, 1);
    frontMotorTorque = sign(gear) * pedal * tcFront * fade * std::min(ef.maxWheelTorque, ef.powerPerWheel / om);
  }

  // --- limitador e marcha lenta
  if (rpm > es.limiterRpm) eng.limiter = true;
  else if (rpm < es.limiterRpm - 150) eng.limiter = false;
  if (eng.limiter) throttle = 0;
  // limitador de ré (~30 km/h)
  if (gear < 0) throttle *= clamp((30 / 3.6 + forwardSpeed()) / 2, 0, 1);
  if (rpm < es.idleRpm) throttle = std::max(throttle, clamp((es.idleRpm - rpm) / 100, 0, 1) * 0.5);
  eng.throttle = throttle;

  // --- torque do motor (turbo com atraso)
  double boostAvail = clamp((rpm - es.boostRpmStart) / (es.boostRpmFull - es.boostRpmStart), 0, 1);
  double boostTarget = throttle * boostAvail;
  double tau = boostTarget > eng.boost ? es.turboLag / (0.4 + 0.6 * boostAvail) : es.turboLag * 2;
  eng.boost += (boostTarget - eng.boost) * clamp(dt / tau, 0, 1);
  double full = table(es.torqueCurve, std::max(rpm, 0.0));
  double charge = 1 - es.turboShare + es.turboShare * (throttle > 0 ? eng.boost / std::max(throttle, 1e-3) : 0);
  double Te = throttle * full * std::min(1.0, charge) - (1 - throttle) * (es.frictionA + es.frictionB * rpm);
  // motor elétrico acoplado ao câmbio (híbrido): torque extra no virabrequim, limitado pela potência
  const ElectricRear& er = spec.electricRear;
  if (er.present) Te += throttle * std::min(er.maxTorque, er.power / std::max(eng.omega, 50.0));
  if (Te > 0) Te *= gb.efficiency;
  eng.torque = Te;

  // --- rodas: torque do pneu linearizado (integração semi-implícita, estável com pneus rígidos)
  double rr = spec.tire.rollingResistance;
  for (Wheel& w : wheels) {
    w.T0 = w.fx * w.radius;
    // rigidez para a integração implícita: o maior entre a derivada local e a secante F/κ.
    // A secante evita a vibração (chatter) do atrito saturado em baixa velocidade.
    double secant = std::fabs(w.slipRatio) > 1e-6 ? w.fx / w.slipRatio : w.dfx;
    w.kT = std::max({0.0, w.dfx, secant}) * w.radius * w.radius / w.denomX;
    double tb = brake * w.maxBrake * w.absFactor;
    if (!w.front) tb += handbrake * spec.brakes.handbrakeTorque;
    w.Tbrake = tb;
    w.Tfric = tb + w.fz * (rr + w.surfaceDrag) * w.radius;
  }

  double g = ratio();
  double cap = gb.clutchMaxTorque * clutch;

  // diferencial autoblocante: torque de bloqueio entre as rodas traseiras
  double dOm = wl.omega - wr.omega;
  double inT = std::fabs(clutchTorque * g);
  double dOmega = drivenOmega();
  double lockMax = df.preload + (clutchTorque * g * sign(dOmega != 0 ? dOmega : 1) >= 0 ? df.powerLock : df.coastLock) * inT;
  double Tlsd = clamp(-dOm * df.viscous, -lockMax, lockMax);

  double Tc = 0;
  bool coupled = g != 0 && cap > 0;
  if (coupled) {
    // Solução implícita da embreagem travada: ωe' = g·(ωL'+ωR')/2
    struct AA { double a, A; };
    auto Af = [&](const Wheel& w, double Text) {
      double aa = w.inertia + dt * w.kT;
      double Tf = w.Tfric * sign(w.omega);
      return AA{aa, (w.inertia * w.omega + dt * (Text - w.T0 + w.kT * w.omega - Tf)) / aa};
    };
    AA Lw = Af(wl, Tlsd), Rw = Af(wr, -Tlsd);
    double num = eng.omega + dt * Te / es.inertia - g * (Lw.A + Rw.A) / 2;
    double den = dt / es.inertia + dt * g * g / 4 * (1 / Lw.a + 1 / Rw.a);
    Tc = num / den;
    clutchLocked = std::fabs(Tc) <= cap;
    if (!clutchLocked) Tc = sign(Tc) * cap;
  } else {
    clutchLocked = false;
  }
  clutchTorque = Tc;

  // integra rodas
  for (Wheel& w : wheels) {
    double Text = 0;
    if (w.driven) Text = Tc * g / 2 + (&w == &wl ? Tlsd : -Tlsd);
    if (w.motor) Text += frontMotorTorque;
    // atrito (freio/rolagem) tratado como atrito seco implícito: trava a roda sem oscilar
    double aa = w.inertia + dt * w.kT;
    double free = (w.inertia * w.omega + dt * (Text - w.T0 + w.kT * w.omega)) / aa;
    double imp = dt * w.Tfric / aa;
    w.omega = std::fabs(free) <= imp ? 0 : free - sign(free) * imp;
  }

  // integra motor
  if (g != 0 && clutchLocked) eng.omega = g * (wl.omega + wr.omega) / 2;
  else eng.omega += dt * (Te - Tc) / es.inertia;
  eng.omega = std::max(0.0, eng.omega);
  eng.rpm = eng.omega * RPM;
  if (overrevBlocked > 0) overrevBlocked -= dt;

  // câmbio automático (opcional)
  if (assists.autoGearbox && !shift) {
    double r = eng.rpm;
    int n = (int)gb.ratios.size();
    if (gear == 0 && input.throttle > 0.05) shiftUp();
    else if (gear >= 1 && gear < n && r > es.limiterRpm - 300 && clutchLocked) shiftUp();
    else if (gear > 1) {
      double after = r * ratio(gear - 1) / ratio(gear);
      if (r < es.limiterRpm * 0.54 && after < es.limiterRpm - 1300) shiftDown();
    }
  }
}

}  // namespace phys
