#include "track.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "../core/assets.h"
#include <chrono>
static double nowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

using phys::V3;

// Triângulos da pista em 2D (XZ) com o tipo: 1 = asfalto da pista, 2 = limite (linha branca, zebra)
struct SurfaceMap {
  struct T { float ax, az, bx, bz, cx, cz; int type; };
  std::vector<T> tris;
  float cell = 8, minX = 0, minZ = 0;
  int nx = 0, nz = 0;
  std::vector<std::vector<int>> grid;
  void build() {
    if (tris.empty()) return;
    float mxX = -1e30f, mxZ = -1e30f;
    minX = minZ = 1e30f;
    for (auto& t : tris) {
      minX = std::min({minX, t.ax, t.bx, t.cx}); mxX = std::max({mxX, t.ax, t.bx, t.cx});
      minZ = std::min({minZ, t.az, t.bz, t.cz}); mxZ = std::max({mxZ, t.az, t.bz, t.cz});
    }
    nx = (int)((mxX - minX) / cell) + 1;
    nz = (int)((mxZ - minZ) / cell) + 1;
    grid.assign((size_t)nx * nz, {});
    for (int i = 0; i < (int)tris.size(); i++) {
      auto& t = tris[i];
      int x0 = (int)((std::min({t.ax, t.bx, t.cx}) - minX) / cell), x1 = (int)((std::max({t.ax, t.bx, t.cx}) - minX) / cell);
      int z0 = (int)((std::min({t.az, t.bz, t.cz}) - minZ) / cell), z1 = (int)((std::max({t.az, t.bz, t.cz}) - minZ) / cell);
      for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) grid[(size_t)z * nx + x].push_back(i);
    }
  }
  // 0 = fora, 1 = pista, 2 = limite (o limite vence quando os dois se sobrepõem)
  int at(float x, float z) const {
    int gx = (int)((x - minX) / cell), gz = (int)((z - minZ) / cell);
    if (gx < 0 || gz < 0 || gx >= nx || gz >= nz) return 0;
    int r = 0;
    for (int i : grid[(size_t)gz * nx + gx]) {
      auto& t = tris[i];
      float d1 = (x - t.bx) * (t.az - t.bz) - (t.ax - t.bx) * (z - t.bz);
      float d2 = (x - t.cx) * (t.bz - t.cz) - (t.bx - t.cx) * (z - t.cz);
      float d3 = (x - t.ax) * (t.cz - t.az) - (t.cx - t.ax) * (z - t.az);
      bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
      if (neg && pos) continue;
      if (t.type == 2) return 2;
      r = 1;
    }
    return r;
  }
};
static SurfaceMap g_surf;

namespace {

bool matches(const std::string& name, const std::vector<std::string>& patterns) {
  for (auto& p : patterns) {
    if (!p.empty() && p.back() == '*') {
      if (name.compare(0, p.size() - 1, p, 0, p.size() - 1) == 0) return true;
    } else if (name == p) {
      return true;
    }
  }
  return false;
}

// ponto do triângulo mais próximo de p (Ericson, Real-Time Collision Detection 5.1.5)
V3 closestOnTri(const V3& p, const V3& a, const V3& b, const V3& c) {
  V3 ab = b - a, ac = c - a, ap = p - a;
  double d1 = ab.dot(ap), d2 = ac.dot(ap);
  if (d1 <= 0 && d2 <= 0) return a;
  V3 bp = p - b;
  double d3 = ab.dot(bp), d4 = ac.dot(bp);
  if (d3 >= 0 && d4 <= d3) return b;
  double vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
  V3 cp = p - c;
  double d5 = ab.dot(cp), d6 = ac.dot(cp);
  if (d6 >= 0 && d5 <= d6) return c;
  double vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
  double va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
  double den = 1 / (va + vb + vc);
  return a + ab * (vb * den) + ac * (vc * den);
}

V3 A(const TrackGround::Tri& t) { return {t.ax, t.ay, t.az}; }
V3 B(const TrackGround::Tri& t) { return {t.bx, t.by, t.bz}; }
V3 C(const TrackGround::Tri& t) { return {t.cx, t.cy, t.cz}; }

}  // namespace

// ---------------------------------------------------------------- grade espacial
void TrackGround::Grid::build(const std::vector<Tri>& tris, float cellSize) {
  cell = cellSize;
  if (tris.empty()) { nx = nz = 0; return; }
  float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
  for (auto& t : tris) {
    x0 = std::min({x0, t.ax, t.bx, t.cx}); x1 = std::max({x1, t.ax, t.bx, t.cx});
    z0 = std::min({z0, t.az, t.bz, t.cz}); z1 = std::max({z1, t.az, t.bz, t.cz});
  }
  minX = x0; minZ = z0;
  nx = std::max(1, (int)std::ceil((x1 - x0) / cell) + 1);
  nz = std::max(1, (int)std::ceil((z1 - z0) / cell) + 1);
  std::vector<uint32_t> count((size_t)nx * nz + 1, 0);
  auto range = [&](const Tri& t, int& i0, int& i1, int& k0, int& k1) {
    i0 = (int)((std::min({t.ax, t.bx, t.cx}) - minX) / cell); i1 = (int)((std::max({t.ax, t.bx, t.cx}) - minX) / cell);
    k0 = (int)((std::min({t.az, t.bz, t.cz}) - minZ) / cell); k1 = (int)((std::max({t.az, t.bz, t.cz}) - minZ) / cell);
  };
  for (auto& t : tris) {
    int i0, i1, k0, k1;
    range(t, i0, i1, k0, k1);
    for (int k = k0; k <= k1; k++)
      for (int i = i0; i <= i1; i++) count[(size_t)k * nx + i]++;
  }
  start.assign((size_t)nx * nz + 1, 0);
  for (size_t c = 0; c < (size_t)nx * nz; c++) start[c + 1] = start[c] + count[c];
  items.resize(start.back());
  std::vector<uint32_t> fill(start.begin(), start.end() - 1);
  for (uint32_t id = 0; id < tris.size(); id++) {
    int i0, i1, k0, k1;
    range(tris[id], i0, i1, k0, k1);
    for (int k = k0; k <= k1; k++)
      for (int i = i0; i <= i1; i++) items[fill[(size_t)k * nx + i]++] = id;
  }
}

