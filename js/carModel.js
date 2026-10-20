// Carrega o GLB, coloca o carro no referencial padrão (frente = −Z, cima = +Y, chão em y = 0,
// centro do entre-eixos na origem, escala em metros) e separa as rodas em pivôs de esterço/giro.
// Os GLB do Sketchfab vêm com orientação/escala arbitrárias e as rodas misturadas na carroceria,
// então tudo é descoberto pela geometria, a partir dos nomes de material dados em `cfg`
// (ver `visual` em cars.js). Padrões: 'nome' = exato, 'prefixo*' = começa com.

import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';

const matches = (name, patterns = []) => !!name && patterns.some((p) => (p.endsWith('*') ? name.startsWith(p.slice(0, -1)) : name === p));

export async function loadCar(url, cfg, onProgress) {
  const gltf = await new GLTFLoader().loadAsync(url, (e) => {
    if (e.lengthComputable && onProgress) onProgress(e.loaded / e.total);
  });
  const model = gltf.scene;
  model.updateMatrixWorld(true);

  const meshes = [];
  model.traverse((o) => { if (o.isMesh) meshes.push(o); });
  // várias malhas do GLB têm a ordem dos vértices invertida em relação às normais; com material
  // de dupla face o three inverte a normal e a pintura vira espelho (Fresnel). Corrige a ordem.
  let fixed = 0;
  for (const m of meshes) if (fixWinding(m.geometry)) fixed++;
  console.debug(`[carModel] winding corrigido em ${fixed} de ${meshes.length} malhas`);
  const matName = (m) => (Array.isArray(m.material) ? m.material[0] : m.material)?.name;
  const centerOf = (o) => new THREE.Box3().setFromObject(o, true).getCenter(new THREE.Vector3());

  const has = (m, key) => matches(matName(m), cfg[key]);

  // modelos com as rodas esquerda+direita na mesma malha: divide pelo eixo lateral (x do modelo)
  if (cfg.splitWheels) {
    for (const m of [...meshes]) {
      if (!has(m, 'splitWheels')) continue;
      const halves = splitByX(m);
      if (!halves) continue;
      meshes.splice(meshes.indexOf(m), 1, ...halves);
    }
    model.updateMatrixWorld(true);
  }

  // --- 1. pneus e eixos
  const tires = meshes.filter((m) => has(m, 'tires'));
  if (tires.length !== 4) throw new Error(`Esperava 4 pneus, encontrei ${tires.length}`);
  const tc = tires.map(centerOf);
  // pares mais próximos = eixos
  let best = null;
  for (const [a, b, c, d] of [[0, 1, 2, 3], [0, 2, 1, 3], [0, 3, 1, 2]]) {
    const s = tc[a].distanceTo(tc[b]) + tc[c].distanceTo(tc[d]);
    if (!best || s < best.s) best = { s, axles: [[a, b], [c, d]] };
  }
  const mid = ([a, b]) => tc[a].clone().add(tc[b]).multiplyScalar(0.5);
  let [axA, axB] = best.axles;
  const lights = meshes.filter((m) => has(m, cfg.frontHint ? 'frontHint' : 'headlights')).map(centerOf);
  const lightPos = lights.length ? lights[0] : mid(axA);
  if (mid(axB).distanceTo(lightPos) < mid(axA).distanceTo(lightPos)) [axA, axB] = [axB, axA];
  const frontAxle = axA, rearAxle = axB;

  // --- 2. base ortonormal do carro no espaço do modelo
  const fwd = mid(frontAxle).sub(mid(rearAxle)).normalize();
  const lat = tc[frontAxle[1]].clone().sub(tc[frontAxle[0]]);
  let up = new THREE.Vector3().crossVectors(lat, fwd).normalize();
  if (up.y < 0) up.negate();
  const right = new THREE.Vector3().crossVectors(fwd, up).normalize();
  up = new THREE.Vector3().crossVectors(right, fwd).normalize();
  // matriz canônico → modelo tem colunas (right, up, −fwd); a inversa leva o modelo ao canônico
  const basis = new THREE.Matrix4().makeBasis(right, up, fwd.clone().negate());
  const toCanon = basis.clone().invert();

  const root = new THREE.Group();
  root.name = 'car-visual';
  const oriented = new THREE.Group();
  oriented.add(model);
  oriented.quaternion.setFromRotationMatrix(toCanon);
  root.add(oriented);
  root.updateMatrixWorld(true);

  // escala: o entre-eixos medido tem de bater com o do carro real
  if (cfg.wheelbase) {
    const c = tires.map((t) => new THREE.Box3().setFromObject(t, true).getCenter(new THREE.Vector3()));
    const wbNow = c[frontAxle[0]].clone().add(c[frontAxle[1]]).sub(c[rearAxle[0]]).sub(c[rearAxle[1]]).length() / 2;
    oriented.scale.setScalar(cfg.wheelbase / wbNow);
    root.updateMatrixWorld(true);
  }

  // --- 3. medidas no referencial canônico
  const tireBoxes = tires.map((t) => new THREE.Box3().setFromObject(t, true));
  const tireCenters = tireBoxes.map((b) => b.getCenter(new THREE.Vector3()));
  const radii = tireBoxes.map((b) => b.getSize(new THREE.Vector3()).y / 2);
  const groundY = Math.min(...tireBoxes.map((b) => b.min.y));
  const fMid = tireCenters[frontAxle[0]].clone().add(tireCenters[frontAxle[1]]).multiplyScalar(0.5);
  const rMid = tireCenters[rearAxle[0]].clone().add(tireCenters[rearAxle[1]]).multiplyScalar(0.5);
  const center = fMid.clone().add(rMid).multiplyScalar(0.5);
  oriented.position.set(-center.x, -groundY, -center.z);
  root.updateMatrixWorld(true);
  for (const c of tireCenters) c.add(oriented.position);

  const geometry = {
    wheelbase: fMid.distanceTo(rMid),
    trackFront: tireCenters[frontAxle[0]].distanceTo(tireCenters[frontAxle[1]]),
    trackRear: tireCenters[rearAxle[0]].distanceTo(tireCenters[rearAxle[1]]),
    radiusFront: (radii[frontAxle[0]] + radii[frontAxle[1]]) / 2,
    radiusRear: (radii[rearAxle[0]] + radii[rearAxle[1]]) / 2,
  };

  // --- 4. separa peças de cada roda
  const wheelIds = {};
  for (const i of [...frontAxle, ...rearAxle]) {
    const front = frontAxle.includes(i);
    const left = tireCenters[i].x < 0;
    wheelIds[i] = (front ? 'F' : 'R') + (left ? 'L' : 'R');
  }
  const pivots = {};
  for (const i of [0, 1, 2, 3]) {
    const steer = new THREE.Group();
    steer.position.copy(tireCenters[i]);
    const spin = new THREE.Group();
    steer.add(spin);
    root.add(steer);
    pivots[wheelIds[i]] = { steer, spin, base: tireCenters[i].clone(), radius: radii[i] };
  }
  root.updateMatrixWorld(true);

  const tmp = new THREE.Vector3();
  const report = [];
  for (const m of meshes) {
    const box = new THREE.Box3().setFromObject(m, true);
    const size = box.getSize(new THREE.Vector3());
    const c = box.getCenter(new THREE.Vector3());
    let owner = -1;
    for (const i of [0, 1, 2, 3]) {
      const d = tireCenters[i];
      const r = radii[i];
      if (Math.max(size.y, size.z) > r * 2.25 || size.x > r * 1.6) continue;
      if (Math.abs(c.y - d.y) < r * 0.9 && Math.abs(c.z - d.z) < r * 0.9 && Math.abs(c.x - d.x) < r * 0.9) { owner = i; break; }
    }
    if (owner < 0) continue;
    const p = pivots[wheelIds[owner]];
    // peças que cobrem a volta inteira em torno do eixo giram (pneu, aro, disco);
    // as demais (pinça de freio, suporte) só esterçam.
    const off = c.clone().sub(tireCenters[owner]);
    const rotating = Math.hypot(off.y, off.z) < 0.03 || angularCoverage(m, tireCenters[owner], tmp) > 0.8;
    if (!rotating) {
      // peça fixa só acompanha o esterço se estiver dentro do aro (evita levar junto pedaços do para-lama)
      const d = c.clone().sub(tireCenters[owner]);
      if (Math.hypot(d.y, d.z) > p.radius * 0.6 || Math.abs(d.x) > p.radius * 0.5) continue;
    }
    (rotating ? p.spin : p.steer).attach(m);
    report.push(`${wheelIds[owner]} ${rotating ? 'gira ' : 'fixa '} ${m.name} (${matName(m)})`);
  }
  console.debug('[carModel] peças das rodas:\n' + report.join('\n'));

  // --- brilho do disco de freio. Cada roda ganha seu próprio material para acender conforme a
  // temperatura do disco: ou um quad de brilho que o modelo já traz ('brakeGlow'), ou o próprio
  // material do disco ('discs') ficando emissivo.
  for (const p of Object.values(pivots)) {
    p.glow = [];
    p.blur = [];
    p.steer.traverse((o) => {
      if (!o.isMesh) return;
      if (has(o, 'brakeGlow')) {
        const m = o.material.clone();
        m.transparent = true;
        m.opacity = 0;
        m.depthWrite = false;
        m.blending = THREE.AdditiveBlending;
        m.emissive = new THREE.Color(1, 0.38, 0.08);
        m.emissiveMap = m.map;
        m.emissiveIntensity = 0;
        m.color.setScalar(0);
        m.userData.glowQuad = true;
        o.material = m;
        o.castShadow = false;
        o.userData.noShadow = true;
        p.glow.push(m);
      } else if (has(o, 'discs')) {
        const m = o.material.clone();
        m.emissive = new THREE.Color(1, 0.3, 0.05);
        m.emissiveIntensity = 0;
        o.material = m;
        p.glow.push(m);
      } else if (has(o, 'rimBlur')) {
        // disco de "roda borrada" do modelo: aparece com a roda girando rápido
        const m = o.material.clone();
        m.transparent = true;
        m.opacity = 0;
        m.depthWrite = false;
        o.material = m;
        o.userData.noShadow = true;
        p.blur.push(m);
      }
    });
  }

  // --- lanternas traseiras (faixas finas na traseira): acendem mais forte ao frear
  const tailLights = [];
  for (const m of meshes) {
    if (!has(m, 'tailLights')) continue;
    if (!tailLights.includes(m.material)) tailLights.push(m.material);
  }
  for (const m of tailLights) {
    m.emissive = new THREE.Color(1, 0.06, 0.03);
    if (cfg.tailLightUseMap && m.map) { m.emissive.setRGB(1, 0.25, 0.2); m.emissiveMap = m.map; }
    m.emissiveIntensity = 0.6;
    m.toneMapped = true;
  }
  // faróis acesos (carro de corrida anda com farol ligado)
  if (cfg.headlightGlow) {
    const done = new Set();
    for (const m of meshes) {
      if (!has(m, 'headlights') || done.has(m.material)) continue;
      const mt = m.material;
      done.add(mt);
      mt.emissive = new THREE.Color(1, 0.97, 0.9);
      if (mt.map) mt.emissiveMap = mt.map;
      mt.emissiveIntensity = cfg.headlightGlow;
    }
  }

  // --- volante: pivô no centro, girando em torno do eixo da coluna (direção de menor variância dos vértices)
  let steeringWheel = null;
  const swMeshes = meshes.filter((m) => has(m, 'steeringWheel') || (cfg.steeringWheelNodes || []).includes(m.name));
  if (swMeshes.length) {
    const box = new THREE.Box3();
    for (const m of swMeshes) box.expandByObject(m, true);
    const center = box.getCenter(new THREE.Vector3());
    let axis = columnAxis(swMeshes, center);
    if (axis.z < 0) axis.negate(); // aponta para o piloto
    // volantes com botões/borboletas confundem a análise; usa coluna típica de GT3 (~18°)
    if (axis.z < 0.75) axis = new THREE.Vector3(0, Math.sin(0.31), Math.cos(0.31));
    const pivot = new THREE.Group();
    pivot.position.copy(center);
    root.add(pivot);
    root.updateMatrixWorld(true);
    for (const m of swMeshes) pivot.attach(m);
    steeringWheel = { pivot, axis };
  }

  // camadas da carroceria (malhas do tamanho do carro) não podem ser metal puro
  const bodyMats = new Set();
  if (cfg.fixBodyMetal) for (const m of meshes) {
    const s = new THREE.Box3().setFromObject(m, true).getSize(new THREE.Vector3());
    if (s.z > 3 && m.material.metalness > 0.5 && !has(m, 'glass')) bodyMats.add(m.material);
  }
  // pintura sólida (a decoração branca/vermelha está na textura): sem metal, verniz por cima
  for (const mt of bodyMats) {
    mt.metalness = 0;
    mt.roughness = 0.38;
    if (mt.specularIntensity !== undefined) mt.specularIntensity = 1;
  }
  const tuned = new Set();

  root.traverse((o) => {
    if (o.isMesh) {
      o.castShadow = !o.userData.noShadow;
      o.receiveShadow = true;
      const mats = Array.isArray(o.material) ? o.material : [o.material];
      for (const mt of mats) {
        if (!mt || tuned.has(mt)) continue;
        tuned.add(mt);
        mt.envMapIntensity = 1.0;
        // vidros: o autor do modelo pintou o material de verde (0, 0.13, 0). O policarbonato do
        // GT3 real é incolor: vidro neutro levemente fumê, liso, com reflexo do céu.
        if (matches(mt.name, cfg.glass)) {
          mt.color.setRGB(0.02, 0.022, 0.024);
          mt.opacity = 0.2;
          mt.metalness = 0;
          mt.roughness = 0.03;
          if (mt.isMeshPhysicalMaterial) { mt.clearcoat = 1; mt.clearcoatRoughness = 0.02; }
          continue;
        }
        // pintura: verniz (clearcoat) liso, como carro de pista recém-polido
        if (mt.isMeshPhysicalMaterial && mt.clearcoat > 0) {
          // o glTF deixa metalness = 1 quando não informado; tinta metálica real é base parcial + verniz
          if (!bodyMats.has(mt)) { mt.metalness = Math.min(mt.metalness, 0.45); mt.roughness = Math.min(mt.roughness, 0.42); }
          mt.clearcoat = 0.85;
          mt.clearcoatRoughness = Math.min(mt.clearcoatRoughness, 0.06);
        }
        if (matches(mt.name, cfg.tires)) { mt.metalness = 0; mt.roughness = 0.82; }
      }
      // pintura sem verniz no arquivo: troca por material físico com clearcoat
      for (let k = 0; k < mats.length; k++) {
        const mt = mats[k];
        if (!mt || mt.isMeshPhysicalMaterial || !matches(mt.name, cfg.paint)) continue;
        o.material = paintFor(mt);
      }
    }
  });

  // posição do olho do piloto (cockpit) e da cabeça/capacete a esconder nessa câmera
  let cockpit = cfg.cockpit;
  if (!cockpit && steeringWheel) {
    const c = steeringWheel.pivot.position;
    cockpit = { eye: [c.x, c.y + 0.29, c.z + 0.40], helmet: [c.x, c.y + 0.26, c.z + 0.58] };
  }

  // câmera de capô: acha a superfície do capô com um raio vindo de cima, à frente do para-brisa
  let hoodEye = cfg.hoodEye;
  if (!hoodEye && steeringWheel) {
    const z = steeringWheel.pivot.position.z - 0.7;
    root.updateMatrixWorld(true);
    const hit = new THREE.Raycaster(new THREE.Vector3(0, 3, z), new THREE.Vector3(0, -1, 0), 0, 3)
      .intersectObject(root, true).find((h) => !matches(matName(h.object), cfg.glass));
    if (hit) hoodEye = [0, hit.point.y + 0.14, z];
  }

  return { root, geometry, pivots, tailLights, steeringWheel, cockpit, hoodEye, attribution: gltf.asset?.extras };
}

