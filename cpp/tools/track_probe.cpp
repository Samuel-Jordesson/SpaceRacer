// Ferramenta de análise de um GLB de pista: estatísticas por material e o pórtico da largada.
#include <cmath>
#include <cstdio>
#include <map>

#include "core/gltf_loader.h"

int main(int argc, char** argv) {
  if (argc < 2) return 1;
  GltfAsset a = loadGltf(argv[1], nullptr);
  a.scene->updateMatrixWorld();
  struct S { size_t tris = 0; Box3 box; double nx = 0, ny = 0, nz = 0; int alpha = 0; float alphaTest = 0; bool tr = false; };
  std::map<std::string, S> st;
  a.scene->traverse([&](Node& n) {
    if (!n.isMesh()) return;
    auto& s = st[n.material->name];
    const Geometry& g = *n.geometry;
    s.tris += g.idx.size() / 3;
    s.alphaTest = n.material->alphaTest;
    s.tr = n.material->transparent;
    for (auto& p : g.pos) s.box.expand(n.matrixWorld.point(p));
    for (size_t i = 0; i + 2 < g.idx.size(); i += 3) {
      vec3 A = n.matrixWorld.point(g.pos[g.idx[i]]), B = n.matrixWorld.point(g.pos[g.idx[i + 1]]), C = n.matrixWorld.point(g.pos[g.idx[i + 2]]);
      vec3 c = cross(B - A, C - A);
      s.nx += c.x; s.ny += c.y; s.nz += c.z;
    }
  });
  // centroides de "sand" perto da largada
  int shown = 0;
  a.scene->traverse([&](Node& n) {
    if (!n.isMesh() || n.material->name.rfind("sand.", 0) != 0 || shown > 8) return;
    const Geometry& g = *n.geometry;
    for (size_t i = 0; i + 2 < g.idx.size() && shown < 8; i += 300) {
      vec3 c = (n.matrixWorld.point(g.pos[g.idx[i]]) + n.matrixWorld.point(g.pos[g.idx[i + 1]]) + n.matrixWorld.point(g.pos[g.idx[i + 2]])) / 3.0f;
      if (std::hypot(c.x + 331, c.z + 761) < 600) { std::printf("sand at %.0f %.0f %.0f\n", c.x, c.y, c.z); shown++; }
    }
  });
  for (auto& [k, s] : st) {
    vec3 c = s.box.center(), z = s.box.size();
    std::printf("%-28s tris=%7zu at=%.2f tr=%d  center=(%.0f %.0f %.0f) size=(%.0f %.0f %.0f) areaN=(%.0f %.0f %.0f)\n", k.c_str(), s.tris, s.alphaTest, s.tr,
                c.x, c.y, c.z, z.x, z.y, z.z, s.nx / 2, s.ny / 2, s.nz / 2);
  }
}
