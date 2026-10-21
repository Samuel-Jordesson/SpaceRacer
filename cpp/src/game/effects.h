// Marcas de pneu no asfalto (buffer circular de quads) e fumaça de pneu (partículas que nascem
// no contato quando o pneu desliza, herdam parte da velocidade do carro, sobem e se expandem).
#pragma once
#include <map>
#include <memory>
#include <random>
#include <string>

#include "../render/renderer.h"

class SkidMarks : public CustomDrawable {
 public:
  SkidMarks();
  ~SkidMarks() override;
  // id da roda, ponto de contato, direção lateral e intensidade 0..1
  void add(const std::string& id, const vec3& p, const vec3& side, float intensity, float width = 0.3f);
  void clear();
  void draw(const RenderContext& ctx) override;

 private:
  static const int MAX = 6000;
  struct Last { float x, y, z, xr, zr, a; };
  std::vector<float> pos, alpha;
  std::map<std::string, Last> last;
  int next = 0;
  int dirtyMin = MAX, dirtyMax = -1;
  bool dirtyAll = true;
  GLuint vao = 0, vbo[2] = {0, 0}, ebo = 0;
  std::unique_ptr<Shader> shader;
};

class TireSmoke : public CustomDrawable {
 public:
  TireSmoke();
  ~TireSmoke() override;
  void emit(const std::string& id, const vec3& pos, const vec3& vel, float intensity, float dt);
  void update(float dt, float fovDeg, float viewportH);
  void clear();
  void draw(const RenderContext& ctx) override;

 private:
  static const int MAX = 700;
  std::vector<float> p, v, age, life, size, alpha, seed;
  std::map<std::string, float> acc;
  int next = 0;
  float uScale = 500, uMaxSize = 300;
  std::mt19937 rng{1234};
  float rnd() { return std::uniform_real_distribution<float>(0, 1)(rng); }
  GLuint vao = 0, vbo = 0;
  std::vector<float> gpu;
  std::unique_ptr<Shader> shader;
};
