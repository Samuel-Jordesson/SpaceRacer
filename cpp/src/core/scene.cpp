#include "scene.h"

#include <algorithm>

// ---------------------------------------------------------------- textura
void Texture::upload() {
  if (id || (pixels.empty() && halfPixels.empty() && floatPixels.empty())) return;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (!floatPixels.empty())
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, floatPixels.data());
  else if (!halfPixels.empty())
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, halfPixels.data());
  else
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  if (mipmaps) {
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  }
  if (anisotropy > 1) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, std::min(anisotropy, gl::maxAnisotropy()));
  pixels = {};
  halfPixels = {};
  floatPixels = {};
}

void Texture::release() {
  if (id) glDeleteTextures(1, &id);
  id = 0;
}
Texture::~Texture() { release(); }
DecalSet::~DecalSet() {
  if (array) glDeleteTextures(1, &array);
}

// ---------------------------------------------------------------- geometria
void Geometry::computeBounds() {
  box = Box3();
  for (auto& p : pos) box.expand(p);
  sphereCenter = box.center();
  float r2 = 0;
  for (auto& p : pos) {
    vec3 d = p - sphereCenter;
    r2 = std::max(r2, dot(d, d));
  }
  sphereRadius = std::sqrt(r2);
}

void Geometry::upload() {
  if (vao) return;
  size_t n = pos.size();
  if (nrm.size() != n) nrm.assign(n, vec3(0, 1, 0));
  if (uv0.size() != n) uv0.assign(n, vec2());
  if (uv1.size() != n) uv1 = uv0;
  if (tan.size() != n) tan.assign(n, vec4(1, 0, 0, 1));
  if (idx.empty()) {
    idx.resize(n);
    for (size_t i = 0; i < n; i++) idx[i] = (uint32_t)i;
  }
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(5, vbo);
  auto attr = [&](int loc, GLuint buf, const void* data, size_t bytes, int comps) {
    glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, bytes, data, GL_STATIC_DRAW);
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, comps, GL_FLOAT, GL_FALSE, 0, nullptr);
  };
  attr(0, vbo[0], pos.data(), n * sizeof(vec3), 3);
  attr(1, vbo[1], nrm.data(), n * sizeof(vec3), 3);
  attr(2, vbo[2], uv0.data(), n * sizeof(vec2), 2);
  attr(3, vbo[3], uv1.data(), n * sizeof(vec2), 2);
  attr(4, vbo[4], tan.data(), n * sizeof(vec4), 4);
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
  count = (GLsizei)idx.size();
  glBindVertexArray(0);
}

void Geometry::release() {
  if (vao) {
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(5, vbo);
    glDeleteBuffers(1, &ebo);
  }
  vao = 0;
}
Geometry::~Geometry() { release(); }

// ---------------------------------------------------------------- nó
void Node::add(const NodePtr& child) {
  if (child->parent) child->parent->remove(child.get());
  child->parent = this;
  children.push_back(child);
}

void Node::remove(Node* child) {
  auto it = std::find_if(children.begin(), children.end(), [&](const NodePtr& c) { return c.get() == child; });
  if (it != children.end()) {
    (*it)->parent = nullptr;
    children.erase(it);
  }
}

void Node::attach(const NodePtr& child) {
  // world(child) = world(this) · local'  →  local' = world(this)⁻¹ · world(child)
  updateMatrixWorld();
  child->updateMatrixWorld();
  mat4 m = matrixWorld.inverse() * child->matrixWorld;
  add(child);
  m.decompose(child->position, child->quaternion, child->scale);
  child->updateMatrixWorld();
}

static void updateWorldRec(Node& n, const mat4* parentWorld) {
  n.updateMatrix();
  n.matrixWorld = parentWorld ? (*parentWorld) * n.matrix : n.matrix;
  for (auto& c : n.children) updateWorldRec(*c, &n.matrixWorld);
}

void Node::updateMatrixWorld() {
  // garante que os pais estejam atualizados (equivale a updateWorldMatrix(true, true))
  std::vector<Node*> chain;
  for (Node* p = parent; p; p = p->parent) chain.push_back(p);
  const mat4* pw = nullptr;
  mat4 acc;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    (*it)->updateMatrix();
    acc = pw ? acc * (*it)->matrix : (*it)->matrix;
    (*it)->matrixWorld = acc;
    pw = &acc;
  }
  updateWorldRec(*this, pw);
}

void Node::traverse(const std::function<void(Node&)>& fn) {
  fn(*this);
  auto copy = children;  // permite mudar a hierarquia durante a visita
  for (auto& c : copy) c->traverse(fn);
}

Box3 worldBox(Node& n) {
  Box3 b;
  n.traverse([&](Node& o) {
    if (!o.isMesh()) return;
    for (auto& p : o.geometry->pos) b.expand(o.matrixWorld.point(p));
  });
  return b;
}

std::vector<RayHitInfo> raycast(Node& root, const vec3& origin, const vec3& dir, float near, float far) {
  std::vector<RayHitInfo> hits;
  root.traverse([&](Node& o) {
    if (!o.isMesh()) return;
    const Geometry& g = *o.geometry;
    const mat4& m = o.matrixWorld;
    // teste rápido contra a caixa no mundo
    Box3 wb = g.box.transformed(m);
    float t0 = 0, t1 = far;
    for (int a = 0; a < 3; a++) {
      float d = dir[a], o0 = origin[a];
      if (std::fabs(d) < 1e-12f) {
        if (o0 < wb.min[a] || o0 > wb.max[a]) return;
        continue;
      }
      float ta = (wb.min[a] - o0) / d, tb = (wb.max[a] - o0) / d;
      if (ta > tb) std::swap(ta, tb);
      t0 = std::max(t0, ta);
      t1 = std::min(t1, tb);
      if (t0 > t1) return;
    }
    size_t tri = g.idx.empty() ? g.pos.size() / 3 : g.idx.size() / 3;
    auto vi = [&](size_t k) { return g.idx.empty() ? (uint32_t)k : g.idx[k]; };
    for (size_t f = 0; f < tri; f++) {
      vec3 a = m.point(g.pos[vi(f * 3)]), b = m.point(g.pos[vi(f * 3 + 1)]), c = m.point(g.pos[vi(f * 3 + 2)]);
      // Möller–Trumbore, dupla face
      vec3 e1 = b - a, e2 = c - a, p = cross(dir, e2);
      float det = dot(e1, p);
      if (std::fabs(det) < 1e-12f) continue;
      float inv = 1 / det;
      vec3 s = origin - a;
      float u = dot(s, p) * inv;
      if (u < 0 || u > 1) continue;
      vec3 q = cross(s, e1);
      float v = dot(dir, q) * inv;
      if (v < 0 || u + v > 1) continue;
      float t = dot(e2, q) * inv;
      if (t < near || t > far) continue;
      hits.push_back({t, origin + dir * t, &o});
    }
  });
  std::sort(hits.begin(), hits.end(), [](auto& x, auto& y) { return x.distance < y.distance; });
  return hits;
}
