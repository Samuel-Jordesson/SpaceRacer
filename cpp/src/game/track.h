// Pistas (mapas): catálogo, carregamento do GLB do circuito e o chão físico com colisão.
//
// Para adicionar uma pista: coloque o GLB em mapas/<nome>/ e crie uma entrada em maps.cpp dizendo
// quais materiais são asfalto, zebra, grama, brita (aderência de cada um), quais são muros
// (colisão), quais usam recorte por transparência (árvores, cercas) e onde fica a largada.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../core/gltf_loader.h"
#include "../physics/vehicle.h"

struct SurfaceRule {
  std::vector<std::string> patterns;
  double mu = 1, drag = 0;  // aderência relativa e resistência extra de rolagem
};

struct MapCfg {
  std::string textureDir;                        // texturas em alta resolução (opcional)
  std::vector<SurfaceRule> surfaces;             // onde as rodas apoiam
  std::vector<std::string> walls;                // colisão lateral (faces verticais)
  std::vector<std::string> alphaTest, glass;     // correções de material
  std::vector<std::string> decals;               // faixas, marcas de pneu: mistura sobre o asfalto
  std::vector<std::string> noShadow;             // não projetam sombra (chão, decalques)
  struct Detail { std::vector<std::string> patterns; std::string texture; float scale, gain; };
  std::vector<Detail> details;                   // grama etc.: base clara × textura de detalhe
  std::vector<std::string> spawnMarker, spawnRoad;  // pórtico da largada e o asfalto em volta
  bool clockwise = true;                         // sentido da corrida (visto de cima)
  float gridOffset = 18;                         // metros antes do pórtico
};

struct MapEntry {
  std::string id, name, country, category, model, thumb;
  bool flat = false;  // pista de teste: chão de asfalto infinito
  std::vector<std::pair<std::string, std::string>> figures;
  MapCfg cfg;
};

const std::vector<MapEntry>& mapCatalog();

// Chão de uma pista feita de triângulos (grade espacial para consultas rápidas).
class TrackGround : public phys::Ground {
 public:
  struct Tri {
    float ax, ay, az, bx, by, bz, cx, cy, cz;
    float mu, drag;
  };
  void build(std::vector<Tri> ground, std::vector<Tri> walls);
  std::optional<phys::RayHit> raycast(const phys::V3& o, const phys::V3& d, double maxDist) const override;
  phys::GroundQuery heightAt(double x, double z) const override;
  phys::GroundQuery heightNear(double x, double z, double y) const override;
  bool wallContact(const phys::V3& p, double r, phys::V3& normal, double& depth) const override;
  size_t groundCount() const { return ground.size(); }
  size_t wallCount() const { return walls.size(); }

 private:
  struct Grid {
    float minX = 0, minZ = 0, cell = 4;
    int nx = 0, nz = 0;
    std::vector<uint32_t> start, items;
    void build(const std::vector<Tri>& tris, float cellSize);
    template <class F> void query(float x0, float z0, float x1, float z1, F&& f) const;
  };
  std::optional<phys::RayHit> cast(const phys::V3& o, const phys::V3& d, double maxDist) const;
  std::vector<Tri> ground, walls;
  Grid gGrid, wGrid;
};

// Pista carregada: cena 3D + chão físico + largada.
class TrackMap {
 public:
  void loadCPU(const MapEntry& entry, const std::function<void(float)>& onProgress);  // outra thread
  void upload();                                                                       // thread principal
  NodePtr root;
  TrackGround ground;
  vec3 spawnPos;
  float spawnHeading = 0;
};
