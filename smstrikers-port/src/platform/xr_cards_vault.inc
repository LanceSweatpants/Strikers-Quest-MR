// strikers-quest r230 (Lance 11-12): the LANCINI EXQUISITE opening - exactly his two asks, nothing else:
//  1 UNLOCK THE VAULT YOURSELF: the VAULT case (xr_vault.h) appears in front of you; you flick open BOTH brass latches with your own hands
//    (a grip / a flick of either hand at a latch, or a trigger on it - the hub's A flicks the next one too), a heavy metal click + a thunk in
//    the controllers each; the lid creaks open slowly and warm gold light spills out of the case; the pack rises out of its black velvet into
//    your hands (then it rips as every pack). THE VAULT (a box of five: its packs in velvet, the case opened once - its next packs out of the
//    open case) or a smaller one-pack case for a pack bought alone.
//  2 THE ROOM BECOMES A PRIVATE GALLERY for every Exquisite opening: the room dims to a warm gallery mood (the passthrough's own style: xr_quest
//    reads room_gallery(); a warm veil round you in the effects - the cards' light model), a soft spotlight on the card in your hand, quiet
//    lounge music (made here in code: no download), the crowd / the menus' music down (PortCardGallerySet); back to normal after the last card.
//    r219's build-ups / parties still play on top (a 1/1's lights-out inside the gallery mood).
// Included inside xr_cards.cpp's anonymous namespace after the scene (S, buzz, the sheets) and the looks.

struct VaultScene {
  bool on = false, box = false, unlocked = false, lidMoving = false;
  int caseNo = 0, firstSlot = 0, packs = 1, model = -1;
  unsigned mask = 1;
  uint64_t t0 = 0, latchNs[2] = {0, 0}, lidNs = 0, liftNs = 0;
  float yaw = 0.0f, glow = 0.0f, lastLid = 0.0f;
  XrVector3f at{0, 0, 0};
  XrQuaternionf q{0, 0, 0, 1};
  xrvault::Mesh mesh;
  std::vector<float> verts;
  std::shared_ptr<Pics> pics;
  XrVector3f prevHand[2] = {};
  bool havePrev[2] = {false, false}, prevGripV[2] = {false, false};
  XrQuaternionf liftFrom{0, 0, 0, 1};
  XrVector3f liftAt{0, 0, 0};
  float liftScale0 = 0.5f;
};
VaultScene g_vault;
bool exq_scene() { return (S.mode == kPackUp || S.mode == kAway) && (S.packData.flags & kExqPack) != 0; }

