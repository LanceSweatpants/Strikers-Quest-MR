#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <webgpu/webgpu_cpp.h>

namespace aurora::gfx {

inline constexpr size_t InlineDrawPayloadSize = 128;
inline constexpr size_t MaxColorAttachments = 8;
inline constexpr uint32_t SceneColorAttachmentIndex = 0;

enum class ColorAttachmentSemantic : uint8_t {
  SceneColor,
  Normal,
  Auxiliary,
};

struct ColorAttachmentLayout {
  ColorAttachmentSemantic semantic = ColorAttachmentSemantic::Auxiliary;
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  uint32_t width = 0;
  uint32_t height = 0;
};

struct RenderTargetLayout {
  uint64_t key = 0;
  uint32_t colorAttachmentCount = 0;
  std::array<ColorAttachmentLayout, MaxColorAttachments> colorAttachments{};
  wgpu::TextureFormat depthStencilFormat = wgpu::TextureFormat::Undefined;
  uint32_t sampleCount = 1;
};

/// Generational handle: 0 is never valid, and IDs are not reused after unregister_draw_type.
using DrawTypeId = uint64_t;
inline constexpr DrawTypeId InvalidDrawType = 0;

struct Range {
  uint32_t offset = 0;
  uint32_t size = 0;

  bool operator==(const Range& rhs) const { return offset == rhs.offset && size == rhs.size; }
  bool operator!=(const Range& rhs) const { return !(*this == rhs); }
};

struct DrawContext {
  wgpu::Device device;
  wgpu::Queue queue;
  wgpu::Buffer vertexBuffer;
  wgpu::Buffer indexBuffer;
  wgpu::Buffer uniformBuffer;
  wgpu::Buffer storageBuffer;
  RenderTargetLayout layout;
};

/// Invoked on the render worker thread while replaying the pass the draw was
/// recorded into. The encoder's pipeline/bind-group/viewport/scissor state is
/// restored after the callback returns. Handles in the context are borrowed and
/// valid only for the duration of the call. targetLayout and target dimensions
/// describe the containing pass.
using DrawCallback = void (*)(const DrawContext& ctx, const wgpu::RenderPassEncoder& pass, const void* payload,
                              size_t payloadSize, void* userdata);

struct DrawTypeDescriptor {
  const char* label = nullptr;
  DrawCallback draw = nullptr;
  void* userdata = nullptr;
};

wgpu::Device device() noexcept;
wgpu::Queue queue() noexcept;
wgpu::TextureFormat color_format() noexcept;
wgpu::TextureFormat depth_format() noexcept;
uint32_t sample_count() noexcept;
RenderTargetLayout scene_render_target_layout() noexcept;
bool uses_reversed_z() noexcept;

DrawTypeId register_draw_type(const DrawTypeDescriptor& desc);
void unregister_draw_type(DrawTypeId type) noexcept;
/// Records an inline custom draw into the currently open render pass at the
/// current position in the command stream. Payload (<= InlineDrawPayloadSize)
/// is copied. Returns false (with a warning) outside an active render pass.
bool push_custom_draw(DrawTypeId type, const void* payload, size_t payloadSize);

/// Generational handle: 0 is never valid, and IDs are not reused after unregister_encoder_task_type.
using EncoderTaskId = uint64_t;
inline constexpr EncoderTaskId InvalidEncoderTask = 0;

struct EncoderTaskContext {
  wgpu::Device device;
  wgpu::Queue queue;
  wgpu::Buffer vertexBuffer;
  wgpu::Buffer indexBuffer;
  wgpu::Buffer uniformBuffer;
  wgpu::Buffer storageBuffer;
};

struct EncoderTaskCompletionContext {
  wgpu::Device device;
  wgpu::Queue queue;
};

/// Invoked on the render worker thread with the frame's command encoder,
/// positioned between two render passes. The callback may begin/end compute
/// passes and record copies on the encoder; it must leave no pass open when it
/// returns and must not Finish the encoder. Handles in the context are borrowed
/// and valid only for the duration of the call. Data appended to the streaming
/// buffers before the task was pushed is GPU-visible inside it.
using EncoderTaskCallback = void (*)(const EncoderTaskContext& ctx, const wgpu::CommandEncoder& cmd,
                                     const void* payload, size_t payloadSize, void* userdata);

/// Invoked on the render worker after the frame command buffer containing the
/// encoder task has been submitted. This can be used to e.g. call present on
/// an externally managed surface rendered to by the task.
using EncoderTaskCompletionCallback = void (*)(const EncoderTaskCompletionContext& ctx, const void* payload,
                                               size_t payloadSize, void* userdata);

struct EncoderTaskDescriptor {
  const char* label = nullptr;
  EncoderTaskCallback callback = nullptr;
  void* userdata = nullptr;
  EncoderTaskCompletionCallback afterSubmit = nullptr;
};

EncoderTaskId register_encoder_task_type(const EncoderTaskDescriptor& desc);
void unregister_encoder_task_type(EncoderTaskId type) noexcept;
/// Seals the current EFB pass, records an encoder task to execute on the frame
/// encoder at this point, and resumes rendering on a pass that loads the
/// existing contents. Payload semantics match push_custom_draw. Returns false
/// (with a warning) outside an active render pass or while an offscreen pass
/// is open.
bool push_encoder_task(EncoderTaskId type, const void* payload, size_t payloadSize);

/// Append transient data to the shared per-frame streaming buffers. Returned
/// ranges are valid for the current frame only. Returns an empty Range (with a
/// warning) outside an active recording frame.
Range push_verts(const uint8_t* data, size_t length, size_t alignment);
Range push_indices(const uint8_t* data, size_t length, size_t alignment);
Range push_uniform(const uint8_t* data, size_t length);
Range push_storage(const uint8_t* data, size_t length);

struct ResolveDesc {
  bool color = true;
  bool depth = false;
};

struct ResolvedTargets {
  wgpu::TextureView color; // single-sample snapshot; null if not requested
  wgpu::TextureView depth; // single-sample R32Float depth snapshot; null if not requested
  wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined;
  uint32_t width = 0;
  uint32_t height = 0;
};

/// Snapshots the current pass targets into pooled textures (valid for the
/// current frame), then: on the EFB, continues rendering on a fresh EFB pass
/// (GXCopyTex semantics); in an offscreen pass created by create_pass, ends it
/// and restores the suspended EFB pass (GXRestoreFrameBuffer semantics).
/// Requesting neither color nor depth is a plain pass break (or offscreen
/// close, discarding its output). Depth is left null when unsupported by the
/// device. Returns false (with a warning) outside an active render pass.
bool resolve_pass(const ResolveDesc& desc, ResolvedTargets& out);

/// Opens an offscreen render pass (GXCreateFrameBuffer semantics): cleared
/// single-sample color+depth at (width, height) with full-target
/// viewport/scissor. Subsequent draws target it until resolve_pass restores the
/// EFB. Nesting is unsupported: returns false (with a warning) outside an
/// active render pass or while any offscreen pass is already open.
bool create_pass(uint32_t width, uint32_t height);

/// strikers-quest (from aurora-vr): a caller-owned color target for create_pass_external().
/// `view` must be a view of `texture` in `format`, and `format` must be color_format() so the same
/// pipelines apply. The texture needs RenderAttachment, CopySrc and TextureBinding usages.
struct ExternalPassTarget {
  wgpu::Texture texture;
  wgpu::TextureView view;
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  uint32_t width = 0;
  uint32_t height = 0;
  /// strikers-quest: 4 renders into a pooled multisampled color + depth target that resolves into
  /// `texture` when the pass ends (smooth edges on the headset); 1 renders into it directly.
  uint32_t sampleCount = 1;
};

/// create_pass() with the caller's own color target instead of a pooled offscreen texture (depth
/// is still pooled). The pass counts as consumed (its output IS the external texture), so close
/// it with resolve_pass({.color = false}). The headset eye passes render straight into the XR
/// swapchain's shared images through this.
bool create_pass_external(const ExternalPassTarget& target);

/// strikers-quest r43: the headset's eye pass opened and closed in the GX stream
/// (GXAuroraXrEyePass) instead of with the FIFO drained, so the game thread starts the eye send
/// while the FIFO thread is still on the normal view. Targets are registered once per slot (the
/// texture must outlive every pass that uses it); the pass's sample count comes with each begin.
void register_xr_eye_target(uint32_t slot, const ExternalPassTarget& target) noexcept;

/// strikers-quest r131: the headset's GPU-direct big screen. Aurora frame `frame` (current_frame() while it is being
/// recorded) copies its presented picture (the GX framebuffer the XR readback reads) into `texture` on the frame's own
/// encoder, just before its submit, so the submit callback (aurora_xr_set_submit_callback) ends a shared texture's
/// access as usual. `texture`: the picture's size, its format or that format's sRGB twin, CopyDst; null, or a frame
/// whose sizes differ (or whose readback is skipped), gets no copy. readback: the frame callback's CPU readback for
/// that frame too; a frame set here has none otherwise. Set during the frame, before its aurora_end_frame (game thread).
void set_xr_screen_copy(uint32_t frame, const wgpu::Texture& texture, bool readback) noexcept;
/// r131: whether frame `frame`'s screen copy was recorded: from its submit callback on, for 4 frames.
bool xr_screen_copied(uint32_t frame) noexcept;

/// What GX viewport/scissor scaling treats as the "logical" frame size while an offscreen pass is
/// open: off (default), the pass's own size; on, the native configured size, for callers that
/// replay the full-scene draw path (which sets its viewport in native units) into a larger target.
/// Scope it to the caller's own pass.
void set_offscreen_uses_native_logical_size(bool enabled) noexcept;
bool offscreen_uses_native_logical_size() noexcept;

/// True while an offscreen pass (create_pass or GXCreateFrameBuffer) is open.
bool is_offscreen() noexcept;

/// Increments on aurora_end_frame.
uint32_t current_frame() noexcept;

/// Blocks until the render worker has drained its queue. After this returns,
/// no draw callback is executing or queued to execute; used before unloading
/// code that registered draw types. Callable from the game thread only.
void synchronize();

} // namespace aurora::gfx
