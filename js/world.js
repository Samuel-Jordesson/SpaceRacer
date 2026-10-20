// Cena: céu HDRI real (Poly Haven, CC0), sol direcional alinhado com o sol da foto,
// reflexos por image-based lighting e asfalto PBR escaneado sem repetição visível.

import * as THREE from 'three';
import { RGBELoader } from 'three/addons/loaders/RGBELoader.js';

const HDRI_URL = 'assets/hdri/sky_4k.hdr';
const ASPHALT = {
  map: 'assets/asphalt/asphalt_02_diff.jpg',
  normal: 'assets/asphalt/asphalt_02_nor_gl.jpg',
  rough: 'assets/asphalt/asphalt_02_rough.jpg',
};
const ASPHALT_TILE = 3.2; // metros cobertos por uma repetição da textura

export async function createWorld(renderer, onProgress = () => {}) {
  const scene = new THREE.Scene();

  // --- céu HDRI
  const hdr = await new RGBELoader().setDataType(THREE.FloatType).loadAsync(HDRI_URL, (e) => {
    if (e.lengthComputable) onProgress(e.loaded / e.total);
  });
  hdr.mapping = THREE.EquirectangularReflectionMapping;
  const { sunDir, horizon, envTex } = analyzeSky(hdr);
  // o fundo vai para a GPU em meia precisão (metade da memória de vídeo)
  const half = new Uint16Array(hdr.image.data.length);
  for (let i = 0; i < half.length; i++) half[i] = THREE.DataUtils.toHalfFloat(Math.min(hdr.image.data[i], 65000));
  hdr.image.data = half;
  hdr.type = THREE.HalfFloatType;
  hdr.needsUpdate = true;
  scene.background = hdr;
  scene.backgroundIntensity = 1.0;

  // environment map sem o disco do sol (o sol vem da luz direcional, com sombra)
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromEquirectangular(envTex).texture;
  scene.environmentIntensity = 1.0;
  envTex.dispose();
  pmrem.dispose();

  scene.fog = new THREE.FogExp2(horizon, 0.00028);

  // --- sol
  const sun = new THREE.DirectionalLight(0xfff1dc, 6.0);
  sun.castShadow = true;
  sun.shadow.mapSize.set(4096, 4096);
  const sc = sun.shadow.camera;
  sc.left = -12; sc.right = 12; sc.top = 12; sc.bottom = -12; sc.near = 1; sc.far = 200;
  sun.shadow.bias = -0.00015;
  sun.shadow.normalBias = 0.025;
  sun.shadow.radius = 3;
  scene.add(sun, sun.target);

  // --- chão
  const ground = await createGround(renderer);
  scene.add(ground);

  return {
    scene,
    sun,
    sunDir,
    setShadowQuality(size) {
      sun.shadow.mapSize.set(size, size);
      if (sun.shadow.map) { sun.shadow.map.dispose(); sun.shadow.map = null; }
    },
    // a sombra acompanha o carro
    follow(p) {
      sun.target.position.set(p.x, 0, p.z);
      sun.position.set(p.x + sunDir.x * 80, sunDir.y * 80, p.z + sunDir.z * 80);
    },
  };
}

// Encontra o sol (pixel mais brilhante), a cor do horizonte, e cria uma cópia do HDRI
// com o sol limitado para a iluminação ambiente.
function analyzeSky(tex) {
  const { width: W, height: H, data } = tex.image;
  let best = 0, bi = 0;
  for (let i = 0; i < W * H; i++) {
    const l = data[i * 4] * 0.2126 + data[i * 4 + 1] * 0.7152 + data[i * 4 + 2] * 0.0722;
    if (l > best) { best = l; bi = i; }
  }
  const px = bi % W, py = Math.floor(bi / W);
  // RGBELoader usa flipY: linha 0 = topo da imagem
  const u = (px + 0.5) / W, v = 1 - (py + 0.5) / H;
  const lat = (v - 0.5) * Math.PI, phi = (u - 0.5) * 2 * Math.PI;
  const sunDir = new THREE.Vector3(Math.cos(phi) * Math.cos(lat), Math.sin(lat), Math.sin(phi) * Math.cos(lat)).normalize();
  if (sunDir.y < 0.15) { sunDir.y = 0.15; sunDir.normalize(); }

  // cor média logo acima do horizonte (para a neblina)
  const row = Math.floor(H * 0.47);
  const c = [0, 0, 0];
  for (let x = 0; x < W; x++) for (let k = 0; k < 3; k++) c[k] += data[(row * W + x) * 4 + k];
  const horizon = new THREE.Color(c[0] / W, c[1] / W, c[2] / W);
  const m = Math.max(horizon.r, horizon.g, horizon.b);
  if (m > 1) horizon.multiplyScalar(1 / m);

  // cópia em meia resolução (a iluminação ambiente é borrada de qualquer jeito)
  const w2 = W >> 1, h2 = H >> 1;
  const clamped = new Float32Array(w2 * h2 * 4);
  for (let y = 0; y < h2; y++) for (let x = 0; x < w2; x++) {
    const o = (y * w2 + x) * 4, a = ((y * 2) * W + x * 2) * 4, b = a + W * 4;
    for (let k = 0; k < 4; k++) clamped[o + k] = (data[a + k] + data[a + 4 + k] + data[b + k] + data[b + 4 + k]) / 4;
  }
  const LIMIT = 12;
  for (let i = 0; i < w2 * h2; i++) {
    const o = i * 4;
    const l = clamped[o] * 0.2126 + clamped[o + 1] * 0.7152 + clamped[o + 2] * 0.0722;
    if (l > LIMIT) { const s = LIMIT / l; clamped[o] *= s; clamped[o + 1] *= s; clamped[o + 2] *= s; }
  }
  // metade inferior do HDRI "puresky" não tem chão: preenche com tom de asfalto iluminado
  for (let y = Math.floor(h2 * 0.5); y < h2; y++) {
    const t = Math.min(1, (y - h2 * 0.5) / (h2 * 0.03));
    for (let x = 0; x < w2; x++) {
      const o = (y * w2 + x) * 4;
      clamped[o] = clamped[o] * (1 - t) + 0.16 * t;
      clamped[o + 1] = clamped[o + 1] * (1 - t) + 0.155 * t;
      clamped[o + 2] = clamped[o + 2] * (1 - t) + 0.15 * t;
    }
  }
  const envTex = new THREE.DataTexture(clamped, w2, h2, THREE.RGBAFormat, THREE.FloatType);
  envTex.mapping = THREE.EquirectangularReflectionMapping;
  envTex.flipY = tex.flipY;
  envTex.colorSpace = THREE.LinearSRGBColorSpace;
  envTex.needsUpdate = true;
  return { sunDir, horizon, envTex };
}