// ---------------------------------------------------------------------------------------------- its pictures (lk::xq's kit)
namespace vtex {
using namespace lk;
using namespace lk::xq;
inline float ox(const Cv& cv) { return static_cast<float>(cv.W) / kX; }  // (a picture's width in the kit's units)
inline float oy(const Cv& cv) { return static_cast<float>(cv.H) / kX; }
// pebbled black leather lit from the top left (xkit leather)
struct Leather {
  F3 b;
  uint32_t seed;
  float cell;
  F3 operator()(float X, float Y) const {
    const float x = X / kX, y = Y / kX;
    const float n1 = vnoise(x, y, cell, seed, 1), n2 = vnoise(x, y, cell * 0.55f, seed + 1u, 1);
    const float h = std::pow(sat((n1 * 0.7f + n2 * 0.3f - 0.35f) * 3.0f), 0.6f);
    const float hx = std::pow(sat((vnoise(x + 1.0f, y, cell, seed, 1) * 0.7f + vnoise(x + 1.0f, y, cell * 0.55f, seed + 1u, 1) * 0.3f - 0.35f) * 3.0f), 0.6f);
    const float hy = std::pow(sat((vnoise(x, y + 1.0f, cell, seed, 1) * 0.7f + vnoise(x, y + 1.0f, cell * 0.55f, seed + 1u, 1) * 0.3f - 0.35f) * 3.0f), 0.6f);
    const float sh = std::max(-1.0f, std::min(1.0f, ((hx - h) + (hy - h)) * -6.0f));
    return F3{std::max(0.0f, b.r * (0.9f + 0.2f * h) + sh * 7.0f), std::max(0.0f, b.g * (0.9f + 0.2f * h) + sh * 7.0f), std::max(0.0f, b.b * (0.9f + 0.2f * h) + sh * 7.0f)};
  }
};
struct Velvet {
  F3 b;
  uint32_t seed;
  float sx, sy;  // its sheen's place (0..1)
  F3 operator()(float X, float Y) const {
    const float x = X / kX, y = Y / kX;
    const float n = vnoise(x, y, 60.0f, seed, 3), f = fine(X, Y, seed + 1u) * 0.04f;
    return F3{b.r * (0.7f + 0.5f * n + f), b.g * (0.7f + 0.5f * n + f), b.b * (0.7f + 0.5f * n + f)};
  }
};
inline std::vector<uint8_t> out(const Cv& cv) { return bytes(cv, true); }
// gold corner pieces (an L each, embossed), `s` their size (kit units)
// r233 (Lance: one corner of THE VAULT a white L): the foil was painted in the picture's own place, so its sheen band / beam fell on
// one corner and topped its ramp out at pure white (the VAULT lid's (W,H) = its front-right, the one-pack lid's (W,0); logs\r233
// VCORNER) while the others ranged dark to bright gold. Now each L is painted with the (0,0) corner's foil mirrored into its own corner:
// all four the same gold.
inline void corners(Cv& cv, float s, float t) {
  const Foil fo = foil(kFGold, 7.0f);
  const float W = ox(cv), H = oy(cv);
  const float c4[4][4] = {{0, 0, 1, 1}, {W, 0, -1, 1}, {W, H, -1, -1}, {0, H, 1, -1}};
  for (const auto& c : c4) {
    const float x = c[0], y = c[1], sx = c[2], sy = c[3];
    const M m = poly(cv, {x, y, x + sx * s, y, x + sx * s, y + sy * t, x + sx * t, y + sy * t, x + sx * t, y + sy * s, x, y + sy * s});
    const float px0 = x * kX, py0 = y * kX;
    cv.fillp(m, [&](float X, float Y) { return fo(sx * (X - px0), sy * (Y - py0)); }, 1.0f, 1.0f);
    emboss(cv, m, 1.5f, 0.6f);
  }
}
// a brass plate with engraved words (cards.py plaque)
inline void plaque(Cv& cv, float x0, float y0, float x1, float y1, const std::string& name, const std::string& sub, float px) {
  const M pm = rr(cv, x0, y0, x1, y1, 5);
  cv.fill(cv.blur(cv.shift(pm, 3 * kX, 7 * kX), 6 * kX), 0x000000, 0.55f);
  Foil brass = foil(kFGold, 3.0f, false, 0.0f, 0.45f);
  brass.ca = 1.0f, brass.sa = 0.0f;
  cv.fillp(pm, [&](float X, float Y) {
    const float br = 0.82f + 0.18f * (0.5f + 0.5f * fsin(Y / kX * 1.7f + vnoise(X / kX, Y / kX, 14.0f, 9u, 1) * 4.0f));
    return brass(X, Y) * br;
  }, 1.0f, 1.0f);
  emboss(cv, pm, 2.0f, 0.55f);
  cv.fill(ring(cv, x0 + 7, y0 + 7, x1 - 7, y1 - 7, 3, 1.2f), 0x3A2A0E, 0.55f);
  for (float sx : {x0 + 18, x1 - 18}) {
    const M sc = ell(cv, sx - 5, (y0 + y1) / 2 - 5, sx + 5, (y0 + y1) / 2 + 5);
    cv.fill(sc, 0x6A4E1C);
    emboss(cv, sc, 1.0f, 0.6f);
    cv.fill(rr(cv, sx - 4, (y0 + y1) / 2 - 0.7f, sx + 4, (y0 + y1) / 2 + 0.7f, 0), 0x2A1C08);
  }
  const float cy = (y0 + y1) / 2;
  KT k = kmask(cv, name, (x0 + x1) / 2, cy - 8, px, 'c', 10, px * 0.03f, (x1 - x0) - 70);
  cv.fill(cv.shift(k.fill, 0, 1.4f * kX), 0xFFF0C0, 0.55f);
  cv.fill(k.fill, 0x241808);
  ktextc(cv, sub, (x0 + x1) / 2, y1 - 13, 9.5f, 0x241808, 'c', 60, 0.3f, (x1 - x0) - 40, 0.85f);
}
}  // namespace vtex

