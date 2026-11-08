// strikers-quest r168: BALL SKINS, the game's side (include/port/ball_skin.h). Looks only: nothing here touches the
// ball's physics, size or collision - DrawableBall.cpp hands this the ball's per-frame copy of its model (the one
// DrawableModel::DrawModel makes before its callback, glModelDupNoStreams) and this swaps, in that copy only:
//   - its geometry: a UV sphere of ours at the ball model's own radius (non-indexed triangles, host-endian arrays laid
//     out like the ball's own streams - the same stream ids, so the game's vertex set-up and programs are unchanged);
//   - its picture: one frame of a looping texture of ours (ball_shade.h, baked on a thread at the first need; RGBA8
//     with its mips, one GX texture object a frame, so Aurora uploads each frame once and keeps it).
// Every eye re-send (the Tabletop field, the life-size stadium) and the broadcast picture (the big screen) send the
// same packets, so the skin shows in every view and in the replays. CLASSIC (or anything unexpected about the ball's
// model) leaves the game's ball exactly as it was.
//
// Cost: the textures are made once (FIREBALL 32 frames 256 x 128, ~5.6 MB; PLANET EARTH 96 frames 192 x 96, ~9.4 MB,
// with mips; only the skins in use), the sphere 3,072 vertices (~84 KB) made once; per frame nothing is uploaded.

#include "port/ball_skin.h"

#include "NL/gl/glModel.h"
#include "NL/gl/glState.h"
#include "NL/gl/glUserData.h"
#include "NL/glx/glxTexture.h"
#include "dolphin/gx/GXTexture.h"
#include "dolphin/os.h"

#include "ball_shade.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

extern "C" void* port_region_reserve(std::size_t size, std::size_t align);  // memalloc.cpp (glx_GetTex's own region)
extern "C" int PortBroadcastNdc(float x, float y, float z, float* ndcX, float* ndcY);  // glPlat.cpp

