// See xr_evo.h. Built with xr_quest.cpp in strikers_xr (C++20, no port prelude).

#if defined(__ANDROID__) && defined(STRIKERS_XR)

#define VK_USE_PLATFORM_ANDROID_KHR 1
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#include "xr_evo.h"
#include "../../include/port/xr_guest.h"  // r105: PortGuestSizeScale

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace xrevo {
namespace {

#define ELOG(...)                          \
  do {                                     \
    std::fprintf(stderr, "[evo] ");        \
    std::fprintf(stderr, __VA_ARGS__);     \
    std::fprintf(stderr, "\n");            \
    std::fflush(stderr);                   \
  } while (0)

std::string home_dir() {
  const char* home = std::getenv("STRIKERS_HOME");
  return home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers";
}

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  bool ok = size > 0 && size < (64L << 20);
  if (ok) {
    out->resize(static_cast<size_t>(size));
    ok = std::fread(out->data(), 1, out->size(), f) == out->size();
  }
  std::fclose(f);
  return ok;
}

uint32_t rd32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}
float rdf(const uint8_t* p) {
  float v;
  std::memcpy(&v, p, 4);
  return v;
}
uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

// row vectors: out = a * b (4 x 4)
void mul(const float* a, const float* b, float* out) {
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] + a[r * 4 + 2] * b[2 * 4 + c] +
                       a[r * 4 + 3] * b[3 * 4 + c];
}

struct Vert {
  float pos[3], nrm[3];
  uint8_t node[4];
  float w[4];
};

}  // namespace

struct Figure {
  uint32_t mode = 0;  // GPK2 mode bits: 1 retarget, 2 flies, 4 floats
  std::vector<bool> hasInv;
  std::vector<float> inv;  // nodes x 16
  std::vector<Vert> verts;
  uint32_t frames = 0, nodes = 0;
  float fps = 30.0f;
  std::vector<float> pose;  // frames x nodes x 12 (rows 0-2 x y z, then the position)
  float height = 1.0f, radius = 0.4f, front = 0.3f, middle[2] = {0.0f, 0.0f};
  float size = 1.0f;  // r105: its own size over its pack's (guests/limbs.txt "size", as in the match)
  // r119 (Lance: "i want more interaction"): its moves by kind (a home figure's loop file's "SEG1" part) and its
  // head (it looks at you): the nodes that turn with it
  std::vector<Segment> segs;
  int head = -1;
  std::vector<uint8_t> inHead;  // nodes: 1 = the head or under it
};

