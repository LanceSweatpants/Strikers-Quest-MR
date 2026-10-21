// See xr_ctrl_model.h.

#if defined(__ANDROID__) && defined(STRIKERS_XR)

#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "xr_ctrl_model.h"

#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <functional>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace xrctrl {
namespace {

void clog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void clog(const char* fmt, ...) {
  char line[1600];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  __android_log_print(ANDROID_LOG_INFO, "strikers_xr", "%s", line);
  std::fprintf(stderr, "[xr-ctrl] %s\n", line);
}

// ---------------------------------------------------------------------------------------------
// A small JSON reader: enough for a glTF document.
// ---------------------------------------------------------------------------------------------

struct Json {
  enum Type { kNull, kBool, kNumber, kString, kArray, kObject } type = kNull;
  bool b = false;
  double n = 0.0;
  std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;

  const Json* get(const char* key) const {
    if (type != kObject) return nullptr;
    for (const auto& kv : o)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }
  const Json* at(size_t i) const { return type == kArray && i < a.size() ? &a[i] : nullptr; }
  double num(const char* key, double def) const {
    const Json* v = get(key);
    return v != nullptr && v->type == kNumber ? v->n : def;
  }
  std::string str(const char* key) const {
    const Json* v = get(key);
    return v != nullptr && v->type == kString ? v->s : std::string();
  }
};

struct Parser {
  const char* p;
  const char* end;
  bool ok = true;

  void ws() {
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
  }
  bool lit(const char* w) {
    const size_t n = std::strlen(w);
    if (static_cast<size_t>(end - p) >= n && std::memcmp(p, w, n) == 0) {
      p += n;
      return true;
    }
    return false;
  }
  std::string string() {
    std::string out;
    ++p;  // the quote
    while (p < end && *p != '"') {
      if (*p == '\\' && p + 1 < end) {
        ++p;
        switch (*p) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u':
          // Names in these files are ASCII; anything else becomes '?'.
          out += '?';
          p += std::min<ptrdiff_t>(4, end - p - 1);
          break;
        default: out += *p; break;
        }
        ++p;
      } else {
        out += *p++;
      }
    }
    if (p < end) ++p;  // the closing quote
    else ok = false;
    return out;
  }
  Json value(int depth) {
    Json v;
    ws();
    if (p >= end || depth > 64) {
      ok = false;
      return v;
    }
    if (*p == '{') {
      v.type = Json::kObject;
      ++p;
      ws();
      if (p < end && *p == '}') {
        ++p;
        return v;
      }
      while (ok && p < end) {
        ws();
        if (p >= end || *p != '"') {
          ok = false;
          break;
        }
        std::string key = string();
        ws();
        if (p >= end || *p != ':') {
          ok = false;
          break;
        }
        ++p;
        v.o.emplace_back(std::move(key), value(depth + 1));
        ws();
        if (p < end && *p == ',') {
          ++p;
          continue;
        }
        if (p < end && *p == '}') {
          ++p;
          break;
        }
        ok = false;
      }
    } else if (*p == '[') {
      v.type = Json::kArray;
      ++p;
      ws();
      if (p < end && *p == ']') {
        ++p;
        return v;
      }
      while (ok && p < end) {
        v.a.push_back(value(depth + 1));
        ws();
        if (p < end && *p == ',') {
          ++p;
          continue;
        }
        if (p < end && *p == ']') {
          ++p;
          break;
        }
        ok = false;
      }
    } else if (*p == '"') {
      v.type = Json::kString;
      v.s = string();
    } else if (lit("true")) {
      v.type = Json::kBool;
      v.b = true;
    } else if (lit("false")) {
      v.type = Json::kBool;
    } else if (lit("null")) {
      v.type = Json::kNull;
    } else {
      char* stop = nullptr;
      std::string num;
      while (p < end && (std::strchr("+-0123456789.eE", *p) != nullptr)) num += *p++;
      v.type = Json::kNumber;
      v.n = std::strtod(num.c_str(), &stop);
      if (num.empty()) ok = false;
    }
    return v;
  }
};

// ---------------------------------------------------------------------------------------------
// glTF -> grip-space triangles, split into parts.
// ---------------------------------------------------------------------------------------------