template <class F>
void TrackGround::Grid::query(float x0, float z0, float x1, float z1, F&& f) const {
  if (!nx) return;
  int i0 = std::max(0, (int)((x0 - minX) / cell)), i1 = std::min(nx - 1, (int)((x1 - minX) / cell));
  int k0 = std::max(0, (int)((z0 - minZ) / cell)), k1 = std::min(nz - 1, (int)((z1 - minZ) / cell));
  for (int k = k0; k <= k1; k++)
    for (int i = i0; i <= i1; i++) {
      size_t c = (size_t)k * nx + i;
      for (uint32_t j = start[c]; j < start[c + 1]; j++) f(items[j]);
    }
}

void TrackGround::build(std::vector<Tri> g, std::vector<Tri> w) {
  ground = std::move(g);
  walls = std::move(w);
  gGrid.build(ground, 6);
  wGrid.build(walls, 4);
}

std::optional<phys::RayHit> TrackGround::cast(const V3& o, const V3& d, double maxDist) const {
  V3 e = o + d * maxDist;
  double best = maxDist + 1;
  const Tri* hitTri = nullptr;
  gGrid.query((float)std::min(o.x, e.x), (float)std::min(o.z, e.z), (float)std::max(o.x, e.x), (float)std::max(o.z, e.z), [&](uint32_t id) {
    const Tri& t = ground[id];
    V3 a = A(t), e1 = B(t) - a, e2 = C(t) - a, p = d.cross(e2);
    double det = e1.dot(p);
    if (std::fabs(det) < 1e-12) return;
    double inv = 1 / det;
    V3 s = o - a;
    double u = s.dot(p) * inv;
    if (u < 0 || u > 1) return;
    V3 q = s.cross(e1);
    double v = d.dot(q) * inv;
    if (v < 0 || u + v > 1) return;
    double tt = e2.dot(q) * inv;
    if (tt < 0 || tt > maxDist || tt >= best) return;
    best = tt;
    hitTri = &t;
  });
  if (!hitTri) return std::nullopt;
  V3 n = (B(*hitTri) - A(*hitTri)).cross(C(*hitTri) - A(*hitTri)).normalize();
  if (n.dot(d) > 0) n = n * -1;  // a normal aponta para quem olha
  phys::RayHit h{best, n.x, n.y, n.z, hitTri->mu};
  h.drag = hitTri->drag;
  return h;
}

std::optional<phys::RayHit> TrackGround::raycast(const V3& o, const V3& d, double maxDist) const { return cast(o, d, maxDist); }

phys::GroundQuery TrackGround::heightAt(double x, double z) const {
  auto h = cast(V3(x, 5000, z), V3(0, -1, 0), 10000);
  if (!h) return {-1e9, 0, 1, 0};
  return {5000 - h->dist, h->nx, h->ny, h->nz};
}

phys::GroundQuery TrackGround::heightNear(double x, double z, double y) const {
  // procura a superfície logo abaixo (um pouco acima do ponto, para pegar o que já entrou no chão)
  auto h = cast(V3(x, y + 1.5, z), V3(0, -1, 0), 40);
  if (!h) return {-1e9, 0, 1, 0};
  return {y + 1.5 - h->dist, h->nx, h->ny, h->nz};
}

bool TrackGround::wallContact(const V3& p, double r, V3& normal, double& depth) const {
  bool any = false;
  depth = 0;
  wGrid.query((float)(p.x - r), (float)(p.z - r), (float)(p.x + r), (float)(p.z + r), [&](uint32_t id) {
    const Tri& t = walls[id];
    if (p.y < std::min({t.ay, t.by, t.cy}) - r || p.y > std::max({t.ay, t.by, t.cy}) + r) return;
    V3 q = closestOnTri(p, A(t), B(t), C(t));
    V3 dv = p - q;
    double dist = dv.length();
    if (dist >= r || r - dist <= depth) return;
    depth = r - dist;
    if (dist > 1e-6) normal = dv * (1 / dist);
    else {
      V3 n = (B(t) - A(t)).cross(C(t) - A(t)).normalize();
      normal = V3(n.x, 0, n.z).normalize();
    }
    // muros não empurram para cima nem para baixo
    normal = V3(normal.x, 0, normal.z).normalize();
    any = normal.length() > 0.5;
  });
  return any;
}

