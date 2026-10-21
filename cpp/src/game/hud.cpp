#include "hud.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>

namespace {
const Color FG = Color::hex(0xf2f2f2);
const Color DIM(1, 1, 1, 0.55f);
const Color PANEL(10 / 255.f, 12 / 255.f, 16 / 255.f, 0.62f);
const Color RED = Color::hex(0xe8202a);
const Color AMBER = Color::hex(0xffb400);
const Color GREEN = Color::hex(0x2bd94a);
const Color BLUE = Color::hex(0x2f7bff);
const Color TRACK = Color::hex(0x1b1d22);

// quebra o texto em linhas que caibam em maxW
std::vector<std::string> wrap(UI& ui, const std::string& text, const TextStyle& st, float maxW) {
  std::vector<std::string> lines;
  std::string cur, word;
  auto flushWord = [&] {
    if (word.empty()) return;
    std::string cand = cur.empty() ? word : cur + " " + word;
    if (!cur.empty() && ui.measure(cand, st) > maxW) {
      lines.push_back(cur);
      cur = word;
    } else {
      cur = cand;
    }
    word.clear();
  };
  for (char c : text) {
    if (c == ' ') flushWord();
    else word += c;
  }
  flushWord();
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}
}  // namespace

Hud::Hud(const phys::CarSpec& s) : spec(s) {
  for (auto& c : tireColor) c = Color::hex(0x333333);
}

void Hud::flash(const std::string& text, float sec) {
  msg = text;
  msgOpacity.set(1);
  msgT = sec;
}

void Hud::update(const phys::Vehicle& car, const Input& input, const std::string& camName, float dt) {
  const auto& es = spec.engine;
  clock += dt;
  double rpm = car.engine.rpm;
  speed = (int)std::lround(std::fabs(car.forwardSpeed()) * 3.6);
  gear = car.gear == 0 ? "N" : car.gear < 0 ? "R" : std::to_string(car.gear);
  shifting = (bool)car.shift;
  rpmFill = (float)std::min(1.0, rpm / (es.limiterRpm + 500));
  rpmText = (int)std::lround(rpm / 10) * 10;
  double start = es.limiterRpm - 1600;
  lit = (int)std::max(0.0, std::min(10.0, std::floor(((rpm - start) / 1450) * 10)));
  blink = rpm > es.limiterRpm - 150 && ((int)std::floor(SDL_GetTicks() / 70.0) % 2);
  thr = input.throttle;
  brk = input.brake;
  clutch = (float)(1 - car.clutch);
  steer = input.steer;

  msgOpacity.update(dt);
  if (msgT > 0) {
    msgT -= dt;
    if (msgT <= 0) msgOpacity.set(0);
  }
  if (car.overrevBlocked > 0.55) flash("Redução bloqueada: rotação excessiva", 0.8f);

  // atualizações mais lentas (~20 Hz)
  slow += dt;
  frames++;
  fpsT += dt;
  if (fpsT > 0.5f) {
    fps = std::to_string((int)std::lround(frames / fpsT)) + " fps";
    frames = 0;
    fpsT = 0;
  }
  if (slow < 0.05f) return;
  slow = 0;
  absOn = car.assists.abs;
  absAct = car.absActive;
  tcOn = car.assists.tc;
  tcAct = car.tcActive;
  autoGb = car.assists.autoGearbox;
  cam = camName;
  inputSrc = input.source;
  for (int i = 0; i < 4; i++) {
    const auto& w = car.wheels[i];
    float load = (float)std::min(1.0, w.fz / 7000);
    float slip = (float)std::min(1.6, w.slip);
    // verde = aderindo, amarelo = no pico, vermelho = deslizando, azul = travada
    Color c;
    if (!w.contact) c = Color::hex(0x333333);
    else if (w.slipRatio < -0.25 && std::fabs(w.omega) < 1) c = Color::hex(0x3aa0ff);
    else if (slip < 0.75f) c = hsl(130, 0.7f, (30 + load * 25) / 100);
    else if (slip < 1.1f) c = hsl(50, 0.9f, (40 + load * 15) / 100);
    else c = hsl(0, 0.85f, (40 + load * 15) / 100);
    tireColor[i] = c;
    tireScale[i] = 0.55f + load * 0.45f;
  }
  // medidor de G (aceleração no referencial do carro)
  gx = (float)(car.accel.x / 9.81);
  gz = (float)(car.accel.z / 9.81);
  trail.emplace_back(gx, gz);
  if (trail.size() > 40) trail.pop_front();
}

bool Hud::click(float x, float y) {
  if (creditLink.contains(x, y) && !creditSource.empty()) {
    SDL_OpenURL(creditSource.c_str());
    return true;
  }
  return false;
}