struct Mat4 {
  float m[16];  // column-major, as glTF stores it
};
Mat4 identity() {
  Mat4 r{};
  r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
  return r;
}
Mat4 mul(const Mat4& a, const Mat4& b) {
  Mat4 r{};
  for (int c = 0; c < 4; ++c)
    for (int rr = 0; rr < 4; ++rr) {
      float s = 0.0f;
      for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
      r.m[c * 4 + rr] = s;
    }
  return r;
}
void xform_point(const Mat4& m, const float in[3], float out[3]) {
  for (int r = 0; r < 3; ++r) out[r] = m.m[r] * in[0] + m.m[4 + r] * in[1] + m.m[8 + r] * in[2] + m.m[12 + r];
}
void xform_dir(const Mat4& m, const float in[3], float out[3]) {
  for (int r = 0; r < 3; ++r) out[r] = m.m[r] * in[0] + m.m[4 + r] * in[1] + m.m[8 + r] * in[2];
}
Mat4 node_local(const Json& node) {
  Mat4 r = identity();
  if (const Json* mm = node.get("matrix")) {
    if (mm->type == Json::kArray && mm->a.size() == 16)
      for (int i = 0; i < 16; ++i) r.m[i] = static_cast<float>(mm->a[i].n);
    return r;
  }
  float t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
  if (const Json* v = node.get("translation"))
    for (int i = 0; i < 3 && i < static_cast<int>(v->a.size()); ++i) t[i] = static_cast<float>(v->a[i].n);
  if (const Json* v = node.get("rotation"))
    for (int i = 0; i < 4 && i < static_cast<int>(v->a.size()); ++i) q[i] = static_cast<float>(v->a[i].n);
  if (const Json* v = node.get("scale"))
    for (int i = 0; i < 3 && i < static_cast<int>(v->a.size()); ++i) s[i] = static_cast<float>(v->a[i].n);
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  const float rm[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
                       2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                       2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
  // Columns: R's columns times the scale, then the translation.
  for (int c = 0; c < 3; ++c)
    for (int rr = 0; rr < 3; ++rr) r.m[c * 4 + rr] = rm[c * 3 + rr] * s[c];
  r.m[12] = t[0];
  r.m[13] = t[1];
  r.m[14] = t[2];
  return r;
}

struct Gltf {
  Json doc;
  const uint8_t* bin = nullptr;
  size_t binSize = 0;

  // Reads accessor `index` as floats (normalized integers scaled to 0..1 / -1..1), `comps` per item.
  bool read(int index, std::vector<float>& out, int* compsOut) const {
    const Json* acc = doc.get("accessors") ? doc.get("accessors")->at(static_cast<size_t>(index)) : nullptr;
    if (acc == nullptr) return false;
    const int count = static_cast<int>(acc->num("count", 0));
    const int ctype = static_cast<int>(acc->num("componentType", 0));
    const std::string type = acc->str("type");
    const bool normalized = acc->get("normalized") != nullptr && acc->get("normalized")->b;
    const int comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : 0;
    const int csize = (ctype == 5126 || ctype == 5125) ? 4 : (ctype == 5123 || ctype == 5122) ? 2 : (ctype == 5121 || ctype == 5120) ? 1 : 0;
    if (comps == 0 || csize == 0 || count <= 0) return false;
    out.assign(static_cast<size_t>(count) * comps, 0.0f);
    *compsOut = comps;
    const Json* bvIndex = acc->get("bufferView");
    if (bvIndex == nullptr) return true;  // all zeros (sparse not handled)
    const Json* bv = doc.get("bufferViews") ? doc.get("bufferViews")->at(static_cast<size_t>(bvIndex->n)) : nullptr;
    if (bv == nullptr || bv->get("extensions") != nullptr) return false;  // e.g. meshopt-compressed
    const size_t offset = static_cast<size_t>(bv->num("byteOffset", 0) + acc->num("byteOffset", 0));
    const size_t stride = bv->get("byteStride") != nullptr ? static_cast<size_t>(bv->num("byteStride", 0))
                                                          : static_cast<size_t>(comps * csize);
    if (offset + stride * (count - 1) + comps * csize > binSize) return false;
    for (int i = 0; i < count; ++i) {
      const uint8_t* e = bin + offset + stride * i;
      for (int c = 0; c < comps; ++c) {
        const uint8_t* q = e + c * csize;
        float v = 0.0f;
        switch (ctype) {
        case 5126: std::memcpy(&v, q, 4); break;
        case 5125: { uint32_t u; std::memcpy(&u, q, 4); v = static_cast<float>(u); break; }
        case 5123: { uint16_t u; std::memcpy(&u, q, 2); v = normalized ? u / 65535.0f : static_cast<float>(u); break; }
        case 5122: { int16_t u; std::memcpy(&u, q, 2); v = normalized ? std::max(u / 32767.0f, -1.0f) : static_cast<float>(u); break; }
        case 5121: v = normalized ? q[0] / 255.0f : static_cast<float>(q[0]); break;
        case 5120: { const int8_t u = static_cast<int8_t>(q[0]); v = normalized ? std::max(u / 127.0f, -1.0f) : static_cast<float>(u); break; }
        default: break;
        }
        out[static_cast<size_t>(i) * comps + c] = v;
      }
    }
    return true;
  }
};

// r54b: a moving part only by a "b_" joint's name (Meta's models: b_button_a / b_button_x,
// b_button_b / b_button_y, b_button_oculus, b_trigger_front, b_trigger_grip, b_thumbstick). r54
// matched any name and filed the whole body under the menu button ("..._oculus_controller_mesh"), so
// it took the button colour (white for Mario: Lance, "bland as fuck").
PartKind kind_of(std::string name) {
  for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (name.compare(0, 2, "b_") != 0) return kBody;
  auto has = [&](const char* w) { return name.find(w) != std::string::npos; };
  if (has("thumbstick") || has("joystick")) return kStick;
  if (has("trigger_grip") || has("squeeze") || has("grip")) return kGrip;
  if (has("trigger")) return kTrigger;
  if (has("oculus") || has("menu") || has("system") || has("home") || has("meta")) return kMenu;
  if (has("button_a") || has("button_x")) return kButton1;
  if (has("button_b") || has("button_y")) return kButton2;
  return kBody;
}

const char* kKindNames[kPartKinds] = {"body", "stick", "button1", "button2", "trigger", "grip", "menu"};

// Quaternion helpers (x, y, z, w).
void q_mul(const float a[4], const float b[4], float out[4]) {
  const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
  out[0] = x;
  out[1] = y;
  out[2] = z;
  out[3] = w;
}
void q_conj(const float a[4], float out[4]) {
  out[0] = -a[0];
  out[1] = -a[1];
  out[2] = -a[2];
  out[3] = a[3];
}
// A rotation matrix's quaternion (m column-major 4x4, its upper 3x3 assumed a pure rotation after
// normalising the columns).
void q_from_mat(const Mat4& m, float q[4]) {
  float c[3][3];
  for (int col = 0; col < 3; ++col) {
    float l = std::sqrt(m.m[col * 4] * m.m[col * 4] + m.m[col * 4 + 1] * m.m[col * 4 + 1] + m.m[col * 4 + 2] * m.m[col * 4 + 2]);
    if (l < 1e-12f) l = 1.0f;
    for (int r = 0; r < 3; ++r) c[col][r] = m.m[col * 4 + r] / l;
  }
  // r[row][col] = c[col][row]
  const float m00 = c[0][0], m11 = c[1][1], m22 = c[2][2];
  const float m01 = c[1][0], m10 = c[0][1], m02 = c[2][0], m20 = c[0][2], m12 = c[2][1], m21 = c[1][2];
  const float tr = m00 + m11 + m22;
  if (tr > 0.0f) {
    const float s = 2.0f * std::sqrt(tr + 1.0f);
    q[3] = 0.25f * s;
    q[0] = (m21 - m12) / s;
    q[1] = (m02 - m20) / s;
    q[2] = (m10 - m01) / s;
  } else if (m00 > m11 && m00 > m22) {
    const float s = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);
    q[3] = (m21 - m12) / s;
    q[0] = 0.25f * s;
    q[1] = (m01 + m10) / s;
    q[2] = (m02 + m20) / s;
  } else if (m11 > m22) {
    const float s = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);
    q[3] = (m02 - m20) / s;
    q[0] = (m01 + m10) / s;
    q[1] = 0.25f * s;
    q[2] = (m12 + m21) / s;
  } else {
    const float s = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);
    q[3] = (m10 - m01) / s;
    q[0] = (m02 + m20) / s;
    q[1] = (m12 + m21) / s;
    q[2] = 0.25f * s;
  }
}