// ---------------------------------------------------------------- pista
void TrackMap::loadCPU(const MapEntry& entry, const std::function<void(float)>& onProgress) {
  const MapCfg& cfg = entry.cfg;
  double t0 = nowS();
  GltfAsset g = loadGltf(assetPath(entry.model), [&](float p) { onProgress(0.8f * p); },
                         cfg.textureDir.empty() ? "" : assetPath(cfg.textureDir));
  std::printf("[pista] GLB + texturas: %.1f s\n", nowS() - t0);
  t0 = nowS();
  root = makeNode("track");
  root->add(g.scene);
  root->updateMatrixWorld();

  std::vector<NodePtr> meshes;
  std::function<void(const NodePtr&)> gather = [&](const NodePtr& n) {
    if (n->isMesh()) meshes.push_back(n);
    for (auto& c : n->children) gather(c);
  };
  gather(root);

  // --- correções de material
  std::set<Material*> done;
  for (auto& m : meshes) {
    Material& mt = *m->material;
    if (!done.insert(&mt).second) continue;
    for (TexRef* t : {&mt.map, &mt.normalMap, &mt.roughnessMap, &mt.emissiveMap})
      if (*t) t->tex->anisotropy = 8;
    if (matches(mt.name, cfg.alphaTest)) {
      // árvores, arbustos, cercas e grades: o PNG tem transparência, mas o material veio opaco
      mt.alphaTest = 0.5f;
      mt.transparent = false;
    }
    if (matches(mt.name, cfg.decals)) {
      mt.transparent = true;
      mt.depthWrite = false;
      mt.decal = true;
    }
    if (matches(mt.name, cfg.glass)) {
      mt.transparent = true;
      mt.opacity = 0.3f;
      mt.depthWrite = false;
    }
    // exportador do Blender: cor especular > 1 e colorida (asfalto avermelhado) → neutra
    if (mt.physical) {
      float mx = std::max({mt.specularColor.x, mt.specularColor.y, mt.specularColor.z});
      float mn = std::min({mt.specularColor.x, mt.specularColor.y, mt.specularColor.z});
      if (mx > 1 || mx > mn * 1.3f) mt.specularColor = vec3(1);
    }
  }
  // texturas de detalhe (o Sketchfab exporta só a base clara do shader do Assetto Corsa)
  for (auto& d : cfg.details) {
    TexturePtr tex = loadImage(cfg.textureDir + "/" + d.texture, true);
    if (!tex) continue;
    tex->repeat = true;
    tex->anisotropy = 8;
    for (auto& m : meshes)
      if (matches(m->material->name, d.patterns)) {
        m->material->detailMap = {tex, 0};
        m->material->detailScale = d.scale;
        m->material->detailGain = d.gain;
      }
  }
  for (auto& m : meshes) {
    m->receiveShadow = true;
    m->castShadow = !matches(m->material->name, cfg.noShadow);
  }

  // --- colisão (coordenadas do mundo)
  std::vector<TrackGround::Tri> gTris, wTris;
  std::vector<std::pair<vec3, float>> road;  // centroides do asfalto da largada (para a direção)
  for (auto& m : meshes) {
    const std::string& name = m->material->name;
    const SurfaceRule* rule = nullptr;
    for (auto& r : cfg.surfaces)
      if (matches(name, r.patterns)) { rule = &r; break; }
    bool wall = matches(name, cfg.walls);
    bool isRoad = matches(name, cfg.spawnRoad);
    if (!rule && !wall && !isRoad) continue;
    const Geometry& geo = *m->geometry;
    const mat4& w = m->matrixWorld;
    for (size_t i = 0; i + 2 < geo.idx.size(); i += 3) {
      vec3 a = w.point(geo.pos[geo.idx[i]]), b = w.point(geo.pos[geo.idx[i + 1]]), c = w.point(geo.pos[geo.idx[i + 2]]);
      vec3 cr = cross(b - a, c - a);
      float area = length(cr);
      if (area < 1e-8f) continue;
      float ny = std::fabs(cr.y) / area;
      TrackGround::Tri t{a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, 1, 0};
      if (rule) {
        t.mu = (float)rule->mu;
        t.drag = (float)rule->drag;
        gTris.push_back(t);
      }
      if (wall && ny < 0.6f) wTris.push_back(t);
      if (isRoad) road.push_back({(a + b + c) / 3.0f, area / 2});
    }
  }
  std::printf("[pista] %zu triângulos de chão, %zu de muro\n", gTris.size(), wTris.size());
  std::printf("[pista] colisão: %.1f s\n", nowS() - t0);
  t0 = nowS();
  onProgress(0.85f);

  // --- árvores perto da pista viram o modelo 3D
  if (!cfg.treeModel.empty() && !cfg.treeReplace.empty()) {
    try {
      replaceTrees(cfg, meshes);
    } catch (std::exception& e) {
      std::printf("[pista] árvores 3D desligadas: %s\n", e.what());
    }
    std::printf("[pista] árvores: %.1f s\n", nowS() - t0);
    t0 = nowS();
  }

  // --- divide malhas enormes em blocos de 250 m (o recorte por visibilidade passa a funcionar)
  const float CELL = 400;
  for (auto& m : meshes) {
    Box3 wb = worldBox(*m);
    vec3 sz = wb.size();
    if (std::max(sz.x, sz.z) < CELL * 1.2f) continue;
    Geometry& geo = *m->geometry;
    const mat4& w = m->matrixWorld;
    std::map<std::pair<int, int>, std::vector<uint32_t>> cells;
    for (size_t i = 0; i + 2 < geo.idx.size(); i += 3) {
      vec3 c = (w.point(geo.pos[geo.idx[i]]) + w.point(geo.pos[geo.idx[i + 1]]) + w.point(geo.pos[geo.idx[i + 2]])) / 3.0f;
      auto& v = cells[{(int)std::floor(c.x / CELL), (int)std::floor(c.z / CELL)}];
      v.insert(v.end(), {geo.idx[i], geo.idx[i + 1], geo.idx[i + 2]});
    }
    for (auto& [key, tri] : cells) {
      auto ng = std::make_shared<Geometry>();
      ng->hasTangents = geo.hasTangents;
      std::unordered_map<uint32_t, uint32_t> remap;
      for (uint32_t o : tri) {
        auto it = remap.find(o);
        if (it == remap.end()) {
          it = remap.emplace(o, (uint32_t)ng->pos.size()).first;
          ng->pos.push_back(geo.pos[o]);
          if (!geo.nrm.empty()) ng->nrm.push_back(geo.nrm[o]);
          if (!geo.tan.empty()) ng->tan.push_back(geo.tan[o]);
          if (!geo.uv0.empty()) ng->uv0.push_back(geo.uv0[o]);
          if (!geo.uv1.empty()) ng->uv1.push_back(geo.uv1[o]);
        }
        ng->idx.push_back(it->second);
      }
      ng->computeBounds();
      auto n = makeNode(m->name);
      n->geometry = ng;
      n->material = m->material;
      n->position = m->position;
      n->quaternion = m->quaternion;
      n->scale = m->scale;
      n->castShadow = m->castShadow;
      n->receiveShadow = m->receiveShadow;
      m->parent->add(n);
    }
    m->parent->remove(m.get());
  }
  root->updateMatrixWorld();
  onProgress(0.9f);

  std::printf("[pista] blocos: %.1f s\n", nowS() - t0);
  t0 = nowS();
  ground.build(std::move(gTris), std::move(wTris));
  std::printf("[pista] grade: %.1f s\n", nowS() - t0);
  t0 = nowS();

  // --- largada: embaixo do pórtico de semáforos, alinhada com o asfalto
  Box3 marker;
  for (auto& m : meshes)
    if (matches(m->material->name, cfg.spawnMarker)) marker.expand(worldBox(*m));
  vec3 mc = marker.empty() ? vec3() : marker.center();
  // direção principal (PCA no plano) do asfalto num raio de 40 m
  double sx = 0, sz = 0, sw = 0, cxx = 0, cxz = 0, czz = 0, rx = 0, rz = 0, rw = 0;
  for (auto& [c, a] : road) {
    rx += c.x * a; rz += c.z * a; rw += a;
    if (std::hypot(c.x - mc.x, c.z - mc.z) > 40) continue;
    sx += c.x * a; sz += c.z * a; sw += a;
  }
  vec3 dir(0, 0, -1);
  if (sw > 0) {
    double mx = sx / sw, mz = sz / sw;
    for (auto& [c, a] : road) {
      if (std::hypot(c.x - mc.x, c.z - mc.z) > 40) continue;
      double dx = c.x - mx, dz = c.z - mz;
      cxx += a * dx * dx; cxz += a * dx * dz; czz += a * dz * dz;
    }
    double ang = 0.5 * std::atan2(2 * cxz, cxx - czz);
    dir = vec3((float)std::cos(ang), 0, (float)std::sin(ang));
    // sentido: horário visto de cima → (r × t).y < 0, com r a partir do centro do traçado
    vec3 r(mc.x - (float)(rx / rw), 0, mc.z - (float)(rz / rw));
    float cy = r.z * dir.x - r.x * dir.z;
    if ((cfg.clockwise && cy > 0) || (!cfg.clockwise && cy < 0)) dir = -dir;
  }
  vec3 sp = mc - dir * cfg.gridOffset;
  auto h = ground.heightNear(sp.x, sp.z, mc.y + 2);
  spawnPos = vec3(sp.x, h.h > -1e8 ? (float)h.h : mc.y, sp.z);
  spawnHeading = std::atan2(-dir.x, -dir.z);
  std::printf("[pista] largada em (%.1f %.1f %.1f), direção (%.2f %.2f)\n", spawnPos.x, spawnPos.y, spawnPos.z, dir.x, dir.z);
  t0 = nowS();
  g_surf = SurfaceMap{};
  {
    static const std::vector<std::string> bounds = {"line*", "kerb_new*", "CURB_B*"};
    for (auto& m : meshes) {
      if (!m->geometry) continue;
      // asfalto da pista e o que é pintado/remendado em cima dele (senão viram buracos)
      static const std::vector<std::string> onRoad = {"asph_patch_joint*", "groove*", "texts_new*", "diss2*", "doted_line*"};
      const std::string& mn = m->material->name;
      static const std::vector<std::string> paved = {"Parking*", "road-ext-tile*", "carpet*", "asph*"};
      int type = matches(mn, cfg.spawnRoad) || matches(mn, onRoad) ? 1 : matches(mn, bounds) ? 2 : matches(mn, paved) ? 3 : 0;
      if (!type) continue;
      const Geometry& g = *m->geometry;
      const mat4& w = m->matrixWorld;
      for (size_t i = 0; i + 2 < g.idx.size(); i += 3) {
        vec3 a = w.point(g.pos[g.idx[i]]), b = w.point(g.pos[g.idx[i + 1]]), c = w.point(g.pos[g.idx[i + 2]]);
        g_surf.tris.push_back({a.x, a.z, b.x, b.z, c.x, c.z, type});
      }
    }
    g_surf.build();
  }
  buildRaceLine(sp + dir * cfg.gridOffset, dir);
  g_surf = SurfaceMap{};
  std::printf("[pista] traçado: %zu pontos, %.0f m (%.1f s)\n", line.size(), lineLen, nowS() - t0);
  onProgress(1);
}

