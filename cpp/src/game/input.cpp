#include "input.h"

#include <cmath>

#include "../physics/vmath.h"

using phys::clamp;

static float ramp(float v, float target, float dt, float up, float down) {
  float r = target > v ? up : down;
  return v + (float)clamp(target - v, -r * dt, r * dt);
}

Input::~Input() {
  if (pad) SDL_CloseGamepad(pad);
}

void Input::keyDown(SDL_Scancode sc, bool repeat) {
  if (!repeat) pressed.insert(sc);
  keys.insert(sc);
}

void Input::gamepadAdded(SDL_JoystickID id) {
  if (!pad) pad = SDL_OpenGamepad(id);
}

void Input::gamepadRemoved(SDL_JoystickID id) {
  if (pad && SDL_GetGamepadID(pad) == id) {
    SDL_CloseGamepad(pad);
    pad = nullptr;
  }
}

PadStyle Input::padStyle() const {
  if (const char* f = SDL_getenv("SR_PAD")) {
    std::string s = f;
    return s == "xbox" ? PadStyle::Xbox : s == "ps" ? PadStyle::PlayStation : PadStyle::Generic;
  }
  if (!pad) return PadStyle::Generic;
  switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return PadStyle::Xbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return PadStyle::PlayStation;
    default: return PadStyle::Generic;
  }
}

bool Input::has(std::initializer_list<SDL_Scancode> codes) const {
  for (auto c : codes)
    if (keys.count(c)) return true;
  return false;
}

bool Input::take(std::initializer_list<SDL_Scancode> codes) {
  bool hit = false;
  for (auto c : codes)
    if (pressed.erase(c)) hit = true;
  return hit;
}

Input::Events Input::update(float dt, float speedKmh) {
  Events ev;
  float kbThrottle = has({SDL_SCANCODE_W, SDL_SCANCODE_UP}) ? 1.f : 0.f;
  float kbBrake = has({SDL_SCANCODE_S, SDL_SCANCODE_DOWN}) ? 1.f : 0.f;
  float kbSteer = (has({SDL_SCANCODE_D, SDL_SCANCODE_RIGHT}) ? 1.f : 0.f) - (has({SDL_SCANCODE_A, SDL_SCANCODE_LEFT}) ? 1.f : 0.f);
  bool usingKb = kbThrottle || kbBrake || kbSteer || has({SDL_SCANCODE_SPACE});

  bool padActive = false;
  if (pad) {
    // mesmo layout do "standard gamepad" do navegador
    auto trig = [&](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(pad, a) / 32767.f; };
    auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b) ? 1.f : 0.f; };
    auto dz = [](float v, float d = 0.06f) { return std::fabs(v) < d ? 0.f : (v - (v > 0 ? 1 : -1) * d) / (1 - d); };
    float steerAx = dz(std::max(-1.f, SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f));
    float thr = trig(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), brk = trig(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    float a = btn(SDL_GAMEPAD_BUTTON_SOUTH);
    padActive = std::fabs(steerAx) > 0 || thr > 0.02f || brk > 0.02f || a > 0.5f;
    auto edge = [&](int i, float value) {
      bool now = value > 0.5f, was = padPrev[i];
      padPrev[i] = now;
      return now && !was;
    };
    if (edge(5, btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))) ev.shiftUp = true;   // RB
    if (edge(4, btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))) ev.shiftDown = true;  // LB
    if (edge(3, btn(SDL_GAMEPAD_BUTTON_NORTH))) pressed.insert(SDL_SCANCODE_C);  // Y = câmera
    if (edge(9, btn(SDL_GAMEPAD_BUTTON_START))) pressed.insert(SDL_SCANCODE_R);  // Start = reset
    if (edge(8, btn(SDL_GAMEPAD_BUTTON_BACK))) pressed.insert(SDL_SCANCODE_ESCAPE);  // View/Share/Select = garagem
    if (edge(10, btn(SDL_GAMEPAD_BUTTON_DPAD_UP))) pressed.insert(SDL_SCANCODE_H);   // ajuda
    if (padActive && !usingKb) {
      source = "controle";
      throttle = thr;
      brake = brk;
      handbrake = a > 0.5f ? 1 : 0;
      // curva de sensibilidade não-linear para precisão perto do centro
      steer = (steerAx > 0 ? 1 : steerAx < 0 ? -1 : 0) * std::pow(std::fabs(steerAx), 1.4f) * steerLimit(speedKmh);
    }
  }

  if (!padActive || usingKb) {
    source = "teclado";
    // pedais com rampa (aperta em ~0,15 s, solta em ~0,08 s)
    throttle = ramp(throttle, kbThrottle, dt, 6.5f, 12);
    brake = ramp(brake, kbBrake, dt, 8, 12);
    handbrake = has({SDL_SCANCODE_SPACE}) ? 1 : 0;
    // direção: o teclado é digital; a rampa faz o papel das mãos e a sensibilidade (configurável)
    // reduz o ângulo máximo com a velocidade
    float target = kbSteer * steerLimit(speedKmh);
    float sgnK = kbSteer > 0 ? 1.f : kbSteer < 0 ? -1.f : 0.f, sgnS = steer > 0 ? 1.f : steer < 0 ? -1.f : 0.f;
    float rate = (kbSteer == 0 || sgnK != sgnS) ? 4.5f : 2.2f;
    steer += (float)clamp(target - steer, -rate * dt, rate * dt);
  }

  if (take({SDL_SCANCODE_E, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT})) ev.shiftUp = true;
  if (take({SDL_SCANCODE_Q, SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL})) ev.shiftDown = true;
  return ev;
}
