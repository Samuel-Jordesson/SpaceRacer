// Interface 2D desenhada em OpenGL, em "px de CSS" (multiplicados pela densidade da tela):
// retângulos arredondados com borda e sombra (SDF), gradientes, imagens, texto (stb_truetype)
// e painéis com desfoque do que está atrás (backdrop-filter).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "../core/math3d.h"
#include "../gl/gl.h"

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
  Color() = default;
  constexpr Color(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
  static Color hex(uint32_t h, float a = 1) { return {((h >> 16) & 255) / 255.f, ((h >> 8) & 255) / 255.f, (h & 255) / 255.f, a}; }
  Color withAlpha(float m) const { return {r, g, b, a * m}; }
};
Color hsl(float h, float s, float l, float a = 1);
Color mix(const Color& a, const Color& b, float t);

enum class Font { Barlow300, Barlow400, Barlow500, Barlow600, BC500, BC600, BC700, Sans400, Sans700, Mono400, Mono700, Count };

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct TextStyle {
  Font font = Font::Sans400;
  float size = 16;
  float letterSpacing = 0;  // em px
  Color color{1, 1, 1, 1};
  bool tabular = false;     // dígitos de largura fixa (font-variant-numeric: tabular-nums)
};

class UI {
 public:
  UI();
  ~UI();
  bool loadFonts(const std::string& dir);

  // fbW/fbH em pixels; dpr = pixels por px de CSS
  void begin(int fbW, int fbH, float dpr);
  void end();
  float width() const { return w; }   // em px de CSS
  float height() const { return h; }
  float dpr() const { return scale; }

  // opacidade aplicada a tudo que for desenhado (pilha)
  void pushOpacity(float a);
  void popOpacity();
  void pushClip(const Rect& r);
  void popClip();
  // textura da cena já desfocada, para painéis com backdrop-filter
  void setBackdrop(GLuint tex) { backdropTex = tex; }

  void rect(const Rect& r, const Color& c, float radius = 0);
  void rectGradient(const Rect& r, const Color& left, const Color& right, float radius = 0);
  void border(const Rect& r, const Color& c, float width, float radius = 0);
  // box-shadow: deslocamento, desfoque e espalhamento como no CSS
  void shadow(const Rect& r, const Color& c, float dx, float dy, float blur, float spread, float radius = 0);
  // painel com o fundo desfocado + cor por cima
  void blurPanel(const Rect& r, const Color& tintColor, float radius = 0);
  // quad recortado por um retângulo arredondado maior (ex.: preenchimento dentro de uma barra)
  void rectInside(const Rect& quad, const Rect& shape, float radius, const Color& left, const Color& right);
  // retângulo girado em torno de (cx, cy)
  void rotatedRect(const Rect& r, float cx, float cy, float angle, const Color& c, float radius = 0);
  void circle(float cx, float cy, float r, const Color& c);
  void ring(float cx, float cy, float r, float width, const Color& c);
  void line(float x0, float y0, float x1, float y1, float width, const Color& c);
  void triangle(vec2 a, vec2 b, vec2 c, const Color& col);
  // imagem com object-fit: cover e object-position (px, py em 0..1)
  void imageCover(GLuint tex, int texW, int texH, const Rect& r, float posX = 0.5f, float posY = 0.5f, float opacity = 1);

  // texto: (x, y) = canto superior esquerdo da caixa de linha com altura lineHeight (0 = normal)
  float text(const std::string& s, float x, float y, const TextStyle& st, float lineHeight = 0);
  // texto posicionado pela linha de base
  float textBaseline(const std::string& s, float x, float baseline, const TextStyle& st) { return text(s, x, baseline - ascent(st.font, st.size), st, ascent(st.font, st.size) + descent(st.font, st.size)); }
  float measure(const std::string& s, const TextStyle& st);
  float lineHeightNormal(const TextStyle& st);
  // métricas: ascendente e descendente (positivo) em px para o tamanho dado
  float ascent(Font f, float size);
  float descent(Font f, float size);

 private:
  struct Vertex {
    float x, y, u, v;
    float r, g, b, a;
    float lx, ly, hw, hh;  // posição local relativa ao centro da forma e meia-dimensão
    float radius, border, soft, mode;
  };
  struct Glyph {
    float u0, v0, u1, v1;
    int w, h, xoff, yoff;
    float advance;
  };
  struct FontData;
  void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const Color& cl, const Color& cr,
            const Rect& shape, float radius, float border, float soft, float mode, GLuint tex);
  void flush();
  void setTexture(GLuint tex);
  const Glyph* glyph(Font f, int px, uint32_t cp);
  Font resolve(Font f, uint32_t cp);
  // avanço do caractere (com largura fixa nos dígitos se tabular); offset = deslocamento do glifo na célula
  float advance(Font f, uint32_t cp, float size, bool tabular, float* offset = nullptr);
  float opacity() const { return opacityStack.empty() ? 1 : opacityStack.back(); }

  std::vector<Vertex> verts;
  GLuint vao = 0, vbo = 0, prog = 0, atlas = 0, white = 0, curTex = 0, backdropTex = 0;
  GLint uScreen = -1, uTex = -1, uBackdrop = -1, uFb = -1;
  int fbW = 1, fbH = 1;
  float w = 1, h = 1, scale = 1;
  std::vector<float> opacityStack;
  std::vector<Rect> clipStack;
  std::unique_ptr<FontData> fonts[(int)Font::Count];
  // atlas de glifos
  int atlasSize = 2048, penX = 1, penY = 1, rowH = 0;
};

// decodifica UTF-8 em pontos de código
std::vector<uint32_t> utf8Decode(const std::string& s);
// maiúsculas (ASCII e Latin-1), como text-transform: uppercase
std::string upper(const std::string& s);
