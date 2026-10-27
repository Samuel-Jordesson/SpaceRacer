// Space Racer (versão nativa em C++): garagem (tela inicial) → escolher carro → pilotar →
// Esc volta para a garagem.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <memory>
#include <thread>

#include "core/assets.h"
#include "game/car_model.h"
#include "game/cars.h"
#include "game/cinematic.h"
#include "game/workshop.h"
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
    // logo do cabeçalho: escura na garagem (fundo claro), branca nas configurações
    for (int i = 0; i < 2; i++) {
      headerLogo[i] = loadImage(i == 0 ? "assets/ui/logo-dark.png" : "assets/ui/logo-white.png", false);
      if (headerLogo[i]) headerLogo[i]->upload();
    }
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
        if (mode == Mode::Garage && shopPage && pickerTarget >= 0) {
          if (sc == SDL_SCANCODE_ESCAPE && !e.key.repeat) pickerTarget = -1;
          if (sc == SDL_SCANCODE_LEFT) pickerKey(-1, 0);
          if (sc == SDL_SCANCODE_RIGHT) pickerKey(1, 0);
          if (sc == SDL_SCANCODE_UP) pickerKey(0, -1);
          if (sc == SDL_SCANCODE_DOWN) pickerKey(0, 1);
          if ((sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_SPACE) && !e.key.repeat) pickerApply(pickerSel);
          break;
        }
        if (mode == Mode::Garage && shopPage) {
          if (sc == SDL_SCANCODE_ESCAPE && !e.key.repeat) closeShop(false);
          if ((sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) && !e.key.repeat) closeShop(true);
          if (sc == SDL_SCANCODE_UP) moveFocus(-1);
          if (sc == SDL_SCANCODE_DOWN) moveFocus(1);
          if (sc == SDL_SCANCODE_LEFT) adjustFocus(-1);
          if (sc == SDL_SCANCODE_RIGHT) adjustFocus(1);
          if (sc == SDL_SCANCODE_SPACE && !e.key.repeat) activateFocus();
          if (sc == SDL_SCANCODE_TAB && !e.key.repeat) setShopTab(shopTab + ((SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1));
          if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_5 && !e.key.repeat) setShopTab(sc - SDL_SCANCODE_1);
          break;
        }
        if (mode == Mode::Garage && mapPage && raceSetup) {
          if ((sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_C) && !e.key.repeat)
            raceSetup = false;
          if (sc == SDL_SCANCODE_UP) moveFocus(-1);
          if (sc == SDL_SCANCODE_DOWN) moveFocus(1);
          if (sc == SDL_SCANCODE_LEFT) adjustFocus(-1);
          if (sc == SDL_SCANCODE_RIGHT) adjustFocus(1);
          if (sc == SDL_SCANCODE_SPACE && !e.key.repeat) activateFocus();
          break;
        }
        if (mode == Mode::Garage && !e.key.repeat && mapPage) {
          if (sc == SDL_SCANCODE_ESCAPE) setMapPage(false);
          if (sc == SDL_SCANCODE_C) openRaceSetup();
          if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) startPlay();
          if (sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_LEFT) {
            int n = (int)mapCards.size();
            selectedMap = ((selectedMap + (sc == SDL_SCANCODE_RIGHT ? 1 : -1)) % n + n) % n;
          }
          break;
        }
        if (mode == Mode::Garage && !e.key.repeat) {
          if (sc == SDL_SCANCODE_ESCAPE) setTab(true);  // "Menu"
          if (sc == SDL_SCANCODE_O) openShop();
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
        else if (stickerDrag) placeSticker(mouseX, mouseY);
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
          // oficina, aba adesivo: clicar no carro arrasta o adesivo; fora dele gira a câmera
          if (pressTarget == -100 && e.button.button == 1 && shopPage && shopTab == 3 && pickerTarget < 0 && stickerSel >= 0 &&
              placeSticker(x, y)) {
            stickerDrag = true;
            pressTarget = -101;
          }
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
        stickerDrag = false;
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
        else if (mode == Mode::Garage && (settingsTab || shopPage) && settingsPanel.contains(mouseX, mouseY)) settingsScroll += dy * 0.4f;
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
      else if (op == "settings") setTab(true);
      else if (op == "shop") openShop();
      else if (op == "shoptab") setShopTab(std::stoi(arg));
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
    applyLivery(*loaded, loadShop(cards[i].car->id));  // pintura salva na oficina
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
      game.race = {mp.flat ? 0 : settings.raceMode, settings.raceLaps, settings.raceOpponents};
      if (selectedCard >= 0) game.playerCarId = cards[selectedCard].car->id;
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
    // desenhada numa referência de 1672×941 (mesma escala da tela de configurações)
    Layout L;
    float W = uiW;
    L.narrow = false;
    L.pad = sX(70);
    L.infoX = sX(70);
    L.infoY = sY(118);
    L.infoW = sX(322);
    L.showRatings = true;
    L.showFigures = true;
    play.rect = {sX(1343), sY(836), sX(278), sY(59)};
    float backW = sX(180);
    cineBtn.rect = {play.rect.x - sX(12) - backW, play.rect.y, backW, play.rect.h};  // VOLTAR da escolha de pista
    L.strip = {sX(104), sY(664), W - sX(104) * 2, sY(144)};
    float cw = sX(252), ch = sY(116), gap = sX(10);
    float contentW = cards.empty() ? 0 : cards.size() * (cw + gap) - gap + sX(16);
    stripMax = std::max(0.f, contentW - L.strip.w);
    stripTarget = std::clamp(stripTarget, 0.f, stripMax);
    stripScroll = std::clamp(stripScroll, 0.f, stripMax);
    for (size_t i = 0; i < cards.size(); i++)
      cards[i].rect = {L.strip.x + sX(8) + i * (cw + gap) - stripScroll, L.strip.y + (L.strip.h - ch) / 2, cw, ch};
    L.cardW = cw;
    L.headY = L.strip.y - sY(18);
    return L;
  }

  // -100 = nada (canvas); 0..n-1 = card; -1 = Pilotar; -2 = Cinematic
  int hitTest(float x, float y) {
    if (garageUi.to < 0.5f) return -100;
    for (size_t i = 0; i < hits.size(); i++)
      if (hits[i].r.contains(x, y)) return -1000 - (int)i;
    if (settingsTab || shopPage) return settingsPanel.contains(x, y) ? -3 : -100;
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

    // --- cabeçalho: marca | GARAGEM | CONFIGURAÇÕES
    {
      float x = sX(55), cy = sY(46);
      float w = drawHeaderLogo(0, x, cy);
      float dx = x + w + sX(22);
      ui.rect({dx, cy - sY(14), 1, sY(28)}, LINE.withAlpha(2.2f));
      TextStyle g{Font::Barlow500, sK(19), sK(2.6f), INK};
      float gx = dx + sX(22);
      float gw = ui.text(mapPage ? "SELECIONAR PISTA" : "GARAGEM", gx, cy - ui.lineHeightNormal(g) / 2, g);
      float d2 = gx + gw + sX(24);
      ui.rect({d2, cy - sY(14), 1, sY(28)}, LINE.withAlpha(mapPage ? 1.1f : 2.2f));
      if (shopPage) {
        // abas da oficina: a ativa sublinhada
        const char* tabsN[5] = {"MOTOR", "SUSPENSÃO", "PINTURA", "ADESIVO", "ARO"};
        float tx = d2 + sX(56);
        PadStyle ps0 = promptStyle();
        if (ps0 != PadStyle::Keyboard) tx += drawPrompt(ui, ps0, PadButton::LB, tx - sX(30), cy, sK(18), "") - sX(10);
        for (int t = 0; t < 5; t++) {
          TextStyle ts{t == shopTab ? Font::Barlow600 : Font::Barlow500, sK(13.5f), sK(1.6f), t == shopTab ? INK : INK2};
          float tw = ui.measure(tabsN[t], ts);
          Rect tr{tx - sX(10), cy - sY(18), tw + sX(20), sY(40)};
          bool th = tr.contains(mouseX, mouseY);
          if (th && t != shopTab) ts.color = INK;
          ui.text(tabsN[t], tx, cy - ui.lineHeightNormal(ts) / 2, ts);
          if (t == shopTab) ui.rect({tx, cy + sY(16), tw - sK(1.6f), 2.5f}, INK);
          if (t != shopTab) addHit(tr, [this, t] { setShopTab(t); });
          tx += tw + sX(40);
        }
        if (ps0 != PadStyle::Keyboard) drawPrompt(ui, ps0, PadButton::RB, tx - sX(30), cy, sK(18), "");
      }
      TextStyle c{Font::Barlow400, sK(14.5f), sK(2.2f), INK2};
      const char* sub = mapPage ? "" : "CONFIGURAÇÕES";
      if (shopPage) sub = "";
      float cx = d2 + sX(26);
      float cw = ui.measure(sub, c);
      Rect cr{cx - sX(8), cy - sY(16), cw + sX(16), sY(32)};
      bool hov = !mapPage && !shopPage && mode == Mode::Garage && cr.contains(mouseX, mouseY);
      c.color = hov ? INK : INK2;
      ui.text(sub, cx, cy - ui.lineHeightNormal(c) / 2, c);
      if (hov) ui.rect({cx, cy + sY(13), cw - sK(2.2f), 1}, INK);
      if (!mapPage && !shopPage && mode == Mode::Garage) addHit(cr, [this] { setTab(true); });
      PadStyle ps = promptStyle();
      if (ps != PadStyle::Keyboard && !mapPage && !shopPage) drawPrompt(ui, ps, PadButton::Start, cx + cw + sX(14), cy, sK(20), "");
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
    shopT.update(dt);
    garage.shopView = shopPage;
    if (shopT.value > 0.001f) drawShop(dt, L);
    if (shopT.value > 0.999f) {
      ui.popOpacity();
      ui.popOpacity();
      ui.popOpacity();
      return;
    }
    ui.pushOpacity(1 - shopT.value);

    // "Escolha um carro"
    if (emptyOpacity.value > 0.001f) {
      ui.pushOpacity(emptyOpacity.value);
      TextStyle st{Font::Barlow500, sK(14), sK(4), INK3};
      std::string sm = "ESCOLHA UM CARRO";
      ui.text(sm, (W - ui.measure(sm, st)) / 2, H / 2 - sK(7), st);
      ui.popOpacity();
    }

    // --- ficha do carro (entra deslizando)
    if (infoCar) {
      float k = INFO_EASE(std::min(1.f, infoT / 0.7f));
      ui.pushOpacity(k);
      float x = L.infoX - sX(16) * (1 - k), y = L.infoY;
      auto lg = logos.find(infoCar->id);
      if (lg != logos.end()) {
        const Texture& t = *lg->second;
        float ar = (float)t.width / t.height;
        float lh = sY(62), lw = std::min(sX(90), lh * ar);
        lh = lw / ar;
        ui.imageCover(t.id, t.width, t.height, {x + sX(22), y, lw, lh});
        y += lh + sY(20);
      } else {
        y += sY(82);
      }
      TextStyle mk{Font::Barlow500, sK(13.5f), sK(3.4f), INK};
      ui.text(upper(infoCar->maker), x, y, mk);
      y += sY(24);
      TextStyle nm{Font::Barlow400, sK(33), sK(1.8f), INK};
      ui.text(upper(infoCar->name), x, y, nm);
      y += sY(40);
      TextStyle yr{Font::Barlow300, sK(21), sK(1.2f), INK};
      ui.text(std::to_string(infoCar->year), x, y, yr);
      y += sY(44);
      // etiqueta da tração + bandeira do país da marca
      {
        std::string drive = "RWD";
        for (auto& f : infoCar->figures)
          if (f.first == "Tração") drive = f.second == "Dianteira" ? "FWD" : f.second == "Integral" ? "AWD" : "RWD";
        TextStyle tg{Font::Barlow500, sK(13.5f), sK(0.8f), INK};
        float tw = ui.measure(drive, tg) + sX(18), th = sY(25);
        ui.border({x, y, tw, th}, INK, 1.2f, sK(2));
        ui.text(drive, x + sX(9), y + th / 2 - ui.lineHeightNormal(tg) / 2, tg);
        float fx = x + tw + sX(12);
        ui.rect({fx, y + sY(3), 1, th - sY(6)}, LINE.withAlpha(2.f));
        fx += sX(12);
        Rect fl{fx, y + sY(2), sX(30), th - sY(4)};
        const std::string& m = infoCar->maker;
        if (m == "Ferrari" || m == "Lamborghini") {
          ui.rect({fl.x, fl.y, fl.w / 3, fl.h}, Color::hex(0x009246));
          ui.rect({fl.x + fl.w / 3, fl.y, fl.w / 3, fl.h}, Color::hex(0xf1f2f1));
          ui.rect({fl.x + fl.w * 2 / 3, fl.y, fl.w / 3, fl.h}, Color::hex(0xce2b37));
        } else if (m == "Honda") {
          ui.rect(fl, Color::hex(0xf7f7f7));
          ui.border(fl, LINE.withAlpha(2.f), 1);
          ui.circle(fl.x + fl.w / 2, fl.y + fl.h / 2, fl.h * 0.3f, Color::hex(0xbc002d));
        } else {  // Porsche, BMW: Alemanha
          ui.rect({fl.x, fl.y, fl.w, fl.h / 3}, Color::hex(0x111111));
          ui.rect({fl.x, fl.y + fl.h / 3, fl.w, fl.h / 3}, Color::hex(0xdd0000));
          ui.rect({fl.x, fl.y + fl.h * 2 / 3, fl.w, fl.h / 3}, Color::hex(0xffce00));
        }
        y += th + sY(12);
      }
      // tabela de números
      {
        struct Row { const char* label; const char* key; };
        static const Row rows[4] = {{"VELOCIDADE MÁXIMA", "Velocidade máx."}, {"ACELERAÇÃO (0–100)", "0–100 km/h"},
                                    {"POTÊNCIA", "Potência"}, {"PESO", "Peso"}};
        TextStyle rl{Font::Barlow400, sK(12.5f), sK(0.5f), INK};
        TextStyle rv{Font::Barlow400, sK(13.5f), 0, INK, true};
        float rh = sY(45);
        ui.rect({x, y, L.infoW, 1}, LINE.withAlpha(1.6f));
        for (auto& r : rows) {
          std::string v = "—";
          for (auto& f : infoCar->figures)
            if (f.first == r.key) v = f.second;
          ui.text(r.label, x, y + rh / 2 - ui.lineHeightNormal(rl) / 2, rl);
          ui.text(v, x + L.infoW - ui.measure(v, rv), y + rh / 2 - ui.lineHeightNormal(rv) / 2 + sY(2), rv);
          y += rh;
          ui.rect({x, y, L.infoW, 1}, LINE.withAlpha(1.6f));
        }
      }
      ui.popOpacity();
    }

    // --- faixa de carros (a rolagem desliza suave até o alvo)
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
      // o escolhido cresce um pouco e ganha borda preta
      float g = c.selected.value * sK(7);
      Rect r{c.rect.x - g, c.rect.y - g, c.rect.w + 2 * g, c.rect.h + 2 * g};
      r.y -= c.hover.value * sY(2);
      ui.shadow(r, Color(0, 0, 0, 0.06f + 0.08f * std::max(c.hover.value, c.selected.value)), 0, 8, 22, -10);
      ui.rect(r, Color(1, 1, 1, 0.97f));
      float lab = sY(34) + g * 0.5f;
      ui.pushClip(r);
      if (c.thumb) ui.imageCover(c.thumb->id, c.thumb->width, c.thumb->height, {r.x, r.y, r.w, r.h - lab + sY(4)}, 0.5f, 0.58f);
      float ly = r.y + r.h - lab / 2 - sY(4), lx = r.x + sX(12);
      auto lg = logos.find(c.car->id);
      if (lg != logos.end()) {
        const Texture& t = *lg->second;
        float ar = (float)t.width / t.height, lh = sY(20), lw = std::min(sX(22), lh * ar);
        lh = lw / ar;
        ui.imageCover(t.id, t.width, t.height, {lx, ly - lh / 2, lw, lh});
        lx += sX(30);
      }
      TextStyle nm{Font::Barlow400, sK(12.5f + 1.5f * c.selected.value), 0, INK};
      std::string label = c.car->maker + " " + c.car->name;
      if (c.selected.value > 0.5f) label = c.car->name;
      if (c.loading) label += "  ·  carregando";
      ui.text(label, lx, ly - ui.lineHeightNormal(nm) / 2, nm);
      if (c.progressOpacity.value > 0.001f) {
        ui.pushOpacity(c.progressOpacity.value);
        ui.rect({r.x, r.y + r.h - 2, r.w * c.progress, 2}, ACCENT);
        ui.popOpacity();
      }
      ui.popClip();
      if (c.selected.value > 0.001f) ui.border(r, INK.withAlpha(c.selected.value), 2);
    };
    const float fadeW = sX(60);
    float fadeR = fadeW * std::clamp((stripMax - stripScroll) / 40, 0.f, 1.f);
    float fadeL = fadeW * std::clamp(stripScroll / 40, 0.f, 1.f);
    float top = L.strip.y - 10, hgt = L.strip.h + 20;
    auto drawSlice = [&](float x0, float x1, float alpha) {
      if (x1 <= x0 || alpha <= 0.003f) return;
      ui.pushClip({x0, top, x1 - x0, hgt});
      ui.pushOpacity(alpha);
      // o escolhido por último (fica por cima dos vizinhos)
      for (auto& c : cards)
        if (!c.isSelected && c.rect.x < x1 && c.rect.x + c.rect.w > x0) drawCard(c);
      for (auto& c : cards)
        if (c.isSelected && c.rect.x < x1 + 20 && c.rect.x + c.rect.w > x0 - 20) drawCard(c);
      ui.popOpacity();
      ui.popClip();
    };
    drawSlice(L.strip.x + fadeL, L.strip.x + L.strip.w - fadeR, 1);
    const int N = 16;
    for (int kk = 0; kk < N; kk++) {
      float t = (kk + 0.5f) / N, ease = std::sqrt(t);
      if (fadeL > 0) drawSlice(L.strip.x + fadeL * kk / N, L.strip.x + fadeL * (kk + 1) / N, ease);
      if (fadeR > 0) drawSlice(L.strip.x + L.strip.w - fadeR * (kk + 1) / N, L.strip.x + L.strip.w - fadeR * kk / N, ease);
    }
    // setas da faixa: carro anterior / próximo
    {
      float acy = L.strip.y + L.strip.h / 2;
      int n = (int)cards.size(), cur = selectedCard < 0 ? 0 : selectedCard;
      for (int side = 0; side < 2; side++) {
        bool rgt = side == 1;
        float ax = rgt ? W - sX(62) : sX(60);
        Rect ar{ax - sX(24), acy - sY(30), sX(48), sY(60)};
        bool ah = ar.contains(mouseX, mouseY);
        chevron(ax, acy, sK(10), rgt, INK.withAlpha(ah ? 1.f : 0.75f));
        if (n > 0) addHit(ar, [this, rgt, cur, n] { selectCar(((cur + (rgt ? 1 : -1)) % n + n) % n); });
      }
    }

    // --- rodapé: dicas (teclado ou controle)
    {
      PadStyle ps = promptStyle();
      TextStyle hs{Font::Barlow400, sK(15.5f), 0, INK};
      float fy = play.rect.y + play.rect.h / 2 + sY(8), fx = sX(54);
      auto hint = [&](PadButton b, const char* key, const char* text) {
        fx += drawPrompt(ui, ps, b, fx, fy, sK(24), key) + sX(12);
        fx += ui.text(text, fx, fy - ui.lineHeightNormal(hs) / 2, hs) + sX(46);
      };
      hint(PadButton::DPadH, "← →", "Trocar carro");
      hint(PadButton::Confirm, "Enter", "Selecionar");
      hint(PadButton::Start, "Esc", "Menu");
      // Oficina: mesma aparência das outras dicas, mas clicável
      float ox = fx;
      hint(PadButton::West, "O", "Oficina");
      if (!play.disabled && mode == Mode::Garage && !shopPage) addHit({ox - 6, fy - sY(20), fx - sX(46) - ox + 12, sY(40)}, [this] { openShop(); });
    }

    // --- IR PARA A PISTA
    {
      play.opacity.set(play.disabled ? 0.25f : 1.f);
      play.hover.set(hover == -1 && !play.disabled ? 1.f : 0.f);
      play.opacity.update(dt);
      play.hover.update(dt);
      Rect r = play.rect;
      ui.pushOpacity(play.opacity.value);
      ui.rect(r, mix(Color::hex(0x16181b), ACCENT, play.hover.value * 0.85f));
      float cy = r.y + r.h / 2;
      // bandeira quadriculada
      float fx = r.x + sX(50), fw = sX(30), fh = sY(20), fy0 = cy - fh / 2;
      ui.line(fx - sX(2), fy0 - sY(2), fx - sX(6), fy0 + fh + sY(6), sK(2), WHITE);
      for (int yy = 0; yy < 4; yy++)
        for (int xx = 0; xx < 6; xx++) {
          float sk = (fh / 4) * 0.25f * (3 - yy) / 3;
          Rect q{fx + xx * fw / 6 - sk, fy0 + yy * fh / 4, fw / 6, fh / 4};
          ui.rect(q, (xx + yy) % 2 ? Color(1, 1, 1, 0.95f) : Color(1, 1, 1, 0.12f));
        }
      TextStyle st{Font::Barlow500, sK(15.5f), sK(1.0f), WHITE};
      ui.text("IR PARA A PISTA", r.x + sX(96), cy - ui.lineHeightNormal(st) / 2, st);
      chevron(r.x + r.w - sX(44), cy, sK(6), true, WHITE);
      ui.popOpacity();
    }
    ui.popOpacity();
    ui.popOpacity();
    ui.popOpacity();
    ui.popOpacity();
  }

  // ---------------------------------------------------------------- oficina
  const CarEntry* shopEntry() const { return selectedCard >= 0 ? cards[selectedCard].car : nullptr; }
  LoadedCar* shopLoaded() {
    const CarEntry* e = shopEntry();
    if (!e) return nullptr;
    auto it = cache.find(e->id);
    return it == cache.end() ? nullptr : it->second.get();
  }
  void openShop() {
    if (mode != Mode::Garage || settingsTab || mapPage || shopPage || play.disabled || !shopLoaded()) return;
    const CarEntry& e = *shopEntry();
    shop = shopSaved = loadShop(e.id);
    perfBase = estimatePerf(tunedSpec(*e.spec, CarTune{}), &shopLoaded()->geometry);
    perfNow = estimatePerf(tunedSpec(*e.spec, shop.tune), &shopLoaded()->geometry);
    perfDirty = false;
    decals = decalFiles();
    shopPage = true;
    shopT.set(1);
    pickerTarget = -1;
    stickerSel = -1;
    stickerDrag = false;
    settingsFocus = 0;
    settingsScroll = 0;
    focusVisible = false;
  }
  // sai da oficina; sem aplicar, volta ao que estava salvo
  void closeShop(bool apply) {
    if (!shopPage) return;
    const CarEntry* e = shopEntry();
    if (e) {
      if (apply) {
        saveShop(e->id, shop);
        shopSaved = shop;
      } else {
        shop = shopSaved;
      }
      if (LoadedCar* lc = shopLoaded()) applyLivery(*lc, shop);
    }
    shopPage = false;
    shopT.set(0);
    pickerTarget = -1;
    stickerDrag = false;
    garage.controls.autoRotate = true;
  }
  void setShopTab(int t) {
    shopTab = ((t % 5) + 5) % 5;
    stickerDrag = false;
    settingsFocus = 0;
    settingsScroll = 0;
  }
  void shopChanged(bool livery) {
    if (livery) {
      if (LoadedCar* lc = shopLoaded()) applyLivery(*lc, shop);
    } else {
      perfDirty = true;
      perfTimer = 0.2f;
    }
  }

  // pintura em edição (na original, cria uma para guardar adesivos/aros sem repintar)
  Livery& ensureLivery() {
    if (shop.livery < 0) {
      Livery l;
      l.name = "Pintura " + std::to_string(shop.liveries.size() + 1);
      shop.liveries.push_back(l);
      shop.livery = (int)shop.liveries.size() - 1;
    }
    return shop.liveries[shop.livery];
  }
  void openPicker(int target) {
    pickerTarget = target;
    pickerSel = 0;
    if (target == 2) {
      decals = decalFiles();
      for (auto& f : decals)
        if (!galleryTex.count(f)) {
          galleryTex[f] = loadImage("decals/" + f, true);
          if (galleryTex[f]) galleryTex[f]->upload();
        }
    }
  }
  int pickerCount() {
    int cols;
    return pickerTarget == 2 ? (int)decals.size() : (int)colorGrid(cols).size();
  }
  int pickerCols() { int c; colorGrid(c); return pickerTarget == 2 ? 5 : c; }
  void pickerApply(int i) {
    int cols;
    if (pickerTarget == 0 || pickerTarget == 1) {
      auto g = colorGrid(cols);
      if (i < 0 || i >= (int)g.size()) return;
      if (pickerTarget == 0) {
        if (shop.livery < 0) {
          ensureLivery();
          shop.liveries[shop.livery].keepArt = false;
        }
        Livery& l = shop.liveries[shop.livery];
        l.repaint = true;
        l.paint = g[i];
      } else {
        Livery& l = ensureLivery();
        l.rimCustom = true;
        l.rim = g[i];
      }
    } else if (pickerTarget == 2) {
      if (i >= 0 && i < (int)decals.size()) addSticker(decals[i]);
    }
    pickerTarget = -1;
    shopChanged(true);
  }
  void addSticker(const std::string& file) {
    LoadedCar* lc = shopLoaded();
    if (!lc || file.empty()) return;
    Livery& l = ensureLivery();
    if ((int)l.stickers.size() >= DecalSet::MAX) return;
    Sticker st;
    st.file = file;
    // começa na lateral esquerda, no meio do carro (projetado de fora para dentro)
    const Box3& b = lc->box;
    st.c = vec3(b.min.x, b.min.y + (b.max.y - b.min.y) * 0.45f, (b.min.z + b.max.z) / 2);
    st.n = vec3(1, 0, 0);
    st.up = vec3(0, 1, 0);
    l.stickers.push_back(st);
    stickerSel = (int)l.stickers.size() - 1;
    shopChanged(true);
  }
  void openFileDialog() {
    static const SDL_DialogFileFilter filters[] = {{"Imagens", "png;jpg;jpeg"}};
    SDL_ShowOpenFileDialog(
        [](void* ud, const char* const* list, int) {
          App* app = (App*)ud;
          if (!list || !list[0]) return;
          std::lock_guard<std::mutex> lk(app->importMutex);
          app->pendingImport = list[0];
        },
        this, window, filters, 1, nullptr, false);
  }

  // espaço do carro sem escala (o mesmo dos adesivos)
  mat4 carSpace(LoadedCar& lc) {
    vec3 p, sc;
    quat q;
    lc.root->matrixWorld.decompose(p, q, sc);
    return mat4::compose(p, q, vec3(1));
  }
  // raio da câmera da garagem pelo ponto da tela (unidades da interface)
  void screenRay(float mx, float my, vec3& o, vec3& d) {
    mat4 inv = (garage.camera.proj * garage.camera.view).inverse();
    float nx = mx / uiW * 2 - 1, ny = 1 - my / uiH * 2;
    vec4 a = inv * vec4(nx, ny, -1, 1), b = inv * vec4(nx, ny, 1, 1);
    o = vec3(a.x, a.y, a.z) / a.w;
    vec3 e = vec3(b.x, b.y, b.z) / b.w;
    d = normalize(e - o);
  }
  // cola o adesivo selecionado onde o raio da tela acerta o carro (como no Forza: projeta da câmera)
  bool placeSticker(float mx, float my, bool apply = true) {
    LoadedCar* lc = shopLoaded();
    if (!lc || shop.livery < 0 || stickerSel < 0 || stickerSel >= (int)shop.liveries[shop.livery].stickers.size()) return false;
    vec3 o, d;
    screenRay(mx, my, o, d);
    auto hits = raycast(*lc->root, o, d, 0.05f, 200);
    if (hits.empty()) return false;
    if (!apply) return true;
    mat4 inv = carSpace(*lc).inverse();
    Sticker& st = shop.liveries[shop.livery].stickers[stickerSel];
    st.c = inv.point(hits[0].point);
    st.n = normalize(inv.point(o + d) - inv.point(o));
    vec3 camUp = garage.camera.quaternion.rotate({0, 1, 0});
    st.up = normalize(inv.point(o + camUp) - inv.point(o));
    shopChanged(true);
    return true;
  }
  // posição do centro do adesivo na tela
  bool stickerScreen(vec2& out) {
    LoadedCar* lc = shopLoaded();
    if (!lc || shop.livery < 0 || stickerSel < 0 || stickerSel >= (int)shop.liveries[shop.livery].stickers.size()) return false;
    vec3 w = carSpace(*lc).point(shop.liveries[shop.livery].stickers[stickerSel].c);
    vec4 c = garage.camera.proj * garage.camera.view * vec4(w.x, w.y, w.z, 1);
    if (c.w <= 0) return false;
    out = vec2((c.x / c.w * 0.5f + 0.5f) * uiW, (0.5f - c.y / c.w * 0.5f) * uiH);
    return true;
  }
  // mover/girar/redimensionar com teclado (WASD, Q/E, Z/X) e controle (analógico direito, gatilhos)
  void stickerKeys(float dt) {
    if (pickerTarget >= 0 || shopTab != 3 || shop.livery < 0 || stickerSel < 0) return;
    const bool* k = SDL_GetKeyboardState(nullptr);
    float dx = (k[SDL_SCANCODE_D] ? 1.f : 0.f) - (k[SDL_SCANCODE_A] ? 1.f : 0.f);
    float dy = (k[SDL_SCANCODE_S] ? 1.f : 0.f) - (k[SDL_SCANCODE_W] ? 1.f : 0.f);
    float rot = (k[SDL_SCANCODE_E] ? 1.f : 0.f) - (k[SDL_SCANCODE_Q] ? 1.f : 0.f);
    float scl = (k[SDL_SCANCODE_X] ? 1.f : 0.f) - (k[SDL_SCANCODE_Z] ? 1.f : 0.f);
    if (SDL_Gamepad* pad = input.gamepad()) {
      auto ax = [&](SDL_GamepadAxis a) {
        float v = SDL_GetGamepadAxis(pad, a) / 32767.f;
        return std::fabs(v) < 0.18f ? 0.f : v;
      };
      dx += ax(SDL_GAMEPAD_AXIS_RIGHTX);
      dy += ax(SDL_GAMEPAD_AXIS_RIGHTY);
      rot += std::max(0.f, ax(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) - std::max(0.f, ax(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    }
    Sticker& st = shop.liveries[shop.livery].stickers[stickerSel];
    bool changedAny = false;
    if (rot != 0) { st.rot = std::fmod(st.rot + rot * 90 * dt + 360, 360.f); changedAny = true; }
    if (scl != 0) { st.size = std::clamp(st.size * std::exp(scl * dt), 0.1f, 3.f); changedAny = true; }
    if (dx != 0 || dy != 0) {
      vec2 sp;
      if (stickerScreen(sp)) placeSticker(sp.x + dx * 260 * dt, sp.y + dy * 260 * dt);
    }
    if (changedAny) shopChanged(true);
  }

  // grade de cores / galeria por cima da oficina
  void drawPicker() {
    if (pickerTarget < 0) return;
    int cols = pickerCols(), n = pickerCount();
    bool gallery = pickerTarget == 2;
    float cell = gallery ? sX(96) : sX(38), gap = gallery ? sX(10) : sX(4);
    int rows = std::max(1, (n + cols - 1) / cols);
    float pw = cols * cell + (cols - 1) * gap + sX(48), ph = sY(90) + rows * (cell * (gallery ? 0.8f : 1) + gap) + sY(70);
    Rect pr{sX(55), sY(250), pw, ph};
    ui.rect({0, 0, uiW, uiH}, Color(0, 0, 0, 0.18f));
    ui.shadow(pr, Color(0, 0, 0, 0.25f), 0, 18, 40, -10);
    ui.rect(pr, Color(1, 1, 1, 0.98f), sK(4));
    TextStyle hd{Font::Barlow500, sK(14), sK(2), INK};
    const char* title = pickerTarget == 0 ? "COR DA CARROCERIA" : pickerTarget == 1 ? "COR DO ARO" : "GALERIA DE ADESIVOS";
    ui.text(title, pr.x + sX(24), pr.y + sY(26), hd);
    float gx = pr.x + sX(24), gy = pr.y + sY(66);
    int cgc;
    auto grid = colorGrid(cgc);
    for (int i = 0; i < n; i++) {
      int cx = i % cols, cy = i / cols;
      float ch = gallery ? cell * 0.8f : cell;
      Rect r{gx + cx * (cell + gap), gy + cy * (ch + gap), cell, ch};
      bool hov = r.contains(mouseX, mouseY), sel = i == pickerSel;
      if (gallery) {
        ui.rect(r, Color(0.93f, 0.93f, 0.94f, 1), sK(3));
        auto it = galleryTex.find(decals[i]);
        if (it != galleryTex.end() && it->second) {
          const Texture& t = *it->second;
          float ar = (float)t.width / t.height, iw = std::min(r.w - 8, (r.h - 8) * ar), ih = iw / ar;
          ui.imageCover(t.id, t.width, t.height, {r.x + (r.w - iw) / 2, r.y + (r.h - ih) / 2, iw, ih});
        }
      } else {
        // a grade guarda cor linear; mostra em sRGB
        vec3 c = grid[i];
        auto srgb = [](float v) { return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f; };
        ui.rect(r, Color(srgb(c.x), srgb(c.y), srgb(c.z), 1), sK(2));
      }
      if (sel || hov) ui.border({r.x - 3, r.y - 3, r.w + 6, r.h + 6}, INK, sel ? 2.f : 1.f, sK(3));
      addHit(r, [this, i] { pickerApply(i); });
    }
    // botões
    float by = pr.y + ph - sY(52);
    TextStyle bt{Font::Barlow500, sK(13), sK(1), INK};
    auto button = [&](float x, const char* t, std::function<void()> fn) {
      float w = ui.measure(t, bt) + sX(32);
      Rect r{x, by, w, sY(36)};
      bool hov = r.contains(mouseX, mouseY);
      ui.border(r, hov ? INK : LINE.withAlpha(3.f), 1.2f, sK(2));
      ui.text(t, r.x + sX(16), r.y + r.h / 2 - ui.lineHeightNormal(bt) / 2, bt);
      addHit(r, fn);
      return w;
    };
    float bx = pr.x + sX(24);
    if (pickerTarget == 0)
      bx += button(bx, "COR ORIGINAL", [this] {
        if (shop.livery >= 0) shop.liveries[shop.livery].repaint = false;
        pickerTarget = -1;
        shopChanged(true);
      }) + sX(10);
    if (pickerTarget == 1)
      bx += button(bx, "ARO ORIGINAL", [this] {
        if (shop.livery >= 0) shop.liveries[shop.livery].rimCustom = false;
        pickerTarget = -1;
        shopChanged(true);
      }) + sX(10);
    if (pickerTarget == 2) bx += button(bx, "IMAGEM DO COMPUTADOR…", [this] { pickerTarget = -1; openFileDialog(); }) + sX(10);
    button(bx, "CANCELAR", [this] { pickerTarget = -1; });
  }
  void pickerKey(int dx, int dy) {
    int n = pickerCount(), cols = pickerCols();
    if (n <= 0) return;
    pickerSel = std::clamp(pickerSel + dx + dy * cols, 0, n - 1);
  }

  // ícones dos cards da oficina (contorno escuro)
  void shopIcon(int kind, float cx, float cy, float s, const Color& c) {
    float w = std::max(1.4f, s * 0.05f);
    if (kind == 0) {  // motor: bloco, cabeçote e coletor
      ui.border({cx - s * 0.36f, cy - s * 0.12f, s * 0.6f, s * 0.36f}, c, w, s * 0.03f);
      ui.border({cx - s * 0.3f, cy - s * 0.3f, s * 0.48f, s * 0.16f}, c, w, s * 0.03f);
      for (int i = 0; i < 3; i++) ui.line(cx - s * 0.2f + i * s * 0.14f, cy - s * 0.3f, cx - s * 0.2f + i * s * 0.14f, cy - s * 0.4f, w, c);
      ui.line(cx + s * 0.24f, cy, cx + s * 0.42f, cy, w, c);
      ui.border({cx + s * 0.36f, cy - s * 0.1f, s * 0.1f, s * 0.2f}, c, w);
      ui.circle(cx - s * 0.06f, cy + s * 0.06f, s * 0.07f, c);
    } else if (kind == 1) {  // suspensão: mola e amortecedor
      float x0 = cx - s * 0.12f, x1 = cx + s * 0.12f, top = cy - s * 0.38f, n = 7;
      for (int i = 0; i < n; i++) {
        float y0 = top + s * 0.1f + i * s * 0.08f, y1 = y0 + s * 0.08f;
        ui.line(i % 2 ? x1 : x0, y0, i % 2 ? x0 : x1, y1, w * 1.4f, c);
      }
      ui.line(cx, top - s * 0.02f, cx, top + s * 0.1f, w * 2, c);
      ui.border({cx - s * 0.05f, cy + s * 0.22f, s * 0.1f, s * 0.18f}, c, w);
      ui.rect({cx - s * 0.18f, top + s * 0.08f, s * 0.36f, w * 1.5f}, c);
      ui.rect({cx - s * 0.18f, cy + s * 0.2f, s * 0.36f, w * 1.5f}, c);
    } else if (kind == 3) {  // adesivo: folha com a ponta descolando
      float h = s * 0.36f;
      ui.line(cx - h, cy - h, cx + h * 0.4f, cy - h, w, c);
      ui.line(cx - h, cy - h, cx - h, cy + h, w, c);
      ui.line(cx - h, cy + h, cx + h, cy + h, w, c);
      ui.line(cx + h, cy + h, cx + h, cy - h * 0.4f, w, c);
      ui.line(cx + h * 0.4f, cy - h, cx + h, cy - h * 0.4f, w, c);
      ui.line(cx + h * 0.4f, cy - h, cx + h * 0.4f, cy - h * 0.4f, w, c);
      ui.line(cx + h * 0.4f, cy - h * 0.4f, cx + h, cy - h * 0.4f, w, c);
      ui.circle(cx - h * 0.2f, cy + h * 0.2f, h * 0.32f, c);
    } else if (kind == 4) {  // aro: roda com raios
      float r = s * 0.38f;
      ui.ring(cx, cy, r, w * 1.6f, c);
      ui.ring(cx, cy, r * 0.25f, w, c);
      for (int i = 0; i < 5; i++) {
        float a = i * 2 * (float)M_PI / 5 - (float)M_PI / 2;
        ui.line(cx + std::cos(a) * r * 0.25f, cy + std::sin(a) * r * 0.25f, cx + std::cos(a) * r * 0.92f, cy + std::sin(a) * r * 0.92f, w * 1.4f, c);
      }
    } else {  // design: pincel/rolo de pintura
      ui.border({cx - s * 0.32f, cy - s * 0.34f, s * 0.5f, s * 0.18f}, c, w, s * 0.04f);
      ui.line(cx + s * 0.18f, cy - s * 0.25f, cx + s * 0.3f, cy - s * 0.25f, w, c);
      ui.line(cx + s * 0.3f, cy - s * 0.25f, cx + s * 0.3f, cy - s * 0.06f, w, c);
      ui.line(cx + s * 0.3f, cy - s * 0.06f, cx - s * 0.04f, cy - s * 0.02f, w, c);
      ui.line(cx - s * 0.04f, cy - s * 0.02f, cx - s * 0.04f, cy + s * 0.1f, w, c);
      ui.rect({cx - s * 0.08f, cy + s * 0.1f, s * 0.08f, s * 0.28f}, c, s * 0.02f);
    }
  }

  void drawShop(float dt, const Layout& L) {
    (void)L;
    rowCtls.clear();
    const CarEntry* e = shopEntry();
    LoadedCar* lc = shopLoaded();
    if (!e || !lc) return;
    if (perfDirty && (perfTimer -= dt) <= 0) {
      perfNow = estimatePerf(tunedSpec(*e->spec, shop.tune), &lc->geometry);
      perfDirty = false;
    }
    float W = uiW;
    float k = INFO_EASE(shopT.value);
    ui.pushOpacity(shopT.value);

    // --- ficha curta do carro
    float x = sX(55) - sX(16) * (1 - k), y = sY(140);
    auto lg = logos.find(e->id);
    if (lg != logos.end()) {
      const Texture& t = *lg->second;
      float ar = (float)t.width / t.height, lh = sY(40), lw = std::min(sX(60), lh * ar);
      lh = lw / ar;
      ui.imageCover(t.id, t.width, t.height, {x + sX(10), y, lw, lh});
      y += lh + sY(8);
    }
    TextStyle mk{Font::Barlow500, sK(12), sK(3), INK};
    ui.text(upper(e->maker), x, y, mk);
    y += sY(18);
    TextStyle nm{Font::Barlow400, sK(27), sK(1.6f), INK};
    ui.text(upper(e->name), x, y, nm);
    y += sY(34);
    TextStyle yr{Font::Barlow300, sK(17), sK(1), INK};
    ui.text(std::to_string(e->year), x, y, yr);

    // --- lista de ajustes da aba (com rolagem)
    float lx = sX(70), lright = sX(560);
    Rect content{sX(40), sY(292), lright - sX(40) + sX(20), sY(668) - sY(292)};
    settingsPanel = content;
    settingsScroll = std::fmax(0.f, std::fmin(settingsScroll, std::fmax(0.f, shopContentH - content.h)));
    ui.pushClip(content);
    float ry = content.y - settingsScroll;
    settingsContentTop = content.y;
    rowLight = true;
    auto lvl = [](int v) {
      char b[16];
      if (v == 0) return std::string("Original");
      std::snprintf(b, sizeof b, "%+d", v);
      return std::string(b);
    };
    if (shopTab < 2) {
      for (const TuneItem& it : TUNE_ITEMS) {
        if (it.tab != shopTab) continue;
        int v = shop.tune.*it.field;
        std::string val = it.names.empty() ? lvl(v) : it.names[v - it.lo];
        const TuneItem* ip = &it;
        settingRow(lx, lright, ry, it.label, val, it.desc, [this, ip](int d) {
          int& f = shop.tune.*(ip->field);
          int nv = std::clamp(f + d, ip->lo, ip->hi);
          if (nv != f) { f = nv; shopChanged(false); }
        });
      }
      // volta tudo da aba ao original
      rowAction = true;
      settingRow(lx, lright, ry, shopTab == 0 ? "Restaurar motor original" : "Restaurar suspensão original", "Restaurar",
                 "Volta todos os ajustes desta aba para os valores de fábrica.", [this](int) {
                   for (const TuneItem& it : TUNE_ITEMS)
                     if (it.tab == shopTab) shop.tune.*it.field = 0;
                   shopChanged(false);
                 });
    } else if (shopTab == 2) {
      // --- PINTURA
      int n = (int)shop.liveries.size();
      std::string cur = shop.livery < 0 ? "Original" : shop.liveries[shop.livery].name;
      settingRow(lx, lright, ry, "Pintura", cur, "Escolha a pintura do carro: a original de fábrica ou uma das suas.", [this, n](int d) {
        shop.livery = ((shop.livery + 1 + d) % (n + 1) + n + 1) % (n + 1) - 1;
        settingsFocus = 0;
        stickerSel = -1;
        shopChanged(true);
      });
      rowAction = true;
      settingRow(lx, lright, ry, "Nova pintura", "Criar", "Cria uma pintura nova a partir da escolhida, para você editar e guardar.", [this](int) {
        Livery l = shop.livery >= 0 ? shop.liveries[shop.livery] : Livery{};
        l.name = "Pintura " + std::to_string(shop.liveries.size() + 1);
        if (shop.livery < 0) { l.repaint = true; l.keepArt = false; }
        shop.liveries.push_back(l);
        shop.livery = (int)shop.liveries.size() - 1;
        stickerSel = -1;
        shopChanged(true);
      });
      if (shop.livery >= 0) {
        Livery& l = shop.liveries[shop.livery];
        if (l.repaint) rowSwatch = Color(l.paint.x, l.paint.y, l.paint.z, 1);
        rowAction = true;
        settingRow(lx, lright, ry, "Cor da carroceria", l.repaint ? "Escolher" : "Original",
                   "Abre a grade de cores para pintar a carroceria inteira.", [this](int) { openPicker(0); });
        if (l.repaint) {
          const char* fin[3] = {"Brilhante", "Metálico", "Fosco"};
          settingRow(lx, lright, ry, "Acabamento", fin[l.finish], "Brilhante (verniz), metálico (com flocos) ou fosco.", [this](int d) {
            Livery& L2 = shop.liveries[shop.livery];
            L2.finish = ((L2.finish + d) % 3 + 3) % 3;
            shopChanged(true);
          });
          settingRow(lx, lright, ry, "Decoração original", l.keepArt ? "Manter" : "Remover",
                     "Manter: a cor tinge a decoração de fábrica (patrocínios continuam). Remover: carro de uma cor só.", [this](int) {
                       Livery& L2 = shop.liveries[shop.livery];
                       L2.keepArt = !L2.keepArt;
                       shopChanged(true);
                     });
        }
        rowAction = true;
        settingRow(lx, lright, ry, "Excluir pintura", "Excluir", "Apaga esta pintura (volta para a original).", [this](int) {
          shop.liveries.erase(shop.liveries.begin() + shop.livery);
          shop.livery = -1;
          stickerSel = -1;
          settingsFocus = 0;
          shopChanged(true);
        });
      }
    } else if (shopTab == 3) {
      // --- ADESIVO
      rowAction = true;
      settingRow(lx, lright, ry, "Adicionar adesivo", "Galeria", "Escolhe uma imagem da galeria (pasta decals) para colar no carro.",
                 [this](int) { openPicker(2); });
      rowAction = true;
      settingRow(lx, lright, ry, "Imagem do computador", "Abrir…",
                 "Escolhe uma imagem PNG ou JPG do seu computador; ela é copiada para a galeria e colada no carro.", [this](int) { openFileDialog(); });
      Livery* l = shop.livery >= 0 ? &shop.liveries[shop.livery] : nullptr;
      if (l) {
        if (stickerSel >= (int)l->stickers.size()) stickerSel = (int)l->stickers.size() - 1;
        for (int i = 0; i < (int)l->stickers.size(); i++) {
          std::string nmS = l->stickers[i].file.substr(0, l->stickers[i].file.find_last_of('.'));
          rowAction = true;
          settingRow(lx, lright, ry, "Adesivo " + std::to_string(i + 1) + (i == stickerSel ? "  ●" : ""), nmS,
                     "Seleciona este adesivo para mover, girar e mudar o tamanho.", [this, i](int) { stickerSel = i; });
        }
        if (stickerSel >= 0) {
          Sticker& st = l->stickers[stickerSel];
          char b[24];
          std::snprintf(b, sizeof b, "%.2f m", st.size);
          settingRow(lx, lright, ry, "Tamanho", b, "Largura do adesivo (teclas Z / X).", [this](int d) {
            Sticker& s2 = shop.liveries[shop.livery].stickers[stickerSel];
            s2.size = std::clamp(s2.size * (d > 0 ? 1.12f : 1 / 1.12f), 0.1f, 3.f);
            shopChanged(true);
          });
          std::snprintf(b, sizeof b, "%.0f°", st.rot);
          settingRow(lx, lright, ry, "Rotação", b, "Gira o adesivo (teclas Q / E, ou os gatilhos do controle).", [this](int d) {
            Sticker& s2 = shop.liveries[shop.livery].stickers[stickerSel];
            s2.rot = std::fmod(s2.rot + d * 15 + 360, 360.f);
            shopChanged(true);
          });
          settingRow(lx, lright, ry, "Espelhar no outro lado", st.mirror ? "Sim" : "Não", "Repete o adesivo, espelhado, no lado oposto do carro.",
                     [this](int) {
                       Sticker& s2 = shop.liveries[shop.livery].stickers[stickerSel];
                       s2.mirror = !s2.mirror;
                       shopChanged(true);
                     });
          rowAction = true;
          settingRow(lx, lright, ry, "Duplicar adesivo", "Duplicar", "Cria uma cópia deste adesivo para posicionar em outro lugar.", [this](int) {
            auto& v = shop.liveries[shop.livery].stickers;
            if ((int)v.size() >= DecalSet::MAX) return;
            Sticker c = v[stickerSel];
            c.c = c.c + vec3(0, 0, 0.3f);
            v.push_back(c);
            stickerSel = (int)v.size() - 1;
            shopChanged(true);
          });
          rowAction = true;
          settingRow(lx, lright, ry, "Excluir adesivo", "Excluir", "Remove este adesivo do carro.", [this](int) {
            auto& v = shop.liveries[shop.livery].stickers;
            v.erase(v.begin() + stickerSel);
            stickerSel = std::min(stickerSel, (int)v.size() - 1);
            shopChanged(true);
          });
        }
      }
    } else {
      // --- ARO
      Livery* l = shop.livery >= 0 ? &shop.liveries[shop.livery] : nullptr;
      if (l && l->rimCustom) rowSwatch = Color(l->rim.x, l->rim.y, l->rim.z, 1);
      rowAction = true;
      settingRow(lx, lright, ry, "Cor do aro", l && l->rimCustom ? "Escolher" : "Original", "Abre a grade de cores para pintar os aros das rodas.",
                 [this](int) { openPicker(1); });
      if (l && l->rimCustom) {
        const char* fin[4] = {"Brilhante", "Metálico", "Fosco", "Cromado"};
        settingRow(lx, lright, ry, "Acabamento do aro", fin[l->rimFinish], "Brilhante, metálico, fosco ou cromado (espelhado).", [this](int d) {
          Livery& L2 = shop.liveries[shop.livery];
          L2.rimFinish = ((L2.rimFinish + d) % 4 + 4) % 4;
          shopChanged(true);
        });
        rowAction = true;
        settingRow(lx, lright, ry, "Aro original", "Restaurar", "Volta os aros para a cor de fábrica.", [this](int) {
          shop.liveries[shop.livery].rimCustom = false;
          shopChanged(true);
        });
      }
    }
    rowLight = false;
    shopContentH = ry + settingsScroll - content.y + sY(8);
    ui.popClip();
    if (focusVisible && settingsFocus < (int)rowCtls.size()) {
      float fy = rowCtls[settingsFocus].y, fh = rowCtls[settingsFocus].h;
      if (fy - settingsScroll < 0) settingsScroll = std::max(0.f, fy);
      if (fy + fh - settingsScroll > content.h) settingsScroll = fy + fh - content.h;
    }
    if (shopContentH > content.h + 1) {
      float bh = content.h * content.h / shopContentH, by = content.y + (content.h - bh) * settingsScroll / (shopContentH - content.h);
      ui.rect({lright + sX(14), by, 2, bh}, Color(0, 0, 0, 0.25f), 1);
    }
    // descrição da linha em foco / sob o mouse, embaixo da lista
    if (!rowCtls.empty()) {
      int act = -1;
      for (size_t i = 0; i < rowCtls.size(); i++)
        if (rowCtls[i].hovered) act = (int)i;
      if (act < 0 && focusVisible && settingsFocus < (int)rowCtls.size()) act = settingsFocus;
      if (act >= 0) {
        TextStyle ds{Font::Barlow400, sK(13), 0, INK2};
        float dw = lright - lx, dy = sY(674), dlh = ui.lineHeightNormal(ds) * 1.25f;
        std::string cur, word;
        int lines = 0;
        auto flush = [&] {
          if (word.empty()) return;
          std::string cand = cur.empty() ? word : cur + " " + word;
          if (!cur.empty() && ui.measure(cand, ds) > dw) {
            if (lines < 2) ui.text(cur, lx, dy + lines * dlh, ds, dlh);
            lines++;
            cur = word;
          } else {
            cur = cand;
          }
          word.clear();
        };
        for (char c : rowCtls[act].desc) {
          if (c == ' ') flush();
          else word += c;
        }
        flush();
        if (!cur.empty() && lines < 2) ui.text(cur, lx, dy + lines * dlh, ds, dlh);
      }
    }

    // --- desempenho (direita): original → com o acerto (só na aba do motor)
    if (shopTab == 0) {
      float px = W - sX(420), pw = sX(236) + sX(130), py = sY(150);
      px = W - sX(70) - pw;
      TextStyle hd{Font::Barlow500, sK(12.5f), sK(2.2f), INK};
      ui.text("DESEMPENHO ATUAL", px, py, hd);
      py += sY(30);
      ui.rect({px, py, pw, 1}, LINE.withAlpha(1.6f));
      struct R { const char* l; float base, now; const char* fmt; bool lowerBetter; };
      R rows[5] = {{"POTÊNCIA", perfBase.powerCv, perfNow.powerCv, "%.0f cv", false},
                   {"TORQUE", perfBase.torqueNm, perfNow.torqueNm, "%.0f Nm", false},
                   {"PESO", perfBase.massKg, perfNow.massKg, "%.0f kg", true},
                   {"0 - 100 km/h", perfBase.zeroTo100, perfNow.zeroTo100, "%.1f s", true},
                   {"VELOCIDADE MÁXIMA", perfBase.topKmh, perfNow.topKmh, "%.0f km/h", false}};
      TextStyle rl{Font::Barlow400, sK(12), sK(0.5f), INK};
      TextStyle rv{Font::Barlow400, sK(14), 0, INK, true};
      TextStyle rd{Font::Barlow500, sK(11.5f), 0, INK, true};
      float rh = sY(46);
      for (auto& r : rows) {
        char b[32];
        std::snprintf(b, sizeof b, r.fmt, r.now);
        std::string v = b;
        bool secs = std::strstr(r.fmt, " s") != nullptr;
        if (secs) { for (auto& ch : v) if (ch == '.') ch = ','; }
        float cy = py + rh / 2;
        ui.text(r.l, px, cy - ui.lineHeightNormal(rl) / 2, rl);
        float vw = ui.measure(v, rv);
        ui.text(v, px + pw - vw, cy - ui.lineHeightNormal(rv) / 2, rv);
        float diff = r.now - r.base;
        float rel = std::fabs(diff) / std::max(0.001f, std::fabs(r.base));
        if (rel > 0.004f && std::fabs(diff) >= (secs ? 0.05f : 0.5f)) {
          bool good = (diff > 0) != r.lowerBetter;
          char db[32];
          std::snprintf(db, sizeof db, secs ? "%+.1f" : "%+.0f", diff);
          std::string ds = db;
          for (auto& ch : ds) if (ch == '.') ch = ',';
          rd.color = good ? Color::hex(0x1a8f3a) : Color::hex(0xc8102e);
          ui.text(ds, px + pw - vw - sX(10) - ui.measure(ds, rd), cy - ui.lineHeightNormal(rd) / 2, rd);
        }
        py += rh;
        ui.rect({px, py, pw, 1}, LINE.withAlpha(1.6f));
      }
      // índice de desempenho (classe e número)
      py += sY(44);
      ui.text("ÍNDICE DE DESEMPENHO", px, py, hd);
      py += sY(34);
      float bc = sK(17);
      ui.circle(px + bc, py + bc, bc, INK);
      TextStyle cl{Font::Barlow500, sK(17), 0, WHITE};
      std::string c(1, perfNow.cls());
      ui.text(c, px + bc - ui.measure(c, cl) / 2, py + bc - ui.lineHeightNormal(cl) / 2, cl);
      TextStyle pn{Font::Barlow400, sK(22), sK(0.6f), INK, true};
      ui.text(std::to_string(perfNow.pi), px + bc * 2 + sX(14), py + sY(2), pn);
      float bx = px + bc * 2 + sX(14), bw = pw - (bx - px), byy = py + bc * 2 - sY(4);
      ui.rect({bx, byy, bw, 2}, LINE.withAlpha(2.f));
      ui.rect({bx, byy, bw * std::clamp((perfNow.pi - 100) / 899.f, 0.f, 1.f), 3}, INK);
    }

    // --- cards: as três seções (ou as pinturas, na aba design)
    {
      float cy0 = sY(712), ch = sY(118), gap = sX(12);
      struct C { int icon; std::string title, sub; std::function<void()> click; bool sel; vec3 swatch; bool hasSwatch; };
      std::vector<C> cs;
      const Livery* cl0 = shop.livery >= 0 ? &shop.liveries[shop.livery] : nullptr;
      if (shopTab != 2) {
        auto summary = [&](int tab) {
          int changed = 0, total = 0;
          for (auto& it : TUNE_ITEMS)
            if (it.tab == tab) { total++; if (shop.tune.*it.field != 0) changed++; }
          return changed == 0 ? std::string("Original") : std::to_string(changed) + " de " + std::to_string(total) + " ajustes";
        };
        cs.push_back({0, "MOTOR", summary(0), [this] { setShopTab(0); }, shopTab == 0, {}, false});
        cs.push_back({1, "SUSPENSÃO", summary(1), [this] { setShopTab(1); }, shopTab == 1, {}, false});
        cs.push_back({2, "PINTURA", cl0 ? cl0->name : "Pintura original", [this] { setShopTab(2); }, false, {}, false});
        int nst = cl0 ? (int)cl0->stickers.size() : 0;
        cs.push_back({3, "ADESIVO", nst == 0 ? std::string("Nenhum") : std::to_string(nst) + (nst == 1 ? " adesivo" : " adesivos"),
                      [this] { setShopTab(3); }, shopTab == 3, {}, false});
        cs.push_back({4, "ARO", cl0 && cl0->rimCustom ? "Personalizado" : "Original", [this] { setShopTab(4); }, shopTab == 4, {}, false});
      } else {
        cs.push_back({2, "ORIGINAL", "Pintura de fábrica", [this] { shop.livery = -1; stickerSel = -1; shopChanged(true); }, shop.livery < 0, {}, false});
        for (int i = 0; i < (int)shop.liveries.size(); i++) {
          const Livery& l = shop.liveries[i];
          vec3 sw = l.repaint ? l.paint : vec3(0.75f);
          auto srgb = [](float v) { return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f; };
          sw = vec3(srgb(sw.x), srgb(sw.y), srgb(sw.z));
          std::string sub = l.repaint ? "Pintura personalizada" : "Cores de fábrica";
          if (!l.stickers.empty()) sub += " · " + std::to_string(l.stickers.size()) + " adesivo" + (l.stickers.size() > 1 ? "s" : "");
          cs.push_back({2, upper(l.name), sub, [this, i] { shop.livery = i; stickerSel = -1; shopChanged(true); }, shop.livery == i, sw, true});
        }
      }
      int n = (int)cs.size();
      float cw = std::max(sX(150), std::min(sX(250), (W - sX(110) - gap * (n - 1)) / std::max(1, n)));
      float cx = sX(55);
      ui.pushClip({sX(40), cy0 - sY(20), W - sX(80), ch + sY(40)});
      for (auto& c : cs) {
        float g = c.sel ? sK(4) : 0;
        Rect r{cx - g, cy0 - g, cw + 2 * g, ch + 2 * g};
        bool hov = r.contains(mouseX, mouseY);
        ui.shadow(r, Color(0, 0, 0, hov || c.sel ? 0.12f : 0.06f), 0, 8, 22, -10);
        ui.rect(r, Color(1, 1, 1, 0.97f));
        if (c.hasSwatch) {
          ui.rect({r.x + sX(14), r.y + sY(16), r.w - sX(28), sY(46)}, Color(c.swatch.x, c.swatch.y, c.swatch.z, 1), sK(3));
        } else {
          shopIcon(c.icon, r.x + r.w / 2, r.y + sY(40), sK(56), INK);
        }
        TextStyle t{Font::Barlow500, sK(13), sK(0.8f), INK};
        TextStyle st{Font::Barlow400, sK(11.5f), 0, INK3};
        ui.text(c.title, r.x + sX(16), r.y + r.h - sY(44), t);
        ui.text(c.sub, r.x + sX(16), r.y + r.h - sY(24), st);
        chevron(r.x + r.w - sX(22), r.y + r.h - sY(30), sK(5), true, INK.withAlpha(0.7f));
        if (c.sel) ui.border(r, INK, 2);
        auto cl = c.click;
        if (r.x + r.w < W - sX(40)) addHit(r, [cl] { cl(); });
        cx += cw + gap;
      }
      ui.popClip();
    }

    // --- rodapé: Voltar e APLICAR ALTERAÇÕES
    {
      PadStyle ps = promptStyle();
      TextStyle hs{Font::Barlow400, sK(15), 0, INK};
      float fy = play.rect.y + play.rect.h / 2 + sY(8), fx = sX(54), x0 = fx;
      fx += drawPrompt(ui, ps, PadButton::Back, fx, fy, sK(24), "Esc") + sX(12);
      fx += ui.text("Voltar", fx, fy - ui.lineHeightNormal(hs) / 2, hs);
      addHit({x0 - 6, fy - sY(18), fx - x0 + 12, sY(36)}, [this] { closeShop(false); });
      Rect r = play.rect;
      bool hov = r.contains(mouseX, mouseY);
      ui.rect(r, mix(Color::hex(0x16181b), ACCENT, hov ? 0.85f : 0.f));
      TextStyle st{Font::Barlow500, sK(14.5f), sK(1.0f), WHITE};
      std::string t = "APLICAR ALTERAÇÕES";
      float tw = ui.measure(t, st), kw = ps == PadStyle::Keyboard ? 0.f : promptWidth(ui, ps, PadButton::Start, sK(20), "");
      float tx = r.x + (r.w - tw - kw - (kw > 0 ? sX(12) : 0)) / 2 - sX(10);
      ui.text(t, tx, r.y + r.h / 2 - ui.lineHeightNormal(st) / 2, st);
      if (kw > 0) drawPrompt(ui, ps, PadButton::Start, tx + tw + sX(12), r.y + r.h / 2, sK(20), "", {true});
      chevron(r.x + r.w - sX(34), r.y + r.h / 2, sK(6), true, WHITE);
      addHit(r, [this] { closeShop(true); });
    }
    // adesivos: câmera parada para posicionar; dica de comandos; imagem do computador recém-escolhida
    garage.controls.autoRotate = shopTab != 3;
    {
      std::string imp;
      {
        std::lock_guard<std::mutex> lk(importMutex);
        imp.swap(pendingImport);
      }
      if (!imp.empty()) {
        std::string f = importDecal(imp);
        if (!f.empty()) {
          if (shopTab != 3) setShopTab(3);
          addSticker(f);
        }
      }
    }
    stickerKeys(dt);
    if (shopTab == 3 && stickerSel >= 0 && shop.livery >= 0) {
      vec2 sp;
      if (stickerScreen(sp)) {
        ui.ring(sp.x, sp.y, sK(9), 2, INK);
        ui.ring(sp.x, sp.y, sK(11), 1.5f, WHITE);
      }
      TextStyle hs{Font::Barlow400, sK(12.5f), 0, INK2};
      const char* tip = promptStyle() == PadStyle::Keyboard
                            ? "Arraste o adesivo sobre o carro com o mouse · WASD move · Q/E gira · Z/X tamanho · arraste fora do carro para girar a câmera"
                            : "Analógico direito move o adesivo · gatilhos giram · analógico esquerdo/direcional navega";
      ui.text(tip, sX(640), sY(140), hs);
    }
    drawPicker();
    ui.popOpacity();
  }

  // ---------------------------------------------------------------- escolha da pista
  // bandeira pequena desenhada (país da pista)
  void drawFlag(const std::string& country, Rect fl) {
    if (country == "Bélgica") {
      ui.rect({fl.x, fl.y, fl.w / 3, fl.h}, Color::hex(0x111111));
      ui.rect({fl.x + fl.w / 3, fl.y, fl.w / 3, fl.h}, Color::hex(0xfdda24));
      ui.rect({fl.x + fl.w * 2 / 3, fl.y, fl.w / 3, fl.h}, Color::hex(0xef3340));
    }
  }

  void drawMapPage(float dt, const Layout& L) {
    (void)L;
    float k = INFO_EASE(pageT.value);
    ui.pushOpacity(pageT.value);
    const MapEntry& mp = *mapCards[selectedMap].map;
    float W = uiW;
    // --- ficha da pista
    {
      float x = sX(60) - sX(16) * (1 - k), y = sY(152);
      TextStyle co{Font::Barlow400, sK(13.5f), sK(2.4f), INK2};
      float cw = ui.text(upper(mp.country), x, y, co);
      drawFlag(mp.country, {x + cw + sX(12), y + sY(1), sX(24), sY(16)});
      y += sY(26);
      TextStyle nm{Font::Barlow500, sK(28), sK(2.2f), INK};
      ui.text(upper(mp.name), x, y, nm);
      y += sY(44);
      TextStyle ct{Font::Barlow400, sK(15), sK(2), INK2};
      ui.text(upper(mp.city), x, y, ct);
      y += sY(32);
      ui.rect({x, y, sX(272), 1}, LINE.withAlpha(1.8f));
      y += sY(14);
      // bandeira quadriculada + extensão | curvas
      {
        float fx = x + sX(2), fw = sX(20), fh = sY(14), fy0 = y + sY(4);
        ui.line(fx, fy0 - sY(1), fx - sX(3), fy0 + fh + sY(4), sK(1.6f), INK);
        for (int yy = 0; yy < 3; yy++)
          for (int xx = 0; xx < 4; xx++)
            ui.rect({fx + xx * fw / 4, fy0 + yy * fh / 3, fw / 4, fh / 3}, (xx + yy) % 2 ? INK : Color(0, 0, 0, 0.08f));
        TextStyle fs{Font::Barlow400, sK(15), sK(1.2f), INK};
        float tx = x + sX(46);
        tx += ui.text(mp.length, tx, y + sY(2), fs) + sX(20);
        ui.rect({tx, y + sY(4), 1, sY(18)}, LINE.withAlpha(2.f));
        ui.text(mp.turns, tx + sX(20), y + sY(2), fs);
      }
      y += sY(46);
      // desenho do traçado (ou um quadrado tracejado na área livre)
      Rect box{x + sX(4), y, sX(272), sY(168)};
      auto it = mapOutlines.find(mp.id);
      if (it == mapOutlines.end() && !mp.outline.empty()) {
        TexturePtr t = loadImage(mp.outline, false);
        if (t) t->upload();
        it = mapOutlines.emplace(mp.id, t).first;
      }
      if (it != mapOutlines.end() && it->second) {
        const Texture& t = *it->second;
        float ar = (float)t.width / t.height, bw = std::min(box.w, box.h * ar), bh = bw / ar;
        ui.imageCover(t.id, t.width, t.height, {box.x, box.y + (box.h - bh) / 2, bw, bh});
      } else {
        for (int i = 0; i < 4; i++) {
          float ox = box.x + sX(30), oy = box.y + sY(30), ow = box.w * 0.62f, oh = box.h * 0.66f;
          for (float t = 0; t < 1; t += 0.08f) {
            float a = t, b = std::min(1.f, t + 0.045f);
            if (i == 0) ui.line(ox + ow * a, oy, ox + ow * b, oy, 2, INK);
            if (i == 1) ui.line(ox + ow, oy + oh * a, ox + ow, oy + oh * b, 2, INK);
            if (i == 2) ui.line(ox + ow * a, oy + oh, ox + ow * b, oy + oh, 2, INK);
            if (i == 3) ui.line(ox, oy + oh * a, ox, oy + oh * b, 2, INK);
          }
        }
      }
      y += box.h + sY(18);
      // descrição
      TextStyle ds{Font::Barlow400, sK(13.5f), 0, INK3};
      float dlh = ui.lineHeightNormal(ds) * 1.4f, dw = sX(330);
      std::string cur, word;
      int lines = 0;
      auto flush = [&] {
        if (word.empty()) return;
        std::string cand = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && ui.measure(cand, ds) > dw) {
          if (lines < 3) ui.text(cur, x, y + lines * dlh, ds, dlh);
          lines++;
          cur = word;
        } else cur = cand;
        word.clear();
      };
      for (char c : mp.description) { if (c == ' ') flush(); else word += c; }
      flush();
      if (!cur.empty() && lines < 3) ui.text(cur, x, y + lines * dlh, ds, dlh);
      int shown = std::min(3, lines + (cur.empty() ? 0 : 1));
      y = std::min(y + shown * dlh + sY(14), sY(614));
      // resumo da corrida configurada (muda no modal "Configurar corrida")
      {
        TextStyle hl{Font::Barlow500, sK(11.5f), sK(1.6f), INK2};
        TextStyle hv{Font::Barlow500, sK(13), sK(0.8f), INK};
        static const char* MODES[3] = {"Corrida livre", "Contra o tempo", "Corrida contra a IA"};
        int modeNow = mp.flat ? 0 : settings.raceMode;
        std::string sum = std::string(MODES[modeNow]);
        if (modeNow > 0) sum += " · " + std::to_string(settings.raceLaps) + (settings.raceLaps == 1 ? " volta" : " voltas");
        if (modeNow == 2) sum += " · " + std::to_string(settings.raceOpponents) + " adversários";
        sum += std::string(" · ") + (settings.timeOfDay ? "Pôr do sol" : "Dia");
        float hx = x + ui.text("CORRIDA", x, y + sY(4), hl) + sX(16);
        ui.text(sum, hx, y + sY(2), hv);
      }
    }
    // --- cards das pistas: foto inteira com faixa escura e o nome
    {
      float cy0 = sY(662), ch = sY(150), cw = sX(280), gap = sX(14), x0 = sX(60);
      for (size_t i = 0; i < mapCards.size(); i++) {
        MapCard& c = mapCards[i];
        Rect base{x0 + i * (cw + gap), cy0, cw, ch};
        bool hov = mode == Mode::Garage && pageT.to > 0.5f && base.contains(mouseX, mouseY);
        c.hover.set(hov ? 1.f : 0.f);
        c.selected.set((int)i == selectedMap ? 1.f : 0.f);
        c.hover.update(dt);
        c.selected.update(dt);
        float g = c.selected.value * sK(6);
        Rect r{base.x - g, base.y - g, base.w + 2 * g, base.h + 2 * g};
        r.y -= c.hover.value * sY(2);
        ui.shadow(r, Color(0, 0, 0, 0.12f + 0.12f * std::max(c.hover.value, c.selected.value)), 0, 10, 26, -12);
        ui.pushClip(r);
        if (c.thumb) ui.imageCover(c.thumb->id, c.thumb->width, c.thumb->height, r, 0.5f, 0.5f);
        else ui.rect(r, Color(0.8f, 0.8f, 0.82f, 1));
        // degradê escuro de baixo para cima
        const int N = 12;
        float gh = r.h * 0.55f;
        for (int s2 = 0; s2 < N; s2++) {
          float t = (s2 + 0.5f) / N;
          ui.rect({r.x, r.y + r.h - gh + gh * s2 / N, r.w, gh / N + 1}, Color(0.05f, 0.05f, 0.06f, 0.78f * t * t));
        }
        // ícone de traçado + nome + extensão · curvas
        float ix = r.x + sX(32), iy = r.y + r.h - sY(32);
        ui.ring(ix, iy, sK(9), 1.6f, WHITE);
        ui.line(ix + sK(6), iy - sK(6), ix + sK(14), iy - sK(12), 1.6f, WHITE);
        TextStyle tn{Font::Barlow500, sK(13.5f), sK(1.0f), WHITE};
        TextStyle td{Font::Barlow400, sK(11.5f), sK(0.4f), Color(1, 1, 1, 0.85f)};
        ui.text(upper(c.map->name), r.x + sX(56), r.y + r.h - sY(50), tn);
        ui.text(c.map->length + "  ·  " + c.map->turns, r.x + sX(58), r.y + r.h - sY(28), td);
        ui.popClip();
        if (c.selected.value > 0.001f) {
          ui.border({r.x - sK(4), r.y - sK(4), r.w + sK(8), r.h + sK(8)}, INK.withAlpha(c.selected.value), 2, sK(3));
        }
        if (pageT.to > 0.5f) addHit(base, [this, i] { selectedMap = (int)i; });
      }
      // setas
      int n = (int)mapCards.size();
      for (int side = 0; side < 2; side++) {
        bool rgt = side == 1;
        float ax = rgt ? W - sX(35) : sX(32), acy = cy0 + ch / 2;
        Rect ar{ax - sX(20), acy - sY(30), sX(40), sY(60)};
        bool ah = ar.contains(mouseX, mouseY);
        chevron(ax, acy, sK(9), rgt, INK.withAlpha(ah ? 1.f : 0.75f));
        if (pageT.to > 0.5f && n > 0) addHit(ar, [this, rgt, n] { selectedMap = ((selectedMap + (rgt ? 1 : -1)) % n + n) % n; });
      }
    }
    // --- rodapé: Voltar e Selecionar; CORRER à direita
    {
      PadStyle ps = promptStyle();
      TextStyle hs{Font::Barlow400, sK(15), 0, INK};
      float fy = play.rect.y + play.rect.h / 2 + sY(8), fx = sX(60), x0 = fx;
      fx += drawPrompt(ui, ps, PadButton::Back, fx, fy, sK(24), "ESC") + sX(12);
      fx += ui.text("Voltar", fx, fy - ui.lineHeightNormal(hs) / 2, hs);
      if (pageT.to > 0.5f) addHit({x0 - 6, fy - sY(18), fx - x0 + 12, sY(36)}, [this] { setMapPage(false); });
      fx += sX(56);
      x0 = fx;
      fx += drawPrompt(ui, ps, PadButton::Confirm, fx, fy, sK(24), "ENTER") + sX(12);
      fx += ui.text("Selecionar", fx, fy - ui.lineHeightNormal(hs) / 2, hs);
      if (pageT.to > 0.5f) addHit({x0 - 6, fy - sY(18), fx - x0 + 12, sY(36)}, [this] { startPlay(); });
      fx += sX(56);
      x0 = fx;
      fx += drawPrompt(ui, ps, PadButton::West, fx, fy, sK(24), "C") + sX(12);
      fx += ui.text("Configurar corrida", fx, fy - ui.lineHeightNormal(hs) / 2, hs);
      if (pageT.to > 0.5f) addHit({x0 - 6, fy - sY(18), fx - x0 + 12, sY(36)}, [this] { openRaceSetup(); });
      Rect r = play.rect;
      bool hov = r.contains(mouseX, mouseY);
      ui.rect(r, mix(Color::hex(0x16181b), ACCENT, hov ? 0.85f : 0.f));
      TextStyle st{Font::Barlow500, sK(15.5f), sK(1.0f), WHITE};
      ui.text("CORRER", r.x + sX(40), r.y + r.h / 2 - ui.lineHeightNormal(st) / 2, st);
      chevron(r.x + r.w - sX(44), r.y + r.h / 2, sK(6), true, WHITE);
      if (pageT.to > 0.5f) addHit(r, [this] { startPlay(); });
    }
    drawRaceSetup(mp);
    ui.popOpacity();
  }

  // modal "Configurar corrida": horário, modo, voltas e adversários
  void openRaceSetup() {
    raceSetup = true;
    settingsFocus = 0;
    settingsScroll = 0;
    focusVisible = false;
  }
  void drawRaceSetup(const MapEntry& mp) {
    if (!raceSetup) return;
    rowCtls.clear();
    hits.clear();  // o modal bloqueia o resto da tela
    float W = uiW, H = uiH;
    ui.rect({0, 0, W, H}, Color(0, 0, 0, 0.32f));
    float pw = sX(620), ph = sY(420), px = (W - pw) / 2, py = (H - ph) / 2;
    Rect pr{px, py, pw, ph};
    ui.shadow(pr, Color(0, 0, 0, 0.3f), 0, 20, 50, -10);
    ui.rect(pr, Color(0.985f, 0.985f, 0.985f, 1), sK(4));
    TextStyle t{Font::Barlow500, sK(18), sK(3), INK};
    ui.text("CONFIGURAR CORRIDA", px + sX(36), py + sY(30), t);
    TextStyle st{Font::Barlow400, sK(13), 0, INK3};
    ui.text(upper(mp.name), px + sX(36), py + sY(62), st);
    float lx = px + sX(50), lright = px + pw - sX(36), y = py + sY(104);
    settingsPanel = {px, py, pw, ph};
    settingsContentTop = y;
    rowLight = true;
    settingRow(lx, lright, y, "Horário", settings.timeOfDay ? "Pôr do sol" : "Dia", "Dia ou fim de tarde (céu, luz e neblina).", [this](int) {
      settings.timeOfDay = 1 - settings.timeOfDay;
      settings.save();
    });
    static const char* MODES[3] = {"Corrida livre", "Contra o tempo", "Contra a IA"};
    bool canRace = !mp.flat;
    int modeNow = canRace ? settings.raceMode : 0;
    settingRow(lx, lright, y, "Modo", MODES[modeNow],
               canRace ? "Livre: dirija à vontade. Contra o tempo: voltas cronometradas. Contra a IA: largada no grid contra adversários."
                       : "A pista de teste não tem traçado: só corrida livre.",
               [this](int d) {
                 settings.raceMode = ((settings.raceMode + d) % 3 + 3) % 3;
                 settings.save();
               },
               canRace);
    if (modeNow > 0) {
      settingRow(lx, lright, y, "Voltas", std::to_string(settings.raceLaps), "Quantas voltas tem a corrida.", [this](int d) {
        settings.raceLaps = std::clamp(settings.raceLaps + d, 1, 30);
        settings.save();
      });
    }
    if (modeNow == 2) {
      settingRow(lx, lright, y, "Adversários", std::to_string(settings.raceOpponents), "Quantos carros da IA largam com você.", [this](int d) {
        settings.raceOpponents = std::clamp(settings.raceOpponents + d, 1, 7);
        settings.save();
      });
    }
    rowLight = false;
    // botões
    PadStyle ps = promptStyle();
    TextStyle bt{Font::Barlow500, sK(14), sK(1), WHITE};
    Rect ok{px + pw - sX(36) - sX(200), py + ph - sY(36) - sY(52), sX(200), sY(52)};
    bool hov = ok.contains(mouseX, mouseY);
    ui.rect(ok, mix(Color::hex(0x16181b), ACCENT, hov ? 0.85f : 0.f));
    ui.text("CONFIRMAR", ok.x + sX(28), ok.y + ok.h / 2 - ui.lineHeightNormal(bt) / 2, bt);
    chevron(ok.x + ok.w - sX(28), ok.y + ok.h / 2, sK(5), true, WHITE);
    addHit(ok, [this] { raceSetup = false; });
    TextStyle hs{Font::Barlow400, sK(13.5f), 0, INK};
    float fx = px + sX(36), fy = ok.y + ok.h / 2;
    fx += drawPrompt(ui, ps, PadButton::Back, fx, fy, sK(22), "ESC") + sX(10);
    ui.text("Fechar", fx, fy - ui.lineHeightNormal(hs) / 2, hs);
    addHit({px + sX(30), fy - sY(18), sX(120), sY(36)}, [this] { raceSetup = false; });
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
    if (shopPage && pickerTarget >= 0) {
      if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) pickerKey(-1, 0);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) pickerKey(1, 0);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_UP) pickerKey(0, -1);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) pickerKey(0, 1);
      if (b == SDL_GAMEPAD_BUTTON_SOUTH) pickerApply(pickerSel);
      if (b == SDL_GAMEPAD_BUTTON_EAST) pickerTarget = -1;
      return;
    }
    if (shopPage) {
      if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) setShopTab(shopTab - 1);
      if (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) setShopTab(shopTab + 1);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_UP) moveFocus(-1);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) moveFocus(1);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) adjustFocus(-1);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) adjustFocus(1);
      if (b == SDL_GAMEPAD_BUTTON_SOUTH) activateFocus();
      if (b == SDL_GAMEPAD_BUTTON_EAST) closeShop(false);
      if (b == SDL_GAMEPAD_BUTTON_START) closeShop(true);
      return;
    }
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
    if (mapPage && raceSetup) {
      if (b == SDL_GAMEPAD_BUTTON_DPAD_UP) moveFocus(-1);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) moveFocus(1);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) adjustFocus(-1);
      if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) adjustFocus(1);
      if (b == SDL_GAMEPAD_BUTTON_SOUTH) activateFocus();
      if (b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_WEST || b == SDL_GAMEPAD_BUTTON_START) raceSetup = false;
      return;
    }
    if (mapPage) {
      int n = (int)mapCards.size();
      if (b == SDL_GAMEPAD_BUTTON_WEST) { openRaceSetup(); return; }
      if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) selectedMap = (selectedMap + n - 1) % n;
      if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) selectedMap = (selectedMap + 1) % n;

      if (b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START) startPlay();
      if (b == SDL_GAMEPAD_BUTTON_EAST) setMapPage(false);
      return;
    }
    int n = (int)cards.size(), i = selectedCard < 0 ? 0 : selectedCard;
    if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) selectCar((i + n - 1) % n);
    if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) selectCar((i + 1) % n);
    if (b == SDL_GAMEPAD_BUTTON_SOUTH) startPlay();
    if (b == SDL_GAMEPAD_BUTTON_WEST) openShop();      // X do Xbox / □ do PlayStation
    if (b == SDL_GAMEPAD_BUTTON_START) setTab(true);  // "Menu"
  }

  // foco das linhas das configurações (controle e setas do teclado); passar do topo/fim da
  // lista troca de categoria (só há Gráficos e Áudio) e entra pela primeira linha dela
  void moveFocus(int d) {
    int n = (int)rowCtls.size();
    if (!n) return;
    int nf = settingsFocus + d;
    if (shopPage || raceSetup) {
      settingsFocus = std::clamp(nf, 0, n - 1);
      focusVisible = true;
      return;
    }
    if (nf < 0 || nf >= n) {
      settingsCategory = (settingsCategory + (d > 0 ? 1 : 2)) % 3;
      settingsFocus = 0;
      settingsScroll = 0;
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
    raceSetup = false;
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

  // logo do jogo no cabeçalho (0 = escura, 1 = branca), centrada em cy; devolve a largura
  float drawHeaderLogo(int v, float x, float cy) {
    const TexturePtr& t = headerLogo[v];
    if (!t) return 0;
    float h = sY(34), w = h * t->width / t->height;
    ui.imageCover(t->id, t->width, t->height, {x, cy - h / 2, w, h});
    return w;
  }

  // escala da tela de configurações: desenhada numa referência de 1672×941
  float sX(float v) const { return v * uiW / 1672.f; }
  float sY(float v) const { return v * uiH / 941.f; }
  float sK(float v) const { return v * std::min(uiW / 1672.f, uiH / 941.f); }

  // arco em segmentos (ícones de contorno)
  void arc(float cx, float cy, float r, float a0, float a1, float w, const Color& c) {
    int n = std::max(4, (int)(std::fabs(a1 - a0) * r / 2));
    vec2 prev{cx + std::cos(a0) * r, cy + std::sin(a0) * r};
    for (int i = 1; i <= n; i++) {
      float a = a0 + (a1 - a0) * i / n;
      vec2 p{cx + std::cos(a) * r, cy + std::sin(a) * r};
      ui.line(prev.x, prev.y, p.x, p.y, w, c);
      prev = p;
    }
  }

  // ícones de contorno (estilo da tela de configurações), centrados em (cx, cy), lado s
  void settingsIcon(int kind, float cx, float cy, float s, const Color& c) {
    float w = std::max(1.2f, s * 0.07f);
    if (kind == 0) {  // monitor
      float bw = s, bh = s * 0.66f, top = cy - s * 0.42f;
      ui.border({cx - bw / 2, top, bw, bh}, c, w, s * 0.05f);
      ui.line(cx, top + bh, cx, top + bh + s * 0.16f, w, c);
      ui.line(cx - s * 0.24f, top + bh + s * 0.17f, cx + s * 0.24f, top + bh + s * 0.17f, w, c);
    } else if (kind == 1) {  // alto-falante com ondas
      float x0 = cx - s * 0.45f;
      ui.line(x0, cy - s * 0.14f, x0 + s * 0.2f, cy - s * 0.14f, w, c);
      ui.line(x0, cy + s * 0.14f, x0 + s * 0.2f, cy + s * 0.14f, w, c);
      ui.line(x0, cy - s * 0.14f, x0, cy + s * 0.14f, w, c);
      ui.line(x0 + s * 0.2f, cy - s * 0.14f, x0 + s * 0.45f, cy - s * 0.36f, w, c);
      ui.line(x0 + s * 0.2f, cy + s * 0.14f, x0 + s * 0.45f, cy + s * 0.36f, w, c);
      ui.line(x0 + s * 0.45f, cy - s * 0.36f, x0 + s * 0.45f, cy + s * 0.36f, w, c);
      arc(cx + s * 0.02f, cy, s * 0.2f, -0.9f, 0.9f, w, c);
      arc(cx + s * 0.02f, cy, s * 0.38f, -0.9f, 0.9f, w, c);
    } else {  // controle
      float bw = s, bh = s * 0.52f;
      ui.border({cx - bw / 2, cy - bh / 2, bw, bh}, c, w, bh / 2);
      ui.line(cx - s * 0.3f, cy, cx - s * 0.1f, cy, w, c);
      ui.line(cx - s * 0.2f, cy - s * 0.1f, cx - s * 0.2f, cy + s * 0.1f, w, c);
      ui.circle(cx + s * 0.16f, cy + s * 0.05f, s * 0.045f, c);
      ui.circle(cx + s * 0.27f, cy - s * 0.06f, s * 0.045f, c);
    }
  }

  // seta fina "<" ou ">" (dois traços)
  void chevron(float cx, float cy, float s, bool pointRight, const Color& c) {
    float d = pointRight ? 1.f : -1.f, w = std::max(1.2f, s * 0.28f);
    ui.line(cx - d * s * 0.35f, cy - s, cx + d * s * 0.35f, cy, w, c);
    ui.line(cx + d * s * 0.35f, cy, cx - d * s * 0.35f, cy + s, w, c);
  }

  // linha "rótulo ‹ valor ›": faixa clara quando em foco/sob o mouse e divisória fina embaixo.
  // onAdjust(d) recebe −1 ou +1; sem onAdjust a linha é só informativa (sem setas).
  void settingRow(float x, float right, float& y, const std::string& label, const std::string& value, const std::string& desc,
                  const std::function<void(int)>& onAdjust, bool enabled = true) {
    const float rowH = sY(46);
    Rect rowRect{x - sX(14), y, right - x + sX(14) + sX(56), rowH};
    bool hov = rowRect.contains(mouseX, mouseY) && mouseY >= settingsPanel.y && mouseY < settingsPanel.y + settingsPanel.h;
    int idx = (int)rowCtls.size();
    bool focused = (focusVisible && idx == settingsFocus) || (!focusVisible && hov);
    // tema: claro (oficina) usa tinta escura; escuro (configurações) usa branco
    vec3 fgc = rowLight ? vec3(0.067f, 0.071f, 0.078f) : vec3(1);
    auto fg = [&](float a) { return Color(fgc.x, fgc.y, fgc.z, a); };
    if (rowLight) {
      if (focused) ui.rect(rowRect, Color(0.067f, 0.071f, 0.078f, 0.92f), sK(2));
      else if (hov) ui.rect(rowRect, Color(0, 0, 0, 0.05f), sK(2));
      if (focused) fgc = vec3(1);
    } else {
      if (focused) ui.rectGradient(rowRect, Color(1, 1, 1, 0.10f), Color(1, 1, 1, 0.015f));
      else if (hov) ui.rectGradient(rowRect, Color(1, 1, 1, 0.05f), Color(1, 1, 1, 0.0f));
    }
    ui.rect({x - sX(14), y + rowH - 1, right - x + sX(14), 1}, rowLight ? Color(0, 0, 0, 0.09f) : Color(1, 1, 1, 0.07f));
    std::function<void(int)> adj = enabled ? onAdjust : std::function<void(int)>();
    bool action = rowAction;
    rowAction = false;
    std::function<void()> act = adj ? std::function<void()>([adj] { adj(1); }) : std::function<void()>();
    rowCtls.push_back({y + settingsScroll - settingsContentTop, rowH, action ? std::function<void(int)>() : adj, act, label, desc, hov});
    float cy = y + rowH / 2, a = enabled ? 1.f : 0.4f;
    float fs = rowLight ? 14.f : 15.5f;
    TextStyle lab{Font::Barlow400, sK(fs), 0, fg((focused ? 1.f : 0.86f) * a)};
    if (rowLight) lab = TextStyle{Font::Barlow500, sK(13), sK(0.6f), fg((focused ? 1.f : 0.9f) * a)};
    ui.text(rowLight ? upper(label) : label, x, cy - ui.lineHeightNormal(lab) / 2, lab);
    TextStyle vs{Font::Barlow400, sK(fs), 0, fg((focused ? 1.f : 0.84f) * a), true};
    float vx = right - sX(rowLight ? 92 : 138), vw = ui.measure(value, vs);
    ui.text(value, vx - vw / 2, cy - ui.lineHeightNormal(vs) / 2, vs);
    if (rowSwatch.a > 0) {
      float sw = sK(14);
      ui.rect({vx - vw / 2 - sw - sX(8), cy - sw / 2, sw, sw}, rowSwatch, sK(2));
      ui.border({vx - vw / 2 - sw - sX(8), cy - sw / 2, sw, sw}, fg(0.4f), 1, sK(2));
      rowSwatch = Color(0, 0, 0, 0);
    }
    if (!onAdjust) {
      y += rowH;
      return;
    }
    if (action) {
      // botão: a linha inteira é clicável e não tem setas
      if (enabled) addHit(rowRect, act);
      y += rowH;
      return;
    }
    for (int side = 0; side < 2; side++) {
      bool rgt = side == 1;
      float ax = rgt ? right - sX(rowLight ? 14 : 23) : right - sX(rowLight ? 170 : 253);
      Rect ar{ax - sX(18), cy - rowH / 2, sX(36), rowH};
      bool ah = enabled && ar.contains(mouseX, mouseY);
      chevron(ax, cy, sK(6), rgt, fg(!enabled ? 0.2f : ah ? 1.f : focused ? 0.85f : 0.55f));
      if (enabled) addHit(ar, [onAdjust, rgt] { onAdjust(rgt ? 1 : -1); });
    }
    y += rowH;
  }

  // divisória entre grupos de opções
  void settingsDivider(float x, float right, float& y) {
    y += sY(12);
    ui.rect({x, y, right - x, 1}, Color(1, 1, 1, 0.12f));
    y += sY(12);
  }

  // avança/recua um índice entre 0..n−1, dando a volta; −1 (personalizado) entra por uma ponta
  static int cycleIndex(int cur, int d, int n) {
    if (cur < 0) return d > 0 ? 0 : n - 1;
    return ((cur + d) % n + n) % n;
  }

  // tecla desenhada como contorno (ESC, R) no rodapé
  float keycap(const std::string& t, float x, float cy) {
    TextStyle st{Font::Barlow500, sK(12.5f), sK(0.6f), Color(1, 1, 1, 0.85f)};
    float w = std::max(sK(26), ui.measure(t, st) + sK(16)), h = sK(25);
    ui.border({x, cy - h / 2, w, h}, Color(1, 1, 1, 0.7f), 1.2f, sK(3));
    ui.text(t, x + (w - ui.measure(t, st)) / 2, cy - ui.lineHeightNormal(st) / 2, st);
    return w;
  }

  void drawSettings(float dt, const Layout& L) {
    (void)dt;
    (void)L;
    rowCtls.clear();
    float k = INFO_EASE(tabT.value);
    float W = uiW, H = uiH;
    ui.pushOpacity(tabT.value);

    // --- fundo: a garagem (o carro) desfocada e escurecida, mais escura à esquerda
    Rect full{0, 0, W, H};
    // (a garagem é clara: escurece bem para o texto branco ler como na tela de um console)
    ui.blurPanel(full, Color(0.02f, 0.022f, 0.026f, 0.88f));
    ui.rectGradient({0, 0, W * 0.62f, H}, Color(0.01f, 0.012f, 0.015f, 0.6f), Color(0.01f, 0.012f, 0.015f, 0.0f));

    // --- cabeçalho: marca | CONFIGURAÇÕES e linha fina
    float hx = sX(64) - sX(12) * (1 - k), hcy = sY(62);
    {
      float mw = drawHeaderLogo(1, hx, hcy);
      float dx = hx + mw + sX(24);
      ui.rect({dx, hcy - sY(14), 1, sY(28)}, Color(1, 1, 1, 0.3f));
      TextStyle tt{Font::Barlow400, sK(18), sK(5.2f), Color(1, 1, 1, 0.92f)};
      ui.text("CONFIGURAÇÕES", dx + sX(24), hcy - ui.lineHeightNormal(tt) / 2, tt);
      ui.rect({sX(64), sY(104), W - sX(128), 1}, Color(1, 1, 1, 0.12f));
    }

    // --- barra lateral de categorias
    struct Cat { const char* label, *title, *desc; int icon; };
    static const Cat cats[3] = {
        {"GRÁFICOS", "GRÁFICOS", "Ajuste a qualidade visual do jogo conforme o seu sistema.", 0},
        {"ÁUDIO", "ÁUDIO", "Ajuste o volume do motor, dos pneus e do ambiente.", 1},
        {"CONTROLES", "CONTROLES", "Pilotagem, teclas e botões usados na pista e nos menus.", 2},
    };
    bool showRight = W > 1100;
    float sideL = sX(38), sideR = sX(358);
    for (int i = 0; i < 3; i++) {
      bool active = settingsCategory == i;
      Rect r{sideL, sY(156) + i * sY(63), sideR - sideL, sY(57)};
      bool hov = r.contains(mouseX, mouseY);
      if (active) {
        ui.rectGradient(r, Color(1, 1, 1, 0.11f), Color(1, 1, 1, 0.02f));
        ui.rect({r.x, r.y, 2.5f, r.h}, WHITE);
      } else if (hov) {
        ui.rectGradient(r, Color(1, 1, 1, 0.05f), Color(1, 1, 1, 0.0f));
      }
      Color fg = active ? WHITE : Color(1, 1, 1, hov ? 0.92f : 0.78f);
      settingsIcon(cats[i].icon, sX(80), r.y + r.h / 2, sK(26), fg);
      TextStyle ns{Font::Barlow500, sK(15.5f), sK(0.8f), fg};
      ui.text(cats[i].label, sX(118), r.y + r.h / 2 - ui.lineHeightNormal(ns) / 2, ns);
      if (!active) addHit(r, [this, i] { settingsCategory = i; settingsFocus = 0; settingsScroll = 0; });
    }
    ui.rect({sideR, sY(156), 1, sY(604)}, Color(1, 1, 1, 0.08f));

    // --- conteúdo: título, subtítulo e linhas (com rolagem)
    float x = sX(398), right = showRight ? sX(1036) : W - sX(64);
    {
      TextStyle ct{Font::Barlow500, sK(21), sK(3.2f), WHITE};
      ui.text(cats[settingsCategory].title, x, sY(158), ct);
      TextStyle cs{Font::Barlow400, sK(14.5f), 0, Color(1, 1, 1, 0.68f)};
      ui.text(cats[settingsCategory].desc, x, sY(192), cs);
    }
    Rect content{sX(380), sY(238), right - sX(380) + sX(70), sY(800) - sY(238)};
    settingsPanel = content;
    settingsScroll = std::fmax(0.f, std::fmin(settingsScroll, std::fmax(0.f, settingsContentH[settingsCategory] - content.h)));
    ui.pushClip(content);
    float y = content.y - settingsScroll;
    settingsContentTop = content.y;
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
    } else if (settingsCategory == 1) {
      char buf[16];
      std::snprintf(buf, sizeof buf, "%d%%", (int)std::lround(settings.volume * 100));
      settingRow(x, right, y, "Volume", buf, "Volume geral do motor, dos pneus e do ambiente.", [this](int d) {
        settings.volume = std::fmax(0.f, std::fmin(1.f, settings.volume + d * 0.05f));
        changed(false);
      });
    } else {
      // pilotagem (ajustáveis)
      {
        char b[16];
        std::snprintf(b, sizeof b, "%d%%", (int)std::lround(settings.steerSens * 100));
        settingRow(x, right, y, "Sensibilidade da direção à velocidade", b,
                   "Reduz o ângulo máximo do volante conforme a velocidade, como nos simuladores com controle ou teclado. "
                   "0% = direção direta (como um volante de verdade). Não corrige o carro: só muda quanto o volante vira.",
                   [this](int d) {
                     settings.steerSens = std::fmax(0.f, std::fmin(1.f, std::round(settings.steerSens * 10 + d) / 10));
                     changed(false);
                   });
        settingRow(x, right, y, "Assistência de estabilidade", settings.stabilityAssist ? "Ligada" : "Desligada",
                   "Ligada: o jogo contraesterça e alivia o acelerador sozinho quando a traseira escapa (não existe num carro de corrida). "
                   "Desligada: física realista, o carro faz exatamente o que você manda.",
                   [this](int) {
                     settings.stabilityAssist = !settings.stabilityAssist;
                     changed(false);
                   });
        settingsDivider(x, right, y);
      }
      // lista informativa: teclado ou o controle em uso
      bool pad = promptStyle() != PadStyle::Keyboard;
      bool ps = promptStyle() == PadStyle::PlayStation;
      struct K { const char* what; const char* kb; const char* xb; const char* pl; const char* desc; };
      static const K keys[] = {
          {"Acelerar", "W / ↑", "RT", "R2", "Acelerador progressivo no gatilho do controle."},
          {"Frear / ré", "S / ↓", "LT", "L2", "Freio com ABS; parado, segure para engatar a ré."},
          {"Esterçar", "A D / ← →", "Analógico esquerdo", "Analógico esquerdo", "Direção com assistência de retorno ao centro."},
          {"Freio de mão", "Espaço", "A", "✕", "Trava as rodas traseiras (para curvas fechadas e drift)."},
          {"Trocar marcha", "E / Q", "RB / LB", "R1 / L1", "Sobe e desce marcha (com câmbio manual)."},
          {"Câmera", "C", "Y", "△", "Alterna entre perseguição, capô, cockpit e livre."},
          {"Reposicionar o carro", "R", "Start", "Options", "Coloca o carro de volta na pista."},
          {"Ajuda na tela", "H", "↑ (direcional)", "↑ (direcional)", "Mostra ou esconde a lista de comandos."},
          {"Câmbio automático / manual", "M", "—", "—", "Alterna o câmbio entre automático e manual."},
          {"ABS / controle de tração", "B / T", "—", "—", "Liga ou desliga o ABS e o controle de tração."},
          {"Pausar / sem som", "P / N", "—", "—", "Pausa a simulação ou silencia o som."},
          {"Voltar à garagem", "Esc", "View", "Share", "Sai da pista e volta para a garagem."},
      };
      for (auto& kk : keys)
        settingRow(x, right, y, kk.what, pad ? (ps ? kk.pl : kk.xb) : kk.kb, kk.desc, {});
    }
    settingsContentH[settingsCategory] = y + settingsScroll - content.y + sY(8);
    ui.popClip();

    // mantém a linha em foco visível
    if (focusVisible && settingsFocus < (int)rowCtls.size()) {
      float fy = rowCtls[settingsFocus].y, fh = rowCtls[settingsFocus].h;
      if (fy - settingsScroll < 0) settingsScroll = std::max(0.f, fy);
      if (fy + fh - settingsScroll > content.h) settingsScroll = fy + fh - content.h;
    }
    // barra de rolagem fina quando o conteúdo não cabe
    if (settingsContentH[settingsCategory] > content.h + 1) {
      float ch = settingsContentH[settingsCategory];
      float bh = content.h * content.h / ch, by = content.y + (content.h - bh) * settingsScroll / (ch - content.h);
      ui.rect({right + sX(24), by, 2, bh}, Color(1, 1, 1, 0.22f), 1);
    }

    // --- painel de descrição à direita (linha em foco ou sob o mouse)
    if (showRight) {
      ui.rect({sX(1143), sY(156), 1, sY(654)}, Color(1, 1, 1, 0.08f));
      if (!rowCtls.empty()) {
        int active = -1;
        for (size_t i = 0; i < rowCtls.size(); i++)
          if (rowCtls[i].hovered) { active = (int)i; break; }
        if (active < 0 && focusVisible && settingsFocus < (int)rowCtls.size()) active = settingsFocus;
        if (active < 0) active = 0;
        const RowCtl& r = rowCtls[active];
        float rx = sX(1178), rw = W - rx - sX(80);
        settingsIcon(cats[settingsCategory].icon, rx + sK(22), sY(252), sK(44), WHITE);
        TextStyle rh{Font::Barlow500, sK(17), sK(1.4f), WHITE};
        ui.text(upper(r.label), rx, sY(298), rh);
        TextStyle rd{Font::Barlow400, sK(14), 0, Color(1, 1, 1, 0.72f)};
        float rdlh = ui.lineHeightNormal(rd) * 1.35f, ry = sY(336);
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
    }

    // --- rodapé: Voltar, Restaurar padrão e Aplicar (teclas ou botões do controle)
    {
      PadStyle ps = promptStyle();
      TextStyle hs{Font::Barlow400, sK(15), 0, Color(1, 1, 1, 0.88f)};
      float fy = sY(870), fx = sX(66);
      float x0 = fx;
      fx += (ps == PadStyle::Keyboard ? keycap("ESC", fx, fy) : drawPrompt(ui, ps, PadButton::Back, fx, fy, sK(24), "", {true})) + sX(16);
      fx += ui.text("Voltar", fx, fy - ui.lineHeightNormal(hs) / 2, hs);
      addHit({x0 - 6, fy - sY(18), fx - x0 + 12, sY(36)}, [this] { setTab(false); });
      fx += sX(44);
      x0 = fx;
      fx += (ps == PadStyle::Keyboard ? keycap("R", fx, fy) : drawPrompt(ui, ps, PadButton::North, fx, fy, sK(24), "", {true})) + sX(16);
      fx += ui.text("Restaurar Padrão", fx, fy - ui.lineHeightNormal(hs) / 2, hs);
      addHit({x0 - 6, fy - sY(18), fx - x0 + 12, sY(36)}, [this] { resetSettings(); });
      // as mudanças já valem na hora: "Aplicar" salva e volta para a garagem
      float aw = ui.measure("Aplicar", hs);
      float ax = W - sX(70) - aw;
      Rect ar{ax - sX(30), fy - sY(18), aw + sX(36), sY(36)};
      bool ah = ar.contains(mouseX, mouseY);
      chevron(ax - sX(16), fy, sK(5), true, Color(1, 1, 1, ah ? 1.f : 0.8f));
      hs.color = Color(1, 1, 1, ah ? 1.f : 0.88f);
      ui.text("Aplicar", ax, fy - ui.lineHeightNormal(hs) / 2, hs);
      addHit(ar, [this] { settings.save(); setTab(false); });
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
  bool rowLight = false;           // linhas no tema claro (oficina)
  bool rowAction = false;          // próxima linha é um botão (só clique / Enter / A)
  Color rowSwatch{0, 0, 0, 0};     // amostra de cor na próxima linha
  int settingsCategory = 0;  // 0 = Gráficos, 1 = Áudio, 2 = Controles
  float settingsContentH[3] = {0, 0, 0}, settingsContentTop = 0;
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
  bool raceSetup = false;  // modal "Configurar corrida" aberto
  TexturePtr skyThumb[2];
  TexturePtr headerLogo[2];
  std::map<std::string, TexturePtr> mapOutlines;  // desenho do traçado de cada pista
  // oficina
  bool shopPage = false;
  Transition shopT{0, 0.35f};
  int shopTab = 0;  // 0 motor, 1 suspensão, 2 design
  CarShop shop, shopSaved;
  Perf perfBase{}, perfNow{};
  bool perfDirty = false;
  float perfTimer = 0, shopContentH = 0;
  std::vector<std::string> decals;
  int pickerTarget = -1, pickerSel = 0;  // −1 fechado; 0 cor da carroceria, 1 cor do aro, 2 galeria
  std::map<std::string, TexturePtr> galleryTex;
  int stickerSel = -1;
  bool stickerDrag = false;
  std::mutex importMutex;
  std::string pendingImport;  // caminho escolhido no seletor de arquivos (chega por outra thread)  // miniaturas do céu de dia e do pôr do sol
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
  // Wayland/GNOME: o plugin de bordas do libdecor (GTK) aborta o processo quando o esquema de
  // configurações do GNOME não tem a chave que ele espera; sem libdecor a janela abre igual
  // (antes ele já falhava e o jogo abria sem bordas; agora o GNOME derruba o processo)
#ifdef __linux__
  if (!SDL_getenv("LIBDECOR_PLUGIN_DIR")) SDL_setenv_unsafe("LIBDECOR_PLUGIN_DIR", "/nonexistent", 1);
#endif
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