// Os "cartazes" de árvore do Spa são quads soltos (2 a 3 cruzados por árvore) fundidos em
// malhas de centenas de milhares de vértices. Aqui eles são agrupados em árvores; as que ficam
// a até treeReplaceDist do asfalto saem da malha e ganham o modelo 3D. Cada uma fica numa célula
// de 160 m com duas versões: o modelo (perto da câmera) e o cartaz original (longe), ver updateLod.
void TrackMap::replaceTrees(const MapCfg& cfg, const std::vector<NodePtr>& meshes) {
  // --- modelo: geometria assada com a base no chão (0) e altura 1
  GltfAsset tg = loadGltf(assetPath(cfg.treeModel), [](float) {});
  tg.scene->updateMatrixWorld();
  struct Part { GeometryPtr geo; MaterialPtr mat; };
  std::vector<Part> parts;
  Box3 mb;
  tg.scene->traverse([&](Node& n) {
    if (!n.isMesh()) return;
    auto g = std::make_shared<Geometry>();
    const Geometry& s = *n.geometry;
    for (size_t i = 0; i < s.pos.size(); i++) {
      vec3 p = n.matrixWorld.point(s.pos[i]);
      g->pos.push_back(p);
      mb.expand(p);
      if (!s.nrm.empty()) g->nrm.push_back(normalize(n.matrixWorld.point(s.pos[i] + s.nrm[i]) - p));
      if (!s.uv0.empty()) g->uv0.push_back(s.uv0[i]);
    }
    g->idx = s.idx;
    n.material->doubleSided = true;
    // partes transparentes da textura da folha são brancas: nos mipmaps (árvore ao longe) elas se
    // misturam às folhas e a copa fica esbranquiçada. Pinta-as com a cor média das folhas.
    if (n.material->alphaTest > 0 && n.material->map && !n.material->map.tex->pixels.empty()) {
      auto& px = n.material->map.tex->pixels;
      double sum[3] = {0, 0, 0}, cnt = 0;
      for (size_t i = 0; i + 3 < px.size(); i += 4)
        if (px[i + 3] > 128) { sum[0] += px[i]; sum[1] += px[i + 1]; sum[2] += px[i + 2]; cnt++; }
      if (cnt > 0)
        for (size_t i = 0; i + 3 < px.size(); i += 4)
          if (px[i + 3] <= 128)
            for (int k = 0; k < 3; k++) px[i + k] = (uint8_t)(sum[k] / cnt);
    }
    // folha e casca foscas: o GLB pede especular 0 (KHR_materials_specular), senão as folhas
    // viradas para o sol estouram em branco
    n.material->physical = true;
    n.material->specularIntensity = 0.05f;
    n.material->roughness = 1;
    n.material->metalness = 0;
    for (TexRef* t : {&n.material->map, &n.material->normalMap})
      if (*t) t->tex->anisotropy = 8;
    parts.push_back({g, n.material});
  });
  if (parts.empty() || mb.empty()) throw std::runtime_error("modelo de árvore vazio");
  float H0 = mb.max.y - mb.min.y;
  vec3 base((mb.min.x + mb.max.x) / 2, mb.min.y, (mb.min.z + mb.max.z) / 2);
  for (auto& p : parts) {
    for (auto& v : p.geo->pos) v = (v - base) / H0;
    p.geo->computeBounds();
  }

  // --- pontos do asfalto numa grade de 20 m (vértices e centros dos triângulos)
  const float RG = 20, R = cfg.treeReplaceDist;
  auto key = [](int x, int z) { return ((int64_t)x << 32) ^ (int64_t)(uint32_t)z; };
  std::unordered_map<int64_t, std::vector<vec2>> rgrid;
  for (auto& m : meshes) {
    if (!matches(m->material->name, cfg.spawnRoad)) continue;
    const Geometry& g = *m->geometry;
    const mat4& w = m->matrixWorld;
    for (size_t i = 0; i + 2 < g.idx.size(); i += 3) {
      vec3 a = w.point(g.pos[g.idx[i]]), b = w.point(g.pos[g.idx[i + 1]]), c = w.point(g.pos[g.idx[i + 2]]);
      for (vec3 p : {a, b, c, (a + b + c) / 3.0f, (a + b) / 2.0f, (b + c) / 2.0f, (a + c) / 2.0f})
        rgrid[key((int)std::floor(p.x / RG), (int)std::floor(p.z / RG))].push_back({p.x, p.z});
    }
  }
  auto nearRoad = [&](float x, float z) {
    int cx = (int)std::floor(x / RG), cz = (int)std::floor(z / RG), r = (int)std::ceil(R / RG);
    for (int i = cx - r; i <= cx + r; i++)
      for (int k = cz - r; k <= cz + r; k++) {
        auto it = rgrid.find(key(i, k));
        if (it == rgrid.end()) continue;
        for (auto& p : it->second)
          if ((p.x - x) * (p.x - x) + (p.y - z) * (p.y - z) < R * R) return true;
      }
    return false;
  };

  // --- células de 160 m (modelo perto / cartaz longe)
  const float CELL = 160;
  std::map<std::pair<int, int>, int> cellOf;
  auto cellFor = [&](float x, float z) {
    std::pair<int, int> k{(int)std::floor(x / CELL), (int)std::floor(z / CELL)};
    auto it = cellOf.find(k);
    if (it != cellOf.end()) return it->second;
    TreeCell c{(k.first + 0.5f) * CELL, (k.second + 0.5f) * CELL, makeNode("trees-near"), makeNode("trees-far")};
    c.near->visible = false;
    root->add(c.near);
    root->add(c.far);
    treeCells.push_back(c);
    return cellOf[k] = (int)treeCells.size() - 1;
  };

  size_t replaced = 0, total = 0;
  for (auto& m : meshes) {
    if (!matches(m->material->name, cfg.treeReplace)) continue;
    Geometry& g = *m->geometry;
    const mat4& w = m->matrixWorld;
    size_t nv = g.pos.size();
    // quads = componentes ligados por vértices
    std::vector<uint32_t> par(nv);
    for (size_t i = 0; i < nv; i++) par[i] = (uint32_t)i;
    auto find = [&](uint32_t a) {
      while (par[a] != a) a = par[a] = par[par[a]];
      return a;
    };
    for (size_t i = 0; i + 2 < g.idx.size(); i += 3) {
      uint32_t a = find(g.idx[i]);
      par[find(g.idx[i + 1])] = a;
      par[find(g.idx[i + 2])] = a;
    }
    std::unordered_map<uint32_t, int> compOf;
    std::vector<Box3> comps;
    for (size_t v = 0; v < nv; v++) {
      uint32_t r = find((uint32_t)v);
      auto it = compOf.find(r);
      if (it == compOf.end()) {
        it = compOf.emplace(r, (int)comps.size()).first;
        comps.emplace_back();
      }
      comps[it->second].expand(w.point(g.pos[v]));
    }
    // quads cruzados com o mesmo centro (até 1 m) = uma árvore
    struct Tree { Box3 b; bool replace = false; };
    std::vector<Tree> trees;
    std::unordered_map<int64_t, std::vector<int>> tgrid;
    std::vector<int> treeOf(comps.size());
    for (size_t c = 0; c < comps.size(); c++) {
      vec3 ctr = comps[c].center();
      int kx = (int)std::floor(ctr.x), kz = (int)std::floor(ctr.z), found = -1;
      for (int i = kx - 1; i <= kx + 1 && found < 0; i++)
        for (int k = kz - 1; k <= kz + 1 && found < 0; k++) {
          auto it = tgrid.find(key(i, k));
          if (it == tgrid.end()) continue;
          for (int t : it->second) {
            vec3 tc = trees[t].b.center();
            if (std::hypot(tc.x - ctr.x, tc.z - ctr.z) < 1.0f) { found = t; break; }
          }
        }
      if (found < 0) {
        found = (int)trees.size();
        trees.emplace_back();
        tgrid[key(kx, kz)].push_back(found);
      }
      trees[found].b.expand(comps[c].min);
      trees[found].b.expand(comps[c].max);
      treeOf[c] = found;
    }
    for (auto& t : trees) {
      vec3 c = t.b.center();
      t.replace = t.b.max.y - t.b.min.y > 3 && nearRoad(c.x, c.z);
    }
    total += trees.size();
    // tira os triângulos das árvores trocadas (eles vão para o cartaz "longe" da célula)
    std::vector<uint32_t> keep;
    std::map<int, std::vector<uint32_t>> far;
    for (size_t i = 0; i + 2 < g.idx.size(); i += 3) {
      int t = treeOf[compOf[find(g.idx[i])]];
      if (!trees[t].replace) {
        keep.insert(keep.end(), {g.idx[i], g.idx[i + 1], g.idx[i + 2]});
        continue;
      }
      vec3 c = trees[t].b.center();
      auto& v = far[cellFor(c.x, c.z)];
      v.insert(v.end(), {g.idx[i], g.idx[i + 1], g.idx[i + 2]});
    }
    g.idx = std::move(keep);
    g.computeBounds();
    vec3 mp, ms;
    quat mq;
    w.decompose(mp, mq, ms);
    for (auto& [cell, tri] : far) {
      auto ng = std::make_shared<Geometry>();
      std::unordered_map<uint32_t, uint32_t> remap;
      for (uint32_t o : tri) {
        auto it = remap.find(o);
        if (it == remap.end()) {
          it = remap.emplace(o, (uint32_t)ng->pos.size()).first;
          ng->pos.push_back(g.pos[o]);
          if (!g.nrm.empty()) ng->nrm.push_back(g.nrm[o]);
          if (!g.uv0.empty()) ng->uv0.push_back(g.uv0[o]);
          if (!g.uv1.empty()) ng->uv1.push_back(g.uv1[o]);
        }
        ng->idx.push_back(it->second);
      }
      ng->computeBounds();
      auto n = makeNode(m->name + "-far");
      n->geometry = ng;
      n->material = m->material;
      n->position = mp;
      n->quaternion = mq;
      n->scale = ms;
      n->castShadow = m->castShadow;
      n->receiveShadow = m->receiveShadow;
      treeCells[cell].far->add(n);
    }
    // o modelo 3D no lugar de cada árvore trocada (altura parecida, giro e tamanho variados)
    for (auto& t : trees) {
      if (!t.replace) continue;
      replaced++;
      vec3 c = t.b.center();
      uint32_t h = (uint32_t)(int)(c.x * 73.13f) * 2654435761u ^ (uint32_t)(int)(c.z * 91.7f) * 40503u;
      float r0 = (h & 0xffff) / 65535.f, r1 = ((h >> 16) & 0xffff) / 65535.f;
      float height = std::clamp(t.b.max.y - t.b.min.y, 5.f, 22.f) * (0.9f + 0.2f * r1);
      quat q = quat::fromAxisAngle({0, 1, 0}, r0 * 6.2831853f);
      NodePtr& grp = treeCells[cellFor(c.x, c.z)].near;
      for (auto& p : parts) {
        auto n = makeNode("tree3d");
        n->geometry = p.geo;
        n->material = p.mat;
        n->position = vec3(c.x, t.b.min.y - 0.15f, c.z);
        n->quaternion = q;
        n->scale = vec3(height);
        n->castShadow = true;
        n->receiveShadow = true;
        grp->add(n);
      }
    }
  }
  std::printf("[pista] árvores: %zu de %zu trocadas pelo modelo 3D (até %.0f m da pista), %zu células\n", replaced, total, R,
              treeCells.size());
}


