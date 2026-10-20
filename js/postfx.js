// Pós-processamento estilo console: render HDR com MSAA → oclusão de ambiente (GTAO) → bloom →
// tone mapping ACES → gradação de cor, vinheta, aberração cromática, blur radial de velocidade e grão.

import * as THREE from 'three';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { N8AOPass } from 'n8ao';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { ShaderPass } from 'three/addons/postprocessing/ShaderPass.js';

export const QUALITY = [
  { name: 'Baixo', pixelRatio: 0.85, msaa: 0, ao: false, bloom: false, shadow: 1024 },
  { name: 'Médio', pixelRatio: 1.0, msaa: 2, ao: false, bloom: true, shadow: 2048 },
  { name: 'Alto', pixelRatio: 1.25, msaa: 4, ao: true, aoSamples: 12, aoHalfRes: true, bloom: true, shadow: 4096 },
  { name: 'Ultra', pixelRatio: 2.0, msaa: 4, ao: true, aoSamples: 16, aoHalfRes: false, bloom: true, shadow: 4096 },
];

const FinalShader = {
  uniforms: {
    tDiffuse: { value: null },
    uTime: { value: 0 },
    uSpeed: { value: 0 },      // 0..1
    uResolution: { value: new THREE.Vector2(1, 1) },
    uSaturation: { value: 1.08 },
    uContrast: { value: 1.06 },
    uVignette: { value: 0.32 },
    uFxaa: { value: 0 },
    uTint: { value: 1 },       // tom quente/frio da gradação (0 = neutro)
  },
  vertexShader: `varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
  fragmentShader: `
    uniform sampler2D tDiffuse; uniform float uTime, uSpeed, uSaturation, uContrast, uVignette, uFxaa, uTint; uniform vec2 uResolution;
    varying vec2 vUv;
    float luma(vec3 c){ return dot(c, vec3(0.299, 0.587, 0.114)); }
    vec3 fxaa(vec2 uv){
      vec2 px = 1.0 / uResolution;
      vec3 m = texture2D(tDiffuse, uv).rgb;
      float lN = luma(texture2D(tDiffuse, uv + vec2(0, px.y)).rgb), lS = luma(texture2D(tDiffuse, uv - vec2(0, px.y)).rgb);
      float lE = luma(texture2D(tDiffuse, uv + vec2(px.x, 0)).rgb), lW = luma(texture2D(tDiffuse, uv - vec2(px.x, 0)).rgb);
      float lM = luma(m), mn = min(lM, min(min(lN, lS), min(lE, lW))), mx = max(lM, max(max(lN, lS), max(lE, lW)));
      if (mx - mn < max(0.04, mx * 0.12)) return m;
      vec2 dir = normalize(vec2(-(lN - lS), lE - lW) + 1e-5) * px;
      return 0.5 * (texture2D(tDiffuse, uv + dir * 0.5).rgb + texture2D(tDiffuse, uv - dir * 0.5).rgb);
    }
    void main(){
      vec2 uv = vUv, c = uv - 0.5;
      float r = length(c);
      // aberração cromática sutil nas bordas
      vec2 ca = c * r * 0.0035 * (1.0 + uSpeed * 1.5);
      vec3 col;
      if (uFxaa > 0.5) col = fxaa(uv);
      else col = vec3(texture2D(tDiffuse, uv - ca).r, texture2D(tDiffuse, uv).g, texture2D(tDiffuse, uv + ca).b);
      // blur radial de velocidade (só nas bordas, aumenta com a velocidade)
      float sb = uSpeed * smoothstep(0.18, 0.7, r) * 0.03;
      if (sb > 0.0005) {
        vec3 acc = col;
        for (int i = 1; i <= 6; i++) acc += texture2D(tDiffuse, uv - c * sb * float(i)).rgb;
        col = acc / 7.0;
      }
      // gradação: contraste, saturação e leve tom quente nos realces / frio nas sombras
      col = (col - 0.5) * uContrast + 0.5;
      float l = luma(col);
      col = mix(vec3(l), col, uSaturation);
      col *= mix(vec3(1.0), mix(vec3(0.97, 0.99, 1.04), vec3(1.03, 1.0, 0.96), smoothstep(0.2, 0.8, l)), uTint);
      // vinheta
      col *= 1.0 - uVignette * smoothstep(0.35, 0.85, r);
      // grão de filme
      float n = fract(sin(dot(uv * uResolution + uTime * 61.0, vec2(12.9898, 78.233))) * 43758.5453);
      col += (n - 0.5) * 0.018;
      gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
    }`,
};

export class PostFX {
  constructor(renderer, scene, camera) {
    this.renderer = renderer;
    this.scene = scene;
    this.camera = camera;
    this.level = 2;
    this.build();
  }

  get quality() { return QUALITY[this.level]; }

  build() {
    const q = this.quality, r = this.renderer;
    this.composer?.dispose();
    this.ao?.dispose?.();
    r.setPixelRatio(Math.min(devicePixelRatio, q.pixelRatio));
    const size = r.getDrawingBufferSize(new THREE.Vector2());
    const rt = new THREE.WebGLRenderTarget(size.x, size.y, { type: THREE.HalfFloatType, samples: q.msaa });
    const composer = this.composer = new EffectComposer(r, rt);
    composer.setPixelRatio(1);
    composer.setSize(size.x, size.y);
    this.ao = null;
    if (!q.ao) composer.addPass(new RenderPass(this.scene, this.camera));
    else {
      // N8AO renderiza a cena e aplica a oclusão de ambiente (substitui o RenderPass)
      const ao = this.ao = new N8AOPass(this.scene, this.camera, size.x, size.y);
      const c = ao.configuration;
      c.aoRadius = 1.2;
      c.distanceFalloff = 0.6;
      c.intensity = 2.2;
      c.aoSamples = q.aoSamples;
      c.denoiseSamples = 8;
      c.denoiseRadius = 8;
      c.halfRes = q.aoHalfRes;
      c.depthAwareUpsampling = true;
      c.gammaCorrection = false; // o OutputPass faz tone mapping + sRGB depois
      c.color = new THREE.Color(0, 0, 0);
      // MSAA na imagem principal
      ao.beautyRenderTarget.samples = q.msaa;
      composer.addPass(ao);
    }
    if (q.bloom) {
      this.bloom = new UnrealBloomPass(new THREE.Vector2(size.x / 2, size.y / 2), 0.22, 0.55, 1.6);
      composer.addPass(this.bloom);
    }
    composer.addPass(new OutputPass());
    this.final = new ShaderPass(FinalShader);
    this.final.uniforms.uResolution.value.copy(size);
    this.final.uniforms.uFxaa.value = q.msaa === 0 ? 1 : 0;
    composer.addPass(this.final);
  }

  setLevel(level) {
    this.level = (level + QUALITY.length) % QUALITY.length;
    this.build();
    return this.quality;
  }

  resize() {
    const r = this.renderer;
    r.setPixelRatio(Math.min(devicePixelRatio, this.quality.pixelRatio));
    r.setSize(innerWidth, innerHeight);
    const size = r.getDrawingBufferSize(new THREE.Vector2());
    this.composer.setSize(size.x, size.y);
    this.final.uniforms.uResolution.value.copy(size);
  }

  render(dt, speed01) {
    const u = this.final.uniforms;
    u.uTime.value = (u.uTime.value + dt) % 100;
    u.uSpeed.value += (speed01 - u.uSpeed.value) * Math.min(1, dt * 3);
    this.composer.render(dt);
  }
}
