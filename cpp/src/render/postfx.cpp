#include "postfx.h"

#include <cmath>
#include <string>

const Quality QUALITY[4] = {
    {"Baixo", 0.85f, 0, false, 0, false, false, 1024},
    {"Médio", 1.0f, 2, false, 0, false, true, 2048},
    {"Alto", 1.25f, 4, true, 12, true, true, 4096},
    {"Ultra", 2.0f, 4, true, 16, false, true, 4096},
};

namespace {

// ---------------------------------------------------------------- oclusão de ambiente
const char* AO_FS = R"(
in vec2 vUv;
uniform sampler2D tDepth;
uniform mat4 uProj, uInvProj;
uniform vec2 uSize;        // resolução da AO
uniform int uSamples;
uniform float uRadius, uFalloff;
out vec4 fragColor;
vec3 viewPos(vec2 uv) {
  float d = textureLod(tDepth, uv, 0.0).r;
  vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  return p.xyz / p.w;
}
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
void main() {
  float d = textureLod(tDepth, vUv, 0.0).r;
  if (d >= 1.0) { fragColor = vec4(1.0); return; }
  vec3 P = viewPos(vUv);
  // normal reconstruída escolhendo o vizinho mais próximo em profundidade em cada eixo
  vec2 px = 1.0 / uSize;
  vec3 pr = viewPos(vUv + vec2(px.x, 0)), pl = viewPos(vUv - vec2(px.x, 0));
  vec3 pu = viewPos(vUv + vec2(0, px.y)), pd = viewPos(vUv - vec2(0, px.y));
  vec3 dx = abs(pr.z - P.z) < abs(pl.z - P.z) ? pr - P : P - pl;
  vec3 dy = abs(pu.z - P.z) < abs(pd.z - P.z) ? pu - P : P - pd;
  vec3 N = normalize(cross(dx, dy));
  float rnd = ign(gl_FragCoord.xy) * 6.2831853;
  vec3 rv = vec3(cos(rnd), sin(rnd), 0.0);
  vec3 T = normalize(rv - N * dot(rv, N));
  vec3 B = cross(N, T);
  float occ = 0.0;
  for (int i = 0; i < 32; i++) {
    if (i >= uSamples) break;
    float fi = float(i) + 0.5;
    // espiral de Fibonacci no hemisfério, mais amostras perto do ponto
    float z = 1.0 - fi / float(uSamples);
    float r = sqrt(1.0 - z * z);
    float a = fi * 2.39996323 + rnd;
    vec3 h = vec3(cos(a) * r, sin(a) * r, z);
    float scale = mix(0.12, 1.0, pow(fi / float(uSamples), 2.0));
    vec3 S = P + (T * h.x + B * h.y + N * h.z) * uRadius * scale;
    vec4 c = uProj * vec4(S, 1.0);
    vec2 suv = c.xy / c.w * 0.5 + 0.5;
    if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
    float sz = viewPos(suv).z;
    float range = smoothstep(0.0, 1.0, uFalloff / abs(P.z - sz));
    occ += (sz >= S.z + 0.02 ? 1.0 : 0.0) * range;
  }
  fragColor = vec4(vec3(1.0 - occ / float(uSamples)), 1.0);
}
)";

// filtro bilateral (denoise) respeitando a profundidade
const char* AO_BLUR_FS = R"(
in vec2 vUv;
uniform sampler2D tAo, tDepth;
uniform vec2 uDir;
uniform mat4 uInvProj;
out vec4 fragColor;
float lin(vec2 uv) {
  float d = textureLod(tDepth, uv, 0.0).r;
  vec4 p = uInvProj * vec4(0.0, 0.0, d * 2.0 - 1.0, 1.0);
  return -p.z / p.w;
}
void main() {
  float z0 = lin(vUv);
  float acc = 0.0, w = 0.0;
  for (int i = -4; i <= 4; i++) {
    vec2 uv = vUv + uDir * float(i) * 2.0;
    float z = lin(uv);
    float wt = exp(-float(i * i) / 18.0) * exp(-abs(z - z0) / max(0.05 * z0, 0.01) * 4.0);
    acc += textureLod(tAo, uv, 0.0).r * wt;
    w += wt;
  }
  fragColor = vec4(vec3(acc / max(w, 1e-4)), 1.0);
}
)";