namespace {

// A GX RGBA8 picture (4 x 4 tiles: 32 bytes AR, then 32 bytes GB) as plain RGBA rows.
bool decode_rgba8(const uint8_t* data, size_t size, uint32_t w, uint32_t h, std::vector<uint8_t>* out) {
  const uint32_t tw = (w + 3) / 4, th = (h + 3) / 4;
  if (size_t(tw) * th * 64 > size) return false;
  out->assign(size_t(w) * h * 4, 255);
  const uint8_t* p = data;
  for (uint32_t ty = 0; ty < th; ++ty)
    for (uint32_t tx = 0; tx < tw; ++tx, p += 64)
      for (int k = 0; k < 16; ++k) {
        const uint32_t x = tx * 4 + k % 4, y = ty * 4 + k / 4;
        if (x >= w || y >= h) continue;
        uint8_t* o = &(*out)[(size_t(y) * w + x) * 4];
        o[3] = p[k * 2];
        o[0] = p[k * 2 + 1];
        o[1] = p[32 + k * 2];
        o[2] = p[32 + k * 2 + 1];
      }
  return true;
}

// One frame's skinning matrices (row vectors) at time t: the pose's two nearest frames blended; r119: the head (and
// all under it) turned by lookYaw about +Z and lookPitch up, round the head (Strikers model space: facing +X).
void skin_at(const Figure& f, float t, std::vector<float>* skin, float lookYaw = 0.0f, float lookPitch = 0.0f) {
  const uint32_t n = f.nodes;
  skin->assign(size_t(n) * 16, 0.0f);
  const float fr = std::max(0.0f, t) * f.fps;
  const uint32_t i0 = static_cast<uint32_t>(fr) % f.frames, i1 = (i0 + 1) % f.frames;
  const float a = fr - std::floor(fr);
  // r119: the look: pitch up about +Y (row vectors: +X toward +Z), then yaw about +Z; round the head's place
  const bool look = f.head >= 0 && (lookYaw != 0.0f || lookPitch != 0.0f) && f.inHead.size() == n;
  float M[3][3] = {}, pivot[3] = {};
  if (look) {
    const float cp = std::cos(lookPitch), sp = std::sin(lookPitch), cy = std::cos(lookYaw), sy = std::sin(lookYaw);
    const float P[3][3] = {{cp, 0.0f, sp}, {0.0f, 1.0f, 0.0f}, {-sp, 0.0f, cp}};
    const float Y[3][3] = {{cy, sy, 0.0f}, {-sy, cy, 0.0f}, {0.0f, 0.0f, 1.0f}};
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c) M[r][c] = P[r][0] * Y[0][c] + P[r][1] * Y[1][c] + P[r][2] * Y[2][c];
    const float* h0 = &f.pose[(size_t(i0) * n + f.head) * 12];
    const float* h1 = &f.pose[(size_t(i1) * n + f.head) * 12];
    for (int c = 0; c < 3; ++c) pivot[c] = h0[9 + c] + (h1[9 + c] - h0[9 + c]) * a;
  }
  for (uint32_t i = 0; i < n; ++i) {
    const float* p0 = &f.pose[(size_t(i0) * n + i) * 12];
    const float* p1 = &f.pose[(size_t(i1) * n + i) * 12];
    float world[16] = {};
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 3; ++c) world[r * 4 + c] = p0[r * 3 + c] + (p1[r * 3 + c] - p0[r * 3 + c]) * a;
    world[15] = 1.0f;
    if (look && f.inHead[i] != 0) {
      for (int r = 0; r < 3; ++r) {  // the axes turned
        const float x = world[r * 4], y = world[r * 4 + 1], z = world[r * 4 + 2];
        for (int c = 0; c < 3; ++c) world[r * 4 + c] = x * M[0][c] + y * M[1][c] + z * M[2][c];
      }
      const float x = world[12] - pivot[0], y = world[13] - pivot[1], z = world[14] - pivot[2];
      for (int c = 0; c < 3; ++c) world[12 + c] = x * M[0][c] + y * M[1][c] + z * M[2][c] + pivot[c];
    }
    if (f.hasInv[i]) {
      mul(&f.inv[size_t(i) * 16], world, &(*skin)[size_t(i) * 16]);
    } else {
      std::memcpy(&(*skin)[size_t(i) * 16], world, sizeof(world));
    }
  }
}

// A vertex in the Strikers model space (Z up, facing +X), skinned.
void skin_vert(const Figure& f, const std::vector<float>& skin, const Vert& v, float p[3], float n[3]) {
  float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0, tw = 0;
  for (int k = 0; k < 4; ++k) {
    const float w = v.w[k];
    if (w <= 0.0f || v.node[k] >= f.nodes) continue;
    const float* m = &skin[size_t(v.node[k]) * 16];
    px += w * (v.pos[0] * m[0] + v.pos[1] * m[4] + v.pos[2] * m[8] + m[12]);
    py += w * (v.pos[0] * m[1] + v.pos[1] * m[5] + v.pos[2] * m[9] + m[13]);
    pz += w * (v.pos[0] * m[2] + v.pos[1] * m[6] + v.pos[2] * m[10] + m[14]);
    nx += w * (v.nrm[0] * m[0] + v.nrm[1] * m[4] + v.nrm[2] * m[8]);
    ny += w * (v.nrm[0] * m[1] + v.nrm[1] * m[5] + v.nrm[2] * m[9]);
    nz += w * (v.nrm[0] * m[2] + v.nrm[1] * m[6] + v.nrm[2] * m[10]);
    tw += w;
  }
  if (tw <= 0.0f) {
    const float* m = &skin[0];
    px = v.pos[0] * m[0] + v.pos[1] * m[4] + v.pos[2] * m[8] + m[12];
    py = v.pos[0] * m[1] + v.pos[1] * m[5] + v.pos[2] * m[9] + m[13];
    pz = v.pos[0] * m[2] + v.pos[1] * m[6] + v.pos[2] * m[10] + m[14];
    nx = v.nrm[0];
    ny = v.nrm[1];
    nz = v.nrm[2];
    tw = 1.0f;
  }
  p[0] = px / tw;
  p[1] = py / tw;
  p[2] = pz / tw;
  const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
  const float inv = nl > 1e-12f ? 1.0f / nl : 0.0f;
  n[0] = nx * inv;
  n[1] = ny * inv;
  n[2] = nz * inv;
}

}  // namespace

