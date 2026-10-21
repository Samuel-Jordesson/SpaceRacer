// OrbitControls do three.js (r170): girar com o botão esquerdo, zoom com a roda,
// deslocar com o direito, amortecimento por quadro e auto-rotação.
#pragma once
#include <functional>

#include "../render/camera.h"

class OrbitControls {
 public:
  explicit OrbitControls(Camera* cam) : camera(cam) {}

  Camera* camera;
  vec3 target;
  bool enabled = true, enableDamping = false, enablePan = true, autoRotate = false;
  float dampingFactor = 0.05f, rotateSpeed = 1, zoomSpeed = 1, autoRotateSpeed = 2;
  float minDistance = 0, maxDistance = 1e30f;
  float minPolarAngle = 0, maxPolarAngle = (float)M_PI;
  std::function<void()> onStart, onEnd;

  // eventos do mouse (coordenadas em px; viewH = altura da janela em px)
  bool pointerDown(int button, float x, float y);
  void pointerMove(float x, float y, float viewH);
  void pointerUp();
  void wheel(float deltaY);
  bool dragging() const { return state != None; }

  // deltaTime < 0: sem auto-rotação baseada em tempo (como update() sem argumento)
  void update(float deltaTime = -1);

 private:
  enum State { None, Rotate, Pan } state = None;
  float lastX = 0, lastY = 0;
  float dTheta = 0, dPhi = 0, scale = 1;
  vec3 panOffset;
};
