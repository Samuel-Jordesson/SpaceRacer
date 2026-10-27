#include "car_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace {

bool matches(const std::string& name, const std::vector<std::string>& patterns) {
  if (name.empty()) return false;
  for (auto& p : patterns) {
    if (!p.empty() && p.back() == '*') {
      if (name.compare(0, p.size() - 1, p, 0, p.size() - 1) == 0) return true;
    } else if (name == p) {
      return true;
    }
  }
  return false;
}

std::string matName(const Node& m) { return m.material ? m.material->name : ""; }
vec3 centerOf(Node& o) { return worldBox(o).center(); }

// Inverte a ordem dos triângulos se a maioria contradiz as normais dos vértices.
bool fixWinding(Geometry& g) {
  if (g.nrm.empty()) return false;
  size_t tris = g.idx.empty() ? g.pos.size() / 3 : g.idx.size() / 3;
  auto vi = [&](size_t k) { return g.idx.empty() ? (uint32_t)k : g.idx[k]; };
  int agree = 0, disagree = 0;
  size_t step = std::max<size_t>(1, tris / 3000);
  for (size_t f = 0; f < tris; f += step) {
    uint32_t i0 = vi(f * 3), i1 = vi(f * 3 + 1), i2 = vi(f * 3 + 2);
    vec3 a = g.pos[i0], b = g.pos[i1], c = g.pos[i2];
    vec3 t = cross(c - b, a - b);  // normal geométrica com o sinal do winding
    vec3 n = g.nrm[i0] + g.nrm[i1] + g.nrm[i2];
    float d = dot(t, n);
    if (d > 0) agree++;
    else if (d < 0) disagree++;
  }
  if (disagree <= agree * 1.5) return false;
  if (!g.idx.empty()) {
    for (size_t k = 0; k + 2 < g.idx.size(); k += 3) std::swap(g.idx[k + 1], g.idx[k + 2]);
  } else {
    g.idx.resize(g.pos.size());
    for (size_t k = 0; k < g.pos.size(); k++) g.idx[k] = (uint32_t)(k % 3 == 1 ? k + 1 : k % 3 == 2 ? k - 1 : k);
  }
  return true;
}

// Fração de setores angulares (em torno do eixo X da roda) ocupados pelos vértices da malha.
float angularCoverage(Node& m, const vec3& wheelCenter) {
  const auto& pos = m.geometry->pos;
  bool bins[24] = {};
  size_t step = std::max<size_t>(1, pos.size() / 4000);
  for (size_t i = 0; i < pos.size(); i += step) {
    vec3 t = m.matrixWorld.point(pos[i]) - wheelCenter;
    if (std::hypot(t.y, t.z) < 0.04f) continue;
    float a = std::atan2(t.y, t.z);
    bins[(int)std::floor(((a + M_PI) / (2 * M_PI)) * 24) % 24] = true;
  }
  int s = 0;
  for (bool b : bins) s += b;
  return s / 24.f;
}

// Eixo normal ao plano do volante: autovetor de menor variância (iteração de potência em tr(C)·I − C).
vec3 columnAxis(const std::vector<Node*>& meshes, const vec3& center) {
  double C[9] = {};
  long n = 0;
  for (Node* m : meshes)
    for (auto& p : m->geometry->pos) {
      vec3 v = m->matrixWorld.point(p) - center;
      double a[3] = {v.x, v.y, v.z};
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) C[r * 3 + c] += a[r] * a[c];
      n++;
    }
  double tr = (C[0] + C[4] + C[8]) / n;
  double M[9];
  for (int i = 0; i < 9; i++) M[i] = (i % 4 == 0 ? tr : 0) - C[i] / n;
  vec3 x = normalize(vec3(0.1f, 0.3f, 1));
  for (int k = 0; k < 60; k++)
    x = normalize(vec3((float)(M[0] * x.x + M[1] * x.y + M[2] * x.z), (float)(M[3] * x.x + M[4] * x.y + M[5] * x.z),
                       (float)(M[6] * x.x + M[7] * x.y + M[8] * x.z)));
  return x;
}

