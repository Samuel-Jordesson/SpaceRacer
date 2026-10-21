// Câmera perspectiva (PerspectiveCamera do three.js).
#pragma once
#include "../core/math3d.h"

struct Camera {
  vec3 position;
  quat quaternion;
  vec3 up{0, 1, 0};
  float fov = 50, aspect = 1, near = 0.1f, far = 2000;
  mat4 matrixWorld, view, proj;

  void lookAt(const vec3& target) {
    quaternion = quatFromMatrix(mat4::lookAt(position, target, up));
  }
  void update() {
    matrixWorld = mat4::compose(position, quaternion, vec3(1));
    view = matrixWorld.inverse();
    proj = mat4::perspective(fov, aspect, near, far);
  }
  vec3 forward() const { return quaternion.rotate({0, 0, -1}); }
};
