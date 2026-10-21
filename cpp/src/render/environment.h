// Iluminação baseada em imagem: cubemap pré-filtrado (GGX) para reflexos e cubemap de
// irradiância para a luz difusa, o mesmo papel do PMREMGenerator do three.js.
#pragma once
#include <memory>

#include "../core/scene.h"

class Environment {
 public:
  ~Environment();
  GLuint specular = 0;   // cubemap com mips por rugosidade
  GLuint irradiance = 0; // média cosseno-ponderada da radiância (E/π)
  int specularMips = 1;

  // a partir de uma textura equirretangular em float (céu HDRI)
  static std::unique_ptr<Environment> fromEquirect(Texture& equirect, int cubeSize);
  // RoomEnvironment do three.js: sala com caixas e seis softboxes (estúdio)
  static std::unique_ptr<Environment> room();

 private:
  static std::unique_ptr<Environment> build(GLuint source, int size);
};