bool build_model(const std::vector<uint8_t>& glb, Model* out) {
  if (glb.size() < 20) return false;
  uint32_t magic, version, total;
  std::memcpy(&magic, glb.data(), 4);
  std::memcpy(&version, glb.data() + 4, 4);
  std::memcpy(&total, glb.data() + 8, 4);
  if (magic != 0x46546C67u || version != 2) {
    clog("model: not a binary glTF 2 (magic %08x version %u)", magic, version);
    return false;
  }
  Gltf g;
  size_t at = 12;
  bool haveJson = false;
  while (at + 8 <= glb.size()) {
    uint32_t len, type;
    std::memcpy(&len, glb.data() + at, 4);
    std::memcpy(&type, glb.data() + at + 4, 4);
    const uint8_t* data = glb.data() + at + 8;
    if (at + 8 + len > glb.size()) break;
    if (type == 0x4E4F534Au) {  // JSON
      Parser p{reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + len};
      g.doc = p.value(0);
      haveJson = p.ok && g.doc.type == Json::kObject;
    } else if (type == 0x004E4942u) {  // BIN
      g.bin = data;
      g.binSize = len;
    }
    at += 8 + ((len + 3u) & ~3u);
  }
  if (!haveJson) {
    clog("model: its JSON could not be read");
    return false;
  }
  const Json* nodes = g.doc.get("nodes");
  const Json* meshes = g.doc.get("meshes");
  if (nodes == nullptr || meshes == nullptr) {
    clog("model: no nodes or meshes");
    return false;
  }
  const size_t nodeCount = nodes->a.size();
  // Each node's world matrix (the scene root = the grip pose) and its parent.
  std::vector<Mat4> world(nodeCount, identity());
  std::vector<int> parent(nodeCount, -1);
  for (size_t i = 0; i < nodeCount; ++i)
    if (const Json* ch = nodes->a[i].get("children"))
      for (const Json& c : ch->a)
        if (c.n >= 0 && c.n < nodeCount) parent[static_cast<size_t>(c.n)] = static_cast<int>(i);
  std::vector<int> done(nodeCount, 0);
  std::function<void(size_t)> resolve = [&](size_t i) {
    if (done[i]) return;
    done[i] = 1;  // also guards a cycle
    const Mat4 local = node_local(nodes->a[i]);
    if (parent[i] >= 0) {
      resolve(static_cast<size_t>(parent[i]));
      world[i] = mul(world[static_cast<size_t>(parent[i])], local);
    } else {
      world[i] = local;
    }
  };
  for (size_t i = 0; i < nodeCount; ++i) resolve(i);
  auto node_name = [&](size_t i) { return nodes->a[i].str("name"); };
  // A node's kind: its own "b_" name, else (an unskinned mesh under a button) its nearest "b_" ancestor's.
  auto node_kind = [&](size_t i) {
    for (int n = static_cast<int>(i); n >= 0; n = parent[static_cast<size_t>(n)]) {
      const PartKind k = kind_of(node_name(static_cast<size_t>(n)));
      if (k != kBody) return k;
    }
    return kBody;
  };

  // Triangles per part kind; a skinned vertex goes by its strongest joint.
  struct Bucket {
    std::vector<uint32_t> idx;
    int pivotNode = -1;
  };
  Bucket buckets[kPartKinds];
  for (size_t ni = 0; ni < nodeCount; ++ni) {
    const Json& node = nodes->a[ni];
    const Json* meshIndex = node.get("mesh");
    if (meshIndex == nullptr) continue;
    const Json* mesh = meshes->at(static_cast<size_t>(meshIndex->n));
    const Json* prims = mesh != nullptr ? mesh->get("primitives") : nullptr;
    if (prims == nullptr) continue;
    std::string lname = node_name(ni) + " " + (mesh != nullptr ? mesh->str("name") : std::string());
    for (char& c : lname) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lname.find("battery") != std::string::npos || lname.find("laser") != std::string::npos) continue;  // UI bits
    // Skinned: each joint's matrix in the rest pose (joint world x inverse bind), so the vertices land
    // where the model's skeleton puts them (r54 left them in mesh space: upside down, off the hand).
    std::vector<int> jointNodes;
    std::vector<Mat4> jointMats;
    if (const Json* skinIndex = node.get("skin")) {
      const Json* skin = g.doc.get("skins") ? g.doc.get("skins")->at(static_cast<size_t>(skinIndex->n)) : nullptr;
      if (skin != nullptr && skin->get("joints")) {
        for (const Json& j : skin->get("joints")->a) jointNodes.push_back(static_cast<int>(j.n));
        std::vector<float> ibm;
        int ic = 0;
        const bool haveIbm = skin->get("inverseBindMatrices") &&
                             g.read(static_cast<int>(skin->get("inverseBindMatrices")->n), ibm, &ic) && ic == 16;
        for (size_t j = 0; j < jointNodes.size(); ++j) {
          Mat4 inv = identity();
          if (haveIbm && (j + 1) * 16 <= ibm.size())
            for (int k = 0; k < 16; ++k) inv.m[k] = ibm[j * 16 + k];
          const int jn = jointNodes[j];
          jointMats.push_back(jn >= 0 && static_cast<size_t>(jn) < nodeCount ? mul(world[static_cast<size_t>(jn)], inv) : identity());
        }
      }
    }
    const bool skinned = !jointNodes.empty();
    for (const Json& prim : prims->a) {
      if (prim.num("mode", 4) != 4) continue;  // triangles only
      const Json* attrs = prim.get("attributes");
      if (attrs == nullptr || attrs->get("POSITION") == nullptr) continue;
      std::vector<float> pos, nrm, joints, weights, ind;
      int pc = 0, nc = 0, jc = 0, wc = 0, ic = 0;
      if (!g.read(static_cast<int>(attrs->get("POSITION")->n), pos, &pc) || pc != 3) {
        clog("model: positions of a primitive unreadable (compressed?)");
        continue;
      }
      const bool hasNormals = attrs->get("NORMAL") && g.read(static_cast<int>(attrs->get("NORMAL")->n), nrm, &nc) && nc == 3;
      const bool hasJoints = skinned && attrs->get("JOINTS_0") && attrs->get("WEIGHTS_0") &&
                             g.read(static_cast<int>(attrs->get("JOINTS_0")->n), joints, &jc) &&
                             g.read(static_cast<int>(attrs->get("WEIGHTS_0")->n), weights, &wc) && jc == 4 && wc == 4;
      const size_t vcount = pos.size() / 3;
      std::vector<uint32_t> tri;
      if (prim.get("indices") && g.read(static_cast<int>(prim.get("indices")->n), ind, &ic) && ic == 1) {
        for (float f : ind) tri.push_back(static_cast<uint32_t>(f));
      } else {
        for (uint32_t i = 0; i < vcount; ++i) tri.push_back(i);
      }
      const uint32_t base = static_cast<uint32_t>(out->verts.size());
      std::vector<PartKind> vkind(vcount, node_kind(ni));
      for (size_t v = 0; v < vcount; ++v) {
        float p[3] = {0.0f, 0.0f, 0.0f}, n[3] = {0.0f, 0.0f, 0.0f};
        if (hasJoints) {
          // Linear blend skinning at rest, and the strongest joint names the part.
          int best = 0;
          float wsum = 0.0f;
          for (int k = 0; k < 4; ++k) {
            const float w = weights[v * 4 + k];
            const int j = static_cast<int>(joints[v * 4 + k]);
            if (w > weights[v * 4 + best]) best = k;
            if (w <= 0.0f || j < 0 || j >= static_cast<int>(jointMats.size())) continue;
            float tp[3], tn[3];
            xform_point(jointMats[static_cast<size_t>(j)], &pos[v * 3], tp);
            const float in[3] = {hasNormals ? nrm[v * 3] : 0.0f, hasNormals ? nrm[v * 3 + 1] : 0.0f, hasNormals ? nrm[v * 3 + 2] : 0.0f};
            xform_dir(jointMats[static_cast<size_t>(j)], in, tn);
            for (int d = 0; d < 3; ++d) {
              p[d] += w * tp[d];
              n[d] += w * tn[d];
            }
            wsum += w;
          }
          if (wsum > 1e-6f)
            for (int d = 0; d < 3; ++d) p[d] /= wsum;
          else
            xform_point(world[ni], &pos[v * 3], p);
          const int j = static_cast<int>(joints[v * 4 + best]);
          if (j >= 0 && j < static_cast<int>(jointNodes.size()) && jointNodes[static_cast<size_t>(j)] >= 0 &&
              static_cast<size_t>(jointNodes[static_cast<size_t>(j)]) < nodeCount) {
            const int jn = jointNodes[static_cast<size_t>(j)];
            vkind[v] = kind_of(node_name(static_cast<size_t>(jn)));
            Bucket& bk = buckets[vkind[v]];
            if (vkind[v] != kBody && bk.pivotNode < 0) bk.pivotNode = jn;
          }
        } else {
          xform_point(world[ni], &pos[v * 3], p);
          if (hasNormals) xform_dir(world[ni], &nrm[v * 3], n);
        }
        if (hasNormals) {
          const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
          if (l > 1e-8f)
            for (float& c : n) c /= l;
        }
        xrfx::mesh::Vertex vx{};
        std::memcpy(vx.pos, p, sizeof(p));
        std::memcpy(vx.nrm, n, sizeof(n));
        vx.mat = vkind[v] == kBody ? 0.0f : 1.0f;
        vx.aux = static_cast<float>(vkind[v]);
        out->verts.push_back(vx);
      }
      if (!hasNormals) {
        // Normals from the faces, summed per vertex.
        for (size_t t = 0; t + 2 < tri.size(); t += 3) {
          if (tri[t] >= vcount || tri[t + 1] >= vcount || tri[t + 2] >= vcount) continue;
          float* a = out->verts[base + tri[t]].pos;
          float* b = out->verts[base + tri[t + 1]].pos;
          float* c = out->verts[base + tri[t + 2]].pos;
          const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
          const float fn[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
          for (int k = 0; k < 3; ++k)
            for (int d = 0; d < 3; ++d) out->verts[base + tri[t + k]].nrm[d] += fn[d];
        }
        for (uint32_t v = base; v < out->verts.size(); ++v) {
          float* nn = out->verts[v].nrm;
          const float l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
          if (l > 1e-12f)
            for (int d = 0; d < 3; ++d) nn[d] /= l;
        }
      }
      for (size_t t = 0; t + 2 < tri.size(); t += 3) {
        if (tri[t] >= vcount || tri[t + 1] >= vcount || tri[t + 2] >= vcount) continue;
        // A triangle goes with its first vertex's part.
        Bucket& bk = buckets[vkind[tri[t]]];
        if (!skinned && bk.pivotNode < 0) bk.pivotNode = static_cast<int>(ni);
        for (int k = 0; k < 3; ++k) bk.idx.push_back(base + tri[t + k]);
      }
    }
  }
  if (out->verts.empty()) {
    clog("model: no triangles");
    return false;
  }

  // Each moving joint's press, from the model's animation: the key farthest from its first (rest).
  std::map<int, std::pair<std::vector<float>, std::vector<float>>> rotKeys, transKeys;  // node -> (rest, pressed)
  if (const Json* anims = g.doc.get("animations")) {
    for (const Json& anim : anims->a) {
      const Json* channels = anim.get("channels");
      const Json* samplers = anim.get("samplers");
      if (channels == nullptr || samplers == nullptr) continue;
      for (const Json& ch : channels->a) {
        const Json* target = ch.get("target");
        if (target == nullptr || target->get("node") == nullptr) continue;
        const int nodeIdx = static_cast<int>(target->get("node")->n);
        if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= nodeCount || kind_of(node_name(static_cast<size_t>(nodeIdx))) == kBody) continue;
        const std::string path = target->str("path");
        const Json* sampler = samplers->at(static_cast<size_t>(ch.num("sampler", -1)));
        if (sampler == nullptr || (path != "rotation" && path != "translation")) continue;
        std::vector<float> vals;
        int comps = 0;
        if (!g.read(static_cast<int>(sampler->num("output", -1)), vals, &comps) || (comps != 4 && comps != 3)) continue;
        const size_t keys = vals.size() / comps;
        if (keys < 2) continue;
        size_t far = 0;
        float farD = 0.0f;
        for (size_t k = 1; k < keys; ++k) {
          float d = 0.0f;
          if (comps == 4) {
            float dot = 0.0f;
            for (int c = 0; c < 4; ++c) dot += vals[c] * vals[k * 4 + c];
            d = 2.0f * std::acos(std::min(1.0f, std::fabs(dot)));
          } else {
            for (int c = 0; c < 3; ++c) d += (vals[k * 3 + c] - vals[c]) * (vals[k * 3 + c] - vals[c]);
            d = std::sqrt(d);
          }
          if (d > farD) {
            farD = d;
            far = k;
          }
        }
        std::vector<float> rest(vals.begin(), vals.begin() + comps), pressed(vals.begin() + far * comps, vals.begin() + (far + 1) * comps);
        (comps == 4 ? rotKeys : transKeys)[nodeIdx] = {rest, pressed};
      }
    }
  }

  for (int k = 0; k < kPartKinds; ++k) {
    Bucket& bk = buckets[k];
    if (bk.idx.empty()) continue;
    Part part;
    part.kind = static_cast<PartKind>(k);
    part.firstIndex = static_cast<uint32_t>(out->indices.size());
    part.indexCount = static_cast<uint32_t>(bk.idx.size());
    out->indices.insert(out->indices.end(), bk.idx.begin(), bk.idx.end());
    double c[3] = {0, 0, 0};
    for (uint32_t i : bk.idx)
      for (int d = 0; d < 3; ++d) c[d] += out->verts[i].pos[d];
    for (int d = 0; d < 3; ++d) part.centre[d] = static_cast<float>(c[d] / bk.idx.size());
    if (bk.pivotNode >= 0) {
      const Mat4& w = world[static_cast<size_t>(bk.pivotNode)];
      part.pivot[0] = w.m[12];
      part.pivot[1] = w.m[13];
      part.pivot[2] = w.m[14];
      // The press: the joint's local change, in its parent's frame, carried into grip space.
      const int jn = bk.pivotNode;
      const Mat4& pw = parent[static_cast<size_t>(jn)] >= 0 ? world[static_cast<size_t>(parent[static_cast<size_t>(jn)])] : identity();
      float pq[4];
      q_from_mat(pw, pq);
      float pqi[4];
      q_conj(pq, pqi);
      auto rk = rotKeys.find(jn);
      if (rk != rotKeys.end()) {
        // D = pressed * rest^-1 (parent frame) -> grip space: P D P^-1.
        float restInv[4], d[4], t[4];
        q_conj(rk->second.first.data(), restInv);
        q_mul(rk->second.second.data(), restInv, d);
        q_mul(pq, d, t);
        q_mul(t, pqi, part.pressTurn);
        part.hasPress = true;
      }
      auto tk = transKeys.find(jn);
      if (tk != transKeys.end()) {
        const float dl[3] = {tk->second.second[0] - tk->second.first[0], tk->second.second[1] - tk->second.first[1],
                             tk->second.second[2] - tk->second.first[2]};
        xform_dir(pw, dl, part.pressMove);
        part.hasPress = true;
      }
    } else {
      std::memcpy(part.pivot, part.centre, sizeof(part.pivot));
    }
    out->parts.push_back(part);
  }
  // The face's axes: out of it = a face button's press reversed (else the stick's cap from its pivot);
  // across it toward the trigger (the stick's "up").
  const Part* stick = nullptr;
  const Part* trigger = nullptr;
  const Part* button = nullptr;
  for (const Part& p : out->parts) {
    if (p.kind == kStick) stick = &p;
    if (p.kind == kTrigger) trigger = &p;
    if (p.kind == kButton1) button = &p;
  }
  auto set_unit = [](float* dst, float x, float y, float z) {
    const float l = std::sqrt(x * x + y * y + z * z);
    if (l < 1e-7f) return false;
    dst[0] = x / l;
    dst[1] = y / l;
    dst[2] = z / l;
    return true;
  };
  if (!(button != nullptr && set_unit(out->faceUp, -button->pressMove[0], -button->pressMove[1], -button->pressMove[2])) &&
      stick != nullptr)
    set_unit(out->faceUp, stick->centre[0] - stick->pivot[0], stick->centre[1] - stick->pivot[1], stick->centre[2] - stick->pivot[2]);
  if (stick != nullptr && trigger != nullptr) {
    float d[3] = {trigger->centre[0] - stick->pivot[0], trigger->centre[1] - stick->pivot[1], trigger->centre[2] - stick->pivot[2]};
    const float u = d[0] * out->faceUp[0] + d[1] * out->faceUp[1] + d[2] * out->faceUp[2];
    set_unit(out->faceFwd, d[0] - u * out->faceUp[0], d[1] - u * out->faceUp[1], d[2] - u * out->faceUp[2]);
  }
  float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
  for (const auto& v : out->verts)
    for (int d = 0; d < 3; ++d) {
      lo[d] = std::min(lo[d], v.pos[d]);
      hi[d] = std::max(hi[d], v.pos[d]);
    }
  std::string parts;
  for (const Part& p : out->parts) {
    char b[220];
    const float ang = 2.0f * std::acos(std::min(1.0f, std::fabs(p.pressTurn[3]))) * 57.29578f;
    std::snprintf(b, sizeof(b), "%s %u tris pivot (%.3f %.3f %.3f) centre (%.3f %.3f %.3f) press %s %.0f deg %.1f mm; ",
                  kKindNames[p.kind], p.indexCount / 3, p.pivot[0], p.pivot[1], p.pivot[2], p.centre[0], p.centre[1],
                  p.centre[2], p.hasPress ? "yes" : "no", ang,
                  1000.0f * std::sqrt(p.pressMove[0] * p.pressMove[0] + p.pressMove[1] * p.pressMove[1] + p.pressMove[2] * p.pressMove[2]));
    parts += b;
  }
  clog("model: %zu vertices, box x[%.3f %.3f] y[%.3f %.3f] z[%.3f %.3f], face up (%.2f %.2f %.2f) fwd (%.2f %.2f %.2f)",
       out->verts.size(), lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], out->faceUp[0], out->faceUp[1], out->faceUp[2],
       out->faceFwd[0], out->faceFwd[1], out->faceFwd[2]);
  clog("model parts: %s", parts.c_str());
  return true;
}

