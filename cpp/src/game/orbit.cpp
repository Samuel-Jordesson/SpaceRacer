#include "orbit.h"

#include <cmath>

bool OrbitControls::pointerDown(int button, float x, float y) {
  if (!enabled) return false;
  if (button == 1) state = Rotate;
  else if (button == 3 && enablePan) state = Pan;
  else return false;
  lastX = x;
  lastY = y;
  if (onStart) onStart();
  return true;
}

void OrbitControls::pointerMove(float x, float y, float viewH) {
  if (!enabled || state == None) return;
  float dx = x - lastX, dy = y - lastY;
  lastX = x;
  lastY = y;
  if (state == Rotate) {
    dTheta -= 2 * (float)M_PI * dx / viewH * rotateSpeed;
    dPhi -= 2 * (float)M_PI * dy / viewH * rotateSpeed;
  } else if (state == Pan) {
    camera->update();
    vec3 offset = camera->position - target;
    float targetDistance = length(offset) * std::tan(camera->fov / 2 * (float)M_PI / 180);
    // panLeft: coluna 0 da matriz da câmera; panUp (screenSpacePanning): coluna 1
    panOffset += camera->matrixWorld.col(0) * (-2 * dx * targetDistance / viewH);
    panOffset += camera->matrixWorld.col(1) * (2 * dy * targetDistance / viewH);
  }
}

void OrbitControls::pointerUp() {
  if (state == None) return;
  state = None;
  if (onEnd) onEnd();
}

void OrbitControls::wheel(float deltaY) {
  if (!enabled || deltaY == 0) return;
  if (onStart) onStart();
  float s = std::pow(0.95f, zoomSpeed * std::fabs(deltaY * 0.01f));
  if (deltaY < 0) scale *= s;  // aproxima
  else scale /= s;
  if (onEnd) onEnd();
}

void OrbitControls::update(float deltaTime) {
  vec3 offset = camera->position - target;
  // esférico com Y para cima
  float radius = length(offset);
  float theta = radius == 0 ? 0 : std::atan2(offset.x, offset.z);
  float phi = radius == 0 ? 0 : std::acos(std::fmax(-1.f, std::fmin(1.f, offset.y / radius)));
  if (autoRotate && state == None && deltaTime >= 0) dTheta -= 2 * (float)M_PI / 60 * autoRotateSpeed * deltaTime;
  if (enableDamping) {
    theta += dTheta * dampingFactor;
    phi += dPhi * dampingFactor;
  } else {
    theta += dTheta;
    phi += dPhi;
  }
  phi = std::fmax(minPolarAngle, std::fmin(maxPolarAngle, phi));
  const float EPS = 1e-6f;
  phi = std::fmax(EPS, std::fmin((float)M_PI - EPS, phi));
  if (enableDamping) target += panOffset * dampingFactor;
  else target += panOffset;
  radius = std::fmax(minDistance, std::fmin(maxDistance, radius * scale));
  float sp = std::sin(phi) * radius;
  offset = vec3(sp * std::sin(theta), std::cos(phi) * radius, sp * std::cos(theta));
  camera->position = target + offset;
  camera->up = vec3(0, 1, 0);
  camera->lookAt(target);
  if (enableDamping) {
    dTheta *= 1 - dampingFactor;
    dPhi *= 1 - dampingFactor;
    panOffset *= 1 - dampingFactor;
  } else {
    dTheta = dPhi = 0;
    panOffset = vec3();
  }
  scale = 1;
}