Figure* load(const std::string& pack, const std::string& posePack, xrfx::Model* model) {
  return load_files(home_dir() + "/guests/" + pack + ".gpk", home_dir() + "/guests/evo/" + posePack + ".gev", model);
}

// r117: from any pack and pose file (the home corner's: guests/home/<pack>.gpk / .gev)
Figure* load_files(const std::string& packPath, const std::string& posePath, xrfx::Model* model) {
  std::vector<uint8_t> b, g;
  // r118: its pack name (the file's, no folder or .gpk) - r117 used the whole path, so guests/limbs.txt's
  // "<pack> size" (r105: Grovyle x1.25 ...) no longer matched in the 3D evolution
  std::string pack = packPath;
  const size_t sl = pack.find_last_of('/');
  if (sl != std::string::npos) pack = pack.substr(sl + 1);
  if (pack.size() > 4 && pack.compare(pack.size() - 4, 4, ".gpk") == 0) pack.resize(pack.size() - 4);
  if (!read_file(packPath, &b) || b.size() < 64 || std::memcmp(b.data(), "GPK2", 4) != 0) {
    ELOG("%s: no retarget pack at %s", pack.c_str(), packPath.c_str());
    return nullptr;
  }
  if (!read_file(posePath, &g) || g.size() < 16 || std::memcmp(g.data(), "GEV1", 4) != 0) {
    ELOG("%s: no standing pose at %s", pack.c_str(), posePath.c_str());
    return nullptr;
  }
  const uint8_t* p = b.data() + 4;
  const uint8_t* end = b.data() + b.size();
  const uint32_t nNodes = rd32(p + 4), nPackets = rd32(p + 8), nTex = rd32(p + 12);
  Figure* f = new Figure();
  f->mode = rd32(b.data() + 60);
  f->nodes = nNodes;
  f->frames = rd32(g.data() + 8);
  f->fps = rdf(g.data() + 12);
  if (rd32(g.data() + 4) != nNodes || f->frames == 0 || f->fps <= 0.0f ||
      g.size() < 16 + size_t(f->frames) * nNodes * 12 * 4 || nNodes == 0 || nNodes > 400) {
    ELOG("%s: pose file does not fit the pack (%u nodes in the pack, %u in the pose)", pack.c_str(), nNodes, rd32(g.data() + 4));
    delete f;
    return nullptr;
  }
  f->pose.resize(size_t(f->frames) * nNodes * 12);
  std::memcpy(f->pose.data(), g.data() + 16, f->pose.size() * 4);
  // r119: after the frames, "SEG1": its moves (count, the head node or ~0, then per move: first frame, frames, kind)
  {
    const size_t at = 16 + f->pose.size() * 4;
    if (g.size() >= at + 12 && std::memcmp(g.data() + at, "SEG1", 4) == 0) {
      const uint32_t count = rd32(g.data() + at + 4), head = rd32(g.data() + at + 8);
      if (count < 256 && g.size() >= at + 12 + size_t(count) * 12) {
        for (uint32_t i = 0; i < count; ++i) {
          Segment sg;
          sg.start = rd32(g.data() + at + 12 + i * 12);
          sg.frames = rd32(g.data() + at + 16 + i * 12);
          sg.kind = rd32(g.data() + at + 20 + i * 12);
          if (sg.frames > 0 && sg.start + sg.frames <= f->frames) f->segs.push_back(sg);
        }
        f->head = head < nNodes ? static_cast<int>(head) : -1;
      }
    }
  }
  p = b.data() + 64;
  std::vector<int32_t> parents(nNodes, -1);
  f->hasInv.resize(nNodes);
  f->inv.resize(size_t(nNodes) * 16);
  for (uint32_t i = 0; i < nNodes; ++i, p += 156) {
    if (p + 156 > end) {
      delete f;
      return nullptr;
    }
    f->hasInv[i] = rd32(p + 20) != 0;
    std::memcpy(&f->inv[size_t(i) * 16], p + 24, 64);
    parents[i] = static_cast<int32_t>(rd32(p + 4));
  }
  // r119: the head and everything under it (parents come before their children)
  if (f->head >= 0) {
    f->inHead.assign(nNodes, 0);
    for (uint32_t i = 0; i < nNodes; ++i) {
      int k = static_cast<int>(i);
      for (int guard = 0; guard < 64 && k >= 0 && k < static_cast<int>(nNodes); ++guard) {
        if (k == f->head) {
          f->inHead[i] = 1;
          break;
        }
        const int par = parents[k];
        k = par < k ? par : -1;
      }
    }
  }
  // the pictures (GX RGBA8 records, as meleepack.texture_record writes them), by hash
  std::unordered_map<uint32_t, int> texIndex;
  xrfx::Model m;
  for (uint32_t i = 0; i < nTex && p + 8 <= end; ++i) {
    const uint32_t hash = rd32(p), sz = rd32(p + 4);
    const uint8_t* rec = p + 8;
    p += 8 + ((sz + 3) & ~3u);
    if (rec + sz > end || sz < 0x20) continue;
    const uint32_t fmt = be32(rec + 4);
    const uint32_t w = be16(rec + 0x0E), h = be16(rec + 0x10);
    std::vector<uint8_t> rgba;
    if (fmt != 3 || w == 0 || h == 0 || w > 2048 || h > 2048 || !decode_rgba8(rec + 0x20, sz - 0x20, w, h, &rgba)) {
      ELOG("%s: picture %08x not RGBA8 (format %u), grey", pack.c_str(), hash, fmt);
      rgba.assign(4, 160);
      rgba[3] = 255;
      texIndex[hash] = static_cast<int>(m.texRgba.size());
      m.texRgba.push_back(std::move(rgba));
      m.texW.push_back(1);
      m.texH.push_back(1);
      continue;
    }
    texIndex[hash] = static_cast<int>(m.texRgba.size());
    m.texRgba.push_back(std::move(rgba));
    m.texW.push_back(w);
    m.texH.push_back(h);
  }
  int grey = -1;  // for a part whose picture isn't there
  // the parts: every packet's strip as triangles, its vertices after the last one's
  for (uint32_t i = 0; i < nPackets && p + 12 <= end; ++i) {
    const uint32_t texHash = rd32(p);
    uint32_t nv = rd32(p + 4);
    const uint32_t ni = rd32(p + 8);
    p += 12;
    xrfx::ModelPart part;
    part.soft = (nv & 0xC0000000u) != 0;
    part.flameFrame = (nv & 0x40000000u) != 0 ? static_cast<int>((nv >> 28) & 3u) : -1;
    nv &= 0x0FFFFFFFu;
    if (p + size_t(nv) * 52 + size_t(ni) * 2 > end) break;
    const uint32_t base = static_cast<uint32_t>(f->verts.size());
    for (uint32_t v = 0; v < nv; ++v) {
      Vert vt;
      std::memcpy(vt.pos, p, 12);
      std::memcpy(vt.nrm, p + 12, 12);
      m.uv.push_back(rdf(p + 24));
      m.uv.push_back(rdf(p + 28));
      std::memcpy(vt.node, p + 32, 4);
      std::memcpy(vt.w, p + 36, 16);
      f->verts.push_back(vt);
      p += 52;
    }
    std::vector<uint16_t> strip(ni);
    if (ni > 0) std::memcpy(strip.data(), p, size_t(ni) * 2);
    p += (size_t(ni) * 2 + 3) & ~size_t(3);
    part.firstIndex = static_cast<uint32_t>(m.indices.size());
    for (uint32_t k = 0; k + 2 < ni; ++k) {
      const uint32_t a = strip[k], c = strip[k + 1], d = strip[k + 2];
      if (a == c || c == d || a == d || a >= nv || c >= nv || d >= nv) continue;
      m.indices.push_back(base + a);
      m.indices.push_back(base + c);
      m.indices.push_back(base + d);
    }
    part.indexCount = static_cast<uint32_t>(m.indices.size()) - part.firstIndex;
    auto it = texIndex.find(texHash);
    if (it != texIndex.end()) {
      part.tex = it->second;
    } else {
      if (grey < 0) {
        grey = static_cast<int>(m.texRgba.size());
        m.texRgba.push_back(std::vector<uint8_t>{160, 160, 160, 255});
        m.texW.push_back(1);
        m.texH.push_back(1);
      }
      part.tex = grey;
    }
    if (part.indexCount > 0) m.parts.push_back(part);
  }
  m.vertexCount = static_cast<uint32_t>(f->verts.size());
  if (m.vertexCount == 0 || m.indices.empty()) {
    ELOG("%s: nothing to draw", pack.c_str());
    delete f;
    return nullptr;
  }
  // standing: its height, its middle and how far it reaches (the pose's first frame)
  std::vector<float> skin;
  skin_at(*f, 0.0f, &skin);
  float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
  for (const Vert& v : f->verts) {
    float q[3], n[3];
    skin_vert(*f, skin, v, q, n);
    for (int k = 0; k < 3; ++k) {
      lo[k] = std::min(lo[k], q[k]);
      hi[k] = std::max(hi[k], q[k]);
    }
  }
  // r105: as big as it is in the match (its own size, guests/limbs.txt)
  f->size = PortGuestSizeScale(pack.c_str());
  f->height = std::max(0.2f, hi[2] * f->size);
  // it stands where its root is (its feet); how far it reaches out in front of that (+X: Charizard's
  // idle holds its head 0.9 m forward), and round it
  f->middle[0] = 0.0f;
  f->middle[1] = 0.0f;
  f->front = std::max(0.1f, hi[0] * f->size);
  f->radius = std::max(0.15f, 0.5f * f->size * std::max(hi[0] - lo[0], hi[1] - lo[1]));
  ELOG("%s: %u vertices, %zu triangles, %zu pictures, %u nodes, %u pose frames; %.2f m tall, %.2f m round, mode %u",
       pack.c_str(), m.vertexCount, m.indices.size() / 3, m.texRgba.size(), nNodes, f->frames, f->height, f->radius, f->mode);
  if (model != nullptr) *model = std::move(m);
  return f;
}

