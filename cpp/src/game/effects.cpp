#include "effects.h"

#include <cmath>

namespace {

const char* FOG_FS = R"(
uniform int uFogType;
uniform vec3 uFogColor;
uniform float uFogNear, uFogFar, uFogDensity;
vec3 applyFog(vec3 c, float depth) {
  if (uFogType == 1) return mix(c, uFogColor, smoothstep(uFogNear, uFogFar, depth));
  if (uFogType == 2) return mix(c, uFogColor, 1.0 - exp(-uFogDensity * uFogDensity * depth * depth));
  if (uFogType == 3) return mix(c, uFogColor, 1.0 - exp(-uFogDensity * depth));
  return c;
}
)";

void setFog(Shader& s, const RenderContext& ctx) {
  s.set("uFogType", (int)ctx.fog.type);
  s.set("uFogColor", ctx.fog.color);
  s.set("uFogNear", ctx.fog.near);
  s.set("uFogFar", ctx.fog.far);
  s.set("uFogDensity", ctx.fog.density);
}

const char* SKID_VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aAlpha;
uniform mat4 uView, uProj;
uniform vec4 uClipPlane;
out float vA;
out float vDepth;
void main() {
  vA = aAlpha;
  vec4 mv = uView * vec4(aPos, 1.0);
  vDepth = -mv.z;
  gl_Position = uProj * mv;
  gl_ClipDistance[0] = dot(vec4(aPos, 1.0), uClipPlane);
}
)";
const char* SKID_FS = R"(
in float vA;
in float vDepth;
out vec4 fragColor;
void main() {
  fragColor = vec4(applyFog(vec3(0.03), vDepth), vA * 0.75);
}
)";

// fumaça: billboards do tamanho que o gl_PointSize teria
const char* SMOKE_VS = R"(
layout(location = 0) in vec4 aPosSize;   // posição + tamanho
layout(location = 1) in vec2 aAlphaSeed;
uniform mat4 uView, uProj;
uniform float uScale, uMaxSize;
uniform vec2 uViewport;
out float vA, vSeed, vDepth;
out vec2 vCoord;
void main() {
  vec2 corner = vec2((gl_VertexID == 1 || gl_VertexID == 2 || gl_VertexID == 4) ? 1.0 : 0.0,
                     (gl_VertexID == 2 || gl_VertexID == 4 || gl_VertexID == 5) ? 1.0 : 0.0);
  vA = aAlphaSeed.x;
  vSeed = aAlphaSeed.y;
  vec4 mv = uView * vec4(aPosSize.xyz, 1.0);
  vDepth = -mv.z;
  float sizePx = min(aPosSize.w * uScale / max(0.5, -mv.z), uMaxSize);
  vec4 clip = uProj * mv;
  vec2 offs = (corner - 0.5) * sizePx / uViewport * 2.0 * clip.w;
  gl_Position = clip + vec4(offs, 0.0, 0.0);
  vCoord = vec2(corner.x, 1.0 - corner.y);  // gl_PointCoord: y = 0 em cima
  if (vA <= 0.0) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
}
)";
const char* SMOKE_FS = R"(
in float vA, vSeed, vDepth;
in vec2 vCoord;
uniform vec3 uLight;
out vec4 fragColor;
float h(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float n(vec2 p) {
  vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(h(i), h(i + vec2(1, 0)), f.x), mix(h(i + vec2(0, 1)), h(i + vec2(1, 1)), f.x), f.y);
}
void main() {
  vec2 c = vCoord - 0.5; float r = length(c);
  float puff = n(c * 4.0 + vSeed * 17.0) * 0.6 + n(c * 9.0 - vSeed * 5.0) * 0.4;
  float a = smoothstep(0.5, 0.1, r) * (0.55 + 0.45 * puff) * vA;
  if (a < 0.003) discard;
  // iluminação simples: parte de cima mais clara (sol), de baixo mais escura
  vec3 col = uLight * mix(0.62, 0.95, 0.5 - c.y);
  fragColor = vec4(applyFog(col, vDepth), a);
}
)";

}  // namespace