// Divide uma malha em metade esquerda/direita (pelo centróide de cada triângulo no x do mundo).
// axis: 0 = x (esquerda/direita), 2 = z (frente/trás); "at" = coordenada do corte no mundo
std::vector<NodePtr> splitByAxis(const NodePtr& mesh, int axis, float at) {
  Geometry& geo = *mesh->geometry;
  size_t nIdx = geo.idx.empty() ? geo.pos.size() : geo.idx.size();
  auto vi = [&](size_t k) { return geo.idx.empty() ? (uint32_t)k : geo.idx[k]; };
  std::vector<uint32_t> side[2];
  for (size_t t = 0; t + 2 < nIdx; t += 3) {
    float c = 0;
    for (int k = 0; k < 3; k++) {
      vec3 p = mesh->matrixWorld.point(geo.pos[vi(t + k)]);
      c += axis == 0 ? p.x : p.z;
    }
    auto& s = side[c / 3 < at ? 0 : 1];
    s.push_back(vi(t)); s.push_back(vi(t + 1)); s.push_back(vi(t + 2));
  }
  if (side[0].empty() || side[1].empty()) return {};
  std::vector<NodePtr> out;
  for (int i = 0; i < 2; i++) {
    // compacta: só os vértices usados por esta metade
    std::unordered_map<uint32_t, uint32_t> remap;
    std::vector<uint32_t> order;
    auto g = std::make_shared<Geometry>();
    g->hasTangents = geo.hasTangents;
    for (uint32_t o : side[i]) {
      auto it = remap.find(o);
      if (it == remap.end()) {
        it = remap.emplace(o, (uint32_t)order.size()).first;
        order.push_back(o);
      }
      g->idx.push_back(it->second);
    }
    for (uint32_t o : order) {
      g->pos.push_back(geo.pos[o]);
      if (!geo.nrm.empty()) g->nrm.push_back(geo.nrm[o]);
      if (!geo.tan.empty()) g->tan.push_back(geo.tan[o]);
      if (!geo.uv0.empty()) g->uv0.push_back(geo.uv0[o]);
      if (!geo.uv1.empty()) g->uv1.push_back(geo.uv1[o]);
    }
    g->computeBounds();
    auto m = makeNode(mesh->name + (axis == 0 ? (i ? "_R" : "_L") : (i ? "_zpos" : "_zneg")));
    m->geometry = g;
    m->material = mesh->material;
    m->position = mesh->position;
    m->quaternion = mesh->quaternion;
    m->scale = mesh->scale;
    mesh->parent->add(m);
    out.push_back(m);
  }
  mesh->parent->remove(mesh.get());
  return out;
}

MaterialPtr clone(const MaterialPtr& m) { return std::make_shared<Material>(*m); }

}  // namespace

void LoadedCar::upload() {
  root->traverse([](Node& n) {
    if (!n.isMesh()) return;
    n.geometry->upload();
    Material& m = *n.material;
    for (TexRef* t : {&m.map, &m.metalnessMap, &m.roughnessMap, &m.normalMap, &m.aoMap, &m.emissiveMap, &m.clearcoatMap,
                      &m.clearcoatRoughnessMap, &m.clearcoatNormalMap, &m.specularIntensityMap, &m.specularColorMap})
      if (*t) t->tex->upload();
  });
}

