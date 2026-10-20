// Câmeras: perseguição (com mola), capô, cockpit e livre (órbita com o mouse).

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

export class CameraRig {
  constructor(camera, dom) {
    this.camera = camera;
    this.modes = ['Perseguição', 'Perseguição longe', 'Capô', 'Cockpit', 'Livre'];
    this.mode = 0;
    this.orbit = new OrbitControls(camera, dom);
    this.orbit.enabled = false;
    this.orbit.enableDamping = true;
    this.orbit.maxPolarAngle = Math.PI * 0.495;
    this.orbit.minDistance = 2.5;
    this.orbit.maxDistance = 40;
    this.pos = new THREE.Vector3(0, 1.65, 5.6); // deslocamento da câmera em relação ao carro
    this.yaw = 0;
    this.head = new THREE.Vector3();
    // posições no referencial do modelo (frente = −z); cada carro define as suas
    this.cockpitEye = new THREE.Vector3(-0.33, 0.93, -0.14);
    this.hoodEye = new THREE.Vector3(0, 1.0, -0.75);
    this.t = 0;
    this.lastCar = new THREE.Vector3();
  }

  get name() { return this.modes[this.mode]; }

  next() {
    this.mode = (this.mode + 1) % this.modes.length;
    const free = this.name === 'Livre';
    this.orbit.enabled = free;
    if (free) this.orbit.target.copy(this.lastCar);
    this.camera.near = this.mode >= 2 && !free ? 0.03 : 0.1;
    // esconde o capacete do piloto na câmera de cockpit
    for (const m of this.hideInCockpit || []) m.visible = this.name !== 'Cockpit';
    this.camera.updateProjectionMatrix();
  }

  update(dt, carObj, car) {
    const cam = this.camera;
    const p = carObj.position;
    const q = carObj.quaternion;
    const fwd = new THREE.Vector3(0, 0, -1).applyQuaternion(q);
    const name = this.name;
    const vel = new THREE.Vector3(car.vel.x, car.vel.y, car.vel.z);

    if (name === 'Livre' || name.startsWith('Perseguição')) cam.up.set(0, 1, 0);
    if (name === 'Livre') {
      this.orbit.target.add(p.clone().sub(this.lastCar));
      cam.position.add(p.clone().sub(this.lastCar));
      this.orbit.update();
      cam.fov = 55;
    } else if (name.startsWith('Perseguição')) {
      const far = name.endsWith('longe');
      // a câmera acompanha a direção do movimento (com atraso), não a rotação crua do carro
      const heading = Math.atan2(-fwd.x, -fwd.z);
      const moveHeading = vel.length() > 3 ? Math.atan2(-vel.x, -vel.z) : heading;
      let target = moveHeading;
      if (car.forwardSpeed < -1) target = heading; // em ré, não gira a câmera
      let dy = target - this.yaw;
      dy = Math.atan2(Math.sin(dy), Math.cos(dy));
      this.yaw += dy * (1 - Math.exp(-dt * 4.5));
      const dist = far ? 8.5 : 5.6, height = far ? 2.6 : 1.65;
      const back = new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw));
      // suaviza o deslocamento relativo ao carro (sem ficar para trás em alta velocidade)
      const desired = back.clone().multiplyScalar(dist).add(new THREE.Vector3(0, height, 0));
      this.pos.lerp(desired, 1 - Math.exp(-dt * 10));
      cam.position.copy(p).add(this.pos);
      // vibração leve em alta velocidade
      const kmh = vel.length() * 3.6;
      if (kmh > 120) {
        this.t += dt;
        const amp = Math.min(1, (kmh - 120) / 160) * 0.012;
        cam.position.x += Math.sin(this.t * 37.1) * Math.sin(this.t * 11.3) * amp;
        cam.position.y += Math.sin(this.t * 43.7 + 1.3) * Math.sin(this.t * 7.9) * amp;
      }
      if (cam.position.y < 0.3) cam.position.y = 0.3;
      cam.lookAt(p.clone().add(new THREE.Vector3(0, 0.75, 0)).addScaledVector(back, -1.5));
      const speed = vel.length() * 3.6;
      cam.fov = 58 + Math.min(14, speed / 22);
    } else {
      // câmeras presas à carroceria (no referencial do modelo: frente = −z)
      const local = (name === 'Capô' ? this.hoodEye : this.cockpitEye).clone();
      if (name === 'Cockpit') {
        // a cabeça do piloto é jogada pelas forças G (pescoço = mola amortecida)
        const a = car.accel;
        this.head.lerp(new THREE.Vector3(-a.x * 0.0028, -a.y * 0.0012, -a.z * 0.0032), 1 - Math.exp(-dt * 7));
        local.add(this.head);
      }
      const visualRoot = carObj.children[0];
      const world = visualRoot.localToWorld(local.clone());
      cam.position.copy(world);
      const ahead = visualRoot.localToWorld(local.clone().add(new THREE.Vector3(0, -0.05, -10)));
      cam.up.set(0, 1, 0).applyQuaternion(q);
      cam.lookAt(ahead);
      cam.fov = name === 'Capô' ? 62 : 68;
    }
    cam.updateProjectionMatrix();
    this.lastCar.copy(p);
  }
}