// the case's pictures (xrvault::Tex): drawn on a thread - the leather / velvet / brass once a session, the lid's inside (its plate's CASE
// number) each case, the pack's face from the wrapper
std::vector<std::vector<uint8_t>> vault_textures(bool box, int caseNo) {
  using namespace lk;
  using namespace lk::xq;
  using namespace vtex;
  static std::mutex m;
  static std::vector<std::vector<uint8_t>> keepBy[2];  // (all but the lid's inside; THE VAULT's, the one-pack case's)
  std::vector<std::vector<uint8_t>>& keep = keepBy[box ? 1 : 0];
  std::vector<std::vector<uint8_t>> t(xrvault::kTexCount);
  {
    std::lock_guard<std::mutex> lock(m);
    if (!keep.empty()) t = keep;
  }
  auto canvas = [](int i) { return Cv(xrvault::kTexW[i], xrvault::kTexH[i]); };
  // (the one-pack case's lid is tall: its lid pictures drawn upright (portrait) and squeezed into the picture's size, so on the lid they read true)
  auto lid_canvas = [&](int i) { return box ? Cv(xrvault::kTexW[i], xrvault::kTexH[i]) : Cv(xrvault::kTexH[i], xrvault::kTexW[i]); };
  auto lid_out = [&](const Cv& cv, int i) {
    std::vector<uint8_t> px = vtex::out(cv);
    if (box) return px;
    const int w = xrvault::kTexW[i], h = xrvault::kTexH[i];
    std::vector<uint8_t> o(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float sx = (x + 0.5f) * cv.W / w - 0.5f, sy = (y + 0.5f) * cv.H / h - 0.5f;
        const int x0 = std::max(0, std::min(cv.W - 1, static_cast<int>(sx))), y0 = std::max(0, std::min(cv.H - 1, static_cast<int>(sy)));
        const int x1 = std::min(cv.W - 1, x0 + 1), y1 = std::min(cv.H - 1, y0 + 1);
        const float fx = clampf(sx - x0, 0.0f, 1.0f), fy = clampf(sy - y0, 0.0f, 1.0f);
        for (int c = 0; c < 4; ++c) {
          auto at = [&](int xx, int yy) { return static_cast<float>(px[(static_cast<size_t>(yy) * cv.W + xx) * 4 + c]); };
          o[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<uint8_t>(std::lround((at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy));
        }
      }
    return o;
  };
  const Foil fo = foil(kFGold, 4.0f);
  if (t[xrvault::kTexFront].empty()) {
    {  // the base's front: leather, stitching, gold corners, the latches' back plates, a small brass name plate
      Cv cv = canvas(xrvault::kTexFront);
      const float W = ox(cv), H = oy(cv);
      cv.fillp(rr(cv, 0, 0, W, H, 0), Leather{f3(0x121214), 5u, 3.5f});
      cv.fill(xq::stitches(cv, 0, 0, W, H, 9, 7, 5, 1.6f), 0x8C7A5A, 0.8f);
      corners(cv, 44, 12);
      for (float u : {0.2f, 0.8f}) {
        const float lx = u * W;
        const M bp = rr(cv, lx - 20, 6, lx + 20, H * 0.62f, 4);
        cv.fill(cv.blur(cv.shift(bp, 1 * kX, 3 * kX), 3 * kX), 0x000000, 0.6f);
        cv.fillp(bp, fo, 1.0f, 1.0f);
        emboss(cv, bp, 1.5f, 0.6f);
      }
      plaque(cv, W / 2 - 92, H * 0.30f, W / 2 + 92, H * 0.80f, "EXQUISITE", "LANCINI  |  2026-27 STRIKERS", 20);
      t[xrvault::kTexFront] = out(cv);
    }
    {  // the sides / back / the lid's edges
      Cv cv = canvas(xrvault::kTexSide);
      cv.fillp(rr(cv, 0, 0, ox(cv), oy(cv), 0), Leather{f3(0x0E0E10), 6u, 3.5f});
      cv.fill(xq::stitches(cv, 0, 0, ox(cv), oy(cv), 9, 7, 5, 1.6f), 0x8C7A5A, 0.75f);
      t[xrvault::kTexSide] = out(cv);
    }
    {  // the rim
      Cv cv = canvas(xrvault::kTexRim);
      cv.fillp(rr(cv, 0, 0, ox(cv), oy(cv), 0), Leather{f3(0x1A1A1D), 7u, 3.5f});
      t[xrvault::kTexRim] = out(cv);
    }
    {  // black velvet
      Cv cv = canvas(xrvault::kTexVelvet);
      cv.fillp(rr(cv, 0, 0, ox(cv), oy(cv), 0), Velvet{f3(0x0B0B0E), 3u, 0.5f, 0.4f});
      t[xrvault::kTexVelvet] = out(cv);
    }
    {  // the lid's top: leather, a gold hairline frame, gold corners, the embossed foil gem, LANCINI EXQUISITE
      Cv cv = lid_canvas(xrvault::kTexLidTop);
      const float W = ox(cv), H = oy(cv);
      cv.fillp(rr(cv, 0, 0, W, H, 0), Leather{f3(0x111113), 8u, 3.5f});
      cv.fillp(ring(cv, 22, 22, W - 22, H - 22, 6, 1.6f), fo, 1.0f, 1.0f);
      corners(cv, 60, 14);
      xgem(cv, W / 2, H * 0.40f, 52, fo);
      {
        std::vector<float> p;
        for (int i = 0; i < 6; ++i) p.push_back(W / 2 + 52 * std::cos(-kPi / 2 + i * kPi / 3)), p.push_back(H * 0.40f + 52 * std::sin(-kPi / 2 + i * kPi / 3));
        emboss(cv, xq::poly(cv, p), 2.5f, 0.55f);
      }
      ktextc(cv, "LANCINI", W / 2, H * 0.40f + 86, 18, kIvory, 'c', 80, 0.35f, 0, 0.8f);
      ktext(cv, "EXQUISITE", W / 2, H * 0.40f + 128, 44, fo, 1.0f, 'c', 30, 1.3f, 0, 1.0f, 1.4f);
      t[xrvault::kTexLidTop] = lid_out(cv, xrvault::kTexLidTop);
    }
    {  // brushed brass (the latches, the keepers)
      Cv cv = canvas(xrvault::kTexBrass);
      Foil br = foil(kFGold, 2.0f, false, 0.0f, 0.3f);
      cv.fillp(rr(cv, 0, 0, ox(cv), oy(cv), 0), br, 1.0f, 1.0f);
      emboss(cv, rr(cv, 4, 4, ox(cv) - 4, oy(cv) - 4, 6), 1.5f, 0.5f);
      t[xrvault::kTexBrass] = out(cv);
    }
    {  // the pack's face: the wrapper, smaller (its gold foil kept in the alpha)
      const std::vector<uint8_t> big = draw_pack(false, g_setN, 2);
      Img full;
      full.w = kPackW, full.h = kPackH, full.rgba = big;
      Cv cv = canvas(xrvault::kTexPack);
      for (size_t i = 3; i < full.rgba.size(); i += 4) full.rgba[i] = full.rgba[i] >= 120 ? full.rgba[i] : 0;
      paste(cv, full, 0, 0, static_cast<float>(cv.W), static_cast<float>(cv.H));
      std::vector<uint8_t> px = bytes(cv, false);
      // (its foil back into the alpha: the wrapper's own code, sampled the same way)
      for (int y = 0; y < cv.H; ++y)
        for (int x = 0; x < cv.W; ++x) {
          const int sx = x * kPackW / cv.W, sy = y * kPackH / cv.H;
          const uint8_t a = big[(static_cast<size_t>(sy) * kPackW + sx) * 4 + 3];
          uint8_t& o = px[(static_cast<size_t>(y) * cv.W + x) * 4 + 3];
          o = a >= 120 ? a : 0;
        }
      t[xrvault::kTexPack] = px;
    }
    {  // the glow pool: a warm radial (its alpha = its strength: added)
      std::vector<uint8_t> px(static_cast<size_t>(xrvault::kTexW[xrvault::kTexGlow]) * xrvault::kTexH[xrvault::kTexGlow] * 4);
      const int w = xrvault::kTexW[xrvault::kTexGlow], h = xrvault::kTexH[xrvault::kTexGlow];
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const float u = (x + 0.5f) / w * 2 - 1, v = (y + 0.5f) / h * 2 - 1, d = std::sqrt(u * u + v * v);
          const float f = d < 1.0f ? std::pow(1.0f - d, 1.6f) : 0.0f;
          uint8_t* o = &px[(static_cast<size_t>(y) * w + x) * 4];
          o[0] = 255, o[1] = 200, o[2] = 110, o[3] = static_cast<uint8_t>(std::lround(f * 255.0f));
        }
      t[xrvault::kTexGlow] = px;
    }
    {  // a shaft of it rising: bright at the foot, soft sides, fading up (a soft volumetric look)
      const int w = xrvault::kTexW[xrvault::kTexBeam], h = xrvault::kTexH[xrvault::kTexBeam];
      std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const float u = (x + 0.5f) / w * 2 - 1, up = 1.0f - (y + 0.5f) / h;  // (0 its foot .. 1 its top)
          const float side = std::pow(std::max(0.0f, 1.0f - u * u), 2.2f), f = side * std::pow(1.0f - up, 1.8f) * (0.55f + 0.45f * std::sin(u * 7.0f + up * 3.0f) * 0.5f + 0.225f);
          uint8_t* o = &px[(static_cast<size_t>(y) * w + x) * 4];
          o[0] = 255, o[1] = 206, o[2] = 120, o[3] = static_cast<uint8_t>(std::lround(clampf(f, 0.0f, 1.0f) * 200.0f));
        }
      t[xrvault::kTexBeam] = px;
    }
    std::lock_guard<std::mutex> lock(m);
    keep = t;
    keep[xrvault::kTexLidIn].clear();
  }
  {  // the lid's inside: satin, gold hairlines, the rosette, the gem, the words, the engraved brass plate with this case's number
    Cv cv = lid_canvas(xrvault::kTexLidIn);
    const float W = ox(cv), H = oy(cv);
    cv.fillp(rr(cv, 0, 0, W, H, 0), vtex::Velvet{f3(0x0F0F12), 3u, 0.35f, 0.3f});
    cv.fillp(ring(cv, 24, 24, W - 24, H - 24, 8, 2.2f), fo, 1.0f, 1.0f);
    cv.fillp(ring(cv, 32, 32, W - 32, H - 32, 6, 1.0f), fo, 0.7f, 1.0f);
    cv.fillp(rosette(cv, rr(cv, 0, 0, W, H, 0), W / 2, H * 0.42f, 8, 18, 10, 1.0f, 2, 200), fo, 0.12f, 1.0f);
    xgem(cv, W / 2, H * 0.27f, 34, fo);
    ktextc(cv, "LANCINI", W / 2, H * 0.27f + 60, 15, kIvory, 'c', 80, 0.35f, 0, 0.8f);
    ktext(cv, "EXQUISITE", W / 2, H * 0.27f + 96, 38, fo, 1.0f, 'c', 30, 1.2f, 0, 1.0f, 1.3f);
    ktextc(cv, "2026-27 STRIKERS  |  THE COLLECTOR'S CASE", W / 2, H * 0.27f + 130, 12, kIvory, 'c', 45, 0.25f, W - 120, 0.75f);
    char nm[48];
    std::snprintf(nm, sizeof(nm), "CASE %04d / 0999", caseNo);
    vtex::plaque(cv, W / 2 - 128, H - 112, W / 2 + 128, H - 52, nm, box ? "SERIAL NUMBERED  |  5 PACKS" : "SERIAL NUMBERED  |  1 PACK", 22);
    t[xrvault::kTexLidIn] = lid_out(cv, xrvault::kTexLidIn);
  }
  return t;
}

