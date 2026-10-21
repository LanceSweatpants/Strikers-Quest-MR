#ifndef DOLPHIN_GXAURORA_H
#define DOLPHIN_GXAURORA_H

#include <dolphin/types.h>

#if __cplusplus
extern "C" {
#endif

//
// Subcommands for GX_AURORA.
//

/**
 * Sets the actual render viewport in native framebuffer coordinates.
 * Must be followed by six f32 values: left, top, width, height, nearz, farz.
 */
#define GX_AURORA_LOAD_VIEWPORT_RENDER 0x0001

/**
 * Sets the actual render scissor in native framebuffer coordinates.
 * Must be followed by four u32 values: left, top, width, height.
 */
#define GX_AURORA_LOAD_SCISSOR_RENDER 0x0002

/**
 * Loads a full 4x4 projection matrix, bypassing GXSetProjection's 6-parameter
 * hardware encoding. Must be followed by sixteen f32 values in row-major order.
 */
#define GX_AURORA_LOAD_PROJECTION_FULL 0x0003

/**
 * Aurora equivalent of CP_REG_ARRAYBASE_ID: sets the base address and size of a vertex array.
 * This command must be followed by a 64-bit memory address, 32-bit size, and 1-byte little-endian flag.
 * The index of the vertex array is given by the lowest 4 bits of the command ID,
 * e.g. writing GX_AURORA_LOAD_ARRAYBASE + 5 will set the vertex array for the sixth vertex attribute.
 * To set strides, use the normal CP_REG_ARRAYSTRIDE_ID register.
 */
#define GX_AURORA_LOAD_ARRAYBASE 0x0010

/**
 * Pushes a debug group to the backend graphics API. These may show in debugging tools such as RenderDoc.
 * Must be followed by a u16 string length and that many UTF-8 characters (no null terminator required).
 * It is considered an error to have unpopped debug groups at the end of the frame. They will be automatically cleared.
 */
#define GX_AURORA_DEBUG_GROUP_PUSH 0x0020

/**
 * Pops a previously pushed debug group.
 * Followed by nothing.
 */
#define GX_AURORA_DEBUG_GROUP_POP 0x0021

/**
 * Sends a debug marker to the backend graphics API.
 * Must be followed by a u16 string length and that many UTF-8 characters (no null terminator required).
 */
#define GX_AURORA_DEBUG_MARKER_INSERT 0x0022

#define GX_AURORA_LOAD_TEXOBJ 0x0030

#define GX_AURORA_LOAD_TLUT 0x0031

#define GX_AURORA_DESTROY_TEXOBJ 0x0032

#define GX_AURORA_DESTROY_TLUT 0x0033

#define GX_AURORA_DESTROY_COPY_TEX 0x0034

#define GX_AURORA_LOAD_COPY_SRC 0x0035

#define GX_AURORA_LOAD_COPY_DST 0x0036

#define GX_AURORA_LOAD_COPY_DEST 0x0037

#define GX_AURORA_REQUEST_DEPTH_SNAPSHOT 0x0038

#define GX_AURORA_BEGIN_OFFSCREEN 0x0039

#define GX_AURORA_END_OFFSCREEN 0x003A

/**
 * Draw primitives with the vertex count derived from a byte length, as written by
 * GXBegin(prim, fmt, GX_AUTO). Must be followed by a u8 draw opcode (vtxfmt|prim),
 * a u32 vertex data byte length, then that many bytes of vertex data. The byte length
 * must be a whole multiple of the current vertex size or zero (no draw).
 */
#define GX_AURORA_DRAW_SIZED 0x0040

/**
 * Draw pre-merged triangles with a prebuilt index buffer, as written by the display
 * list optimizer (aurora::gx::dl::optimize). Must be followed by a u8 draw opcode
 * (vtxfmt | GX_TRIANGLES), a u16 vertex count, a u32 index count, that many u16
 * indices, then vertex count * vertex size bytes of packed vertex data. Index data
 * is always host-endian regardless of stream endianness.
 */
#define GX_AURORA_DRAW_INDEXED 0x0041

/**
 * strikers-quest: marks the draws that follow as worth decoding once per frame into plain floats
 * (aurora_xr_set_vertex_decode mode 1). Must be followed by a u8: 1 = begin, 0 = end.
 */
#define GX_AURORA_DECODE_SCOPE 0x0042

/**
 * strikers-quest r43: single-pass stereo for the headset's eye passes. Followed by a u8 (1 = on,
 * 0 = off) and, when on, per eye (left, right): 12 f32 (a 3x4 row-major eye-space correction from
 * the send's centre view to that eye's view) and 4 f32 (that eye's clip-space x and y as
 * a * clip + b * w of the send's projection). While on, inside an offscreen pass, the pass is
 * recorded once and encoded twice into the halves of a double-wide target.
 */
#define GX_AURORA_SET_STEREO 0x0043

/**
 * strikers-quest r43: open (u8 1, then u8 target slot, u8 sample count, u8 coverage) or close
 * (u8 0) the headset's eye pass in-band. The target is one registered with
 * aurora::gfx::register_xr_eye_target; while open, GX viewports are in native units
 * (set_offscreen_uses_native_logical_size) and coverage is the pass's alpha (aurora_gx_set_xr_coverage).
 */
#define GX_AURORA_XR_EYE_PASS 0x0044

/**
 * strikers-quest r44e: switch the open eye pass's coverage mode (u8: 0 off, 1 over passthrough, 2 over
 * a drawn layer) for the draws that follow, in-band (aurora_gx_set_xr_coverage_mode).
 */
#define GX_AURORA_XR_COVERAGE 0x0045

/**
 * strikers-quest r59: cut the stereo draws that follow to four planes. Followed by a u8 (1 = on,
 * 0 = off) and, when on, 16 f32: four planes (a, b, c, d) in the send's centre-view eye space; a
 * fragment is kept where a*x + b*y + c*z + d >= 0 for all four.
 */
#define GX_AURORA_SET_XR_CLIP 0x0046

#define GX2_SET_POLYGON_OFFSET 0x1000


/*
 * Debug marker stuff
 */

/**
 * Pushes a debug group to the backend graphics API. These may show in debugging tools such as RenderDoc.
 * It is considered an error to have unpopped debug groups at the end of the frame. They will be automatically cleared.
 */
void GXPushDebugGroup(const char* label);

/**
 * Pop a debug group previously pushed via GXPushDebugGroup().
 */
void GXPopDebugGroup();

/**
 * Sends a debug marker to the backend graphics API. These may show in debugging tools such as RenderDoc.
 */
void GXInsertDebugMarker(const char* label);

typedef enum _AuroraViewportPolicy {
  AURORA_VIEWPORT_FIT = 0,     // Preserve logical aspect in the content framebuffer
  AURORA_VIEWPORT_STRETCH = 1, // Match content framebuffer aspect to the native surface
  AURORA_VIEWPORT_NATIVE = 2,  // Use active framebuffer pixels directly
} AuroraViewportPolicy;

/**
 * Configures content framebuffer sizing and how GXSetViewport/GXSetScissor parameters are applied to rendering.
 * When AURORA_VIEWPORT_NATIVE is used, GXSetTexCopySrc/GXSetTexCopyDst will use native framebuffer resolution.
 */
void AuroraSetViewportPolicy(AuroraViewportPolicy policy);

/**
 * Retrieves the current content framebuffer size.
 */
void AuroraGetRenderSize(u32* width, u32* height);

/**
 * Flush pending GX state and wait for FIFO processing without signaling a draw-done callback.
 */
void AuroraGXSync(void);

/**
 * Sets the actual render viewport in native framebuffer coordinates.
 * Overrides the automatically scaled values set by the logical GXSetViewport.
 */
void GXSetViewportRender(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz);

/**
 * Sets the actual render scissor in native framebuffer coordinates.
 * Overrides the automatically scaled values set by the logical GXSetScissor.
 */
void GXSetScissorRender(u32 left, u32 top, u32 wd, u32 ht);

void GX2SetPolygonOffset(f32 mFrontOffset, f32 mFrontScale, f32 mBackOffset, f32 mBackScale, f32 mClamp);

/**
 * Load an arbitrary 4x4 projection matrix, avoiding the 6-parameter hardware encoding.
 */
void GXSetProjectionFull(const void* mtx);

/**
 * Create an offscreen framebuffer and switch rendering to it.
 * All subsequent GX rendering will target this framebuffer until GXRestoreFrameBuffer() is called.
 * Use GXCopyTex to resolve the offscreen content into a texture.
 */
void GXCreateFrameBuffer(u32 width, u32 height);

/**
 * Restore rendering to the main EFB framebuffer.
 * Must be called after GXCreateFrameBuffer() to resume normal rendering.
 */
void GXRestoreFrameBuffer(void);

/**
 * strikers-quest: begin (1) or end (0) a run of draws that Aurora may decode once per frame and
 * reuse across sends (GX_AURORA_DECODE_SCOPE). In-band, so it applies exactly to the draws between.
 */
void GXAuroraSetDecodeScope(u8 on);

/**
 * strikers-quest r43: single-pass stereo on (GX_AURORA_SET_STEREO) for the draws that follow, with
 * each eye's 3x4 correction (row-major, 12 floats) and clip-space x/y remap (ax, bx, ay, by), or off
 * (the pointers are then ignored). In-band, so it applies exactly to the draws between.
 */
void GXAuroraSetStereo(u8 on, const f32* tLeft, const f32* tRight, const f32* clipLeft, const f32* clipRight);

/**
 * strikers-quest r43: open (begin = 1) or close (begin = 0) the headset's eye pass in the GX stream
 * (GX_AURORA_XR_EYE_PASS). slot / samples / coverage are read only when opening.
 */
void GXAuroraXrEyePass(u8 begin, u8 slot, u8 samples, u8 coverage);

/**
 * strikers-quest r44e: the coverage mode for the draws that follow inside the open eye pass
 * (GX_AURORA_XR_COVERAGE); the pass's end sets it back to 0.
 */
void GXAuroraXrCoverage(u8 mode);

/**
 * strikers-quest r59: clip the stereo draws that follow to four eye-space planes (16 floats), or
 * stop (on = 0; planes ignored) (GX_AURORA_SET_XR_CLIP). In-band.
 */
void GXAuroraSetXrClip(u8 on, const f32* planes16);

/**
 * strikers-quest: 0 = off, 1 = draws inside a decode scope, 2 = every eligible draw. Read by the
 * FIFO thread at each draw. Draws are decoded to floats on the CPU once per game frame (content
 * keyed) and read by a vertex-shader variant whose attribute loads are plain vec4 loads.
 */
void aurora_xr_set_vertex_decode(int mode);

/**
 * strikers-quest: counters since the last call: [0] draws decoded (cache misses), [1] draws reused
 * (cache hits), [2] vertices decoded, [3] draws that fell back (pipeline not ready yet), [4] draws
 * not eligible, [5] decode CPU time in microseconds.
 */
void aurora_xr_take_decode_stats(u32 out[6]);

/**
 * strikers-quest: decoded bytes pushed into the vertex pool so far (monotonic), so the headset's
 * pool guard can count them once per frame rather than once per send.
 */
unsigned long long aurora_xr_decoded_bytes(void);

/** strikers-quest: decoded bytes the last completed frame pushed into the vertex pool. */
unsigned long long aurora_xr_decoded_bytes_last_frame(void);

#if __cplusplus
}
#endif

#endif
