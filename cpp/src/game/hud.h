// HUD: velocidade, marcha, conta-giros com luzes de troca, pedais, medidor de G,
// estado dos pneus (carga e escorregamento), auxílios, mensagens, ajuda e créditos.
#pragma once
#include <deque>
#include <string>

#include "../physics/vehicle.h"
#include "../ui/anim.h"
#include "../ui/glyphs.h"
#include "../ui/ui.h"
#include "input.h"

class Hud {
 public:
  explicit Hud(const phys::CarSpec& spec);
  void flash(const std::string& text, float sec = 1.5f);
  void update(const phys::Vehicle& car, const Input& input, const std::string& camName, float dt);
  void draw(UI& ui, float fade);
  // clique nos créditos (link do modelo)
  bool click(float x, float y);

  bool helpOpen = false;
  PadStyle prompt = PadStyle::Keyboard;
  std::string creditTitle, creditAuthor, creditLicense, creditSource;

 private:
  const phys::CarSpec& spec;
  // valores mostrados
  int speed = 0, rpmText = 0;
  std::string gear = "N";
  bool shifting = false;
  float rpmFill = 0, thr = 0, brk = 0, clutch = 0, steer = 0;
  int lit = 0;
  bool blink = false;
  bool absOn = true, absAct = false, tcOn = true, tcAct = false, autoGb = true;
  std::string cam, inputSrc, fps;
  Color tireColor[4];
  float tireScale[4] = {0.55f, 0.55f, 0.55f, 0.55f};
  std::deque<std::pair<float, float>> trail;
  float gx = 0, gz = 0;
  // tempos
  float slow = 0, fpsT = 0, msgT = 0, clock = 0;
  int frames = 0;
  std::string msg;
  Transition msgOpacity{0, 0.2f};
  Rect creditLink;
};
