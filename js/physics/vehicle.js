// Simulação do veículo: corpo rígido 6DOF + 4 rodas com suspensão por raycast,
// pneus Pacejka, motor turbo, embreagem, câmbio sequencial, diferencial autoblocante,
// aerodinâmica, ABS e controle de tração.
//
// Referencial do carro (corpo): x = direita, y = cima, z = trás (frente = −z). Origem no CG.

import { V3, Quat, clamp, sign, table } from './math.js';
import { TireModel } from './tire.js';

const G = 9.81;
const R_FWD = (q) => q.toMat3().col(2).scale(-1); // frente do carro no mundo
const RPM = 60 / (2 * Math.PI); // rad/s → rpm

export class Vehicle {
  constructor(spec, ground, geometry = {}) {
    this.spec = spec;
    this.ground = ground;
    this.tire = new TireModel(spec.tire);
    this.assists = { ...spec.assists };

    const g = { ...spec.geometry, ...geometry };
    this.geometry = g;
    this.mass = spec.mass;
    this.inertia = new V3(spec.inertia.pitch, spec.inertia.yaw, spec.inertia.roll);
    this.a = (1 - spec.frontWeight) * g.wheelbase; // CG → eixo dianteiro
    this.b = spec.frontWeight * g.wheelbase;       // CG → eixo traseiro
    const h = spec.cgHeight;

    this.wheels = [];
    const corners = [['FL', true, true], ['FR', true, false], ['RL', false, true], ['RR', false, false]];
    for (const [id, front, left] of corners) {
      const susp = front ? spec.suspension.front : spec.suspension.rear;
      const radius = front ? g.radiusFront : g.radiusRear;
      const track = front ? g.trackFront : g.trackRear;
      const axleLoad = this.mass * G * (front ? spec.frontWeight : 1 - spec.frontWeight) / 2;
      this.wheels.push({
        id, front, left, driven: !front,
        motor: front && !!spec.electric?.front, // motor elétrico no eixo dianteiro (híbridos AWD)
        hardpoint: new V3((left ? -1 : 1) * track / 2, radius - h, front ? -this.a : this.b),
        radius,
        susp,
        x0: axleLoad / susp.k,     // compressão estática
        inertia: front ? spec.tire.inertiaFront : spec.tire.inertiaRear,
        maxBrake: front ? spec.brakes.maxTorqueFront : spec.brakes.maxTorqueRear,
        // estado
        omega: 0, spin: 0, steer: 0,
        compression: 0, contact: false, fz: 0, fsusp: 0,
        slipRatio: 0, slipAngle: 0, slip: 0, fx: 0, fy: 0, dfx: 0,
        contactPoint: new V3(), contactNormal: new V3(0, 1, 0), surfaceMu: 1,
        absFactor: 1, denomX: 1,
      });
    }

    // Pontos de colisão da carroceria (corpo), para o caso de capotar/bater no chão.
    const hx = 0.98, zf = -this.a - 1.12, zr = this.b + 1.05;
    const yb = 0.09 - h, yt = 1.18 - h;
    this.hull = [];
    for (const x of [-hx, hx]) for (const z of [zf, zr]) for (const y of [yb, yt]) this.hull.push(new V3(x, y, z));
    for (const x of [-hx * 0.8, hx * 0.8]) this.hull.push(new V3(x, yt, 0));

    this.tireOut = { fx: 0, fy: 0, dfx: 0, s: 0 };
    this.reset();
  }

  reset(position = new V3(0, 0, 0), heading = 0) {
    this.pos = new V3(position.x, position.y + this.spec.cgHeight + 0.01, position.z);
    this.quat = Quat.fromAxisAngle(new V3(0, 1, 0), heading);
    this.vel = new V3();
    this.angVel = new V3();
    for (const w of this.wheels) { w.omega = 0; w.compression = w.x0; w.absFactor = 1; w.Tbrake = 0; w.brakeTemp = this.spec.brakes.ambientTemp + 60; }
    const e = this.spec.engine;
    this.engine = { omega: e.idleRpm / RPM, rpm: e.idleRpm, torque: 0, throttle: 0, boost: 0, limiter: false };
    this.gear = 0;              // −1 = R, 0 = N, 1..6
    this.shift = null;          // { target, t, up }
    this.clutch = 0;
    this.clutchLocked = false;
    this.clutchTorque = 0;
    this.tcCut = 1;
    this.tcFront = 1;
    this.frontMotorTorque = 0;
    this.tcActive = false;
    this.absActive = false;
    this.steerAngle = 0;
    this.accel = new V3();      // aceleração no referencial do carro (para o HUD)
    this.time = 0;
    this.overrevBlocked = 0;
  }