const char* AO_COMPOSITE_FS = R"(
in vec2 vUv;
uniform sampler2D tScene, tAo, tDepth;
uniform mat4 uInvProj;
uniform float uIntensity;
out vec4 fragColor;
void main() {
  vec4 c = texture(tScene, vUv);
  float ao = pow(clamp(texture(tAo, vUv).r, 0.0, 1.0), uIntensity);
  // a AO é aplicada depois da neblina: ao longe ela apagaria a névoa (morros e árvores
  // escuros); some entre 25 e 120 m, onde a oclusão de contato já não se vê
  float d = textureLod(tDepth, vUv, 0.0).r;
  vec4 vp = uInvProj * vec4(vUv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  float dist = -vp.z / vp.w;
  ao = mix(ao, 1.0, smoothstep(25.0, 120.0, dist));
  fragColor = vec4(c.rgb * ao, c.a);
}
)";

// ---------------------------------------------------------------- bloom (UnrealBloomPass)
const char* HIGHPASS_FS = R"(
in vec2 vUv;
uniform sampler2D tDiffuse;
uniform float uThreshold, uSmoothWidth;
out vec4 fragColor;
void main() {
  vec4 texel = texture(tDiffuse, vUv);
  float v = luminance(texel.xyz);
  float alpha = smoothstep(uThreshold, uThreshold + uSmoothWidth, v);
  fragColor = mix(vec4(0.0), texel, alpha);
}
)";

std::string blurFs(int kernelRadius) {
  // coeficientes gaussianos com sigma = raio, como no three
  std::string coeffs;
  for (int i = 0; i < kernelRadius; i++) {
    float sigma = (float)kernelRadius;
    float c = 0.39894f * std::exp(-0.5f * i * i / (sigma * sigma)) / sigma;
    coeffs += (i ? ", " : "") + std::to_string(c);
  }
  return "#define KERNEL_RADIUS " + std::to_string(kernelRadius) + "\n" +
         "const float coeffs[KERNEL_RADIUS] = float[](" + coeffs + ");\n" + R"(
in vec2 vUv;
uniform sampler2D tColor;
uniform vec2 uInvSize, uDirection;
out vec4 fragColor;
void main() {
  float weightSum = coeffs[0];
  vec3 diffuseSum = texture(tColor, vUv).rgb * weightSum;
  for (int i = 1; i < KERNEL_RADIUS; i++) {
    float x = float(i);
    float w = coeffs[i];
    vec2 uvOffset = uDirection * uInvSize * x;
    vec3 s1 = texture(tColor, vUv + uvOffset).rgb;
    vec3 s2 = texture(tColor, vUv - uvOffset).rgb;
    diffuseSum += (s1 + s2) * w;
    weightSum += 2.0 * w;
  }
  fragColor = vec4(diffuseSum / weightSum, 1.0);
}
)";
}

const char* BLOOM_COMPOSITE_FS = R"(
in vec2 vUv;
uniform sampler2D tBlur1, tBlur2, tBlur3, tBlur4, tBlur5;
uniform float uStrength, uRadius;
out vec4 fragColor;
float lerpBloomFactor(float factor) { return mix(factor, 1.2 - factor, uRadius); }
void main() {
  fragColor = uStrength * (lerpBloomFactor(1.0) * texture(tBlur1, vUv) + lerpBloomFactor(0.8) * texture(tBlur2, vUv) +
    lerpBloomFactor(0.6) * texture(tBlur3, vUv) + lerpBloomFactor(0.4) * texture(tBlur4, vUv) + lerpBloomFactor(0.2) * texture(tBlur5, vUv));
}
)";

const char* COPY_FS = R"(
in vec2 vUv;
uniform sampler2D tDiffuse;
uniform float uOpacity;
out vec4 fragColor;
void main() { fragColor = texture(tDiffuse, vUv) * uOpacity; }
)";

