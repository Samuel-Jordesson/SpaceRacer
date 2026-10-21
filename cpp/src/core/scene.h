// Grafo de cena mínimo (equivalente ao Object3D/Mesh/Material do three.js usado pelo jogo).
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../gl/gl.h"
#include "math3d.h"

// ---------------------------------------------------------------- textura
struct Texture {
  GLuint id = 0;
  int width = 0, height = 0;
  // dados pendentes (decodificados em outra thread, enviados à GPU na thread principal)
  std::vector<uint8_t> pixels;  // RGBA8
  std::vector<uint16_t> halfPixels;  // RGBA16F
  std::vector<float> floatPixels;  // RGBA32F
  bool srgb = false, mipmaps = true, repeat = true, linear = true;
  float anisotropy = 1;
  void upload();
  void release();
  ~Texture();
};
using TexturePtr = std::shared_ptr<Texture>;

struct TexRef {
  TexturePtr tex;
  int uv = 0;  // canal de UV (0 ou 1)
  explicit operator bool() const { return (bool)tex; }
};

// ---------------------------------------------------------------- geometria
struct Geometry {
  std::vector<vec3> pos, nrm;
  std::vector<vec4> tan;
  std::vector<vec2> uv0, uv1;
  std::vector<uint32_t> idx;
  bool hasTangents = false;
  // limites locais
  vec3 sphereCenter;
  float sphereRadius = 0;
  Box3 box;
  GLuint vao = 0, vbo[5] = {0, 0, 0, 0, 0}, ebo = 0;
  GLsizei count = 0;
  void computeBounds();
  void upload();
  void release();
  ~Geometry();
};
using GeometryPtr = std::shared_ptr<Geometry>;

// ---------------------------------------------------------------- material (MeshStandard/MeshPhysical/MeshBasic)
struct Material {
  enum class Kind { Standard, Basic };
  Kind kind = Kind::Standard;
  std::string name;
  vec3 color{1, 1, 1};
  float opacity = 1;
  TexRef map;
  vec2 mapRepeat{1, 1};
  float metalness = 1, roughness = 1;
  TexRef metalnessMap, roughnessMap;  // azul = metal, verde = rugosidade
  TexRef normalMap;
  vec2 normalScale{1, 1};
  TexRef aoMap;
  float aoMapIntensity = 1;
  vec3 emissive{0, 0, 0};
  TexRef emissiveMap;
  float emissiveIntensity = 1;
  bool transparent = false, doubleSided = false, depthWrite = true, additive = false;
  float alphaTest = 0;
  // MeshPhysicalMaterial
  bool physical = false;
  float clearcoat = 0, clearcoatRoughness = 0;
  TexRef clearcoatMap, clearcoatRoughnessMap, clearcoatNormalMap;
  vec2 clearcoatNormalScale{1, 1};
  float specularIntensity = 1, ior = 1.5f;
  vec3 specularColor{1, 1, 1};
  TexRef specularIntensityMap, specularColorMap;
  float envMapIntensity = 1;
  bool glowQuad = false;  // quad de brilho do disco de freio (userData do JS)
  bool gltf = false;      // material do GLTFLoader (inverte Y do normal map sem tangentes)
  float asphaltTile = 0;  // > 0: shader do asfalto (UV em metros, anti-repetição)
  bool decal = false;     // decalque sobre outra superfície (deslocamento de profundidade)
  // textura de detalhe repetida em coordenadas do mundo (XZ), multiplicada pela base
  TexRef detailMap;
  float detailScale = 4;  // metros por repetição
  float detailGain = 1;   // compensa a base clara
};
using MaterialPtr = std::shared_ptr<Material>;

// ---------------------------------------------------------------- nó
struct Node : std::enable_shared_from_this<Node> {
  std::string name;
  Node* parent = nullptr;
  std::vector<std::shared_ptr<Node>> children;
  vec3 position;
  quat quaternion;
  vec3 scale{1, 1, 1};
  mat4 matrix, matrixWorld;
  bool visible = true;
  // malha
  GeometryPtr geometry;
  MaterialPtr material;
  bool castShadow = false, receiveShadow = false, noShadow = false;
  int renderOrder = 0;

  bool isMesh() const { return (bool)geometry; }
  void add(const std::shared_ptr<Node>& child);
  void remove(Node* child);
  // como Object3D.attach: muda de pai mantendo a transformação no mundo
  void attach(const std::shared_ptr<Node>& child);
  void setRotationX(float a) { quaternion = quat::fromAxisAngle({1, 0, 0}, a); }
  void setRotationY(float a) { quaternion = quat::fromAxisAngle({0, 1, 0}, a); }
  void updateMatrix() { matrix = mat4::compose(position, quaternion, scale); }
  void updateMatrixWorld();
  vec3 localToWorld(const vec3& p) const { return matrixWorld.point(p); }
  void traverse(const std::function<void(Node&)>& fn);
};
using NodePtr = std::shared_ptr<Node>;

inline NodePtr makeNode(const std::string& name = "") {
  auto n = std::make_shared<Node>();
  n->name = name;
  return n;
}

// limites no mundo a partir dos vértices (Box3.setFromObject(obj, true)); exige matrixWorld atualizada
Box3 worldBox(Node& n);

struct RayHitInfo {
  float distance;
  vec3 point;
  Node* object;
};
// Raycaster.intersectObject(root, true): todas as interseções ordenadas por distância
std::vector<RayHitInfo> raycast(Node& root, const vec3& origin, const vec3& dir, float near, float far);
