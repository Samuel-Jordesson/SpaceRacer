// Transições no estilo CSS: curvas cubic-bezier e valores que animam até um alvo.
#pragma once
#include <cmath>

struct Bezier {
  float x1, y1, x2, y2;
  float operator()(float t) const {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    // resolve x(s) = t por Newton com fallback em bisseção
    auto bx = [&](float s) { float u = 1 - s; return 3 * u * u * s * x1 + 3 * u * s * s * x2 + s * s * s; };
    auto by = [&](float s) { float u = 1 - s; return 3 * u * u * s * y1 + 3 * u * s * s * y2 + s * s * s; };
    float lo = 0, hi = 1, s = t;
    for (int i = 0; i < 24; i++) {
      float x = bx(s);
      if (std::fabs(x - t) < 1e-5f) break;
      if (x < t) lo = s; else hi = s;
      s = (lo + hi) / 2;
    }
    return by(s);
  }
};

inline constexpr Bezier EASE{0.25f, 0.1f, 0.25f, 1};
inline constexpr Bezier EASE_OUT{0, 0, 0.58f, 1};
inline constexpr Bezier LINEAR{0, 0, 1, 1};

// valor com transition: duração, atraso e curva; mudar o alvo parte do valor atual
struct Transition {
  float value = 0, from = 0, to = 0;
  float elapsed = 1e9f, duration = 0.2f, delay = 0;
  Bezier ease = EASE;

  Transition() = default;
  Transition(float v, float dur, Bezier e = EASE, float del = 0) : value(v), from(v), to(v), duration(dur), delay(del), ease(e) {}
  void set(float target) {
    if (target == to) return;
    from = value;
    to = target;
    elapsed = 0;
  }
  void snap(float v) {
    value = from = to = v;
    elapsed = 1e9f;
  }
  void update(float dt) {
    elapsed += dt;
    float t = duration > 0 ? (elapsed - delay) / duration : 1;
    if (t <= 0) value = from;
    else if (t >= 1) value = to;
    else value = from + (to - from) * ease(t);
  }
  bool running() const { return elapsed < delay + duration; }
};
