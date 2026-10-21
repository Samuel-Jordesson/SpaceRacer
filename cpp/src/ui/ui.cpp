#include "ui.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>

#include "../../third_party/stb_truetype.h"

namespace {

const char* UI_VS = R"(#version 410 core
layout(location = 0) in vec4 aPosUv;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aLocal;
layout(location = 3) in vec4 aParams;
uniform vec2 uScreen;
out vec2 vUv;
out vec4 vColor;
out vec4 vLocal;
out vec4 vParams;
void main() {
  vUv = aPosUv.zw;
  vColor = aColor;
  vLocal = aLocal;
  vParams = aParams;
  gl_Position = vec4(aPosUv.x / uScreen.x * 2.0 - 1.0, 1.0 - aPosUv.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";

const char* UI_FS = R"(#version 410 core
in vec2 vUv;
in vec4 vColor;
in vec4 vLocal;
in vec4 vParams;
uniform sampler2D uTex;
uniform sampler2D uBackdrop;
uniform vec2 uFb;
out vec4 fragColor;
float sdRound(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float erfApprox(float x) {
  // Abramowitz–Stegun 7.1.26
  float s = sign(x); x = abs(x);
  float t = 1.0 / (1.0 + 0.3275911 * x);
  float y = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * exp(-x * x);
  return s * y;
}
void main() {
  float radius = vParams.x, border = vParams.y, soft = vParams.z;
  int mode = int(vParams.w + 0.5);
  if (mode == 3) border = 0.0;  // no painel desfocado o campo leva a opacidade do grupo
  vec2 hs = vLocal.zw;
  float d = sdRound(vLocal.xy, hs, min(radius, min(hs.x, hs.y)));
  float shape;
  if (soft > 0.0) shape = 0.5 - 0.5 * erfApprox(d / (soft * 1.41421356));
  else if (border > 0.0) shape = clamp(0.5 - d, 0.0, 1.0) * clamp(0.5 + d + border, 0.0, 1.0);
  else shape = clamp(0.5 - d, 0.0, 1.0);
  if (mode == 1) {
    fragColor = vec4(vColor.rgb, vColor.a * texture(uTex, vUv).r);
  } else if (mode == 2) {
    vec4 t = texture(uTex, vUv);
    fragColor = vec4(t.rgb * vColor.rgb, t.a * vColor.a * shape);
  } else if (mode == 3) {
    vec3 b = texture(uBackdrop, gl_FragCoord.xy / uFb).rgb;
    fragColor = vec4(mix(b, vColor.rgb, vColor.a), shape * vParams.y);
  } else if (mode == 4) {
    fragColor = vColor;
  } else {
    fragColor = vec4(vColor.rgb, vColor.a * shape);
  }
}
)";

GLuint compileProg() {
  auto comp = [](GLenum t, const char* src) {
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[2048];
      glGetShaderInfoLog(s, sizeof log, nullptr, log);
      std::fprintf(stderr, "[ui] shader: %s\n", log);
    }
    return s;
  };
  GLuint p = glCreateProgram();
  GLuint v = comp(GL_VERTEX_SHADER, UI_VS), f = comp(GL_FRAGMENT_SHADER, UI_FS);
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  return p;
}

}  // namespace

struct UI::FontData {
  std::vector<unsigned char> data;
  int digitAdvance = 0;  // maior avanço entre 0–9 (unidades da fonte)
  stbtt_fontinfo info;
  float ascent = 0.8f, descent = 0.2f, lineGap = 0;  // em unidades de em
  std::map<std::pair<int, uint32_t>, Glyph> glyphs;
};

Color hsl(float h, float s, float l, float a) {
  h = std::fmod(h, 360.f) / 360.f;
  auto hue = [](float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.f / 3) return p + (q - p) * (2.f / 3 - t) * 6;
    return p;
  };
  if (s == 0) return {l, l, l, a};
  float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
  return {hue(p, q, h + 1.f / 3), hue(p, q, h), hue(p, q, h - 1.f / 3), a};
}

Color mix(const Color& a, const Color& b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

std::vector<uint32_t> utf8Decode(const std::string& s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    uint32_t cp;
    int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c >> 5) == 6) { cp = c & 31; n = 2; }
    else if ((c >> 4) == 14) { cp = c & 15; n = 3; }
    else { cp = c & 7; n = 4; }
    for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 63);
    out.push_back(cp);
    i += n;
  }
  return out;
}

static void utf8Append(std::string& o, uint32_t cp) {
  if (cp < 0x80) o += (char)cp;
  else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
  else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
  else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 63)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
}

