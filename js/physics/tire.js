// Modelo de pneu Pacejka "Magic Formula" com escorregamento combinado normalizado.
//   y(s) = sin(C · atan(B·s − E·(B·s − atan(B·s))))
// O escorregamento longitudinal (κ) e o ângulo de deriva (tan α) são normalizados pelos
// seus valores de pico; o vetor resultante passa pela mesma curva e a força é repartida
// entre X e Y na direção do escorregamento (círculo de atrito / elipse de atrito).

export class TireModel {
  constructor(p) {
    this.mu = p.mu;
    this.fz0 = p.fz0;
    this.loadSens = p.loadSensitivity;
    this.kPeak = p.slipRatioPeak;
    this.tanAPeak = Math.tan(p.slipAnglePeakDeg * Math.PI / 180);
    this.C = p.C;
    this.E = p.E;
    this.longScale = p.longScale;
    // Escolhe B para que o pico fique exatamente em s = 1.
    this.B = this._solveB();
  }

  _shape(s, B = this.B) {
    const Bs = B * s;
    return Math.sin(this.C * Math.atan(Bs - this.E * (Bs - Math.atan(Bs))));
  }

  _solveB() {
    // Pico quando C·atan(φ) = π/2  →  φ = tan(π/(2C)); resolve B·(1−E) + E·atan(B) = φ.
    const phi = Math.tan(Math.PI / (2 * this.C));
    let lo = 0.01, hi = 50;
    for (let i = 0; i < 80; i++) {
      const B = (lo + hi) / 2;
      const v = B - this.E * (B - Math.atan(B));
      if (v < phi) lo = B; else hi = B;
    }
    return (lo + hi) / 2;
  }

  peakMu(fz, surfaceMu = 1) {
    return surfaceMu * this.mu * Math.max(0.4, 1 + this.loadSens * (fz - this.fz0) / this.fz0);
  }

  // Calcula forças. out recebe: fx, fy (sinal: fy oposto à deriva), dfx (dFx/dκ), s (escorregamento normalizado).
  forces(fz, kappa, tanA, surfaceMu, out) {
    if (fz <= 0) { out.fx = 0; out.fy = 0; out.dfx = 0; out.s = 0; return out; }
    const D = this.peakMu(fz, surfaceMu) * fz;
    const sx = kappa / this.kPeak;
    const sy = tanA / this.tanAPeak;
    const fx = this._fx(D, sx, sy);
    out.fx = fx;
    out.fy = this._fy(D, sx, sy);
    const e = 1e-3;
    out.dfx = (this._fx(D, sx + e, sy) - this._fx(D, sx - e, sy)) / (2 * e) / this.kPeak;
    out.s = Math.hypot(sx, sy);
    return out;
  }

  _ratio(s) {
    // y(s)/s, com limite em s→0 igual a B·C
    return s < 1e-6 ? this.B * this.C : this._shape(s) / s;
  }

  _fx(D, sx, sy) { return D * this.longScale * this._ratio(Math.hypot(sx, sy)) * sx; }
  _fy(D, sx, sy) { return -D * this._ratio(Math.hypot(sx, sy)) * sy; }
}