  // ------------------------------------------------------------------ câmbio
  ratio(gear = this.gear) {
    const gb = this.spec.gearbox;
    if (gear === 0) return 0;
    return (gear < 0 ? gb.reverse : gb.ratios[gear - 1]) * gb.finalDrive;
  }

  get speed() { return this.vel.length(); }
  get forwardSpeed() { return this.vel.dot(this.quat.toMat3().mulV(new V3(0, 0, -1))); }

  drivenOmega() {
    const r = this.wheels.filter((w) => w.driven);
    return (r[0].omega + r[1].omega) / 2;
  }

  shiftUp() {
    if (this.shift) return false;
    if (this.gear >= this.spec.gearbox.ratios.length) return false;
    this.shift = { target: this.gear + 1, t: 0, up: true };
    return true;
  }

  shiftDown() {
    if (this.shift) return false;
    const target = this.gear - 1;
    if (target < -1) return false;
    if (target === -1 && Math.abs(this.forwardSpeed) > 1.5) return false;
    if (target >= 1) {
      const rpmAfter = Math.abs(this.drivenOmega() * this.ratio(target)) * RPM;
      if (rpmAfter > this.spec.engine.limiterRpm + 300) { this.overrevBlocked = 0.6; return false; }
    }
    this.shift = { target, t: 0, up: false };
    return true;
  }

