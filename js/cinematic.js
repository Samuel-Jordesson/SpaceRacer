// Cinemática do carro na garagem: sequência de planos com cortes, como num comercial automotivo.
// Linguagem visual: "angle study" das marcas (¾ frontal na altura da cintura, ⅞, perfil, frente,
// ¾ traseira), closes de detalhe (roda/pinça, farol, asa, escapamento), movimentos de câmera de
// cinema (slider rente ao chão, grua, push-in, revelação, órbita aérea), lente longa no perfil,
// profundidade de campo rasa e tela 2,39:1 com barras. Rodas dianteiras viradas ~16° para mostrar
// o aro. Qualquer tecla ou clique sai.

import * as THREE from 'three';
import { BokehPass } from 'three/addons/postprocessing/BokehPass.js';

const V = (x, y, z) => new THREE.Vector3(x, y, z);
const BARS_MS = 2600; // tempo das barras pretas entrando/saindo (igual ao CSS)
// movimento contínuo, velocidade constante: o plano nunca desacelera, é cortado em movimento
const linear = (t) => t;

export class Cinematic {
  constructor(garage) {
    this.garage = garage;
    this.el = {
      root: document.getElementById('cine'),
      title: document.getElementById('cine-title'),
      maker: document.getElementById('cine-maker'),
      name: document.getElementById('cine-name'),
      dip: document.getElementById('cine-dip'),
      hint: document.getElementById('cine-hint'),
    };
    this.active = false;
    this.onExit = null;
    this._exit = (e) => {
      if (!this.active || this.closing || performance.now() - this.startedAt < 400) return;
      e.preventDefault();
      e.stopImmediatePropagation();
      // engole o clique que vem logo depois do pointerdown
      if (e.type === 'pointerdown') addEventListener('click', (c) => { c.stopImmediatePropagation(); c.preventDefault(); }, { capture: true, once: true });
      this.stop();
    };
  }

  start(entry, loaded) {
    const g = this.garage;
    this.loaded = loaded;
    this.saved = { pos: g.camera.position.clone(), target: g.controls.target.clone(), fov: g.camera.fov, auto: g.controls.autoRotate };
    g.controls.enabled = false;
    g.controls.autoRotate = false;

    // profundidade de campo só durante a cinemática
    if (!this.bokeh || this.bokehComposer !== g.fx.composer) {
      this.bokeh = new BokehPass(g.scene, g.camera, { focus: 5, aperture: 0.004, maxblur: 0.008 });
      const passes = g.fx.composer.passes;
      g.fx.composer.insertPass(this.bokeh, passes.length - 2); // antes do OutputPass
      this.bokehComposer = g.fx.composer;
    }
    this.bokeh.enabled = true;

    // rodas da frente viradas para mostrar o aro
    for (const id of ['FL', 'FR']) if (loaded.pivots[id]) loaded.pivots[id].steer.rotation.y = 0.28;

    this.shots = buildShots(loaded);
    this.index = 0;
    this.t = 0;
    this.loops = 0;
    this.el.maker.textContent = entry.maker;
    this.el.name.textContent = entry.name;
    this.closing = false;
    this.el.root.classList.remove('closing');
    this.el.root.classList.add('show');
    this.el.hint.classList.add('show');
    clearTimeout(this._hintT);
    this._hintT = setTimeout(() => this.el.hint.classList.remove('show'), 3500);
    this.beginShot();
    this.active = true;
    this.startedAt = performance.now();
    addEventListener('keydown', this._exit, true);
    addEventListener('pointerdown', this._exit, true);
  }

  // sair: as barras recolhem devagar com o filme ainda rodando; só depois volta para a garagem
  stop() {
    if (!this.active || this.closing) return;
    this.closing = true;
    removeEventListener('keydown', this._exit, true);
    removeEventListener('pointerdown', this._exit, true);
    this.el.root.classList.add('closing');
    this.el.title.classList.remove('show');
    this.el.hint.classList.remove('show');
    setTimeout(() => this.finish(), BARS_MS);
  }