// ---------------------------------------------------------------------------------------------- the scene
XrVector3f vault_world(const XrVector3f& local) { return v_add(g_vault.at, q_rot(g_vault.q, local)); }
XrVector3f vault_local(const XrVector3f& world) { return q_rot(q_conj(g_vault.q), v_sub(world, g_vault.at)); }
float vault_lid(uint64_t now) {
  if (g_vault.lidNs == 0) return 0.0f;
  return now >= g_vault.lidNs ? xrvault::lid_angle(secs(now, g_vault.lidNs)) : 0.0f;
}
bool vault_open(uint64_t now) { return g_vault.lidNs != 0 && now >= g_vault.lidNs && secs(now, g_vault.lidNs) >= xrvault::kLidOpenS; }
// r234 (Lance 11-12: the lid's creak sounded like a creaky floorboard / door): the lid opens SILENTLY - no creak (xrvault::creak_pcm is
// never made or played any more); only the latches he flicks click
void vault_sound(int which) {  // 0 a latch (the only sound left)
  if (which != 0) return;
  static std::vector<short> s_latch;
  if (s_latch.empty()) xrvault::latch_pcm(&s_latch);
  PortCardSound(s_latch.data(), static_cast<int>(s_latch.size()), 1.0f);
}
// a latch flicked open (once): its click, a thunk in the controllers; both open: the lid starts after kLidWait
void vault_flick(int i, uint64_t now, const char* how) {
  if (!g_vault.on || g_vault.unlocked || i < 0 || i > 1 || g_vault.latchNs[i] != 0) return;
  g_vault.latchNs[i] = now;
  vault_sound(0);
  buzz(0.9f, 0.07f);
  CLOG("THE VAULT: latch %d flicked open (%s)", i + 1, how);
  if (g_vault.latchNs[0] != 0 && g_vault.latchNs[1] != 0) {
    g_vault.lidNs = now + static_cast<uint64_t>(xrvault::kLidWait * 1e9f);
    CLOG("THE VAULT: both latches open - the lid opens");
  }
}
void vault_flick_next(uint64_t now) {  // (the hub's A: the next latch shut)
  for (int i = 0; i < 2; ++i)
    if (g_vault.latchNs[i] == 0) {
      vault_flick(i, now, "A");
      return;
    }
}
// an EXQUISITE pack opened: its case in front of you (a VAULT case's first pack: shut, latched; its next packs: the case open, the packs left
// in velvet); the pack waits in it (S.phase kVault)
void vault_begin(uint64_t now, unsigned flags, int serial) {
  VaultScene& v = g_vault;
  const bool box = (flags & kExqBox) != 0;
  const int pos = box ? static_cast<int>((flags >> kExqBoxShift) & 7u) % xrvault::dims(true).packs : 0;
  const int keepModel = v.box == box && v.model >= 0 ? v.model : -1;
  v.on = true;
  v.box = box;
  v.unlocked = box && pos > 0;
  v.caseNo = 1 + static_cast<int>(pp_hash(static_cast<uint32_t>(g_salt) ^ static_cast<uint32_t>((serial - pos) * 2654435761u)) % 999u);
  v.firstSlot = pos;
  v.packs = box ? xrvault::dims(true).packs - pos : 1;
  v.mask = 0;
  for (int k = pos; k < (box ? xrvault::dims(true).packs : 1); ++k) v.mask |= 1u << k;
  v.t0 = now;
  v.latchNs[0] = v.latchNs[1] = 0;
  v.lidNs = 0;
  v.liftNs = 0;
  v.glow = 0.0f;
  v.lastLid = 0.0f;
  v.lidMoving = false;
  if (v.unlocked) {  // (the case was opened for its first pack: open now, latches hanging open)
    v.latchNs[0] = v.latchNs[1] = now - 2000000000ull;
    v.lidNs = now - static_cast<uint64_t>((xrvault::kLidOpenS + 0.1f) * 1e9f);
  }
  // in front of you, below your eyes (as if on your lap / a table), its front to you
  const XrVector3f fwd = S.fwd;
  v.at = v_add(v_add(S.anchor, v_mul(fwd, box ? 0.46f : 0.40f)), XrVector3f{0.0f, -0.42f, 0.0f});
  v.yaw = std::atan2(-fwd.x, -fwd.z);
  v.q = q_axis(0, 1, 0, v.yaw);
  v.mesh = xrvault::build(box);
  v.model = keepModel;
  v.pics.reset();
  if (v.model < 0 || !v.unlocked) {  // (its pictures: the lid's plate this case's number)
    auto pics = std::make_shared<Pics>();
    pics->rgba.resize(xrvault::kTexCount);
    pics->left = 1;
    pics->gen = ++g_picGen;
    v.pics = pics;
    const int caseNo = v.caseNo;
    std::thread([pics, box, caseNo]() {
      std::vector<std::vector<uint8_t>> t = vault_textures(box, caseNo);
      std::lock_guard<std::mutex> lock(pics->m);
      pics->rgba = std::move(t);
      for (int i = 0; i < xrvault::kTexCount; ++i) pics->w.push_back(xrvault::kTexW[i]), pics->h.push_back(xrvault::kTexH[i]);
      pics->left = 0;
    }).detach();
    v.model = -1;
  }
  S.phase = kVault;
  S.pack.shown = false;
  for (int h = 0; h < 2; ++h) v.havePrev[h] = false, v.prevGripV[h] = true;  // (a grip already closed doesn't flick)
  CLOG("THE VAULT: %s, case %04d / 0999, %d pack%s in it%s", box ? "THE VAULT (a case of 5)" : "the one-pack case", v.caseNo, v.packs,
       v.packs == 1 ? "" : "s", v.unlocked ? " - open already (its first pack opened it)" : " - latched: flick both latches open");
}
void vault_end(bool fxReady) {
  if (fxReady && g_vault.model >= 0) xrfx::set_model(kSpecialSlot1, xrfx::Model{});
  g_vault.model = -1;
  g_vault.on = false;
  g_vault.pics.reset();
}
// the hands at the latches (game thread, hands()): a grip closing at a latch, or a hand flicking up / out through it
void vault_hands(const bool grip[2], const bool have[2], const XrVector3f hand[2], uint64_t now) {
  VaultScene& v = g_vault;
  if (!v.on || v.unlocked || S.phase != kVault || now < v.t0 + static_cast<uint64_t>(xrvault::kAppear * 1e9f)) {
    for (int h = 0; h < 2; ++h) v.prevGripV[h] = grip[h], v.havePrev[h] = false;
    return;
  }
  const xrvault::Dims d = v.mesh.d;
  for (int h = 0; h < 2; ++h) {
    if (!have[h]) {
      v.havePrev[h] = false;
      v.prevGripV[h] = grip[h];
      continue;
    }
    const XrVector3f l = vault_local(hand[h]);
    for (int i = 0; i < 2; ++i) {
      if (v.latchNs[i] != 0) continue;
      const xrvault::V3 lp = xrvault::latch_point(d, i);
      const float dx = l.x - lp.x, dy = l.y - lp.y, dz = l.z - lp.z, dist = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (grip[h] && !v.prevGripV[h] && dist < 0.08f) vault_flick(i, now, h == 0 ? "left grip" : "right grip");
      if (v.havePrev[h] && dist < 0.05f) {  // (a flick: the hand through it, up or out, quick)
        const XrVector3f pl = vault_local(v.prevHand[h]);
        const float dt = std::max(1e-3f, 1.0f / 72.0f);
        const float up = (l.y - pl.y) / dt, outv = (l.z - pl.z) / dt;
        if (up > 0.35f || outv > 0.45f) vault_flick(i, now, h == 0 ? "left hand flick" : "right hand flick");
      }
    }
    v.prevHand[h] = hand[h];
    v.havePrev[h] = true;
    v.prevGripV[h] = grip[h];
  }
}
// a trigger pulled while the case waits: the latch nearest the hand (within 10 cm), else the one its pointer is on (4 cm)
bool vault_trigger(int h, uint64_t now) {
  VaultScene& v = g_vault;
  if (!v.on || v.unlocked || S.phase != kVault) return false;
  const xrvault::Dims d = v.mesh.d;
  int best = -1;
  float bestD = 0.10f;
  if (S.handHave[h]) {
    const XrVector3f l = vault_local(S.handAt[h]);
    for (int i = 0; i < 2; ++i) {
      if (v.latchNs[i] != 0) continue;
      const xrvault::V3 lp = xrvault::latch_point(d, i);
      const float dd = std::sqrt((l.x - lp.x) * (l.x - lp.x) + (l.y - lp.y) * (l.y - lp.y) + (l.z - lp.z) * (l.z - lp.z));
      if (dd < bestD) bestD = dd, best = i;
    }
  }
  if (best < 0 && S.aimOk[h]) {
    const XrVector3f o = vault_local(S.aimAt[h].position), dir = q_rot(q_conj(v.q), q_rot(S.aimAt[h].orientation, XrVector3f{0, 0, -1}));
    float bestR = 0.04f;
    for (int i = 0; i < 2; ++i) {
      if (v.latchNs[i] != 0) continue;
      const xrvault::V3 lp = xrvault::latch_point(d, i);
      const XrVector3f w{lp.x - o.x, lp.y - o.y, lp.z - o.z};
      const float t = w.x * dir.x + w.y * dir.y + w.z * dir.z;
      if (t < 0.05f || t > 1.5f) continue;
      const XrVector3f c = v_sub(w, v_mul(dir, t));
      const float r = v_len(c);
      if (r < bestR) bestR = r, best = i;
    }
  }
  if (best >= 0) vault_flick(best, now, h == 0 ? "left trigger" : "right trigger");
  return true;  // (the trigger is the case's while it waits)
}
// every frame of the scene: the case (fading in, its latches, its lid, its light), the pack rising out of the velvet into your hands
void vault_frame(xrfx::Scene& scene, uint64_t now, bool fxReady) {
  VaultScene& v = g_vault;
  if (!v.on) return;
  const xrvault::Dims d = v.mesh.d;
  if (fxReady && v.model < 0 && v.pics) {
    bool done;
    {
      std::lock_guard<std::mutex> lock(v.pics->m);
      done = v.pics->left == 0;
    }
    if (done) {
      xrfx::Model m;
      {
        std::lock_guard<std::mutex> lock(v.pics->m);
        for (int i = 0; i < xrvault::kTexCount; ++i) {
          m.texRgba.push_back(v.pics->rgba[static_cast<size_t>(i)]);
          m.texW.push_back(static_cast<uint32_t>(xrvault::kTexW[i]));
          m.texH.push_back(static_cast<uint32_t>(xrvault::kTexH[i]));
        }
      }
      m.uv = v.mesh.uv;
      m.indices = v.mesh.idx;
      for (const xrvault::Part& p : v.mesh.parts) {
        xrfx::ModelPart mp;
        mp.tex = p.tex, mp.soft = p.soft, mp.firstIndex = p.first, mp.indexCount = p.count;
        m.parts.push_back(mp);
      }
      m.vertexCount = v.mesh.verts();
      xrfx::set_model(kSpecialSlot1, std::move(m));
      v.model = v.pics->gen;
      CLOG("THE VAULT: its pictures up%s", xrfx::model_up(kSpecialSlot1) ? "" : " - failed");
    }
  }
  // the lid, its light (r234: no creak - it opens silently; the soft rumble in the hands as it starts stays)
  const float lid = vault_lid(now);
  if (lid > 0.0f && v.lastLid == 0.0f && !v.unlocked) buzz(0.35f, 0.25f);
  v.lastLid = lid;
  const float sinceOpen = v.lidNs != 0 && now > v.lidNs ? secs(now, v.lidNs) - xrvault::kLidOpenS : 0.0f;
  v.glow = xrvault::glow_of(lid, std::max(0.0f, sinceOpen));
  if (S.phase != kVault) v.glow *= 0.75f;  // (the pack out: its light softer, still there)
  // the pack rising out of its bed into your hands (S.pack: from the velvet, flat, the case's size, to where a pack comes up)
  if (S.phase == kVault && vault_open(now) && v.liftNs == 0 && secs(now, v.lidNs) >= xrvault::kLidOpenS + xrvault::kPackWait) {
    v.liftNs = now;
    v.mask &= ~(1u << v.firstSlot);
    const xrvault::V3 c = xrvault::pack_center(d, v.firstSlot);
    v.liftAt = vault_world(XrVector3f{c.x, c.y + 0.002f, c.z});
    v.liftFrom = q_mul(v.q, q_axis(1, 0, 0, -1.5707963f));  // (its face up, its top to the back of the case)
    v.liftScale0 = d.packW / kPackWm;
    S.pack.shown = true;
    S.pack.free = true;
    S.pack.posed = true;
    S.pack.pose = v.liftFrom;
    S.pack.pos = v.liftAt;
    S.pack.scale = v.liftScale0;
    card_sound(1);
    CLOG("THE VAULT: the pack rises out of its velvet");
  }
  if (S.phase == kVault && v.liftNs != 0) {
    const float u = clampf(secs(now, v.liftNs) / xrvault::kPackLift, 0.0f, 1.0f), e = smooth(u);
    Sheet probe = S.pack;
    probe.posed = false;
    probe.fixedBase = false;
    probe.pos = S.pack.tPos;
    probe.relYaw = probe.roll = 0.0f;
    const XrQuaternionf to = sheet_base(probe);
    const XrVector3f up = v_add(v.liftAt, XrVector3f{0.0f, 0.10f, 0.0f});
    const XrVector3f a = v_lerp(v.liftAt, up, smooth(clampf(u * 2.5f, 0.0f, 1.0f)));
    S.pack.pos = v_lerp(a, S.pack.tPos, smooth(clampf((u - 0.25f) / 0.75f, 0.0f, 1.0f)));
    S.pack.pose = q_nlerp(v.liftFrom, to, e);
    S.pack.scale = v.liftScale0 + (1.0f - v.liftScale0) * e;
    S.pack.lastPos = S.pack.pos;
    if (u >= 1.0f) {
      S.phase = kWaiting;
      S.phaseNs = now;
      S.pack.posed = false;
      S.pack.free = false;
      S.pack.scale = 1.0f;
      buzz(0.35f, 0.08f);
      CLOG("THE VAULT: the pack is in your hands - rip it");
    }
  }
  // its pose, drawn
  if (!fxReady || v.model < 0 || !xrfx::model_up(kSpecialSlot1)) return;
  xrvault::Pose p;
  p.lid = lid;
  for (int i = 0; i < 2; ++i) p.latch[i] = v.latchNs[i] != 0 ? xrvault::latch_angle(secs(now, v.latchNs[i])) : 0.0f;
  p.packs = v.mask;
  p.glow = v.glow;
  xrvault::pose(v.mesh, p, &v.verts);
  const float appear = smooth(secs(now, v.t0) / xrvault::kAppear);
  xrfx::Scene::ModelDraw& md = scene.models[kSpecialSlot1];
  md.on = true;
  md.posNrm = v.verts.data();
  md.local = true;
  const float c = std::cos(v.yaw), s = std::sin(v.yaw), sc = 0.85f + 0.15f * appear;
  const float m[16] = {c * sc, 0, -s * sc, 0, 0, sc, 0, 0, s * sc, 0, c * sc, 0, v.at.x, v.at.y + 0.04f * (1.0f - appear), v.at.z, 1};
  std::copy(m, m + 16, md.toWorld);
  md.white = 0.0f;
  md.alpha = appear;
  md.glow = v.glow;
  md.under = -11.0f;  // THE VAULT (xr_fx_model.frag vault_main)
  md.glowRgb = 0xFFC870;
  md.flameFrame = -1;
  md.shine = 0.0f;
  md.frame = -1;
  md.kaleido = 0;
  md.reveal = -1.0f;
  md.dim = 0.0f;
}