  // ------------------------------------------------------------------ passo de física
  // input: { throttle 0..1, brake 0..1, steer −1..1 (direita +), handbrake 0..1 }
  step(dt, input) {
    const spec = this.spec;
    this.time += dt;
    const R = this.quat.toMat3();
    const up = R.col(1), fwd = R.col(2).scale(-1);
    const force = new V3(0, -this.mass * G, 0);
    const torque = new V3();
    const addForceAt = (f, p) => {
      force.addScaled(f, 1);
      const r = p.sub(this.pos);
      torque.addScaled(r.cross(f), 1);
    };
    const pointVel = (p) => this.vel.add(this.angVel.cross(p.sub(this.pos)));

    // ---------------- direção (Ackermann)
    const maxSteer = spec.steering.maxAngleDeg * Math.PI / 180;
    const delta = -clamp(input.steer, -1, 1) * maxSteer; // positivo = esquerda
    this.steerAngle = delta;
    const L = this.geometry.wheelbase, tF = this.geometry.trackFront;
    for (const w of this.wheels) {
      if (!w.front) { w.steer = 0; continue; }
      if (Math.abs(delta) < 1e-5) { w.steer = delta; continue; }
      const rT = L / Math.tan(Math.abs(delta));
      const inner = Math.atan(L / (rT - tF / 2)), outer = Math.atan(L / (rT + tF / 2));
      const isInner = (delta > 0) === w.left;
      const ack = isInner ? inner : outer;
      w.steer = sign(delta) * (Math.abs(delta) + spec.steering.ackermann * (ack - Math.abs(delta)));
    }

    // ---------------- aerodinâmica
    const ae = spec.aero;
    const vF = this.vel.dot(fwd);
    const spd = this.vel.length();
    force.addScaled(this.vel, -0.5 * ae.rho * ae.CdA * spd);
    const down = 0.5 * ae.rho * ae.ClA * vF * vF;
    addForceAt(up.scale(-down * ae.frontBalance), this.pos.add(R.mulV(new V3(0, 0, -this.a))));
    addForceAt(up.scale(-down * (1 - ae.frontBalance)), this.pos.add(R.mulV(new V3(0, 0, this.b))));

    // ---------------- suspensão
    const sp = spec.suspension;
    for (const w of this.wheels) {
      const O = this.pos.add(R.mulV(w.hardpoint));
      w.origin = O;
      const lift = 0.6;
      const hit = this.ground.raycast(O.add(up.scale(lift)), up.scale(-1), lift + w.x0 + w.radius);
      if (hit) {
        const t = hit.dist - lift;
        const x = w.x0 + w.radius - t;
        w.contact = x > 0;
        w.contactNormal.set(hit.nx, hit.ny, hit.nz);
        w.surfaceMu = hit.mu ?? 1;
        w.compression = Math.max(0, x);
      } else {
        w.contact = false;
        w.compression = 0;
      }
      w.fsusp = 0;
      if (w.contact) {
        const n = w.contactNormal;
        const xdot = -pointVel(O).dot(n) / Math.max(0.3, up.dot(n));
        const xmax = w.x0 + w.susp.travel;
        let f = w.susp.k * w.compression + (w.compression > xmax ? sp.bumpStopK * (w.compression - xmax) : 0);
        f += (xdot > 0 ? w.susp.bump : w.susp.rebound) * xdot;
        w.fsusp = f;
      }
    }
    // barras estabilizadoras
    for (const [l, r] of [[0, 1], [2, 3]]) {
      const wl = this.wheels[l], wr = this.wheels[r];
      const d = (wl.contact ? wl.compression : 0) - (wr.contact ? wr.compression : 0);
      const f = wl.susp.arb * d;
      if (wl.contact) wl.fsusp += f;
      if (wr.contact) wr.fsusp -= f;
    }
    for (const w of this.wheels) {
      w.fsusp = Math.max(0, w.fsusp);
      if (!w.contact) { w.fz = 0; continue; }
      addForceAt(up.scale(w.fsusp), w.origin);
      w.fz = w.fsusp * Math.max(0, up.dot(w.contactNormal));
    }

    // ---------------- pedais e auxílios
    let brake = clamp(input.brake, 0, 1);
    const handbrake = clamp(input.handbrake || 0, 0, 1);

    // ---------------- pneus
    const tm = this.tire, to = this.tireOut;
    for (const w of this.wheels) {
      const n = w.contactNormal;
      const center = w.origin.sub(up.scale(w.x0 - w.compression));
      w.center = center;
      if (!w.contact) { w.fx = w.fy = w.dfx = 0; w.slip = 0; w.slipRatio = 0; w.slipAngle = 0; continue; }
      const P = center.sub(n.scale(w.radius));
      w.contactPoint.copy(P);
      // direção da roda no plano do chão
      const cs = Math.cos(w.steer), sn = Math.sin(w.steer);
      const right = R.col(0);
      let wf = fwd.scale(cs).add(right.scale(-sn));
      wf = wf.sub(n.scale(wf.dot(n))).normalize();
      const wl = wf.cross(n); // lateral (direita)
      const vP = pointVel(P);
      const vx = vP.dot(wf), vy = vP.dot(wl);
      const denomX = Math.max(Math.abs(vx), 0.6);
      const denomY = Math.max(Math.abs(vx), 1.2);
      w.denomX = denomX;
      const kappa = (w.omega * w.radius - vx) / denomX;
      const tanA = vy / denomY;
      tm.forces(w.fz, kappa, tanA, w.surfaceMu, to);
      w.fx = to.fx; w.fy = to.fy; w.dfx = to.dfx; w.slip = to.s;
      w.slipRatio = kappa; w.slipAngle = Math.atan(tanA);
      addForceAt(wf.scale(w.fx).add(wl.scale(w.fy)), P);
    }

    // ABS: modula pressão por roda para manter κ próximo do pico em frenagem
    this.absActive = false;
    for (const w of this.wheels) {
      let target = 1;
      if (this.assists.abs && w.contact && spd > 2 && brake > 0.05) {
        const lim = this.tire.kPeak * 1.05;
        if (-w.slipRatio > lim) target = clamp(1 - (-w.slipRatio - lim) / (this.tire.kPeak * 0.6), 0, 1);
      }
      w.absFactor += (target - w.absFactor) * clamp(dt / (target < w.absFactor ? 0.004 : 0.03), 0, 1);
      if (w.absFactor < 0.97) this.absActive = true;
    }

    // ---------------- motor, embreagem, câmbio e rodas
    this._drivetrain(dt, input, brake, handbrake);

    // ---------------- colisão da carroceria com o chão
    for (const hp of this.hull) {
      const P = this.pos.add(R.mulV(hp));
      const gq = this.ground.heightAt(P.x, P.z);
      const d = gq.h - P.y;
      if (d <= 0) continue;
      const n = new V3(gq.nx, gq.ny, gq.nz);
      const v = pointVel(P);
      const vn = v.dot(n);
      const fn = Math.max(0, 250000 * d - 12000 * vn);
      const vt = v.sub(n.scale(vn));
      const vtl = vt.length();
      const ft = vtl > 1e-4 ? vt.scale(-Math.min(0.55 * fn, 4000 * vtl) / vtl) : new V3();
      addForceAt(n.scale(fn).add(ft), P);
    }

    // ---------------- integração do corpo rígido
    this.vel.addScaled(force, dt / this.mass);
    const wb = R.mulTV(this.angVel), tb = R.mulTV(torque), I = this.inertia;
    const Iw = new V3(I.x * wb.x, I.y * wb.y, I.z * wb.z);
    const gyro = wb.cross(Iw);
    wb.x += (tb.x - gyro.x) / I.x * dt;
    wb.y += (tb.y - gyro.y) / I.y * dt;
    wb.z += (tb.z - gyro.z) / I.z * dt;
    this.angVel = R.mulV(wb);
    this.pos.addScaled(this.vel, dt);
    this.quat.integrate(this.angVel, dt);

    // aceleração no referencial do carro (sem gravidade): para o medidor de G
    const aW = force.add(new V3(0, this.mass * G, 0)).scale(1 / this.mass);
    const aB = R.mulTV(aW);
    this.accel.set(this.accel.x + (aB.x - this.accel.x) * 0.02, this.accel.y + (aB.y - this.accel.y) * 0.02, this.accel.z + (aB.z - this.accel.z) * 0.02);

    // temperatura do disco: calor = torque de freio × rotação; resfria com o fluxo de ar
    const b = spec.brakes;
    for (const w of this.wheels) {
      w.spin = (w.spin + w.omega * dt) % (Math.PI * 2);
      const heat = w.Tbrake * Math.abs(w.omega);
      w.brakeTemp += dt * (heat / b.heatCapacity - (w.brakeTemp - b.ambientTemp) * (b.coolingBase + b.coolingPerMs * spd));
    }
  }