// ---------------------------------------------------------------- BokehShader do three
const char* BOKEH_FS = R"(
in vec2 vUv;
uniform sampler2D tColor, tDepth;
uniform float uMaxblur, uAperture, uFocus, uAspect, uNear, uFar;
out vec4 fragColor;
float getViewZ(float depth) { return (uNear * uFar) / ((uFar - uNear) * depth - uFar); }
void main() {
  vec2 aspectcorrect = vec2(1.0, uAspect);
  float viewZ = getViewZ(texture(tDepth, vUv).x);
  float factor = (uFocus + viewZ);
  vec2 dofblur = vec2(clamp(factor * uAperture, -uMaxblur, uMaxblur));
  vec2 dofblur9 = dofblur * 0.9, dofblur7 = dofblur * 0.7, dofblur4 = dofblur * 0.4;
  vec4 col = texture(tColor, vUv);
#define T(o, b) col += texture(tColor, vUv + (o * aspectcorrect) * b);
  T(vec2(0.0, 0.4), dofblur) T(vec2(0.15, 0.37), dofblur) T(vec2(0.29, 0.29), dofblur) T(vec2(-0.37, 0.15), dofblur)
  T(vec2(0.40, 0.0), dofblur) T(vec2(0.37, -0.15), dofblur) T(vec2(0.29, -0.29), dofblur) T(vec2(-0.15, -0.37), dofblur)
  T(vec2(0.0, -0.4), dofblur) T(vec2(-0.15, 0.37), dofblur) T(vec2(-0.29, 0.29), dofblur) T(vec2(0.37, 0.15), dofblur)
  T(vec2(-0.4, 0.0), dofblur) T(vec2(-0.37, -0.15), dofblur) T(vec2(-0.29, -0.29), dofblur) T(vec2(0.15, -0.37), dofblur)
  T(vec2(0.15, 0.37), dofblur9) T(vec2(-0.37, 0.15), dofblur9) T(vec2(0.37, -0.15), dofblur9) T(vec2(-0.15, -0.37), dofblur9)
  T(vec2(-0.15, 0.37), dofblur9) T(vec2(0.37, 0.15), dofblur9) T(vec2(-0.37, -0.15), dofblur9) T(vec2(0.15, -0.37), dofblur9)
  T(vec2(0.29, 0.29), dofblur7) T(vec2(0.40, 0.0), dofblur7) T(vec2(0.29, -0.29), dofblur7) T(vec2(0.0, -0.4), dofblur7)
  T(vec2(-0.29, 0.29), dofblur7) T(vec2(-0.4, 0.0), dofblur7) T(vec2(-0.29, -0.29), dofblur7) T(vec2(0.0, 0.4), dofblur7)
  T(vec2(0.29, 0.29), dofblur4) T(vec2(0.4, 0.0), dofblur4) T(vec2(0.29, -0.29), dofblur4) T(vec2(0.0, -0.4), dofblur4)
  T(vec2(-0.29, 0.29), dofblur4) T(vec2(-0.4, 0.0), dofblur4) T(vec2(-0.29, -0.29), dofblur4) T(vec2(0.0, 0.4), dofblur4)
  fragColor = col / 41.0;
  fragColor.a = 1.0;
}
)";

// resolve do MSAA ponderado pelo tone mapping: cada amostra pesa 1/(1+brilho), assim bordas de
// reflexos fortes (lataria, cromados) não ficam serrilhadas depois do ACES
const char* RESOLVE_FS = R"(
uniform sampler2DMS tMs;
uniform int uSamples;
uniform float uExposure;
out vec4 fragColor;
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  vec3 sum = vec3(0.0);
  float wsum = 0.0;
  for (int i = 0; i < uSamples; i++) {
    vec3 c = max(texelFetch(tMs, p, i).rgb, vec3(0.0));
    float w = 1.0 / (1.0 + max(c.r, max(c.g, c.b)) * uExposure);
    sum += c * w;
    wsum += w;
  }
  fragColor = vec4(sum / max(wsum, 1e-6), 1.0);
}
)";

