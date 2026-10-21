// Configurações do jogo (gráficos, tela e som), salvas em settings.ini na pasta de preferências.
#pragma once
#include <string>

#include "../render/postfx.h"

// menor janela permitida (a interface escala junto com a janela)
constexpr int MIN_WINDOW_W = 480, MIN_WINDOW_H = 270;

struct Settings {
  // gráficos
  int preset = 2;            // 0..3 = Baixo/Médio/Alto/Ultra; −1 = personalizado
  float pixelRatio = 1.25f;  // escala de resolução
  int msaa = 4;              // 0 (FXAA), 2 ou 4
  int ao = 1;                // 0 = desligada, 1 = normal (meia resolução), 2 = alta
  bool bloom = true;
  int shadow = 4096;         // 1024, 2048 ou 4096
  bool lensFx = true;        // vinheta, grão e aberração cromática
  bool speedBlur = true;     // desfoque radial em alta velocidade
  bool autoQuality = true;   // baixa a qualidade sozinho se o fps cair
  // tela
  bool fullscreen = false;
  bool vsync = true;
  // modo janela: tamanho (em pontos), posição e se estava maximizada — lembrados entre sessões
  int windowW = 1600, windowH = 900;
  int windowX = -1, windowY = -1;  // −1 = centralizada
  bool windowMax = true;
  // som
  float volume = 1;
  // garagem
  std::string lastCar;  // id do último carro escolhido (abre com ele)
  std::string lastMap;  // id da última pista escolhida

  void applyPreset(int p);
  // depois de mudar um item: vira "personalizado" se não bater com nenhum nível
  void matchPreset();
  Quality quality() const;

  void load();
  void save() const;
  static std::string path();
};
