#include "environment.h"

#include <cmath>
#include <string>

#include "shader.h"

namespace {

// direção da amostra a partir do texel da face do cubemap (convenção da especificação do GL)
const char* CUBE_DIR = R"(
uniform int uFace;
uniform float uSize;
vec3 cubeDir() {
  vec2 st = gl_FragCoord.xy / uSize * 2.0 - 1.0;
  float sc = st.x, tc = st.y;
  if (uFace == 0) return normalize(vec3(1.0, -tc, -sc));
  if (uFace == 1) return normalize(vec3(-1.0, -tc, sc));
  if (uFace == 2) return normalize(vec3(sc, 1.0, tc));
  if (uFace == 3) return normalize(vec3(sc, -1.0, -tc));
  if (uFace == 4) return normalize(vec3(sc, -tc, 1.0));
  return normalize(vec3(-sc, -tc, -1.0));
}
)";

const char* EQUIRECT_FS = R"(
uniform sampler2D tEquirect;
out vec4 fragColor;
void main() {
  vec3 d = cubeDir();
  fragColor = vec4(textureLod(tEquirect, equirectUv(d), 0.0).rgb, 1.0);
}
)";

// RoomEnvironment (three.js r170) traçado analiticamente: caixas brancas iluminadas por uma
// luz pontual (900, alcance 28, decaimento 2) e caixas emissivas (softboxes).
const char* ROOM_FS = R"(
out vec4 fragColor;
struct Box { vec3 p; float ry; vec3 s; float emit; };
const int N = 13;
Box boxes[N] = Box[N](
  Box(vec3(-0.757, 13.219, 0.717), 0.0, vec3(31.713, 28.305, 28.591), -1.0),  // sala (lado de dentro)
  Box(vec3(-10.906, 2.009, 1.846), -0.195, vec3(2.328, 7.905, 4.651), 0.0),
  Box(vec3(-5.607, -0.754, -0.758), 0.994, vec3(1.970, 1.534, 3.955), 0.0),
  Box(vec3(6.167, 0.857, 7.803), 0.561, vec3(3.927, 6.285, 3.687), 0.0),
  Box(vec3(-2.017, 0.018, 6.124), 0.333, vec3(2.002, 4.566, 2.064), 0.0),
  Box(vec3(2.291, -0.756, -2.621), -0.286, vec3(1.546, 1.552, 1.496), 0.0),
  Box(vec3(-2.193, -0.369, -5.547), 0.516, vec3(3.875, 3.487, 2.986), 0.0),
  Box(vec3(-16.116, 14.37, 8.208), 0.0, vec3(0.1, 2.428, 2.739), 50.0),
  Box(vec3(-16.109, 18.021, -8.207), 0.0, vec3(0.1, 2.425, 2.751), 50.0),
  Box(vec3(14.904, 12.198, -1.832), 0.0, vec3(0.15, 4.265, 6.331), 17.0),
  Box(vec3(-0.462, 8.89, 14.520), 0.0, vec3(4.38, 5.441, 0.088), 43.0),
  Box(vec3(3.235, 11.486, -12.541), 0.0, vec3(2.5, 2.0, 0.1), 20.0),
  Box(vec3(0.0, 20.0, 0.0), 0.0, vec3(1.0, 0.1, 1.0), 100.0)
);
const vec3 LIGHT_POS = vec3(0.418, 16.199, 0.300);

// interseção raio × caixa orientada (rotação em Y); devolve (t entrada, t saída) e normais
bool hitBox(Box b, vec3 ro, vec3 rd, out float tn, out float tf, out vec3 nn, out vec3 nf) {
  float c = cos(b.ry), s = sin(b.ry);
  // mundo → local: rotação inversa em Y
  mat3 toLocal = mat3(c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c);
  mat3 toWorld = transpose(toLocal);
  vec3 o = toLocal * (ro - b.p), d = toLocal * rd;
  vec3 h = b.s * 0.5;
  vec3 inv = 1.0 / d;
  vec3 t0 = (-h - o) * inv, t1 = (h - o) * inv;
  vec3 tmin = min(t0, t1), tmax = max(t0, t1);
  tn = max(max(tmin.x, tmin.y), tmin.z);
  tf = min(min(tmax.x, tmax.y), tmax.z);
  if (tn > tf || tf < 0.0) return false;
  vec3 ln = tn == tmin.x ? vec3(-sign(d.x), 0, 0) : tn == tmin.y ? vec3(0, -sign(d.y), 0) : vec3(0, 0, -sign(d.z));
  vec3 lf = tf == tmax.x ? vec3(-sign(d.x), 0, 0) : tf == tmax.y ? vec3(0, -sign(d.y), 0) : vec3(0, 0, -sign(d.z));
  nn = toWorld * ln;
  nf = toWorld * lf;  // normal apontando para dentro (lado de trás da sala)
  return true;
}