const paintCache = new Map();
function paintFor(std) {
  if (paintCache.has(std)) return paintCache.get(std);
  const p = new THREE.MeshPhysicalMaterial();
  THREE.MeshStandardMaterial.prototype.copy.call(p, std);
  p.clearcoat = 1;
  p.clearcoatRoughness = 0.03;
  p.metalness = Math.min(p.metalness, 0.35);
  p.roughness = Math.max(p.roughness, 0.25);
  paintCache.set(std, p);
  return p;
}

// Eixo normal ao plano do volante: autovetor de menor variância (iteração de potência em tr(C)·I − C).
function columnAxis(meshes, center) {
  const C = [0, 0, 0, 0, 0, 0, 0, 0, 0];
  const v = new THREE.Vector3();
  let n = 0;
  for (const m of meshes) {
    const pos = m.geometry.attributes.position;
    for (let i = 0; i < pos.count; i++) {
      v.fromBufferAttribute(pos, i).applyMatrix4(m.matrixWorld).sub(center);
      const a = [v.x, v.y, v.z];
      for (let r = 0; r < 3; r++) for (let c = 0; c < 3; c++) C[r * 3 + c] += a[r] * a[c];
      n++;
    }
  }
  const tr = (C[0] + C[4] + C[8]) / n;
  const M = C.map((x, i) => (i % 4 === 0 ? tr : 0) - x / n);
  let x = new THREE.Vector3(0.1, 0.3, 1).normalize();
  for (let k = 0; k < 60; k++) {
    x = new THREE.Vector3(M[0] * x.x + M[1] * x.y + M[2] * x.z, M[3] * x.x + M[4] * x.y + M[5] * x.z, M[6] * x.x + M[7] * x.y + M[8] * x.z).normalize();
  }
  return x;
}

