// Space Racer (versão nativa em C++): garagem (tela inicial) → escolher carro → pilotar →
// Esc volta para a garagem.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <thread>

#include "core/assets.h"
#include "game/car_model.h"
#include "game/cars.h"
#include "game/cinematic.h"
#include "game/game.h"
#include "game/garage.h"
#include "game/input.h"
#include "game/settings.h"
#include "game/track.h"
#include "render/renderer.h"
#include "render/shader.h"
#include "ui/anim.h"
#include "ui/glyphs.h"
#include "ui/ui.h"
#include "../third_party/stb_image_write.h"
#include <sstream>

namespace {

// cores do CSS da garagem
const Color INK = Color::hex(0x111214), INK2 = Color::hex(0x5d6066), INK3 = Color::hex(0x9a9da3);
const Color LINE(17 / 255.f, 18 / 255.f, 20 / 255.f, 0.10f);
const Color ACCENT = Color::hex(0xd4001a);
const Color WHITE(1, 1, 1, 1);
const Bezier INFO_EASE{0.2f, 0.7f, 0.2f, 1};

// ---------------------------------------------------------------- desfoque do fundo (backdrop-filter)
class Backdrop {
 public:
  Backdrop() {
    const char* fs = R"(
in vec2 vUv;
uniform sampler2D tSrc;
uniform vec2 uDir;
out vec4 fragColor;
void main() {
  // gaussiana de 9 amostras (sigma ≈ 2,5 texels a 1/4 da resolução ≈ blur(10px))
  const float w[5] = float[](0.1784, 0.1588, 0.1121, 0.0627, 0.0278);
  vec3 c = texture(tSrc, vUv).rgb * w[0];
  for (int i = 1; i < 5; i++) {
    c += texture(tSrc, vUv + uDir * float(i)).rgb * w[i];
    c += texture(tSrc, vUv - uDir * float(i)).rgb * w[i];
  }
  fragColor = vec4(c / (w[0] + 2.0 * (w[1] + w[2] + w[3] + w[4])), 1.0);
}
)";
    shader = std::make_unique<Shader>(GLSL_FULLSCREEN_VS, fs);
  }
  ~Backdrop() { release(); }
  GLuint make(GLuint src, int srcW, int srcH) {
    int w = std::max(1, srcW / 4), h = std::max(1, srcH / 4);
    if (w != bw || h != bh) {
      release();
      bw = w;
      bh = h;
      for (int i = 0; i < 2; i++) {
        glGenTextures(1, &tex[i]);
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &fbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[i], 0);
      }
    }
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    shader->use();
    shader->set("tSrc", 0);
    glActiveTexture(GL_TEXTURE0);
    glViewport(0, 0, bw, bh);
    // passada horizontal lendo a imagem cheia (o filtro linear já faz a redução)
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
    glBindTexture(GL_TEXTURE_2D, src);
    shader->set("uDir", vec2(1.0f / bw, 0));
    drawFullscreen();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    shader->set("uDir", vec2(0, 1.0f / bh));
    drawFullscreen();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return tex[1];
  }

 private:
  void release() {
    for (int i = 0; i < 2; i++) {
      if (fbo[i]) glDeleteFramebuffers(1, &fbo[i]);
      if (tex[i]) glDeleteTextures(1, &tex[i]);
      fbo[i] = tex[i] = 0;
    }
    bw = bh = 0;
  }
  std::unique_ptr<Shader> shader;
  GLuint fbo[2] = {0, 0}, tex[2] = {0, 0};
  int bw = 0, bh = 0;
};

struct Card {
  const CarEntry* car;
  TexturePtr thumb;
  Transition hover{0, 0.25f}, selected{0, 0.25f};
  Transition progressOpacity{0, 0.4f};
  bool isSelected = false, loading = false;
  float progress = 0;
  Rect rect;
};

struct MapCard {
  const MapEntry* map;
  TexturePtr thumb;
  Transition hover{0, 0.25f}, selected{0, 0.25f};
};

struct Button {
  Rect rect;
  bool disabled = true;
  Transition hover{0, 0.2f};
  Transition opacity;
};

}  // namespace

class App {
 public:
  App(SDL_Window* w) : window(w), garage(renderer), game(renderer, input), cine(garage) {
    garage.cine = &cine;
    ui.loadFonts(assetPath("assets/fonts"));
    for (auto& car : carCatalog()) {
      Card c;
      c.car = &car;
      c.thumb = loadImage(car.thumb, false);
      if (c.thumb) {
        c.thumb->mipmaps = true;
        c.thumb->upload();
      }
      // logotipo da marca (mostrado acima da ficha do carro)
      if (auto lg = loadImage(car.logo, false)) {
        lg->repeat = false;
        lg->upload();
        logos[car.id] = lg;
      }
      cards.push_back(std::move(c));
    }
    for (auto& mp : mapCatalog()) {
      MapCard mc;
      mc.map = &mp;
      mc.thumb = loadImage(mp.thumb, false);
      if (mc.thumb) mc.thumb->upload();
      mapCards.push_back(std::move(mc));
    }
    play.opacity = Transition(0.18f, 0.3f);
    cineBtn.opacity = Transition(0.3f, 0.3f);
    game.onExit = [this] { backToGarage(); };
    cine.onExit = [this] {
      after(0.25f, [this] {
        garageUi.set(1);
        mode = Mode::Garage;
      });
    };
    settings.load();
    game.settings = &settings;
    for (int k = 0; k < (int)mapCards.size(); k++)
      if (mapCards[k].map->id == settings.lastMap) selectedMap = k;
    resize();
    garage.setActive(true);
    applySettings(true);
    // abre a garagem já com o último carro escolhido (ou o primeiro da lista)
    int start = 0;
    for (int k = 0; k < (int)cards.size(); k++)
      if (cards[k].car->id == settings.lastCar) start = k;
    selectCar(start);
  }

  ~App() {
    if (loadThread.joinable()) loadThread.join();
  }

  bool running = true;

  void resize() {
    int w, h, pw, ph;
    SDL_GetWindowSize(window, &w, &h);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    fbW = pw;
    fbH = ph;
    dpr = w > 0 ? (float)pw / w : 1;
    float scale = SDL_GetWindowDisplayScale(window);
    if (scale > 0) dpr = scale;
    cssW = (int)std::lround(pw / dpr);
    cssH = (int)std::lround(ph / dpr);
    if (mode == Mode::Game) game.resize(cssW, cssH, dpr);
    else garage.resize(cssW, cssH, dpr);
    // os dois precisam do tamanho certo quando forem ativados
    game.resize(cssW, cssH, dpr);
  }

