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
    bootLogoTex = loadImage("assets/ui/logo.png", false);
    if (bootLogoTex) { bootLogoTex->repeat = false; bootLogoTex->upload(); }
    bootIconTex = loadImage("assets/ui/icone.png", false);
    if (bootIconTex) { bootIconTex->repeat = false; bootIconTex->upload(); }
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
    for (int i = 0; i < 2; i++) {
      skyThumb[i] = loadImage(i == 0 ? "assets/maps/sky-day.jpg" : "assets/maps/sky-sunset.jpg", false);
      if (skyThumb[i]) skyThumb[i]->upload();
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
    startBootIntro();
  }

  ~App() {
    if (loadThread.joinable()) loadThread.join();
    settings.save();  // guarda o tamanho/posição da janela
  }

  // modo janela: lembra tamanho, posição e se está maximizada (ignora a tela cheia)
  void rememberWindow() {
    SDL_WindowFlags f = SDL_GetWindowFlags(window);
    if (f & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED)) return;
    settings.windowMax = f & SDL_WINDOW_MAXIMIZED;
    if (settings.windowMax) return;
    SDL_GetWindowSize(window, &settings.windowW, &settings.windowH);
    int x, y;
    if (SDL_GetWindowPosition(window, &x, &y) && x >= 0 && y >= 0) {
      settings.windowX = x;
      settings.windowY = y;
    }
  }

  // tamanhos prontos do modo janela (o último item é "maximizada")
  static constexpr int WINDOW_SIZES[3][2] = {{1280, 720}, {1600, 900}, {1920, 1080}};
  int windowSizeIndex() const {
    if (settings.windowMax) return 3;
    for (int i = 0; i < 3; i++)
      if (settings.windowW == WINDOW_SIZES[i][0] && settings.windowH == WINDOW_SIZES[i][1]) return i;
    return -1;  // tamanho livre (redimensionada à mão)
  }
  void setWindowSize(int i) {
    settings.fullscreen = false;
    SDL_SetWindowFullscreen(window, false);
    if (i == 3) {
      settings.windowMax = true;
      SDL_MaximizeWindow(window);
    } else {
      settings.windowMax = false;
      settings.windowW = WINDOW_SIZES[i][0];
      settings.windowH = WINDOW_SIZES[i][1];
      SDL_RestoreWindow(window);
      SDL_SetWindowSize(window, settings.windowW, settings.windowH);
      SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
      settings.windowX = settings.windowY = -1;
    }
    changed(false);
  }

  // "restaurar padrão" volta gráficos e som, mas mantém a janela e as escolhas da garagem
  void resetSettings() {
    Settings keep = settings;
    settings = Settings{};
    settings.fullscreen = keep.fullscreen;
    settings.windowW = keep.windowW;
    settings.windowH = keep.windowH;
    settings.windowX = keep.windowX;
    settings.windowY = keep.windowY;
    settings.windowMax = keep.windowMax;
    settings.lastCar = keep.lastCar;
    settings.lastMap = keep.lastMap;
    changed(false);
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
    // a interface (HUD, menus) foi desenhada para 1600×900 e escala junto com a janela,
    // ocupando sempre a mesma proporção da tela
    uiScale = std::max(0.3f, std::min(cssW / 1600.f, cssH / 900.f));
    uiW = cssW / uiScale;
    uiH = cssH / uiScale;
    if (mode == Mode::Game) game.resize(cssW, cssH, dpr);
    else garage.resize(cssW, cssH, dpr);
    // os dois precisam do tamanho certo quando forem ativados
    game.resize(cssW, cssH, dpr);
  }

  void event(const SDL_Event& e) {
    switch (e.type) {
      case SDL_EVENT_QUIT: running = false; break;
      case SDL_EVENT_WINDOW_RESIZED:
        rememberWindow();
        resize();
        break;
      case SDL_EVENT_WINDOW_MOVED:
      case SDL_EVENT_WINDOW_MAXIMIZED:
      case SDL_EVENT_WINDOW_RESTORED: rememberWindow(); break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
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
          if (sc == SDL_SCANCODE_R && !e.key.repeat) resetSettings();
          break;
        }
        if (mode == Mode::Garage && !e.key.repeat && mapPage) {
          if (sc == SDL_SCANCODE_ESCAPE) setMapPage(false);
          if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) startPlay();
          if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN) {
            settings.timeOfDay = 1 - settings.timeOfDay;
            settings.save();
          }
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
        // mouseX/Y em unidades da interface; a câmera usa a posição real da janela
        mouseX = e.motion.x / uiScale;
        mouseY = e.motion.y / uiScale;
        if (dragFn) {
          dragFn(mouseX);
          break;
        }
        if (stripPress) {
          float dx = mouseX - stripPressX;
          if (std::fabs(dx) > 6) stripDragged = true;
          if (stripDragged) {
            stripTarget = std::clamp(stripPressScroll - dx, 0.f, stripMax);
            stripScroll = stripTarget;  // acompanha o dedo/mouse sem atraso
          }
          break;
        }
        if (mode == Mode::Game) game.mouseMove(e.motion.x, e.motion.y, (float)cssH);
        else garage.mouseMove(e.motion.x, e.motion.y);
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        usePad = false;
        float x = e.button.x / uiScale, y = e.button.y / uiScale;
        if (cine.active) {
          cine.requestExit();
          swallowClick = true;
          break;
        }
        if (fade.value > 0.5f) break;  // a tela de transição bloqueia cliques
        if (mode == Mode::Game) {
          if (e.button.button == 1 && game.hudClick(x, y)) break;
          game.mouseDown(e.button.button, e.button.x, e.button.y);
        } else if (mode == Mode::Garage) {
          pressTarget = hitTest(x, y);
          if (pressTarget == -100) garage.mouseDown(e.button.button, e.button.x, e.button.y);
          // pressionou sobre a lista de carros: pode virar um arrasto para os lados
          if (e.button.button == 1 && !settingsTab && !mapPage && (pressTarget >= 0 || pressTarget == -3)) {
            stripPress = true;
            stripDragged = false;
            stripPressX = x;
            stripPressScroll = stripTarget;
          }
          if (pressTarget <= -1000 && e.button.button == 1 && hits[-1000 - pressTarget].drag) {
            dragFn = hits[-1000 - pressTarget].drag;
            dragFn(x);
            pressTarget = -100;
          }
        }
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        float x = e.button.x / uiScale, y = e.button.y / uiScale;
        game.mouseUp();
        garage.mouseUp();
        dragFn = nullptr;
        if (swallowClick) {
          swallowClick = false;
          break;
        }
        // soltou depois de arrastar a lista: não é clique
        if (stripPress) {
          stripPress = false;
          if (stripDragged) {
            stripDragged = false;
            pressTarget = -100;
            break;
          }
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
        else if (mode == Mode::Garage && !settingsTab && !mapPage && stripMax > 0 && layout().strip.contains(mouseX, mouseY))
          stripTarget = std::clamp(stripTarget + (dy + e.wheel.x * 100) * 0.6f, 0.f, stripMax);
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

    ui.begin(fbW, fbH, dpr * uiScale);
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
      else if (op == "press" || op == "release") {
        // segurar/soltar o botão esquerdo (para testar arrastos): "press 320 600"
        std::string ys;
        ss >> ys;
        SDL_Event d{};
        d.type = op == "press" ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        d.button.button = 1;
        d.button.x = std::stof(arg);
        d.button.y = std::stof(ys);
        event(d);
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
  // rola a lista para o card i ficar inteiro na tela, com um pedaço do vizinho aparecendo
  void scrollToCard(int i) {
    Layout L = layout();
    float step = L.cardW + 14, peek = 48;
    float left = 2 + i * step, right = left + L.cardW;
    if (right - stripTarget > L.strip.w - (i + 1 < (int)cards.size() ? peek : 0)) stripTarget = right - L.strip.w + (i + 1 < (int)cards.size() ? peek : 0);
    if (left - stripTarget < (i > 0 ? peek : 0)) stripTarget = left - (i > 0 ? peek : 0);
    stripTarget = std::clamp(stripTarget, 0.f, stripMax);
  }

  void selectCar(int i) {
    scrollToCard(i);
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
    bootProgress = loadProgress;
    if (!loadDone) return;
    loadThread.join();
    int i = loadingCard;
    loadingCard = -1;
    c.loading = false;
    if (!loadError.empty()) {
      std::fprintf(stderr, "%s\n", loadError.c_str());
      infoMeta = "Erro ao carregar o modelo: " + loadError;
      introCinematic = false;
      bootStage = BootStage::Done;  // a abertura não deve esconder o erro
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
      bootProgress = 1;
      bootAssetsReady = true;
      tryFinishBoot();
    }
  }

  // tela de abertura: logo (fade in/hold/fade out) → texto + ícone com anel de carregamento.
  // Cada etapa é agendada com after(); tryFinishBoot() só sai da etapa de texto quando o carro
  // selecionado já carregou E o texto já ficou visível tempo suficiente para ser lido.
  void startBootIntro() {
    bootLogoOpacity.set(1);
    after(0.9f + 0.9f, [this] {
      if (bootStage != BootStage::Logo) return;
      bootLogoOpacity.set(0);
    });
    after(0.9f + 0.9f + 0.9f + 0.3f, [this] {
      if (bootStage != BootStage::Logo) return;
      bootStage = BootStage::Text;
      bootTextOpacity.set(1);
      after(2.2f, [this] {
        bootTextMinTimeDone = true;
        tryFinishBoot();
      });
    });
  }

  void tryFinishBoot() {
    if (bootStage != BootStage::Text || !bootTextMinTimeDone || !bootAssetsReady) return;
    bootStage = BootStage::Done;
    startCinematic();
    boot.set(0);
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
    fadeError = false;
    fadeBar.snap(0);
    after(0.45f, [this, &mp] {
      game.beginPrepare(mp, settings.timeOfDay);
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
      fadeError = true;
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
    fadeError = false;
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
    float cardW = 236;
  };
  Layout layout() {
    Layout L;
    float W = uiW, H = uiH;
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
    // a lista rola na horizontal quando os carros não cabem
    float contentW = cards.empty() ? 0 : cards.size() * (cw + 14) - 14 + 4;
    stripMax = std::max(0.f, contentW - L.strip.w);
    stripTarget = std::clamp(stripTarget, 0.f, stripMax);
    stripScroll = std::clamp(stripScroll, 0.f, stripMax);
    for (size_t i = 0; i < cards.size(); i++)
      cards[i].rect = {L.strip.x + 2 + i * (cw + 14) - stripScroll, L.strip.y + L.strip.h - 6 - ch, cw, ch};
    L.cardW = cw;
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
      if (cards[i].rect.contains(x, y) && x >= L.strip.x && x <= L.strip.x + L.strip.w) return (int)i;
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

    // cards (a rolagem desliza suave até o alvo)
    stripScroll += (stripTarget - stripScroll) * (1 - std::exp(-dt * 12));
    if (std::fabs(stripTarget - stripScroll) < 0.3f) stripScroll = stripTarget;
    for (size_t i = 0; i < cards.size(); i++) {
      Card& c = cards[i];
      c.hover.set(hover == (int)i ? 1.f : 0.f);
      c.selected.set(c.isSelected ? 1.f : 0.f);
      c.progressOpacity.set(c.loading ? 1.f : 0.f);
      c.hover.update(dt);
      c.selected.update(dt);
      c.progressOpacity.update(dt);
    }
    auto drawCard = [&](Card& c) {
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
    };
    // onde há carros escondidos, a borda da lista some num degradê de transparência
    // (fatias com opacidade decrescente: o fundo do estúdio aparece através dos cards)
    const float fadeW = 80;
    float fadeR = fadeW * std::clamp((stripMax - stripScroll) / 40, 0.f, 1.f);
    float fadeL = fadeW * std::clamp(stripScroll / 40, 0.f, 1.f);
    float top = L.strip.y - 20, hgt = L.strip.h + 40;
    auto drawSlice = [&](float x0, float x1, float alpha) {
      if (x1 <= x0 || alpha <= 0.003f) return;
      ui.pushClip({x0, top, x1 - x0, hgt});
      ui.pushOpacity(alpha);
      for (auto& c : cards)
        if (c.rect.x < x1 && c.rect.x + c.rect.w > x0) drawCard(c);
      ui.popOpacity();
      ui.popClip();
    };
    drawSlice(L.strip.x + fadeL, L.strip.x + L.strip.w - fadeR, 1);
    const int N = 16;
    for (int k = 0; k < N; k++) {
      // de 0 (borda) a 1: começa a sumir devagar, para o pedaço do próximo carro continuar legível
      float t = (k + 0.5f) / N, ease = std::sqrt(t);
      if (fadeL > 0) drawSlice(L.strip.x + fadeL * k / N, L.strip.x + fadeL * (k + 1) / N, ease);
      if (fadeR > 0) drawSlice(L.strip.x + L.strip.w - fadeR * (k + 1) / N, L.strip.x + L.strip.w - fadeR * k / N, ease);
    }

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
      // horário da corrida: dia ou pôr do sol (céu, luz e neblina mudam)
      y += ((mp.figures.size() + 2) / 3) * (klh + 3 + 17 * 1.1f + 14) + 8;
      ui.text("HORÁRIO", x, y, fk, klh);
      y += klh + 8;
      const char* names[2] = {"DIA", "PÔR DO SOL"};
      float tw = (L.infoW - 12) / 2, th = L.narrow ? 54.f : 70.f;
      for (int i = 0; i < 2; i++) {
        Rect r{x + i * (tw + 12), y, tw, th + 28};
        bool sel = settings.timeOfDay == i;
        bool hov = pageT.to > 0.5f && r.contains(mouseX, mouseY);
        if (sel || hov) ui.shadow(r, Color(0, 0, 0, sel ? 0.25f : 0.15f), 0, 10, 24, -14);
        ui.blurPanel(r, Color(1, 1, 1, sel ? 1.f : hov ? 0.9f : 0.72f));
        ui.pushClip(r);
        if (skyThumb[i]) ui.imageCover(skyThumb[i]->id, skyThumb[i]->width, skyThumb[i]->height, {r.x, r.y, r.w, th}, 0.5f, 0.6f);
        ui.popClip();
        TextStyle ts{Font::BC600, 12, 12 * 0.2f, sel ? INK : INK2};
        ui.text(names[i], r.x + 10, r.y + th + 8, ts, 12);
        if (sel) ui.border(r, INK, 1.5f);
        if (pageT.to > 0.5f && !sel) addHit(r, [this, i] { settings.timeOfDay = i; settings.save(); });
      }
      if (promptStyle() != PadStyle::Keyboard) {
        TextStyle hs{Font::Barlow400, 11, 0, INK3};
        float hx = x, hy = y + th + 28 + 16;
        hx += drawPrompt(ui, promptStyle(), PadButton::DPadV, hx, hy, 14, "") + 6;
        ui.text("Trocar horário", hx, hy - 7, hs, 14);
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
      if (b == SDL_GAMEPAD_BUTTON_NORTH) resetSettings();
      if (b == SDL_GAMEPAD_BUTTON_EAST) setTab(false);
      return;
    }
    if (mapPage) {
      int n = (int)mapCards.size();
      if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) selectedMap = (selectedMap + n - 1) % n;
      if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) selectedMap = (selectedMap + 1) % n;
      if (b == SDL_GAMEPAD_BUTTON_DPAD_UP || b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
        settings.timeOfDay = 1 - settings.timeOfDay;
        settings.save();
      }
      if (b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START) startPlay();
      if (b == SDL_GAMEPAD_BUTTON_EAST) setMapPage(false);
      return;
    }
    int n = (int)cards.size(), i = selectedCard < 0 ? 0 : selectedCard;
    if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) selectCar((i + n - 1) % n);
    if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) selectCar((i + 1) % n);
    if (b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START) startPlay();
  }

  // foco das linhas das configurações (controle e setas do teclado); passar do topo/fim da
  // lista troca de categoria (só há Gráficos e Áudio) e entra pela primeira linha dela
  void moveFocus(int d) {
    int n = (int)rowCtls.size();
    if (!n) return;
    int nf = settingsFocus + d;
    if (nf < 0 || nf >= n) {
      settingsCategory = 1 - settingsCategory;
      settingsFocus = 0;
    } else {
      settingsFocus = nf;
    }
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

  // ícones de linha simples, em vetor, na cor c (s = tamanho do lado)
  void settingsIcon(int kind, float cx, float cy, float s, const Color& c) {
    if (kind == 0) {  // monitor (Gráficos)
      float w = s, h = s * 0.72f;
      ui.border({cx - w / 2, cy - h / 2 - s * 0.06f, w, h}, c, std::max(1.3f, s * 0.09f), s * 0.1f);
      ui.rect({cx - s * 0.22f, cy + h / 2 - s * 0.05f, s * 0.44f, s * 0.07f}, c, s * 0.03f);
    } else {  // equalizador (Áudio)
      float bw = s * 0.16f, gap = s * 0.14f;
      float hs[3] = {s * 0.34f, s * 0.64f, s * 0.46f};
      float x0 = cx - (bw * 3 + gap * 2) / 2;
      for (int i = 0; i < 3; i++) ui.rect({x0 + i * (bw + gap), cy + s * 0.34f - hs[i], bw, hs[i]}, c, bw * 0.3f);
    }
  }

  // linha "< valor >" no estilo do menu: rótulo à esquerda, valor com setas à direita.
  // onAdjust(d) recebe −1 (seta/D-pad esquerda) ou +1 (direita); registra o foco em rowCtls.
  void settingRow(float x, float right, float& y, const std::string& label, const std::string& value, const std::string& desc,
                  const std::function<void(int)>& onAdjust, bool enabled = true) {
    const float rowH = 44;
    Rect rowRect{x - 14, y, right - x + 28, rowH};
    bool hov = enabled && rowRect.contains(mouseX, mouseY);
    int idx = (int)rowCtls.size();
    bool focused = focusVisible && idx == settingsFocus;
    if (hov || focused) {
      ui.rect(rowRect, Color(1, 1, 1, focused ? 0.09f : 0.05f), 6);
      if (focused) ui.rect({rowRect.x, rowRect.y, 3, rowRect.h}, ACCENT, 2);
    }
    rowCtls.push_back({y + settingsScroll - settingsContentTop, rowH, onAdjust, [onAdjust] { onAdjust(1); }, label, desc, hov});
    float cy = y + rowH / 2;
    TextStyle lab{Font::Barlow400, 14, 0, Color(1, 1, 1, enabled ? 0.9f : 0.35f)};
    ui.text(label, x, cy - ui.lineHeightNormal(lab) / 2, lab);
    TextStyle vs{Font::BC600, 13, 13 * 0.04f, Color(1, 1, 1, enabled ? 1.f : 0.35f), true};
    float chev = 22, valueW = std::max(64.f, ui.measure(value, vs) + 8), blockW = chev * 2 + valueW;
    float bx = right - blockW;
    auto arrow = [&](float ax, bool pointRight) {
      Rect ar{ax, cy - chev / 2, chev, chev};
      bool ahov = enabled && ar.contains(mouseX, mouseY);
      Color c = !enabled ? Color(1, 1, 1, 0.15f) : ahov ? WHITE : Color(1, 1, 1, 0.55f);
      float s = chev * 0.3f, mx = ax + chev / 2, dir = pointRight ? 1.f : -1.f;
      ui.triangle({mx + s * 0.5f * dir, cy}, {mx - s * 0.5f * dir, cy - s}, {mx - s * 0.5f * dir, cy + s}, c);
      if (enabled) addHit(ar, [onAdjust, pointRight] { onAdjust(pointRight ? 1 : -1); });
    };
    arrow(bx, false);
    float vw = ui.measure(value, vs);
    ui.text(value, bx + chev + (valueW - vw) / 2, cy - ui.lineHeightNormal(vs) / 2, vs);
    arrow(bx + chev + valueW, true);
    y += rowH;
  }

  // divisor fino entre grupos de opções
  void settingsDivider(float x, float right, float& y) {
    ui.rect({x, y + 8, right - x, 1}, Color(1, 1, 1, 0.08f));
    y += 24;
  }

  // avança/recua um índice entre 0..n−1, dando a volta; −1 (personalizado) entra por uma ponta
  static int cycleIndex(int cur, int d, int n) {
    if (cur < 0) return d > 0 ? 0 : n - 1;
    return ((cur + d) % n + n) % n;
  }

  void drawSettings(float dt, const Layout& L) {
    (void)dt;
    rowCtls.clear();
    float k = INFO_EASE(tabT.value);
    float top = L.narrow ? 72.f : 86.f, bottomReserve = L.narrow ? 46.f : 56.f;
    Rect panel{L.pad - 16 * (1 - k), top, uiW - L.pad * 2, uiH - top - bottomReserve};
    settingsPanel = panel;
    settingsScroll = std::fmax(0.f, std::fmin(settingsScroll, std::fmax(0.f, settingsContentH[settingsCategory] - panel.h)));
    ui.pushOpacity(tabT.value);
    ui.shadow(panel, Color(0, 0, 0, 0.32f), 0, 18, 40, -8);
    ui.blurPanel(panel, Color(0.035f, 0.04f, 0.045f, 0.88f), 14);
    ui.pushClip(panel);

    // --- barra lateral de categorias
    struct Cat { const char* label, *desc; int icon; };
    static const Cat cats[2] = {
        {"GRÁFICOS", "Qualidade visual, resolução e desempenho.", 0},
        {"ÁUDIO", "Volume do motor, dos pneus e do ambiente.", 1},
    };
    float sideW = L.narrow ? 158.f : 226.f, rightW = L.narrow ? 0.f : 270.f;
    float sy = panel.y + 22;
    for (int i = 0; i < 2; i++) {
      bool active = settingsCategory == i;
      Rect r{panel.x, sy, sideW, 46};
      bool hov = r.contains(mouseX, mouseY);
      if (active) {
        ui.rect(r, Color(1, 1, 1, 0.08f));
        ui.rect({panel.x, r.y, 3, r.h}, ACCENT);
      } else if (hov) {
        ui.rect(r, Color(1, 1, 1, 0.04f));
      }
      Color fg = active ? WHITE : hov ? Color(1, 1, 1, 0.85f) : Color(1, 1, 1, 0.45f);
      settingsIcon(cats[i].icon, panel.x + 27, r.y + r.h / 2, 20, fg);
      TextStyle ns{Font::BC600, 13, 13 * 0.18f, fg};
      ui.text(cats[i].label, panel.x + 52, r.y + r.h / 2 - 6.5f, ns, 13);
      if (!active) addHit(r, [this, i] { settingsCategory = i; settingsFocus = 0; });
      sy += 46 + 4;
    }
    ui.rect({panel.x + sideW, panel.y + 18, 1, panel.h - 36}, Color(1, 1, 1, 0.08f));

    // --- cabeçalho do conteúdo
    float x = panel.x + sideW + 32, right = panel.x + panel.w - 28 - (rightW > 0 ? rightW + 28 : 0);
    if (rightW > 0) ui.rect({right + 28, panel.y + 18, 1, panel.h - 36}, Color(1, 1, 1, 0.08f));
    ui.pushClip({panel.x + sideW + 1, panel.y, panel.w - sideW - 1, panel.h});
    float y = panel.y + 24 - settingsScroll;
    settingsContentTop = panel.y;
    TextStyle catTitle{Font::BC600, 20, 20 * 0.06f, WHITE};
    TextStyle catSub{Font::Barlow400, 13, 0, Color(1, 1, 1, 0.45f)};
    ui.text(cats[settingsCategory].label, x, y, catTitle, 20);
    y += 20 + 8;
    ui.text(cats[settingsCategory].desc, x, y, catSub, 16);
    y += 16 + 18;
    settingsDivider(x, right, y);

    // --- linhas de cada categoria
    if (settingsCategory == 0) {
      const char* presetNames[4] = {"Baixo", "Médio", "Alto", "Ultra"};
      settingRow(x, right, y, "Preset de qualidade", settings.preset < 0 ? "Personalizada" : presetNames[settings.preset],
                 "Define automaticamente as melhores configurações gráficas para o seu computador.", [this](int d) {
                   settings.applyPreset(cycleIndex(settings.preset, d, 4));
                   changed(false);
                 });
      {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d%%", (int)std::lround(settings.pixelRatio * 100));
        settingRow(x, right, y, "Resolução de renderização", buf,
                   "Resolução interna em relação ao tamanho da janela. Valores menores melhoram o desempenho.", [this](int d) {
                     settings.pixelRatio = std::fmax(0.5f, std::fmin(2.f, settings.pixelRatio + d * 0.05f));
                     changed();
                   });
      }
      settingRow(x, right, y, "Modo de exibição", settings.fullscreen ? "Tela cheia" : "Janela",
                 "Alterna entre tela cheia e janela (tecla F11).", [this](int) {
                   settings.fullscreen = !settings.fullscreen;
                   changed(false);
                 });
      {
        const char* winNames[4] = {"1280 × 720", "1600 × 900", "1920 × 1080", "Maximizada"};
        int idx = windowSizeIndex();
        char buf[24];
        std::string val;
        if (idx >= 0) val = winNames[idx];
        else {
          std::snprintf(buf, sizeof buf, "%d × %d", settings.windowW, settings.windowH);
          val = buf;
        }
        settingRow(x, right, y, "Tamanho da janela", val,
                   "Tamanho da janela fora da tela cheia. A borda também pode ser arrastada com o mouse.",
                   [this](int d) { setWindowSize(cycleIndex(windowSizeIndex(), d, 4)); }, !settings.fullscreen);
      }
      settingRow(x, right, y, "Sincronia vertical", settings.vsync ? "Ligada" : "Desligada",
                 "Sincroniza os quadros com a tela para evitar o corte horizontal da imagem (tearing).",
                 [this](int) { settings.vsync = !settings.vsync; changed(false); });
      settingsDivider(x, right, y);
      {
        const char* aaNames[3] = {"FXAA", "MSAA 2×", "MSAA 4×"};
        int sel = settings.msaa == 0 ? 0 : settings.msaa == 2 ? 1 : 2;
        settingRow(x, right, y, "Anti-serrilhado", aaNames[sel],
                   "Suaviza as bordas serrilhadas. O MSAA tem mais qualidade, mas custa mais desempenho.", [this](int d) {
                     int sel2 = cycleIndex(settings.msaa == 0 ? 0 : settings.msaa == 2 ? 1 : 2, d, 3);
                     settings.msaa = sel2 == 0 ? 0 : sel2 == 1 ? 2 : 4;
                     changed();
                   });
      }
      {
        const char* aoNames[3] = {"Desligada", "Normal", "Alta"};
        settingRow(x, right, y, "Oclusão de ambiente", aoNames[settings.ao],
                   "Sombreamento de contato entre superfícies próximas, para mais profundidade.",
                   [this](int d) { settings.ao = cycleIndex(settings.ao, d, 3); changed(); });
      }
      {
        const char* shNames[3] = {"Baixa", "Média", "Alta"};
        int sel = settings.shadow == 1024 ? 0 : settings.shadow == 2048 ? 1 : 2;
        settingRow(x, right, y, "Qualidade das sombras", shNames[sel], "Resolução do mapa de sombra. Mais alta = sombras mais definidas, principalmente com alcance grande.", [this, sel](int d) {
          int sel2 = cycleIndex(sel, d, 3);
          settings.shadow = sel2 == 0 ? 1024 : sel2 == 1 ? 2048 : 4096;
          changed();
        });
      }
      {
        static const int dists[5] = {20, 40, 60, 100, 150};
        int sel = 2;
        for (int i = 0; i < 5; i++)
          if (settings.shadowDist == dists[i]) sel = i;
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d m", settings.shadowDist);
        settingRow(x, right, y, "Distância das sombras", buf,
                   "Até onde as sombras aparecem em volta do carro. Mais longe deixa a pista mais realista, mas cada texel "
                   "da sombra fica maior (use qualidade Alta junto).",
                   [this, sel](int d) {
                     settings.shadowDist = dists[std::max(0, std::min(4, sel + d))];
                     changed(false);
                   });
      }
      {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%.1f×", settings.shadowSoft);
        settingRow(x, right, y, "Suavidade das sombras", buf,
                   "Borda das sombras: valores baixos deixam a sombra nítida; altos, mais suave e difusa (como em fim de tarde).",
                   [this](int d) {
                     settings.shadowSoft = std::fmax(0.5f, std::fmin(3.f, std::round((settings.shadowSoft + d * 0.25f) * 4) / 4));
                     changed(false);
                   });
      }
      settingRow(x, right, y, "Brilho (bloom)", settings.bloom ? "Ligado" : "Desligado", "Brilho ao redor das luzes e dos reflexos fortes.",
                 [this](int) { settings.bloom = !settings.bloom; changed(); });
      settingRow(x, right, y, "Efeitos de lente", settings.lensFx ? "Ligado" : "Desligado",
                 "Vinheta, grão de filme e aberração cromática nas bordas da tela.",
                 [this](int) { settings.lensFx = !settings.lensFx; changed(false); });
      settingRow(x, right, y, "Desfoque de velocidade", settings.speedBlur ? "Ligado" : "Desligado",
                 "Desfoque radial nas bordas da tela em alta velocidade.",
                 [this](int) { settings.speedBlur = !settings.speedBlur; changed(false); });
      settingRow(x, right, y, "Qualidade automática", settings.autoQuality ? "Ligada" : "Desligada",
                 "Reduz a qualidade gráfica sozinha se os quadros por segundo ficarem baixos.",
                 [this](int) { settings.autoQuality = !settings.autoQuality; changed(false); });
    } else {
      char buf[16];
      std::snprintf(buf, sizeof buf, "%d%%", (int)std::lround(settings.volume * 100));
      settingRow(x, right, y, "Volume", buf, "Volume geral do motor, dos pneus e do ambiente.", [this](int d) {
        settings.volume = std::fmax(0.f, std::fmin(1.f, settings.volume + d * 0.05f));
        changed(false);
      });
    }
    settingsContentH[settingsCategory] = y - panel.y + 24;
    ui.popClip();

    // --- painel de descrição à direita (linha com foco ou sob o mouse)
    if (rightW > 0 && !rowCtls.empty()) {
      int active = -1;
      for (size_t i = 0; i < rowCtls.size(); i++)
        if (rowCtls[i].hovered) { active = (int)i; break; }
      if (active < 0 && focusVisible && settingsFocus < (int)rowCtls.size()) active = settingsFocus;
      if (active < 0) active = 0;
      const RowCtl& r = rowCtls[active];
      float rx = right + 56, ry = panel.y + 30, rw = rightW - 28;
      settingsIcon(cats[settingsCategory].icon, rx + 18, ry + 18, 36, Color(1, 1, 1, 0.55f));
      ry += 54;
      TextStyle rh{Font::BC600, 15, 15 * 0.1f, WHITE};
      ui.text(upper(r.label), rx, ry, rh, 15);
      ry += 15 + 12;
      TextStyle rd{Font::Barlow400, 13, 0, Color(1, 1, 1, 0.55f)};
      float rdlh = ui.lineHeightNormal(rd);
      std::string cur, word;
      auto flush = [&] {
        if (word.empty()) return;
        std::string cand = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && ui.measure(cand, rd) > rw) {
          ui.text(cur, rx, ry, rd, rdlh);
          ry += rdlh;
          cur = word;
        } else {
          cur = cand;
        }
        word.clear();
      };
      for (char c : r.desc) {
        if (c == ' ') flush();
        else word += c;
      }
      flush();
      if (!cur.empty()) ui.text(cur, rx, ry, rd, rdlh);
    }

    // mantém a linha em foco visível
    if (focusVisible && settingsFocus < (int)rowCtls.size()) {
      float fy = rowCtls[settingsFocus].y, fh = rowCtls[settingsFocus].h;
      if (fy - settingsScroll < 0) settingsScroll = std::max(0.f, fy);
      if (fy + fh - settingsScroll > panel.h) settingsScroll = fy + fh - panel.h;
    }
    // barra de rolagem quando o conteúdo não cabe
    if (settingsContentH[settingsCategory] > panel.h + 1) {
      float ch = settingsContentH[settingsCategory];
      float bh = panel.h * panel.h / ch, by = panel.y + (panel.h - bh) * settingsScroll / (ch - panel.h);
      ui.rect({panel.x + panel.w - 6, by + 6, 3, bh - 12}, Color(1, 1, 1, 0.18f), 1.5f);
    }
    ui.popClip();

    // --- rodapé fixo: Voltar e Restaurar padrão (teclado ou controle)
    {
      PadStyle ps = promptStyle();
      TextStyle hs{Font::Barlow400, 13, 0, Color(1, 1, 1, 0.5f)};
      float fy = panel.y + panel.h + bottomReserve / 2 + 2;
      float hx = panel.x + 6;
      hx += drawPrompt(ui, ps, PadButton::Back, hx, fy, 22.f, "Esc", {true}) + 8;
      hx += ui.text("Voltar", hx, fy - 8, hs, 16) + 28;
      float resetX0 = hx;
      hx += drawPrompt(ui, ps, PadButton::North, hx, fy, 22.f, "R", {true}) + 8;
      hx += ui.text("Restaurar padrão", hx, fy - 8, hs, 16);
      addHit({resetX0 - 6, fy - 16, hx - resetX0 + 12, 32}, [this] { resetSettings(); });
    }
    ui.popOpacity();
  }

  // anel de carregamento: trilho fino + arco que preenche a partir do topo (12h), sentido horário
  void drawProgressRing(float cx, float cy, float r, float width, float t, const Color& track, const Color& fill) {
    ui.ring(cx, cy, r, width, track);
    t = std::clamp(t, 0.f, 1.f);
    if (t <= 0.002f) return;
    float a0 = -(float)M_PI / 2, a1 = a0 + t * 2 * (float)M_PI;
    int n = std::max(1, (int)std::ceil(72 * t));
    vec2 prev{cx + std::cos(a0) * r, cy + std::sin(a0) * r};
    for (int i = 1; i <= n; i++) {
      float a = a0 + (a1 - a0) * i / n;
      vec2 p{cx + std::cos(a) * r, cy + std::sin(a) * r};
      ui.line(prev.x, prev.y, p.x, p.y, width, fill);
      prev = p;
    }
  }

  void drawFade(float dt) {
    fade.update(dt);
    fadeBar.update(dt);
    boot.update(dt);
    float W = ui.width(), H = ui.height();
    // tela preta de transição ao entrar/sair da pista: texto + ícone com o mesmo anel de
    // carregamento da abertura, preenchendo até completar (ou a mensagem de erro, sozinha)
    if (fade.value > 0.001f) {
      ui.pushOpacity(fade.value);
      ui.rect({0, 0, W, H}, Color::hex(0x0b0b0c));
      if (fadeError) {
        TextStyle st{Font::BC500, 13, 13 * 0.3f, Color(1, 0.55f, 0.52f, 0.9f)};
        float bw = std::min(420.f, W * 0.78f);
        std::string t = upper(fadeText);
        ui.text(t, (W - std::min(bw, ui.measure(t, st))) / 2, H / 2 - 6, st, 13);
      } else if (!fadeText.empty()) {
        TextStyle st{Font::BC500, 13, 13 * 0.3f, Color(1, 1, 1, 0.78f)};
        std::string t = upper(fadeText);
        float ringR = 40, ringW = 3, gap = 30;
        float top = H / 2 - (13 + gap + (ringR + ringW / 2) * 2) / 2;
        ui.text(t, (W - ui.measure(t, st)) / 2, top, st, 13);
        float cy = top + 13 + gap + ringR + ringW / 2;
        drawProgressRing(W / 2, cy, ringR, ringW, fadeBar.value, Color(1, 1, 1, 0.14f), WHITE);
        if (bootIconTex) {
          float iw = (ringR - ringW) * 1.1f, ih = iw * bootIconTex->height / bootIconTex->width;
          ui.imageCover(bootIconTex->id, bootIconTex->width, bootIconTex->height, {W / 2 - iw / 2, cy - ih / 2, iw, ih});
        }
      }
      ui.popOpacity();
    }
    // tela de abertura: logo em fade → texto de apresentação + ícone com anel de carregamento
    bootFrames++;
    bootLogoOpacity.update(dt);
    bootTextOpacity.update(dt);
    bootProgressShown += (bootProgress - bootProgressShown) * std::min(1.f, dt * 4);
    if (boot.value > 0.001f) {
      ui.pushOpacity(boot.value);
      ui.rect({0, 0, W, H}, Color::hex(0x0b0b0c));

      if (bootLogoOpacity.value > 0.001f && bootLogoTex) {
        ui.pushOpacity(bootLogoOpacity.value);
        float lw = std::min(460.f, W * 0.52f), lh = lw * bootLogoTex->height / bootLogoTex->width;
        ui.imageCover(bootLogoTex->id, bootLogoTex->width, bootLogoTex->height, {(W - lw) / 2, (H - lh) / 2, lw, lh});
        ui.popOpacity();
      }

      if (bootTextOpacity.value > 0.001f) {
        ui.pushOpacity(bootTextOpacity.value);
        TextStyle para{Font::Barlow400, 14, 14 * 0.02f, Color(1, 1, 1, 0.75f)};
        std::string msg = "Space Racer é um simulador de corrida totalmente gratuito e de código aberto, "
                           "com física realista e carros licenciados.";
        float maxW = std::min(440.f, W * 0.78f), lh = ui.lineHeightNormal(para);
        std::vector<std::string> lines;
        {
          std::string cur, word;
          auto push = [&] {
            if (word.empty()) return;
            std::string cand = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && ui.measure(cand, para) > maxW) { lines.push_back(cur); cur = word; }
            else cur = cand;
            word.clear();
          };
          for (char ch : msg) { if (ch == ' ') push(); else word += ch; }
          push();
          if (!cur.empty()) lines.push_back(cur);
        }
        float ringR = 40, ringW = 3, gap = 40;
        float textH = lines.size() * lh, ringD = (ringR + ringW / 2) * 2;
        float top = H / 2 - (textH + gap + ringD) / 2;
        for (size_t k = 0; k < lines.size(); k++)
          ui.text(lines[k], (W - ui.measure(lines[k], para)) / 2, top + k * lh, para, lh);
        float cy = top + textH + gap + ringR + ringW / 2;
        drawProgressRing(W / 2, cy, ringR, ringW, bootProgressShown, Color(1, 1, 1, 0.14f), WHITE);
        if (bootIconTex) {
          float iw = (ringR - ringW) * 1.1f, ih = iw * bootIconTex->height / bootIconTex->width;
          ui.imageCover(bootIconTex->id, bootIconTex->width, bootIconTex->height, {W / 2 - iw / 2, cy - ih / 2, iw, ih});
        }
        ui.popOpacity();
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
    std::string label, desc;  // para o painel de descrição à direita
    bool hovered = false;
  };
  std::vector<RowCtl> rowCtls;
  int settingsFocus = 0;
  int settingsCategory = 0;  // 0 = Gráficos, 1 = Áudio
  float settingsContentH[2] = {0, 0}, settingsContentTop = 0;
  bool focusVisible = false;
  Transition tabAnim[2] = {Transition(1, 0.2f), Transition(0, 0.2f)};
  Transition tabT{0, 0.35f};
  std::vector<Hit> hits;
  std::function<void(float)> dragFn;
  Rect settingsPanel;
  float settingsScroll = 0;
  // lista de carros: rolagem atual, alvo, máximo e arrasto com o mouse
  float stripScroll = 0, stripTarget = 0, stripMax = 0;
  bool stripPress = false, stripDragged = false;
  float stripPressX = 0, stripPressScroll = 0;
  int fbW = 1, fbH = 1, cssW = 1, cssH = 1;
  float uiScale = 1, uiW = 1, uiH = 1;  // escala e tamanho da interface (em unidades de 1600×900)
  float dpr = 1;
  float mouseX = -1, mouseY = -1;
  int pressTarget = -100;
  bool swallowClick = false, cursorPointer = false;
  std::vector<std::pair<float, std::function<void()>>> timers;

  std::vector<Card> cards;
  Button play, cineBtn, raceBtn, backBtn;
  std::vector<MapCard> mapCards;
  int selectedMap = 0;
  TexturePtr skyThumb[2];  // miniaturas do céu de dia e do pôr do sol
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
  bool fadeError = false;
  bool waitingPrepare = false;
  int bootFrames = 0;
  bool introCinematic = true;
  bool hideHud = false;  // roteiro de teste: fotos sem interface  // ao abrir o jogo, começa pela cinemática

  // ---------------------------------------------------------------- abertura: tela preta com a
  // logo (entra e sai em fade) → texto de apresentação + ícone com anel de carregamento → funde
  // para a cinemática, já com o carro carregado por trás.
  enum class BootStage { Logo, Text, Done };
  BootStage bootStage = BootStage::Logo;
  TexturePtr bootLogoTex, bootIconTex;
  Transition bootLogoOpacity{0, 0.9f}, bootTextOpacity{0, 0.7f};
  bool bootTextMinTimeDone = false, bootAssetsReady = false;
  float bootProgress = 0, bootProgressShown = 0;
};

