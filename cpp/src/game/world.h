// Cena da pista: céu HDRI real (Poly Haven, CC0), sol direcional alinhado com o sol da foto,
// reflexos por image-based lighting e asfalto PBR escaneado sem repetição visível.
#pragma once
#include <functional>
#include <memory>

#include "../render/renderer.h"
#include "track.h"

class World {
 public:
  // parte pesada (decodificar HDR e texturas), pode rodar em outra thread
  void loadCPU(const std::function<void(float)>& onProgress);
  // envio à GPU e montagem da cena (thread principal)
  void finishGPU();

  void setShadowQuality(int size) { sun.mapSize = size; }
  // a sombra acompanha o carro
  void follow(const vec3& p);
  // troca a pista: nullptr = pista de teste (asfalto infinito)
  void setTrack(TrackMap* track);
  phys::Ground* physicsGround() { return track ? (phys::Ground*)&track->ground : &flat; }
  TrackMap* currentTrack() { return track; }
  vec3 spawnPos() const { return track ? track->spawnPos : vec3(); }
  float spawnHeading() const { return track ? track->spawnHeading : 0.f; }

  Scene scene;
  DirectionalLight sun;
  vec3 sunDir;

 private:
  std::unique_ptr<Environment> env;
  NodePtr flatGround;
  phys::FlatGround flat;
  TrackMap* track = nullptr;
  TexturePtr background, envTex, asphaltMap, asphaltNormal, asphaltRough;
  vec3 horizon;
};
