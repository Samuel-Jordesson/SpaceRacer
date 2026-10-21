#include "gltf_loader.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "../../third_party/cgltf.h"
#include "../../third_party/stb_image.h"

namespace {

std::string extraString(const cgltf_data* data, const cgltf_extras& ex, const char* key) {
  if (!ex.data) return {};
  // extras é um objeto JSON pequeno: procura "key": "valor"
  std::string json = ex.data;
  std::string pat = std::string("\"") + key + "\"";
  size_t p = json.find(pat);
  if (p == std::string::npos) return {};
  p = json.find(':', p + pat.size());
  if (p == std::string::npos) return {};
  p = json.find('"', p);
  if (p == std::string::npos) return {};
  std::string out;
  for (size_t i = p + 1; i < json.size() && json[i] != '"'; i++) {
    if (json[i] == '\\' && i + 1 < json.size()) i++;
    out += json[i];
  }
  (void)data;
  return out;
}

// PropertyBinding.sanitizeNodeName do three
std::string sanitize(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\n') o += '_';
    else if (c == '[' || c == ']' || c == '.' || c == ':' || c == '/') continue;
    else o += c;
  }
  return o;
}

// luminância média em blocos 16×16 (imagem RGBA8)
void thumb16(const uint8_t* px, int w, int h, float out[256], bool flip) {
  for (int by = 0; by < 16; by++)
    for (int bx = 0; bx < 16; bx++) {
      double s = 0;
      int n = 0;
      int y0 = by * h / 16, y1 = std::max(y0 + 1, (by + 1) * h / 16);
      int x0 = bx * w / 16, x1 = std::max(x0 + 1, (bx + 1) * w / 16);
      int stepY = std::max(1, (y1 - y0) / 8), stepX = std::max(1, (x1 - x0) / 8);
      for (int y = y0; y < y1; y += stepY)
        for (int x = x0; x < x1; x += stepX) {
          int yy = flip ? h - 1 - y : y;
          const uint8_t* q = px + ((size_t)yy * w + x) * 4;
          s += (q[0] * 0.3 + q[1] * 0.59 + q[2] * 0.11) * (q[3] / 255.0);
          n++;
        }
      out[by * 16 + bx] = (float)(s / std::max(1, n));
    }
}
// 0 = igual, 1 = de cabeça para baixo, 2 = outra imagem (mesmo nome, conteúdo diferente)
int compareWithReference(const std::vector<uint8_t>& px, int w, int h, const uint8_t* ref, int rw, int rh) {
  float a[256], b[256], r[256];
  thumb16(px.data(), w, h, a, false);
  thumb16(px.data(), w, h, b, true);
  thumb16(ref, rw, rh, r, false);
  double da = 0, db = 0;
  for (int i = 0; i < 256; i++) {
    da += std::fabs(a[i] - r[i]);
    db += std::fabs(b[i] - r[i]);
  }
  da /= 256;
  db /= 256;
  if (std::min(da, db) > 12) return 2;
  return db < da * 0.8 ? 1 : 0;
}
void flipRows(std::vector<uint8_t>& px, int w, int h) {
  size_t row = (size_t)w * 4;
  std::vector<uint8_t> tmp(row);
  for (int y = 0; y < h / 2; y++) {
    uint8_t* a = px.data() + y * row;
    uint8_t* b = px.data() + (size_t)(h - 1 - y) * row;
    std::copy(a, a + row, tmp.data());
    std::copy(b, b + row, a);
    std::copy(tmp.data(), tmp.data() + row, b);
  }
}

mat4 nodeLocal(const cgltf_node* n) {
  mat4 m;
  if (n->has_matrix) {
    for (int i = 0; i < 16; i++) m.e[i] = n->matrix[i];
    return m;
  }
  vec3 t = n->has_translation ? vec3(n->translation[0], n->translation[1], n->translation[2]) : vec3();
  quat r = n->has_rotation ? quat(n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]) : quat();
  vec3 s = n->has_scale ? vec3(n->scale[0], n->scale[1], n->scale[2]) : vec3(1);
  return mat4::compose(t, r, s);
}

mat4 nodeWorld(const cgltf_node* n) {
  mat4 m = nodeLocal(n);
  for (const cgltf_node* p = n->parent; p; p = p->parent) m = nodeLocal(p) * m;
  return m;
}

}  // namespace