int main(int argc, char** argv) {
  (void)argc;
  initAssetRoot(argv[0]);
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  // sem placa/driver de som o jogo continua, só que mudo
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    std::fprintf(stderr, "[audio] som desativado: %s\n", SDL_GetError());
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  // abre no modo da última sessão: janela com o tamanho/posição salvos (ou maximizada);
  // a tela cheia é ligada logo depois, em App::applySettings
  Settings saved;
  saved.load();
  SDL_WindowFlags wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (saved.windowMax) wflags |= SDL_WINDOW_MAXIMIZED;
  SDL_Window* window = SDL_CreateWindow("Space Racer", saved.windowW, saved.windowH, wflags);
  if (!window) {
    std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetWindowMinimumSize(window, MIN_WINDOW_W, MIN_WINDOW_H);
  if (saved.windowX >= 0 && saved.windowY >= 0 && !saved.windowMax) SDL_SetWindowPosition(window, saved.windowX, saved.windowY);
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
      double raw = (now - last) / (double)SDL_GetPerformanceFrequency();
      last = now;
      // com VSync a tela mostra um quadro a cada 1/Hz exato, mas o relógio do processador oscila
      // alguns ms em volta disso; avançar o jogo pelo tempo "ruidoso" faz a paisagem andar
      // desigual nas curvas (céu e chão tremendo). Encaixa no múltiplo do período da tela.
      int swap = 0;
      SDL_GL_GetSwapInterval(&swap);
      if (swap != 0) {
        const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
        if (dm && dm->refresh_rate > 20) {
          double period = 1.0 / dm->refresh_rate;
          double n = std::round(raw / period);
          if (n >= 1 && std::fabs(raw - n * period) < period * 0.3) raw = n * period;
        }
      }
      float dt = std::min(0.05f, (float)raw);
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
