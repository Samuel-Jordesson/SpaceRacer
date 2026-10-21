#include "gl.h"

#include <cstdio>
#include <cstring>

#define SR_DEFINE(type, name) type sr_##name = nullptr;
SR_GL_FUNCS(SR_DEFINE)
#undef SR_DEFINE

namespace gl {

static float s_aniso = 1;

bool load(void* (*getProc)(const char*)) {
  bool ok = true;
#define SR_LOAD(type, name)                                   \
  sr_##name = reinterpret_cast<type>(getProc(#name));         \
  if (!sr_##name) {                                           \
    std::fprintf(stderr, "[gl] função ausente: %s\n", #name); \
    ok = false;                                               \
  }
  SR_GL_FUNCS(SR_LOAD)
#undef SR_LOAD
  if (ok) {
    // anisotropia: núcleo no 4.6, extensão GL_EXT_texture_filter_anisotropic antes disso
    float a = 1;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &a);
    while (glGetError() != GL_NO_ERROR) {}
    s_aniso = a > 1 ? a : 1;
  }
  return ok;
}

float maxAnisotropy() { return s_aniso; }

}  // namespace gl