// ---------------------------------------------------------------------------------------------
// Loader thread
// ---------------------------------------------------------------------------------------------

std::mutex s_mutex;
std::unique_ptr<Model> s_ready[2];
std::thread s_thread;
std::atomic<bool> s_stop{false};
bool s_started = false;

template <class T>
bool proc(XrInstance instance, const char* name, T* out) {
  PFN_xrVoidFunction fn = nullptr;
  if (XR_FAILED(xrGetInstanceProcAddr(instance, name, &fn)) || fn == nullptr) return false;
  *out = reinterpret_cast<T>(fn);
  return true;
}

void loader(XrInstance instance, XrSession session) {
  PFN_xrEnumerateRenderModelPathsFB enumerate = nullptr;
  PFN_xrGetRenderModelPropertiesFB properties = nullptr;
  PFN_xrLoadRenderModelFB load = nullptr;
  if (!proc(instance, "xrEnumerateRenderModelPathsFB", &enumerate) ||
      !proc(instance, "xrGetRenderModelPropertiesFB", &properties) || !proc(instance, "xrLoadRenderModelFB", &load)) {
    clog("render models: entry points missing");
    return;
  }
  // The runtime wants the paths enumerated before any properties are asked for.
  uint32_t pathCount = 0;
  if (XR_FAILED(enumerate(session, 0, &pathCount, nullptr))) {
    clog("render models: enumerate failed");
    return;
  }
  std::vector<XrRenderModelPathInfoFB> paths(pathCount, XrRenderModelPathInfoFB{XR_TYPE_RENDER_MODEL_PATH_INFO_FB});
  enumerate(session, pathCount, &pathCount, paths.data());
  {
    std::string list;
    for (const auto& p : paths) {
      char buf[XR_MAX_PATH_LENGTH];
      uint32_t n = 0;
      if (XR_SUCCEEDED(xrPathToString(instance, p.path, sizeof(buf), &n, buf))) list += std::string(buf) + " ";
    }
    clog("render models: %u paths: %s", pathCount, list.c_str());
  }
  const char* const kPaths[2] = {"/model_fb/controller/left", "/model_fb/controller/right"};
  const char* const kFiles[2] = {"/storage/emulated/0/Strikers/xr_ctrl_left.glb", "/storage/emulated/0/Strikers/xr_ctrl_right.glb"};
  bool loaded[2] = {false, false};
  int lastResult[2] = {1, 1};
  for (int attempt = 0; !s_stop.load() && !(loaded[0] && loaded[1]); ++attempt) {
    for (int h = 0; h < 2 && !s_stop.load(); ++h) {
      if (loaded[h]) continue;
      XrPath path = XR_NULL_PATH;
      xrStringToPath(instance, kPaths[h], &path);
      XrRenderModelCapabilitiesRequestFB caps{XR_TYPE_RENDER_MODEL_CAPABILITIES_REQUEST_FB};
      caps.flags = XR_RENDER_MODEL_SUPPORTS_GLTF_2_0_SUBSET_2_BIT_FB;
      XrRenderModelPropertiesFB props{XR_TYPE_RENDER_MODEL_PROPERTIES_FB};
      props.next = &caps;
      XrResult r = properties(session, path, &props);
      if (r != XR_SUCCESS || props.modelKey == XR_NULL_RENDER_MODEL_KEY_FB) {
        if (static_cast<int>(r) != lastResult[h]) clog("render models: %s not available yet (%d)", kPaths[h], static_cast<int>(r));
        lastResult[h] = static_cast<int>(r);
        continue;
      }
      XrRenderModelLoadInfoFB info{XR_TYPE_RENDER_MODEL_LOAD_INFO_FB};
      info.modelKey = props.modelKey;
      XrRenderModelBufferFB buffer{XR_TYPE_RENDER_MODEL_BUFFER_FB};
      r = load(session, &info, &buffer);
      if (XR_FAILED(r) || buffer.bufferCountOutput == 0) {
        if (static_cast<int>(r) != lastResult[h]) clog("render models: %s load size failed (%d)", kPaths[h], static_cast<int>(r));
        lastResult[h] = static_cast<int>(r);
        continue;
      }
      std::vector<uint8_t> glb(buffer.bufferCountOutput);
      buffer.bufferCapacityInput = static_cast<uint32_t>(glb.size());
      buffer.buffer = glb.data();
      r = load(session, &info, &buffer);
      if (XR_FAILED(r)) {
        clog("render models: %s load failed (%d)", kPaths[h], static_cast<int>(r));
        continue;
      }
      clog("render models: %s = \"%s\" vendor %04x version %u, %zu bytes", kPaths[h], props.modelName, props.vendorId,
           props.modelVersion, glb.size());
      if (FILE* f = std::fopen(kFiles[h], "wb")) {
        std::fwrite(glb.data(), 1, glb.size(), f);
        std::fclose(f);
      }
      auto model = std::make_unique<Model>();
      model->name = props.modelName;
      loaded[h] = true;  // one try at reading it either way
      if (build_model(glb, model.get())) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_ready[h] = std::move(model);
      }
    }
    // Controllers not on yet (or asleep): look again in a couple of seconds.
    for (int i = 0; i < 20 && !s_stop.load() && !(loaded[0] && loaded[1]); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

}  // namespace

const char* extension_name() { return XR_FB_RENDER_MODEL_EXTENSION_NAME; }

void start(XrInstance instance, XrSession session, bool systemSupports) {
  if (s_started) return;
  s_started = true;
  if (!systemSupports) {
    clog("render models: this headset does not offer them; no controllers drawn");
    return;
  }
  s_stop = false;
  s_thread = std::thread(loader, instance, session);
}

void stop() {
  s_stop = true;
  if (s_thread.joinable()) s_thread.join();
}

bool take(int hand, Model* out) {
  if (hand < 0 || hand > 1) return false;
  std::lock_guard<std::mutex> lock(s_mutex);
  if (!s_ready[hand]) return false;
  *out = std::move(*s_ready[hand]);
  s_ready[hand].reset();
  return true;
}

}  // namespace xrctrl

#endif  // __ANDROID__ && STRIKERS_XR