// Traçado como caminho de menor custo numa grade de 1 m sobre o chão pavimentado: asfalto da pista
// é barato, linha/zebra um pouco mais caro, estacionamento/boxes bem caro, o resto proibido; perto
// da borda custa mais (o caminho fica no meio e evita vias estreitas). Uma barreira na linha de
// largada obriga o caminho a dar a volta inteira.
void TrackMap::buildRaceLine(const vec3& start, const vec3& dir0) {
  line.clear();
  lineS.clear();
  lineHalfW.clear();
  lineLen = 0;
  if (g_surf.tris.empty()) return;
  vec3 dir = normalize(vec3(dir0.x, 0, dir0.z)), right(-dir.z, 0, dir.x);
  float minX = 1e30f, minZ = 1e30f, maxX = -1e30f, maxZ = -1e30f;
  for (auto& t : g_surf.tris) {
    minX = std::min({minX, t.ax, t.bx, t.cx}); maxX = std::max({maxX, t.ax, t.bx, t.cx});
    minZ = std::min({minZ, t.az, t.bz, t.cz}); maxZ = std::max({maxZ, t.az, t.bz, t.cz});
  }
  minX -= 10; minZ -= 10; maxX += 10; maxZ += 10;
  const float C = 1;
  int W = (int)((maxX - minX) / C) + 1, H = (int)((maxZ - minZ) / C) + 1;
  std::vector<uint8_t> type((size_t)W * H, 0);
  // rasteriza (o tipo de prioridade maior vence: pista 1 > limite 2 > outro pavimento 3)
  static const int RANK[4] = {0, 3, 2, 1};
  for (auto& t : g_surf.tris) {
    int x0 = std::max(0, (int)((std::min({t.ax, t.bx, t.cx}) - minX) / C)), x1 = std::min(W - 1, (int)((std::max({t.ax, t.bx, t.cx}) - minX) / C));
    int z0 = std::max(0, (int)((std::min({t.az, t.bz, t.cz}) - minZ) / C)), z1 = std::min(H - 1, (int)((std::max({t.az, t.bz, t.cz}) - minZ) / C));
    for (int z = z0; z <= z1; z++)
      for (int x = x0; x <= x1; x++) {
        float px = minX + (x + 0.5f) * C, pz = minZ + (z + 0.5f) * C;
        float d1 = (px - t.bx) * (t.az - t.bz) - (t.ax - t.bx) * (pz - t.bz);
        float d2 = (px - t.cx) * (t.bz - t.cz) - (t.bx - t.cx) * (pz - t.cz);
        float d3 = (px - t.ax) * (t.cz - t.az) - (t.cx - t.ax) * (pz - t.az);
        bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
        if (neg && pos) continue;
        uint8_t& c = type[(size_t)z * W + x];
        if (RANK[t.type] > RANK[c]) c = (uint8_t)t.type;
      }
  }
  // distância até a borda (fora da pista/limite), chanfro em duas passadas
  std::vector<float> dist((size_t)W * H);
  // borda = tudo que não é asfalto da pista (linhas brancas e zebras inclusive): o meio fica entre as linhas
  for (size_t i = 0; i < dist.size(); i++) dist[i] = type[i] == 1 ? 1e9f : 0;
  auto relax = [&](int x, int z, int dx, int dz, float w) {
    int nx = x + dx, nz = z + dz;
    if (nx < 0 || nz < 0 || nx >= W || nz >= H) return;
    float& d = dist[(size_t)z * W + x];
    d = std::min(d, dist[(size_t)nz * W + nx] + w);
  };
  for (int z = 0; z < H; z++)
    for (int x = 0; x < W; x++) { relax(x, z, -1, 0, 1); relax(x, z, 0, -1, 1); relax(x, z, -1, -1, 1.414f); relax(x, z, 1, -1, 1.414f); }
  for (int z = H - 1; z >= 0; z--)
    for (int x = W - 1; x >= 0; x--) { relax(x, z, 1, 0, 1); relax(x, z, 0, 1, 1); relax(x, z, 1, 1, 1.414f); relax(x, z, -1, 1, 1.414f); }
  auto cell = [&](const vec3& p, int& x, int& z) {
    x = (int)((p.x - minX) / C);
    z = (int)((p.z - minZ) / C);
    return x >= 0 && z >= 0 && x < W && z < H;
  };
  // barreira na largada (faixa transversal de 2 m) para o caminho não voltar direto
  std::vector<uint8_t> blocked((size_t)W * H, 0);
  for (float lat = -40; lat <= 40; lat += 0.5f)
    for (float lon = -1.5f; lon <= 1.5f; lon += 0.5f) {
      int x, z;
      if (cell(start + right * lat + dir * lon, x, z)) blocked[(size_t)z * W + x] = 1;
    }
  int sx, sz, gx, gz;
  // começa logo depois da linha e termina logo antes dela (o centro da pista mais próximo)
  auto snap = [&](vec3 p, int& x, int& z, float side) {
    if (!cell(p, x, z)) return false;
    float best = -1;
    int bx = x, bz = z;
    for (int dz = -12; dz <= 12; dz++)
      for (int dx = -12; dx <= 12; dx++) {
        int nx = x + dx, nz = z + dz;
        if (nx < 0 || nz < 0 || nx >= W || nz >= H || blocked[(size_t)nz * W + nx]) continue;
        vec3 cp(minX + (nx + 0.5f) * C, 0, minZ + (nz + 0.5f) * C);
        if (dot(cp - vec3(start.x, 0, start.z), dir) * side < 2.5f) continue;  // cada um do seu lado da linha
        float d = dist[(size_t)nz * W + nx];
        if (type[(size_t)nz * W + nx] == 1 && d > best) { best = d; bx = nx; bz = nz; }
      }
    x = bx;
    z = bz;
    return best > 0;
  };
  if (!snap(start + dir * 5.f, sx, sz, 1) || !snap(start - dir * 5.f, gx, gz, -1)) {
    std::printf("[pista] traçado: largada fora do asfalto\n");
    return;
  }
  auto stepCost = [&](size_t i) -> float {
    uint8_t t = type[i];
    if (t == 0 || blocked[i]) return -1;
    // cruzar linha/zebra é caro: impede o atalho por áreas de escape asfaltadas (ex.: La Source)
    float base = t == 1 ? 1.f : t == 2 ? 25.f : 40.f;
    float d = std::min(dist[i], 8.f);
    return base * (1 + 12 / ((d + 0.5f) * (d + 0.5f)));
  };
  std::vector<float> g((size_t)W * H, 1e30f);
  std::vector<int> from((size_t)W * H, -1);
  using QE = std::pair<float, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
  int sIdx = sz * W + sx, gIdx = gz * W + gx;
  g[sIdx] = 0;
  pq.push({0, sIdx});
  static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  static const float DW[8] = {1, 1, 1, 1, 1.414f, 1.414f, 1.414f, 1.414f};
  while (!pq.empty()) {
    auto [cg, ci] = pq.top();
    pq.pop();
    if (cg > g[ci]) continue;
    if (ci == gIdx) break;
    int cx = ci % W, cz = ci / W;
    for (int k = 0; k < 8; k++) {
      int nx = cx + DX[k], nz = cz + DZ[k];
      if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
      int ni = nz * W + nx;
      float sc = stepCost(ni);
      if (sc < 0) continue;
      float ng = cg + sc * DW[k] * C;
      if (ng < g[ni]) { g[ni] = ng; from[ni] = ci; pq.push({ng, ni}); }
    }
  }
  if (from[gIdx] < 0) {
    std::printf("[pista] traçado: sem caminho fechando a volta\n");
    return;
  }
  // caminho (do começo ao fim), reamostrado a cada 4 m e suavizado
  std::vector<vec3> raw;
  for (int i = gIdx; i >= 0; i = from[i]) raw.push_back(vec3(minX + (i % W + 0.5f) * C, 0, minZ + (i / W + 0.5f) * C));
  std::reverse(raw.begin(), raw.end());
  raw.insert(raw.begin(), vec3(start.x, 0, start.z));
  for (int pass = 0; pass < 12; pass++) {
    std::vector<vec3> sm = raw;
    for (size_t i = 2; i + 2 < raw.size(); i++) sm[i] = (raw[i - 2] + raw[i - 1] + raw[i] + raw[i + 1] + raw[i + 2]) / 5.f;
    raw = sm;
  }
  float acc = 0, next = 0;
  float y = start.y;
  for (size_t i = 0; i + 1 < raw.size(); i++) {
    float seg = distance(raw[i], raw[i + 1]);
    while (next <= acc + seg) {
      vec3 q = raw[i] + (raw[i + 1] - raw[i]) * ((next - acc) / std::max(seg, 1e-4f));
      auto h = ground.raycast(V3(q.x, y + 8, q.z), V3(0, -1, 0), 30);
      if (h) y = (float)(y + 8 - h->dist);
      q.y = y;
      int x, z;
      float hw = cell(q, x, z) ? std::min(dist[(size_t)z * W + x], 12.f) : 4.f;
      line.push_back(q);
      lineHalfW.push_back(hw);
      next += 4;
    }
    acc += seg;
  }
  size_t n = line.size();
  if (n < 50) { line.clear(); lineHalfW.clear(); return; }
  if (std::getenv("SR_LINEDUMP")) {
    FILE* f = std::fopen(std::getenv("SR_LINEDUMP"), "w");
    for (size_t i = 0; i < n; i++) std::fprintf(f, "%.2f %.2f %.2f %.2f\n", line[i].x, line[i].y, line[i].z, lineHalfW[i]);
    std::fclose(f);
  }
  lineS.resize(n);
  float s2 = 0;
  for (size_t i = 0; i < n; i++) {
    lineS[i] = s2;
    s2 += distance(line[i], line[(i + 1) % n]);
  }
  lineLen = s2;
}