void main() {
  vec3 rd = cubeDir();
  float best = 1e9; vec3 n = vec3(0, 1, 0); float emit = 0.0; vec3 p;
  for (int i = 0; i < N; i++) {
    float tn, tf; vec3 nn, nf;
    if (!hitBox(boxes[i], vec3(0.0), rd, tn, tf, nn, nf)) continue;
    if (boxes[i].emit < 0.0) {
      if (tf > 0.0 && tf < best) { best = tf; n = nf; emit = 0.0; }
    } else if (tn > 0.0 && tn < best) {
      best = tn; n = nn; emit = boxes[i].emit;
    }
  }
  if (emit > 0.0) { fragColor = vec4(vec3(emit), 1.0); return; }
  p = rd * best;
  vec3 L = LIGHT_POS - p;
  float dist = length(L);
  L /= dist;
  float falloff = 1.0 / max(pow(dist, 2.0), 0.01);
  falloff *= pow2(clamp(1.0 - pow4(dist / 28.0), 0.0, 1.0));
  float dotNL = clamp(dot(n, L), 0.0, 1.0);
  vec3 irr = vec3(900.0 * falloff * dotNL);
  // MeshStandardMaterial branco, rugosidade 1: Lambert + GGX
  vec3 V = -rd, H = normalize(L + V);
  float dotNV = clamp(dot(n, V), 0.0, 1.0), dotNH = clamp(dot(n, H), 0.0, 1.0), dotVH = clamp(dot(V, H), 0.0, 1.0);
  float a2 = 1.0;
  float fres = exp2((-5.55473 * dotVH - 6.98316) * dotVH);
  float F = 0.04 * (1.0 - fres) + fres;
  float gv = dotNL * sqrt(a2 + (1.0 - a2) * pow2(dotNV)), gl = dotNV * sqrt(a2 + (1.0 - a2) * pow2(dotNL));
  float Vis = 0.5 / max(gv + gl, EPSILON);
  float D = RECIPROCAL_PI * a2 / pow2(pow2(dotNH) * (a2 - 1.0) + 1.0);
  vec3 col = irr * RECIPROCAL_PI + irr * F * Vis * D;
  fragColor = vec4(col, 1.0);
}
)";

const char* PREFILTER_FS = R"(
uniform samplerCube tSource;
uniform float uRoughness;
uniform float uSourceSize;
out vec4 fragColor;
float radicalInverse(uint bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10;
}
void main() {
  vec3 N = cubeDir();
  if (uRoughness <= 0.0) { fragColor = vec4(textureLod(tSource, N, 0.0).rgb, 1.0); return; }
  vec3 V = N;
  float a = uRoughness * uRoughness;
  vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
  vec3 T = normalize(cross(up, N)), B = cross(N, T);
  const uint COUNT = 1024u;
  vec3 acc = vec3(0.0); float w = 0.0;
  for (uint i = 0u; i < COUNT; i++) {
    vec2 Xi = vec2(float(i) / float(COUNT), radicalInverse(i));
    float phi = 2.0 * PI * Xi.x;
    float cosT = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    vec3 H = normalize(T * (cos(phi) * sinT) + B * (sin(phi) * sinT) + N * cosT);
    vec3 L = normalize(2.0 * dot(V, H) * H - V);
    float NdotL = dot(N, L);
    if (NdotL <= 0.0) continue;
    float NdotH = max(dot(N, H), 0.0), HdotV = max(dot(H, V), 0.0);
    float a2 = a * a, dd = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * dd * dd);
    float pdf = D * NdotH / (4.0 * HdotV) + 0.0001;
    float saTexel = 4.0 * PI / (6.0 * uSourceSize * uSourceSize);
    float saSample = 1.0 / (float(COUNT) * pdf + 0.0001);
    float mip = 0.5 * log2(saSample / saTexel) + 1.0;
    acc += textureLod(tSource, L, max(mip, 0.0)).rgb * NdotL;
    w += NdotL;
  }
  fragColor = vec4(acc / max(w, 1e-4), 1.0);
}
)";

