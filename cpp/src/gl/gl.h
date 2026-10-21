// Carregador mínimo de OpenGL 4.1 core: ponteiros de função obtidos pelo SDL.
#pragma once
#define GL_GLEXT_PROTOTYPES_NOT
#include "../../third_party/glcorearb.h"

#define SR_GL_FUNCS(X)                                                                                  \
  X(PFNGLCLEARPROC, glClear) X(PFNGLCLEARCOLORPROC, glClearColor) X(PFNGLCLEARDEPTHPROC, glClearDepth)  \
  X(PFNGLVIEWPORTPROC, glViewport) X(PFNGLSCISSORPROC, glScissor) X(PFNGLENABLEPROC, glEnable)          \
  X(PFNGLDISABLEPROC, glDisable) X(PFNGLBLENDFUNCPROC, glBlendFunc)                                     \
  X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) X(PFNGLBLENDEQUATIONPROC, glBlendEquation)         \
  X(PFNGLDEPTHFUNCPROC, glDepthFunc) X(PFNGLDEPTHMASKPROC, glDepthMask) X(PFNGLCOLORMASKPROC, glColorMask) \
  X(PFNGLCULLFACEPROC, glCullFace) X(PFNGLFRONTFACEPROC, glFrontFace)                                   \
  X(PFNGLPOLYGONOFFSETPROC, glPolygonOffset) X(PFNGLGETINTEGERVPROC, glGetIntegerv)                     \
  X(PFNGLGETFLOATVPROC, glGetFloatv) X(PFNGLGETSTRINGPROC, glGetString) X(PFNGLGETERRORPROC, glGetError) \
  X(PFNGLPIXELSTOREIPROC, glPixelStorei) X(PFNGLREADPIXELSPROC, glReadPixels) X(PFNGLFINISHPROC, glFinish) \
  X(PFNGLDRAWARRAYSPROC, glDrawArrays) X(PFNGLDRAWELEMENTSPROC, glDrawElements)                         \
  X(PFNGLDRAWARRAYSINSTANCEDPROC, glDrawArraysInstanced)                                                \
  X(PFNGLGENTEXTURESPROC, glGenTextures) X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                   \
  X(PFNGLBINDTEXTUREPROC, glBindTexture) X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                     \
  X(PFNGLTEXIMAGE2DPROC, glTexImage2D) X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D)                       \
  X(PFNGLTEXPARAMETERIPROC, glTexParameteri) X(PFNGLTEXPARAMETERFPROC, glTexParameterf)                 \
  X(PFNGLTEXPARAMETERFVPROC, glTexParameterfv) X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)             \
  X(PFNGLTEXIMAGE2DMULTISAMPLEPROC, glTexImage2DMultisample)                                            \
  X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)   \
  X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
  X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                                        \
  X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) \
  X(PFNGLDRAWBUFFERSPROC, glDrawBuffers) X(PFNGLDRAWBUFFERPROC, glDrawBuffer) X(PFNGLREADBUFFERPROC, glReadBuffer) \
  X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers) \
  X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                                                      \
  X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample)                          \
  X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                                                \
  X(PFNGLGENBUFFERSPROC, glGenBuffers) X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                       \
  X(PFNGLBINDBUFFERPROC, glBindBuffer) X(PFNGLBUFFERDATAPROC, glBufferData)                             \
  X(PFNGLBUFFERSUBDATAPROC, glBufferSubData) X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)             \
  X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)   \
  X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                        \
  X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)                                      \
  X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) X(PFNGLVERTEXATTRIBDIVISORPROC, glVertexAttribDivisor) \
  X(PFNGLVERTEXATTRIB4FPROC, glVertexAttrib4f) X(PFNGLVERTEXATTRIB2FPROC, glVertexAttrib2f)             \
  X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLDELETESHADERPROC, glDeleteShader)                     \
  X(PFNGLSHADERSOURCEPROC, glShaderSource) X(PFNGLCOMPILESHADERPROC, glCompileShader)                   \
  X(PFNGLGETSHADERIVPROC, glGetShaderiv) X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)               \
  X(PFNGLCREATEPROGRAMPROC, glCreateProgram) X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                 \
  X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram)                       \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)           \
  X(PFNGLUSEPROGRAMPROC, glUseProgram) X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)             \
  X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X(PFNGLUNIFORM1IPROC, glUniform1i)               \
  X(PFNGLUNIFORM1FPROC, glUniform1f) X(PFNGLUNIFORM2FPROC, glUniform2f) X(PFNGLUNIFORM3FPROC, glUniform3f) \
  X(PFNGLUNIFORM4FPROC, glUniform4f) X(PFNGLUNIFORM1FVPROC, glUniform1fv) X(PFNGLUNIFORM2FVPROC, glUniform2fv) \
  X(PFNGLUNIFORM3FVPROC, glUniform3fv) X(PFNGLUNIFORM4FVPROC, glUniform4fv)                             \
  X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv) X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)     \
  X(PFNGLGENSAMPLERSPROC, glGenSamplers)

#define SR_DECLARE(type, name) extern type sr_##name;
SR_GL_FUNCS(SR_DECLARE)
#undef SR_DECLARE

