#include "assets.h"

#include <cstring>
#include <filesystem>

#include "../../third_party/stb_image.h"

namespace fs = std::filesystem;

static std::string s_root = ".";

void initAssetRoot(const char* argv0) {
  std::vector<fs::path> starts = {fs::current_path()};
  std::error_code ec;
  fs::path exe = fs::weakly_canonical(fs::path(argv0), ec);
  if (!ec) starts.push_back(exe.parent_path());
  for (auto start : starts) {
    for (fs::path p = start; !p.empty(); p = p.parent_path()) {
      if (fs::exists(p / "assets" / "hdri" / "sky_4k.hdr")) {
        s_root = p.string();
        return;
      }
      if (p == p.parent_path()) break;
    }
  }
}

std::string assetPath(const std::string& rel) { return (fs::path(s_root) / rel).string(); }

TexturePtr loadImage(const std::string& rel, bool srgb) {
  int w, h, c;
  uint8_t* px = stbi_load(assetPath(rel).c_str(), &w, &h, &c, 4);
  if (!px) return nullptr;
  auto t = std::make_shared<Texture>();
  t->width = w;
  t->height = h;
  t->pixels.assign(px, px + (size_t)w * h * 4);
  t->srgb = srgb;
  stbi_image_free(px);
  return t;
}

uint16_t floatToHalf(float f) {
  uint32_t x;
  std::memcpy(&x, &f, 4);
  uint32_t sign = (x >> 16) & 0x8000;
  int32_t exp = (int32_t)((x >> 23) & 0xff) - 127 + 15;
  uint32_t mant = x & 0x7fffff;
  if (exp <= 0) {
    if (exp < -10) return (uint16_t)sign;
    mant |= 0x800000;
    uint32_t t = 14 - exp;
    uint32_t a = (1u << (t - 1)) - 1, b = (mant >> t) & 1;
    return (uint16_t)(sign | ((mant + a + b) >> t));
  }
  if (exp >= 31) return (uint16_t)(sign | 0x7c00);
  // arredondamento para o par mais próximo
  uint32_t r = mant + 0xfff + ((mant >> 13) & 1);
  if (r & 0x800000) {
    r = 0;
    exp++;
    if (exp >= 31) return (uint16_t)(sign | 0x7c00);
  }
  return (uint16_t)(sign | (exp << 10) | (r >> 13));
}
