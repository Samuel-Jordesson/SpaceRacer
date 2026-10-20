// Modo pilotagem: mundo (céu HDRI + asfalto), física, câmeras, HUD, som e efeitos.

import * as THREE from 'three';
import { createWorld } from './world.js';
import { CameraRig } from './cameras.js';
import { Input } from './input.js';
import { Hud } from './hud.js';
import { CarAudio } from './audio.js';
import { SkidMarks } from './skidmarks.js';
import { TireSmoke } from './smoke.js';
import { PostFX } from './postfx.js';
import { Vehicle, FlatGround } from './physics/vehicle.js';
import { V3 } from './physics/math.js';

const PHYSICS_DT = 1 / 1000; // 1000 Hz, passo fixo

export class Game {
  constructor(renderer) {
    this.renderer = renderer;
    this.camera = new THREE.PerspectiveCamera(60, innerWidth / innerHeight, 0.1, 12000);
    this.rig = new CameraRig(this.camera, renderer.domElement);
    this.input = new Input();
    this.audio = new CarAudio();
    this.help = document.getElementById('help');
    this.hudEl = document.getElementById('hud');
    this.acc = 0;
    this.paused = false;
    this.brakeLight = 0;
    this.autoQ = { enabled: true, t: 0, frames: 0, drops: 0 };
    this.onExit = null;
  }

  // carrega o mundo uma única vez
  async prepare(onProgress) {
    if (this.world) return;
    this.world = await createWorld(this.renderer, onProgress);
    this.scene = this.world.scene;
    this.skids = new SkidMarks(this.scene);
    this.smoke = new TireSmoke(this.scene);
    this.fx = new PostFX(this.renderer, this.scene, this.camera);
    this.world.setShadowQuality(this.fx.quality.shadow);
    this.carObj = new THREE.Group();
    this.visual = new THREE.Group();
    this.carObj.add(this.visual);
    this.scene.add(this.carObj);
    await this.renderer.compileAsync(this.scene, this.camera);
  }

  start(entry, loaded) {
    const spec = entry.spec;
    this.entry = entry;
    this.loaded = loaded;
    this.hud = new Hud(spec);
    this.car = new Vehicle(spec, new FlatGround(), loaded.geometry);
    this.visual.position.set(0, -spec.cgHeight, loaded.geometry.wheelbase / 2 - this.car.a);
    this.visual.add(loaded.root);
    loaded.root.position.set(0, 0, 0);
    loaded.root.quaternion.identity();
    if (loaded.cockpit) this.rig.cockpitEye.fromArray(loaded.cockpit.eye);
    if (loaded.hoodEye) this.rig.hoodEye.fromArray(loaded.hoodEye);
    this.findHelmet();
    this.skids.clear();
    this.smoke.clear();
    this.syncVisuals(0);
    this.rig.mode = this.rig.modes.length - 1;
    this.rig.next(); // volta para a câmera de perseguição
    this.rig.pos.set(0, 1.65, 5.6);
    this.rig.yaw = 0;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 0.95;
    this.audio.start();
    this.audio.setPaused(false);
    this.hudEl.classList.add('show');
    this.running = true;
    this.paused = false;
    this.last = performance.now();
    this.fx.resize();
    this.input.pressed.clear();
    const a = loaded.attribution;
    document.getElementById('credit').innerHTML = a
      ? `Modelo: <a href="${a.source}" target="_blank" rel="noopener">${a.title}</a> por ${a.author} — ${a.license} · Céu e asfalto: Poly Haven (CC0)`
      : '';
  }

  stop() {
    this.running = false;
    this.audio.setPaused(true);
    this.hudEl.classList.remove('show');
    this.help.classList.remove('show');
    this.rig.orbit.enabled = false;
    for (const m of this.rig.hideInCockpit || []) m.visible = true;
    this.visual.remove(this.loaded.root);
  }

  // capacete/cabeça do piloto (malhas perto do olho do cockpit) somem na câmera de cockpit
  findHelmet() {
    this.rig.hideInCockpit = [];
    if (!this.loaded.cockpit) return;
    const eye = new THREE.Vector3().fromArray(this.loaded.cockpit.helmet), box = new THREE.Box3(), c = new THREE.Vector3(), s = new THREE.Vector3();
    this.carObj.position.set(0, 0, 0);
    this.carObj.quaternion.identity();
    this.visual.updateMatrixWorld(true);
    const inv = new THREE.Matrix4().copy(this.visual.matrixWorld).invert();
    this.loaded.root.traverse((o) => {
      if (!o.isMesh) return;
      box.setFromObject(o, true).applyMatrix4(inv);
      box.getCenter(c); box.getSize(s);
      if (c.distanceTo(eye) < 0.15 && Math.max(s.x, s.y, s.z) < 0.45) this.rig.hideInCockpit.push(o);
    });
  }

  resize() {
    this.camera.aspect = innerWidth / innerHeight;
    this.camera.updateProjectionMatrix();
    this.fx?.resize();
  }

  setQuality(level, auto = false) {
    const q = this.fx.setLevel(level);
    this.world.setShadowQuality(q.shadow);
    this.fx.resize();
    this.hud.flash(`Gráficos: ${q.name}${auto ? ' (ajuste automático)' : ''}`, 2);
  }

  resetCar() {
    const p = this.car.pos, f = new THREE.Vector3(0, 0, -1).applyQuaternion(this.carObj.quaternion);
    this.car.reset(new V3(p.x, 0, p.z), Math.atan2(-f.x, -f.z));
    this.skids.clear();
    this.smoke.clear();
  }