// OutputPass: tone mapping + sRGB
const char* OUTPUT_FS = R"(
in vec2 vUv;
uniform sampler2D tDiffuse;
uniform int uToneMapping;
uniform float uExposure;
out vec4 fragColor;
void main() {
  vec4 c = texture(tDiffuse, vUv);
  vec3 rgb = uToneMapping == 0 ? ACESFilmicToneMapping(c.rgb, uExposure) : NeutralToneMapping(c.rgb, uExposure);
  fragColor = vec4(linearToSRGB(rgb), c.a);
}
)";

// FinalShader do postfx.js
const char* FINAL_FS = R"(
in vec2 vUv;
uniform sampler2D tDiffuse;
uniform float uTime, uSpeed, uSaturation, uContrast, uVignette, uFxaa, uTint, uLens, uSpeedBlur;
uniform vec2 uResolution;
out vec4 fragColor;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 fxaa(vec2 uv) {
  vec2 px = 1.0 / uResolution;
  vec3 m = texture(tDiffuse, uv).rgb;
  float lN = luma(texture(tDiffuse, uv + vec2(0, px.y)).rgb), lS = luma(texture(tDiffuse, uv - vec2(0, px.y)).rgb);
  float lE = luma(texture(tDiffuse, uv + vec2(px.x, 0)).rgb), lW = luma(texture(tDiffuse, uv - vec2(px.x, 0)).rgb);
  float lM = luma(m), mn = min(lM, min(min(lN, lS), min(lE, lW))), mx = max(lM, max(max(lN, lS), max(lE, lW)));
  if (mx - mn < max(0.04, mx * 0.12)) return m;
  vec2 dir = normalize(vec2(-(lN - lS), lE - lW) + 1e-5) * px;
  return 0.5 * (texture(tDiffuse, uv + dir * 0.5).rgb + texture(tDiffuse, uv - dir * 0.5).rgb);
}
void main() {
  vec2 uv = vUv, c = uv - 0.5;
  float r = length(c);
  vec2 ca = c * r * 0.0035 * (1.0 + uSpeed * 1.5) * uLens;
  vec3 col;
  if (uFxaa > 0.5) {
    // FXAA suaviza o que o MSAA não pega (brilho de reflexo, detalhes finos ao longe);
    // a aberração cromática entra como diferença sobre o resultado
    col = fxaa(uv);
    if (uLens > 0.0) {
      vec3 m = texture(tDiffuse, uv).rgb;
      col.r += texture(tDiffuse, uv - ca).r - m.r;
      col.b += texture(tDiffuse, uv + ca).b - m.b;
    }
  } else {
    col = vec3(texture(tDiffuse, uv - ca).r, texture(tDiffuse, uv).g, texture(tDiffuse, uv + ca).b);
  }
  float sb = uSpeed * smoothstep(0.18, 0.7, r) * 0.03 * uSpeedBlur;
  if (sb > 0.0005) {
    vec3 acc = col;
    for (int i = 1; i <= 6; i++) acc += texture(tDiffuse, uv - c * sb * float(i)).rgb;
    col = acc / 7.0;
  }
  col = (col - 0.5) * uContrast + 0.5;
  float l = luma(col);
  col = mix(vec3(l), col, uSaturation);
  col *= mix(vec3(1.0), mix(vec3(0.97, 0.99, 1.04), vec3(1.03, 1.0, 0.96), smoothstep(0.2, 0.8, l)), uTint);
  col *= 1.0 - uVignette * uLens * smoothstep(0.35, 0.85, r);
  // grão com hash inteiro (o truque do sin() com argumentos enormes vira um padrão em blocos
  // que "rasteja" sobre áreas lisas como o céu); mais fraco nas partes claras
  uvec3 hv = uvec3(uvec2(gl_FragCoord.xy), uint(uTime * 60.0));
  hv = hv * 1664525u + 1013904223u;
  hv.x += hv.y * hv.z; hv.y += hv.z * hv.x; hv.z += hv.x * hv.y;
  hv ^= hv >> 16u;
  float n = float(hv.x + hv.y + hv.z & 0xffffu) / 65535.0;
  col += (n - 0.5) * 0.010 * uLens * (1.0 - 0.6 * l);
  fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)";