namespace {

using ballshade::V;

double now_sec() {
  using namespace std::chrono;
  static const steady_clock::time_point t0 = steady_clock::now();
  return duration<double>(steady_clock::now() - t0).count();
}

// ------------------------------------------------------------------------------------------------------------------
// The textures: a skin's frames, baked on a thread (pixels only), made GX texture objects on the game thread
// ------------------------------------------------------------------------------------------------------------------
struct Bake {
  std::atomic<int> state{0};  // 0 not asked, 1 baking, 2 pixels ready, 3 textures ready, -1 failed
  int w = 0, h = 0, levels = 0, frames = 0;
  size_t frameBytes = 0;
  std::vector<uint8_t> data;           // every frame's mip chain, one after the other
  std::vector<PlatTexture*> tex;       // one a frame (game thread)
};
Bake g_bake[PORT_BALL_COUNT];

void mip_chain(uint8_t* base, int w, int h, int levels) {
  uint8_t* src = base;
  for (int l = 1; l < levels; ++l) {
    const int sw = w >> (l - 1), sh = h >> (l - 1);
    const int dw = sw / 2 > 0 ? sw / 2 : 1, dh = sh / 2 > 0 ? sh / 2 : 1;
    uint8_t* dst = src + static_cast<size_t>(sw) * sh * 4;
    for (int y = 0; y < dh; ++y)
      for (int x = 0; x < dw; ++x)
        for (int c = 0; c < 4; ++c) {
          const int x0 = std::min(2 * x, sw - 1), x1 = std::min(2 * x + 1, sw - 1);
          const int y0 = std::min(2 * y, sh - 1), y1 = std::min(2 * y + 1, sh - 1);
          const int s = src[(static_cast<size_t>(y0) * sw + x0) * 4 + c] + src[(static_cast<size_t>(y0) * sw + x1) * 4 + c] +
                        src[(static_cast<size_t>(y1) * sw + x0) * 4 + c] + src[(static_cast<size_t>(y1) * sw + x1) * 4 + c];
          dst[(static_cast<size_t>(y) * dw + x) * 4 + c] = static_cast<uint8_t>((s + 2) / 4);
        }
    src = dst;
  }
}

void bake_thread(int skin) {
  Bake& b = g_bake[skin];
  const double t0 = now_sec();
  // r170: every skin from its table (ball_shade.h skin_defs: frames, size); mips down to a few texels
  const ballshade::SkinDef& def = ballshade::skin_defs()[skin];
  b.w = def.w, b.h = def.h, b.frames = std::max(1, def.frames);
  b.levels = 1;
  while ((b.h >> b.levels) >= 2 && b.levels < 8) ++b.levels;
  size_t bytes = 0;
  for (int l = 0; l < b.levels; ++l) bytes += static_cast<size_t>(std::max(b.w >> l, 1)) * std::max(b.h >> l, 1) * 4;
  b.frameBytes = bytes;
  b.data.assign(bytes * b.frames, 0);
  // the land / ocean / ice / cities don't move between PLANET EARTH's frames (the clouds and the twinkle do), but every
  // frame is the shade function's own, as the shop's preview and the PC proofs draw it
  std::vector<V> dirs(static_cast<size_t>(b.w) * b.h);
  for (int j = 0; j < b.h; ++j)
    for (int i = 0; i < b.w; ++i) dirs[static_cast<size_t>(j) * b.w + i] = ballshade::texel_dir(i, j, b.w, b.h);
  for (int k = 0; k < b.frames; ++k) {
    uint8_t* px = b.data.data() + bytes * k;
    for (int j = 0; j < b.h; ++j)
      for (int i = 0; i < b.w; ++i) {
        const V n = dirs[static_cast<size_t>(j) * b.w + i];
        const V c = ballshade::shade_frame(skin, n, k);  // r170
        uint8_t* o = px + (static_cast<size_t>(j) * b.w + i) * 4;
        o[0] = static_cast<uint8_t>(c.x * 255.0f + 0.5f);
        o[1] = static_cast<uint8_t>(c.y * 255.0f + 0.5f);
        o[2] = static_cast<uint8_t>(c.z * 255.0f + 0.5f);
        o[3] = 255;
      }
    mip_chain(px, b.w, b.h, b.levels);
  }
  OSReport("[ballskin] r168: %s baked: %d frames %dx%d (%.1f MB) in %.2f s\n", def.id,
           b.frames, b.w, b.h, static_cast<double>(b.data.size()) / 1048576.0, now_sec() - t0);
  b.state = 2;
}

void ask_bake(int skin) {
  if (skin <= 0 || skin >= PORT_BALL_COUNT) return;
  int expect = 0;
  if (!g_bake[skin].state.compare_exchange_strong(expect, 1)) return;
  std::thread(bake_thread, skin).detach();
}

// game thread: the frames into GX texture objects (once); true when the skin can be drawn
bool textures_ready(int skin) {
  if (skin <= 0 || skin >= PORT_BALL_COUNT) return false;
  Bake& b = g_bake[skin];
  const int st = b.state.load();
  if (st == 3) return true;
  if (st == 0) ask_bake(skin);
  if (st != 2) return false;
  b.tex.resize(static_cast<size_t>(b.frames), nullptr);
  for (int k = 0; k < b.frames; ++k) {
    void* mem = port_region_reserve(sizeof(PlatTexture), 32);  // (in the region: glx_GetTex takes it as a handle)
    if (mem == nullptr) {
      b.state = -1;
      OSReport("[ballskin] r168: no room for the texture headers - the game's own ball\n");
      return false;
    }
    PlatTexture* t = new (mem) PlatTexture();
    uint8_t* px = b.data.data() + b.frameBytes * k;
    t->m_Width = static_cast<u16>(b.w);
    t->m_Height = static_cast<u16>(b.h);
    t->m_Levels = static_cast<u8>(b.levels);
    t->m_MaxLevel = static_cast<u8>(b.levels);
    t->m_Format = GXTex_RGBA8;  // (the game's own bookkeeping; the GX object below says what the pixels really are)
    t->m_SwizzledData = px;
    GXInitTexObj(&t->m_TexObj, px, static_cast<u16>(b.w), static_cast<u16>(b.h), GX_TF_RGBA8_PC, GX_REPEAT, GX_CLAMP, GX_TRUE);
    GXInitTexObjMaxLOD(&t->m_TexObj, static_cast<float>(b.levels - 1));
    GXInitTexObjFilter(&t->m_TexObj, GX_LIN_MIP_LIN, GX_LINEAR);
    b.tex[static_cast<size_t>(k)] = t;
  }
  b.state = 3;
  return true;
}

PlatTexture* frame_texture(int skin, double t) {
  Bake& b = g_bake[skin];
  if (b.state.load() != 3 || b.frames <= 0) return nullptr;
  const double loop = ballshade::skin_defs()[skin].loop;  // r170
  const int k = static_cast<int>(std::floor(t / loop * b.frames)) % b.frames;
  return b.tex[static_cast<size_t>(k < 0 ? 0 : k)];
}

// ------------------------------------------------------------------------------------------------------------------
// The sphere: 32 round x 16 down, non-indexed triangles (the seam's vertices doubled), at the ball model's radius
// ------------------------------------------------------------------------------------------------------------------
constexpr int kLon = 32, kLat = 16, kVerts = kLon * kLat * 6;
struct Mesh {
  bool built = false;
  float radius = 0.0f;
  std::vector<float> pos, nrm;
  std::vector<int16_t> uv;
  std::vector<uint32_t> clr;
  // the stream table laid out like the ball's own (its ids in its order), our arrays
  glModelStream streams[12];
  int numStreams = 0;
  uint32_t layoutKey = 0;
  bool layoutOk = false;
};
Mesh g_mesh;

void build_sphere(float r) {
  Mesh& m = g_mesh;
  m.radius = r;
  m.pos.resize(kVerts * 3);
  m.nrm.resize(kVerts * 3);
  m.uv.resize(kVerts * 2);
  m.clr.assign(kVerts, 0xFFFFFFFFu);
  int at = 0;
  auto put = [&](int i, int j) {
    const float u = static_cast<float>(i) / kLon, vv = static_cast<float>(j) / kLat;
    const float lon = 2.0f * ballshade::kPi * u, th = ballshade::kPi * vv;
    const float n[3] = {std::sin(th) * std::cos(lon), std::sin(th) * std::sin(lon), std::cos(th)};
    for (int c = 0; c < 3; ++c) {
      m.pos[at * 3 + c] = n[c] * r;
      m.nrm[at * 3 + c] = n[c];
    }
    m.uv[at * 2 + 0] = static_cast<int16_t>(std::lround(u * 1024.0f));  // (S16, 10 fraction bits: glx_SetVtxAttr)
    m.uv[at * 2 + 1] = static_cast<int16_t>(std::lround(vv * 1024.0f));
    ++at;
  };
  for (int j = 0; j < kLat; ++j)
    for (int i = 0; i < kLon; ++i) {
      // two triangles a quad, wound as the game's front faces (counter-clockwise seen from outside)
      put(i, j), put(i, j + 1), put(i + 1, j + 1);
      put(i, j), put(i + 1, j + 1), put(i + 1, j);
    }
  m.built = true;
}

float read_be_f32(const uint8_t* q) {
  const uint32_t u = (uint32_t(q[0]) << 24) | (uint32_t(q[1]) << 16) | (uint32_t(q[2]) << 8) | uint32_t(q[3]);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

// the ball model's radius from its own positions (any packet's), else its drawable's bounding radius, else 0.18
float model_radius(const glModel* model, float bounding) {
  float best = 0.0f;
  if (model != nullptr) {
    for (u32 p = 0; p < model->numPackets && p < 16; ++p) {
      const glModelPacket& pk = model->packets[p];
      for (int s = 0; s < pk.numStreams && pk.streams != nullptr; ++s) {
        const glModelStream& st = pk.streams[s];
        if (st.id != 0 || st.address == 0 || st.dataSize == 0) continue;
        const uint8_t* q = reinterpret_cast<const uint8_t*>(st.address);
        const u32 n = st.stride > 0 ? std::min<u32>(st.dataSize / st.stride, 8192) : 0;
        for (u32 k = 0; k < n; ++k) {
          float x, y, z;
          const uint8_t* e = q + static_cast<size_t>(k) * st.stride;
          if (st.stride == 12) {
            if (st.beData) {
              x = read_be_f32(e), y = read_be_f32(e + 4), z = read_be_f32(e + 8);
            } else {
              std::memcpy(&x, e, 4), std::memcpy(&y, e + 4, 4), std::memcpy(&z, e + 8, 4);
            }
          } else if (st.stride == 6) {
            int16_t a[3];
            for (int c = 0; c < 3; ++c)
              a[c] = st.beData ? static_cast<int16_t>((e[c * 2] << 8) | e[c * 2 + 1]) : static_cast<int16_t>(e[c * 2] | (e[c * 2 + 1] << 8));
            x = a[0] / 256.0f, y = a[1] / 256.0f, z = a[2] / 256.0f;
          } else {
            break;
          }
          const float d = std::sqrt(x * x + y * y + z * z);
          if (d > best && d < 2.0f) best = d;
        }
      }
    }
  }
  if (best >= 0.08f && best <= 0.4f) return best;
  if (bounding >= 0.08f && bounding <= 0.4f) return bounding;
  return 0.18f;
}

// our stream table for a packet laid out as `pk` (false: something about it we don't know - leave the ball be)
bool layout_for(const glModelPacket& pk) {
  Mesh& m = g_mesh;
  uint32_t key = static_cast<uint32_t>(pk.numStreams) * 2654435761u;
  for (int s = 0; s < pk.numStreams && pk.streams != nullptr; ++s) key = key * 31u + pk.streams[s].id + 1u;
  if (key == m.layoutKey && m.numStreams > 0) return m.layoutOk;
  m.layoutKey = key;
  m.layoutOk = false;
  m.numStreams = 0;
  if (pk.streams == nullptr || pk.numStreams == 0 || pk.numStreams > 12) return false;
  bool havePos = false;
  for (int s = 0; s < pk.numStreams; ++s) {
    const u8 id = pk.streams[s].id;
    glModelStream& o = m.streams[s];
    std::memset(&o, 0, sizeof(o));
    o.id = id;
    o.beData = 0;  // (host order: ours)
    if (id == 0) {
      o.address = reinterpret_cast<uintptr_t>(m.pos.data()), o.stride = 12, o.dataSize = kVerts * 12;
      havePos = true;
    } else if (id == 1) {
      o.address = reinterpret_cast<uintptr_t>(m.nrm.data()), o.stride = 12, o.dataSize = kVerts * 12;
    } else if (id == 2) {
      o.address = reinterpret_cast<uintptr_t>(m.clr.data()), o.stride = 4, o.dataSize = kVerts * 4;
    } else if (id >= 3 && id <= 8) {
      o.address = reinterpret_cast<uintptr_t>(m.uv.data()), o.stride = 4, o.dataSize = kVerts * 4;
    } else {
      // (12: a matrix index per vertex - a skinned ball; 9-14: binormals; anything else: not ours to guess)
      OSReport("[ballskin] r168: the ball's model has stream id %u - the game's own ball stays\n", static_cast<unsigned>(id));
      return false;
    }
  }
  m.numStreams = pk.numStreams;
  m.layoutOk = havePos;
  if (!havePos) OSReport("[ballskin] r168: the ball's model has no positions stream - the game's own ball stays\n");
  return m.layoutOk;
}

// ------------------------------------------------------------------------------------------------------------------
// Which skin, the feed
// ------------------------------------------------------------------------------------------------------------------
std::mutex g_mutex;
int g_mine = 0, g_theirs = 0, g_mySide = -1, g_online = 0;
int g_wearer = 0;     // 0 yours, 1 the friend's
int g_lastSide = -2;
PortBallFeed g_feed{};
bool g_feedAny = false;
// event detection
bool g_have = false;
float g_prevV[3] = {0, 0, 0}, g_prevP[3] = {0, 0, 0};
double g_lastKick = -9.0, g_lastBounce = -9.0;
const glModel* g_srcModel = nullptr;  // the ball's own model this frame (DrawableBall's), to tell its copy from it
int g_drawSkin = 0;

}  // namespace

// ------------------------------------------------------------------------------------------------------------------
// The C side
// ------------------------------------------------------------------------------------------------------------------
extern "C" void PortBallSkinSetIds(int mine, int theirs, int mySide, int online) {
  mine = mine > 0 && mine < PORT_BALL_COUNT ? mine : 0;
  theirs = theirs > 0 && theirs < PORT_BALL_COUNT ? theirs : 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (mine != g_mine || theirs != g_theirs) OSReport("[ballskin] r168: your ball %d, the friend's %d\n", mine, theirs);
    g_mine = mine;
    g_theirs = theirs;
    g_mySide = mySide;
    g_online = online;
  }
  // (baked ahead, on a thread: ready by the kick-off)
  if (mine > 0) ask_bake(mine);
  if (online && theirs > 0) ask_bake(theirs);
}