#define glClear sr_glClear
#define glClearColor sr_glClearColor
#define glClearDepth sr_glClearDepth
#define glViewport sr_glViewport
#define glScissor sr_glScissor
#define glEnable sr_glEnable
#define glDisable sr_glDisable
#define glBlendFunc sr_glBlendFunc
#define glBlendFuncSeparate sr_glBlendFuncSeparate
#define glBlendEquation sr_glBlendEquation
#define glDepthFunc sr_glDepthFunc
#define glDepthMask sr_glDepthMask
#define glColorMask sr_glColorMask
#define glCullFace sr_glCullFace
#define glFrontFace sr_glFrontFace
#define glPolygonOffset sr_glPolygonOffset
#define glGetIntegerv sr_glGetIntegerv
#define glGetFloatv sr_glGetFloatv
#define glGetString sr_glGetString
#define glGetError sr_glGetError
#define glPixelStorei sr_glPixelStorei
#define glReadPixels sr_glReadPixels
#define glFinish sr_glFinish
#define glDrawArrays sr_glDrawArrays
#define glDrawElements sr_glDrawElements
#define glDrawArraysInstanced sr_glDrawArraysInstanced
#define glGenTextures sr_glGenTextures
#define glDeleteTextures sr_glDeleteTextures
#define glBindTexture sr_glBindTexture
#define glActiveTexture sr_glActiveTexture
#define glTexImage2D sr_glTexImage2D
#define glTexSubImage2D sr_glTexSubImage2D
#define glTexParameteri sr_glTexParameteri
#define glTexParameterf sr_glTexParameterf
#define glTexParameterfv sr_glTexParameterfv
#define glGenerateMipmap sr_glGenerateMipmap
#define glTexImage2DMultisample sr_glTexImage2DMultisample
#define glGenFramebuffers sr_glGenFramebuffers
#define glDeleteFramebuffers sr_glDeleteFramebuffers
#define glBindFramebuffer sr_glBindFramebuffer
#define glFramebufferTexture2D sr_glFramebufferTexture2D
#define glFramebufferRenderbuffer sr_glFramebufferRenderbuffer
#define glCheckFramebufferStatus sr_glCheckFramebufferStatus
#define glBlitFramebuffer sr_glBlitFramebuffer
#define glDrawBuffers sr_glDrawBuffers
#define glDrawBuffer sr_glDrawBuffer
#define glReadBuffer sr_glReadBuffer
#define glGenRenderbuffers sr_glGenRenderbuffers
#define glDeleteRenderbuffers sr_glDeleteRenderbuffers
#define glBindRenderbuffer sr_glBindRenderbuffer
#define glRenderbufferStorageMultisample sr_glRenderbufferStorageMultisample
#define glRenderbufferStorage sr_glRenderbufferStorage
#define glGenBuffers sr_glGenBuffers
#define glDeleteBuffers sr_glDeleteBuffers
#define glBindBuffer sr_glBindBuffer
#define glBufferData sr_glBufferData
#define glBufferSubData sr_glBufferSubData
#define glGenVertexArrays sr_glGenVertexArrays
#define glDeleteVertexArrays sr_glDeleteVertexArrays
#define glBindVertexArray sr_glBindVertexArray
#define glEnableVertexAttribArray sr_glEnableVertexAttribArray
#define glDisableVertexAttribArray sr_glDisableVertexAttribArray
#define glVertexAttribPointer sr_glVertexAttribPointer
#define glVertexAttribDivisor sr_glVertexAttribDivisor
#define glVertexAttrib4f sr_glVertexAttrib4f
#define glVertexAttrib2f sr_glVertexAttrib2f
#define glCreateShader sr_glCreateShader
#define glDeleteShader sr_glDeleteShader
#define glShaderSource sr_glShaderSource
#define glCompileShader sr_glCompileShader
#define glGetShaderiv sr_glGetShaderiv
#define glGetShaderInfoLog sr_glGetShaderInfoLog
#define glCreateProgram sr_glCreateProgram
#define glDeleteProgram sr_glDeleteProgram
#define glAttachShader sr_glAttachShader
#define glLinkProgram sr_glLinkProgram
#define glGetProgramiv sr_glGetProgramiv
#define glGetProgramInfoLog sr_glGetProgramInfoLog
#define glUseProgram sr_glUseProgram
#define glBindAttribLocation sr_glBindAttribLocation
#define glGetUniformLocation sr_glGetUniformLocation
#define glUniform1i sr_glUniform1i
#define glUniform1f sr_glUniform1f
#define glUniform2f sr_glUniform2f
#define glUniform3f sr_glUniform3f
#define glUniform4f sr_glUniform4f
#define glUniform1fv sr_glUniform1fv
#define glUniform2fv sr_glUniform2fv
#define glUniform3fv sr_glUniform3fv
#define glUniform4fv sr_glUniform4fv
#define glUniformMatrix3fv sr_glUniformMatrix3fv
#define glUniformMatrix4fv sr_glUniformMatrix4fv
#define glGenSamplers sr_glGenSamplers

#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif

namespace gl {
// Carrega todas as funções; devolve false se alguma faltar.
bool load(void* (*getProc)(const char*));
float maxAnisotropy();
}  // namespace gl