async function createGround(renderer) {
  const loader = new THREE.TextureLoader();
  const [map, normal, rough] = await Promise.all([ASPHALT.map, ASPHALT.normal, ASPHALT.rough].map((u) => loader.loadAsync(u)));
  const aniso = renderer.capabilities.getMaxAnisotropy();
  for (const t of [map, normal, rough]) {
    t.wrapS = t.wrapT = THREE.RepeatWrapping;
    t.anisotropy = aniso;
  }
  map.colorSpace = THREE.SRGBColorSpace;

  const extent = 12000;
  const geo = new THREE.PlaneGeometry(extent, extent).rotateX(-Math.PI / 2);
  // UV em metros; a escala é aplicada no shader
  const uv = geo.attributes.uv;
  for (let i = 0; i < uv.count; i++) uv.setXY(i, (uv.getX(i) - 0.5) * extent, (uv.getY(i) - 0.5) * extent);

  const mat = new THREE.MeshStandardMaterial({
    map, normalMap: normal, roughnessMap: rough,
    normalScale: new THREE.Vector2(1.2, 1.2),
    roughness: 1, metalness: 0, color: 0xb4b4b4,
  });
  mat.onBeforeCompile = (sh) => {
    sh.uniforms.uTile = { value: 1 / ASPHALT_TILE };
    sh.vertexShader = sh.vertexShader
      .replace('#include <common>', '#include <common>\nvarying vec2 vWorldUv;')
      .replace('#include <uv_vertex>', '#include <uv_vertex>\nvWorldUv = uv;');
    // anti-repetição: mistura duas escalas giradas da mesma textura + variação macro de cor e rugosidade
    sh.fragmentShader = sh.fragmentShader
      .replace('#include <common>', `#include <common>
        varying vec2 vWorldUv;
        uniform float uTile;
        float hash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * .1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
        float vnoise(vec2 p){ vec2 i = floor(p), f = fract(p); vec2 u = f*f*(3.0-2.0*f);
          return mix(mix(hash12(i), hash12(i+vec2(1,0)), u.x), mix(hash12(i+vec2(0,1)), hash12(i+vec2(1,1)), u.x), u.y); }
        float fbm(vec2 p){ float s = 0.0, a = 0.5; for(int i=0;i<4;i++){ s += a*vnoise(p); p *= 2.03; a *= 0.5; } return s; }
        vec2 uvA(){ return vWorldUv * uTile; }
        vec2 uvB(){ mat2 r = mat2(0.788, -0.616, 0.616, 0.788); return r * vWorldUv * uTile * 0.37 + 0.31; }
        float blendW(){ return smoothstep(0.35, 0.65, fbm(vWorldUv * 0.045)); }`)
      .replace('#include <map_fragment>', `
        vec4 cA = texture2D(map, uvA()), cB = texture2D(map, uvB());
        vec4 sampledDiffuseColor = mix(cA, cB, blendW() * 0.6);
        float macro = fbm(vWorldUv * 0.012);
        float patches = smoothstep(0.55, 0.75, fbm(vWorldUv * 0.03 + 7.0));
        sampledDiffuseColor.rgb *= mix(0.82, 1.12, macro) * mix(1.0, 0.86, patches);
        diffuseColor *= sampledDiffuseColor;`)
      .replace('#include <roughnessmap_fragment>', `
        float roughnessFactor = roughness;
        float rA = texture2D(roughnessMap, uvA()).g, rB = texture2D(roughnessMap, uvB()).g;
        roughnessFactor *= clamp(mix(rA, rB, blendW() * 0.6) * mix(0.85, 1.05, fbm(vWorldUv * 0.02 + 3.0)), 0.35, 1.0);`)
      .replace('#include <normal_fragment_maps>', `
        vec3 mapN = texture2D(normalMap, uvA()).xyz * 2.0 - 1.0;
        mapN.xy *= normalScale;
        normal = normalize(tbn * mapN);`);
  };
  const plane = new THREE.Mesh(geo, mat);
  plane.receiveShadow = true;
  return plane;
}
