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
#include "workshop.h"
#include "world.h"

class Game {
 public:
  Game(Renderer& r, Input& in);
  ~Game();

  // carrega o mundo uma única vez (em segundo plano); chame pollPrepare() a cada quadro
  void beginPrepare(const MapEntry& map, int timeOfDay = 0);
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
    drawRace(ui);
  }
  // corrida: 0 livre, 1 contra o tempo, 2 contra a IA (definido antes de start)
  struct RaceCfg { int mode = 0, laps = 3, opponents = 5; };
  RaceCfg race;
  std::string playerCarId;  // para não repetir o carro do jogador entre os adversários
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
  phys::CarSpec tuned;
  // --- corrida (race.cpp)
  enum class Phase { Free, Countdown, Running, Finished };
  Phase phase = Phase::Free;
  struct Racer {
    std::string name;
    phys::Vehicle* car = nullptr;
    int seg = -1;
    float s = 0;
    int laps = -1;
    double lapStart = 0, lastLap = 0, bestLap = 0, finish = -1;
    float stuck = 0;
  };
  struct Bot {
    std::unique_ptr<phys::Vehicle> car;
    NodePtr obj, visual;
    std::shared_ptr<LoadedCar> model;
    float lane = 0, skill = 1;
    phys::Controls ctl;
    const CarEntry* entry = nullptr;
    std::vector<float> plan;  // velocidade planejada em cada ponto do traçado para este carro (m/s)
    vec3 color{1, 1, 1};      // cor no minimapa
  };
  std::vector<Racer> racers;  // [0] = jogador
  std::vector<Bot> bots;
  std::vector<const CarEntry*> oppPlan;                      // carro de cada adversário
  std::map<std::string, std::shared_ptr<LoadedCar>> oppModels;  // modelos já carregados
  std::vector<std::pair<std::string, std::shared_ptr<LoadedCar>>> oppLoaded;  // carregados na outra thread
  double raceClock = 0;
  float countdown = 0;
  bool resultsOpen = false;
  bool raceActive() const;
  void raceStart();
  void raceStop();
  void raceUpdate(float dt);
  void raceAfterPhysics(float dt);
  void botDrive(Bot& b, Racer& r);
  void collideCars();
  void syncBots();
  void drawRace(UI& ui);
  void placeOnLine(phys::Vehicle& v, float s, float lat);
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
