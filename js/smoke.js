// Fumaça de pneu: partículas suaves que nascem no contato quando o pneu desliza,
// herdam parte da velocidade do carro, sobem e se expandem.

import * as THREE from 'three';

const MAX = 700;

export class TireSmoke {
  constructor(scene) {
    this.p = new Float32Array(MAX * 3);
    this.v = new Float32Array(MAX * 3);
    this.age = new Float32Array(MAX).fill(1e9);
    this.life = new Float32Array(MAX).fill(1);
    this.size = new Float32Array(MAX);
    this.alpha = new Float32Array(MAX);
    this.seed = new Float32Array(MAX).map(() => Math.random());
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(this.p, 3).setUsage(THREE.DynamicDrawUsage));
    geo.setAttribute('size', new THREE.BufferAttribute(this.size, 1).setUsage(THREE.DynamicDrawUsage));
    geo.setAttribute('alpha', new THREE.BufferAttribute(this.alpha, 1).setUsage(THREE.DynamicDrawUsage));
    geo.setAttribute('seed', new THREE.BufferAttribute(this.seed, 1));
    this.mat = new THREE.ShaderMaterial({
      transparent: true, depthWrite: false, fog: true,
      uniforms: { ...THREE.UniformsUtils.clone(THREE.UniformsLib.fog), uScale: { value: 500 }, uMaxSize: { value: 300 }, uLight: { value: new THREE.Color(1, 0.97, 0.92) } },
      vertexShader: `
        attribute float size, alpha, seed; varying float vA, vSeed; uniform float uScale, uMaxSize;
        #include <common>
        #include <fog_pars_vertex>
        void main(){ vA = alpha; vSeed = seed; vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
          gl_PointSize = min(size * uScale / max(0.5, -mvPosition.z), uMaxSize); gl_Position = projectionMatrix * mvPosition;
          #include <fog_vertex>
        }`,
      fragmentShader: `
        varying float vA, vSeed; uniform vec3 uLight;
        #include <common>
        #include <fog_pars_fragment>
        float h(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
        float n(vec2 p){ vec2 i = floor(p), f = fract(p); f = f*f*(3.0-2.0*f);
          return mix(mix(h(i), h(i+vec2(1,0)), f.x), mix(h(i+vec2(0,1)), h(i+vec2(1,1)), f.x), f.y); }
        void main(){
          vec2 c = gl_PointCoord - 0.5; float r = length(c);
          float puff = n(c * 4.0 + vSeed * 17.0) * 0.6 + n(c * 9.0 - vSeed * 5.0) * 0.4;
          float a = smoothstep(0.5, 0.1, r) * (0.55 + 0.45 * puff) * vA;
          if (a < 0.003) discard;
          // iluminação simples: parte de cima mais clara (sol), de baixo mais escura
          vec3 col = uLight * mix(0.62, 0.95, 0.5 - c.y);
          gl_FragColor = vec4(col, a);
          #include <fog_fragment>
        }`,
    });
    this.points = new THREE.Points(geo, this.mat);
    this.points.frustumCulled = false;
    scene.add(this.points);
    this.next = 0;
    this.acc = new Map();
  }

  emit(id, pos, vel, intensity, dt) {
    if (intensity <= 0) return;
    const rate = 45 * intensity; // partículas por segundo por roda
    let a = (this.acc.get(id) || 0) + rate * dt;
    while (a >= 1) {
      a -= 1;
      const i = this.next;
      this.next = (this.next + 1) % MAX;
      this.p[i * 3] = pos.x + (Math.random() - 0.5) * 0.25;
      this.p[i * 3 + 1] = 0.15 + Math.random() * 0.1;
      this.p[i * 3 + 2] = pos.z + (Math.random() - 0.5) * 0.25;
      this.v[i * 3] = vel.x * 0.35 + (Math.random() - 0.5) * 1.5;
      this.v[i * 3 + 1] = 0.4 + Math.random() * 0.6;
      this.v[i * 3 + 2] = vel.z * 0.35 + (Math.random() - 0.5) * 1.5;
      this.age[i] = 0;
      this.life[i] = 1.6 + Math.random() * 1.8;
      this.alpha[i] = 0;
      this.size[i] = 0.4;
      this.seed[i] = Math.random();
    }
    this.acc.set(id, a);
  }

  update(dt, camera, viewportH) {
    this.mat.uniforms.uScale.value = viewportH / (2 * Math.tan(THREE.MathUtils.degToRad(camera.fov) / 2));
    this.mat.uniforms.uMaxSize.value = viewportH * 0.4;
    const drag = Math.exp(-dt * 1.8);
    for (let i = 0; i < MAX; i++) {
      const t = (this.age[i] += dt);
      const L = this.life[i];
      if (t > L) { this.alpha[i] = 0; continue; }
      const k = t / L;
      this.v[i * 3] *= drag; this.v[i * 3 + 2] *= drag;
      this.v[i * 3 + 1] = this.v[i * 3 + 1] * drag + 0.25 * dt;
      this.p[i * 3] += this.v[i * 3] * dt;
      this.p[i * 3 + 1] += this.v[i * 3 + 1] * dt;
      this.p[i * 3 + 2] += this.v[i * 3 + 2] * dt;
      this.size[i] = 0.5 + k * 3.6;
      this.alpha[i] = Math.min(1, t * 6) * Math.pow(1 - k, 1.6) * 0.42;
    }
    const g = this.points.geometry;
    g.attributes.position.needsUpdate = true;
    g.attributes.size.needsUpdate = true;
    g.attributes.alpha.needsUpdate = true;
    g.attributes.seed.needsUpdate = true;
  }

  clear() { this.age.fill(1e9); this.alpha.fill(0); }
}
