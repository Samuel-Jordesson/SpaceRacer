// Câmeras: perseguição (com mola), capô, cockpit e livre (órbita com o mouse).
#pragma once
#include <string>
#include <vector>

#include "../core/scene.h"
#include "../physics/vehicle.h"
#include "orbit.h"

class CameraRig {
 public:
  explicit CameraRig(Camera* cam);
  const std::string& name() const { return modes[mode]; }
  void next();
  // carObj = nó do carro (posição/rotação da física); visual = filho com o modelo
  void update(float dt, Node& carObj, Node& visual, const phys::Vehicle& car);

  Camera* camera;
  std::vector<std::string> modes{"Perseguição", "Perseguição longe", "Capô", "Cockpit", "Livre"};
  int mode = 0;
  OrbitControls orbit;
  vec3 pos{0, 1.65f, 5.6f};  // deslocamento da câmera em relação ao carro
  float yaw = 0;
  vec3 head;
  // posições no referencial do modelo (frente = −z); cada carro define as suas
  vec3 cockpitEye{-0.33f, 0.93f, -0.14f};
  vec3 hoodEye{0, 1.0f, -0.75f};
  float t = 0;
  vec3 lastCar;
  std::vector<Node*> hideInCockpit;
};
