#include "glyphs.h"

#include <cmath>

namespace {

const Color INK = Color::hex(0x111214);

// cores dos botões de face
Color xboxColor(PadButton b) {
  switch (b) {
    case PadButton::Confirm: return Color::hex(0x6cc04a);  // A
    case PadButton::Back: return Color::hex(0xe5484d);     // B
    case PadButton::West: return Color::hex(0x2f8be6);     // X
    case PadButton::North: return Color::hex(0xf4c20d);    // Y
    default: return Color(1, 1, 1);
  }
}
Color psColor(PadButton b) {
  switch (b) {
    case PadButton::Confirm: return Color::hex(0x7cb2e8);  // ✕
    case PadButton::Back: return Color::hex(0xff6b6b);     // ○
    case PadButton::West: return Color::hex(0xd88ec9);     // □
    case PadButton::North: return Color::hex(0x40e2a0);    // △
    default: return Color(1, 1, 1);
  }
}
const char* xboxLetter(PadButton b) {
  switch (b) {
    case PadButton::Confirm: return "A";
    case PadButton::Back: return "B";
    case PadButton::West: return "X";
    case PadButton::North: return "Y";
    default: return "";
  }
}
bool isFace(PadButton b) { return b == PadButton::Confirm || b == PadButton::Back || b == PadButton::West || b == PadButton::North; }

// tecla do teclado: caixa com contorno e o texto dentro
float keycap(UI& ui, const std::string& key, float x, float cy, float size, const PromptTheme& th, bool draw) {
  Color fg = th.dark ? Color(1, 1, 1, 0.75f) : INK;
  float h = size;
  if (key == "space") {
    float w = size * 2.2f;
    if (draw) {
      ui.border({x, cy - h / 2, w, h}, fg, 1.2f, 4);
      float sx = x + w * 0.3f, sw = w * 0.4f, sy = cy + h * 0.12f;
      ui.rect({sx, sy, sw, 1.5f}, fg);
      ui.rect({sx, sy - h * 0.22f, 1.5f, h * 0.22f + 1.5f}, fg);
      ui.rect({sx + sw - 1.5f, sy - h * 0.22f, 1.5f, h * 0.22f + 1.5f}, fg);
    }
    return w;
  }
  TextStyle st{Font::Barlow500, size * 0.55f, size * 0.05f, fg};
  float w = std::max(h, ui.measure(key, st) + size * 0.7f);
  if (draw) {
    ui.border({x, cy - h / 2, w, h}, th.dark ? Color(1, 1, 1, 0.3f) : INK.withAlpha(0.6f), 1, 3);
    ui.text(key, x + (w - ui.measure(key, st)) / 2, cy - size * 0.3f, st, size * 0.6f);
  }
  return w;
}

// símbolos do PlayStation
void psSymbol(UI& ui, PadButton b, float cx, float cy, float r, const Color& c) {
  float s = r * 0.5f, lw = std::max(1.5f, r * 0.16f);
  switch (b) {
    case PadButton::Confirm:  // ✕
      ui.rotatedRect({cx - s, cy - lw / 2, s * 2, lw}, cx, cy, 0.785f, c, lw / 2);
      ui.rotatedRect({cx - s, cy - lw / 2, s * 2, lw}, cx, cy, -0.785f, c, lw / 2);
      break;
    case PadButton::Back:  // ○
      ui.ring(cx, cy, s * 0.85f, lw, c);
      break;
    case PadButton::West:  // □
      ui.border({cx - s * 0.75f, cy - s * 0.75f, s * 1.5f, s * 1.5f}, c, lw, 1);
      break;
    case PadButton::North: {  // △
      float t = s * 0.95f;
      vec2 a{cx, cy - t}, bl{cx - t * 0.95f, cy + t * 0.65f}, br{cx + t * 0.95f, cy + t * 0.65f};
      ui.line(a.x, a.y, bl.x, bl.y, lw, c);
      ui.line(bl.x, bl.y, br.x, br.y, lw, c);
      ui.line(br.x, br.y, a.x, a.y, lw, c);
      break;
    }
    default: break;
  }
}

// direcional: cruz com as setas do eixo destacadas
float dpad(UI& ui, PadButton b, float x, float cy, float size, const PromptTheme& th, bool draw) {
  float w = size;
  if (!draw) return w;
  float cx = x + w / 2, a = size * 0.5f, t = size * 0.3f;
  Color base = th.dark ? Color(1, 1, 1, 0.3f) : INK.withAlpha(0.25f);
  Color hi = th.dark ? Color(1, 1, 1, 0.95f) : INK;
  ui.rect({cx - a, cy - t / 2, a * 2, t}, b == PadButton::DPadH ? hi : base, 2);
  ui.rect({cx - t / 2, cy - a, t, a * 2}, b == PadButton::DPadV ? hi : base, 2);
  return w;
}

float shoulder(UI& ui, PadStyle style, PadButton b, float x, float cy, float size, const PromptTheme& th, bool draw) {
  bool ps = style == PadStyle::PlayStation, left = b == PadButton::LB || b == PadButton::LT;
  bool trig = b == PadButton::LT || b == PadButton::RT;
  std::string t = trig ? (ps ? (left ? "L2" : "R2") : (left ? "LT" : "RT")) : (ps ? (left ? "L1" : "R1") : (left ? "LB" : "RB"));
  TextStyle st{Font::BC600, size * 0.55f, size * 0.04f, th.dark ? Color(1, 1, 1, 0.9f) : Color(1, 1, 1)};
  float w = ui.measure(t, st) + size * 0.8f, h = size * 0.82f;
  if (draw) {
    Rect r{x, cy - h / 2, w, h};
    // ombro arredondado do lado de fora
    ui.rect(r, th.dark ? Color(1, 1, 1, 0.16f) : INK, h * 0.42f);
    ui.text(t, x + (w - ui.measure(t, st)) / 2, cy - size * 0.29f, st, size * 0.58f);
  }
  return w;
}

float menuButton(UI& ui, PadStyle style, PadButton b, float x, float cy, float size, const PromptTheme& th, bool draw) {
  Color bg = th.dark ? Color(1, 1, 1, 0.16f) : INK;
  Color fg = Color(1, 1, 1, 0.95f);
  if (style == PadStyle::PlayStation) {
    std::string t = b == PadButton::Start ? "OPTIONS" : "SHARE";
    TextStyle st{Font::BC600, size * 0.42f, size * 0.05f, fg};
    float w = ui.measure(t, st) + size * 0.7f, h = size * 0.7f;
    if (draw) {
      ui.rect({x, cy - h / 2, w, h}, bg, h / 2);
      ui.text(t, x + (w - ui.measure(t, st)) / 2, cy - size * 0.22f, st, size * 0.44f);
    }
    return w;
  }
  float w = size;
  if (draw) {
    float cx = x + w / 2;
    ui.circle(cx, cy, size / 2, bg);
    float s = size * 0.22f, lw = std::max(1.2f, size * 0.07f);
    if (b == PadButton::Start) {  // ≡ (menu)
      for (int i = -1; i <= 1; i++) ui.rect({cx - s, cy + i * s * 0.6f - lw / 2, s * 2, lw}, fg);
    } else {  // ⧉ (exibir)
      ui.border({cx - s * 0.9f, cy - s * 0.8f, s * 1.3f, s * 1.1f}, fg, lw, 1);
      ui.border({cx - s * 0.4f, cy - s * 0.3f, s * 1.3f, s * 1.1f}, fg, lw, 1);
    }
  }
  return w;
}

float draw(UI& ui, PadStyle style, PadButton b, float x, float cy, float size, const std::string& key, const PromptTheme& th, bool doDraw) {
  if (style == PadStyle::Keyboard) return keycap(ui, key, x, cy, size, th, doDraw);
  if (b == PadButton::DPadH || b == PadButton::DPadV) return dpad(ui, b, x, cy, size, th, doDraw);
  if (b == PadButton::DPadUp) {
    if (doDraw) {
      float cx = x + size / 2, a = size * 0.5f, t = size * 0.3f;
      Color base = th.dark ? Color(1, 1, 1, 0.3f) : INK.withAlpha(0.25f), hi = th.dark ? Color(1, 1, 1, 0.95f) : INK;
      ui.rect({cx - a, cy - t / 2, a * 2, t}, base, 2);
      ui.rect({cx - t / 2, cy - a, t, a * 2}, base, 2);
      ui.rect({cx - t / 2, cy - a, t, a}, hi, 2);
    }
    return size;
  }
  if (b == PadButton::LStick) {
    if (doDraw) {
      float cx = x + size / 2;
      ui.circle(cx, cy, size / 2, th.dark ? Color(1, 1, 1, 0.16f) : INK);
      ui.ring(cx, cy, size * 0.28f, std::max(1.2f, size * 0.07f), Color(1, 1, 1, 0.9f));
      TextStyle st{Font::BC600, size * 0.3f, 0, Color(1, 1, 1, 0.9f)};
      ui.text("L", cx - ui.measure("L", st) / 2, cy - size * 0.16f, st, size * 0.32f);
    }
    return size;
  }
  if (b == PadButton::LB || b == PadButton::RB || b == PadButton::LT || b == PadButton::RT) return shoulder(ui, style, b, x, cy, size, th, doDraw);
  if (b == PadButton::Start || b == PadButton::Select) return menuButton(ui, style, b, x, cy, size, th, doDraw);
  if (!isFace(b)) return 0;
  float w = size;
  if (!doDraw) return w;
  float cx = x + w / 2, r = size / 2;
  Color disc = Color::hex(0x1d1f24);
  if (style == PadStyle::Xbox) {
    ui.circle(cx, cy, r, disc);
    TextStyle st{Font::BC700, size * 0.62f, 0, xboxColor(b)};
    const char* l = xboxLetter(b);
    ui.text(l, cx - ui.measure(l, st) / 2, cy - size * 0.33f, st, size * 0.66f);
  } else if (style == PadStyle::PlayStation) {
    ui.circle(cx, cy, r, disc);
    psSymbol(ui, b, cx, cy, r, psColor(b));
  } else {
    // genérico: posição do botão no losango (o de baixo confirma)
    ui.circle(cx, cy, r, disc);
    float d = r * 0.42f, dot = r * 0.2f;
    vec2 pos[4] = {{cx, cy + d}, {cx + d, cy}, {cx - d, cy}, {cx, cy - d}};
    int idx = b == PadButton::Confirm ? 0 : b == PadButton::Back ? 1 : b == PadButton::West ? 2 : 3;
    for (int i = 0; i < 4; i++) ui.circle(pos[i].x, pos[i].y, dot, i == idx ? Color(1, 1, 1) : Color(1, 1, 1, 0.25f));
  }
  if (th.dark) ui.ring(cx, cy, r, 1, Color(1, 1, 1, 0.25f));
  return w;
}

}  // namespace

float drawPrompt(UI& ui, PadStyle style, PadButton b, float x, float cy, float size, const std::string& key, PromptTheme th) {
  return draw(ui, style, b, x, cy, size, key, th, true);
}

float promptWidth(UI& ui, PadStyle style, PadButton b, float size, const std::string& key) {
  return draw(ui, style, b, 0, 0, size, key, {}, false);
}
