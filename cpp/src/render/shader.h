// Programa GLSL com cache de uniformes.
#pragma once
#include <string>
#include <unordered_map>

#include "../core/math3d.h"
#include "../gl/gl.h"

class Shader {
 public:
  Shader() = default;
  Shader(const std::string& vs, const std::string& fs, const std::string& defines = "");
  ~Shader();
  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;
  Shader(Shader&& o) noexcept { *this = std::move(o); }
  Shader& operator=(Shader&& o) noexcept;

  void use() const { glUseProgram(prog); }
  GLint loc(const char* name);
  void set(const char* n, int v) { glUniform1i(loc(n), v); }
  void set(const char* n, bool v) { glUniform1i(loc(n), v ? 1 : 0); }
  void set(const char* n, float v) { glUniform1f(loc(n), v); }
  void set(const char* n, vec2 v) { glUniform2f(loc(n), v.x, v.y); }
  void set(const char* n, const vec3& v) { glUniform3f(loc(n), v.x, v.y, v.z); }
  void set(const char* n, const vec4& v) { glUniform4f(loc(n), v.x, v.y, v.z, v.w); }
  void set(const char* n, const mat4& m) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, m.e); }
  GLuint id() const { return prog; }

 private:
  GLuint prog = 0;
  std::unordered_map<const char*, GLint> cache;  // chave = endereço do literal
};

// Triângulo de tela cheia (passes de pós-processamento).
void drawFullscreen();

// Funções GLSL comuns (tone mapping do three.js, sRGB, ruído).
extern const char* GLSL_COMMON;
// Vertex shader do triângulo de tela cheia: saída vUv.
extern const char* GLSL_FULLSCREEN_VS;