void release(Figure* f) { delete f; }

float height(const Figure* f) { return f != nullptr ? f->height : 1.0f; }
float radius(const Figure* f) { return f != nullptr ? f->radius : 0.4f; }
float front(const Figure* f) { return f != nullptr ? f->front : 0.3f; }
float duration(const Figure* f) { return f != nullptr && f->fps > 0.0f ? static_cast<float>(f->frames) / f->fps : 1.0f; }

const std::vector<Segment>& segments(const Figure* f) {
  static const std::vector<Segment> kNone;
  return f != nullptr ? f->segs : kNone;
}
float fps(const Figure* f) { return f != nullptr ? f->fps : 30.0f; }
bool looks(const Figure* f) { return f != nullptr && f->head >= 0; }

bool clean_stretch(const Figure* f, uint32_t start, uint32_t frames, uint32_t minFrames, float maxTravel, uint32_t* outStart,
                   uint32_t* outFrames, float* travel) {
  if (f == nullptr || frames < 2 || f->nodes == 0 || start + frames > f->frames) return false;
  const uint32_t n = f->nodes;
  auto at = [&](uint32_t fr, uint32_t node) { return &f->pose[(size_t(fr) * n + node) * 12 + 9]; };  // (its place: Z up)
  float zlo = 1e9f, zhi = -1e9f;  // (its height: its first frame's)
  for (uint32_t k = 0; k < n; ++k) zlo = std::min(zlo, at(0, k)[2]), zhi = std::max(zhi, at(0, k)[2]);
  const float H = std::max(1e-4f, zhi - zlo);
  std::vector<float> step(frames - 1), mx(frames), my(frames);
  for (uint32_t i = 0; i < frames; ++i) {
    double sx = 0.0, sy = 0.0;
    for (uint32_t k = 0; k < n; ++k) sx += at(start + i, k)[0], sy += at(start + i, k)[1];
    mx[i] = static_cast<float>(sx / n);
    my[i] = static_cast<float>(sy / n);
    if (i + 1 < frames) {
      float m = 0.0f;
      for (uint32_t k = 0; k < n; ++k) {
        const float* a = at(start + i, k);
        const float* b = at(start + i + 1, k);
        const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
        m = std::max(m, dx * dx + dy * dy + dz * dz);
      }
      step[i] = std::sqrt(m);
    }
  }
  std::vector<float> sorted = step;
  std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
  float med = sorted[sorted.size() / 2];
  if (sorted.size() % 2 == 0 && sorted.size() >= 2) {  // (numpy's median: the two middle ones' mean)
    const float hi = med;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2 - 1, sorted.end());
    med = 0.5f * (hi + sorted[sorted.size() / 2 - 1]);
  }
  bool found = false;
  uint32_t a = 0;
  for (uint32_t i = 0; i <= frames - 1; ++i) {
    const bool cut = i == frames - 1 || (step[i] > 5.0f * med && step[i] > 0.06f * H);
    if (!cut) continue;
    const uint32_t e = i + 1, len = e - a;  // (a stretch: frames a .. e - 1)
    if (len >= minFrames) {
      double path = 0.0;
      for (uint32_t j = a + 1; j < e; ++j) path += std::sqrt((mx[j] - mx[j - 1]) * (mx[j] - mx[j - 1]) + (my[j] - my[j - 1]) * (my[j] - my[j - 1]));
      const float tr = static_cast<float>(path / H / std::max(1u, len - 1) * f->fps);
      if ((maxTravel < 0.0f || tr <= maxTravel) && (!found || len > *outFrames)) {
        found = true;
        *outStart = start + a;
        *outFrames = len;
        if (travel != nullptr) *travel = tr;
      }
    }
    a = e;
  }
  return found;
}

