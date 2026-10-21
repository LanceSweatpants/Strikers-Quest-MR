#include "dolphin/gx/GXAurora.h"

#include <limits>

#include "__gx.h"
#include "gx.hpp"
#include "../../window.hpp"

#include "../../gx/fifo.hpp"

static void GXWriteString(const char* label) {
  auto length = strlen(label);

  if (length > std::numeric_limits<u16>::max()) {
    Log.warn("Debug marker size over u16 max, truncating");
    length = std::numeric_limits<u16>::max();
  }

  GX_WRITE_U16(length);
  GX_WRITE_DATA(label, length);
}

void GXPushDebugGroup(const char* label) {
  GX_WRITE_AURORA(GX_AURORA_DEBUG_GROUP_PUSH);
  GXWriteString(label);
}

void GXPopDebugGroup() { GX_WRITE_AURORA(GX_AURORA_DEBUG_GROUP_POP); }

void GXInsertDebugMarker(const char* label) {
  GX_WRITE_AURORA(GX_AURORA_DEBUG_MARKER_INSERT);
  GXWriteString(label);
}

void AuroraSetViewportPolicy(AuroraViewportPolicy policy) {
  aurora::gx::set_viewport_policy(policy);
}

void AuroraGetRenderSize(u32* width, u32* height) {
  const auto windowSize = aurora::window::get_window_size();
  if (width != nullptr) {
    *width = windowSize.fb_width;
  }
  if (height != nullptr) {
    *height = windowSize.fb_height;
  }
}

void AuroraGXSync() {
  GXFlush();
  aurora::gx::fifo::drain();
}

void GXSetViewportRender(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {
  GX_WRITE_AURORA(GX_AURORA_LOAD_VIEWPORT_RENDER);
  GX_WRITE_F32(left);
  GX_WRITE_F32(top);
  GX_WRITE_F32(wd);
  GX_WRITE_F32(ht);
  GX_WRITE_F32(nearz);
  GX_WRITE_F32(farz);
}

void GXSetScissorRender(u32 left, u32 top, u32 wd, u32 ht) {
  GX_WRITE_AURORA(GX_AURORA_LOAD_SCISSOR_RENDER);
  GX_WRITE_U32(left);
  GX_WRITE_U32(top);
  GX_WRITE_U32(wd);
  GX_WRITE_U32(ht);
}

void GXSetProjectionFull(const void* mtx) {
  const f32* values = reinterpret_cast<const f32*>(mtx);
  GX_WRITE_AURORA(GX_AURORA_LOAD_PROJECTION_FULL);
  for (int i = 0; i < 16; ++i) {
    GX_WRITE_F32(values[i]);
  }
}

void GX2SetPolygonOffset(f32 mFrontOffset, f32 mFrontScale, f32 mBackOffset, f32 mBackScale, f32 mClamp) {
  GX_WRITE_AURORA(GX2_SET_POLYGON_OFFSET);
  GX_WRITE_F32(mFrontOffset);
  GX_WRITE_F32(mFrontScale);
  GX_WRITE_F32(mBackOffset);
  GX_WRITE_F32(mBackScale);
  GX_WRITE_F32(mClamp);
}

void GXCreateFrameBuffer(u32 width, u32 height) {
  GX_WRITE_AURORA(GX_AURORA_BEGIN_OFFSCREEN);
  GX_WRITE_U32(width);
  GX_WRITE_U32(height);
  aurora::gx::fifo::publish();
}

void GXRestoreFrameBuffer() {
  GX_WRITE_AURORA(GX_AURORA_END_OFFSCREEN);
  aurora::gx::fifo::publish();
}

void GXAuroraSetDecodeScope(u8 on) {
  GX_WRITE_AURORA(GX_AURORA_DECODE_SCOPE);
  GX_WRITE_U8(on != 0 ? 1 : 0);
}

void GXAuroraSetStereo(u8 on, const f32* tLeft, const f32* tRight, const f32* clipLeft, const f32* clipRight) {
  const bool enable = on != 0 && tLeft != nullptr && tRight != nullptr && clipLeft != nullptr && clipRight != nullptr;
  GX_WRITE_AURORA(GX_AURORA_SET_STEREO);
  GX_WRITE_U8(enable ? 1 : 0);
  if (!enable) {
    return;
  }
  const f32* const t[2] = {tLeft, tRight};
  const f32* const clip[2] = {clipLeft, clipRight};
  for (int eye = 0; eye < 2; ++eye) {
    for (int i = 0; i < 12; ++i) {
      GX_WRITE_F32(t[eye][i]);
    }
  }
  for (int eye = 0; eye < 2; ++eye) {
    for (int i = 0; i < 4; ++i) {
      GX_WRITE_F32(clip[eye][i]);
    }
  }
}

void GXAuroraXrCoverage(u8 mode) {
  GX_WRITE_AURORA(GX_AURORA_XR_COVERAGE);
  GX_WRITE_U8(mode);
}

void GXAuroraSetXrClip(u8 on, const f32* planes16) {
  const bool enable = on != 0 && planes16 != nullptr;
  GX_WRITE_AURORA(GX_AURORA_SET_XR_CLIP);
  GX_WRITE_U8(enable ? 1 : 0);
  if (!enable) {
    return;
  }
  for (int i = 0; i < 16; ++i) {
    GX_WRITE_F32(planes16[i]);
  }
}

void GXAuroraXrEyePass(u8 begin, u8 slot, u8 samples, u8 coverage) {
  GX_WRITE_AURORA(GX_AURORA_XR_EYE_PASS);
  GX_WRITE_U8(begin != 0 ? 1 : 0);
  if (begin != 0) {
    GX_WRITE_U8(slot);
    GX_WRITE_U8(samples);
    GX_WRITE_U8(coverage);
  }
  // The FIFO thread switches passes when it gets here: let it see this now.
  aurora::gx::fifo::publish();
}
