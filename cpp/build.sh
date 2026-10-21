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

# Linux: o SDL3 só ganha driver de som se os cabeçalhos de áudio estiverem instalados
AUDIO=""
if [ "$(uname)" = "Linux" ]; then
  [ -f /usr/include/alsa/asoundlib.h ] && AUDIO="$AUDIO alsa"
  [ -f /usr/include/pulse/pulseaudio.h ] && AUDIO="$AUDIO pulse"
  [ -d /usr/include/pipewire-0.3 ] && AUDIO="$AUDIO pipewire"
  if [ -z "$AUDIO" ]; then
    echo "AVISO: nenhum cabeçalho de áudio encontrado; o jogo vai compilar, mas sem som."
    echo "       Para ter som: sudo apt install libasound2-dev libpulse-dev  e rode este script de novo."
  fi
  # se os pacotes de áudio mudaram desde a última compilação, refaz a detecção do SDL3
  if [ -f build/CMakeCache.txt ] && [ "$(cat build/.audio 2>/dev/null)" != "$AUDIO" ]; then
    echo "Pacotes de áudio mudaram; reconfigurando o SDL3 ..."
    rm -rf build/CMakeCache.txt build/CMakeFiles
  fi
fi

"$CMAKE" -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
[ "$(uname)" = "Linux" ] && echo "$AUDIO" > build/.audio
"$CMAKE" --build build -j"$(nproc 2>/dev/null || echo 4)"
echo
echo "Pronto: ./space_racer (na pasta do projeto)  ·  testes da física: ./sr_physics_test"