GLuint makeTex(int w, int h, GLenum fmt, GLenum type, GLenum ifmt, GLenum filter) {
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}
GLuint colorTarget(int w, int h, GLuint& tex) {
  tex = makeTex(w, h, GL_RGBA, GL_HALF_FLOAT, GL_RGBA16F, GL_LINEAR);
  GLuint f;
  glGenFramebuffers(1, &f);
  glBindFramebuffer(GL_FRAMEBUFFER, f);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  return f;
}
void bindTex(int unit, GLuint t) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, t);
}
void target(GLuint fbo, int w, int h) {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, w, h);
}
std::string fs(const char* s) { return std::string(GLSL_COMMON) + s; }

}  // namespace

PostFX::PostFX() {
  aoShader = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, fs(AO_FS));
  aoBlur = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, fs(AO_BLUR_FS));
  aoComposite = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, fs(AO_COMPOSITE_FS));
  highPass = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, fs(HIGHPASS_FS));
  const int kernels[5] = {3, 5, 7, 9, 11};
  for (int i = 0; i < 5; i++) blur[i] = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, blurFs(kernels[i]));
  bloomComposite = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, fs(BLOOM_COMPOSITE_FS));
  copyShader = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, fs(COPY_FS));
  bokehShader = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, fs(BOKEH_FS));
  output = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, fs(OUTPUT_FS));
  finalShader = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, fs(FINAL_FS));
  resolveShader = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, fs(RESOLVE_FS));
}

PostFX::~PostFX() { destroy(); }

void PostFX::setLevel(int l) {
  l = ((l % 4) + 4) % 4;
  setQuality(QUALITY[l], l);
}

void PostFX::setQuality(const Quality& nq, int lvl) {
  q = nq;
  level = lvl;
  destroy();
  build();
}

void PostFX::resize(int w, int h, float d) {
  if (w == winW && h == winH && d == dpr && built) return;
  winW = std::max(1, w);
  winH = std::max(1, h);
  dpr = d;
  destroy();
  build();
}

void PostFX::destroy() {
  if (!built) return;
  auto delF = [](GLuint& f) { if (f) glDeleteFramebuffers(1, &f); f = 0; };
  auto delT = [](GLuint& t) { if (t) glDeleteTextures(1, &t); t = 0; };
  delF(msFbo);
  if (msColor) glDeleteTextures(1, &msColor);
  if (msDepth) glDeleteRenderbuffers(1, &msDepth);
  msColor = msDepth = 0;
  delF(sceneFbo); delT(sceneTex); delT(depthTex);
  for (int i = 0; i < 2; i++) { delF(aoFbo[i]); delT(aoTex[i]); delF(pingFbo[i]); delT(pingTex[i]); }
  delF(brightFbo); delT(brightTex);
  for (int i = 0; i < 5; i++) { delF(bloomHFbo[i]); delT(bloomH[i]); delF(bloomVFbo[i]); delT(bloomV[i]); }
  delF(finalFbo); delT(finalTex);
  built = false;
}

