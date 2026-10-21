// Catálogo de carros da garagem. Para adicionar um carro: um GLB, uma miniatura, um spec de física
// e o bloco `visual`, que diz ao carregador quais materiais do GLB são o quê
// (padrões: "nome" = exato, "prefixo*" = começa com).
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../core/math3d.h"
#include "../physics/carspec.h"

struct VisualCfg {
  float wheelbase = 0;  // ajusta a escala pelo entre-eixos real
  std::vector<std::string> splitWheels, tires, frontHint, headlights, tailLights, brakeGlow, discs, rimBlur;
  std::vector<std::string> steeringWheel, steeringWheelNodes, glass, paint;
  float headlightGlow = 0;
  bool tailLightUseMap = false;
  bool fixBodyMetal = false;
  struct Cockpit { vec3 eye, helmet; };
  std::optional<Cockpit> cockpit;
  std::optional<vec3> hoodEye;
};

struct CarEntry {
  std::string id, maker, name;
  int year;
  std::string category, model, thumb, logo;
  const phys::CarSpec* spec;
  VisualCfg visual;
  std::vector<std::pair<std::string, std::string>> figures;
  std::vector<std::pair<std::string, float>> ratings;
};

const std::vector<CarEntry>& carCatalog();