std::string upper(const std::string& s) {
  std::string o;
  for (uint32_t cp : utf8Decode(s)) {
    if (cp >= 'a' && cp <= 'z') cp -= 32;
    else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) cp -= 32;
    utf8Append(o, cp);
  }
  return o;
}

UI::UI() {
  prog = compileProg();
  uScreen = glGetUniformLocation(prog, "uScreen");
  uTex = glGetUniformLocation(prog, "uTex");
  uBackdrop = glGetUniformLocation(prog, "uBackdrop");
  uFb = glGetUniformLocation(prog, "uFb");
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  for (int i = 0; i < 4; i++) {
    glEnableVertexAttribArray(i);
    glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(float) * 4 * i));
  }
  glBindVertexArray(0);
  // atlas de glifos (R8)
  glGenTextures(1, &atlas);
  glBindTexture(GL_TEXTURE_2D, atlas);
  std::vector<uint8_t> zero(atlasSize * atlasSize, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasSize, atlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, zero.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  uint8_t wpx[4] = {255, 255, 255, 255};
  glGenTextures(1, &white);
  glBindTexture(GL_TEXTURE_2D, white);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, wpx);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

UI::~UI() {
  glDeleteProgram(prog);
  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
  glDeleteTextures(1, &atlas);
  glDeleteTextures(1, &white);
}

bool UI::loadFonts(const std::string& dir) {
  const char* files[(int)Font::Count] = {"Barlow-Light.ttf", "Barlow-Regular.ttf", "Barlow-Medium.ttf", "Barlow-SemiBold.ttf",
                                         "BarlowCondensed-Medium.ttf", "BarlowCondensed-SemiBold.ttf", "BarlowCondensed-Bold.ttf",
                                         "NotoSans-Regular.ttf", "NotoSans-Bold.ttf", "DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf"};
  bool ok = true;
  for (int i = 0; i < (int)Font::Count; i++) {
    auto fd = std::make_unique<FontData>();
    std::ifstream in(dir + "/" + files[i], std::ios::binary);
    if (!in) {
      std::fprintf(stderr, "[ui] fonte ausente: %s\n", files[i]);
      ok = false;
      continue;
    }
    fd->data.assign(std::istreambuf_iterator<char>(in), {});
    if (!stbtt_InitFont(&fd->info, fd->data.data(), stbtt_GetFontOffsetForIndex(fd->data.data(), 0))) {
      ok = false;
      continue;
    }
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&fd->info, &asc, &desc, &gap);
    float em = stbtt_ScaleForMappingEmToPixels(&fd->info, 1);
    fd->ascent = asc * em;
    fd->descent = -desc * em;
    fd->lineGap = gap * em;
    fonts[i] = std::move(fd);
  }
  return ok;
}

float UI::ascent(Font f, float size) { return fonts[(int)f] ? fonts[(int)f]->ascent * size : size * 0.8f; }
float UI::descent(Font f, float size) { return fonts[(int)f] ? fonts[(int)f]->descent * size : size * 0.2f; }
float UI::lineHeightNormal(const TextStyle& st) {
  auto& fd = fonts[(int)st.font];
  return fd ? (fd->ascent + fd->descent + fd->lineGap) * st.size : st.size * 1.2f;
}

const UI::Glyph* UI::glyph(Font f, int px, uint32_t cp) {
  FontData* fd = fonts[(int)f].get();
  if (!fd) return nullptr;
  auto key = std::make_pair(px, cp);
  auto it = fd->glyphs.find(key);
  if (it != fd->glyphs.end()) return &it->second;
  float sc = stbtt_ScaleForMappingEmToPixels(&fd->info, (float)px);
  int x0, y0, x1, y1;
  stbtt_GetCodepointBitmapBox(&fd->info, cp, sc, sc, &x0, &y0, &x1, &y1);
  int gw = x1 - x0, gh = y1 - y0;
  int adv, lsb;
  stbtt_GetCodepointHMetrics(&fd->info, cp, &adv, &lsb);
  Glyph g{};
  g.w = gw;
  g.h = gh;
  g.xoff = x0;
  g.yoff = y0;
  g.advance = adv * sc;
  if (gw > 0 && gh > 0) {
    if (penX + gw + 1 >= atlasSize) {
      penX = 1;
      penY += rowH + 1;
      rowH = 0;
    }
    if (penY + gh + 1 >= atlasSize) {
      // atlas cheio: recomeça (os glifos são recriados sob demanda)
      flush();
      for (auto& fdd : fonts)
        if (fdd) fdd->glyphs.clear();
      penX = penY = 1;
      rowH = 0;
      return glyph(f, px, cp);
    }
    std::vector<unsigned char> bmp(gw * gh);
    stbtt_MakeCodepointBitmap(&fd->info, bmp.data(), gw, gh, gw, sc, sc, cp);
    flush();
    glBindTexture(GL_TEXTURE_2D, atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, penX, penY, gw, gh, GL_RED, GL_UNSIGNED_BYTE, bmp.data());
    g.u0 = (float)penX / atlasSize;
    g.v0 = (float)penY / atlasSize;
    g.u1 = (float)(penX + gw) / atlasSize;
    g.v1 = (float)(penY + gh) / atlasSize;
    penX += gw + 1;
    rowH = std::max(rowH, gh);
  }
  return &(fd->glyphs[key] = g);
}