void PostFX::build() {
  float pr = std::min(dpr, q.pixelRatio);
  // o tamanho do buffer de desenho segue o pixelRatio da qualidade (como renderer.setPixelRatio)
  width = std::max(1, (int)std::floor(winW * pr));
  height = std::max(1, (int)std::floor(winH * pr));

  // cena resolvida (cor HDR + profundidade)
  sceneFbo = colorTarget(width, height, sceneTex);
  depthTex = makeTex(width, height, GL_DEPTH_COMPONENT, GL_FLOAT, GL_DEPTH_COMPONENT32F, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
  if (q.msaa > 0) {
    glGenFramebuffers(1, &msFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    // cor em textura multiamostra (lida pelo resolve ponderado)
    glGenTextures(1, &msColor);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msColor);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, q.msaa, GL_RGBA16F, width, height, GL_TRUE);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, msColor, 0);
    glGenRenderbuffers(1, &msDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, q.msaa, GL_DEPTH_COMPONENT32F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
  }
  for (int i = 0; i < 2; i++) pingFbo[i] = colorTarget(width, height, pingTex[i]);
  if (q.ao) {
    aoW = q.aoHalfRes ? std::max(1, width / 2) : width;
    aoH = q.aoHalfRes ? std::max(1, height / 2) : height;
    for (int i = 0; i < 2; i++) aoFbo[i] = colorTarget(aoW, aoH, aoTex[i]);
  }
  if (q.bloom) {
    // composer.setSize: o primeiro nível tem metade da resolução
    int rx = (int)std::round(width / 2.0), ry = (int)std::round(height / 2.0);
    brightFbo = colorTarget(rx, ry, brightTex);
    for (int i = 0; i < 5; i++) {
      bloomW[i] = std::max(1, rx);
      bloomHt[i] = std::max(1, ry);
      bloomHFbo[i] = colorTarget(bloomW[i], bloomHt[i], bloomH[i]);
      bloomVFbo[i] = colorTarget(bloomW[i], bloomHt[i], bloomV[i]);
      rx = (int)std::round(rx / 2.0);
      ry = (int)std::round(ry / 2.0);
    }
  }
  finalTex = makeTex(width, height, GL_RGBA, GL_UNSIGNED_BYTE, GL_RGBA8, GL_LINEAR);
  glGenFramebuffers(1, &finalFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, finalFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, finalTex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  built = true;
}

void PostFX::beginScene() {
  if (!built) build();
  glBindFramebuffer(GL_FRAMEBUFFER, quality().msaa > 0 ? msFbo : sceneFbo);
  glViewport(0, 0, width, height);
}

void PostFX::endScene(float dt, float speed01, const Camera& cam) {
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);
  if (q.msaa > 0) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    target(sceneFbo, width, height);
    resolveShader->use();
    resolveShader->set("tMs", 0);
    resolveShader->set("uSamples", q.msaa);
    resolveShader->set("uExposure", exposure);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msColor);
    drawFullscreen();
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
  }
  GLuint cur = sceneTex;  // textura com a imagem atual
  int ping = 0;
  auto nextTarget = [&]() {
    GLuint f = pingFbo[ping];
    GLuint t = pingTex[ping];
    ping ^= 1;
    target(f, width, height);
    return t;
  };

  // --- oclusão de ambiente (aoRadius 1,2; distanceFalloff 0,6; intensidade 2,2)
  if (q.ao) {
    mat4 invProj = cam.proj.inverse();
    target(aoFbo[0], aoW, aoH);
    aoShader->use();
    aoShader->set("tDepth", 0);
    aoShader->set("uProj", cam.proj);
    aoShader->set("uInvProj", invProj);
    aoShader->set("uSize", vec2((float)aoW, (float)aoH));
    aoShader->set("uSamples", q.aoSamples);
    aoShader->set("uRadius", 1.2f);
    aoShader->set("uFalloff", 0.6f);
    bindTex(0, depthTex);
    drawFullscreen();
    aoBlur->use();
    aoBlur->set("tAo", 0);
    aoBlur->set("tDepth", 1);
    aoBlur->set("uInvProj", invProj);
    bindTex(1, depthTex);
    target(aoFbo[1], aoW, aoH);
    aoBlur->set("uDir", vec2(1.0f / aoW, 0));
    bindTex(0, aoTex[0]);
    drawFullscreen();
    target(aoFbo[0], aoW, aoH);
    aoBlur->set("uDir", vec2(0, 1.0f / aoH));
    bindTex(0, aoTex[1]);
    drawFullscreen();
    GLuint out = nextTarget();
    aoComposite->use();
    aoComposite->set("tScene", 0);
    aoComposite->set("tAo", 1);
    aoComposite->set("uIntensity", 2.2f);
    aoComposite->set("tDepth", 2);
    aoComposite->set("uInvProj", invProj);
    bindTex(0, cur);
    bindTex(1, aoTex[0]);
    bindTex(2, depthTex);
    drawFullscreen();
    cur = out;
  }

  // --- bloom (força 0,22; raio 0,55; limiar 1,6)
  if (q.bloom) {
    target(brightFbo, bloomW[0], bloomHt[0]);
    highPass->use();
    highPass->set("tDiffuse", 0);
    highPass->set("uThreshold", 1.6f);
    highPass->set("uSmoothWidth", 0.01f);
    bindTex(0, cur);
    drawFullscreen();
    GLuint input = brightTex;
    for (int i = 0; i < 5; i++) {
      Shader& b = *blur[i];
      b.use();
      b.set("tColor", 0);
      b.set("uInvSize", vec2(1.0f / bloomW[i], 1.0f / bloomHt[i]));
      target(bloomHFbo[i], bloomW[i], bloomHt[i]);
      b.set("uDirection", vec2(1, 0));
      bindTex(0, input);
      drawFullscreen();
      target(bloomVFbo[i], bloomW[i], bloomHt[i]);
      b.set("uDirection", vec2(0, 1));
      bindTex(0, bloomH[i]);
      drawFullscreen();
      input = bloomV[i];
    }
    target(bloomHFbo[0], bloomW[0], bloomHt[0]);
    bloomComposite->use();
    const char* names[5] = {"tBlur1", "tBlur2", "tBlur3", "tBlur4", "tBlur5"};
    for (int i = 0; i < 5; i++) {
      bloomComposite->set(names[i], i);
      bindTex(i, bloomV[i]);
    }
    bloomComposite->set("uStrength", 0.22f);
    bloomComposite->set("uRadius", 0.55f);
    drawFullscreen();
    // soma aditiva sobre a imagem atual
    GLuint out = nextTarget();
    copyShader->use();
    copyShader->set("tDiffuse", 0);
    copyShader->set("uOpacity", 1.0f);
    bindTex(0, cur);
    drawFullscreen();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    bindTex(0, bloomH[0]);
    drawFullscreen();
    glDisable(GL_BLEND);
    cur = out;
  }

  // --- profundidade de campo (só na cinemática)
  if (bokeh) {
    GLuint out = nextTarget();
    bokehShader->use();
    bokehShader->set("tColor", 0);
    bokehShader->set("tDepth", 1);
    bokehShader->set("uFocus", focus);
    bokehShader->set("uAperture", aperture);
    bokehShader->set("uMaxblur", maxblur);
    bokehShader->set("uAspect", cam.aspect);
    bokehShader->set("uNear", cam.near);
    bokehShader->set("uFar", cam.far);
    bindTex(0, cur);
    bindTex(1, depthTex);
    drawFullscreen();
    cur = out;
  }

  // --- tone mapping + sRGB
  {
    GLuint out = nextTarget();
    output->use();
    output->set("tDiffuse", 0);
    output->set("uToneMapping", toneMapping == ToneMapping::ACES ? 0 : 1);
    output->set("uExposure", exposure);
    bindTex(0, cur);
    drawFullscreen();
    cur = out;
  }

  // --- shader final
  time = std::fmod(time + dt, 100.0f);
  speed += (speed01 - speed) * std::min(1.0f, dt * 3);
  target(finalFbo, width, height);
  Shader& f = *finalShader;
  f.use();
  f.set("tDiffuse", 0);
  f.set("uTime", time);
  f.set("uSpeed", speed);
  f.set("uResolution", vec2((float)width, (float)height));
  f.set("uSaturation", saturation);
  f.set("uContrast", contrast);
  f.set("uVignette", vignette);
  f.set("uTint", tint);
  f.set("uLens", lensFx ? 1.0f : 0.0f);
  f.set("uSpeedBlur", speedBlur ? 1.0f : 0.0f);
  f.set("uFxaa", 1.0f);
  bindTex(0, cur);
  drawFullscreen();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDepthMask(GL_TRUE);
}

void PostFX::present(int fbW, int fbH) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, finalFbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBlitFramebuffer(0, 0, width, height, 0, 0, fbW, fbH, GL_COLOR_BUFFER_BIT, width == fbW && height == fbH ? GL_NEAREST : GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