std::shared_ptr<LoadedCar> loadCar(const std::string& path, const VisualCfg& cfg, const std::function<void(float)>& onProgress) {
  GltfAsset gltf = loadGltf(path, onProgress);
  NodePtr model = gltf.scene;
  model->updateMatrixWorld();

  std::vector<NodePtr> meshes;
  std::function<void(const NodePtr&)> gather = [&](const NodePtr& n) {
    if (n->isMesh()) meshes.push_back(n);
    for (auto& c : n->children) gather(c);
  };
  gather(model);
  // várias malhas do GLB têm a ordem dos vértices invertida em relação às normais; com material
  // de dupla face a normal é invertida e a pintura vira espelho (Fresnel). Corrige a ordem.
  int fixed = 0;
  for (auto& m : meshes) fixed += fixWinding(*m->geometry);
  std::printf("[carModel] winding corrigido em %d de %zu malhas\n", fixed, meshes.size());

  auto has = [&](const Node& m, const std::vector<std::string>& pats) { return matches(matName(m), pats); };

  // modelos com as rodas esquerda+direita na mesma malha: divide pelo eixo lateral
  if (!cfg.splitWheels.empty()) {
    for (auto m : std::vector<NodePtr>(meshes)) {
      if (!has(*m, cfg.splitWheels)) continue;
      auto halves = splitByAxis(m, 0, 0);
      if (halves.empty()) continue;
      auto it = std::find(meshes.begin(), meshes.end(), m);
      it = meshes.erase(it);
      meshes.insert(it, halves.begin(), halves.end());
    }
    model->updateMatrixWorld();
  }
  // e modelos com os eixos dianteiro e traseiro na mesma malha: divide também frente/trás
  if (!cfg.splitWheelsZ.empty()) {
    for (auto m : std::vector<NodePtr>(meshes)) {
      if (!has(*m, cfg.splitWheelsZ)) continue;
      auto halves = splitByAxis(m, 2, cfg.splitZ);
      if (halves.empty()) continue;
      auto it = std::find(meshes.begin(), meshes.end(), m);
      it = meshes.erase(it);
      meshes.insert(it, halves.begin(), halves.end());
    }
    model->updateMatrixWorld();
  }

  // peças que não fazem parte do carro (sobras do arquivo): some com elas
  if (!cfg.hideNodes.empty()) {
    std::vector<NodePtr> keep;
    for (auto& m : meshes) {
      if (std::find(cfg.hideNodes.begin(), cfg.hideNodes.end(), m->name) != cfg.hideNodes.end()) {
        if (m->parent) m->parent->remove(m.get());
      } else keep.push_back(m);
    }
    meshes = keep;
  }
  // exportações do Blender às vezes põem a textura de cor também como emissão: o carro "brilharia"
  if (cfg.stripEmissive) {
    std::set<Material*> done;
    for (auto& m : meshes) {
      if (!m->material || !done.insert(m->material.get()).second) continue;
      m->material->emissive = vec3(0, 0, 0);
      m->material->emissiveMap = {};
    }
  }

  // --- 1. pneus e eixos
  std::vector<NodePtr> tires;
  for (auto& m : meshes)
    if (has(*m, cfg.tires)) tires.push_back(m);
  if (tires.size() != 4) throw std::runtime_error("Esperava 4 pneus, encontrei " + std::to_string(tires.size()));
  std::vector<vec3> tc;
  for (auto& t : tires) tc.push_back(centerOf(*t));
  // pares mais próximos = eixos
  const int combos[3][4] = {{0, 1, 2, 3}, {0, 2, 1, 3}, {0, 3, 1, 2}};
  float bestS = 1e30f;
  std::array<int, 2> axA{}, axB{};
  for (auto& c : combos) {
    float s = distance(tc[c[0]], tc[c[1]]) + distance(tc[c[2]], tc[c[3]]);
    if (s < bestS) { bestS = s; axA = {c[0], c[1]}; axB = {c[2], c[3]}; }
  }
  auto mid = [&](std::array<int, 2> a) { return (tc[a[0]] + tc[a[1]]) * 0.5f; };
  std::vector<vec3> lights;
  for (auto& m : meshes)
    if (!cfg.frontHint.empty() ? (has(*m, cfg.frontHint) || matches(m->name, cfg.frontHint)) : has(*m, cfg.headlights))
      lights.push_back(centerOf(*m));
  vec3 lightPos = lights.empty() ? mid(axA) : lights[0];
  if (distance(mid(axB), lightPos) < distance(mid(axA), lightPos)) std::swap(axA, axB);
  auto frontAxle = axA, rearAxle = axB;

  // --- 2. base ortonormal do carro no espaço do modelo
  vec3 fwd = normalize(mid(frontAxle) - mid(rearAxle));
  vec3 lat = tc[frontAxle[1]] - tc[frontAxle[0]];
  vec3 up = normalize(cross(lat, fwd));
  if (up.y < 0) up = -up;
  vec3 right = normalize(cross(fwd, up));
  up = normalize(cross(right, fwd));
  // matriz canônico → modelo tem colunas (right, up, −fwd); a inversa leva o modelo ao canônico
  mat4 toCanon = mat4::basis(right, up, -fwd).inverse();

  auto car = std::make_shared<LoadedCar>();
  NodePtr root = car->root = makeNode("car-visual");
  NodePtr oriented = makeNode("oriented");
  oriented->add(model);
  oriented->quaternion = quatFromMatrix(toCanon);
  root->add(oriented);
  root->updateMatrixWorld();

  // escala: o entre-eixos medido tem de bater com o do carro real
  if (cfg.wheelbase > 0) {
    std::vector<vec3> c;
    for (auto& t : tires) c.push_back(centerOf(*t));
    float wbNow = length(c[frontAxle[0]] + c[frontAxle[1]] - c[rearAxle[0]] - c[rearAxle[1]]) / 2;
    oriented->scale = vec3(cfg.wheelbase / wbNow);
    root->updateMatrixWorld();
  }

  // --- 3. medidas no referencial canônico
  std::vector<Box3> tireBoxes;
  std::vector<vec3> tireCenters;
  std::vector<float> radii;
  float groundY = 1e30f;
  for (auto& t : tires) {
    Box3 b = worldBox(*t);
    tireBoxes.push_back(b);
    tireCenters.push_back(b.center());
    radii.push_back(b.size().y / 2);
    groundY = std::min(groundY, b.min.y);
  }
  vec3 fMid = (tireCenters[frontAxle[0]] + tireCenters[frontAxle[1]]) * 0.5f;
  vec3 rMid = (tireCenters[rearAxle[0]] + tireCenters[rearAxle[1]]) * 0.5f;
  vec3 center = (fMid + rMid) * 0.5f;
  oriented->position = vec3(-center.x, -groundY, -center.z);
  root->updateMatrixWorld();
  for (auto& c : tireCenters) c += oriented->position;

  car->geometry.wheelbase = distance(fMid, rMid);
  car->geometry.trackFront = distance(tireCenters[frontAxle[0]], tireCenters[frontAxle[1]]);
  car->geometry.trackRear = distance(tireCenters[rearAxle[0]], tireCenters[rearAxle[1]]);
  car->geometry.radiusFront = (radii[frontAxle[0]] + radii[frontAxle[1]]) / 2;
  car->geometry.radiusRear = (radii[rearAxle[0]] + radii[rearAxle[1]]) / 2;

  // --- 4. separa peças de cada roda
  std::string wheelIds[4];
  for (int i : {frontAxle[0], frontAxle[1], rearAxle[0], rearAxle[1]}) {
    bool front = i == frontAxle[0] || i == frontAxle[1];
    bool left = tireCenters[i].x < 0;
    wheelIds[i] = std::string(front ? "F" : "R") + (left ? "L" : "R");
  }
  for (int i = 0; i < 4; i++) {
    WheelPivot p;
    p.steer = makeNode("steer-" + wheelIds[i]);
    p.steer->position = tireCenters[i];
    p.spin = makeNode("spin-" + wheelIds[i]);
    p.steer->add(p.spin);
    root->add(p.steer);
    p.base = tireCenters[i];
    p.radius = radii[i];
    car->pivots[wheelIds[i]] = p;
  }
  root->updateMatrixWorld();

  int report = 0;
  for (auto& m : meshes) {
    Box3 box = worldBox(*m);
    vec3 size = box.size(), c = box.center();
    int owner = -1;
    for (int i = 0; i < 4; i++) {
      const vec3& d = tireCenters[i];
      float r = radii[i];
      if (std::max(size.y, size.z) > r * 2.25f || size.x > r * 1.6f) continue;
      if (std::fabs(c.y - d.y) < r * 0.9f && std::fabs(c.z - d.z) < r * 0.9f && std::fabs(c.x - d.x) < r * 0.9f) { owner = i; break; }
    }
    if (owner < 0) continue;
    WheelPivot& p = car->pivots[wheelIds[owner]];
    // peças que cobrem a volta inteira em torno do eixo giram (pneu, aro, disco);
    // as demais (pinça de freio, suporte) só esterçam.
    vec3 off = c - tireCenters[owner];
    bool rotating = std::hypot(off.y, off.z) < 0.03f || angularCoverage(*m, tireCenters[owner]) > 0.8f;
    if (!rotating) {
      // peça fixa só acompanha o esterço se estiver dentro do aro (evita levar pedaços do para-lama)
      if (std::hypot(off.y, off.z) > p.radius * 0.6f || std::fabs(off.x) > p.radius * 0.5f) continue;
    }
    (rotating ? p.spin : p.steer)->attach(m);
    report++;
  }
  std::printf("[carModel] %d peças presas às rodas\n", report);

  // --- brilho do disco de freio: cada roda ganha seu próprio material, que acende com a temperatura
  for (auto& [id, p] : car->pivots) {
    p.steer->traverse([&](Node& o) {
      if (!o.isMesh()) return;
      if (has(o, cfg.brakeGlow)) {
        auto m = clone(o.material);
        m->transparent = true;
        m->opacity = 0;
        m->depthWrite = false;
        m->additive = true;
        m->emissive = vec3(1, 0.38f, 0.08f);
        m->emissiveMap = m->map;
        m->emissiveIntensity = 0;
        m->color = vec3(0);
        m->glowQuad = true;
        o.material = m;
        o.castShadow = false;
        o.noShadow = true;
        p.glow.push_back(m);
      } else if (has(o, cfg.discs)) {
        auto m = clone(o.material);
        m->emissive = vec3(1, 0.3f, 0.05f);
        m->emissiveIntensity = 0;
        o.material = m;
        p.glow.push_back(m);
      } else if (has(o, cfg.rimBlur)) {
        // disco de "roda borrada" do modelo: aparece com a roda girando rápido
        auto m = clone(o.material);
        m->transparent = true;
        m->opacity = 0;
        m->depthWrite = false;
        o.material = m;
        o.noShadow = true;
        p.blur.push_back(m);
      }
    });
  }

  // --- lanternas traseiras: acendem mais forte ao frear
  for (auto& m : meshes) {
    if (!has(*m, cfg.tailLights)) continue;
    if (std::find(car->tailLights.begin(), car->tailLights.end(), m->material) == car->tailLights.end()) car->tailLights.push_back(m->material);
  }
  // por nome de peça: ganham material próprio (a mesma lâmpada pode ter sido dividida em frente/trás)
  for (auto& m : meshes) {
    if (std::find(cfg.tailLightNodes.begin(), cfg.tailLightNodes.end(), m->name) == cfg.tailLightNodes.end()) continue;
    m->material = std::make_shared<Material>(*m->material);
    car->tailLights.push_back(m->material);
  }
  for (auto& m : car->tailLights) {
    m->emissive = vec3(1, 0.06f, 0.03f);
    if (cfg.tailLightUseMap && m->map) {
      m->emissive = vec3(1, 0.25f, 0.2f);
      m->emissiveMap = m->map;
    }
    m->emissiveIntensity = 0.6f;
  }
  // faróis acesos (carro de corrida anda com farol ligado)
  if (cfg.headlightGlow > 0) {
    std::set<Material*> done;
    for (auto& m : meshes) {
      if (!has(*m, cfg.headlights) || done.count(m->material.get())) continue;
      Material& mt = *m->material;
      done.insert(&mt);
      mt.emissive = vec3(1, 0.97f, 0.9f);
      if (mt.map) mt.emissiveMap = mt.map;
      mt.emissiveIntensity = cfg.headlightGlow;
    }
  }

  // --- volante: pivô no centro, girando em torno do eixo da coluna
  std::vector<Node*> swMeshes;
  std::vector<NodePtr> swPtrs;
  for (auto& m : meshes)
    if (has(*m, cfg.steeringWheel) || std::find(cfg.steeringWheelNodes.begin(), cfg.steeringWheelNodes.end(), m->name) != cfg.steeringWheelNodes.end()) {
      swMeshes.push_back(m.get());
      swPtrs.push_back(m);
    }
  if (!swMeshes.empty()) {
    root->updateMatrixWorld();
    Box3 box;
    for (Node* m : swMeshes) box.expand(worldBox(*m));
    vec3 c = box.center();
    vec3 axis = columnAxis(swMeshes, c);
    if (axis.z < 0) axis = -axis;  // aponta para o piloto
    // volantes com botões/borboletas confundem a análise; usa coluna típica de GT3 (~18°)
    if (axis.z < 0.75f) axis = vec3(0, std::sin(0.31f), std::cos(0.31f));
    NodePtr pivot = makeNode("steering-wheel");
    pivot->position = c;
    root->add(pivot);
    root->updateMatrixWorld();
    for (auto& m : swPtrs) pivot->attach(m);
    car->steeringWheel = LoadedCar::SteeringWheel{pivot, axis};
  }

  // camadas da carroceria (malhas do tamanho do carro) não podem ser metal puro
  std::set<Material*> bodyMats;
  if (cfg.fixBodyMetal)
    for (auto& m : meshes) {
      vec3 s = worldBox(*m).size();
      if (s.z > 3 && m->material->metalness > 0.5f && !has(*m, cfg.glass)) bodyMats.insert(m->material.get());
    }
  // pintura sólida (a decoração está na textura): sem metal, verniz por cima
  for (Material* mt : bodyMats) {
    mt->metalness = 0;
    mt->roughness = 0.38f;
    if (mt->physical) mt->specularIntensity = 1;
  }
  std::set<Material*> tuned;
  std::unordered_map<Material*, MaterialPtr> paintCache;
  root->traverse([&](Node& o) {
    if (!o.isMesh()) return;
    o.castShadow = !o.noShadow;
    o.receiveShadow = true;
    Material* mt = o.material.get();
    if (!tuned.count(mt)) {
      tuned.insert(mt);
      mt->envMapIntensity = 1;
      // vidros: o autor pintou o material de verde. O policarbonato do GT3 real é incolor:
      // vidro neutro levemente fumê, liso, com reflexo do céu.
      if (matches(mt->name, cfg.glass)) {
        mt->color = vec3(0.02f, 0.022f, 0.024f);
        mt->opacity = 0.2f;
        mt->metalness = 0;
        mt->roughness = 0.03f;
        if (mt->physical) { mt->clearcoat = 1; mt->clearcoatRoughness = 0.02f; }
      } else {
        // pintura: verniz (clearcoat) liso, como carro de pista recém-polido
        if (mt->physical && mt->clearcoat > 0) {
          // o glTF deixa metalness = 1 quando não informado; tinta metálica real é base parcial + verniz
          if (!bodyMats.count(mt)) { mt->metalness = std::min(mt->metalness, 0.45f); mt->roughness = std::min(mt->roughness, 0.42f); }
          mt->clearcoat = 0.85f;
          mt->clearcoatRoughness = std::min(mt->clearcoatRoughness, 0.06f);
        }
        if (matches(mt->name, cfg.tires)) { mt->metalness = 0; mt->roughness = 0.82f; }
      }
    }
    // pintura sem verniz no arquivo: troca por material físico com clearcoat
    if (!o.material->physical && matches(o.material->name, cfg.paint)) {
      auto& p = paintCache[o.material.get()];
      if (!p) {
        p = clone(o.material);
        p->physical = true;
        p->clearcoat = 1;
        p->clearcoatRoughness = 0.03f;
        p->metalness = std::min(p->metalness, 0.35f);
        p->roughness = std::max(p->roughness, 0.25f);
        p->specularIntensity = 1;
        p->specularColor = vec3(1);
        p->ior = 1.5f;
      }
      o.material = p;
    }
  });

  // posição do olho do piloto (cockpit) e da cabeça/capacete a esconder nessa câmera
  car->cockpit = cfg.cockpit;
  if (!car->cockpit && car->steeringWheel) {
    vec3 c = car->steeringWheel->pivot->position;
    car->cockpit = VisualCfg::Cockpit{{c.x, c.y + 0.29f, c.z + 0.40f}, {c.x, c.y + 0.26f, c.z + 0.58f}};
  }

  // modelo sem interior: a câmera "Cockpit" vira câmera de para-choque (baixa, na ponta do carro)
  if (!car->cockpit && !car->steeringWheel) {
    root->updateMatrixWorld();
    Box3 b = worldBox(*root);
    car->cockpit = VisualCfg::Cockpit{vec3(0, 0.45f, b.min.z - 0.05f), vec3(0, -10, 0)};  // nenhum capacete a esconder
  }

  // câmera de capô: acha a superfície do capô com um raio vindo de cima, à frente do para-brisa
  car->hoodEye = cfg.hoodEye;
  if (!car->hoodEye) {
    // sem volante (modelo sem interior): logo atrás do eixo dianteiro
    float z = car->steeringWheel ? car->steeringWheel->pivot->position.z - 0.7f : -car->geometry.wheelbase / 2 + 0.55f;
    root->updateMatrixWorld();
    for (auto& hit : raycast(*root, vec3(0, 3, z), vec3(0, -1, 0), 0, 3)) {
      if (matches(matName(*hit.object), cfg.glass)) continue;
      car->hoodEye = vec3(0, hit.point.y + 0.14f, z);
      break;
    }
  }

  if (std::getenv("SR_DEBUG_MATS")) {
    std::set<Material*> seen;
    root->traverse([&](Node& o) {
      if (!o.isMesh() || seen.count(o.material.get())) return;
      Material& m = *o.material;
      seen.insert(&m);
      vec3 sz = worldBox(o).size();
      std::printf("mat %-28s phys=%d metal=%.2f rough=%.2f cc=%.2f map=%d(uv%d) mr=%d nrm=%d ao=%d em=(%.2f %.2f %.2f)x%.2f tr=%d op=%.2f col=(%.2f %.2f %.2f) spec=%.2f(%.2f %.2f %.2f) sz=%.2f,%.2f,%.2f\n",
                  m.name.c_str(), m.physical, m.metalness, m.roughness, m.clearcoat, (bool)m.map, m.map.uv, (bool)m.roughnessMap,
                  (bool)m.normalMap, (bool)m.aoMap, m.emissive.x, m.emissive.y, m.emissive.z, m.emissiveIntensity, m.transparent, m.opacity,
                  m.color.x, m.color.y, m.color.z, m.specularIntensity, m.specularColor.x, m.specularColor.y, m.specularColor.z, sz.x, sz.y, sz.z);
    });
  }
  car->attribution = gltf;
  car->attribution.scene = nullptr;
  root->updateMatrixWorld();

  // carroceria para a oficina: os materiais de pintura informados ou, sem isso, o de maior área
  // (fora vidros, pneus e rodas); entram também os que têm pelo menos metade dessa área
  car->box = worldBox(*root);
  {
    std::map<Material*, std::pair<MaterialPtr, double>> area;
    root->traverse([&](Node& o) {
      if (!o.isMesh() || matches(o.material->name, cfg.glass) || matches(o.material->name, cfg.tires) ||
          matches(o.material->name, cfg.splitWheels) || o.material->transparent)
        return;
      const Geometry& g = *o.geometry;
      double a = 0;
      for (size_t i = 0; i + 2 < g.idx.size(); i += 3) {
        vec3 p0 = o.matrixWorld.point(g.pos[g.idx[i]]), p1 = o.matrixWorld.point(g.pos[g.idx[i + 1]]),
             p2 = o.matrixWorld.point(g.pos[g.idx[i + 2]]);
        a += length(cross(p1 - p0, p2 - p0)) / 2;
      }
      auto& e = area[o.material.get()];
      e.first = o.material;
      e.second += a;
    });
    double best = 0;
    for (auto& [k, v] : area) best = std::max(best, v.second);
    for (auto& [k, v] : area) {
      bool named = !cfg.paint.empty() && matches(k->name, cfg.paint);
      if (named || (cfg.paint.empty() && v.second >= best * 0.5)) {
        car->paintMats.push_back(v.first);
        Material& m = *v.first;
        car->paintOrig.push_back({m.map, m.color, m.metalness, m.roughness, m.clearcoat, m.clearcoatRoughness});
      }
    }
  }
  // aros: materiais das peças que giram com cada roda, menos pneu, disco, rodas borradas e brilho
  {
    std::set<Material*> skip, seen;
    for (auto& [id, p] : car->pivots) {
      for (auto& m : p.glow) skip.insert(m.get());
      for (auto& m : p.blur) skip.insert(m.get());
    }
    for (auto& [id, p] : car->pivots)
      p.spin->traverse([&](Node& o) {
        if (!o.isMesh()) return;
        Material* mt = o.material.get();
        const std::string& n = mt->name;
        if (skip.count(mt) || seen.count(mt) || matches(n, cfg.tires) || matches(n, cfg.discs) || matches(n, cfg.rimBlur) ||
            matches(n, cfg.brakeGlow) || mt->transparent)
          return;
        seen.insert(mt);
        car->rimMats.push_back(o.material);
        car->rimOrig.push_back({mt->map, mt->color, mt->metalness, mt->roughness, mt->clearcoat, mt->clearcoatRoughness});
      });
  }
  return car;
}