// ---------------------------------------------------------------- marcas de pneu
SkidMarks::SkidMarks() {
  pos.assign(MAX * 4 * 3, 0);
  alpha.assign(MAX * 4, 0);
  std::vector<uint32_t> idx(MAX * 6);
  for (int i = 0; i < MAX; i++) {
    uint32_t v = i * 4;
    uint32_t q[6] = {v, v + 1, v + 2, v + 2, v + 1, v + 3};
    std::copy(q, q + 6, idx.begin() + i * 6);
  }
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(2, vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
  glBufferData(GL_ARRAY_BUFFER, pos.size() * 4, pos.data(), GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
  glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
  glBufferData(GL_ARRAY_BUFFER, alpha.size() * 4, alpha.data(), GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
  glBindVertexArray(0);
  shader = std::make_unique<Shader>(SKID_VS, std::string(FOG_FS) + SKID_FS);
}

SkidMarks::~SkidMarks() {
  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(2, vbo);
  glDeleteBuffers(1, &ebo);
}

void SkidMarks::add(const std::string& id, const vec3& p, const vec3& side, float intensity, float width) {
  auto it = last.find(id);
  if (intensity <= 0.02f) {
    if (it != last.end()) last.erase(it);
    return;
  }
  Last l{p.x + side.x * width / 2, p.y + 0.006f, p.z + side.z * width / 2, p.x - side.x * width / 2, p.z - side.z * width / 2, intensity};
  if (it == last.end()) {
    last[id] = l;
    return;
  }
  Last prev = it->second;
  float dx = l.x - prev.x, dz = l.z - prev.z;
  if (dx * dx + dz * dz < 0.09f) return;
  if (dx * dx + dz * dz > 25) {
    it->second = l;
    return;
  }
  int i = next;
  next = (next + 1) % MAX;
  float q[12] = {prev.x, prev.y, prev.z, prev.xr, prev.y, prev.zr, l.x, l.y, l.z, l.xr, l.y, l.zr};
  std::copy(q, q + 12, pos.begin() + i * 12);
  float a[4] = {prev.a, prev.a, l.a, l.a};
  std::copy(a, a + 4, alpha.begin() + i * 4);
  dirtyMin = std::min(dirtyMin, i);
  dirtyMax = std::max(dirtyMax, i);
  it->second = l;
}

void SkidMarks::clear() {
  std::fill(alpha.begin(), alpha.end(), 0.f);
  dirtyAll = true;
  last.clear();
}

void SkidMarks::draw(const RenderContext& ctx) {
  if (dirtyAll) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, pos.size() * 4, pos.data());
    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, alpha.size() * 4, alpha.data());
    dirtyAll = false;
    dirtyMin = MAX;
    dirtyMax = -1;
  } else if (dirtyMax >= dirtyMin) {
    int n = dirtyMax - dirtyMin + 1;
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glBufferSubData(GL_ARRAY_BUFFER, dirtyMin * 48, n * 48, pos.data() + dirtyMin * 12);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
    glBufferSubData(GL_ARRAY_BUFFER, dirtyMin * 16, n * 16, alpha.data() + dirtyMin * 4);
    dirtyMin = MAX;
    dirtyMax = -1;
  }
  Shader& s = *shader;
  s.use();
  s.set("uView", ctx.view);
  s.set("uProj", ctx.proj);
  s.set("uClipPlane", ctx.clip ? ctx.clipPlane : vec4(0, 0, 0, 1));
  setFog(s, ctx);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-2, -2);
  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, MAX * 6, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDepthMask(GL_TRUE);
}

