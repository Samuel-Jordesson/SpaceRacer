// Localização dos arquivos do jogo e carregamento de imagens.
#pragma once
#include <string>

#include "scene.h"

// raiz do projeto (pasta com assets/): procurada a partir do diretório atual e do executável
void initAssetRoot(const char* argv0);
std::string assetPath(const std::string& rel);

// decodifica uma imagem (JPG/PNG) em RGBA8; a textura fica pendente até upload()
TexturePtr loadImage(const std::string& rel, bool srgb);

// meia precisão (para texturas HDR)
uint16_t floatToHalf(float f);