  void event(const SDL_Event& e) {
    switch (e.type) {
      case SDL_EVENT_QUIT: running = false; break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: resize(); break;
      case SDL_EVENT_WINDOW_FOCUS_LOST: input.blur(); break;
      case SDL_EVENT_GAMEPAD_ADDED: input.gamepadAdded(e.gdevice.which); break;
      case SDL_EVENT_GAMEPAD_REMOVED: input.gamepadRemoved(e.gdevice.which); break;
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        usePad = true;
        padButton((SDL_GamepadButton)e.gbutton.button);
        break;
      case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        // analógico esquerdo funciona como direcional nos menus (com histerese)
        float v = e.gaxis.value / 32767.f;
        int ax = e.gaxis.axis;
        if (ax != SDL_GAMEPAD_AXIS_LEFTX && ax != SDL_GAMEPAD_AXIS_LEFTY) {
          if (std::fabs(v) > 0.3f) usePad = true;
          break;
        }
        int& st = ax == SDL_GAMEPAD_AXIS_LEFTX ? stickX : stickY;
        int now = v > 0.6f ? 1 : v < -0.6f ? -1 : (std::fabs(v) < 0.35f ? 0 : st);
        if (now != st) {
          st = now;
          if (now != 0) {
            usePad = true;
            if (ax == SDL_GAMEPAD_AXIS_LEFTX) padButton(now > 0 ? SDL_GAMEPAD_BUTTON_DPAD_RIGHT : SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            else padButton(now > 0 ? SDL_GAMEPAD_BUTTON_DPAD_DOWN : SDL_GAMEPAD_BUTTON_DPAD_UP);
          }
        }
        break;
      }
      case SDL_EVENT_KEY_DOWN: {
        usePad = false;
        SDL_Scancode sc = e.key.scancode;
        if (sc == SDL_SCANCODE_F11 && !e.key.repeat) {
          settings.fullscreen = !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
          settings.save();
          SDL_SetWindowFullscreen(window, settings.fullscreen);
          break;
        }
        // na cinemática, qualquer tecla sai (e não chega ao resto do jogo)
        if (cine.active) {
          if (sc == SDL_SCANCODE_SPACE || sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) cine.requestExit();
          break;
        }
        input.keyDown(sc, e.key.repeat);
        if (mode == Mode::Garage && settingsTab) {
          if (sc == SDL_SCANCODE_ESCAPE && !e.key.repeat) setTab(false);
          if (sc == SDL_SCANCODE_UP) moveFocus(-1);
          if (sc == SDL_SCANCODE_DOWN) moveFocus(1);
          if (sc == SDL_SCANCODE_LEFT) adjustFocus(-1);
          if (sc == SDL_SCANCODE_RIGHT) adjustFocus(1);
          if ((sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_SPACE) && !e.key.repeat) activateFocus();
          break;
        }
        if (mode == Mode::Garage && !e.key.repeat && mapPage) {
          if (sc == SDL_SCANCODE_ESCAPE) setMapPage(false);
          if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) startPlay();
          if (sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_LEFT) {
            int n = (int)mapCards.size();
            selectedMap = ((selectedMap + (sc == SDL_SCANCODE_RIGHT ? 1 : -1)) % n + n) % n;
          }
          break;
        }
        if (mode == Mode::Garage && !e.key.repeat) {
          if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) startPlay();
          // setas trocam de carro na lista
          if (sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_LEFT) {
            int n = (int)cards.size(), i = -1;
            for (int k = 0; k < n; k++)
              if (cards[k].isSelected) i = k;
            int j = ((i + (sc == SDL_SCANCODE_RIGHT ? 1 : -1)) % n + n) % n;
            selectCar(j);
          }
        }
        break;
      }
      case SDL_EVENT_KEY_UP: input.keyUp(e.key.scancode); break;
      case SDL_EVENT_MOUSE_MOTION:
        mouseX = e.motion.x;
        mouseY = e.motion.y;
        if (dragFn) {
          dragFn(mouseX);
          break;
        }
        if (mode == Mode::Game) game.mouseMove(mouseX, mouseY, (float)cssH);
        else garage.mouseMove(mouseX, mouseY);
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        usePad = false;
        float x = e.button.x, y = e.button.y;
        if (cine.active) {
          cine.requestExit();
          swallowClick = true;
          break;
        }
        if (fade.value > 0.5f) break;  // a tela de transição bloqueia cliques
        if (mode == Mode::Game) {
          if (e.button.button == 1 && game.hudClick(x, y)) break;
          game.mouseDown(e.button.button, x, y);
        } else if (mode == Mode::Garage) {
          pressTarget = hitTest(x, y);
          if (pressTarget == -100) garage.mouseDown(e.button.button, x, y);
          if (pressTarget <= -1000 && e.button.button == 1 && hits[-1000 - pressTarget].drag) {
            dragFn = hits[-1000 - pressTarget].drag;
            dragFn(x);
            pressTarget = -100;
          }
        }
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        float x = e.button.x, y = e.button.y;
        game.mouseUp();
        garage.mouseUp();
        dragFn = nullptr;
        if (swallowClick) {
          swallowClick = false;
          break;
        }
        if (mode == Mode::Garage && e.button.button == 1 && pressTarget != -100 && hitTest(x, y) == pressTarget) click(pressTarget);
        pressTarget = -100;
        break;
      }
      case SDL_EVENT_MOUSE_WHEEL: {
        usePad = false;
        float dy = -e.wheel.y * 100;  // como o deltaY do navegador
        if (mode == Mode::Game) game.wheel(dy);
        else if (mode == Mode::Garage && settingsTab && settingsPanel.contains(mouseX, mouseY)) settingsScroll += dy * 0.4f;
        else if (mode == Mode::Garage && hitTest(mouseX, mouseY) == -100) garage.wheel(dy);
        break;
      }
      default: break;
    }
  }

  void frame(float dt) {
    runTimers(dt);
    pollCarLoad();
    pollPrepare();

    PostFX* fx = nullptr;
    if (mode == Mode::Game) {
      game.frame(dt);
      fx = &game.fx;
    } else if (garage.active) {
      garage.frame(dt);
      fx = &garage.fx;
    }
    if (fx) lastFx = fx;
    if (lastFx) {
      lastFx->present(fbW, fbH);
      backdropTex = backdrop.make(lastFx->finalTexture(), lastFx->width, lastFx->height);
    } else {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glClearColor(0.953f, 0.953f, 0.949f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
    }

    ui.begin(fbW, fbH, dpr);
    ui.setBackdrop(backdropTex);
    if (!hideHud && (mode == Mode::Game || (mode == Mode::Loading && game.running))) game.drawHud(ui);
    drawGarageUi(dt);
    cine.prompt = promptStyle();
    cine.drawOverlay(ui, dt);
    drawFade(dt);
    ui.end();
    updateCursor();
    runScript(dt);
  }

  // ---------------------------------------------------------------- roteiro de teste (SR_SCRIPT)
  // comandos separados por ';': sel N, wait S, ready, play, cine, key K, down K, up K, shot ARQ, quit
  void setScript(const std::string& s) {
    std::stringstream ss(s);
    std::string cmd;
    while (std::getline(ss, cmd, ';'))
      if (!cmd.empty()) script.push_back(cmd);
  }
  void sendKey(const std::string& name, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = SDL_GetScancodeFromName(name.c_str());
    event(e);
  }
  void runScript(float dt) {
    while (scriptPos < script.size()) {
      std::stringstream ss(script[scriptPos]);
      std::string op, arg;
      ss >> op >> arg;
      if (op == "wait") {
        scriptWait += dt;
        if (scriptWait < std::stof(arg)) return;
        scriptWait = 0;
      } else if (op == "ready") {
        if (loadingCard >= 0 || waitingPrepare || mode == Mode::Loading || fade.value > 0.01f) return;
      } else if (op == "sel") selectCar(std::stoi(arg));
      else if (op == "play") startPlay();
      else if (op == "cine") startCinematic();
      else if (op == "key") { sendKey(arg, true); sendKey(arg, false); }
      else if (op == "down") sendKey(arg, true);
      else if (op == "up") sendKey(arg, false);
      else if (op == "usepad") usePad = true;
      else if (op == "hideui") { garageUi.snap(0); hideHud = true; }
      else if (op == "pick") {
        std::string zs;
        ss >> zs;
        if (mode == Mode::Game) game.debugPick(std::stof(arg), std::stof(zs));
      }
      else if (op == "tp") {
        // "tp x z rumo": teleporta o carro (graus)
        std::string zs, hs;
        ss >> zs >> hs;
        if (mode == Mode::Game) game.teleport(std::stof(arg), std::stof(zs), std::stof(hs) * (float)M_PI / 180);
      }
      else if (op == "cam") {
        // posição fixa da câmera da garagem (sem auto-rotação): "cam x y z"
        std::string ys, zs;
        ss >> ys >> zs;
        garage.controls.autoRotate = false;
        garage.controls.onEnd = nullptr;
        garage.intro = 1;
        garage.camera.position = vec3(std::stof(arg), std::stof(ys), std::stof(zs));
      }
      else if (op == "move" || op == "click") {
        // coordenadas em px de CSS: "click 320 48"
        std::string ys;
        ss >> ys;
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.x = std::stof(arg);
        e.motion.y = std::stof(ys);
        event(e);
        if (op == "click") {
          SDL_Event d{};
          d.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
          d.button.button = 1;
          d.button.x = e.motion.x;
          d.button.y = e.motion.y;
          event(d);
          d.type = SDL_EVENT_MOUSE_BUTTON_UP;
          event(d);
        }
      }
      else if (op == "shot") {
        std::vector<uint8_t> px((size_t)fbW * fbH * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        stbi_flip_vertically_on_write(1);
        stbi_write_png(arg.c_str(), fbW, fbH, 4, px.data(), fbW * 4);
        std::printf("[script] screenshot %s\n", arg.c_str());
      } else if (op == "quit") running = false;
      scriptPos++;
    }
  }
  std::vector<std::string> script;
  size_t scriptPos = 0;
  float scriptWait = 0;

 private:
  enum class Mode { Garage, Loading, Game, Cinematic };

  // ---------------------------------------------------------------- temporizadores (setTimeout)
  void after(float s, std::function<void()> fn) { timers.push_back({s, std::move(fn)}); }
  void runTimers(float dt) {
    auto list = std::move(timers);
    timers.clear();
    for (auto& t : list) {
      t.first -= dt;
      if (t.first <= 0) t.second();
      else timers.push_back(std::move(t));
    }
  }

  // ---------------------------------------------------------------- seleção e carregamento de carro
  void selectCar(int i) {
    if (loadingCard >= 0 || selectedCard == i) return;
    for (int k = 0; k < (int)cards.size(); k++) cards[k].isSelected = k == i;
    play.disabled = true;
    cineBtn.disabled = true;
    showInfo(*cards[i].car);
    auto it = cache.find(cards[i].car->id);
    if (it != cache.end()) {
      finishSelect(i, it->second);
      return;
    }
    loadingCard = i;
    cards[i].loading = true;
    cards[i].progress = 0;
    loadProgress = 0;
    loadDone = false;
    loadError.clear();
    loadResult.reset();
    const CarEntry* car = cards[i].car;
    loadThread = std::thread([this, car] {
      try {
        loadResult = loadCar(assetPath(car->model), car->visual, [this](float p) { loadProgress = p; });
      } catch (std::exception& e) {
        loadError = e.what();
      }
      loadDone = true;
    });
  }

  void pollCarLoad() {
    if (loadingCard < 0) return;
    Card& c = cards[loadingCard];
    c.progress = loadProgress;
    if (!loadDone) return;
    loadThread.join();
    int i = loadingCard;
    loadingCard = -1;
    c.loading = false;
    if (!loadError.empty()) {
      std::fprintf(stderr, "%s\n", loadError.c_str());
      infoMeta = "Erro ao carregar o modelo: " + loadError;
      introCinematic = false;
      boot.set(0);
      return;
    }
    loadResult->upload();
    cache[c.car->id] = loadResult;
    finishSelect(i, loadResult);
  }

  void finishSelect(int i, const std::shared_ptr<LoadedCar>& loaded) {
    selectedCard = i;
    if (settings.lastCar != cards[i].car->id) {
      settings.lastCar = cards[i].car->id;
      settings.save();
    }
    emptyOpacity.set(0);
    garage.showCar(loaded);
    play.disabled = false;
    cineBtn.disabled = false;
    if (introCinematic) {
      introCinematic = false;
      startCinematic();
      boot.set(0);
    }
  }

  void showInfo(const CarEntry& car) {
    infoCar = &car;
    infoMeta = std::to_string(car.year) + " · " + car.category;
    infoT = 0;  // reinicia a animação de entrada
  }

  // ---------------------------------------------------------------- transições garagem ↔ pista
  void startPlay() {
    if (selectedCard < 0 || mode != Mode::Garage || play.disabled || settingsTab) return;
    if (!mapPage) {
      setMapPage(true);
      return;
    }
    const MapEntry& mp = *mapCards[selectedMap].map;
    if (settings.lastMap != mp.id) {
      settings.lastMap = mp.id;
      settings.save();
    }
    mode = Mode::Loading;
    fade.set(1);
    fadeText = "Preparando " + mp.name + "…";
    fadeBar.snap(0);
    after(0.45f, [this, &mp] {
      game.beginPrepare(mp);
      waitingPrepare = true;
    });
  }

  void pollPrepare() {
    if (!waitingPrepare) return;
    fadeBar.set(game.prepareProgress());
    std::string err;
    if (!game.pollPrepare(err)) return;
    waitingPrepare = false;
    if (!err.empty()) {
      std::fprintf(stderr, "%s\n", err.c_str());
      fadeText = "Erro ao carregar a pista: " + err;
      mode = Mode::Garage;
      after(3, [this] { fade.set(0); });
      return;
    }
    fadeBar.set(1);
    garage.setActive(false);
    garageUi.set(0);
    setMapPage(false);
    const CarEntry& car = *cards[selectedCard].car;
    game.resize(cssW, cssH, dpr);
    game.start(car, cache[car.id]);
    mode = Mode::Game;
    after(0.15f, [this] { fade.set(0); });
  }

  void backToGarage() {
    if (mode != Mode::Game) return;
    mode = Mode::Loading;
    fadeText.clear();
    fadeBar.snap(0);
    fade.set(1);
    after(0.4f, [this] {
      game.stop();
      garage.reclaimCar();
      garage.setActive(true);
      garage.resize(cssW, cssH, dpr);
      garageUi.set(1);
      mode = Mode::Garage;
      after(0.1f, [this] { fade.set(0); });
    });
  }

  void startCinematic() {
    if (selectedCard < 0 || mode != Mode::Garage || cineBtn.disabled) return;
    mode = Mode::Cinematic;
    garageUi.set(0);
    const CarEntry& car = *cards[selectedCard].car;
    cine.start(car, cache[car.id]);
  }

  // ---------------------------------------------------------------- layout da garagem (px de CSS)
  struct Layout {
    float pad, infoX, infoY, infoW;
    bool narrow, showRatings, showFigures;
    Rect strip;
    float headY;
  };
  Layout layout() {
    Layout L;
    float W = (float)cssW, H = (float)cssH;
    L.narrow = W <= 900;
    L.pad = L.narrow ? 16.f : 44.f;
    L.infoX = L.pad;
    L.infoY = L.narrow ? 88.f : 112.f;
    L.infoW = L.narrow ? W - 32 : 330.f;
    L.showRatings = !L.narrow;
    L.showFigures = !L.narrow && H > 680;
    // botões (altura 64) e a faixa de carros alinhados embaixo
    float bottom = H - (L.narrow ? 18.f : 34.f);
    TextStyle cineSt{Font::BC600, 15, 15 * 0.26f};
    TextStyle playSt{Font::BC600, 22, 22 * 0.28f};
    TextStyle kbdSt{Font::Barlow500, 11, 1.1f};
    float cineW = 24 + 16 + 12 + ui.measure("CINEMATIC", cineSt) + 24;
    float playW = 34 + ui.measure("PILOTAR", playSt) + 22 + ui.measure("Enter", kbdSt) + 14 + 2 + 30;
    float actionsW = cineW + 10 + playW;
    if (L.narrow) {
      play.rect = {L.pad + cineW + 10, bottom - 64, W - L.pad * 2 - cineW - 10, 64};
      cineBtn.rect = {L.pad, bottom - 64, cineW, 64};
      bottom -= 64 + 12;
      L.strip = {L.pad, bottom - 142, W - L.pad * 2, 142};
    } else {
      play.rect = {W - L.pad - playW, bottom - 64, playW, 64};
      cineBtn.rect = {W - L.pad - actionsW, bottom - 64, cineW, 64};
      L.strip = {L.pad, bottom - 142, W - L.pad * 2 - actionsW - 32, 142};
    }
    float cw = L.narrow ? 200.f : 236.f, ch = L.narrow ? 116.f : 132.f;
    for (size_t i = 0; i < cards.size(); i++)
      cards[i].rect = {L.strip.x + 2 + i * (cw + 14), L.strip.y + L.strip.h - 6 - ch, cw, ch};
    L.headY = L.strip.y - 12 - 12;
    return L;
  }

  // -100 = nada (canvas); 0..n-1 = card; -1 = Pilotar; -2 = Cinematic
  int hitTest(float x, float y) {
    if (garageUi.to < 0.5f) return -100;
    for (size_t i = 0; i < hits.size(); i++)
      if (hits[i].r.contains(x, y)) return -1000 - (int)i;
    if (settingsTab) return settingsPanel.contains(x, y) ? -3 : -100;
    if (mapPage) return -100;
    Layout L = layout();
    for (size_t i = 0; i < cards.size(); i++)
      if (cards[i].rect.contains(x, y) && cards[i].rect.x + cards[i].rect.w <= L.strip.x + L.strip.w + 2) return (int)i;
    if (play.rect.contains(x, y)) return -1;
    // a faixa de carros recebe o ponteiro (rolagem) mesmo fora dos cards
    if (L.strip.contains(x, y)) return -3;
    return -100;
  }

  void click(int target) {
    if (target <= -1000) {
      if (hits[-1000 - target].click) hits[-1000 - target].click();
      return;
    }
    if (target >= 0) selectCar(target);
    else if (target == -1) startPlay();
    else if (target == -2) startCinematic();
  }

  void updateCursor() {
    int h = mode == Mode::Garage ? hitTest(mouseX, mouseY) : -100;
    bool pointer = h >= 0 || h <= -1000 || (h == -1 && !play.disabled) || (h == -2 && !cineBtn.disabled);
    if (pointer != cursorPointer) {
      cursorPointer = pointer;
      static SDL_Cursor* hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
      static SDL_Cursor* arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
      SDL_SetCursor(pointer ? hand : arrow);
    }
  }

  // ---------------------------------------------------------------- desenho da interface da garagem
  void drawGarageUi(float dt) {
    garageUi.update(dt);
    emptyOpacity.update(dt);
    infoT += dt;
    hits.clear();
    if (garageUi.value <= 0.001f) return;
    float W = ui.width(), H = ui.height();
    Layout L = layout();
    int hover = mode == Mode::Garage ? hitTest(mouseX, mouseY) : -100;
    ui.pushOpacity(garageUi.value);

    // topo: marca + navegação
    {
      float x = L.pad, y = L.narrow ? 20.f : 28.f;
      TextStyle b1{Font::BC700, 15, 15 * 0.32f, INK};
      TextStyle b2{Font::BC500, 15, 15 * 0.32f, INK3};
      float w1 = ui.text("SPACE", x, y + 3, b1, 15);
      float w2 = ui.text("RACER", x + w1 + 7.5f, y + 3, b2, 15);
      float nx = x + w1 + 7.5f + w2 + 48;
      // abas em quadrados: a ativa fica preta com o texto branco
      const char* tabs[2] = {"GARAGEM", "CONFIGURAÇÕES"};
      PadStyle ps = promptStyle();
      float tabH = 32, cy = y + 7.5f;
      if (ps != PadStyle::Keyboard) nx += drawPrompt(ui, ps, PadButton::LB, nx, cy, 20, "") + 10;
      for (int t = 0; t < 2; t++) {
        bool active = (t == 1) == settingsTab;
        tabAnim[t].set(active ? 1.f : 0.f);
        tabAnim[t].update(dt);
        TextStyle nav{Font::BC600, 13, 13 * 0.22f, INK};
        float nw = ui.measure(tabs[t], nav);
        Rect r{nx, cy - tabH / 2, nw + 32, tabH};
        bool hov = mode == Mode::Garage && r.contains(mouseX, mouseY);
        float a = tabAnim[t].value;
        ui.blurPanel(r, Color(1, 1, 1, 0.72f));
        if (a > 0.001f) ui.rect(r, INK.withAlpha(a));
        if (!active) ui.border(r, hov ? INK : LINE.withAlpha(2.2f), 1);
        nav.color = mix(hov ? INK : INK2, WHITE, a);
        ui.text(tabs[t], r.x + 16, cy - 6.5f, nav, 13);
        if (!active && mode == Mode::Garage) addHit(r, [this, t] { setTab(t == 1); });
        nx += r.w + 8;
      }
      if (ps != PadStyle::Keyboard) drawPrompt(ui, ps, PadButton::RB, nx + 2, cy, 20, "");
    }

    tabT.update(dt);
    if (tabT.value > 0.001f) drawSettings(dt, L);
    if (tabT.value > 0.999f) {
      ui.popOpacity();
      return;
    }
    ui.pushOpacity(1 - tabT.value);
    pageT.update(dt);
    if (pageT.value > 0.001f) drawMapPage(dt, L);
    if (pageT.value > 0.999f) {
      ui.popOpacity();
      ui.popOpacity();
      return;
    }
    ui.pushOpacity(1 - pageT.value);

    // "Escolha um carro"
    if (emptyOpacity.value > 0.001f) {
      ui.pushOpacity(emptyOpacity.value);
      TextStyle st{Font::BC500, 13, 13 * 0.4f, INK3};
      std::string s = "ESCOLHA UM CARRO";
      ui.text(s, (W - ui.measure(s, st)) / 2, H / 2 - 6.5f, st, 13);
      ui.popOpacity();
    }

    // ficha do carro (entra deslizando)
    if (infoCar) {
      float k = INFO_EASE(std::min(1.f, infoT / 0.7f));
      if (L.narrow) k = 1;
      ui.pushOpacity(k);
      float x = L.infoX - 16 * (1 - k), y = L.infoY;
      // logo da marca, um pouco grande, em cima de tudo
      auto lg = logos.find(infoCar->id);
      if (lg != logos.end()) {
        const Texture& t = *lg->second;
        float ar = (float)t.width / t.height;
        // escudo (mais alto que largo)
        float maxW = L.narrow ? 70.f : 96.f, maxH = L.narrow ? 64.f : 88.f;
        float lw = std::min(maxW, maxH * ar), lh = lw / ar;
        ui.imageCover(t.id, t.width, t.height, {x, y, lw, lh});
        y += lh + 22;
      }
      TextStyle mk{Font::BC500, 14, 14 * 0.34f, INK2};
      ui.text(upper(infoCar->maker), x, y, mk, 14);
      y += 14 + 8;
      float ns = L.narrow ? 44.f : 58.f;
      TextStyle nm{Font::BC600, ns, ns * -0.01f, INK};
      ui.text(infoCar->name, x, y, nm, ns * 0.95f);
      y += ns * 0.95f + 6;
      TextStyle meta{Font::Barlow400, 13, 13 * 0.08f, INK3};
      float mlh = ui.lineHeightNormal(meta);
      ui.text(infoMeta, x, y, meta, mlh);
      y += mlh;
      if (L.showRatings) {
        y += 26;
        TextStyle rl{Font::Barlow400, 12, 12 * 0.06f, INK2};
        TextStyle rv{Font::Barlow600, 12, 0, INK, true};
        float rlh = ui.lineHeightNormal(rl);
        for (auto& [label, v] : infoCar->ratings) {
          ui.text(upper(label), x, y, rl, rlh);
          Rect bar{x + 92 + 12, y + rlh / 2 - 1, L.infoW - 92 - 30 - 24, 2};
          ui.rect(bar, LINE);
          ui.rect({bar.x, bar.y, bar.w * v / 10, 2}, INK);
          char buf[16];
          std::snprintf(buf, sizeof buf, "%.1f", v);
          ui.text(buf, x + L.infoW - ui.measure(buf, rv), y, rv, rlh);
          y += rlh + 11;
        }
        y -= 11;
      }
      if (L.showFigures) {
        y += 22;
        ui.rect({x, y, L.infoW, 1}, LINE);
        y += 1 + 20;
        TextStyle fk{Font::Barlow400, 11, 11 * 0.12f, INK3};
        TextStyle fv{Font::BC600, 17, 17 * 0.02f, INK};
        float klh = ui.lineHeightNormal(fk), colW = (L.infoW - 36) / 3, vlh = 17 * 1.1f;
        // grade de 3 colunas; o valor quebra a linha dentro da coluna, como no CSS
        auto wrapWords = [&](const std::string& t) {
          std::vector<std::string> lines;
          std::string cur, word;
          auto push = [&] {
            if (word.empty()) return;
            std::string cand = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && ui.measure(cand, fv) > colW) { lines.push_back(cur); cur = word; }
            else cur = cand;
            word.clear();
          };
          for (char ch : t) { if (ch == ' ') push(); else word += ch; }
          push();
          if (!cur.empty()) lines.push_back(cur);
          return lines;
        };
        for (size_t row = 0; row * 3 < infoCar->figures.size(); row++) {
          size_t rowLines = 1;
          for (size_t i = row * 3; i < std::min(infoCar->figures.size(), row * 3 + 3); i++) {
            float fx = x + (i % 3) * (colW + 18);
            ui.text(upper(infoCar->figures[i].first), fx, y, fk, klh);
            auto lines = wrapWords(infoCar->figures[i].second);
            for (size_t k = 0; k < lines.size(); k++) ui.text(lines[k], fx, y + klh + 3 + k * vlh, fv, vlh);
            rowLines = std::max(rowLines, lines.size());
          }
          y += klh + 3 + rowLines * vlh + 14;
        }
      }
      ui.popOpacity();
    }

    // título da faixa
    {
      TextStyle h{Font::BC600, 12, 12 * 0.3f, INK};
      TextStyle s{Font::Barlow400, 12, 12 * 0.04f, INK3};
      float w = ui.text("CARROS", L.pad, L.headY, h, 12);
      if (promptStyle() == PadStyle::Keyboard) ui.text("Arraste o carro para girar", L.pad + w + 18, L.headY - 2, s, 14);
      else {
        float px = L.pad + w + 18;
        px += drawPrompt(ui, promptStyle(), PadButton::DPadH, px, L.headY + 6, 16, "") + 8;
        ui.text("Trocar de carro", px, L.headY - 2, s, 14);
      }
    }

    // cards
    ui.pushClip({L.strip.x, L.strip.y - 20, L.strip.w, L.strip.h + 40});
    for (size_t i = 0; i < cards.size(); i++) {
      Card& c = cards[i];
      c.hover.set(hover == (int)i ? 1.f : 0.f);
      c.selected.set(c.isSelected ? 1.f : 0.f);
      c.progressOpacity.set(c.loading ? 1.f : 0.f);
      c.hover.update(dt);
      c.selected.update(dt);
      c.progressOpacity.update(dt);
      float lift = std::max(c.hover.value, 0.f) * 3;
      Rect r = c.rect;
      r.y -= lift;
      float strong = std::max(c.hover.value, c.selected.value);
      if (strong > 0.001f) ui.shadow(r, Color(0, 0, 0, 0.35f * strong), 0, 14, 30, -18);
      ui.blurPanel(r, Color(1, 1, 1, 0.72f + 0.28f * strong));
      ui.pushClip(r);
      if (c.thumb) ui.imageCover(c.thumb->id, c.thumb->width, c.thumb->height, {r.x, r.y, r.w, r.h - 34}, 0.5f, 0.58f);
      // rótulo
      TextStyle mk{Font::Barlow400, 11, 11 * 0.2f, INK3};
      TextStyle md{Font::BC600, 17, 0, INK};
      // maker e modelo alinhados pela linha de base, 10 px acima da borda de baixo
      float baseline = r.y + r.h - 10 - ui.descent(Font::BC600, 17);
      float mkW = ui.textBaseline(upper(c.car->maker), r.x + 14, baseline, mk);
      float mdW = ui.textBaseline(c.car->name, r.x + 14 + mkW + 8, baseline, md);
      if (c.loading) {
        TextStyle ld{Font::Barlow400, 12, 0, INK3};
        ui.textBaseline(" · carregando", r.x + 14 + mkW + 8 + mdW, baseline, ld);
      }
      // categoria
      TextStyle cl{Font::BC700, 10, 10 * 0.14f, WHITE};
      std::string cat = upper(c.car->category);
      float cw = ui.measure(cat, cl) + 12;
      Rect tag{r.x + r.w - 10 - cw, r.y + 10, cw, 18};
      ui.rect(tag, INK);
      ui.text(cat, tag.x + 6, tag.y + 4, cl, 10);
      // barra de progresso enquanto carrega
      if (c.progressOpacity.value > 0.001f) {
        ui.pushOpacity(c.progressOpacity.value);
        ui.rect({r.x, r.y + r.h - 2, r.w * c.progress, 2}, ACCENT);
        ui.popOpacity();
      }
      ui.popClip();
      if (c.selected.value > 0.001f) ui.border(r, INK.withAlpha(c.selected.value), 1.5f);
    }
    ui.popClip();

    // botão (o Cinematic saiu: a cinemática abre o jogo)
    {
      play.opacity.set(play.disabled ? 0.18f : 1.f);
      play.hover.set(hover == -1 && !play.disabled ? 1.f : 0.f);
      play.opacity.update(dt);
      play.hover.update(dt);
      Rect r = play.rect;
      r.y -= play.hover.value * 2;
      ui.pushOpacity(play.opacity.value);
      ui.rect(r, mix(INK, ACCENT, play.hover.value));
      TextStyle st{Font::BC600, 22, 22 * 0.28f, WHITE};
      PadStyle ps = promptStyle();
      float tw = ui.measure("PILOTAR", st), kw = promptWidth(ui, ps, PadButton::Confirm, 22, "Enter");
      float content = tw + 22 + kw;
      float x = L.narrow ? r.x + (r.w - content) / 2 : r.x + (r.w - content) / 2, cy = r.y + r.h / 2;
      ui.text("PILOTAR", x, cy - 11, st, 22);
      drawPrompt(ui, ps, PadButton::Confirm, x + tw + 22, cy, 22, "Enter", {true});
      ui.popOpacity();
    }
    ui.popOpacity();
    ui.popOpacity();
    ui.popOpacity();
  }

  // ---------------------------------------------------------------- escolha da pista
  void drawMapPage(float dt, const Layout& L) {
    float k = INFO_EASE(pageT.value);
    ui.pushOpacity(pageT.value);
    const MapEntry& mp = *mapCards[selectedMap].map;
    // ficha da pista (mesmo desenho da ficha do carro)
    {
      float x = L.infoX - 16 * (1 - k), y = L.infoY;
      TextStyle lb{Font::BC500, 14, 14 * 0.34f, INK2};
      ui.text("ESCOLHA A PISTA", x, y, lb, 14);
      y += 14 + 8;
      float ns = L.narrow ? 44.f : 58.f;
      TextStyle nm{Font::BC600, ns, ns * -0.01f, INK};
      ui.text(mp.name, x, y, nm, ns * 0.95f);
      y += ns * 0.95f + 6;
      TextStyle meta{Font::Barlow400, 13, 13 * 0.08f, INK3};
      float mlh = ui.lineHeightNormal(meta);
      ui.text(mp.country + " · " + mp.category, x, y, meta, mlh);
      y += mlh + 22;
      ui.rect({x, y, L.infoW, 1}, LINE);
      y += 1 + 20;
      TextStyle fk{Font::Barlow400, 11, 11 * 0.12f, INK3};
      TextStyle fv{Font::BC600, 17, 17 * 0.02f, INK};
      float klh = ui.lineHeightNormal(fk), colW = (L.infoW - 36) / 3;
      for (size_t i = 0; i < mp.figures.size(); i++) {
        float fx = x + (i % 3) * (colW + 18), fy = y + (i / 3) * (klh + 3 + 17 * 1.1f + 14);
        ui.text(upper(mp.figures[i].first), fx, fy, fk, klh);
        ui.text(mp.figures[i].second, fx, fy + klh + 3, fv, 17 * 1.1f);
      }
    }
    // faixa de pistas
    {
      TextStyle h{Font::BC600, 12, 12 * 0.3f, INK};
      TextStyle s{Font::Barlow400, 12, 12 * 0.04f, INK3};
      float w = ui.text("PISTAS", L.pad, L.headY, h, 12);
      if (promptStyle() == PadStyle::Keyboard) ui.text("Use as setas ou clique para escolher", L.pad + w + 18, L.headY - 2, s, 14);
      else {
        float px = L.pad + w + 18;
        px += drawPrompt(ui, promptStyle(), PadButton::DPadH, px, L.headY + 6, 16, "") + 8;
        ui.text("Escolher a pista", px, L.headY - 2, s, 14);
      }
    }
    float cw = L.narrow ? 200.f : 236.f, ch = L.narrow ? 116.f : 132.f;
    for (size_t i = 0; i < mapCards.size(); i++) {
      MapCard& c = mapCards[i];
      Rect base{L.strip.x + 2 + i * (cw + 14), L.strip.y + L.strip.h - 6 - ch, cw, ch};
      bool hov = mode == Mode::Garage && pageT.to > 0.5f && base.contains(mouseX, mouseY);
      c.hover.set(hov ? 1.f : 0.f);
      c.selected.set((int)i == selectedMap ? 1.f : 0.f);
      c.hover.update(dt);
      c.selected.update(dt);
      Rect r = base;
      r.y -= c.hover.value * 3;
      float strong = std::max(c.hover.value, c.selected.value);
      if (strong > 0.001f) ui.shadow(r, Color(0, 0, 0, 0.35f * strong), 0, 14, 30, -18);
      ui.blurPanel(r, Color(1, 1, 1, 0.72f + 0.28f * strong));
      ui.pushClip(r);
      if (c.thumb) ui.imageCover(c.thumb->id, c.thumb->width, c.thumb->height, {r.x, r.y, r.w, r.h - 34}, 0.5f, 0.5f);
      TextStyle mk{Font::Barlow400, 11, 11 * 0.2f, INK3};
      TextStyle md{Font::BC600, 17, 0, INK};
      float baseline = r.y + r.h - 10 - ui.descent(Font::BC600, 17);
      float mkW = ui.textBaseline(upper(c.map->country), r.x + 14, baseline, mk);
      ui.textBaseline(c.map->name, r.x + 14 + mkW + 8, baseline, md);
      TextStyle cl{Font::BC700, 10, 10 * 0.14f, WHITE};
      std::string cat = upper(c.map->category);
      float tw = ui.measure(cat, cl) + 12;
      Rect tag{r.x + r.w - 10 - tw, r.y + 10, tw, 18};
      ui.rect(tag, INK);
      ui.text(cat, tag.x + 6, tag.y + 4, cl, 10);
      ui.popClip();
      if (c.selected.value > 0.001f) ui.border(r, INK.withAlpha(c.selected.value), 1.5f);
      if (pageT.to > 0.5f) addHit(base, [this, i] { selectedMap = (int)i; });
    }
    // botões: VOLTAR (vidro) e CORRER (principal)
    {
      Rect back = cineBtn.rect, race = play.rect;
      backBtn.hover.set(pageT.to > 0.5f && back.contains(mouseX, mouseY) ? 1.f : 0.f);
      raceBtn.hover.set(pageT.to > 0.5f && race.contains(mouseX, mouseY) ? 1.f : 0.f);
      backBtn.hover.update(dt);
      raceBtn.hover.update(dt);
      Rect r = back;
      r.y -= backBtn.hover.value * 2;
      if (backBtn.hover.value > 0.001f) ui.shadow(r, Color(0, 0, 0, 0.35f * backBtn.hover.value), 0, 14, 30, -18);
      ui.blurPanel(r, Color(1, 1, 1, 0.75f + 0.25f * backBtn.hover.value));
      TextStyle bs{Font::BC600, 15, 15 * 0.26f, INK};
      PadStyle ps = promptStyle();
      float tw = ui.measure("VOLTAR", bs), iw = ps == PadStyle::Keyboard ? 16.f : promptWidth(ui, ps, PadButton::Back, 22, "");
      float ax = r.x + (r.w - (tw + iw + 10)) / 2, cy = r.y + r.h / 2;
      if (ps == PadStyle::Keyboard) {
        // seta para a esquerda
        ui.triangle({ax, cy}, {ax + 7, cy - 6}, {ax + 7, cy + 6}, INK);
        ui.rect({ax + 6, cy - 1, 10, 2}, INK);
      } else {
        drawPrompt(ui, ps, PadButton::Back, ax, cy, 22, "");
      }
      ui.text("VOLTAR", ax + iw + 10, cy - 7.5f, bs, 15);
      r = race;
      r.y -= raceBtn.hover.value * 2;
      ui.rect(r, mix(INK, ACCENT, raceBtn.hover.value));
      TextStyle st{Font::BC600, 22, 22 * 0.28f, WHITE};
      float t2 = ui.measure("CORRER", st), kw = promptWidth(ui, ps, PadButton::Confirm, 22, "Enter");
      float x = r.x + (r.w - (t2 + 22 + kw)) / 2;
      ui.text("CORRER", x, cy - 11, st, 22);
      drawPrompt(ui, ps, PadButton::Confirm, x + t2 + 22, cy, 22, "Enter", {true});
      if (pageT.to > 0.5f) {
        addHit(back, [this] { setMapPage(false); });
        addHit(race, [this] { startPlay(); });
      }
    }
    ui.popOpacity();
  }

  // ---------------------------------------------------------------- configurações
  struct Hit {
    Rect r;
    std::function<void()> click;
    std::function<void(float)> drag;
  };
  void addHit(const Rect& r, std::function<void()> click, std::function<void(float)> drag = {}) {
    hits.push_back({r, std::move(click), std::move(drag)});
  }

  // estilo dos ícones: teclado ou o controle em uso
  PadStyle promptStyle() const { return usePad && input.hasPad() ? input.padStyle() : PadStyle::Keyboard; }

  void padButton(SDL_GamepadButton b) {
    if (cine.active) {
      if (b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_START) cine.requestExit();
      return;
    }
    if (mode != Mode::Garage || fade.value > 0.5f) return;
    if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) { setTab(false); return; }
    if (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) { setTab(true); return; }
    if (settingsTab) {
      if (b == SDL_GAMEPAD_BUTTON_DPAD_UP) moveFocus(-1);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) moveFocus(1);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) adjustFocus(-1);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) adjustFocus(1);
      if (b == SDL_GAMEPAD_BUTTON_SOUTH) activateFocus();
      if (b == SDL_GAMEPAD_BUTTON_EAST) setTab(false);
      return;
    }
    if (mapPage) {
      int n = (int)mapCards.size();
      if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) selectedMap = (selectedMap + n - 1) % n;
      if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) selectedMap = (selectedMap + 1) % n;
      if (b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START) startPlay();
      if (b == SDL_GAMEPAD_BUTTON_EAST) setMapPage(false);
      return;
    }
    int n = (int)cards.size(), i = selectedCard < 0 ? 0 : selectedCard;
    if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) selectCar((i + n - 1) % n);
    if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) selectCar((i + 1) % n);
    if (b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START) startPlay();
  }

  // foco das linhas das configurações (controle e setas do teclado)
  void moveFocus(int d) {
    int n = (int)rowCtls.size();
    if (!n) return;
    settingsFocus = std::max(0, std::min(n - 1, settingsFocus + d));
    focusVisible = true;
  }
  void adjustFocus(int d) {
    focusVisible = true;
    if (settingsFocus < (int)rowCtls.size() && rowCtls[settingsFocus].adjust) rowCtls[settingsFocus].adjust(d);
  }
  void activateFocus() {
    focusVisible = true;
    if (settingsFocus < (int)rowCtls.size() && rowCtls[settingsFocus].activate) rowCtls[settingsFocus].activate();
  }

  void setMapPage(bool on) {
    mapPage = on;
    pageT.set(on ? 1.f : 0.f);
  }

  void setTab(bool settingsOn) {
    if (settingsOn) setMapPage(false);
    settingsTab = settingsOn;
    tabT.set(settingsOn ? 1.f : 0.f);
    settingsScroll = 0;
  }

  // aplica as configurações na garagem, no jogo e na janela; first = na abertura
  void applySettings(bool first = false) {
    garage.applySettings(settings);
    game.applySettings();
    SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
    bool fs = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
    if (fs != settings.fullscreen) SDL_SetWindowFullscreen(window, settings.fullscreen);
    (void)first;
  }
  // mudou um item: vira "personalizado" se não bater com um nível, salva e aplica
  void changed(bool graphics = true) {
    if (graphics) settings.matchPreset();
    settings.save();
    applySettings();
  }

  // controle segmentado alinhado à direita em (right, cy); devolve a largura
  float segmented(float right, float cy, const std::vector<std::string>& opts, int selected, const std::function<void(int)>& onSelect) {
    TextStyle st{Font::BC600, 11, 11 * 0.14f, INK2};
    std::vector<float> ws;
    float total = 0;
    for (auto& o : opts) {
      ws.push_back(ui.measure(o, st) + 22);
      total += ws.back();
    }
    float x = right - total, h = 26, y = cy - h / 2;
    ui.border({x, y, total, h}, LINE.withAlpha(1.6f), 1);
    for (size_t i = 0; i < opts.size(); i++) {
      Rect r{x, y, ws[i], h};
      bool sel = (int)i == selected, hov = r.contains(mouseX, mouseY);
      if (sel) ui.rect(r, INK);
      else if (hov) ui.rect(r, Color(17 / 255.f, 18 / 255.f, 20 / 255.f, 0.06f));
      st.color = sel ? WHITE : hov ? INK : INK2;
      ui.text(opts[i], x + 11, y + 7.5f, st, 11);
      if (!sel) addHit(r, [onSelect, i] { onSelect((int)i); });
      x += ws[i];
    }
    if (!rowCtls.empty()) {
      int n = (int)opts.size();
      rowCtls.back().adjust = [onSelect, selected, n](int d) {
        int nx = selected < 0 ? (d > 0 ? 0 : n - 1) : std::max(0, std::min(n - 1, selected + d));
        if (nx != selected) onSelect(nx);
      };
    }
    return total;
  }

  void toggle(float right, float cy, bool on, const std::function<void(bool)>& onChange) {
    Rect r{right - 38, cy - 10, 38, 20};
    bool hov = r.contains(mouseX, mouseY);
    ui.rect(r, on ? (hov ? mix(INK, ACCENT, 0.35f) : INK) : Color(17 / 255.f, 18 / 255.f, 20 / 255.f, hov ? 0.22f : 0.15f), 10);
    ui.circle(on ? r.x + r.w - 10 : r.x + 10, cy, 8, WHITE);
    TextStyle st{Font::BC600, 11, 11 * 0.14f, INK3};
    std::string t = on ? "SIM" : "NÃO";
    ui.text(t, r.x - 10 - ui.measure(t, st), cy - 5.5f, st, 11);
    addHit({r.x - 40, r.y - 4, r.w + 44, r.h + 8}, [onChange, on] { onChange(!on); });
    if (!rowCtls.empty()) {
      rowCtls.back().adjust = [onChange, on](int d) { if ((d > 0) != on) onChange(d > 0); };
      rowCtls.back().activate = [onChange, on] { onChange(!on); };
    }
  }

  // slider 0..1 com o valor escrito ao lado
  void slider(float right, float cy, float v, const std::string& label, const std::function<void(float)>& onChange) {
    TextStyle vs{Font::Barlow600, 12, 0, INK, true};
    float valW = 44, w = 170;
    Rect track{right - valW - w, cy - 1, w, 2};
    ui.rect(track, LINE.withAlpha(1.4f));
    ui.rect({track.x, track.y, track.w * v, 2}, INK);
    Rect knob{track.x + track.w * v - 7, cy - 7, 14, 14};
    bool hov = Rect{track.x - 8, cy - 12, w + 16, 24}.contains(mouseX, mouseY) || dragFn;
    ui.circle(knob.x + 7, cy, hov ? 7.f : 6.f, INK);
    ui.text(label, right - ui.measure(label, vs), cy - 8, vs, 16);
    float tx = track.x, tw = track.w;
    addHit({track.x - 8, cy - 12, w + 16, 24}, {}, [onChange, tx, tw](float mx) { onChange(std::fmax(0.f, std::fmin(1.f, (mx - tx) / tw))); });
    if (!rowCtls.empty()) rowCtls.back().adjust = [onChange, v](int d) { onChange(std::fmax(0.f, std::fmin(1.f, v + d * 0.05f))); };
  }

  void drawSettings(float dt, const Layout& L) {
    (void)dt;
    rowCtls.clear();
    float k = INFO_EASE(tabT.value);
    float H = ui.height();
    float pw = std::min(500.f, ui.width() - L.pad * 2);
    const float rowH = 40, headH = 30, gapS = 18, pad = 28;
    const int rows = 12, heads = 3;
    float contentH = pad + heads * headH + rows * rowH + (heads - 1) * gapS + 20 + 34 + pad;
    float top = L.infoY - 8, maxH = H - top - 24;
    float ph = std::min(contentH, maxH);
    settingsScroll = std::fmax(0.f, std::fmin(settingsScroll, contentH - ph));
    Rect panel{L.pad - 16 * (1 - k), top, pw, ph};
    settingsPanel = panel;
    ui.pushOpacity(tabT.value);
    ui.shadow(panel, Color(0, 0, 0, 0.18f), 0, 14, 30, -18);
    ui.blurPanel(panel, Color(1, 1, 1, 0.78f));
    ui.pushClip(panel);
    float x = panel.x + pad, right = panel.x + pw - pad, y = panel.y + pad - settingsScroll;
    TextStyle head{Font::BC600, 12, 12 * 0.3f, INK};
    TextStyle lab{Font::Barlow400, 12, 12 * 0.06f, INK2};
    float llh = ui.lineHeightNormal(lab);
    auto section = [&](const char* t) {
      ui.text(t, x, y, head, 12);
      ui.rect({x, y + 20, right - x, 1}, LINE);
      y += headH;
    };
    auto row = [&](const std::string& label) {
      int idx = (int)rowCtls.size();
      rowCtls.push_back({y + settingsScroll - panel.y, rowH, {}, {}});
      if (focusVisible && idx == settingsFocus) {
        ui.rect({x - 14, y + 2, right - x + 28, rowH - 4}, Color(17 / 255.f, 18 / 255.f, 20 / 255.f, 0.06f));
        ui.rect({x - 14, y + 2, 3, rowH - 4}, ACCENT);
      }
      ui.text(upper(label), x, y + (rowH - llh) / 2, lab, llh);
      return y + rowH / 2;
    };
    auto next = [&] { y += rowH; };

    section("GRÁFICOS");
    {
      float cy = row(settings.preset < 0 ? "Qualidade · personalizada" : "Qualidade");
      segmented(right, cy, {"BAIXO", "MÉDIO", "ALTO", "ULTRA"}, settings.preset, [this](int i) {
        settings.applyPreset(i);
        changed(false);
      });
      next();
    }
    {
      float cy = row("Resolução");
      char buf[16];
      std::snprintf(buf, sizeof buf, "%d%%", (int)std::lround(settings.pixelRatio * 100));
      slider(right, cy, (settings.pixelRatio - 0.5f) / 1.5f, buf, [this](float v) {
        float pr = std::round((0.5f + v * 1.5f) * 20) / 20;  // passos de 5%
        if (std::fabs(pr - settings.pixelRatio) < 0.001f) return;
        settings.pixelRatio = pr;
        changed();
      });
      next();
    }
    {
      float cy = row("Anti-serrilhado");
      int sel = settings.msaa == 0 ? 0 : settings.msaa == 2 ? 1 : 2;
      segmented(right, cy, {"FXAA", "MSAA 2×", "MSAA 4×"}, sel, [this](int i) {
        settings.msaa = i == 0 ? 0 : i == 1 ? 2 : 4;
        changed();
      });
      next();
    }
    {
      float cy = row("Oclusão de ambiente");
      segmented(right, cy, {"DESLIGADA", "NORMAL", "ALTA"}, settings.ao, [this](int i) {
        settings.ao = i;
        changed();
      });
      next();
    }
    {
      float cy = row("Sombras");
      int sel = settings.shadow == 1024 ? 0 : settings.shadow == 2048 ? 1 : 2;
      segmented(right, cy, {"BAIXA", "MÉDIA", "ALTA"}, sel, [this](int i) {
        settings.shadow = i == 0 ? 1024 : i == 1 ? 2048 : 4096;
        changed();
      });
      next();
    }
    {
      float cy = row("Brilho (bloom)");
      toggle(right, cy, settings.bloom, [this](bool b) { settings.bloom = b; changed(); });
      next();
    }
    {
      float cy = row("Efeitos de lente");
      toggle(right, cy, settings.lensFx, [this](bool b) { settings.lensFx = b; changed(false); });
      next();
    }
    {
      float cy = row("Desfoque de velocidade");
      toggle(right, cy, settings.speedBlur, [this](bool b) { settings.speedBlur = b; changed(false); });
      next();
    }
    {
      float cy = row("Qualidade automática");
      toggle(right, cy, settings.autoQuality, [this](bool b) { settings.autoQuality = b; changed(false); });
      next();
    }
    y += gapS;
    section("TELA");
    {
      float cy = row("Tela cheia (F11)");
      toggle(right, cy, settings.fullscreen, [this](bool b) { settings.fullscreen = b; changed(false); });
      next();
    }
    {
      float cy = row("Sincronia vertical (VSync)");
      toggle(right, cy, settings.vsync, [this](bool b) { settings.vsync = b; changed(false); });
      next();
    }
    y += gapS;
    section("SOM");
    {
      float cy = row("Volume");
      char buf[16];
      std::snprintf(buf, sizeof buf, "%d%%", (int)std::lround(settings.volume * 100));
      slider(right, cy, settings.volume, buf, [this](float v) {
        float vol = std::round(v * 100) / 100;
        if (std::fabs(vol - settings.volume) < 0.001f) return;
        settings.volume = vol;
        changed(false);
      });
      next();
    }
    // rodapé: restaurar padrão + aviso de salvamento
    y += 20;
    {
      TextStyle bt{Font::BC600, 12, 12 * 0.26f, INK};
      std::string t = "RESTAURAR PADRÃO";
      float tw = ui.measure(t, bt);
      Rect r{x, y, tw + 28, 34};
      int idx = (int)rowCtls.size();
      rowCtls.push_back({y + settingsScroll - panel.y, 34, {}, [this] {
        bool fs = settings.fullscreen;
        std::string car = settings.lastCar, mp = settings.lastMap;
        settings = Settings{};
        settings.fullscreen = fs;
        settings.lastCar = car;
        settings.lastMap = mp;
        changed(false);
      }});
      bool hov = r.contains(mouseX, mouseY) || (focusVisible && idx == settingsFocus);
      ui.border(r, hov ? INK : LINE.withAlpha(2.5f), 1);
      ui.text(t, x + 14, y + 11, bt, 12);
      addHit(r, [this] {
        bool fs = settings.fullscreen;
        std::string car = settings.lastCar;
        settings = Settings{};
        settings.fullscreen = fs;
        settings.lastCar = car;
        changed(false);
      });
      TextStyle hint{Font::Barlow400, 12, 12 * 0.04f, INK3};
      std::string hs = "Salvo automaticamente";
      ui.text(hs, right - ui.measure(hs, hint), y + 9, hint, 16);
    }
    ui.popClip();
    // mantém a linha em foco visível
    if (focusVisible && settingsFocus < (int)rowCtls.size()) {
      float fy = rowCtls[settingsFocus].y, fh = rowCtls[settingsFocus].h;
      if (fy - settingsScroll < pad) settingsScroll = std::max(0.f, fy - pad);
      if (fy + fh - settingsScroll > ph - pad) settingsScroll = fy + fh - ph + pad;
    }
    // dica dos controles na base do painel
    if (promptStyle() != PadStyle::Keyboard) {
      PadStyle ps = promptStyle();
      TextStyle hs{Font::Barlow400, 12, 12 * 0.04f, INK3};
      float hx = panel.x + 4, hy = panel.y + ph + 18;
      hx += drawPrompt(ui, ps, PadButton::DPadV, hx, hy, 16, "") + 6;
      hx += ui.text("Escolher", hx, hy - 8, hs, 16) + 16;
      hx += drawPrompt(ui, ps, PadButton::DPadH, hx, hy, 16, "") + 6;
      hx += ui.text("Ajustar", hx, hy - 8, hs, 16) + 16;
      hx += drawPrompt(ui, ps, PadButton::Back, hx, hy, 18, "") + 6;
      ui.text("Voltar", hx, hy - 8, hs, 16);
    }
    // barra de rolagem quando o painel não cabe
    if (contentH > ph + 1) {
      float bh = ph * ph / contentH, by = panel.y + (ph - bh) * settingsScroll / (contentH - ph);
      ui.rect({panel.x + pw - 5, by + 4, 3, bh - 8}, Color(0, 0, 0, 0.18f), 1.5f);
    }
    ui.popOpacity();
  }

  void drawFade(float dt) {
    fade.update(dt);
    fadeBar.update(dt);
    boot.update(dt);
    float W = ui.width(), H = ui.height();
    if (fade.value > 0.001f) {
      ui.pushOpacity(fade.value);
      ui.rect({0, 0, W, H}, Color::hex(0xf3f3f2));
      float bw = std::min(320.f, W * 0.7f), x = (W - bw) / 2;
      TextStyle st{Font::BC500, 12, 12 * 0.34f, Color::hex(0x6b6e74)};
      float blockH = 12 + 14 + 2, y = (H - blockH) / 2;
      if (!fadeText.empty()) {
        std::string t = upper(fadeText);
        ui.text(t, x + (bw - ui.measure(t, st)) / 2, y, st, 12);
      }
      ui.rect({x, y + 12 + 14, bw, 2}, Color(0, 0, 0, 0.08f));
      ui.rect({x, y + 12 + 14, bw * fadeBar.value, 2}, ACCENT);
      ui.popOpacity();
    }
    // tela de abertura enquanto tudo carrega
    bootFrames++;
    if (boot.value > 0.001f) {
      ui.pushOpacity(boot.value);
      ui.rect({0, 0, W, H}, Color::hex(0xf3f3f2));
      TextStyle b1{Font::BC700, 15, 15 * 0.32f, INK};
      TextStyle b2{Font::BC500, 15, 15 * 0.32f, INK3};
      float w1 = ui.measure("SPACE", b1), w2 = ui.measure("RACER", b2);
      float x = (W - (w1 + 7.5f + w2)) / 2;
      ui.text("SPACE", x, H / 2 - 7.5f, b1, 15);
      ui.text("RACER", x + w1 + 7.5f, H / 2 - 7.5f, b2, 15);
      if (loadingCard >= 0) {
        float bw = 160, bx = (W - bw) / 2, by = H / 2 + 26;
        ui.rect({bx, by, bw, 2}, Color(0, 0, 0, 0.08f));
        ui.rect({bx, by, bw * cards[loadingCard].progress, 2}, ACCENT);
      }
      ui.popOpacity();
    }
  }

  SDL_Window* window;
  Renderer renderer;
  UI ui;
  Input input;
  Garage garage;
  Game game;
  Cinematic cine;
  Backdrop backdrop;
  GLuint backdropTex = 0;
  PostFX* lastFx = nullptr;
  Mode mode = Mode::Garage;
  Settings settings;
  bool settingsTab = false;
  bool usePad = false;          // último aparelho usado foi o controle (troca os ícones)
  int stickX = 0, stickY = 0;   // analógico como direcional nos menus
  struct RowCtl {
    float y, h;  // posição no conteúdo do painel
    std::function<void(int)> adjust;
    std::function<void()> activate;
  };
  std::vector<RowCtl> rowCtls;
  int settingsFocus = 0;
  bool focusVisible = false;
  Transition tabAnim[2] = {Transition(1, 0.2f), Transition(0, 0.2f)};
  Transition tabT{0, 0.35f};
  std::vector<Hit> hits;
  std::function<void(float)> dragFn;
  Rect settingsPanel;
  float settingsScroll = 0;
  int fbW = 1, fbH = 1, cssW = 1, cssH = 1;
  float dpr = 1;
  float mouseX = -1, mouseY = -1;
  int pressTarget = -100;
  bool swallowClick = false, cursorPointer = false;
  std::vector<std::pair<float, std::function<void()>>> timers;

  std::vector<Card> cards;
  Button play, cineBtn, raceBtn, backBtn;
  std::vector<MapCard> mapCards;
  int selectedMap = 0;
  bool mapPage = false;
  Transition pageT{0, 0.35f};
  int selectedCard = -1, loadingCard = -1;
  std::map<std::string, std::shared_ptr<LoadedCar>> cache;
  std::map<std::string, TexturePtr> logos;
  std::thread loadThread;
  std::atomic<float> loadProgress{0};
  std::atomic<bool> loadDone{false};
  std::string loadError;
  std::shared_ptr<LoadedCar> loadResult;

  const CarEntry* infoCar = nullptr;
  std::string infoMeta;
  float infoT = 0;
  Transition garageUi{1, 0.5f}, emptyOpacity{1, 0.6f};
  Transition fade{0, 0.4f}, fadeBar{0, 0.2f}, boot{1, 0.6f};
  std::string fadeText;
  bool waitingPrepare = false;
  int bootFrames = 0;
  bool introCinematic = true;
  bool hideHud = false;  // roteiro de teste: fotos sem interface  // ao abrir o jogo, começa pela cinemática
};

