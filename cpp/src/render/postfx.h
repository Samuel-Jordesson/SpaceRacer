// Pós-processamento estilo console: render HDR com MSAA → oclusão de ambiente → bloom →
// (profundidade de campo) → tone mapping → gradação, vinheta, aberração cromática,
// blur radial de velocidade e grão.
#pragma once
#include <memory>

#include "camera.h"
#include "shader.h"

struct Quality {
  const char* name;
  float pixelRatio;
  int msaa;
  bool ao;
  int aoSamples;
  bool aoHalfRes;
  bool bloom;
  int shadow;
};
extern const Quality QUALITY[4];

class PostFX {
 public:
  enum class ToneMapping { ACES, Neutral };
  PostFX();
  ~PostFX();

  int level = 2;  // nível do QUALITY em uso (−1 = personalizado)
  const Quality& quality() const { return q; }
  void setLevel(int l);
  void setQuality(const Quality& nq, int lvl);
  bool lensFx = true, speedBlur = true;  // vinheta/grão/aberração e blur radial
  // tamanho da janela em pontos e densidade de pixels (devicePixelRatio)
  void resize(int winW, int winH, float dpr);

  // parâmetros do shader final
  float saturation = 1.08f, contrast = 1.06f, vignette = 0.32f, tint = 1;
  ToneMapping toneMapping = ToneMapping::ACES;
  float exposure = 0.95f;
  // BokehPass
  bool bokeh = false;
  float focus = 5, aperture = 0.004f, maxblur = 0.008f;

  void beginScene();  // liga o framebuffer HDR (MSAA); o chamador desenha a cena
  void endScene(float dt, float speed01, const Camera& cam);  // roda a cadeia até a imagem final
  void present(int fbW, int fbH);  // copia a imagem final para a janela

  int width = 1, height = 1;
  GLuint finalTexture() const { return finalTex; }

 private:
  void build();
  void destroy();
  Quality q = QUALITY[2];
  int winW = 1, winH = 1;
  float dpr = 1;
  bool built = false;
  float time = 0, speed = 0;

  GLuint msFbo = 0, msColor = 0, msDepth = 0;           // MSAA
  GLuint sceneFbo = 0, sceneTex = 0, depthTex = 0;      // resolvido
  GLuint aoFbo[2] = {0, 0}, aoTex[2] = {0, 0};
  int aoW = 1, aoH = 1;
  GLuint brightFbo = 0, brightTex = 0;
  GLuint bloomH[5] = {}, bloomHFbo[5] = {}, bloomV[5] = {}, bloomVFbo[5] = {};
  int bloomW[5] = {}, bloomHt[5] = {};
  GLuint pingFbo[2] = {0, 0}, pingTex[2] = {0, 0};
  GLuint finalFbo = 0, finalTex = 0;

  std::unique_ptr<Shader> aoShader, aoBlur, aoComposite, highPass, blur[5], bloomComposite, copyAdd, bokehShader, output, finalShader, copyShader;
};
