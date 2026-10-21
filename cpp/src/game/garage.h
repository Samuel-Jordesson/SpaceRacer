// Garagem: estúdio branco infinito com piso de porcelanato polido (reflexo planar real),
// iluminação de estúdio (softboxes do RoomEnvironment) e o carro no centro.
#pragma once
#include <memory>

#include "../render/postfx.h"
#include "../render/renderer.h"
#include "car_model.h"
#include "orbit.h"
#include "settings.h"

class Cinematic;

// Reflector do three: espelho no plano do chão que mostra a cena refletida
class Mirror : public CustomDrawable {
 public:
  Mirror(float size, float y, vec3 color);
  ~Mirror() override;
  void resize(int w, int h);
  void renderReflection(Renderer& r, Scene& scene, const Camera& cam, const std::function<bool(const Node&)>& exclude);
  void draw(const RenderContext& ctx) override;

 private:
  float size, y;
  vec3 color;
  int w = 0, h = 0;
  GLuint msFbo = 0, msColor = 0, msDepth = 0, fbo = 0, tex = 0;
  GLuint vao = 0, vbo = 0;
  std::unique_ptr<Shader> shader;
};

class Garage {
 public:
  explicit Garage(Renderer& r);
  ~Garage();
  void setActive(bool on);
  void showCar(const std::shared_ptr<LoadedCar>& loaded);
  void reclaimCar();
  void resize(int winW, int winH, float dpr);
  void frame(float dt);
  void render(float dt);
  void applySettings(const Settings& st);

  // mouse no canvas
  void mouseDown(int button, float x, float y);
  void mouseMove(float x, float y);
  void mouseUp();
  void wheel(float dy);

  Renderer& renderer;
  Scene scene;
  Camera camera;
  OrbitControls controls;
  PostFX fx;
  NodePtr turntable;
  std::shared_ptr<LoadedCar> car;
  Cinematic* cine = nullptr;
  bool active = false;
  float intro = 1, baseDist = 7.2f;
  float carScale = 1;  // comprimento do carro em relação a um GT3 típico (4,6 m)
  void updateDistance();
  int winW = 1, winH = 1;
  float dpr = 1;

 private:
  DirectionalLight key;
  std::unique_ptr<Environment> env;
  std::unique_ptr<Mirror> mirror;
  NodePtr floor, disc;
  float resumeTimer = -1;
};

void resetPose(LoadedCar& loaded);
