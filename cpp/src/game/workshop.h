// Oficina: acerto de motor e suspensão e pinturas, guardados por carro (oficina/<id>.ini na pasta
// de preferências). O acerto vira uma cópia modificada do CarSpec usada na pista; as pinturas
// trocam a cor/acabamento da carroceria e podem projetar uma imagem (adesivo) nas laterais.
#pragma once
#include <string>
#include <vector>

#include "../physics/carspec.h"
#include "car_model.h"
#include "cars.h"

// cada ajuste é um nível inteiro (0 = original); os limites ficam em workshop.cpp (TUNE_ITEMS)
struct CarTune {
  // motor
  int power = 0, turbo = 0, limiter = 0, finalDrive = 0, weight = 0;
  // suspensão, pneus, freios, diferencial e aerodinâmica
  int springF = 0, springR = 0, arbF = 0, arbR = 0, damper = 0, rideHeight = 0;
  int tire = 0, brakeForce = 0, brakeBias = 0, diffPower = 0, diffCoast = 0, steerLock = 0, aero = 0, aeroBal = 0;
};

// adesivo projetado na carroceria, no espaço do carro (frente = −z, chão = 0, metros)
struct Sticker {
  std::string file;        // imagem em decals/
  vec3 c{-1.0f, 0.6f, 0};  // centro na superfície
  vec3 n{1, 0, 0};         // direção da projeção (entra na carroceria)
  vec3 up{0, 1, 0};
  float size = 0.7f;       // largura (m)
  float rot = 0;           // graus
  bool mirror = true;      // repete espelhado no outro lado
};

struct Livery {
  std::string name;
  bool repaint = false;    // false = cores de fábrica (só adesivos/aros por cima)
  vec3 paint{0.92f, 0.92f, 0.9f};
  int finish = 0;          // 0 brilhante, 1 metálico, 2 fosco
  bool keepArt = false;    // mantém a decoração original (tinge por cima) em vez da cor sólida
  std::vector<Sticker> stickers;
  bool rimCustom = false;
  vec3 rim{0.08f, 0.08f, 0.09f};
  int rimFinish = 1;       // 0 brilhante, 1 metálico, 2 fosco, 3 cromado
};

// grade de cores do seletor (cinzas + linhas de matiz/brilho), em cor linear
std::vector<vec3> colorGrid(int& cols);

// um ajuste de acerto: rótulo, descrição, faixa e nomes dos níveis (opcional)
struct TuneItem {
  int tab;  // 0 = motor, 1 = suspensão
  const char* label;
  const char* desc;
  int CarTune::*field;
  int lo, hi;
  std::vector<const char*> names;  // se vazio, mostra +n / −n
};
extern const std::vector<TuneItem> TUNE_ITEMS;

struct CarShop {
  CarTune tune;
  std::vector<Livery> liveries;
  int livery = -1;  // −1 = pintura original
};

CarShop loadShop(const std::string& carId);
void saveShop(const std::string& carId, const CarShop& shop);

// spec com o acerto aplicado
phys::CarSpec tunedSpec(const phys::CarSpec& base, const CarTune& t);

// números de desempenho (estimados pela simulação/física)
struct Perf {
  float powerCv, torqueNm, massKg, zeroTo100, topKmh;
  int pi;  // índice de desempenho (100..999) e classe pela faixa
  char cls() const { return pi >= 900 ? 'X' : pi >= 800 ? 'S' : pi >= 700 ? 'A' : pi >= 600 ? 'B' : pi >= 500 ? 'C' : 'D'; }
};
Perf estimatePerf(const phys::CarSpec& spec, const phys::Geometry* geometry);

// imagens disponíveis para adesivos (pasta decals/) e cópia de uma imagem do computador para lá
std::vector<std::string> decalFiles();
std::string importDecal(const std::string& path);  // devolve o nome do arquivo copiado ("" = erro)
// matriz do adesivo (espaço do carro → u, v, profundidade); aspect = largura/altura da imagem
mat4 stickerMatrix(const Sticker& st, float aspect);
// aplica a pintura escolhida (−1 = original) nos materiais da carroceria
void applyLivery(LoadedCar& car, const CarShop& shop);