// ---------------------------------------------------------------------------------------------- the gallery: its mood, its music
// quiet lounge (made here: no download) - 4 phrases of 2 bars at 76 bpm (Dm9 G13 | Cmaj9 Am9 | Fmaj7#11 E7b9 | Dm9 G13sus): a soft electric
// piano (two sine partials + a bell's touch), an upright bass, brushes; laid end to end while the gallery's up (PortCardSoundSfx: the
// SOUND > EFFECTS volume), each phrase at the mood's level
struct Lounge {
  std::vector<short> pcm[4];
  float len = 0.0f;
};
// r231: the phrases come from xr_vault.h's lounge_pcm - tones only (r230's brushes were white noise: a swish on 2 + 4 and a tick on every
// beat), one fixed level for all four
Lounge make_lounge() {
  Lounge L;
  L.len = xrvault::lounge_len();
  for (int ph = 0; ph < 4; ++ph) xrvault::lounge_pcm(ph, &L.pcm[ph]);
  return L;
}
const Lounge& lounge() {  // (made once, the first time the gallery's up: a function-local static - thread-safe, ~0.4 s, ~1.6 MB)
  static const Lounge L = make_lounge();
  return L;
}
std::atomic<bool> g_loungeReady{false};  // (made on a thread at the first EXQUISITE opening: never on the frame thread)
void lounge_prewarm() {
  static std::atomic<bool> asked{false};
  if (asked.exchange(true)) return;
#ifdef HARNESS_REAL_CARDS
  lounge();
  g_loungeReady = true;
#else
  std::thread([] {
    lounge();
    g_loungeReady = true;
  }).detach();
#endif
}
uint64_t g_loungeNext = 0;
int g_loungePh = 0;
// the mood's level a frame (eased in 1.5 s / out 2.5 s) + its music; told to the menus' music and the crowd (PortCardGallerySet)
void gallery_frame(uint64_t now, float dt) {
  bool want = exq_scene() && S.mode == kPackUp;
  if (want && S.n > 0 && S.revealed >= S.n && g_lastRevealNs != 0 && now > g_lastRevealNs + 6000000000ull && g_pullCard < 0) want = false;
  float gal = g_gallery.load();
  gal = want ? std::min(1.0f, gal + dt / 1.5f) : std::max(0.0f, gal - dt / 2.5f);
  g_gallery = gal;
  PortCardGallerySet(gal);
  if (want) lounge_prewarm();
  if (gal > 0.05f && want && g_loungeReady.load()) {
    const Lounge& L = lounge();
    if (g_loungeNext == 0 || now + 20000000ull >= g_loungeNext) {
      const std::vector<short>& p = L.pcm[g_loungePh % 4];
      PortCardSoundSfx(p.data(), static_cast<int>(p.size()), 0.30f * std::max(0.4f, gal));
      g_loungeNext = (g_loungeNext == 0 || now > g_loungeNext + 500000000ull ? now : g_loungeNext) + static_cast<uint64_t>(L.len * 1e9f);
      ++g_loungePh;
    }
  } else if (!want) {
    g_loungeNext = 0;
    g_loungePh = 0;
  }
}