// Divide uma malha em metade esquerda/direita (pelo centróide de cada triângulo no x do mundo).
// Devolve as duas malhas novas (já no lugar da original) ou null se tudo estiver de um lado só.
function splitByX(mesh) {
  const geo = mesh.geometry, pos = geo.attributes.position;
  const idx = geo.index ? geo.index.array : [...Array(pos.count).keys()];
  const wm = mesh.matrixWorld, v = new THREE.Vector3();
  const side = [[], []];
  for (let t = 0; t < idx.length; t += 3) {
    let x = 0;
    for (let k = 0; k < 3; k++) x += v.fromBufferAttribute(pos, idx[t + k]).applyMatrix4(wm).x;
    side[x < 0 ? 0 : 1].push(idx[t], idx[t + 1], idx[t + 2]);
  }
  if (!side[0].length || !side[1].length) return null;
  const out = side.map((tri, i) => {
    // compacta: só os vértices usados por esta metade
    const remap = new Map(), order = [];
    const newIdx = tri.map((o) => { if (!remap.has(o)) { remap.set(o, order.length); order.push(o); } return remap.get(o); });
    const g = new THREE.BufferGeometry();
    for (const [name, attr] of Object.entries(geo.attributes)) {
      const n = attr.itemSize, src = attr.array, dst = new src.constructor(order.length * n);
      order.forEach((o, j) => { for (let c = 0; c < n; c++) dst[j * n + c] = src[o * n + c]; });
      g.setAttribute(name, new THREE.BufferAttribute(dst, n, attr.normalized));
    }
    g.setIndex(newIdx);
    const m = new THREE.Mesh(g, mesh.material);
    m.name = `${mesh.name}_${i ? 'R' : 'L'}`;
    m.matrix.copy(mesh.matrix);
    m.matrix.decompose(m.position, m.quaternion, m.scale);
    mesh.parent.add(m);
    return m;
  });
  mesh.parent.remove(mesh);
  geo.dispose();
  return out;
}

