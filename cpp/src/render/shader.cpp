#include "shader.h"

#include <cstdio>
#include <stdexcept>
#include <vector>

static GLuint compile(GLenum type, const std::string& src) {
  GLuint s = glCreateShader(type);
  const char* p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> log(len + 1);
    glGetShaderInfoLog(s, len, nullptr, log.data());
    // imprime o código numerado para achar a linha do erro
    int line = 1;
    std::fprintf(stderr, "%4d ", line);
    for (char c : src) {
      std::fputc(c, stderr);
      if (c == '\n') std::fprintf(stderr, "%4d ", ++line);
    }
    std::fprintf(stderr, "\n[shader] erro de compilação:\n%s\n", log.data());
    throw std::runtime_error("erro de compilação de shader");
  }
  return s;
}

Shader::Shader(const std::string& vs, const std::string& fs, const std::string& defines) {
  std::string head = "#version 410 core\n" + defines + "\n";
  GLuint v = compile(GL_VERTEX_SHADER, head + vs);
  GLuint f = compile(GL_FRAGMENT_SHADER, head + fs);
  prog = glCreateProgram();
  glAttachShader(prog, v);
  glAttachShader(prog, f);
  glBindAttribLocation(prog, 0, "aPos");
  glBindAttribLocation(prog, 1, "aNormal");
  glBindAttribLocation(prog, 2, "aUv0");
  glBindAttribLocation(prog, 3, "aUv1");
  glBindAttribLocation(prog, 4, "aTangent");
  glLinkProgram(prog);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(prog, sizeof log, nullptr, log);
    std::fprintf(stderr, "[shader] erro de link:\n%s\n", log);
    throw std::runtime_error("erro de link de shader");
  }
}

Shader::~Shader() {
  if (prog) glDeleteProgram(prog);
}

Shader& Shader::operator=(Shader&& o) noexcept {
  if (prog) glDeleteProgram(prog);
  prog = o.prog;
  cache = std::move(o.cache);
  o.prog = 0;
  return *this;
}

GLint Shader::loc(const char* name) {
  auto it = cache.find(name);
  if (it != cache.end()) return it->second;
  GLint l = glGetUniformLocation(prog, name);
  cache.emplace(name, l);  // literais de string: o endereço é estável
  return l;
}

void drawFullscreen() {
  static GLuint vao = 0;
  if (!vao) glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

const char* GLSL_FULLSCREEN_VS = R"(
out vec2 vUv;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* GLSL_COMMON = R"(
#define PI 3.141592653589793
#define RECIPROCAL_PI 0.3183098861837907
#define RECIPROCAL_PI2 0.15915494309189535
#define EPSILON 1e-6
float pow2(float x) { return x * x; }
float pow4(float x) { float x2 = x * x; return x2 * x2; }
float luminance(vec3 rgb) { return dot(rgb, vec3(0.2126729, 0.7151522, 0.0721750)); }
vec2 equirectUv(vec3 dir) {
  float u = atan(dir.z, dir.x) * RECIPROCAL_PI2 + 0.5;
  float v = asin(clamp(dir.y, -1.0, 1.0)) * RECIPROCAL_PI + 0.5;
  return vec2(u, v);
}
vec3 linearToSRGB(vec3 c) {
  return mix(pow(c, vec3(0.41666)) * 1.055 - vec3(0.055), c * 12.92, vec3(lessThanEqual(c, vec3(0.0031308))));
}
// ACESFilmicToneMapping do three.js
vec3 RRTAndODTFit(vec3 v) {
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return a / b;
}
vec3 ACESFilmicToneMapping(vec3 color, float exposure) {
  const mat3 ACESInputMat = mat3(vec3(0.59719, 0.07600, 0.02840), vec3(0.35458, 0.90834, 0.13383), vec3(0.04823, 0.01566, 0.83777));
  const mat3 ACESOutputMat = mat3(vec3(1.60475, -0.10208, -0.00327), vec3(-0.53108, 1.10813, -0.07276), vec3(-0.07367, -0.00605, 1.07602));
  color *= exposure / 0.6;
  color = ACESInputMat * color;
  color = RRTAndODTFit(color);
  color = ACESOutputMat * color;
  return clamp(color, 0.0, 1.0);
}
// NeutralToneMapping (Khronos PBR Neutral) do three.js
vec3 NeutralToneMapping(vec3 color, float exposure) {
  const float StartCompression = 0.8 - 0.04;
  const float Desaturation = 0.15;
  color *= exposure;
  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;
  float peak = max(color.r, max(color.g, color.b));
  if (peak < StartCompression) return color;
  float d = 1. - StartCompression;
  float newPeak = 1. - d * d / (peak + d - StartCompression);
  color *= newPeak / peak;
  float g = 1. - 1. / (Desaturation * (peak - newPeak) + 1.);
  return mix(color, vec3(newPeak), g);
}
)";