std::shared_ptr<LoadedCar> cloneCar(const LoadedCar& src) {
  auto car = std::make_shared<LoadedCar>();
  std::unordered_map<const Node*, NodePtr> nodes;
  std::unordered_map<const Material*, MaterialPtr> mats;
  auto matOf = [&](const MaterialPtr& m) -> MaterialPtr {
    if (!m) return m;
    auto& c = mats[m.get()];
    if (!c) c = std::make_shared<Material>(*m);
    return c;
  };
  std::function<NodePtr(const Node&)> copy = [&](const Node& n) {
    auto c = makeNode(n.name);
    c->position = n.position;
    c->quaternion = n.quaternion;
    c->scale = n.scale;
    c->visible = n.visible;
    c->geometry = n.geometry;
    c->material = matOf(n.material);
    c->castShadow = n.castShadow;
    c->receiveShadow = n.receiveShadow;
    c->noShadow = n.noShadow;
    c->renderOrder = n.renderOrder;
    nodes[&n] = c;
    for (auto& ch : n.children) c->add(copy(*ch));
    return c;
  };
  car->root = copy(*src.root);
  auto nodeOf = [&](const NodePtr& n) { return n ? nodes[n.get()] : NodePtr(); };
  car->geometry = src.geometry;
  for (auto& [id, p] : src.pivots) {
    WheelPivot q = p;
    q.steer = nodeOf(p.steer);
    q.spin = nodeOf(p.spin);
    for (auto& m : q.glow) m = matOf(m);
    for (auto& m : q.blur) m = matOf(m);
    car->pivots[id] = q;
  }
  for (auto& m : src.tailLights) car->tailLights.push_back(matOf(m));
  if (src.steeringWheel) car->steeringWheel = LoadedCar::SteeringWheel{nodeOf(src.steeringWheel->pivot), src.steeringWheel->axis};
  car->cockpit = src.cockpit;
  car->hoodEye = src.hoodEye;
  for (auto& m : src.paintMats) car->paintMats.push_back(matOf(m));
  car->paintOrig = src.paintOrig;
  for (auto& m : src.rimMats) car->rimMats.push_back(matOf(m));
  car->rimOrig = src.rimOrig;
  car->box = src.box;
  for (auto& m : car->paintMats) m->decals = nullptr;
  return car;
}
