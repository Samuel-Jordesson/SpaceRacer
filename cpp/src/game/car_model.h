// Carrega o GLB, coloca o carro no referencial padrão (frente = −Z, cima = +Y, chão em y = 0,
// centro do entre-eixos na origem, escala em metros) e separa as rodas em pivôs de esterço/giro.
// Os GLB do Sketchfab vêm com orientação/escala arbitrárias e as rodas misturadas na carroceria,
// então tudo é descoberto pela geometria, a partir dos nomes de material dados em VisualCfg.
#pragma once
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "../core/gltf_loader.h"
#include "../physics/carspec.h"
#include "cars.h"

struct WheelPivot {
  NodePtr steer, spin;
  vec3 base;
  float radius = 0;
  std::vector<MaterialPtr> glow, blur;
};

struct LoadedCar {
  NodePtr root;
  phys::Geometry geometry;
  std::map<std::string, WheelPivot> pivots;  // FL, FR, RL, RR
  std::vector<MaterialPtr> tailLights;
  struct SteeringWheel { NodePtr pivot; vec3 axis; };
  std::optional<SteeringWheel> steeringWheel;
  std::optional<VisualCfg::Cockpit> cockpit;
  std::optional<vec3> hoodEye;
  GltfAsset attribution;
  // carroceria pintável (oficina): materiais e valores originais, e a caixa do carro em metros
  struct PaintOrig { TexRef map; vec3 color; float metalness, roughness, clearcoat, clearcoatRoughness; };
  std::vector<MaterialPtr> paintMats;
  std::vector<PaintOrig> paintOrig;
  std::vector<MaterialPtr> rimMats;  // aros (giram com a roda; fora pneu, disco e brilho do freio)
  std::vector<PaintOrig> rimOrig;
  Box3 box;
  std::shared_ptr<DecalSet> decals;  // adesivos atuais (textura em camadas)
  std::string decalKey;              // arquivos que estão na textura
  // envia texturas e malhas à GPU (thread principal)
  void upload();
};

// cópia independente do carro (peças e materiais próprios, malhas e texturas compartilhadas):
// usada para os adversários da corrida
std::shared_ptr<LoadedCar> cloneCar(const LoadedCar& src);

// pode rodar fora da thread de OpenGL; lança std::runtime_error em caso de erro
std::shared_ptr<LoadedCar> loadCar(const std::string& path, const VisualCfg& cfg, const std::function<void(float)>& onProgress);