// Inverte a ordem dos triângulos se a maioria contradiz as normais dos vértices.
function fixWinding(geo) {
  const pos = geo.attributes.position, nrm = geo.attributes.normal;
  if (!nrm) return false;
  const idx = geo.index;
  const tris = idx ? idx.count / 3 : pos.count / 3;
  const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3(), n = new THREE.Vector3(), t = new THREE.Vector3();
  const vi = (k) => (idx ? idx.getX(k) : k);
  let agree = 0, disagree = 0;
  const step = Math.max(1, Math.floor(tris / 3000));
  for (let f = 0; f < tris; f += step) {
    const i0 = vi(f * 3), i1 = vi(f * 3 + 1), i2 = vi(f * 3 + 2);
    a.fromBufferAttribute(pos, i0); b.fromBufferAttribute(pos, i1); c.fromBufferAttribute(pos, i2);
    t.subVectors(c, b).cross(n.subVectors(a, b)); // (c−b)×(a−b) = normal geométrica com sinal do winding
    n.fromBufferAttribute(nrm, i0);
    a.fromBufferAttribute(nrm, i1); n.add(a);
    a.fromBufferAttribute(nrm, i2); n.add(a);
    const d = t.dot(n);
    if (d > 0) agree++; else if (d < 0) disagree++;
  }
  if (disagree <= agree * 1.5) return false;
  if (idx) {
    const arr = idx.array;
    for (let k = 0; k < arr.length; k += 3) { const s = arr[k + 1]; arr[k + 1] = arr[k + 2]; arr[k + 2] = s; }
    idx.needsUpdate = true;
  } else {
    geo.setIndex([...Array(pos.count).keys()].map((k) => (k % 3 === 1 ? k + 1 : k % 3 === 2 ? k - 1 : k)));
  }
  return true;
}

// Fração de setores angulares (em torno do eixo X da roda) ocupados pelos vértices da malha.
function angularCoverage(mesh, wheelCenter, tmp) {
  const pos = mesh.geometry.attributes.position;
  const bins = new Uint8Array(24);
  const step = Math.max(1, Math.floor(pos.count / 4000));
  for (let i = 0; i < pos.count; i += step) {
    tmp.fromBufferAttribute(pos, i).applyMatrix4(mesh.matrixWorld).sub(wheelCenter);
    if (Math.hypot(tmp.y, tmp.z) < 0.04) continue;
    const a = Math.atan2(tmp.y, tmp.z);
    bins[Math.floor(((a + Math.PI) / (2 * Math.PI)) * 24) % 24] = 1;
  }
  return bins.reduce((s, b) => s + b, 0) / 24;
}
