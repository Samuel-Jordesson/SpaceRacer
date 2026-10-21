// Cinemática do carro na garagem: sequência de planos com cortes, como num comercial automotivo.
// "Angle study" (¾ frontal na altura da cintura, ⅞, perfil, frente, ¾ traseira), closes de detalhe
// (roda/pinça, farol, asa, escapamento), movimentos de câmera de cinema (slider rente ao chão, grua,
// push-in, revelação, órbita aérea), lente longa no perfil, profundidade de campo rasa e tela 2,39:1
// com barras. Rodas dianteiras viradas ~16° para mostrar o aro. Qualquer tecla ou clique sai.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../ui/anim.h"
#include "../ui/glyphs.h"
#include "../ui/ui.h"
#include "car_model.h"
#include "cars.h"

class Garage;

class Cinematic {
 public:
  explicit Cinematic(Garage& g);
  void start(const CarEntry& entry, const std::shared_ptr<LoadedCar>& loaded);
  // tecla ou clique: começa a sair (as barras recolhem com o filme ainda rodando)
  bool requestExit();
  void update(float dt);
  // overlay: barras, título, dica e o "dip" para preto
  void drawOverlay(UI& ui, float dt);

  bool active = false, closing = false;
  PadStyle prompt = PadStyle::Keyboard;  // ícone da dica (teclado ou controle)
  std::function<void()> onExit;

 private:
  struct Orbit { vec3 c; float a0, a1, r0, r1, h0, h1; };
  struct Shot {
    float dur;
    bool title = false, dip = false;
    float fov0, fov1 = -1;
    vec3 from, to, lookFrom;
    std::optional<vec3> lookTo;
    std::optional<Orbit> orbit;
    std::optional<float> aperture;
  };
  void beginShot();
  void finish();
  void startFlight();
  void updateFlight(float dt);
  std::vector<Shot> buildShots(LoadedCar& loaded);

  Garage& garage;
  std::shared_ptr<LoadedCar> loaded;
  std::vector<Shot> shots;
  int index = 0, loops = 0;
  float t = 0, sinceStart = 0, closeTimer = 0, hintTimer = 0, dipT = 1e9f, pulse = 0;
  struct Saved { vec3 pos, target; float fov; bool autoRotate; } saved{};
  // saída: depois das barras, a câmera voa do último plano até a posição do menu
  vec3 lastLook;
  bool flying = false;
  float flightT = 0;
  struct Flight { vec3 pos0, look0, look1; float fov0, fov1, ap0; float r0, r1, az0, az1, el0, el1; } flight{};
  std::string maker, name;
  // transições do CSS
  Transition bars{0, 2.6f, Bezier{0.45f, 0, 0.25f, 1}};
  Transition title{0, 1.2f, EASE, 0.4f};
  Transition hint{0, 0.8f, EASE, 1.2f};  // aparece depois das barras
};
