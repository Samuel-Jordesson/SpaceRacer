// Som sintetizado com Web Audio: motor V6 (frequência de combustão = rpm/60 · 3),
// turbo, chiado de pneu e vento. Não depende de arquivos de áudio.

export class CarAudio {
  constructor() {
    this.ctx = null;
    this.muted = false;
  }

  start() {
    if (this.ctx) return;
    const ctx = this.ctx = new AudioContext();
    const master = this.master = ctx.createGain();
    master.gain.value = 0.55;
    const comp = ctx.createDynamicsCompressor();
    master.connect(comp).connect(ctx.destination);

    const noiseBuf = ctx.createBuffer(1, ctx.sampleRate * 2, ctx.sampleRate);
    const d = noiseBuf.getChannelData(0);
    for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
    const noise = () => { const n = ctx.createBufferSource(); n.buffer = noiseBuf; n.loop = true; n.start(); return n; };

    // motor: harmônicos + distorção + filtro passa-baixa dependente de carga
    this.engineGain = ctx.createGain();
    this.engineGain.gain.value = 0;
    this.shaper = ctx.createWaveShaper();
    this.shaper.curve = distortionCurve(6);
    this.engineFilter = ctx.createBiquadFilter();
    this.engineFilter.type = 'lowpass';
    this.engineFilter.Q.value = 2.5;
    const mix = ctx.createGain();
    mix.connect(this.shaper).connect(this.engineFilter).connect(this.engineGain).connect(master);
    this.oscs = [
      { type: 'sawtooth', mult: 1, gain: 0.5 },
      { type: 'sawtooth', mult: 2, gain: 0.22 },
      { type: 'square', mult: 0.5, gain: 0.18 },
      { type: 'triangle', mult: 3, gain: 0.12 },
      { type: 'sawtooth', mult: 1.005, gain: 0.25 },
    ].map((o) => {
      const osc = ctx.createOscillator();
      osc.type = o.type;
      const g = ctx.createGain();
      g.gain.value = o.gain;
      osc.connect(g).connect(mix);
      osc.start();
      return { osc, mult: o.mult };
    });
    // ronco/aspereza do escape
    this.rasp = ctx.createBiquadFilter();
    this.rasp.type = 'bandpass';
    this.rasp.Q.value = 1.5;
    this.raspGain = ctx.createGain();
    this.raspGain.gain.value = 0;
    noise().connect(this.rasp).connect(this.raspGain).connect(mix);

    // assobio do turbo
    this.turbo = ctx.createOscillator();
    this.turbo.type = 'sine';
    this.turboGain = ctx.createGain();
    this.turboGain.gain.value = 0;
    this.turbo.connect(this.turboGain).connect(master);
    this.turbo.start();

    // pneu
    this.tireFilter = ctx.createBiquadFilter();
    this.tireFilter.type = 'bandpass';
    this.tireFilter.frequency.value = 1100;
    this.tireFilter.Q.value = 6;
    this.tireGain = ctx.createGain();
    this.tireGain.gain.value = 0;
    noise().connect(this.tireFilter).connect(this.tireGain).connect(master);

    // vento e rolagem
    this.windFilter = ctx.createBiquadFilter();
    this.windFilter.type = 'lowpass';
    this.windFilter.frequency.value = 500;
    this.windGain = ctx.createGain();
    this.windGain.gain.value = 0;
    noise().connect(this.windFilter).connect(this.windGain).connect(master);
  }

  // silencia ao voltar para a garagem (sem perder o contexto de áudio)
  setPaused(p) {
    if (!this.ctx) return;
    if (p) this.ctx.suspend(); else this.ctx.resume();
  }

  toggleMute() {
    this.muted = !this.muted;
    if (this.master) this.master.gain.setTargetAtTime(this.muted ? 0 : 0.55, this.ctx.currentTime, 0.05);
  }

  update(car) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime, k = 0.03;
    const rpm = Math.max(300, car.engine.rpm);
    const f = rpm / 60 * ((car.spec.engine.cylinders || 6) / 2); // quatro tempos: cilindros/2 combustões por volta
    for (const o of this.oscs) o.osc.frequency.setTargetAtTime(f * o.mult, t, 0.01);
    const load = car.engine.throttle;
    const limiter = car.engine.limiter ? 0.55 + 0.45 * Math.sin(t * 90) : 1;
    this.engineGain.gain.setTargetAtTime((0.12 + 0.28 * load + rpm / car.spec.engine.limiterRpm * 0.12) * limiter, t, k);
    this.engineFilter.frequency.setTargetAtTime(500 + load * 2600 + rpm * 0.35, t, k);
    this.rasp.frequency.setTargetAtTime(f * 3.5, t, k);
    this.raspGain.gain.setTargetAtTime(0.15 + 0.5 * load, t, k);

    this.turbo.frequency.setTargetAtTime(2500 + car.engine.boost * 4500, t, 0.05);
    this.turboGain.gain.setTargetAtTime(car.spec.engine.turboShare > 0 ? car.engine.boost * 0.018 : 0, t, 0.05);

    let slip = 0;
    for (const w of car.wheels) if (w.contact) slip = Math.max(slip, Math.min(1, Math.max(0, w.slip - 0.85)) * Math.min(1, w.fz / 3000));
    const spd = car.speed;
    this.tireGain.gain.setTargetAtTime(spd > 1.5 ? slip * 0.35 : 0, t, 0.04);
    this.tireFilter.frequency.setTargetAtTime(800 + slip * 600, t, 0.05);
    this.windGain.gain.setTargetAtTime(Math.min(0.45, spd * spd * 0.00006), t, 0.1);
    this.windFilter.frequency.setTargetAtTime(300 + spd * 12, t, 0.1);
  }
}

function distortionCurve(amount) {
  const n = 1024, c = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const x = (i * 2) / n - 1;
    c[i] = ((1 + amount) * x) / (1 + amount * Math.abs(x));
  }
  return c;
}
