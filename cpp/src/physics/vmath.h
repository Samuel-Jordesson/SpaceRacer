// Matemática mínima da física (double), igual a js/physics/math.js.
#pragma once
#include <cmath>
#include <utility>
#include <vector>

namespace phys {

struct V3 {
  double x = 0, y = 0, z = 0;
  V3() = default;
  V3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
  V3& set(double x_, double y_, double z_) { x = x_; y = y_; z = z_; return *this; }
  V3 operator+(const V3& v) const { return {x + v.x, y + v.y, z + v.z}; }
  V3 operator-(const V3& v) const { return {x - v.x, y - v.y, z - v.z}; }
  V3 operator*(double s) const { return {x * s, y * s, z * s}; }
  V3& addScaled(const V3& v, double s) { x += v.x * s; y += v.y * s; z += v.z * s; return *this; }
  double dot(const V3& v) const { return x * v.x + y * v.y + z * v.z; }
  V3 cross(const V3& v) const { return {y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x}; }
  double length() const { return std::sqrt(x * x + y * y + z * z); }
  V3 normalize() const {
    double l = length();
    return l > 1e-12 ? (*this) * (1 / l) : V3();
  }
};

struct Mat3 {
  double e[9];
  V3 mulV(const V3& v) const {
    return {e[0] * v.x + e[1] * v.y + e[2] * v.z, e[3] * v.x + e[4] * v.y + e[5] * v.z, e[6] * v.x + e[7] * v.y + e[8] * v.z};
  }
  V3 mulTV(const V3& v) const {
    return {e[0] * v.x + e[3] * v.y + e[6] * v.z, e[1] * v.x + e[4] * v.y + e[7] * v.z, e[2] * v.x + e[5] * v.y + e[8] * v.z};
  }
  V3 col(int i) const { return {e[i], e[3 + i], e[6 + i]}; }
};

struct Quat {
  double x = 0, y = 0, z = 0, w = 1;
  Quat() = default;
  Quat(double x_, double y_, double z_, double w_) : x(x_), y(y_), z(z_), w(w_) {}
  static Quat fromAxisAngle(const V3& ax, double angle) {
    double s = std::sin(angle / 2);
    return {ax.x * s, ax.y * s, ax.z * s, std::cos(angle / 2)};
  }
  Quat mul(const Quat& q) const {
    return {w * q.x + x * q.w + y * q.z - z * q.y, w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w, w * q.w - x * q.x - y * q.y - z * q.z};
  }
  Quat& normalize() {
    double l = std::sqrt(x * x + y * y + z * z + w * w);
    if (l == 0) l = 1;
    x /= l; y /= l; z /= l; w /= l;
    return *this;
  }
  // Integra velocidade angular (mundo) por dt.
  Quat& integrate(const V3& wv, double dt) {
    Quat q = Quat(wv.x, wv.y, wv.z, 0).mul(*this);
    x += 0.5 * q.x * dt; y += 0.5 * q.y * dt;
    z += 0.5 * q.z * dt; w += 0.5 * q.w * dt;
    return normalize();
  }
  Mat3 toMat3() const {
    double xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
    return {{1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy),
             2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx),
             2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy)}};
  }
};

inline double clamp(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double sign(double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

// Interpolação linear em tabela {{x, y}, ...}.
inline double table(const std::vector<std::pair<double, double>>& p, double x) {
  if (x <= p[0].first) return p[0].second;
  for (size_t i = 1; i < p.size(); i++) {
    if (x <= p[i].first) {
      auto [x0, y0] = p[i - 1];
      auto [x1, y1] = p[i];
      return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
    }
  }
  return p.back().second;
}

}  // namespace phys