  handleKeys(ev) {
    const { car, input, hud } = this;
    if (ev.shiftUp) car.shiftUp();
    if (ev.shiftDown) car.shiftDown();
    if (input.take('Escape')) { this.onExit?.(); return; }
    if (input.take('KeyC')) this.rig.next();
    if (input.take('KeyR')) { this.resetCar(); hud.flash('Carro reposicionado'); }
    if (input.take('KeyM')) { car.assists.autoGearbox = !car.assists.autoGearbox; hud.flash(`Câmbio ${car.assists.autoGearbox ? 'automático' : 'manual'}`); }
    if (input.take('KeyB')) { car.assists.abs = !car.assists.abs; hud.flash(`ABS ${car.assists.abs ? 'ligado' : 'desligado'}`); }
    if (input.take('KeyT')) { car.assists.tc = !car.assists.tc; hud.flash(`Controle de tração ${car.assists.tc ? 'ligado' : 'desligado'}`); }
    if (input.take('KeyG')) { this.autoQ.enabled = false; this.setQuality(this.fx.level + 1); }
    if (input.take('KeyH')) this.help.classList.toggle('show');
    if (input.take('KeyN')) this.audio.toggleMute();
    if (input.take('KeyP')) { this.paused = !this.paused; hud.flash(this.paused ? 'Pausado' : 'Continuando'); }
  }

  // qualidade automática: se o fps ficar baixo, reduz um nível (até 2 vezes)
  autoQuality(dt) {
    const a = this.autoQ;
    if (!a.enabled) return;
    a.t += dt; a.frames++;
    if (a.t < 3) return;
    const fps = a.frames / a.t;
    a.t = 0; a.frames = 0;
    if (fps < 42 && this.fx.level > 0 && a.drops < 2) { a.drops++; this.setQuality(this.fx.level - 1, true); }
    else a.enabled = fps < 42 && a.drops < 2;
  }

  syncVisuals(dt) {
    const { car, carObj, loaded, input } = this;
    carObj.position.set(car.pos.x, car.pos.y, car.pos.z);
    carObj.quaternion.set(car.quat.x, car.quat.y, car.quat.z, car.quat.w);
    for (const w of car.wheels) {
      const pv = loaded.pivots[w.id];
      if (!pv) continue;
      pv.steer.position.y = pv.base.y - (w.x0 - w.compression);
      pv.steer.rotation.y = w.steer;
      pv.spin.rotation.x = -w.spin;
      // disco incandescente a partir de ~450 °C
      const glow = Math.min(1, Math.max(0, (w.brakeTemp - 450) / 450));
      for (const m of pv.glow) { if (m.userData.glowQuad) m.opacity = glow; m.emissiveIntensity = glow * 4; }
      // roda borrada aparece acima de ~25 rad/s (≈ 30 km/h)
      const blur = Math.min(0.85, Math.max(0, (Math.abs(w.omega) - 25) / 45));
      for (const m of pv.blur) m.opacity = blur;
    }
    if (loaded.steeringWheel) {
      const sw = loaded.steeringWheel;
      sw.pivot.quaternion.setFromAxisAngle(sw.axis, -input.steer * 3.8);
    }
    this.brakeLight += ((input.brake > 0.05 ? 1 : 0) - this.brakeLight) * Math.min(1, dt * 25);
    for (const m of loaded.tailLights) m.emissiveIntensity = 0.8 + this.brakeLight * 14;

    carObj.updateMatrixWorld(true);
    const right = new THREE.Vector3(1, 0, 0).applyQuaternion(carObj.quaternion);
    const spd = car.speed, vel = new THREE.Vector3(car.vel.x, car.vel.y, car.vel.z);
    for (const w of car.wheels) {
      const k = w.contact && spd > 1.5 ? Math.min(1, Math.max(0, (w.slip - 0.95) / 0.9)) * Math.min(1, w.fz / 3500) : 0;
      this.skids.add(w.id, w.contactPoint, right, k);
      const slipSpeed = Math.abs(w.omega * w.radius - car.forwardSpeed) + Math.abs(Math.sin(w.slipAngle)) * spd;
      const s = w.contact ? Math.min(1, Math.max(0, (w.slip - 1.2) / 1.2)) * Math.min(1, Math.max(0, (slipSpeed - 3) / 10)) : 0;
      this.smoke.emit(w.id, w.contactPoint, vel, s, dt);
    }
  }

  frame(dt) {
    const { input, car } = this;
    const ev = input.update(dt, car.speed * 3.6);
    this.handleKeys(ev);
    if (!this.running) { input.endFrame(); return; }

    if (!this.paused) {
      this.acc += dt;
      const controls = { throttle: input.throttle, brake: input.brake, steer: input.steer, handbrake: input.handbrake };
      while (this.acc >= PHYSICS_DT) {
        car.step(PHYSICS_DT, controls);
        this.acc -= PHYSICS_DT;
      }
    }
    this.syncVisuals(dt);
    this.rig.update(dt, this.carObj, car);
    this.world.follow(this.carObj.position);
    this.smoke.update(this.paused ? 0 : dt, this.camera, this.renderer.getDrawingBufferSize(new THREE.Vector2()).y);
    this.hud.update(car, input, this.rig.name, dt);
    this.audio.update(car);
    this.fx.render(dt, Math.min(1, Math.max(0, (car.speed * 3.6 - 140) / 160)));
    this.autoQuality(dt);
    input.endFrame();
  }
}