  finish() {
    this.active = false;
    this.closing = false;
    const g = this.garage, s = this.saved;
    this.bokeh.enabled = false;
    g.camera.position.copy(s.pos);
    g.controls.target.copy(s.target);
    g.camera.fov = s.fov;
    g.camera.updateProjectionMatrix();
    g.controls.enabled = true;
    g.controls.autoRotate = s.auto;
    for (const id of ['FL', 'FR']) if (this.loaded.pivots[id]) this.loaded.pivots[id].steer.rotation.y = 0;
    this.el.root.classList.remove('show', 'closing');
    this.onExit?.();
  }

  beginShot() {
    const shot = this.shots[this.index];
    this.t = 0;
    // corte seco na maioria; "dip" rápido para preto em algumas viradas de bloco
    if (shot.dip) {
      this.el.dip.classList.remove('run');
      void this.el.dip.offsetWidth;
      this.el.dip.classList.add('run');
    }
    this.el.title.classList.toggle('show', !!shot.title);
  }

  update(dt) {
    if (!this.active) return;
    this.t += dt;
    let shot = this.shots[this.index];
    if (this.t >= shot.dur) {
      this.index = (this.index + 1) % this.shots.length;
      if (this.index === 0) this.loops++;
      this.beginShot();
      shot = this.shots[this.index];
    }
    const k = shot.ease(Math.min(1, this.t / shot.dur));
    const cam = this.garage.camera;
    const pos = new THREE.Vector3(), look = new THREE.Vector3();
    if (shot.orbit) {
      const o = shot.orbit;
      const a = o.a0 + (o.a1 - o.a0) * k;
      const r = o.r0 + (o.r1 - o.r0) * k;
      pos.set(o.c.x + Math.sin(a) * r, o.h0 + (o.h1 - o.h0) * k, o.c.z + Math.cos(a) * r);
      look.copy(o.c);
    } else {
      pos.lerpVectors(shot.from, shot.to, k);
      look.lerpVectors(shot.lookFrom, shot.lookTo ?? shot.lookFrom, k);
    }
    // micro-flutuação de câmera em estabilizador (deixa o movimento "orgânico")
    const tt = performance.now() / 1000;
    pos.x += Math.sin(tt * 0.9) * 0.006;
    pos.y += Math.sin(tt * 1.3 + 1.7) * 0.005;
    cam.position.copy(pos);
    cam.up.set(0, 1, 0);
    cam.lookAt(look);
    cam.fov = shot.fov0 + ((shot.fov1 ?? shot.fov0) - shot.fov0) * k;
    cam.updateProjectionMatrix();
    // foco no assunto do plano
    const u = this.bokeh.uniforms;
    u.focus.value = cam.position.distanceTo(shot.focusOn ?? look);
    u.aperture.value = shot.aperture ?? 0.0025;
    u.maxblur.value = 0.009;
  }
}