GltfAsset loadGltf(const std::string& path, const std::function<void(float)>& onProgress, const std::string& textureDir) {
  auto progress = [&](float p) { if (onProgress) onProgress(p); };
  progress(0);

  // leitura em blocos para reportar o progresso
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) throw std::runtime_error("arquivo não encontrado: " + path);
  std::fseek(f, 0, SEEK_END);
  long total = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> bytes(total);
  long got = 0;
  while (got < total) {
    size_t n = std::fread(bytes.data() + got, 1, std::min<long>(1 << 20, total - got), f);
    if (n == 0) break;
    got += (long)n;
    progress(0.25f * got / total);
  }
  std::fclose(f);
  if (got != total) throw std::runtime_error("falha ao ler " + path);

  cgltf_options opt{};
  cgltf_data* data = nullptr;
  if (cgltf_parse(&opt, bytes.data(), bytes.size(), &data) != cgltf_result_success) throw std::runtime_error("GLB inválido: " + path);
  std::unique_ptr<cgltf_data, void (*)(cgltf_data*)> guard(data, cgltf_free);
  if (cgltf_load_buffers(&opt, data, path.c_str()) != cgltf_result_success) throw std::runtime_error("buffers do GLB inválidos");

  GltfAsset asset;
  asset.title = extraString(data, data->asset.extras, "title");
  asset.author = extraString(data, data->asset.extras, "author");
  asset.license = extraString(data, data->asset.extras, "license");
  asset.source = extraString(data, data->asset.extras, "source");

  // ---------------------------------------------------------------- imagens (em paralelo)
  size_t nImg = data->images_count;
  struct Decoded { std::vector<uint8_t> px; int w = 0, h = 0; };
  std::vector<Decoded> decoded(nImg);
  std::atomic<size_t> done{0}, next{0};
  {
    unsigned nThreads = std::max(1u, std::min<unsigned>(std::thread::hardware_concurrency(), 12));
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < nThreads; t++)
      pool.emplace_back([&] {
        for (size_t i; (i = next++) < nImg;) {
          const cgltf_image& im = data->images[i];
          // versão em alta resolução na pasta externa?
          if (!textureDir.empty() && im.name && *im.name) {
            std::string n = im.name;
            if (n.find('.') == std::string::npos) n += ".png";
            std::string file = textureDir + "/" + n;
            int w, h, c;
            if (uint8_t* px = stbi_load(file.c_str(), &w, &h, &c, 4)) {
              decoded[i].px.assign(px, px + (size_t)w * h * 4);
              decoded[i].w = w;
              decoded[i].h = h;
              stbi_image_free(px);
              // muitas texturas convertidas vêm de cabeça para baixo, e algumas têm o mesmo nome
              // mas são outra imagem: compara com a embutida antes de usar
              bool use = true;
              if (im.buffer_view) {
                const uint8_t* p = (const uint8_t*)im.buffer_view->buffer->data + im.buffer_view->offset;
                int ew, eh, ec;
                if (uint8_t* epx = stbi_load_from_memory(p, (int)im.buffer_view->size, &ew, &eh, &ec, 4)) {
                  int r = compareWithReference(decoded[i].px, w, h, epx, ew, eh);
                  if (r == 1) flipRows(decoded[i].px, w, h);
                  if (r == 2) {
                    std::fprintf(stderr, "[gltf] %s externa não corresponde à embutida: usando a embutida\n", n.c_str());
                    use = false;
                    decoded[i].px.assign(epx, epx + (size_t)ew * eh * 4);
                    decoded[i].w = ew;
                    decoded[i].h = eh;
                  }
                  stbi_image_free(epx);
                }
              }
              (void)use;
              done++;
              continue;
            }
          }
          if (im.buffer_view) {
            const uint8_t* p = (const uint8_t*)im.buffer_view->buffer->data + im.buffer_view->offset;
            int w, h, c;
            uint8_t* px = stbi_load_from_memory(p, (int)im.buffer_view->size, &w, &h, &c, 4);
            if (px) {
              decoded[i].px.assign(px, px + (size_t)w * h * 4);
              decoded[i].w = w;
              decoded[i].h = h;
              stbi_image_free(px);
            }
          }
          done++;
        }
      });
    while (done < nImg) {
      progress(0.25f + 0.65f * done / std::max<size_t>(1, nImg));
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    for (auto& t : pool) t.join();
  }

  // texturas: uma por (imagem, espaço de cor)
  std::map<std::pair<const cgltf_image*, bool>, TexturePtr> texCache;
  auto texFor = [&](const cgltf_texture_view& tv, bool srgb) -> TexRef {
    TexRef r;
    if (!tv.texture || !tv.texture->image) return r;
    const cgltf_image* im = tv.texture->image;
    auto key = std::make_pair(im, srgb);
    auto it = texCache.find(key);
    if (it == texCache.end()) {
      const Decoded& d = decoded[im - data->images];
      if (d.px.empty()) return r;
      auto t = std::make_shared<Texture>();
      t->width = d.w;
      t->height = d.h;
      t->pixels = d.px;
      t->srgb = srgb;
      const cgltf_sampler* s = tv.texture->sampler;
      t->repeat = !s || s->wrap_s == 10497 || s->wrap_s == 0;
      it = texCache.emplace(key, t).first;
    }
    r.tex = it->second;
    r.uv = tv.texcoord;
    return r;
  };

  // ---------------------------------------------------------------- materiais
  std::unordered_map<const cgltf_material*, MaterialPtr> mats;
  auto matFor = [&](const cgltf_material* m) -> MaterialPtr {
    if (!m) {
      static MaterialPtr def = [] {
        auto d = std::make_shared<Material>();
        d->color = vec3(1);
        d->metalness = 1;
        d->roughness = 1;
        return d;
      }();
      return def;
    }
    auto it = mats.find(m);
    if (it != mats.end()) return it->second;
    auto mt = std::make_shared<Material>();
    mt->gltf = true;
    mt->name = m->name ? m->name : "";
    if (m->unlit) mt->kind = Material::Kind::Basic;
    const auto& pbr = m->pbr_metallic_roughness;
    mt->color = vec3(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
    mt->opacity = pbr.base_color_factor[3];
    mt->map = texFor(pbr.base_color_texture, true);
    mt->metalness = pbr.metallic_factor;
    mt->roughness = pbr.roughness_factor;
    mt->metalnessMap = mt->roughnessMap = texFor(pbr.metallic_roughness_texture, false);
    mt->doubleSided = m->double_sided;
    if (m->alpha_mode == cgltf_alpha_mode_blend) {
      mt->transparent = true;
      mt->depthWrite = false;
    } else if (m->alpha_mode == cgltf_alpha_mode_mask) {
      mt->alphaTest = m->alpha_cutoff;
    }
    if (m->normal_texture.texture) {
      mt->normalMap = texFor(m->normal_texture, false);
      float s = m->normal_texture.scale;
      mt->normalScale = vec2(s, s);
    }
    if (m->occlusion_texture.texture) {
      mt->aoMap = texFor(m->occlusion_texture, false);
      mt->aoMapIntensity = m->occlusion_texture.scale;
    }
    mt->emissive = vec3(m->emissive_factor[0], m->emissive_factor[1], m->emissive_factor[2]);
    mt->emissiveMap = texFor(m->emissive_texture, true);
    if (m->has_emissive_strength) mt->emissiveIntensity = m->emissive_strength.emissive_strength;
    // extensões que fazem o GLTFLoader criar MeshPhysicalMaterial
    if (m->has_clearcoat || m->has_sheen || m->has_transmission || m->has_volume || m->has_ior || m->has_specular ||
        m->has_iridescence || m->has_anisotropy || m->has_dispersion)
      mt->physical = true;
    if (m->has_clearcoat) {
      const auto& c = m->clearcoat;
      mt->clearcoat = c.clearcoat_factor;
      mt->clearcoatRoughness = c.clearcoat_roughness_factor;
      mt->clearcoatMap = texFor(c.clearcoat_texture, false);
      mt->clearcoatRoughnessMap = texFor(c.clearcoat_roughness_texture, false);
      if (c.clearcoat_normal_texture.texture) {
        mt->clearcoatNormalMap = texFor(c.clearcoat_normal_texture, false);
        float s = c.clearcoat_normal_texture.scale;
        mt->clearcoatNormalScale = vec2(s, s);
      }
    }
    if (m->has_specular) {
      const auto& s = m->specular;
      mt->specularIntensity = s.specular_factor;
      mt->specularColor = vec3(s.specular_color_factor[0], s.specular_color_factor[1], s.specular_color_factor[2]);
      mt->specularIntensityMap = texFor(s.specular_texture, false);
      mt->specularColorMap = texFor(s.specular_color_texture, true);
    }
    if (m->has_ior) mt->ior = m->ior.ior;
    mats[m] = mt;
    return mt;
  };

  // ---------------------------------------------------------------- geometria de uma primitiva
  auto buildGeometry = [&](const cgltf_node* node, const cgltf_primitive& prim) -> GeometryPtr {
    auto g = std::make_shared<Geometry>();
    const cgltf_accessor *pa = nullptr, *na = nullptr, *ta = nullptr, *u0 = nullptr, *u1 = nullptr, *ja = nullptr, *wa = nullptr;
    for (size_t i = 0; i < prim.attributes_count; i++) {
      const auto& at = prim.attributes[i];
      switch (at.type) {
        case cgltf_attribute_type_position: pa = at.data; break;
        case cgltf_attribute_type_normal: na = at.data; break;
        case cgltf_attribute_type_tangent: ta = at.data; break;
        case cgltf_attribute_type_texcoord:
          if (at.index == 0) u0 = at.data;
          else if (at.index == 1) u1 = at.data;
          break;
        case cgltf_attribute_type_joints: if (at.index == 0) ja = at.data; break;
        case cgltf_attribute_type_weights: if (at.index == 0) wa = at.data; break;
        default: break;
      }
    }
    if (!pa) return nullptr;
    size_t n = pa->count;
    g->pos.resize(n);
    for (size_t i = 0; i < n; i++) cgltf_accessor_read_float(pa, i, &g->pos[i].x, 3);
    if (na) {
      g->nrm.resize(n);
      for (size_t i = 0; i < n; i++) cgltf_accessor_read_float(na, i, &g->nrm[i].x, 3);
    }
    if (ta) {
      g->tan.resize(n);
      for (size_t i = 0; i < n; i++) cgltf_accessor_read_float(ta, i, &g->tan[i].x, 4);
      g->hasTangents = true;
    }
    if (u0) {
      g->uv0.resize(n);
      for (size_t i = 0; i < n; i++) cgltf_accessor_read_float(u0, i, &g->uv0[i].x, 2);
    }
    if (u1) {
      g->uv1.resize(n);
      for (size_t i = 0; i < n; i++) cgltf_accessor_read_float(u1, i, &g->uv1[i].x, 2);
    }
    if (prim.indices) {
      g->idx.resize(prim.indices->count);
      for (size_t i = 0; i < prim.indices->count; i++) g->idx[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
    }
    // malha com esqueleto: assa a pose (mundo = Σ w·Junta·IBM·v) no espaço local do nó
    if (node->skin && ja && wa) {
      const cgltf_skin* sk = node->skin;
      std::vector<mat4> jm(sk->joints_count);
      for (size_t j = 0; j < sk->joints_count; j++) {
        mat4 ibm;
        if (sk->inverse_bind_matrices) cgltf_accessor_read_float(sk->inverse_bind_matrices, j, ibm.e, 16);
        jm[j] = nodeWorld(sk->joints[j]) * ibm;
      }
      mat4 invNode = nodeWorld(node).inverse();
      for (size_t i = 0; i < n; i++) {
        cgltf_uint jo[4] = {0, 0, 0, 0};
        float w[4] = {0, 0, 0, 0};
        cgltf_accessor_read_uint(ja, i, jo, 4);
        cgltf_accessor_read_float(wa, i, w, 4);
        mat4 acc;
        for (float& v : acc.e) v = 0;
        for (int k = 0; k < 4; k++)
          if (w[k] != 0 && jo[k] < jm.size())
            for (int e = 0; e < 16; e++) acc.e[e] += jm[jo[k]].e[e] * w[k];
        mat4 full = invNode * acc;
        g->pos[i] = full.point(g->pos[i]);
        if (!g->nrm.empty()) g->nrm[i] = normalize(normalMatrix(full).dir(g->nrm[i]));
        if (!g->tan.empty()) {
          vec3 t = normalize(full.dir(g->tan[i].xyz()));
          g->tan[i] = vec4(t, g->tan[i].w);
        }
      }
    }
    g->computeBounds();
    return g;
  };

  // ---------------------------------------------------------------- nós
  std::set<std::string> usedNames;
  auto uniqueName = [&](const std::string& s) {
    std::string base = sanitize(s), name = base;
    for (int i = 1; usedNames.count(name); i++) name = base + "_" + std::to_string(i);
    usedNames.insert(name);
    return name;
  };

  std::function<NodePtr(const cgltf_node*)> buildNode = [&](const cgltf_node* cn) -> NodePtr {
    NodePtr node;
    std::string nodeName = cn->name ? uniqueName(cn->name) : "";
    if (cn->mesh) {
      const cgltf_mesh& mesh = *cn->mesh;
      std::vector<NodePtr> prims;
      for (size_t p = 0; p < mesh.primitives_count; p++) {
        const auto& prim = mesh.primitives[p];
        if (prim.type != cgltf_primitive_type_triangles) continue;
        auto geo = buildGeometry(cn, prim);
        if (!geo) continue;
        auto m = makeNode(mesh.name ? mesh.name : "");
        m->geometry = geo;
        m->material = matFor(prim.material);
        prims.push_back(m);
      }
      if (prims.size() == 1) node = prims[0];
      else {
        node = makeNode();
        for (auto& m : prims) node->add(m);
      }
    } else {
      node = makeNode();
    }
    node->name = nodeName;
    // (malhas com esqueleto já têm a pose assada no espaço deste nó)
    nodeLocal(cn).decompose(node->position, node->quaternion, node->scale);
    for (size_t i = 0; i < cn->children_count; i++) node->add(buildNode(cn->children[i]));
    return node;
  };

  asset.scene = makeNode("Scene");
  const cgltf_scene* sc = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
  if (sc)
    for (size_t i = 0; i < sc->nodes_count; i++) asset.scene->add(buildNode(sc->nodes[i]));
  asset.scene->updateMatrixWorld();
  progress(1);
  return asset;
}