void UI::begin(int fw, int fh, float dpr) {
  fbW = std::max(1, fw);
  fbH = std::max(1, fh);
  scale = dpr;
  w = fbW / scale;
  h = fbH / scale;
  verts.clear();
  curTex = 0;
  opacityStack.clear();
  clipStack.clear();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, fbW, fbH);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_SCISSOR_TEST);
}

void UI::end() {
  flush();
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
}

void UI::pushOpacity(float a) { opacityStack.push_back(opacity() * a); }
void UI::popOpacity() { if (!opacityStack.empty()) opacityStack.pop_back(); }

void UI::pushClip(const Rect& r) {
  flush();
  Rect c = r;
  if (!clipStack.empty()) {
    const Rect& p = clipStack.back();
    float x0 = std::max(c.x, p.x), y0 = std::max(c.y, p.y);
    float x1 = std::min(c.x + c.w, p.x + p.w), y1 = std::min(c.y + c.h, p.y + p.h);
    c = {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
  }
  clipStack.push_back(c);
  glEnable(GL_SCISSOR_TEST);
  glScissor((int)std::floor(c.x * scale), (int)std::floor(fbH - (c.y + c.h) * scale), (int)std::ceil(c.w * scale), (int)std::ceil(c.h * scale));
}

void UI::popClip() {
  flush();
  if (!clipStack.empty()) clipStack.pop_back();
  if (clipStack.empty()) glDisable(GL_SCISSOR_TEST);
  else {
    const Rect& c = clipStack.back();
    glScissor((int)std::floor(c.x * scale), (int)std::floor(fbH - (c.y + c.h) * scale), (int)std::ceil(c.w * scale), (int)std::ceil(c.h * scale));
  }
}

void UI::setTexture(GLuint tex) {
  if (tex != curTex) {
    flush();
    curTex = tex;
  }
}

void UI::flush() {
  if (verts.empty()) return;
  glUseProgram(prog);
  glUniform2f(uScreen, (float)fbW, (float)fbH);
  glUniform2f(uFb, (float)fbW, (float)fbH);
  glUniform1i(uTex, 0);
  glUniform1i(uBackdrop, 1);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, backdropTex ? backdropTex : white);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, curTex ? curTex : white);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
  glBindVertexArray(0);
  verts.clear();
}

void UI::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const Color& cl, const Color& cr,
              const Rect& shape, float radius, float border, float soft, float mode, GLuint tex) {
  setTexture(tex);
  float s = scale, op = opacity();
  float cx = (shape.x + shape.w / 2) * s, cy = (shape.y + shape.h / 2) * s;
  float hw = shape.w / 2 * s, hh = shape.h / 2 * s;
  auto V = [&](float x, float y, float u, float v, const Color& c) {
    verts.push_back({x * s, y * s, u, v, c.r, c.g, c.b, c.a * op, x * s - cx, y * s - cy, hw, hh, radius * s, border * s, soft * s, mode});
  };
  V(x0, y0, u0, v0, cl); V(x1, y0, u1, v0, cr); V(x1, y1, u1, v1, cr);
  V(x0, y0, u0, v0, cl); V(x1, y1, u1, v1, cr); V(x0, y1, u0, v1, cl);
}

void UI::rect(const Rect& r, const Color& c, float radius) {
  if (c.a <= 0) return;
  quad(r.x - 1, r.y - 1, r.x + r.w + 1, r.y + r.h + 1, 0, 0, 0, 0, c, c, r, radius, 0, 0, 0, 0);
}

void UI::rectGradient(const Rect& r, const Color& l, const Color& rr, float radius) {
  quad(r.x, r.y, r.x + r.w, r.y + r.h, 0, 0, 0, 0, l, rr, r, radius, 0, 0, 0, 0);
}

