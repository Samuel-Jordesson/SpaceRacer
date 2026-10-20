// Matemática mínima, sem dependências, para a física rodar no navegador e no Node.

export class V3 {
  constructor(x = 0, y = 0, z = 0) { this.x = x; this.y = y; this.z = z; }
  set(x, y, z) { this.x = x; this.y = y; this.z = z; return this; }
  copy(v) { this.x = v.x; this.y = v.y; this.z = v.z; return this; }
  clone() { return new V3(this.x, this.y, this.z); }
  add(v) { return new V3(this.x + v.x, this.y + v.y, this.z + v.z); }
  sub(v) { return new V3(this.x - v.x, this.y - v.y, this.z - v.z); }
  scale(s) { return new V3(this.x * s, this.y * s, this.z * s); }
  addScaled(v, s) { this.x += v.x * s; this.y += v.y * s; this.z += v.z * s; return this; }
  dot(v) { return this.x * v.x + this.y * v.y + this.z * v.z; }
  cross(v) {
    return new V3(this.y * v.z - this.z * v.y, this.z * v.x - this.x * v.z, this.x * v.y - this.y * v.x);
  }
  length() { return Math.hypot(this.x, this.y, this.z); }
  normalize() {
    const l = this.length();
    return l > 1e-12 ? this.scale(1 / l) : new V3();
  }
}

export class Quat {
  constructor(x = 0, y = 0, z = 0, w = 1) { this.x = x; this.y = y; this.z = z; this.w = w; }
  static fromAxisAngle(ax, angle) {
    const s = Math.sin(angle / 2);
    return new Quat(ax.x * s, ax.y * s, ax.z * s, Math.cos(angle / 2));
  }
  mul(q) {
    return new Quat(
      this.w * q.x + this.x * q.w + this.y * q.z - this.z * q.y,
      this.w * q.y - this.x * q.z + this.y * q.w + this.z * q.x,
      this.w * q.z + this.x * q.y - this.y * q.x + this.z * q.w,
      this.w * q.w - this.x * q.x - this.y * q.y - this.z * q.z,
    );
  }
  normalize() {
    const l = Math.hypot(this.x, this.y, this.z, this.w) || 1;
    this.x /= l; this.y /= l; this.z /= l; this.w /= l;
    return this;
  }
  // Integra velocidade angular (mundo) por dt.
  integrate(w, dt) {
    const q = new Quat(w.x, w.y, w.z, 0).mul(this);
    this.x += 0.5 * q.x * dt; this.y += 0.5 * q.y * dt;
    this.z += 0.5 * q.z * dt; this.w += 0.5 * q.w * dt;
    return this.normalize();
  }
  toMat3() { return Mat3.fromQuat(this); }
}

// Matriz 3x3 em linhas (row-major).
export class Mat3 {
  constructor(e) { this.e = e; }
  static fromQuat(q) {
    const { x, y, z, w } = q;
    const xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
    return new Mat3([
      1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy),
      2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx),
      2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy),
    ]);
  }
  mulV(v) {
    const e = this.e;
    return new V3(e[0] * v.x + e[1] * v.y + e[2] * v.z, e[3] * v.x + e[4] * v.y + e[5] * v.z, e[6] * v.x + e[7] * v.y + e[8] * v.z);
  }
  mulTV(v) {
    const e = this.e;
    return new V3(e[0] * v.x + e[3] * v.y + e[6] * v.z, e[1] * v.x + e[4] * v.y + e[7] * v.z, e[2] * v.x + e[5] * v.y + e[8] * v.z);
  }
  col(i) { const e = this.e; return new V3(e[i], e[3 + i], e[6 + i]); }
}

export const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
export const lerp = (a, b, t) => a + (b - a) * t;
export const sign = (v) => (v > 0 ? 1 : v < 0 ? -1 : 0);

// Interpolação linear em tabela [[x, y], ...].
export function table(points, x) {
  if (x <= points[0][0]) return points[0][1];
  for (let i = 1; i < points.length; i++) {
    if (x <= points[i][0]) {
      const [x0, y0] = points[i - 1], [x1, y1] = points[i];
      return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
    }
  }
  return points[points.length - 1][1];
}