void Hud::draw(UI& ui, float fade) {
  float W = ui.width(), H = ui.height();
  ui.pushOpacity(fade);
  bool narrow = W <= 700;

  // ---------------------------------------------------------------- canto superior esquerdo
  {
    TextStyle chip{Font::BC600, 12, 12 * 0.12f, Color::hex(0x666666)};
    float lh = ui.lineHeightNormal(chip);
    float x = 16, y = 16;
    struct C { const char* t; bool on, act, always; };
    const C chips[3] = {{"ABS", absOn, absAct, false}, {"TC", tcOn, tcAct, false}, {autoGb ? "AUTO" : "MANUAL", true, false, true}};
    for (auto& c : chips) {
      float tw = ui.measure(c.t, chip);
      Rect r{x, y, tw + 16, lh + 6};
      ui.rect(r, c.act ? AMBER : PANEL, 4);
      TextStyle st = chip;
      st.color = c.act ? Color::hex(0x111111) : c.always ? FG : c.on ? Color::hex(0x7ee07e) : Color::hex(0x666666);
      ui.text(c.t, x + 8, y + 3, st, lh);
      x += r.w + 6 + 4;  // margem de 6 px + espaço entre inline-blocks
    }
    TextStyle small{Font::Barlow400, 12, 12 * 0.04f, DIM, true};
    ui.text(cam + " · " + inputSrc + " · " + fps, 16, y + lh + 6 + 6, small);
  }

  // ---------------------------------------------------------------- dica
  {
    TextStyle st{Font::Barlow400, 12, 12 * 0.04f, DIM};
    if (prompt == PadStyle::Keyboard) {
      std::string hint = "H controles · Esc garagem";
      ui.text(hint, W - 16 - ui.measure(hint, st), 16, st);
    } else {
      // [↑] controles   [View] garagem
      PromptTheme dk{true};
      float w = promptWidth(ui, prompt, PadButton::DPadUp, 16, "") + 6 + ui.measure("controles", st) + 16 +
                promptWidth(ui, prompt, PadButton::Select, 18, "") + 6 + ui.measure("garagem", st);
      float x = W - 16 - w, cy = 16 + 8;
      x += drawPrompt(ui, prompt, PadButton::DPadUp, x, cy, 16, "", dk) + 6;
      x += ui.text("controles", x, 16, st) + 16;
      x += drawPrompt(ui, prompt, PadButton::Select, x, cy, 18, "", dk) + 6;
      ui.text("garagem", x, 16, st);
    }
  }

  // ---------------------------------------------------------------- painel central
  {
    float dw = std::min(520.f, W - 32), dh = 124;
    Rect d{(W - dw) / 2, H - 18 - dh, dw, dh};
    ui.blurPanel(d, PANEL, 12);
    float ix = d.x + 14, iw = dw - 28, y = d.y + 10;
    // luzes de troca
    float lw = 10 * 22 + 9 * 6, lx = ix + (iw - lw) / 2;
    for (int i = 0; i < 10; i++) {
      Rect r{lx + i * 28.f, y, 22, 8};
      Color c = Color::hex(0x222222);
      bool glow = false;
      if (blink) { c = BLUE; glow = true; }
      else if (i < lit) { c = i < 4 ? GREEN : i < 8 ? RED : BLUE; glow = true; }
      if (glow) ui.shadow(r, c, 0, 0, blink ? 12.f : 8.f, 0, 4);
      ui.rect(r, c, 4);
    }
    y += 16;
    // conta-giros: o gradiente é escalado junto com o preenchimento
    Rect bar{ix, y, iw, 14};
    ui.rect(bar, TRACK, 7);
    float fw = iw * rpmFill;
    if (fw > 0.5f) {
      const float stops[4] = {0, 0.7f, 0.85f, 1};
      const Color cols[4] = {Color::hex(0xdddddd), Color::hex(0xffffff), AMBER, RED};
      for (int s = 0; s < 3; s++) {
        Rect q{ix + fw * stops[s], y, fw * (stops[s + 1] - stops[s]) + 0.5f, 14};
        ui.rectInside(q, bar, 7, cols[s], cols[s + 1]);
      }
    }
    // número do rpm (mix-blend-mode: difference) → escuro sobre o preenchimento, claro sobre o fundo
    {
      TextStyle st{Font::BC600, 11, 11 * 0.06f, Color::hex(0x111111), true};
      std::string t = std::to_string(rpmText);
      float tw = ui.measure(t, st), tx = ix + iw - 8 - tw, ty = y - 1;
      float split = ix + fw;
      ui.pushClip({ix, y - 4, std::max(0.f, split - ix), 20});
      ui.text(t, tx, ty, st);
      ui.popClip();
      ui.pushClip({split, y - 4, std::max(0.f, ix + iw - split), 20});
      st.color = Color::hex(0xd3d1cc);
      ui.text(t, tx, ty, st);
      ui.popClip();
    }
    y += 14 + 8;
    // linha principal: pedais, marcha, velocidade, volante (space-between)
    float rowH = 64, cy = y + rowH / 2;
    float wPed = 48, wGear = 70, wSpeed = 120, wSteer = 54;
    float gap = std::max(12.f, (iw - wPed - wGear - wSpeed - wSteer) / 3);
    float x = ix;
    // pedais
    const float vals[3] = {clutch, brk, thr};
    const Color pc[3] = {BLUE, RED, GREEN};
    for (int i = 0; i < 3; i++) {
      Rect r{x + i * 18.f, cy - 29, 12, 58};
      ui.rect(r, TRACK, 3);
      float hh = 58 * std::max(0.f, std::min(1.f, vals[i]));
      if (hh > 0.3f) ui.rectInside({r.x, r.y + 58 - hh, 12, hh}, r, 3, pc[i], pc[i]);
    }
    x += wPed + gap;
    // marcha
    {
      TextStyle st{Font::BC600, 74, 0, shifting ? AMBER : FG, true};
      float tw = ui.measure(gear, st);
      ui.text(gear, x + (wGear - tw) / 2, cy - 37, st, 74);
    }
    x += wGear + gap;
    // velocidade
    {
      TextStyle sp{Font::BC600, 54, 0, FG, true};
      TextStyle un{Font::BC500, 11, 11 * 0.2f, DIM};
      float unitH = ui.lineHeightNormal(un), boxH = 54 + unitH, top = cy - boxH / 2;
      std::string s = std::to_string(speed);
      ui.text(s, x + wSpeed - ui.measure(s, sp), top, sp, 54);
      ui.text("KM/H", x + wSpeed - ui.measure("KM/H", un), top + 54, un);
    }
    x += wSpeed + gap;
    // volante
    {
      float cx = x + 27;
      ui.ring(cx, cy, 25, 4, Color::hex(0x555555));
      // marcador vermelho no topo, girando com o esterço (270° por unidade)
      Rect m{cx - 2, cy - 27 - 4, 4, 12};
      ui.rotatedRect(m, cx, cy, steer * 270 * (float)M_PI / 180, RED, 2);
    }
  }

  // ---------------------------------------------------------------- canto inferior direito
  if (!narrow) {
    Rect p{W - 16 - 240, H - 18 - 150, 240, 150};
    ui.rect(p, PANEL, 12);
    // medidor de G (canvas 130×130)
    float gx0 = p.x + 10, gy0 = p.y + 10;
    float cx = gx0 + 65, cy = gy0 + 65, R = 65 - 6, sc = R / 2.5f;
    Color grid(1, 1, 1, 0.18f);
    for (int r = 1; r <= 2; r++) ui.ring(cx, cy, r * sc, 1, grid);
    ui.line(cx - R, cy, cx + R, cy, 1, grid);
    ui.line(cx, cy - R, cx, cy + R, 1, grid);
    for (auto& [x, z] : trail) ui.circle(cx + x * sc, cy - z * sc, 2, Color(1, 80 / 255.f, 60 / 255.f, 0.25f));
    ui.circle(cx + gx * sc, cy - gz * sc, 4.5f, Color::hex(0xff5a3c));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f g", std::hypot(gx, gz));
    TextStyle gs{Font::BC500, 11, 11 * 0.06f, Color(1, 1, 1, 0.7f), true};
    ui.textBaseline(buf, gx0 + 6, gy0 + 130 - 6, gs);
    // pneus
    float tx = p.x + 10 + 130 + 12 + 8, ty = p.y + p.h - 10 - 122 + 8;
    for (int i = 0; i < 4; i++) {
      float sx = tx + (i % 2) * 40.f, sy = ty + (i / 2) * 66.f;
      float hh = 40 * tireScale[i];
      ui.rect({sx, sy + (40 - hh) / 2, 22, hh}, tireColor[i], 5);
    }
  }

  // ---------------------------------------------------------------- mensagem
  if (msgOpacity.value > 0.001f) {
    ui.pushOpacity(msgOpacity.value);
    TextStyle st{Font::Barlow600, 15, 15 * 0.02f, FG};
    float tw = ui.measure(msg, st), lh = ui.lineHeightNormal(st);
    Rect r{(W - tw - 32) / 2, H * 0.22f, tw + 32, lh + 16};
    ui.rect(r, PANEL, 8);
    ui.text(msg, r.x + 16, r.y + 8, st, lh);
    ui.popOpacity();
  }

  // ---------------------------------------------------------------- créditos
  creditLink = {};
  if (!creditTitle.empty()) {
    TextStyle st{Font::Barlow400, 10, 10 * 0.02f, Color(1, 1, 1, 0.4f)};
    std::string pre = "Modelo: ", post = " por " + creditAuthor + " — " + creditLicense + " · Céu e asfalto: Poly Haven (CC0)";
    std::string full = pre + creditTitle + post;
    float maxW = W * 0.3f, lh = ui.lineHeightNormal(st);
    auto lines = wrap(ui, full, st, maxW);
    float y = H - 8 - lines.size() * lh;
    for (auto& l : lines) {
      ui.text(l, 16, y, st, lh);
      y += lh;
    }
    // sublinha o título na primeira linha (onde ele começa)
    if (!lines.empty()) {
      float x0 = 16 + ui.measure(pre, st);
      float tw = std::min(ui.measure(creditTitle, st), 16 + maxW - x0);
      float ly = H - 8 - lines.size() * lh;
      creditLink = {x0, ly, tw, lh};
      ui.rect({x0, ly + lh - 2, tw, 1}, st.color);
    }
  }

  // ---------------------------------------------------------------- ajuda
  if (helpOpen) {
    struct Row { const char* key; PadButton pad; const char* text; };
    static const Row kb[] = {
        {"W / ↑", PadButton::RT, "Acelerar"}, {"S / ↓", PadButton::LT, "Frear"}, {"A D / ← →", PadButton::LStick, "Esterçar"},
        {"Espaço", PadButton::Confirm, "Freio de mão"}, {"E / Shift", PadButton::RB, "Subir marcha"},
        {"Q / Ctrl", PadButton::LB, "Reduzir marcha (1 → N → R parado)"}, {"M", PadButton::Confirm, "Câmbio automático / manual"},
        {"B / T", PadButton::Confirm, "Liga/desliga ABS / controle de tração"}, {"C", PadButton::North, "Trocar câmera"},
        {"R", PadButton::Start, "Reposicionar carro"}, {"G", PadButton::Confirm, "Qualidade gráfica (Baixo / Médio / Alto / Ultra)"},
        {"N / P", PadButton::Confirm, "Mudo / pausa"}, {"H", PadButton::DPadUp, "Mostrar / esconder os controles"},
        {"Esc", PadButton::Select, "Voltar para a garagem"}};
    // no controle só aparecem as ações que têm botão
    static const int padRows[] = {0, 1, 2, 3, 4, 5, 8, 9, 12, 13};
    bool pad = prompt != PadStyle::Keyboard;
    std::vector<const Row*> rows;
    if (pad) for (int i : padRows) rows.push_back(&kb[i]);
    else for (auto& r : kb) rows.push_back(&r);
    TextStyle h2{Font::BC600, 16, 16 * 0.24f, FG};
    TextStyle k{Font::BC600, 14, 14 * 0.06f, AMBER};
    TextStyle v{Font::Barlow400, 14, 0, FG};
    TextStyle pst{Font::Barlow400, 13, 0, DIM};
    std::string ptxt = "Pneus no HUD: verde = aderindo, amarelo = no limite, vermelho = deslizando, azul = travado.";
    float c1 = 0, c2 = 0;
    for (auto* r : rows) {
      c1 = std::max(c1, pad ? promptWidth(ui, prompt, r->pad, 24, "") : ui.measure(r->key, k));
      c2 = std::max(c2, ui.measure(r->text, v));
    }
    float rowH = std::max(ui.lineHeightNormal(k), ui.lineHeightNormal(v)) + (pad ? 12 : 6);
    float tableW = c1 + 14 + c2 + 14 + 4;
    float maxInner = W - 32 - 48;
    float innerW = std::min(maxInner, std::max(tableW, ui.measure(ptxt, pst)));
    auto plines = wrap(ui, ptxt, pst, innerW);
    float plh = ui.lineHeightNormal(pst);
    float h2h = ui.lineHeightNormal(h2);
    float ph = 18 + h2h + 10 + rowH * rows.size() + 13 + plines.size() * plh + 13 + 18 - 13;
    Rect r{(W - innerW - 48) / 2, (H - ph) / 2, innerW + 48, ph};
    ui.rect(r, Color(10 / 255.f, 12 / 255.f, 16 / 255.f, 0.92f), 12);
    float x = r.x + 24, y = r.y + 18;
    ui.text("CONTROLES", x, y, h2);
    y += h2h + 10;
    for (auto* row : rows) {
      if (pad) drawPrompt(ui, prompt, row->pad, x + 2, y + rowH / 2, 24, "", {true});
      else ui.text(row->key, x + 2, y + 3, k, rowH - 6);
      ui.text(row->text, x + 2 + c1 + 14 + 4, y + (rowH - ui.lineHeightNormal(v)) / 2, v);
      y += rowH;
    }
    y += 13;
    for (auto& l : plines) {
      ui.text(l, x, y, pst, plh);
      y += plh;
    }
  }
  ui.popOpacity();
}