// THE VAULT for the CARD SHOP (its box's picture, 650 x 550): the case open, its latches hanging open, the five packs in velvet, its light -
// this mesh + these pictures through xrvault's own small rasteriser (the same case the headset draws). Thread (shop_pic's)
std::vector<uint8_t> vault_shop_px(const Img& pack, int* w, int* h) {
  (void)pack;
  const xrvault::Mesh m = xrvault::build(true);
  const std::vector<std::vector<uint8_t>> t = vault_textures(true, 127);
  xrvault::TexImg ti[xrvault::kTexCount];
  for (int i = 0; i < xrvault::kTexCount; ++i) ti[i].rgba = t[static_cast<size_t>(i)].data(), ti[i].w = xrvault::kTexW[i], ti[i].h = xrvault::kTexH[i];
  xrvault::Pose p;
  p.lid = xrvault::kLidOpenA;
  p.latch[0] = p.latch[1] = xrvault::kLatchOpen;
  p.packs = 0x1F;
  p.glow = 0.85f;
  xrvault::Cam cam;
  cam.eye = xrvault::v3(0.22f, 0.42f, 0.52f);
  cam.at = xrvault::v3(0.0f, 0.09f, 0.0f);
  cam.fovY = 0.78f;
  *w = 650, *h = 550;
  std::vector<uint8_t> out;
  xrvault::render(m, p, ti, cam, *w, *h, &out);
  return out;
}