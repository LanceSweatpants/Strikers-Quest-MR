// strikers-quest r163 (Lance: "heavyyyyy on every special skin is moving and alive"): the 3D binder's living skins - what
// moves on and round it beyond its pictures: glow halos hugging its emissive parts, a light pool under it, particles
// (embers, slime drips, mist, shooting stars ...), its fur swaying and parting at your hand. xr_cards.cpp room_binder
// calls frame() every display frame while the binder is shown (after laying out its vertices, before they are drawn);
// the per-pixel motion (flow, sheen, twinkle, sprites) is xr_fx_model.frag's binder mode (ModelDraw::kaleido = the skin's
// index + 1). Game thread. Include after the Vulkan/OpenXR platform defines (as xr_fx.h).
#pragma once

#include "xr_fx.h"

#include <cstdint>

namespace xrbfx {

// The effects-layer model slot the binder's halos / light pool use (xr_fx.h kModelSlots; xr_cards.h kBinderSlot's
// neighbours: the binder, its pages, this)
constexpr int kBinderFxSlot = xrfx::kModelSlots - 10;

struct BinderView {
  const char* skin = "";       // the skin's id ("molten_lava" ...)
  int skinIndex = 0;           // its place in the catalogue (xr_cards.cpp kSkins)
  float toWorld[16] = {};      // the binder's own frame -> LOCAL space (column-major: its turn x its size, its place)
  float scale = 1.0f;          // its size (1 = 0.31 m tall)
  float open = 0.0f;           // 0 shut .. 1 flat open (xr_cards.cpp: front | spine | back)
  float W = 0.275f, H = 0.31f, T = 0.055f, ct = 0.0045f;  // binder.mesh's sizes (its frame, at size 1)
  bool handValid[2] = {false, false};
  XrVector3f hand[2] = {};     // the hands (LOCAL)
  XrVector3f vel{0.0f, 0.0f, 0.0f};  // its own velocity (LOCAL, m/s: fur / slime react to it)
  float* verts = nullptr;      // its vertices as about to be drawn (6 floats: position, normal; its own frame) - may be
  int vertCount = 0;           //   changed (fur shells: normals 4..5 long; xr_cards.cpp bd_layout)
  const float* rest = nullptr; // the same, at rest (this frame's layout before frame())
  float floorY = -1.6f;        // the room's floor (LOCAL)
  // r163 quick fixes: the Tabletop field (xrfx::Scene tablePos / tableYaw / tableScale: 41.2 x 24.2 game units x scale,
  // centred at tablePos, turned by yaw about +Y, its top at tablePos.y) - the light pool lands on it when it's under the
  // binder; and for fur skins each vertex's source (the base vertex a shell vertex was copied from, -1 not a shell) and
  // its shell height (0..1, -1 not a shell). Non-fur skins: verts null, vertCount 0 (their poses are baked: nothing moves
  // per frame on the CPU - wobble / breathing in xr_fx_model.vert)
  bool tableValid = false;
  XrVector3f tablePos{0.0f, 0.0f, 0.0f};
  float tableYaw = 0.0f, tableScale = 0.03f;
  const int* shellSrc = nullptr;
  const float* shellH = nullptr;
  float dt = 0.0f;
  uint64_t now = 0;
};
// Every frame it shows (fxReady: the effects layer up). Draws into kBinderFxSlot / the effects' particles.
void frame(xrfx::Scene& scene, BinderView& v, bool fxReady);
// Not shown any more (put away, a preview over): its pieces let go.
void stop(bool fxReady);

}  // namespace xrbfx
