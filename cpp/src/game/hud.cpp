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

  // texto branco com uma sombra leve (legível sobre céu e asfalto, sem painel)
  auto shadowText = [&](const std::string& t, float x, float y, TextStyle st) {
    TextStyle sh = st;
    sh.color = Color(0, 0, 0, 0.35f * st.color.a);
    ui.text(t, x + 1, y + 1.5f, sh);
    return ui.text(t, x, y, st);
  };

  // ---------------------------------------------------------------- dica (embaixo, discreta)
  {
    TextStyle st{Font::Barlow400, 11, 11 * 0.08f, Color(1, 1, 1, 0.45f)};
    std::string hint = prompt == PadStyle::Keyboard ? "H controles  ·  Esc garagem" : "↑ controles  ·  View garagem";
    shadowText(hint, (W - ui.measure(hint, st)) / 2, H - 26, st);
  }

  // ---------------------------------------------------------------- velocímetro (canto inferior direito)
  {
    float R = narrow ? 64.f : 84.f, cx = W - (narrow ? 90.f : 130.f), cy = H - (narrow ? 96.f : 124.f);
    // arco de 270° (de baixo-esquerda, por cima, até baixo-direita) = giro do motor
    const float a0 = 0.75f * (float)M_PI, span = 1.5f * (float)M_PI;
    auto arc = [&](float f0, float f1, float w, const Color& c) {
      int n = std::max(2, (int)((f1 - f0) * 90));
      for (int i = 0; i < n; i++) {
        float t0 = a0 + span * (f0 + (f1 - f0) * i / n), t1 = a0 + span * (f0 + (f1 - f0) * (i + 1) / n);
        ui.line(cx + std::cos(t0) * R, cy + std::sin(t0) * R, cx + std::cos(t1) * R, cy + std::sin(t1) * R, w, c);
      }
    };
    arc(0, 0.86f, 2, Color(1, 1, 1, 0.38f));
    arc(0.86f, 1, 3, Color::hex(0xe8102a, 0.85f));  // faixa vermelha (corte de giro)
    float f = std::clamp(rpmFill, 0.f, 1.f);
    if (f > 0.002f) arc(0, std::min(f, 0.86f), 3, Color(1, 1, 1, 0.95f));
    if (f > 0.86f) arc(0.86f, f, 4, Color::hex(0xff2a40));
    // marcha grande, linha fina, velocidade e unidade
    TextStyle gs{Font::Barlow300, narrow ? 44.f : 58.f, 0, shifting ? AMBER : Color(1, 1, 1, 1), true};
    float gw = ui.measure(gear, gs);
    shadowText(gear, cx - gw / 2, cy - (narrow ? 50.f : 64.f), gs);
    ui.rect({cx - R * 0.42f, cy + 2, R * 0.84f, 1}, Color(1, 1, 1, 0.45f));
    TextStyle sp{Font::Barlow400, narrow ? 24.f : 30.f, 0, Color(1, 1, 1, 1), true};
    std::string sv = std::to_string(speed);
    shadowText(sv, cx - ui.measure(sv, sp) / 2, cy + 8, sp);
    TextStyle un{Font::Barlow400, 11, 0.5f, Color(1, 1, 1, 0.75f)};
    shadowText("km/h", cx - ui.measure("km/h", un) / 2, cy + (narrow ? 36.f : 44.f), un);
    // auxílios à esquerda do arco: aceso = ligado, âmbar = agindo agora
    TextStyle as{Font::Barlow500, 11.5f, 0.8f, Color(1, 1, 1, 1)};
    struct A { const char* t; bool on, act; };
    const A aids[3] = {{"ABS", absOn, absAct}, {"TCS", tcOn, tcAct}, {autoGb ? "AUTO" : "MAN", true, false}};
    float ax = cx - R - 46, ay = cy - 8;
    for (int i = 0; i < 3; i++) {
      as.color = aids[i].act ? AMBER : aids[i].on ? Color(1, 1, 1, 0.85f) : Color(1, 1, 1, 0.3f);
      shadowText(aids[i].t, ax, ay + i * 22, as);
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