const char* IRRADIANCE_FS = R"(
uniform samplerCube tSource;
uniform float uLod;
out vec4 fragColor;
void main() {
  vec3 N = cubeDir();
  vec3 up = abs(N.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
  vec3 right = normalize(cross(up, N));
  up = cross(N, right);
  vec3 acc = vec3(0.0); float w = 0.0;
  const float delta = 0.04;
  for (float phi = 0.0; phi < 2.0 * PI; phi += delta) {
    for (float theta = 0.0; theta < 0.5 * PI; theta += delta) {
      vec3 t = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
      vec3 d = t.x * right + t.y * up + t.z * N;
      float k = cos(theta) * sin(theta);
      acc += textureLod(tSource, d, uLod).rgb * k;
      w += k;
    }
  }
  // média cosseno-ponderada = E/π (o three usa PI·cor · albedo/π)
  fragColor = vec4(acc / w, 1.0);
}
)";

GLuint makeCube(int size, bool mips) {
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_CUBE_MAP, t);
  int levels = mips ? (int)std::floor(std::log2(size)) + 1 : 1;
  for (int l = 0; l < levels; l++) {
    int s = std::max(1, size >> l);
    for (int f = 0; f < 6; f++) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, l, GL_RGBA16F, s, s, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levels - 1);
  return t;
}

// desenha um shader em cada face (e nível) do cubemap
void renderFaces(Shader& sh, GLuint cube, int level, int size) {
  GLuint fbo;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, size, size);
  sh.use();
  sh.set("uSize", (float)size);
  for (int f = 0; f < 6; f++) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, cube, level);
    sh.set("uFace", f);
    drawFullscreen();
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
}

std::string fs(const char* body) { return std::string(GLSL_COMMON) + CUBE_DIR + body; }

}  // namespace

Environment::~Environment() {
  if (specular) glDeleteTextures(1, &specular);
  if (irradiance) glDeleteTextures(1, &irradiance);
}

std::unique_ptr<Environment> Environment::build(GLuint source, int size) {
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glBindTexture(GL_TEXTURE_CUBE_MAP, source);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

  auto env = std::make_unique<Environment>();
  // reflexos: mips de size até 8 px, rugosidade linear por nível
  int levels = (int)std::log2(size) - 2;
  env->specularMips = levels;
  env->specular = makeCube(size, true);
  glBindTexture(GL_TEXTURE_CUBE_MAP, env->specular);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levels - 1);
  Shader pre(GLSL_FULLSCREEN_VS, fs(PREFILTER_FS));
  pre.use();
  pre.set("tSource", 0);
  pre.set("uSourceSize", (float)size);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, source);
  for (int l = 0; l < levels; l++) {
    pre.use();
    pre.set("uRoughness", (float)l / (levels - 1));
    renderFaces(pre, env->specular, l, size >> l);
  }

  // difusa
  env->irradiance = makeCube(32, false);
  Shader irr(GLSL_FULLSCREEN_VS, fs(IRRADIANCE_FS));
  irr.use();
  irr.set("tSource", 0);
  irr.set("uLod", std::max(0.0f, std::log2((float)size) - 5.0f));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, source);
  renderFaces(irr, env->irradiance, 0, 32);
  return env;
}

std::unique_ptr<Environment> Environment::fromEquirect(Texture& equirect, int cubeSize) {
  equirect.upload();
  GLuint src = makeCube(cubeSize, true);
  Shader sh(GLSL_FULLSCREEN_VS, fs(EQUIRECT_FS));
  sh.use();
  sh.set("tEquirect", 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, equirect.id);
  renderFaces(sh, src, 0, cubeSize);
  auto env = build(src, cubeSize);
  glDeleteTextures(1, &src);
  return env;
}

std::unique_ptr<Environment> Environment::room() {
  const int size = 256;
  GLuint src = makeCube(size, true);
  Shader sh(GLSL_FULLSCREEN_VS, fs(ROOM_FS));
  renderFaces(sh, src, 0, size);
  auto env = build(src, size);
  glDeleteTextures(1, &src);
  return env;
}
