// HUD: velocidade, marcha, conta-giros com luzes de troca, pedais, medidor de G,
// estado dos pneus (carga e escorregamento) e auxílios.

const $ = (id) => document.getElementById(id);

export class Hud {
  constructor(spec) {
    this.spec = spec;
    this.el = {
      speed: $('speed'), gear: $('gear'), rpm: $('rpm-fill'), rpmText: $('rpm-text'), lights: $('shift-lights'),
      thr: $('thr'), brk: $('brk'), clutch: $('clu'), steer: $('steer-ind'),
      gcanvas: $('gmeter'), tires: [...document.querySelectorAll('.tire')],
      abs: $('a-abs'), tc: $('a-tc'), gearbox: $('a-gb'), cam: $('cam-name'), fps: $('fps'), msg: $('msg'), input: $('input-src'),
    };
    this.lights = [];
    this.el.lights.innerHTML = '';
    for (let i = 0; i < 10; i++) {
      const s = document.createElement('span');
      this.el.lights.appendChild(s);
      this.lights.push(s);
    }
    this.g = this.el.gcanvas.getContext('2d');
    this.trail = [];
    this.slow = 0;
    this.frames = 0; this.fpsT = 0;
    this.msgT = 0;
  }

  flash(text, sec = 1.5) { this.el.msg.textContent = text; this.el.msg.classList.add('show'); this.msgT = sec; }

  update(car, input, camName, dt) {
    const e = this.el, es = this.spec.engine;
    const rpm = car.engine.rpm;
    e.speed.textContent = Math.round(Math.abs(car.forwardSpeed) * 3.6);
    e.gear.textContent = car.gear === 0 ? 'N' : car.gear < 0 ? 'R' : car.gear;
    e.gear.classList.toggle('shifting', !!car.shift);
    e.rpm.style.transform = `scaleX(${Math.min(1, rpm / (es.limiterRpm + 500))})`;
    e.rpmText.textContent = Math.round(rpm / 10) * 10;
    const start = es.limiterRpm - 1600;
    const lit = Math.max(0, Math.min(10, Math.floor(((rpm - start) / 1450) * 10)));
    const blink = rpm > es.limiterRpm - 150 && Math.floor(performance.now() / 70) % 2;
    this.lights.forEach((s, i) => { s.className = blink ? 'blink' : i < lit ? (i < 4 ? 'g' : i < 8 ? 'r' : 'b') : ''; });
    e.thr.style.transform = `scaleY(${input.throttle})`;
    e.brk.style.transform = `scaleY(${input.brake})`;
    e.clutch.style.transform = `scaleY(${1 - car.clutch})`;
    e.steer.style.transform = `rotate(${input.steer * 270}deg)`;

    if (this.msgT > 0) { this.msgT -= dt; if (this.msgT <= 0) e.msg.classList.remove('show'); }
    if (car.overrevBlocked > 0.55) this.flash('Redução bloqueada: rotação excessiva', 0.8);

    // atualizações mais lentas (~20 Hz)
    this.slow += dt;
    this.frames++; this.fpsT += dt;
    if (this.fpsT > 0.5) { e.fps.textContent = `${Math.round(this.frames / this.fpsT)} fps`; this.frames = 0; this.fpsT = 0; }
    if (this.slow < 0.05) return;
    this.slow = 0;
    e.abs.className = car.assists.abs ? (car.absActive ? 'on act' : 'on') : '';
    e.tc.className = car.assists.tc ? (car.tcActive ? 'on act' : 'on') : '';
    e.gearbox.textContent = car.assists.autoGearbox ? 'AUTO' : 'MANUAL';
    e.cam.textContent = camName;
    e.input.textContent = input.source;

    car.wheels.forEach((w, i) => {
      const t = e.tires[i];
      if (!t) return;
      const load = Math.min(1, w.fz / 7000);
      const slip = Math.min(1.6, w.slip);
      // verde = aderindo, amarelo = no pico, vermelho = deslizando, azul = travada
      let c;
      if (!w.contact) c = '#333';
      else if (w.slipRatio < -0.25 && Math.abs(w.omega) < 1) c = '#3aa0ff';
      else if (slip < 0.75) c = `hsl(130 70% ${30 + load * 25}%)`;
      else if (slip < 1.1) c = `hsl(50 90% ${40 + load * 15}%)`;
      else c = `hsl(0 85% ${40 + load * 15}%)`;
      t.style.background = c;
      t.style.transform = `scaleY(${0.55 + load * 0.45})`;
    });

    // medidor de G (aceleração no referencial do carro)
    const g = this.g, W = e.gcanvas.width, H = e.gcanvas.height, cx = W / 2, cy = H / 2, R = W / 2 - 6, scale = R / 2.5;
    const gx = car.accel.x / 9.81, gz = car.accel.z / 9.81;
    this.trail.push([gx, gz]);
    if (this.trail.length > 40) this.trail.shift();
    g.clearRect(0, 0, W, H);
    g.strokeStyle = 'rgba(255,255,255,0.18)';
    g.lineWidth = 1;
    for (const r of [1, 2]) { g.beginPath(); g.arc(cx, cy, r * scale, 0, Math.PI * 2); g.stroke(); }
    g.beginPath(); g.moveTo(cx - R, cy); g.lineTo(cx + R, cy); g.moveTo(cx, cy - R); g.lineTo(cx, cy + R); g.stroke();
    g.fillStyle = 'rgba(255,80,60,0.25)';
    for (const [x, z] of this.trail) { g.beginPath(); g.arc(cx + x * scale, cy - z * scale, 2, 0, Math.PI * 2); g.fill(); }
    g.fillStyle = '#ff5a3c';
    g.beginPath(); g.arc(cx + gx * scale, cy - gz * scale, 4.5, 0, Math.PI * 2); g.fill();
    g.fillStyle = 'rgba(255,255,255,0.7)';
    g.font = '11px ui-monospace, monospace';
    g.fillText(`${Math.hypot(gx, gz).toFixed(2)} g`, 6, H - 6);
  }
}