void UI::rectInside(const Rect& q, const Rect& shape, float radius, const Color& l, const Color& rr) {
  quad(q.x, q.y, q.x + q.w, q.y + q.h, 0, 0, 0, 0, l, rr, shape, radius, 0, 0, 0, 0);
}

void UI::border(const Rect& r, const Color& c, float width, float radius) {
  quad(r.x - 1, r.y - 1, r.x + r.w + 1, r.y + r.h + 1, 0, 0, 0, 0, c, c, r, radius, width, 0, 0, 0);
}

void UI::shadow(const Rect& r, const Color& c, float dx, float dy, float blur, float spread, float radius) {
  Rect s{r.x + dx - spread, r.y + dy - spread, r.w + spread * 2, r.h + spread * 2};
  if (s.w <= 0 || s.h <= 0) {
    // espalhamento negativo maior que a caixa: sombra mínima centrada
    s = {r.x + dx + r.w / 2 - 0.5f, r.y + dy + r.h / 2 - 0.5f, 1, 1};
  }
  float sigma = std::max(0.01f, blur / 2);
  float pad = blur * 1.5f + 2;
  quad(s.x - pad, s.y - pad, s.x + s.w + pad, s.y + s.h + pad, 0, 0, 0, 0, c, c, s, radius + std::max(0.f, spread), 0, sigma, 0, 0);
}

void UI::blurPanel(const Rect& r, const Color& tint, float radius) {
  if (!backdropTex) {
    rect(r, tint, radius);
    return;
  }
  // mode 3: mistura o fundo desfocado com a cor; "border" leva a opacidade do grupo
  setTexture(curTex);
  float s = scale, op = opacity();
  float cx = (r.x + r.w / 2) * s, cy = (r.y + r.h / 2) * s, hw = r.w / 2 * s, hh = r.h / 2 * s;
  auto V = [&](float x, float y) {
    verts.push_back({x * s, y * s, 0, 0, tint.r, tint.g, tint.b, tint.a, x * s - cx, y * s - cy, hw, hh, radius * s, op, 0, 3});
  };
  float x0 = r.x - 1, y0 = r.y - 1, x1 = r.x + r.w + 1, y1 = r.y + r.h + 1;
  V(x0, y0); V(x1, y0); V(x1, y1); V(x0, y0); V(x1, y1); V(x0, y1);
}

void UI::rotatedRect(const Rect& r, float cx, float cy, float angle, const Color& c, float radius) {
  setTexture(curTex);
  float s = scale, op = opacity();
  float ca = std::cos(angle), sa = std::sin(angle);
  float hw = r.w / 2 * s, hh = r.h / 2 * s;
  float rcx = r.x + r.w / 2, rcy = r.y + r.h / 2;
  auto V = [&](float lx, float ly) {
    // ponto local (em torno do centro do retângulo) → girado em torno de (cx, cy)
    float px = rcx + lx - cx, py = rcy + ly - cy;
    float x = cx + px * ca - py * sa, y = cy + px * sa + py * ca;
    verts.push_back({x * s, y * s, 0, 0, c.r, c.g, c.b, c.a * op, lx * s, ly * s, hw, hh, radius * s, 0, 0, 0});
  };
  float ex = r.w / 2 + 1, ey = r.h / 2 + 1;
  V(-ex, -ey); V(ex, -ey); V(ex, ey); V(-ex, -ey); V(ex, ey); V(-ex, ey);
}

void UI::circle(float cx, float cy, float r, const Color& c) { rect({cx - r, cy - r, r * 2, r * 2}, c, r); }

void UI::ring(float cx, float cy, float r, float width, const Color& c) {
  float o = r + width / 2;
  border({cx - o, cy - o, o * 2, o * 2}, c, width, o);
}

void UI::line(float x0, float y0, float x1, float y1, float width, const Color& c) {
  float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
  float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  rotatedRect({cx - len / 2, cy - width / 2, len, width}, cx, cy, std::atan2(dy, dx), c, 0);
}

void UI::triangle(vec2 a, vec2 b, vec2 c, const Color& col) {
  setTexture(curTex);
  float s = scale, op = opacity();
  for (vec2 p : {a, b, c}) verts.push_back({p.x * s, p.y * s, 0, 0, col.r, col.g, col.b, col.a * op, 0, 0, 1e6f, 1e6f, 0, 0, 0, 4});
}

