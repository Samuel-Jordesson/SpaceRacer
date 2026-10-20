// Garagem: estúdio branco infinito com piso de porcelanato polido (reflexo planar real),
// iluminação de estúdio (softboxes via RoomEnvironment) e o carro no centro.

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { Reflector } from 'three/addons/objects/Reflector.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { PostFX } from './postfx.js';

const BG = 0xf3f3f2;

export class Garage {
  constructor(renderer) {
    this.renderer = renderer;
    const scene = this.scene = new THREE.Scene();
    scene.background = new THREE.Color(BG);
    scene.fog = new THREE.Fog(BG, 14, 38);

    const pmrem = new THREE.PMREMGenerator(renderer);
    scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.03).texture;
    scene.environmentIntensity = 1.0;
    pmrem.dispose();

    // luz principal de cima (sombra suave) + preenchimento
    const key = new THREE.DirectionalLight(0xffffff, 2.2);
    key.position.set(2, 12, 3);
    key.castShadow = true;
    key.shadow.mapSize.set(2048, 2048);
    Object.assign(key.shadow.camera, { left: -5, right: 5, top: 5, bottom: -5, near: 1, far: 30 });
    key.shadow.radius = 8;
    key.shadow.blurSamples = 16;
    key.shadow.bias = -0.0003;
    scene.add(key);
    scene.add(new THREE.HemisphereLight(0xffffff, 0xe8e8e8, 0.6));

    // piso: espelho (reflexo planar) + camada de porcelanato branco semitransparente por cima
    const size = 80;
    const mirror = this.mirror = new Reflector(new THREE.PlaneGeometry(size, size), {
      textureWidth: Math.floor(innerWidth * 0.75), textureHeight: Math.floor(innerHeight * 0.75), color: 0xd0d0d0, multisample: 4,
    });
    mirror.rotation.x = -Math.PI / 2;
    mirror.position.y = -0.001;
    scene.add(mirror);
    const tile = porcelainTexture(renderer);
    const floor = new THREE.Mesh(
      new THREE.PlaneGeometry(size, size).rotateX(-Math.PI / 2),
      new THREE.MeshPhysicalMaterial({
        color: 0xffffff, map: tile, roughness: 0.18, metalness: 0, clearcoat: 1, clearcoatRoughness: 0.05,
        transparent: true, opacity: 0.62,
      }),
    );
    tile.repeat.set(size / 1.2, size / 1.2); // placas de 120 × 120 cm
    floor.receiveShadow = true;
    scene.add(floor);

    // base giratória sutil
    const disc = new THREE.Mesh(
      new THREE.RingGeometry(3.15, 3.2, 128).rotateX(-Math.PI / 2),
      new THREE.MeshBasicMaterial({ color: 0xd9d9d9, transparent: true, opacity: 0.8 }),
    );
    disc.position.y = 0.002;
    scene.add(disc);

    // o piso e o anel ficam acima do espelho: some com eles enquanto o reflexo é renderizado
    const renderMirror = mirror.onBeforeRender;
    mirror.onBeforeRender = (...args) => {
      floor.visible = disc.visible = false;
      renderMirror(...args);
      floor.visible = disc.visible = true;
    };

    this.turntable = new THREE.Group();
    scene.add(this.turntable);

    const cam = this.camera = new THREE.PerspectiveCamera(32, innerWidth / innerHeight, 0.1, 200);
    cam.position.set(-6.2, 2.0, -5.4);
    const ctl = this.controls = new OrbitControls(cam, renderer.domElement);
    ctl.target.set(0, 0.55, 0);
    ctl.enableDamping = true;
    ctl.dampingFactor = 0.06;
    ctl.enablePan = false;
    ctl.minDistance = 5.2;
    ctl.maxDistance = 10.5;
    ctl.minPolarAngle = Math.PI * 0.28;
    ctl.maxPolarAngle = Math.PI * 0.485;
    ctl.autoRotate = true;
    ctl.autoRotateSpeed = 0.6;
    ctl.addEventListener('start', () => { ctl.autoRotate = false; clearTimeout(this._resume); });
    ctl.addEventListener('end', () => { this._resume = setTimeout(() => { ctl.autoRotate = true; }, 4000); });