int TrackMap::nearestOnLine(const vec3& p, int hint, float* sOut) const {
  int n = (int)line.size();
  if (!n) return -1;
  int best = 0;
  float bd = 1e30f;
  auto test = [&](int i) {
    i = ((i % n) + n) % n;
    vec3 a = line[i], b = line[(i + 1) % n];
    vec3 ab = b - a;
    float t = std::clamp(dot(p - a, ab) / std::max(1e-4f, dot(ab, ab)), 0.f, 1.f);
    vec3 q = a + ab * t;
    float dd = (p.x - q.x) * (p.x - q.x) + (p.z - q.z) * (p.z - q.z) + 0.2f * (p.y - q.y) * (p.y - q.y);
    if (dd < bd) {
      bd = dd;
      best = i;
      if (sOut) *sOut = lineS[i] + t * length(ab);
    }
  };
  if (hint < 0) for (int i = 0; i < n; i++) test(i);
  else for (int k = -40; k <= 40; k++) test(hint + k);
  return best;
}

vec3 TrackMap::lineAt(float s) const {
  int n = (int)line.size();
  if (!n) return vec3();
  s = std::fmod(std::fmod(s, lineLen) + lineLen, lineLen);
  int i = (int)(std::upper_bound(lineS.begin(), lineS.end(), s) - lineS.begin()) - 1;
  i = std::clamp(i, 0, n - 1);
  vec3 a = line[i], b = line[(i + 1) % n];
  float segLen = (i + 1 < n ? lineS[i + 1] : lineLen) - lineS[i];
  float t = segLen > 1e-4f ? (s - lineS[i]) / segLen : 0;
  return a + (b - a) * t;
}