extern "C" int PortBallSkinFeed(PortBallFeed* out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (out != nullptr) *out = g_feed;
  return g_feedAny ? 1 : 0;
}

extern "C" void PortBallSkinGoal(float x, float y, float z) {
  float nx = 0.0f, ny = 0.0f;
  const int on = PortBroadcastNdc(x, y, z, &nx, &ny);
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_feed.goals;
  g_feed.goalPos[0] = x, g_feed.goalPos[1] = y, g_feed.goalPos[2] = z;
  g_feed.goalNdcValid = on, g_feed.goalNdc[0] = nx, g_feed.goalNdc[1] = ny;
}

extern "C" void PortBallSkinShade(int skin, float x, float y, float z, float t, float rgb[3]) {
  const V c = ballshade::shade(skin, ballshade::v(x, y, z), t);
  rgb[0] = c.x, rgb[1] = c.y, rgb[2] = c.z;
}

// ------------------------------------------------------------------------------------------------------------------
// DrawableBall.cpp's side (game thread): Game/Drawable/DrawableBall.cpp declares these (C++ linkage)
// ------------------------------------------------------------------------------------------------------------------
namespace portballskin {

// once a frame, before the ball is drawn: which side touched it last (-1 nobody yet), the ball's own model and drawable
// radius. Returns the skin to draw this frame (0: the game's own ball).
int begin_frame(int lastTouchSide, const glModel* srcModel, float boundingRadius) {
  int skin = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (lastTouchSide != g_lastSide) {
      g_lastSide = lastTouchSide;
      if (lastTouchSide >= 0 && g_mySide >= 0) {
        if (lastTouchSide == g_mySide) g_wearer = 0;
        else if (g_online) g_wearer = 1;  // (offline the computer's touches keep yours)
      }
    }
    if (!g_online) g_wearer = 0;
    skin = g_wearer == 1 ? g_theirs : g_mine;
  }
  g_srcModel = srcModel;
  if (skin > 0 && !g_mesh.built) build_sphere(model_radius(srcModel, boundingRadius));
  if (skin > 0 && !textures_ready(skin)) skin = 0;  // (still baking: the game's own ball meanwhile)
  g_drawSkin = skin;
  return skin;
}

