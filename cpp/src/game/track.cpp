#include "track.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "../core/assets.h"
#include <chrono>
static double nowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

using phys::V3;

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
  onProgress(1);
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