vec3 TrackMap::lineDir(float s) const {
  vec3 d = lineAt(s + 2) - lineAt(s - 2);
  d.y = 0;
  return length(d) > 1e-4f ? normalize(d) : vec3(0, 0, -1);
}

void TrackMap::updateLod(const vec3& cam) {
  for (auto& c : treeCells) {
    // o centro da célula está a até ~113 m das bordas; 270 m deixa o modelo a uns 150+ m à frente
    bool near = std::hypot(c.x - cam.x, c.z - cam.z) < 270;
    c.near->visible = near;
    c.far->visible = !near;
  }
}

void TrackMap::upload() {
  double tg = 0, tt = 0;
  size_t ng = 0, nt = 0, px = 0;
  root->traverse([&](Node& n) {
    if (!n.isMesh()) return;
    double t0 = nowS();
    n.geometry->upload();
    tg += nowS() - t0;
    ng++;
    Material& m = *n.material;
    t0 = nowS();
    for (TexRef* t : {&m.map, &m.metalnessMap, &m.roughnessMap, &m.normalMap, &m.aoMap, &m.emissiveMap, &m.clearcoatMap,
                      &m.clearcoatRoughnessMap, &m.clearcoatNormalMap, &m.specularIntensityMap, &m.specularColorMap, &m.detailMap})
      if (*t && !t->tex->id) {
        px += (size_t)t->tex->width * t->tex->height;
        nt++;
        t->tex->upload();
      }
    tt += nowS() - t0;
  });
  std::printf("[pista] GPU: %zu malhas em %.1f s, %zu texturas (%.0f Mpx) em %.1f s\n", ng, tg, nt, px / 1e6, tt);
}
