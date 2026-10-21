// Modelo de pneu Pacejka "Magic Formula" com escorregamento combinado normalizado.
//   y(s) = sin(C · atan(B·s − E·(B·s − atan(B·s))))
// O escorregamento longitudinal (κ) e o ângulo de deriva (tan α) são normalizados pelos
// seus valores de pico; o vetor resultante passa pela mesma curva e a força é repartida
// entre X e Y na direção do escorregamento (elipse de atrito).
#pragma once
#include <cmath>
#include "carspec.h"

namespace phys {

struct TireOut {
  double fx = 0, fy = 0, dfx = 0, s = 0;
};

class TireModel {
 public:
  double mu, fz0, loadSens, kPeak, tanAPeak, C, E, longScale, B;

  explicit TireModel(const TireParams& p)
      : mu(p.mu), fz0(p.fz0), loadSens(p.loadSensitivity), kPeak(p.slipRatioPeak),
        tanAPeak(std::tan(p.slipAnglePeakDeg * M_PI / 180)), C(p.C), E(p.E), longScale(p.longScale) {
    // Escolhe B para que o pico fique exatamente em s = 1.
    B = solveB();
  }

  double peakMu(double fz, double surfaceMu = 1) const {
    return surfaceMu * mu * std::fmax(0.4, 1 + loadSens * (fz - fz0) / fz0);
  }

  // fx, fy (fy oposto à deriva), dfx (dFx/dκ), s (escorregamento normalizado)
  void forces(double fz, double kappa, double tanA, double surfaceMu, TireOut& out) const {
    if (fz <= 0) { out = TireOut{}; return; }
    double D = peakMu(fz, surfaceMu) * fz;
    double sx = kappa / kPeak, sy = tanA / tanAPeak;
    out.fx = fxf(D, sx, sy);
    out.fy = fyf(D, sx, sy);
    const double e = 1e-3;
    out.dfx = (fxf(D, sx + e, sy) - fxf(D, sx - e, sy)) / (2 * e) / kPeak;
    out.s = std::hypot(sx, sy);
  }

 private:
  double shape(double s) const {
    double Bs = B * s;
    return std::sin(C * std::atan(Bs - E * (Bs - std::atan(Bs))));
  }
  double solveB() const {
    // Pico quando C·atan(φ) = π/2 → φ = tan(π/(2C)); resolve B·(1−E) + E·atan(B) = φ.
    double phi = std::tan(M_PI / (2 * C));
    double lo = 0.01, hi = 50;
    for (int i = 0; i < 80; i++) {
      double b = (lo + hi) / 2;
      double v = b - E * (b - std::atan(b));
      if (v < phi) lo = b; else hi = b;
    }
    return (lo + hi) / 2;
  }
  // y(s)/s, com limite em s→0 igual a B·C
  double ratio(double s) const { return s < 1e-6 ? B * C : shape(s) / s; }
  double fxf(double D, double sx, double sy) const { return D * longScale * ratio(std::hypot(sx, sy)) * sx; }
  double fyf(double D, double sx, double sy) const { return -D * ratio(std::hypot(sx, sy)) * sy; }
};

}  // namespace phys