void UI::imageCover(GLuint tex, int tw, int th, const Rect& r, float posX, float posY, float opac) {
  if (!tex || tw <= 0 || th <= 0) return;
  float ar = (float)tw / th, rr = r.w / r.h;
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  if (ar > rr) {  // imagem mais larga: corta nas laterais
    float vis = rr / ar;
    u0 = (1 - vis) * posX;
    u1 = u0 + vis;
  } else {
    float vis = ar / rr;
    v0 = (1 - vis) * posY;
    v1 = v0 + vis;
  }
  Color c(1, 1, 1, opac);
  quad(r.x, r.y, r.x + r.w, r.y + r.h, u0, v0, u1, v1, c, c, r, 0, 0, 0, 2, tex);
}

float UI::advance(Font f, uint32_t cp, float size, bool tabular, float* offset) {
  FontData* fd = fonts[(int)f].get();
  float sc = stbtt_ScaleForMappingEmToPixels(&fd->info, size);
  int adv, lsb;
  stbtt_GetCodepointHMetrics(&fd->info, cp, &adv, &lsb);
  if (offset) *offset = 0;
  if (tabular && cp >= '0' && cp <= '9') {
    if (!fd->digitAdvance)
      for (int d = '0'; d <= '9'; d++) {
        int a, l;
        stbtt_GetCodepointHMetrics(&fd->info, d, &a, &l);
        fd->digitAdvance = std::max(fd->digitAdvance, a);
      }
    if (offset) *offset = (fd->digitAdvance - adv) * sc / 2;  // centraliza o dígito na célula
    return fd->digitAdvance * sc;
  }
  return adv * sc;
}

// fonte que tem o glifo (fallback como no navegador: Barlow/Noto → DejaVu)
Font UI::resolve(Font f, uint32_t cp) {
  FontData* fd = fonts[(int)f].get();
  if (fd && stbtt_FindGlyphIndex(&fd->info, (int)cp)) return f;
  for (Font alt : {Font::Sans400, Font::Mono400})
    if (fonts[(int)alt] && stbtt_FindGlyphIndex(&fonts[(int)alt]->info, (int)cp)) return alt;
  return f;
}

float UI::measure(const std::string& s, const TextStyle& st) {
  if (!fonts[(int)st.font]) return 0;
  float x = 0;
  uint32_t prev = 0;
  Font prevFont = st.font;
  for (uint32_t cp : utf8Decode(s)) {
    Font f = resolve(st.font, cp);
    FontData* fd = fonts[(int)f].get();
    float sc = stbtt_ScaleForMappingEmToPixels(&fd->info, st.size);
    bool digit = st.tabular && cp >= '0' && cp <= '9';
    if (prev && f == prevFont && !digit) x += stbtt_GetCodepointKernAdvance(&fd->info, prev, cp) * sc;
    x += advance(f, cp, st.size, st.tabular) + st.letterSpacing;
    prev = cp;
    prevFont = f;
  }
  return x;
}

float UI::text(const std::string& s, float x, float y, const TextStyle& st, float lineHeight) {
  FontData* fd = fonts[(int)st.font].get();
  if (!fd || st.color.a <= 0) return measure(s, st);
  float lh = lineHeight > 0 ? lineHeight : lineHeightNormal(st);
  float asc = fd->ascent * st.size, desc = fd->descent * st.size;
  float baseline = y + (lh - (asc + desc)) / 2 + asc;
  int px = std::max(1, (int)std::lround(st.size * scale));
  float pen = x;
  uint32_t prev = 0;
  Font prevFont = st.font;
  Rect none{0, 0, 1e6f, 1e6f};
  FontData* base = fd;
  for (uint32_t cp : utf8Decode(s)) {
    Font f = resolve(st.font, cp);
    fd = fonts[(int)f].get();
    float sc = stbtt_ScaleForMappingEmToPixels(&fd->info, st.size);
    bool digit = st.tabular && cp >= '0' && cp <= '9';
    if (prev && f == prevFont && !digit) pen += stbtt_GetCodepointKernAdvance(&fd->info, prev, cp) * sc;
    prevFont = f;
    float cellOff = 0, adv = advance(f, cp, st.size, st.tabular, &cellOff);
    const Glyph* g = glyph(f, px, cp);
    if (g && g->w > 0) {
      // posição do glifo alinhada ao pixel físico
      float gx = std::round((pen + cellOff) * scale + g->xoff) / scale;
      float gy = std::round(baseline * scale + g->yoff) / scale;
      float gw = g->w / scale, gh = g->h / scale;
      quad(gx, gy, gx + gw, gy + gh, g->u0, g->v0, g->u1, g->v1, st.color, st.color, none, 0, 0, 0, 1, atlas);
    }
    pen += adv + st.letterSpacing;
    prev = cp;
  }
  (void)base;
  return pen - x;
}
