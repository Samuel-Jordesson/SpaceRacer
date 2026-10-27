cmake_minimum_required(VERSION 3.20)
project(SpaceRacer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- SDL3
# 1) SDL3 instalado (libsdl3-dev, vcpkg, Homebrew...)
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
  include(FetchContent)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.2/SDL3-3.4.2.tar.gz)
  # 2) Linux com a biblioteca SDL3 já instalada mas sem o pacote -dev: usa só os cabeçalhos
  find_library(SDL3_SYSTEM_LIB NAMES SDL3 libSDL3.so.0 PATHS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib /usr/local/lib)
  if(UNIX AND NOT APPLE AND SDL3_SYSTEM_LIB)
    FetchContent_GetProperties(SDL3)
    if(NOT sdl3_POPULATED)
      FetchContent_Populate(SDL3)
    endif()
    add_library(SDL3::SDL3 SHARED IMPORTED)
    set_target_properties(SDL3::SDL3 PROPERTIES
      IMPORTED_LOCATION "${SDL3_SYSTEM_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/include")
    message(STATUS "SDL3: biblioteca do sistema (${SDL3_SYSTEM_LIB}) + cabeçalhos baixados")
  else()
    # 3) compila o SDL3 junto (Windows, macOS, Linux sem SDL3)
    # extensões do X11 que o jogo não usa (protetor de tela, simulação de entrada): sem elas o
    # SDL3 compila num Ubuntu que não tem libxss-dev / libxtst-dev
    set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
    message(STATUS "SDL3: compilado a partir do código-fonte")
  endif()
endif()

# ---------------------------------------------------------------- física (sem dependências)
add_library(sr_physics STATIC
  src/physics/carspec.cpp
  src/physics/vehicle.cpp)
target_include_directories(sr_physics PUBLIC src)

add_executable(sr_physics_test tests/sim_test.cpp)
target_link_libraries(sr_physics_test PRIVATE sr_physics)

# ---------------------------------------------------------------- jogo
add_executable(space_racer
  src/main.cpp
  src/third_party_impl.cpp
  src/gl/gl.cpp
  src/core/scene.cpp
  src/core/assets.cpp
  src/core/gltf_loader.cpp
  src/render/shader.cpp
  src/render/environment.cpp
  src/render/renderer.cpp
  src/render/postfx.cpp
  src/ui/ui.cpp
  src/ui/glyphs.cpp
  src/game/cars.cpp
  src/game/car_model.cpp
  src/game/world.cpp
  src/game/effects.cpp
  src/game/input.cpp
  src/game/orbit.cpp
  src/game/cameras.cpp
  src/game/audio.cpp
  src/game/hud.cpp
  src/game/game.cpp
  src/game/garage.cpp
  src/game/cinematic.cpp
  src/game/settings.cpp
  src/game/track.cpp
  src/game/maps.cpp
  src/game/workshop.cpp
  src/game/race.cpp)
target_include_directories(space_racer PRIVATE src third_party)
target_link_libraries(space_racer PRIVATE sr_physics SDL3::SDL3)
if(WIN32)
  set_target_properties(space_racer PROPERTIES WIN32_EXECUTABLE ON)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(space_racer PRIVATE -Wall -Wno-missing-field-initializers)
  set_source_files_properties(src/third_party_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
# o executável fica na pasta do projeto, ao lado de assets/
set_target_properties(space_racer sr_physics_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
