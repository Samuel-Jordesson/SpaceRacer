// Entrada: teclado (com rampas, como um piloto modulando pedal) e controle (analógico).
#pragma once
#include <SDL3/SDL.h>

#include <initializer_list>
#include <cmath>
#include <set>
#include <string>

#include "../ui/glyphs.h"

class Input {
 public:
  ~Input();
  void keyDown(SDL_Scancode sc, bool repeat);
  void keyUp(SDL_Scancode sc) { keys.erase(sc); }
  void blur() { keys.clear(); }
  void gamepadAdded(SDL_JoystickID id);
  void gamepadRemoved(SDL_JoystickID id);

  bool has(std::initializer_list<SDL_Scancode> codes) const;
  // consome um evento de tecla
  bool take(std::initializer_list<SDL_Scancode> codes);
  void press(SDL_Scancode sc) { pressed.insert(sc); }

  struct Events {
    bool shiftUp = false, shiftDown = false;
  };
  Events update(float dt, float speedKmh);
  void endFrame() { pressed.clear(); }
  void clearPressed() { pressed.clear(); }

  // estilo dos ícones do controle conectado (Xbox, PlayStation ou genérico)
  PadStyle padStyle() const;
  bool hasPad() const { return pad != nullptr || forced(); }
  SDL_Gamepad* gamepad() const { return pad; }
  // redução fixa do ângulo máximo com a velocidade (0 = direta, como num volante); igual ao
  // "speed sensitivity" dos simuladores para controle/teclado. Não reage ao carro escorregando.
  float steerSens = 0.4f;
  float steerLimit(float kmh) const { return 1 / (1 + steerSens * 2.5f * std::pow(std::max(0.f, kmh) / 120, 1.3f)); }
  // teste sem controle físico: SR_PAD=xbox|ps|generic
  static bool forced() { return SDL_getenv("SR_PAD") != nullptr; }
  bool preferPad() const { return source == "controle" || forced(); }

  float throttle = 0, brake = 0, steer = 0, handbrake = 0;
  std::string source = "teclado";

 private:
  std::set<SDL_Scancode> keys, pressed;
  SDL_Gamepad* pad = nullptr;
  bool padPrev[16] = {};
};