// ---------------------------------------------------------------- fumaça
TireSmoke::TireSmoke() {
  p.assign(MAX * 3, 0);
  v.assign(MAX * 3, 0);
  age.assign(MAX, 1e9f);
  life.assign(MAX, 1);
  size.assign(MAX, 0);
  alpha.assign(MAX, 0);
  seed.resize(MAX);
  for (auto& s : seed) s = rnd();
  gpu.resize(MAX * 6);
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, gpu.size() * 4, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 24, nullptr);
  glVertexAttribDivisor(0, 1);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 24, (void*)16);
  glVertexAttribDivisor(1, 1);
  glBindVertexArray(0);
  shader = std::make_unique<Shader>(SMOKE_VS, std::string(FOG_FS) + SMOKE_FS);
}

TireSmoke::~TireSmoke() {
  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
}

void TireSmoke::emit(const std::string& id, const vec3& pos, const vec3& vel, float intensity, float dt) {
  if (intensity <= 0) return;
  float rate = 45 * intensity;  // partículas por segundo por roda
  float a = acc[id] + rate * dt;
  while (a >= 1) {
    a -= 1;
    int i = next;
    next = (next + 1) % MAX;
    p[i * 3] = pos.x + (rnd() - 0.5f) * 0.25f;
    p[i * 3 + 1] = pos.y + 0.15f + rnd() * 0.1f;
    p[i * 3 + 2] = pos.z + (rnd() - 0.5f) * 0.25f;
    v[i * 3] = vel.x * 0.35f + (rnd() - 0.5f) * 1.5f;
    v[i * 3 + 1] = 0.4f + rnd() * 0.6f;
    v[i * 3 + 2] = vel.z * 0.35f + (rnd() - 0.5f) * 1.5f;
    age[i] = 0;
    life[i] = 1.6f + rnd() * 1.8f;
    alpha[i] = 0;
    size[i] = 0.4f;
    seed[i] = rnd();
  }
  acc[id] = a;
}

void TireSmoke::update(float dt, float fovDeg, float viewportH) {
  uScale = viewportH / (2 * std::tan(fovDeg * (float)M_PI / 360));
  uMaxSize = viewportH * 0.4f;
  float drag = std::exp(-dt * 1.8f);
  for (int i = 0; i < MAX; i++) {
    float t = (age[i] += dt);
    float L = life[i];
    if (t > L) { alpha[i] = 0; continue; }
    float k = t / L;
    v[i * 3] *= drag;
    v[i * 3 + 2] *= drag;
    v[i * 3 + 1] = v[i * 3 + 1] * drag + 0.25f * dt;
    p[i * 3] += v[i * 3] * dt;
    p[i * 3 + 1] += v[i * 3 + 1] * dt;
    p[i * 3 + 2] += v[i * 3 + 2] * dt;
    size[i] = 0.5f + k * 3.6f;
    alpha[i] = std::min(1.0f, t * 6) * std::pow(1 - k, 1.6f) * 0.42f;
  }
}

void TireSmoke::clear() {
  std::fill(age.begin(), age.end(), 1e9f);
  std::fill(alpha.begin(), alpha.end(), 0.f);
}

void TireSmoke::draw(const RenderContext& ctx) {
  int n = 0;
  for (int i = 0; i < MAX; i++) {
    if (alpha[i] <= 0) continue;
    float* g = gpu.data() + n * 6;
    g[0] = p[i * 3]; g[1] = p[i * 3 + 1]; g[2] = p[i * 3 + 2]; g[3] = size[i]; g[4] = alpha[i]; g[5] = seed[i];
    n++;
  }
  if (!n) return;
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, n * 24, gpu.data());
  Shader& s = *shader;
  s.use();
  s.set("uView", ctx.view);
  s.set("uProj", ctx.proj);
  s.set("uScale", uScale);
  s.set("uMaxSize", uMaxSize);
  s.set("uViewport", vec2((float)ctx.viewportW, (float)ctx.viewportH));
  s.set("uLight", vec3(1, 0.97f, 0.92f));
  setFog(s, ctx);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glBindVertexArray(vao);
  glDrawArraysInstanced(GL_TRIANGLES, 0, 6, n);
  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
}