// Monta os planos a partir das medidas do carro (frente = −z, chão em y = 0).
function buildShots(loaded) {
  const box = new THREE.Box3().setFromObject(loaded.root, true);
  const size = box.getSize(new THREE.Vector3());
  const L = size.z, W = size.x, H = size.y;
  const zf = box.min.z, zr = box.max.z; // frente e traseira
  const wheel = (id) => loaded.pivots[id]?.base.clone() ?? V(0, 0.35, 0);
  const FL = wheel('FL'), RR = wheel('RR'), RL = wheel('RL');
  const eye = loaded.cockpit ? new THREE.Vector3().fromArray(loaded.cockpit.eye) : V(-0.3, 0.95, 0);

  return [
    // 1. ¾ frontal, altura da cintura, push-in lento (hero shot de abertura) + título
    { dur: 6.5, ease: linear, title: true, dip: true, fov0: 30, fov1: 28,
      from: V(-W * 2.9, 0.75, zf - 4.6), to: V(-W * 2.2, 0.65, zf - 3.4), lookFrom: V(0, H * 0.42, zf * 0.25), lookTo: V(0, H * 0.4, zf * 0.15), aperture: 0.0012 },
    // 2. close na roda dianteira (aro + pinça), arco curto, foco raso
    { dur: 5, ease: linear, fov0: 26,
      from: V(FL.x - 1.25, FL.y + 0.05, FL.z - 0.95), to: V(FL.x - 1.35, FL.y + 0.12, FL.z + 0.55), lookFrom: FL, aperture: 0.006 },
    // 3. detalhe do farol: slider lateral rente à frente
    { dur: 4.5, ease: linear, fov0: 24,
      from: V(-W * 0.75, H * 0.52, zf - 1.15), to: V(-W * 0.12, H * 0.56, zf - 1.3), lookFrom: V(-W * 0.34, H * 0.48, zf + 0.3), aperture: 0.005 },
    // 4. lateral rente ao chão percorrendo o carro inteiro (slider baixo)
    { dur: 6, ease: linear, fov0: 34,
      from: V(-W / 2 - 1.5, 0.22, zf - 0.4), to: V(-W / 2 - 1.5, 0.26, zr + 0.4), lookFrom: V(-W / 2, 0.45, zf + 0.6), lookTo: V(-W / 2, 0.45, zr - 0.6), aperture: 0.002 },
    // 5. ¾ traseira com grua subindo (revela a asa)
    { dur: 6, ease: linear, dip: true, fov0: 30,
      from: V(W * 2.1, 0.25, zr + 3.1), to: V(W * 2.3, 1.85, zr + 3.4), lookFrom: V(0, H * 0.45, zr * 0.35), lookTo: V(0, H * 0.5, zr * 0.3), aperture: 0.0012 },
    // 6. asa e difusor: travelling por trás
    { dur: 5, ease: linear, fov0: 27,
      from: V(-1.7, H * 0.95, zr + 1.7), to: V(1.7, H * 0.85, zr + 1.55), lookFrom: V(0, H * 0.8, zr - 0.25), aperture: 0.004 },
    // 7. escapamento / difusor de baixo
    { dur: 4, ease: linear, fov0: 28,
      from: V(0.9, 0.12, zr + 1.6), to: V(0.4, 0.18, zr + 1.05), lookFrom: V(0, 0.3, zr - 0.1), aperture: 0.006 },
    // 8. roda traseira de baixo, girando em volta
    { dur: 4.5, ease: linear, fov0: 26,
      from: V(RR.x + 1.15, 0.18, RR.z + 1.0), to: V(RR.x + 1.3, 0.3, RR.z - 0.55), lookFrom: RR, aperture: 0.006 },
    // 9. plano alto girando devagar (grua alta, ~50° de cima: mostra a silhueta sem "lavar" no softbox)
    { dur: 6, ease: linear, dip: true, fov0: 36,
      orbit: { c: V(0, 0.3, 0), a0: 0.35, a1: 1.15, r0: 6.2, r1: 5.8, h0: 4.6, h1: 4.1 } },
    // 10. espiando o cockpit pela janela do piloto
    { dur: 4.5, ease: linear, fov0: 30,
      from: V(eye.x - 1.25, eye.y + 0.08, eye.z + 0.7), to: V(eye.x - 1.3, eye.y + 0.12, eye.z - 0.35), lookFrom: V(eye.x + 0.2, eye.y - 0.15, eye.z - 0.2), aperture: 0.004 },
    // 11. perfil direto com lente longa (compressão), truck lento
    { dur: 5.5, ease: linear, fov0: 17,
      from: V(W / 2 + 10.5, 0.7, -1.4), to: V(W / 2 + 10.5, 0.7, 1.4), lookFrom: V(0, H * 0.42, -1.2), lookTo: V(0, H * 0.42, 1.2), aperture: 0.0008 },
    // 12. frente reta, câmera baixa recuando (revelação)
    { dur: 5.5, ease: linear, fov0: 30,
      from: V(0, 0.32, zf - 1.5), to: V(0, 0.5, zf - 4.6), lookFrom: V(0, H * 0.4, zf + 0.2), lookTo: V(0, H * 0.42, zf * 0.4), aperture: 0.002 },
    // 13. ⅞ traseira em órbita larga e alta, título de novo (fechamento)
    { dur: 7.5, ease: linear, title: true, dip: true, fov0: 32,
      orbit: { c: V(0, H * 0.35, 0), a0: 2.35, a1: 1.55, r0: 7.6, r1: 6.8, h0: 1.9, h1: 1.2 } },
  ].map((s) => ({ ...s, focusOn: s.orbit ? s.orbit.c : undefined }));
}