    this.fx = new PostFX(renderer, scene, cam);
    const u = this.fx.final.uniforms;
    u.uVignette.value = 0.1;
    u.uTint.value = 0;
    u.uContrast.value = 1.03;
    u.uSaturation.value = 1.05;
    this.intro = 1;
    this.car = null;
    this.baseDist = 7.2;
  }

  setActive(on) {
    this.active = on;
    this.controls.enabled = on;
    if (on) {
      // tone mapping "PBR Neutral": brancos ficam brancos, cores fiéis (fotografia de produto)
      this.renderer.toneMapping = THREE.NeutralToneMapping;
      this.renderer.toneMappingExposure = 1.0;
      this.resize();
    }
  }

  // coloca o carro (root já normalizado: chão em y = 0, centro do entre-eixos na origem)
  showCar(loaded) {
    if (this.car) this.turntable.remove(this.car.root);
    this.car = loaded;
    resetPose(loaded);
    this.turntable.add(loaded.root);
    this.intro = 0; // animação de entrada
  }

  // devolve o carro para a garagem depois de pilotar
  reclaimCar() {
    if (!this.car) return;
    resetPose(this.car);
    this.turntable.add(this.car.root);
  }

  resize() {
    const aspect = this.camera.aspect = innerWidth / innerHeight;
    this.camera.updateProjectionMatrix();
    // telas estreitas (celular em pé) precisam de mais distância para o carro caber
    const d = this.baseDist = 7.2 * Math.max(1, 1.2 / aspect);
    this.controls.minDistance = d * 0.72;
    this.controls.maxDistance = d * 1.45;
    this.scene.fog.near = d + 7;
    this.scene.fog.far = d + 31;
    this.fx.resize();
    this.mirror.getRenderTarget().setSize(Math.floor(innerWidth * 0.75), Math.floor(innerHeight * 0.75));
  }

  frame(dt) {
    if (this.cine?.active) {
      this.cine.update(dt);
      this.fx.render(dt, 0);
      return;
    }
    // entrada do carro: câmera se aproxima suavemente
    if (this.intro < 1) {
      this.intro = Math.min(1, this.intro + dt / 1.6);
      const e = 1 - Math.pow(1 - this.intro, 3);
      const dir = this.camera.position.clone().sub(this.controls.target).normalize();
      this.camera.position.copy(this.controls.target).addScaledVector(dir, this.baseDist + 5.8 * (1 - e));
    }
    this.controls.update(dt);
    this.fx.render(dt, 0);
  }
}

function resetPose(loaded) {
  loaded.root.position.set(0, 0, 0);
  loaded.root.quaternion.identity();
  for (const p of Object.values(loaded.pivots)) {
    p.steer.position.copy(p.base);
    p.steer.rotation.set(0, 0, 0);
    p.spin.rotation.set(0, 0, 0);
    for (const m of p.glow) { if (m.userData.glowQuad) m.opacity = 0; m.emissiveIntensity = 0; }
    for (const m of p.blur) m.opacity = 0;
  }
  if (loaded.steeringWheel) loaded.steeringWheel.pivot.quaternion.identity();
  for (const m of loaded.tailLights) m.emissiveIntensity = 0.8;
}

// placa de porcelanato: branco com veios muito sutis e rejunte fino
function porcelainTexture(renderer) {
  const s = 1024, c = document.createElement('canvas');
  c.width = c.height = s;
  const g = c.getContext('2d');
  g.fillStyle = '#fbfbfa';
  g.fillRect(0, 0, s, s);
  const img = g.getImageData(0, 0, s, s);
  for (let i = 0; i < s * s; i++) {
    const x = i % s, y = (i / s) | 0;
    const vein = Math.sin(x * 0.011 + Math.sin(y * 0.007) * 3 + Math.sin((x + y) * 0.003) * 4);
    const v = (Math.abs(vein) < 0.03 ? -6 : 0) + (Math.random() - 0.5) * 3;
    img.data[i * 4] += v; img.data[i * 4 + 1] += v; img.data[i * 4 + 2] += v;
  }
  g.putImageData(img, 0, 0);
  g.strokeStyle = 'rgba(190,190,188,0.9)';
  g.lineWidth = 3;
  g.strokeRect(0, 0, s, s);
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = renderer.capabilities.getMaxAnisotropy();
  return t;
}
