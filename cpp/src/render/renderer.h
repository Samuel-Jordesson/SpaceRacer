// Renderizador forward PBR: sombras da luz direcional, fundo (cor ou céu HDRI), malhas opacas,
// objetos especiais (espelho, marcas de pneu, fumaça) e malhas transparentes ordenadas.
#pragma once
#include <functional>
#include <memory>
#include <vector>

#include "../core/scene.h"
#include "camera.h"
#include "environment.h"
#include "shader.h"

struct DirectionalLight {
  vec3 color{1, 1, 1};
  float intensity = 1;
  vec3 position{0, 1, 0}, target;
  bool castShadow = false;
  int mapSize = 2048;
  float left = -5, right = 5, top = 5, bottom = -5, near = 0.5f, far = 500;
  float bias = 0, normalBias = 0;
  float softness = 1;  // raio do filtro PCF em texels (borda da sombra)
  // GPU
  GLuint fbo = 0, depthTex = 0;
  int allocated = 0;
  mat4 shadowMatrix;
  ~DirectionalLight();
};

struct Fog {
  // Atmosphere: exponencial com queda pela altura e brilho na direção do sol
  enum Type { None, Linear, Exp2, Atmosphere } type = None;
  vec3 color;
  float near = 1, far = 1000, density = 0.00025f;
  float base = 0, falloff = 0.01f;  // Atmosphere: altura de referência (m) e queda por metro
  vec3 sunColor;
}; 

struct RenderContext {
  mat4 view, proj;
  vec3 camPos;
  float fov = 50;
  Fog fog;
  int viewportW = 1, viewportH = 1;
  bool mirrored = false;
  bool clip = false;
  vec4 clipPlane;
};

struct CustomDrawable {
  virtual ~CustomDrawable() = default;
  virtual void draw(const RenderContext& ctx) = 0;
};

struct Scene {
  NodePtr root = makeNode("scene");
  vec3 background{0, 0, 0};
  Texture* backgroundTex = nullptr;  // equirretangular HDR
  float backgroundIntensity = 1;
  Environment* env = nullptr;
  float envIntensity = 1;
  DirectionalLight* light = nullptr;
  bool hemi = false;
  vec3 hemiSky, hemiGround;
  float hemiIntensity = 1;
  Fog fog;
  std::vector<CustomDrawable*> opaqueCustom;       // depois das malhas opacas
  std::vector<CustomDrawable*> transparentFirst;   // antes das malhas transparentes
  std::vector<CustomDrawable*> transparentLast;    // por último
};

struct RenderOptions {
  bool clip = false;
  vec4 clipPlane;           // no mundo: desenha onde dot(p, plano) >= 0
  bool mirrored = false;    // vista refletida: inverte o sentido das faces
  std::function<bool(const Node&)> exclude;
  bool customs = true;
};

class Renderer {
 public:
  Renderer();
  void renderShadows(Scene& scene);
  // desenha no framebuffer atual (viewport já definido); limpa cor e profundidade
  void render(Scene& scene, const Camera& camera, int viewportW, int viewportH, const RenderOptions& opt = {});
  // shader PBR pronto para objetos especiais que queiram reaproveitar os uniformes de cena
  Shader& pbr() { return *pbrShader; }

 private:
  struct Item {
    Node* node;
    float depth;
  };
  void drawMesh(Node& n, const RenderContext& ctx, Scene& scene, int pass);
  void setSceneUniforms(Scene& scene, const RenderContext& ctx);
  std::unique_ptr<Shader> pbrShader, depthShader, skyShader;
  GLuint whiteTex = 0;
  const Material* lastMaterial = nullptr;
  int lastPass = -1;
  bool msaaTarget = false;
};