// r170: a skin's spin (radians about its pole) at this moment, -1 when it doesn't turn; whether it is its own light
float spin_angle(int skin) {
  if (skin <= 0 || skin >= PORT_BALL_COUNT) return -1.0f;
  const float s = ballshade::skin_defs()[skin].spin;
  return s > 0.0f ? static_cast<float>(std::fmod(now_sec() / s, 1.0) * 2.0 * ballshade::kPi) : -1.0f;
}
bool emissive(int skin) { return skin > 0 && skin < PORT_BALL_COUNT && ballshade::skin_defs()[skin].emissive; }

// DrawableModel's per-frame copy of the ball's model, before it goes to the view: our sphere and this moment's frame.
// pass 0 the ball, 1 its motion blur copies, 2 its lighting pass (keeps the game's white picture). Never the model itself.
glModel* apply(glModel* m, int pass) {
  const int skin = g_drawSkin;
  if (skin <= 0 || m == nullptr || m == g_srcModel || m->numPackets == 0 || m->packets == nullptr || !g_mesh.built) return m;
  glModelPacket& pk = m->packets[0];
  if (!layout_for(pk)) return m;
  PlatTexture* tex = pass == 2 ? nullptr : frame_texture(skin, now_sec());
  if (pass != 2 && tex == nullptr) return m;
  pk.streams = g_mesh.streams;
  pk.numStreams = static_cast<u8>(g_mesh.numStreams);
  pk.indexBuffer = 0;
  pk.numVertices = static_cast<u16>(kVerts);
  pk.primType = 0;  // GX_TRIANGLES (glxSend.cpp's primitives[])
  // (both faces: GX's winding is not ours to guess - the depth test keeps the far side hidden behind the near one)
  glSetRasterState(pk.state.raster, GLS_Culling, 0);
  if (tex != nullptr)
    for (int bit = 0; bit < 5; ++bit)
      if (pk.state.texconfig & (1 << bit)) pk.state.texture[bit] = reinterpret_cast<uintptr_t>(tex);
  m->numPackets = 1;  // (the ball is one sphere: the model's other parts are its own panels)
  return m;
}

