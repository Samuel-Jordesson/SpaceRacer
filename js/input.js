// Entrada: teclado (com rampas, como um piloto modulando pedal) e gamepad/volante (analógico).

import { clamp } from './physics/math.js';

export class Input {
  constructor() {
    this.keys = new Set();
    this.pressed = new Set();   // teclas apertadas neste frame (eventos únicos)
    this.throttle = 0; this.brake = 0; this.steer = 0; this.handbrake = 0;
    this.source = 'teclado';
    this.padPrev = [];
    addEventListener('keydown', (e) => {
      if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Space'].includes(e.code)) e.preventDefault();
      if (!e.repeat) this.pressed.add(e.code);
      this.keys.add(e.code);
    });
    addEventListener('keyup', (e) => this.keys.delete(e.code));
    addEventListener('blur', () => this.keys.clear());
  }

  has(...codes) { return codes.some((c) => this.keys.has(c)); }
  // consome um evento de tecla
  take(...codes) {
    let hit = false;
    for (const c of codes) if (this.pressed.delete(c)) hit = true;
    return hit;
  }

  update(dt, speedKmh) {
    const pad = [...(navigator.getGamepads?.() || [])].find((p) => p && p.connected);
    const events = { shiftUp: false, shiftDown: false };

    const kbThrottle = this.has('KeyW', 'ArrowUp') ? 1 : 0;
    const kbBrake = this.has('KeyS', 'ArrowDown') ? 1 : 0;
    const kbSteer = (this.has('KeyD', 'ArrowRight') ? 1 : 0) - (this.has('KeyA', 'ArrowLeft') ? 1 : 0);
    const usingKb = kbThrottle || kbBrake || kbSteer || this.has('Space');

    let padActive = false;
    if (pad) {
      const b = (i) => pad.buttons[i]?.value ?? 0;
      const dz = (v, d = 0.06) => (Math.abs(v) < d ? 0 : (v - Math.sign(v) * d) / (1 - d));
      const steer = dz(pad.axes[0] ?? 0);
      const thr = b(7), brk = b(6);
      padActive = Math.abs(steer) > 0 || thr > 0.02 || brk > 0.02 || b(0) > 0.5;
      const edge = (i) => { const now = b(i) > 0.5; const was = this.padPrev[i]; this.padPrev[i] = now; return now && !was; };
      if (edge(5)) events.shiftUp = true;   // RB
      if (edge(4)) events.shiftDown = true; // LB
      if (edge(3)) this.pressed.add('KeyC'); // Y = câmera
      if (edge(9)) this.pressed.add('KeyR'); // Start = reset
      if (padActive && !usingKb) {
        this.source = 'controle';
        this.throttle = thr;
        this.brake = brk;
        this.handbrake = b(0) > 0.5 ? 1 : 0; // A
        // curva de sensibilidade não-linear para precisão perto do centro
        this.steer = Math.sign(steer) * Math.pow(Math.abs(steer), 1.4);
      }
    }

    if (!padActive || usingKb) {
      this.source = 'teclado';
      // pedais com rampa (aperta em ~0,15 s, solta em ~0,08 s)
      this.throttle = ramp(this.throttle, kbThrottle, dt, 6.5, 12);
      this.brake = ramp(this.brake, kbBrake, dt, 8, 12);
      this.handbrake = this.has('Space') ? 1 : 0;
      // direção: teclado é digital, então limita o esterço com a velocidade
      // (o mesmo que os simuladores fazem com "steering speed sensitivity")
      const limit = 1 / (1 + Math.pow(Math.max(0, speedKmh) / 85, 1.6) * 1.5);
      const target = kbSteer * limit;
      const rate = (kbSteer === 0 || Math.sign(kbSteer) !== Math.sign(this.steer)) ? 4.5 : 2.2;
      this.steer += clamp(target - this.steer, -rate * dt, rate * dt);
    }

    if (this.take('KeyE', 'ShiftLeft', 'ShiftRight')) events.shiftUp = true;
    if (this.take('KeyQ', 'ControlLeft', 'ControlRight')) events.shiftDown = true;
    return events;
  }

  endFrame() { this.pressed.clear(); }
}

function ramp(v, target, dt, up, down) {
  const r = target > v ? up : down;
  return v + clamp(target - v, -r * dt, r * dt);
}