int main(int argc, char** argv) {
  (void)argc;
  initAssetRoot(argv[0]);
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  SDL_Window* window = SDL_CreateWindow("Space Racer", 1600, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MAXIMIZED);
  if (!window) {
    std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GLContext ctx = SDL_GL_CreateContext(window);
  if (!ctx) {
    std::fprintf(stderr, "OpenGL 4.1 indisponível: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_MakeCurrent(window, ctx);
  SDL_GL_SetSwapInterval(1);
  if (!gl::load([](const char* n) { return (void*)SDL_GL_GetProcAddress(n); })) return 1;
  std::printf("[gl] %s · %s\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

  int code = 0;
  try {
    App app(window);
    if (const char* sc = SDL_getenv("SR_SCRIPT")) app.setScript(sc);
    Uint64 last = SDL_GetPerformanceCounter();
    while (app.running) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) app.event(e);
      Uint64 now = SDL_GetPerformanceCounter();
      float dt = std::min(0.05f, (float)((now - last) / (double)SDL_GetPerformanceFrequency()));
      last = now;
      app.frame(dt);
      SDL_GL_SwapWindow(window);
    }
  } catch (std::exception& ex) {
    std::fprintf(stderr, "erro: %s\n", ex.what());
    code = 1;
  }
  SDL_GL_DestroyContext(ctx);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return code;
}
