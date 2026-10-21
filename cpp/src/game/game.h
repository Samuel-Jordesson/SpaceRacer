// Modo pilotagem: mundo (céu HDRI + asfalto), física, câmeras, HUD, som e efeitos.
#pragma once
#include <atomic>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "../physics/vehicle.h"
#include "../render/postfx.h"
#include "../render/renderer.h"
#include "audio.h"
#include "car_model.h"
#include "cameras.h"
#include "cars.h"
#include "effects.h"
#include "hud.h"
#include "settings.h"
#include "input.h"
#include "world.h"

class Game {
 public:
  Game(Renderer& r, Input& in);
  ~Game();

  // carrega o mundo uma única vez (em segundo plano); chame pollPrepare() a cada quadro
  void beginPrepare(const MapEntry& map);
  bool preparing() const { return prepThread.joinable(); }
  bool prepared() const { return ready; }
  float prepareProgress() const { return prepProgress; }
  // devolve true quando terminou (ok) ; error preenchido se falhou
  bool pollPrepare(std::string& error);

  void start(const CarEntry& entry, const std::shared_ptr<LoadedCar>& loaded);
  void stop();
  void teleport(float x, float z, float heading);
  void debugPick(float x, float z);
  void resize(int winW, int winH, float dpr);
  void frame(float dt);
  void drawHud(UI& ui) {
    if (!hud) return;
    hud->prompt = input.preferPad() && input.hasPad() ? input.padStyle() : PadStyle::Keyboard;
    hud->draw(ui, 1);
  }
  bool hudClick(float x, float y) { return hud && hud->click(x, y); }
  void mouseDown(int button, float x, float y) { rig.orbit.pointerDown(button, x, y); }
  void mouseMove(float x, float y, float viewH) { rig.orbit.pointerMove(x, y, viewH); }
  void mouseUp() { rig.orbit.pointerUp(); }
  void wheel(float dy) { rig.orbit.wheel(dy); }

  std::function<void()> onExit;
  Settings* settings = nullptr;
  // reaplica as configurações (gráficos, efeitos, volume) — chamado pela tela de configurações
  void applySettings();
  PostFX fx;
  bool running = false;

 private:
  void findHelmet();
  void setQuality(int level, bool autoAdj = false);
  void resetCar();
  void handleKeys(const Input::Events& ev);
  void autoQuality(float dt);
  void syncVisuals(float dt);

  Renderer& renderer;
  Input& input;
  Camera camera;
  CameraRig rig;
  CarAudio audio;
  std::unique_ptr<World> world;
  std::unique_ptr<SkidMarks> skids;
  std::unique_ptr<TireSmoke> smoke;
  std::unique_ptr<Hud> hud;
  phys::Ground* ground = nullptr;
  const MapEntry* map = nullptr;
  std::map<std::string, std::unique_ptr<TrackMap>> tracks;  // pistas já carregadas
  std::unique_ptr<TrackMap> loadingTrack;
  bool worldReady = false;
  std::unique_ptr<phys::Vehicle> car;
  NodePtr carObj, visual;
  const CarEntry* entry = nullptr;
  std::shared_ptr<LoadedCar> loaded;
  double acc = 0;
  bool paused = false;
  float brakeLight = 0;
  struct { bool enabled = true; float t = 0; int frames = 0, drops = 0; } autoQ;
  // carregamento
  std::thread prepThread;
  std::atomic<float> prepProgress{0};
  std::atomic<bool> prepDone{false};
  std::string prepError;
  bool ready = false;
  int winW = 1, winH = 1;
  float dpr = 1;
};