void pose(const Figure* f, float t, const XrVector3f& feet, float yaw, float scale, float lift, std::vector<float>* out,
          float lookYaw, float lookPitch) {
  if (f == nullptr) return;
  scale *= f->size;  // r105
  std::vector<float> skin;
  skin_at(*f, t, &skin, lookYaw, lookPitch);
  out->resize(f->verts.size() * 6);
  const float cs = std::cos(yaw), sn = std::sin(yaw);
  // Strikers model space (x, y, z) -> LOCAL as xr_fx's game_to_room: (x, z, -y), turned by yaw about +Y;
  // its middle over `feet`
  const float mx = f->middle[0], my = f->middle[1];
  float* o = out->data();
  for (const Vert& v : f->verts) {
    float q[3], n[3];
    skin_vert(*f, skin, v, q, n);
    // r130 (Lance: Moon Knight's cape in his kneel, Dr. Strange's too): nothing under the floor it stands on - a
    // cape folded round a kneel lies flat on the floor instead of 0.3 through it
    if (q[2] < 0.0f) q[2] = 0.0f;
    const float a = (q[0] - mx) * scale, b = q[2] * scale + lift, c = -(q[1] - my) * scale;
    o[0] = a * cs + c * sn + feet.x;
    o[1] = b + feet.y;
    o[2] = -a * sn + c * cs + feet.z;
    const float na = n[0], nb = n[2], nc = -n[1];
    o[3] = na * cs + nc * sn;
    o[4] = nb;
    o[5] = -na * sn + nc * cs;
    o += 6;
  }
}

}  // namespace xrevo

#endif  // __ANDROID__ && STRIKERS_XR
