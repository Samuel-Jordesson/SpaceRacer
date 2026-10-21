#!/usr/bin/env bash
# Compila a versão nativa. Usa o CMake do sistema; se não houver, baixa um CMake portátil
# para cpp/.tools (não precisa de sudo). O SDL3 vem do sistema ou é baixado pelo CMake.
set -euo pipefail
cd "$(dirname "$0")"

CMAKE="$(command -v cmake || true)"
if [ -z "$CMAKE" ]; then
  VER=3.31.6
  DIR=".tools/cmake-$VER-linux-x86_64"
  if [ ! -x "$DIR/bin/cmake" ]; then
    echo "CMake não encontrado; baixando o CMake $VER para cpp/.tools ..."
    mkdir -p .tools
    curl -fsSL "https://github.com/Kitware/CMake/releases/download/v$VER/cmake-$VER-linux-x86_64.tar.gz" | tar xz -C .tools
  fi
  CMAKE="$DIR/bin/cmake"
fi

"$CMAKE" -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
"$CMAKE" --build build -j"$(nproc 2>/dev/null || echo 4)"
echo
echo "Pronto: ./space_racer (na pasta do projeto)  ·  testes da física: ./sr_physics_test"