// after the ball is drawn: where it is (as drawn), its velocity, whether it shows - for the effects; kicks and bounces
// noticed here from its velocity
void feed(bool visible, const float drawnPos[3], const float gamePos[3], const float vel[3], float scale, float boundingRadius) {
  const double t = now_sec();
  const float drawnRadius =
      (g_mesh.built ? g_mesh.radius : (boundingRadius >= 0.08f && boundingRadius <= 0.4f ? boundingRadius : 0.18f)) * scale;
  float nx = 0.0f, ny = 0.0f, rx = 0.0f, ry = 0.0f;
  int on = 0;
  if (visible) {
    on = PortBroadcastNdc(gamePos[0], gamePos[1], gamePos[2], &nx, &ny);
    if (on) {
      if (PortBroadcastNdc(gamePos[0], gamePos[1], gamePos[2] + 0.18f, &rx, &ry) == 0) ry = ny + 0.01f;
    }
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  PortBallFeed& f = g_feed;
  ++f.frame;
  f.visible = visible ? 1 : 0;
  f.skin = g_drawSkin;
  for (int c = 0; c < 3; ++c) f.pos[c] = drawnPos[c], f.vel[c] = vel[c];
  f.radius = drawnRadius;
  f.ndcValid = on;
  f.ndc[0] = nx, f.ndc[1] = ny;
  f.ndcRadius = on ? std::fabs(ry - ny) : 0.0f;
  f.timeSec = t;
  g_feedAny = true;
  if (!visible) {
    g_have = false;
    return;
  }
  if (g_have) {
    const float jump = std::sqrt((gamePos[0] - g_prevP[0]) * (gamePos[0] - g_prevP[0]) + (gamePos[1] - g_prevP[1]) * (gamePos[1] - g_prevP[1]) +
                                 (gamePos[2] - g_prevP[2]) * (gamePos[2] - g_prevP[2]));
    if (jump < 3.0f) {  // (a reset - kick-off, a replay's cut - is no kick)
      const float dv[3] = {vel[0] - g_prevV[0], vel[1] - g_prevV[1], vel[2] - g_prevV[2]};
      const float dvl = std::sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
      const float sp = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
      const float sp0 = std::sqrt(g_prevV[0] * g_prevV[0] + g_prevV[1] * g_prevV[1] + g_prevV[2] * g_prevV[2]);
      const bool bounce = g_prevV[2] < -1.2f && vel[2] > 0.4f && gamePos[2] < 0.45f;
      if (bounce && t - g_lastBounce > 0.15) {
        g_lastBounce = t;
        ++f.bounces;
        for (int c = 0; c < 3; ++c) f.bouncePos[c] = drawnPos[c];
        f.bouncePos[2] = drawnPos[2] - drawnRadius;  // (where it met the ground)
        f.bounceSpeed = -g_prevV[2];
        f.bounceNdcValid = on, f.bounceNdc[0] = nx, f.bounceNdc[1] = ny;
      } else if (!bounce && dvl > 6.0f && sp > 7.0f && sp > sp0 + 3.0f && t - g_lastKick > 0.2) {
        g_lastKick = t;
        ++f.kicks;
        for (int c = 0; c < 3; ++c) f.kickPos[c] = drawnPos[c];
        f.kickSpeed = sp;
        f.kickNdcValid = on, f.kickNdc[0] = nx, f.kickNdc[1] = ny;
      }
    }
  }
  g_have = true;
  for (int c = 0; c < 3; ++c) g_prevV[c] = vel[c], g_prevP[c] = gamePos[c];
}

}  // namespace portballskin