  _drivetrain(dt, input, brake, handbrake) {
    const spec = this.spec, es = spec.engine, gb = spec.gearbox, df = spec.differential;
    const eng = this.engine;
    const rpm = eng.omega * RPM;

    // --- troca de marcha sequencial (corte de ignição / blip automático)
    let throttle = clamp(input.throttle, 0, 1);
    let clutchTarget;
    if (this.shift) {
      const s = this.shift;
      if (s.t === 0) this.gear = s.target;
      s.t += dt;
      clutchTarget = 0;
      if (s.up) throttle = 0;
      else if (this.gear !== 0) {
        const targetRpm = Math.abs(this.drivenOmega() * this.ratio()) * RPM;
        throttle = clamp((targetRpm - rpm) / 600, 0, 1);
      }
      if (s.t >= gb.shiftTime) { this.shift = null; this.clutchLocked = this.gear !== 0 && rpm > 1500; }
    }

    // --- embreagem automática (anti-stall / largada)
    if (!this.shift) {
      if (this.gear === 0) clutchTarget = 0;
      else {
        // a embreagem morde entre a marcha lenta e uma rotação que sobe com o acelerador
        const lo = es.idleRpm + 100;
        const hi = lo + 300 + throttle * (es.clutchFullRpm - lo - 300);
        const win = clamp((rpm - lo) / (hi - lo), 0, 1);
        clutchTarget = this.clutchLocked && rpm > 1100 ? 1 : win;
      }
    }
    const rate = clutchTarget > this.clutch ? (this.clutchLocked ? 25 : 15) : 40;
    this.clutch += clamp(clutchTarget - this.clutch, -rate * dt, rate * dt);

    // --- controle de tração
    const driven = this.wheels.filter((w) => w.driven);
    let tcTarget = 1;
    if (this.assists.tc && this.gear !== 0) {
      const k = Math.max(...driven.map((w) => (w.contact ? w.slipRatio * sign(this.gear) : 0)));
      const lim = this.tire.kPeak * 1.15;
      if (k > lim) tcTarget = clamp(1 - (k - lim) / (this.tire.kPeak * 0.9), 0, 1);
    }
    this.tcCut += (tcTarget - this.tcCut) * clamp(dt / (tcTarget < this.tcCut ? 0.01 : 0.08), 0, 1);
    this.tcActive = this.tcCut < 0.97 && throttle > 0.05;
    throttle *= this.tcCut;

    // --- motores elétricos dianteiros (um por roda, redução fixa, sem câmbio):
    // torque constante até a potência máxima, depois potência constante; desligam na vel. máx. do e-eixo
    const ef = spec.electric?.front;
    this.frontMotorTorque = 0;
    if (ef && this.gear !== 0) {
      const pedal = clamp(input.throttle, 0, 1);
      const fronts = this.wheels.filter((w) => w.motor);
      let fTarget = 1;
      if (this.assists.tc) {
        const k = Math.max(...fronts.map((w) => (w.contact ? w.slipRatio * sign(this.gear) : 0)));
        const lim = this.tire.kPeak * 1.15;
        if (k > lim) fTarget = clamp(1 - (k - lim) / (this.tire.kPeak * 0.9), 0, 1);
      }
      this.tcFront += (fTarget - this.tcFront) * clamp(dt / (fTarget < this.tcFront ? 0.01 : 0.08), 0, 1);
      if (this.tcFront < 0.97 && pedal > 0.05) this.tcActive = true;
      const om = Math.max(1, Math.abs((fronts[0].omega + fronts[1].omega) / 2));
      const v = Math.abs(this.vel.dot(R_FWD(this.quat)));
      // e-eixo desacopla perto da vel. máx.; em ré, limita a ~25 km/h
      const fade = this.gear < 0 ? clamp((25 / 3.6 - v) / 3, 0, 1) : clamp((ef.cutoffKmh / 3.6 - v) / 8, 0, 1);
      this.frontMotorTorque = sign(this.gear) * pedal * this.tcFront * fade * Math.min(ef.maxWheelTorque, ef.powerPerWheel / om);
    }

    // --- limitador e marcha lenta
    if (rpm > es.limiterRpm) eng.limiter = true;
    else if (rpm < es.limiterRpm - 150) eng.limiter = false;
    if (eng.limiter) throttle = 0;
    // limitador de ré (~30 km/h)
    if (this.gear < 0) throttle *= clamp((30 / 3.6 + this.forwardSpeed) / 2, 0, 1);
    if (rpm < es.idleRpm) throttle = Math.max(throttle, clamp((es.idleRpm - rpm) / 100, 0, 1) * 0.5);
    eng.throttle = throttle;

    // --- torque do motor (turbo com atraso)
    const boostAvail = clamp((rpm - es.boostRpmStart) / (es.boostRpmFull - es.boostRpmStart), 0, 1);
    const boostTarget = throttle * boostAvail;
    const tau = boostTarget > eng.boost ? es.turboLag / (0.4 + 0.6 * boostAvail) : es.turboLag * 2;
    eng.boost += (boostTarget - eng.boost) * clamp(dt / tau, 0, 1);
    const full = table(es.torqueCurve, Math.max(rpm, 0));
    const charge = 1 - es.turboShare + es.turboShare * (throttle > 0 ? eng.boost / Math.max(throttle, 1e-3) : 0);
    let Te = throttle * full * Math.min(1, charge) - (1 - throttle) * (es.frictionA + es.frictionB * rpm);
    // motor elétrico acoplado ao câmbio (híbrido): torque extra no virabrequim, limitado pela potência
    const er = spec.electric?.rear;
    if (er) Te += throttle * Math.min(er.maxTorque, er.power / Math.max(eng.omega, 50));
    if (Te > 0) Te *= gb.efficiency;
    eng.torque = Te;

    // --- rodas: torque do pneu linearizado (integração semi-implícita, estável com pneus rígidos)
    const rr = spec.tire.rollingResistance;
    for (const w of this.wheels) {
      w.T0 = w.fx * w.radius;
      // rigidez para a integração implícita: o maior entre a derivada local e a secante F/κ.
      // A secante evita a vibração (chatter) do atrito saturado em baixa velocidade.
      const secant = Math.abs(w.slipRatio) > 1e-6 ? w.fx / w.slipRatio : w.dfx;
      w.kT = Math.max(0, w.dfx, secant) * w.radius * w.radius / w.denomX;
      let tb = brake * w.maxBrake * w.absFactor;
      if (!w.front) tb += handbrake * spec.brakes.handbrakeTorque;
      w.Tbrake = tb;
      w.Tfric = tb + w.fz * rr * w.radius;
    }

    const g = this.ratio();
    const cap = gb.clutchMaxTorque * this.clutch;
    const [wl, wr] = driven;

    // diferencial autoblocante: torque de bloqueio entre as rodas traseiras
    const dOm = wl.omega - wr.omega;
    const inT = Math.abs(this.clutchTorque * g);
    const lockMax = df.preload + (this.clutchTorque * g * sign(this.drivenOmega() || 1) >= 0 ? df.powerLock : df.coastLock) * inT;
    const Tlsd = clamp(-dOm * df.viscous, -lockMax, lockMax);

    let Tc = 0;
    const coupled = g !== 0 && cap > 0;
    if (coupled) {
      // Solução implícita da embreagem travada: ωe' = g·(ωL'+ωR')/2
      const A = (w, Text) => {
        const a = w.inertia + dt * w.kT;
        const Tf = w.Tfric * sign(w.omega);
        return { a, A: (w.inertia * w.omega + dt * (Text - w.T0 + w.kT * w.omega - Tf)) / a };
      };
      const L = A(wl, Tlsd), Rr = A(wr, -Tlsd);
      const num = eng.omega + dt * Te / es.inertia - g * (L.A + Rr.A) / 2;
      const den = dt / es.inertia + dt * g * g / 4 * (1 / L.a + 1 / Rr.a);
      Tc = num / den;
      this.clutchLocked = Math.abs(Tc) <= cap;
      if (!this.clutchLocked) Tc = sign(Tc) * cap;
    } else {
      this.clutchLocked = false;
    }
    this.clutchTorque = Tc;

    // integra rodas
    for (const w of this.wheels) {
      let Text = 0;
      if (w.driven) Text = Tc * g / 2 + (w === wl ? Tlsd : -Tlsd);
      if (w.motor) Text += this.frontMotorTorque;
      // atrito (freio/rolagem) tratado como atrito seco implícito: trava a roda sem oscilar
      const a = w.inertia + dt * w.kT;
      const free = (w.inertia * w.omega + dt * (Text - w.T0 + w.kT * w.omega)) / a;
      const imp = dt * w.Tfric / a;
      w.omega = Math.abs(free) <= imp ? 0 : free - sign(free) * imp;
    }

    // integra motor
    if (g !== 0 && this.clutchLocked) {
      eng.omega = g * (wl.omega + wr.omega) / 2;
    } else {
      eng.omega += dt * (Te - Tc) / es.inertia;
    }
    eng.omega = Math.max(0, eng.omega);
    eng.rpm = eng.omega * RPM;
    if (this.overrevBlocked > 0) this.overrevBlocked -= dt;

    // câmbio automático (opcional)
    if (this.assists.autoGearbox && !this.shift) {
      const r = eng.rpm;
      if (this.gear === 0 && input.throttle > 0.05) this.shiftUp();
      else if (this.gear >= 1 && this.gear < gb.ratios.length && r > es.limiterRpm - 300 && this.clutchLocked) this.shiftUp();
      else if (this.gear > 1) {
        const after = r * this.ratio(this.gear - 1) / this.ratio(this.gear);
        if (r < es.limiterRpm * 0.54 && after < es.limiterRpm - 1300) this.shiftDown();
      }
    }
  }
}

// Chão plano infinito (y = 0). Interface usada pela física: raycast() e heightAt().
export class FlatGround {
  constructor(height = 0, mu = 1) { this.h = height; this.mu = mu; }
  raycast(o, d, maxDist) {
    if (d.y > -1e-6) return null;
    const t = (o.y - this.h) / -d.y;
    if (t < 0 || t > maxDist) return null;
    return { dist: t, nx: 0, ny: 1, nz: 0, mu: this.mu };
  }
  heightAt() { return { h: this.h, nx: 0, ny: 1, nz: 0 }; }
}
