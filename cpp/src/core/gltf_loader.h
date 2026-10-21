// Carregador de GLB com as mesmas regras de material do GLTFLoader do three.js.
// Roda inteiro fora da thread de OpenGL (as texturas ficam pendentes até Texture::upload()).
#pragma once
#include <functional>
#include <string>

#include "scene.h"

struct GltfAsset {
  NodePtr scene;
  std::string title, author, license, source;  // asset.extras (créditos do Sketchfab)
};

// onProgress recebe 0..1. Lança std::runtime_error em caso de erro.
// textureDir (opcional): pasta com versões em alta resolução das imagens (mesmo nome da imagem do GLB);
// quando o arquivo existe, ele substitui a imagem embutida (o Sketchfab reduz as embutidas).
GltfAsset loadGltf(const std::string& path, const std::function<void(float)>& onProgress, const std::string& textureDir = "");
