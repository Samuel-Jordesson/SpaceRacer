// Matemática 3D da renderização (float), com as mesmas convenções do three.js:
// matrizes 4x4 em colunas (column-major), câmera olhando para −Z, Y para cima.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct vec2 {
  float x = 0, y = 0;
  vec2() = default;
  vec2(float x_, float y_) : x(x_), y(y_) {}
  vec2 operator+(vec2 o) const { return {x + o.x, y + o.y}; }
  vec2 operator-(vec2 o) const { return {x - o.x, y - o.y}; }
  vec2 operator*(float s) const { return {x * s, y * s}; }
};

struct vec3 {
  float x = 0, y = 0, z = 0;
  vec3() = default;
  constexpr vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  explicit constexpr vec3(float s) : x(s), y(s), z(s) {}
  vec3 operator+(const vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  vec3 operator-(const vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  vec3 operator-() const { return {-x, -y, -z}; }
  vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  vec3 operator*(const vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
  vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  vec3& operator-=(const vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
  float operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
  float& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
};
inline float dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3& a, const vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const vec3& v) { return std::sqrt(dot(v, v)); }
inline float distance(const vec3& a, const vec3& b) { return length(a - b); }
inline vec3 normalize(const vec3& v) {
  float l = length(v);
  return l > 0 ? v / l : vec3();
}
inline vec3 lerp(const vec3& a, const vec3& b, float t) { return a + (b - a) * t; }
inline vec3 vmin(const vec3& a, const vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline vec3 vmax(const vec3& a, const vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

struct vec4 {
  float x = 0, y = 0, z = 0, w = 0;
  vec4() = default;
  constexpr vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  vec4(const vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
  vec3 xyz() const { return {x, y, z}; }
};

inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline float smoothstepf(float a, float b, float x) {
  float t = clampf((x - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
}

// cor hexadecimal sRGB → linear (como THREE.Color com gestão de cor ativa)
inline float srgbToLinear(float c) { return c < 0.04045f ? c * 0.0773993808f : std::pow(c * 0.9478672986f + 0.0521327014f, 2.4f); }
inline vec3 hexColor(uint32_t hex) {
  return {srgbToLinear(((hex >> 16) & 255) / 255.f), srgbToLinear(((hex >> 8) & 255) / 255.f), srgbToLinear((hex & 255) / 255.f)};
}

struct quat {
  float x = 0, y = 0, z = 0, w = 1;
  quat() = default;
  quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  static quat fromAxisAngle(const vec3& ax, float angle) {
    float s = std::sin(angle / 2);
    return {ax.x * s, ax.y * s, ax.z * s, std::cos(angle / 2)};
  }
  quat operator*(const quat& b) const {
    return {x * b.w + w * b.x + y * b.z - z * b.y, y * b.w + w * b.y + z * b.x - x * b.z,
            z * b.w + w * b.z + x * b.y - y * b.x, w * b.w - x * b.x - y * b.y - z * b.z};
  }
  quat conjugate() const { return {-x, -y, -z, w}; }
  vec3 rotate(const vec3& v) const {
    // v' = q v q*
    float ix = w * v.x + y * v.z - z * v.y, iy = w * v.y + z * v.x - x * v.z;
    float iz = w * v.z + x * v.y - y * v.x, iw = -x * v.x - y * v.y - z * v.z;
    return {ix * w + iw * -x + iy * -z - iz * -y, iy * w + iw * -y + iz * -x - ix * -z, iz * w + iw * -z + ix * -y - iy * -x};
  }
  quat normalized() const {
    float l = std::sqrt(x * x + y * y + z * z + w * w);
    return l > 0 ? quat(x / l, y / l, z / l, w / l) : quat();
  }
};

struct mat4 {
  float e[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // colunas

  static mat4 identity() { return mat4(); }
  float& operator()(int r, int c) { return e[c * 4 + r]; }
  float operator()(int r, int c) const { return e[c * 4 + r]; }

  mat4 operator*(const mat4& b) const {
    mat4 m;
    for (int c = 0; c < 4; c++)
      for (int r = 0; r < 4; r++) {
        float s = 0;
        for (int k = 0; k < 4; k++) s += (*this)(r, k) * b(k, c);
        m(r, c) = s;
      }
    return m;
  }
  vec4 operator*(const vec4& v) const {
    return {e[0] * v.x + e[4] * v.y + e[8] * v.z + e[12] * v.w, e[1] * v.x + e[5] * v.y + e[9] * v.z + e[13] * v.w,
            e[2] * v.x + e[6] * v.y + e[10] * v.z + e[14] * v.w, e[3] * v.x + e[7] * v.y + e[11] * v.z + e[15] * v.w};
  }
  // ponto (com divisão perspectiva, como Vector3.applyMatrix4)
  vec3 point(const vec3& v) const {
    float w = e[3] * v.x + e[7] * v.y + e[11] * v.z + e[15];
    w = w != 0 ? 1 / w : 1;
    return {(e[0] * v.x + e[4] * v.y + e[8] * v.z + e[12]) * w, (e[1] * v.x + e[5] * v.y + e[9] * v.z + e[13]) * w,
            (e[2] * v.x + e[6] * v.y + e[10] * v.z + e[14]) * w};
  }
  vec3 dir(const vec3& v) const {
    return {e[0] * v.x + e[4] * v.y + e[8] * v.z, e[1] * v.x + e[5] * v.y + e[9] * v.z, e[2] * v.x + e[6] * v.y + e[10] * v.z};
  }
  vec3 position() const { return {e[12], e[13], e[14]}; }
  vec3 col(int i) const { return {e[i * 4], e[i * 4 + 1], e[i * 4 + 2]}; }

  static mat4 compose(const vec3& p, const quat& q, const vec3& s) {
    mat4 m;
    float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2, yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
    float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
    m.e[0] = (1 - (yy + zz)) * s.x; m.e[1] = (xy + wz) * s.x; m.e[2] = (xz - wy) * s.x; m.e[3] = 0;
    m.e[4] = (xy - wz) * s.y; m.e[5] = (1 - (xx + zz)) * s.y; m.e[6] = (yz + wx) * s.y; m.e[7] = 0;
    m.e[8] = (xz + wy) * s.z; m.e[9] = (yz - wx) * s.z; m.e[10] = (1 - (xx + yy)) * s.z; m.e[11] = 0;
    m.e[12] = p.x; m.e[13] = p.y; m.e[14] = p.z; m.e[15] = 1;
    return m;
  }
  static mat4 translation(const vec3& p) { return compose(p, quat(), vec3(1)); }
  static mat4 scaling(const vec3& s) { return compose(vec3(), quat(), s); }
  static mat4 basis(const vec3& x, const vec3& y, const vec3& z) {
    mat4 m;
    m.e[0] = x.x; m.e[1] = x.y; m.e[2] = x.z;
    m.e[4] = y.x; m.e[5] = y.y; m.e[6] = y.z;
    m.e[8] = z.x; m.e[9] = z.y; m.e[10] = z.z;
    return m;
  }

  float determinant() const {
    const float* m = e;
    float n11 = m[0], n12 = m[4], n13 = m[8], n14 = m[12], n21 = m[1], n22 = m[5], n23 = m[9], n24 = m[13];
    float n31 = m[2], n32 = m[6], n33 = m[10], n34 = m[14], n41 = m[3], n42 = m[7], n43 = m[11], n44 = m[15];
    return n41 * (+n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34) +
           n42 * (+n11 * n23 * n34 - n11 * n24 * n33 + n14 * n21 * n33 - n13 * n21 * n34 + n13 * n24 * n31 - n14 * n23 * n31) +
           n43 * (+n11 * n24 * n32 - n11 * n22 * n34 - n14 * n21 * n32 + n12 * n21 * n34 + n14 * n22 * n31 - n12 * n24 * n31) +
           n44 * (-n13 * n22 * n31 - n11 * n23 * n32 + n11 * n22 * n33 + n13 * n21 * n32 - n12 * n21 * n33 + n12 * n23 * n31);
  }

  mat4 inverse() const {
    const float* m = e;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    mat4 r;
    if (det == 0) {
      for (float& v : r.e) v = 0;
      return r;
    }
    det = 1 / det;
    for (int i = 0; i < 16; i++) r.e[i] = inv[i] * det;
    return r;
  }

  void decompose(vec3& p, quat& q, vec3& s) const;

  // Matrix4.lookAt do three: só a rotação (eixo −Z olha para o alvo)
  static mat4 lookAt(const vec3& eye, const vec3& target, const vec3& up) {
    vec3 z = eye - target;
    if (dot(z, z) == 0) z.z = 1;
    z = normalize(z);
    vec3 x = cross(up, z);
    if (dot(x, x) == 0) {
      vec3 zz = z;
      if (std::fabs(up.z) == 1) zz.x += 0.0001f; else zz.z += 0.0001f;
      z = normalize(zz);
      x = cross(up, z);
    }
    x = normalize(x);
    vec3 y = cross(z, x);
    return basis(x, y, z);
  }
  static mat4 perspective(float fovDeg, float aspect, float near, float far) {
    float top = near * std::tan(fovDeg * float(M_PI) / 360.f), height = 2 * top, width = aspect * height;
    float left = -0.5f * width;
    mat4 m;
    float x = 2 * near / width, y = 2 * near / height;
    float a = (left + width + left) / width, b = (top + top - height) / height;
    float c = -(far + near) / (far - near), d = -2 * far * near / (far - near);
    m.e[0] = x; m.e[4] = 0; m.e[8] = a; m.e[12] = 0;
    m.e[1] = 0; m.e[5] = y; m.e[9] = b; m.e[13] = 0;
    m.e[2] = 0; m.e[6] = 0; m.e[10] = c; m.e[14] = d;
    m.e[3] = 0; m.e[7] = 0; m.e[11] = -1; m.e[15] = 0;
    return m;
  }
  static mat4 ortho(float l, float r, float t, float b, float n, float f) {
    mat4 m;
    float w = 1 / (r - l), h = 1 / (t - b), p = 1 / (f - n);
    m.e[0] = 2 * w; m.e[5] = 2 * h; m.e[10] = -2 * p;
    m.e[12] = -(r + l) * w; m.e[13] = -(t + b) * h; m.e[14] = -(f + n) * p;
    return m;
  }
};

// rotação de uma matriz de rotação pura → quaternion (Quaternion.setFromRotationMatrix)
inline quat quatFromMatrix(const mat4& m) {
  float m11 = m.e[0], m12 = m.e[4], m13 = m.e[8], m21 = m.e[1], m22 = m.e[5], m23 = m.e[9], m31 = m.e[2], m32 = m.e[6], m33 = m.e[10];
  float trace = m11 + m22 + m33;
  quat q;
  if (trace > 0) {
    float s = 0.5f / std::sqrt(trace + 1.0f);
    q.w = 0.25f / s; q.x = (m32 - m23) * s; q.y = (m13 - m31) * s; q.z = (m21 - m12) * s;
  } else if (m11 > m22 && m11 > m33) {
    float s = 2.0f * std::sqrt(1.0f + m11 - m22 - m33);
    q.w = (m32 - m23) / s; q.x = 0.25f * s; q.y = (m12 + m21) / s; q.z = (m13 + m31) / s;
  } else if (m22 > m33) {
    float s = 2.0f * std::sqrt(1.0f + m22 - m11 - m33);
    q.w = (m13 - m31) / s; q.x = (m12 + m21) / s; q.y = 0.25f * s; q.z = (m23 + m32) / s;
  } else {
    float s = 2.0f * std::sqrt(1.0f + m33 - m11 - m22);
    q.w = (m21 - m12) / s; q.x = (m13 + m31) / s; q.y = (m23 + m32) / s; q.z = 0.25f * s;
  }
  return q;
}

inline void mat4::decompose(vec3& p, quat& q, vec3& s) const {
  float sx = length(col(0)), sy = length(col(1)), sz = length(col(2));
  if (determinant() < 0) sx = -sx;
  p = position();
  mat4 m = *this;
  float ix = 1 / sx, iy = 1 / sy, iz = 1 / sz;
  for (int i = 0; i < 3; i++) { m.e[i] *= ix; m.e[4 + i] *= iy; m.e[8 + i] *= iz; }
  q = quatFromMatrix(m);
  s = {sx, sy, sz};
}

// matriz normal (3x3 inversa transposta) guardada numa mat4
inline mat4 normalMatrix(const mat4& m) {
  mat4 inv = m.inverse(), r;
  for (int c = 0; c < 3; c++)
    for (int rr = 0; rr < 3; rr++) r(rr, c) = inv(c, rr);
  r.e[12] = r.e[13] = r.e[14] = 0;
  return r;
}

struct Box3 {
  vec3 min{std::numeric_limits<float>::infinity()}, max{-std::numeric_limits<float>::infinity()};
  bool empty() const { return max.x < min.x || max.y < min.y || max.z < min.z; }
  void expand(const vec3& p) { min = vmin(min, p); max = vmax(max, p); }
  void expand(const Box3& b) {
    if (b.empty()) return;
    min = vmin(min, b.min); max = vmax(max, b.max);
  }
  vec3 center() const { return empty() ? vec3() : (min + max) * 0.5f; }
  vec3 size() const { return empty() ? vec3() : max - min; }
  Box3 transformed(const mat4& m) const {
    Box3 b;
    if (empty()) return b;
    for (int i = 0; i < 8; i++) b.expand(m.point({i & 1 ? max.x : min.x, i & 2 ? max.y : min.y, i & 4 ? max.z : min.z}));
    return b;
  }
};
