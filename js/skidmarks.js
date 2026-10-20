// Marcas de pneu no asfalto (buffer circular de quads).

import * as THREE from 'three';

const MAX = 6000;

export class SkidMarks {
  constructor(scene) {
    const geo = new THREE.BufferGeometry();
    this.pos = new Float32Array(MAX * 4 * 3);
    this.alpha = new Float32Array(MAX * 4);
    const idx = new Uint32Array(MAX * 6);
    for (let i = 0; i < MAX; i++) {
      const v = i * 4;
      idx.set([v, v + 1, v + 2, v + 2, v + 1, v + 3], i * 6);
    }
    geo.setAttribute('position', new THREE.BufferAttribute(this.pos, 3).setUsage(THREE.DynamicDrawUsage));
    geo.setAttribute('alpha', new THREE.BufferAttribute(this.alpha, 1).setUsage(THREE.DynamicDrawUsage));
    geo.setIndex(new THREE.BufferAttribute(idx, 1));
    const mat = new THREE.ShaderMaterial({
      transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2,
      vertexShader: `attribute float alpha; varying float vA;
        #include <common>
        #include <fog_pars_vertex>
        void main(){ vA = alpha; vec4 mvPosition = modelViewMatrix * vec4(position,1.0); gl_Position = projectionMatrix * mvPosition;
        #include <fog_vertex>
        }`,
      fragmentShader: `varying float vA;
        #include <common>
        #include <fog_pars_fragment>
        void main(){ gl_FragColor = vec4(0.03,0.03,0.03, vA*0.75);
        #include <fog_fragment>
        }`,
      fog: true,
      uniforms: THREE.UniformsUtils.clone(THREE.UniformsLib.fog),
    });
    this.mesh = new THREE.Mesh(geo, mat);
    this.mesh.frustumCulled = false;
    scene.add(this.mesh);
    this.next = 0;
    this.last = new Map();
  }

  // id da roda, ponto de contato, direção lateral (vetor) e intensidade 0..1
  add(id, p, side, intensity, width = 0.3) {
    const prev = this.last.get(id);
    if (intensity <= 0.02) { this.last.delete(id); return; }
    const l = { x: p.x + side.x * width / 2, z: p.z + side.z * width / 2, xr: p.x - side.x * width / 2, zr: p.z - side.z * width / 2, a: intensity };
    if (!prev) { this.last.set(id, l); return; }
    const dx = l.x - prev.x, dz = l.z - prev.z;
    if (dx * dx + dz * dz < 0.09) return;
    if (dx * dx + dz * dz > 25) { this.last.set(id, l); return; }
    const i = this.next;
    this.next = (this.next + 1) % MAX;
    const y = 0.006;
    this.pos.set([prev.x, y, prev.z, prev.xr, y, prev.zr, l.x, y, l.z, l.xr, y, l.zr], i * 12);
    this.alpha.set([prev.a, prev.a, l.a, l.a], i * 4);
    const g = this.mesh.geometry;
    g.attributes.position.addUpdateRange(i * 12, 12);
    g.attributes.alpha.addUpdateRange(i * 4, 4);
    g.attributes.position.needsUpdate = true;
    g.attributes.alpha.needsUpdate = true;
    this.last.set(id, l);
  }

  clear() {
    this.alpha.fill(0);
    const a = this.mesh.geometry.attributes.alpha;
    a.clearUpdateRanges();
    a.needsUpdate = true;
    this.last.clear();
  }
}
