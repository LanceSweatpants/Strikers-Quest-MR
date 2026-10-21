#include "command_processor.hpp"

#include "../gfx/depth_peek.hpp"
#include "../gfx/pipeline_cache.hpp"
#include "../gfx/recording.hpp"
#include "../gfx/resources.hpp"
#include "../internal.hpp"
#include "fifo.hpp" // smstrikers-port: display list provenance
#include "dolphin/gd/GDGeometry.h"
#include "dolphin/gx/GXAurora.h"
#include "gx.hpp"
#include "pipeline.hpp"
#include "regs.hpp"
#include "shader_info.hpp"
#include "texture.hpp"
#include "vertex_decode.hpp"

#include <absl/container/flat_hash_map.h>
#include <tracy/Tracy.hpp>
#include <xxhash.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>

// recording.cpp: bytes used so far in this frame's fixed pools (vertex, uniform, index, storage).
extern "C" void aurora_gfx_pool_usage(uint32_t* usedBytes, size_t count);
extern "C" void aurora_gx_set_xr_coverage_mode(int mode);  // gx.cpp; in-band from GX_AURORA_XR_EYE_PASS
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace aurora::gx::fifo {
namespace {
constexpr Module Log{"aurora::gx::fifo"};

class Reader {
public:
  explicit Reader(std::span<const u8> data) noexcept : mData{data} {}

  [[nodiscard]] bool empty() const noexcept { return mPos == mData.size(); }
  [[nodiscard]] size_t offset() const noexcept { return mPos; }
  [[nodiscard]] size_t size() const noexcept { return mData.size(); }
  [[nodiscard]] size_t remaining() const noexcept { return mData.size() - mPos; }
  [[nodiscard]] const u8* data() const noexcept { return mData.data(); }

  template <typename T>
    requires(std::is_arithmetic_v<T>)
  T read() noexcept {
    const auto bytes = take(sizeof(T));
    return read_bits<T>(bytes.data());
  }

  std::span<const u8> take(size_t count) noexcept {
    AURORA_ASSERT(count <= remaining(), "FIFO read overrun: need {} bytes at offset {}, have {}", count, mPos,
                  remaining());
    const auto bytes = mData.subspan(mPos, count);
    mPos += count;
    return bytes;
  }

  void skip(size_t count) noexcept {
    AURORA_ASSERT(count <= remaining(), "FIFO read overrun: need {} bytes at offset {}, have {}", count, mPos,
                  remaining());
    mPos += count;
  }

  std::string read_string() noexcept {
    const auto len = read<uint16_t>();
    const auto bytes = take(len);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
  }

private:
  std::span<const u8> mData;
  size_t mPos = 0;
};

// smstrikers-port: u32, since a strip, fan or quad draw of a u16 vertex count can need more than 65,535 indices.
u32 prepare_idx_buffer(ByteBuffer& buf, GXPrimitive prim, u16 vtxStart, u16 vtxCount) noexcept {
  u32 numIndices = 0;
  // smstrikers-port: filled in place, since ByteBuffer::append costs a resize check and a memcpy per index.
  if (prim == GX_QUADS) {
    const u32 quads = vtxCount / 4;
    numIndices = quads * 6;
    u16* out = reinterpret_cast<u16*>(buf.append_uninitialized(numIndices * sizeof(u16)));
    for (u32 q = 0; q < quads; ++q) {
      const u16 idx0 = static_cast<u16>(vtxStart + q * 4);
      out[0] = idx0;
      out[1] = static_cast<u16>(idx0 + 1);
      out[2] = static_cast<u16>(idx0 + 2);
      out[3] = static_cast<u16>(idx0 + 2);
      out[4] = static_cast<u16>(idx0 + 3);
      out[5] = idx0;
      out += 6;
    }
  } else if (prim == GX_TRIANGLES) {
    numIndices = vtxCount;
    u16* out = reinterpret_cast<u16*>(buf.append_uninitialized(numIndices * sizeof(u16)));
    for (u16 v = 0; v < vtxCount; ++v) {
      out[v] = static_cast<u16>(vtxStart + v);
    }
  } else if (prim == GX_TRIANGLEFAN) {
    if (vtxCount < 3) {
      numIndices = vtxCount;
      u16* out = reinterpret_cast<u16*>(buf.append_uninitialized(numIndices * sizeof(u16)));
      for (u16 v = 0; v < vtxCount; ++v) {
        out[v] = static_cast<u16>(vtxStart + v);
      }
    } else {
      numIndices = (u32(vtxCount) - 2) * 3;
      u16* out = reinterpret_cast<u16*>(buf.append_uninitialized(numIndices * sizeof(u16)));
      for (u16 v = 2; v < vtxCount; ++v) {
        const u16 idx = static_cast<u16>(vtxStart + v);
        out[0] = vtxStart;
        out[1] = static_cast<u16>(idx - 1);
        out[2] = idx;
        out += 3;
      }
    }
  } else if (prim == GX_TRIANGLESTRIP) {
    if (vtxCount < 3) {
      numIndices = vtxCount;
      u16* out = reinterpret_cast<u16*>(buf.append_uninitialized(numIndices * sizeof(u16)));
      for (u16 v = 0; v < vtxCount; ++v) {
        out[v] = static_cast<u16>(vtxStart + v);
      }
    } else {
      numIndices = (u32(vtxCount) - 2) * 3;
      u16* out = reinterpret_cast<u16*>(buf.append_uninitialized(numIndices * sizeof(u16)));
      for (u16 v = 2; v < vtxCount; ++v) {
        const u16 idx = static_cast<u16>(vtxStart + v);
        if ((v & 1) == 0) {
          out[0] = static_cast<u16>(idx - 2);
          out[1] = static_cast<u16>(idx - 1);
        } else {
          out[0] = static_cast<u16>(idx - 1);
          out[1] = static_cast<u16>(idx - 2);
        }
        out[2] = idx;
        out += 3;
      }
    }
  } else if (prim == GX_LINES || prim == GX_LINESTRIP || prim == GX_POINTS) {
    buf.reserve_extra(6 * sizeof(u16));
    buf.append<u16>(0);
    buf.append<u16>(1);
    buf.append<u16>(3);
    buf.append<u16>(3);
    buf.append<u16>(2);
    buf.append<u16>(0);
    numIndices = 6;
  } else
    UNLIKELY FATAL("unsupported primitive type {}", static_cast<u32>(prim));
  return numIndices;
}

// GX FIFO opcodes - use CP_ prefix to avoid clashing with GXCommandList.h macros
constexpr u8 CP_CMD_NOP = GX_NOP;
constexpr u8 CP_CMD_LOAD_CP_REG = GX_LOAD_CP_REG;
constexpr u8 CP_CMD_LOAD_XF_REG = GX_LOAD_XF_REG;
constexpr u8 CP_CMD_LOAD_INDX_A = GX_LOAD_INDX_A;
constexpr u8 CP_CMD_LOAD_INDX_B = GX_LOAD_INDX_B;
constexpr u8 CP_CMD_LOAD_INDX_C = GX_LOAD_INDX_C;
constexpr u8 CP_CMD_LOAD_INDX_D = GX_LOAD_INDX_D;
constexpr u8 CP_CMD_CALL_DL = GX_CMD_CALL_DL;
constexpr u8 CP_CMD_INVAL_VTX = GX_CMD_INVL_VC;
constexpr u8 CP_CMD_LOAD_BP_REG = GX_LOAD_BP_REG & GX_OPCODE_MASK;

// Primitive type mask
constexpr u8 CP_OPCODE_MASK = GX_OPCODE_MASK;
constexpr u8 CP_VAT_MASK = GX_VAT_MASK;

struct FogRangeLutKey {
  std::array<u16, 10> rangeK;
  f32 rangeCenter;
  f32 renderWidth;
  u32 targetWidth;
  // strikers-quest r43: a double-wide single-pass stereo target; each half gets its own eye's table.
  bool stereo = false;

  bool operator==(const FogRangeLutKey&) const = default;
};

struct FogRangeLutEntry {
  FogRangeLutKey key;
  std::vector<f32> factors;
};

constexpr size_t MaxFogRangeLuts = 32;
std::vector<FogRangeLutEntry> sFogRangeLuts;

struct DrawCache {
  PipelineConfig config{};
  ShaderInfo shaderInfo{};
  gfx::PipelineRef pipelineRef{};
  GXBindGroups bindGroups{};
  uint64_t bindGeneration = 0;
  GXVtxFmt fmt = GX_MAX_VTXFMT;
  u8 lineMode = 0;
  bool hasPipeline = false;
  gfx::Range uniformRange{};
  gfx::Range fogRange{};
  FogRangeLutKey fogRangeKey{};
  bool hasFogRange = false;
  GXVtxFmt lastDrawFmt = GX_MAX_VTXFMT;
  // strikers-quest: the decoded twin of this pipeline (vertex_decode.hpp), re-derived with it.
  bool decodeChecked = false;
  vertex_decode::Layout decodeLayout{};
  gfx::PipelineRef decodedRef{};
  bool decodedReady = false;
  // strikers-quest r43: the single-pass stereo twins of this pipeline and of its decoded twin.
  bool stereoChecked = false;
  bool stereoFits = false;
  gfx::PipelineRef stereoRef{};
  bool stereoReady = false;
  bool stereoDecodedQueued = false;
  gfx::PipelineRef stereoDecodedRef{};
  bool stereoDecodedReady = false;
};
DrawCache sDrawCache;

// strikers-quest: decode once per game frame, reuse across the sends (normal view + eyes). Owned by
// the FIFO thread; cleared in-band at each game send boundary (CP_CMD_INVAL_VTX, written when the
// game flips its double-buffered frame memory, so recycled addresses never hit an old entry) and on
// the main thread after the end-of-frame drain (clear_draw_cache: the vertex-pool Ranges die with
// the frame). The key covers the raw vertex bytes, the vertex format and each indexed array's
// pointer/size/stride/endianness; the arrays' contents are fixed within a game send (the existing
// array cachedRange relies on the same).
std::atomic<int> sDecodeMode{0};  // aurora_xr_set_vertex_decode
bool sDecodeScope = false;        // GX_AURORA_DECODE_SCOPE
bool sLastDrawDecoded = false;    // a decoded draw must not be merged into
struct DecodeEntry {
  u32 base = 0;  // vec4 index into the vertex pool
  bool ok = false;
};
absl::flat_hash_map<u64, DecodeEntry> sDecodeCache;
std::vector<u32> sDecodeScratch;
struct DecodeStats {
  std::atomic<u32> decoded{0}, reused{0}, vertices{0}, fallback{0}, ineligible{0}, micros{0};
};
DecodeStats sDecodeStats;
// Decoded bytes pushed into the vertex pool, ever (the headset's pool guard keeps them out of its
// per-send estimates: they are paid once per frame, not per send).
std::atomic<uint64_t> sDecodedBytesTotal{0};
std::atomic<uint64_t> sDecodedBytesFrame{0};      // this aurora frame so far
std::atomic<uint64_t> sDecodedBytesLastFrame{0};  // the last completed aurora frame
// Decoding stops (draws keep pulling) once the frame's vertex pool is this full: an overflow aborts.
// Decoded copies are 5-7x the raw bytes, and the eye sends still need room for their raw pushes.
constexpr double kDecodePoolLimit = 0.6;

bool decode_candidate(GXPrimitive prim) noexcept {
  const int mode = sDecodeMode.load(std::memory_order_relaxed);
  if (mode == 0 || (mode == 1 && !sDecodeScope)) {
    return false;
  }
  return prim != GX_LINES && prim != GX_LINESTRIP && prim != GX_POINTS;
}

FogRangeLutKey fog_range_lut_key() noexcept {
  const auto& state = g_gxState.fog;
  const f32 logicalWidth = std::max(g_gxState.logicalViewport.width, 1.f);
  // strikers-quest r43: in single-pass stereo the render viewport spans both eyes; one eye is half.
  const bool stereo = stereo_active();
  const f32 renderWidth = std::max(g_gxState.renderViewport.width * (stereo ? 0.5f : 1.f), 1.f);
  return {
      .rangeK = state.rangeK,
      .rangeCenter = ((static_cast<f32>(state.rangeCenter) - g_gxState.logicalViewport.left) / logicalWidth) * 2.f -
                     1.f + (g_gxState.renderViewport.left / renderWidth) * 2.f,
      .renderWidth = renderWidth,
      .targetWidth = gfx::get_render_target_size().x,
      .stereo = stereo,
  };
}

std::vector<f32> build_fog_range_lut(const FogRangeLutKey& key) {
  std::array<f32, 10> rangeK;
  for (u32 i = 0; i < rangeK.size(); ++i) {
    const u32 source = (i & ~1u) | (1u - (i & 1u));
    rangeK[i] = static_cast<f32>(key.rangeK[source]) / 64.f;
  }

  std::vector<f32> lut(key.targetWidth);
  const u32 halfWidth = std::max<u32>(key.targetWidth / 2, 1);
  for (u32 x = 0; x < key.targetWidth; ++x) {
    // strikers-quest r43: in a stereo target each half is one eye, read at the fragment's own x.
    const u32 eyeX = key.stereo ? x % halfWidth : x;
    const f32 screenX = ((static_cast<f32>(eyeX) + 0.5f) / key.renderWidth) * 2.f - 1.f;
    const f32 offset = screenX - key.rangeCenter;
    const f32 rangeIndex = std::clamp(9.f - std::abs(offset) * 9.f, 0.f, 9.f);
    const u32 lower = static_cast<u32>(rangeIndex);
    const u32 upper = std::min(lower + 1, 9u);
    const f32 fraction = rangeIndex - static_cast<f32>(lower);
    const f32 k = std::max(rangeK[lower] * (1.f - fraction) + rangeK[upper] * fraction, 0.000001f);
    lut[x] = std::sqrt(offset * offset + k * k) / k;
  }
  return lut;
}

const std::vector<f32>& resolve_fog_range_lut(const FogRangeLutKey& key) {
  for (const auto& entry : sFogRangeLuts) {
    if (entry.key == key) {
      return entry.factors;
    }
  }
  if (sFogRangeLuts.size() == MaxFogRangeLuts) {
    sFogRangeLuts.erase(sFogRangeLuts.begin());
  }
  sFogRangeLuts.emplace_back(FogRangeLutEntry{key, build_fog_range_lut(key)});
  return sFogRangeLuts.back().factors;
}

gfx::Range push_fog_range_lut(const FogRangeLutKey& key) {
  const auto& lut = resolve_fog_range_lut(key);
  return gfx::push_storage(reinterpret_cast<const u8*>(lut.data()), lut.size() * sizeof(f32));
}

// smstrikers-port: state for report_prim_desync. By the time prepare_idx_buffer
// aborts, nothing on the stack remembers which byte was rejected. Touched only
// from the FIFO processor, under the buffer mutex.
struct CmdTrace {
  size_t offset;
  u8 cmd;
};
constexpr size_t kCmdTraceLen = 12;
std::array<CmdTrace, kCmdTraceLen> sCmdTrace{};
size_t sCmdTraceCount = 0;

struct LastDrawTrace {
  size_t cmdOffset;
  size_t payloadStart;
  u32 vtxCount;
  u32 vtxSize;
  u8 prim;
  u8 fmt;
  bool valid;
};
LastDrawTrace sLastDraw{};

// The opcodes prepare_idx_buffer can build indices for. 0x88 (GX_QUADS2) is a
// real GX opcode that is not among them, so this is not "is a draw command".
bool is_supported_prim(const u8 opcode) noexcept {
  switch (opcode) {
  case GX_DRAW_QUADS:
  case GX_DRAW_TRIANGLES:
  case GX_DRAW_TRIANGLE_STRIP:
  case GX_DRAW_TRIANGLE_FAN:
  case GX_DRAW_LINES:
  case GX_DRAW_LINE_STRIP:
  case GX_DRAW_POINTS:
    return true;
  default:
    return false;
  }
}

std::string hex_window(const u8* data, size_t size, size_t centre, size_t before, size_t after) {
  const size_t start = centre > before ? centre - before : 0;
  const size_t end = std::min(size, centre + after);
  std::string hex;
  for (size_t i = start; i < end; ++i) {
    hex += i == centre ? fmt::format("[{:02x}]", data[i]) : fmt::format(" {:02x}", data[i]);
  }
  return fmt::format("({}-{}){}", start, end == 0 ? 0 : end - 1, hex);
}

u8 line_mode_for_prim(GXPrimitive prim) noexcept {
  switch (prim) {
  case GX_LINES:
    return 1;
  case GX_LINESTRIP:
    return 2;
  case GX_POINTS:
    return 3;
  default:
    return 0;
  }
}
} // namespace

static void handle_draw(u8 cmd, Reader& reader) noexcept;
static void handle_aurora(Reader& reader) noexcept;
// smstrikers-port: see report_prim_desync.
[[noreturn]] static void report_prim_desync(u8 cmd, const Reader& reader, uint64_t streamPos) noexcept;

ProcessResult process(const u8* data, u32 size, uint64_t streamPos) noexcept {
  ZoneScoped;
  Reader reader{{data, size}};

  // smstrikers-port: offsets are chunk-relative, so the trace is too.
  sCmdTraceCount = 0;
  sLastDraw = {};

  while (!reader.empty()) {
    const size_t cmdOffset = reader.offset();
    const u8 cmd = reader.read<u8>();
    u8 opcode = cmd & CP_OPCODE_MASK;
    sCmdTrace[sCmdTraceCount++ % kCmdTraceLen] = {cmdOffset, cmd}; // smstrikers-port

    switch (opcode) {
    case CP_CMD_NOP:
      continue;

    case CP_CMD_LOAD_BP_REG: {
      const u32 value = reader.read<u32>();
      handle_bp(value);
      if (reg_get(value, 8, 24) == GX_BP_REG_DRAWDONE) {
        return {static_cast<u32>(reader.offset()), true};
      }
      break;
    }

    case CP_CMD_LOAD_CP_REG: {
      const u8 addr = reader.read<u8>();
      handle_cp(addr, reader.read<u32>());
      break;
    }

    case CP_CMD_LOAD_XF_REG: {
      const u32 header = reader.read<u32>();
      const u32 count = ((header >> 16) & 0xFFFF) + 1;
      const u16 addr = header & 0xFFFF;
      handle_xf(addr, reader.take(count * sizeof(u32)));
      break;
    }

    case CP_CMD_LOAD_INDX_A:
    case CP_CMD_LOAD_INDX_B:
    case CP_CMD_LOAD_INDX_C:
    case CP_CMD_LOAD_INDX_D: {
      ZoneScopedN("LOAD_INDX");
      const u32 arrayType = GX_POS_MTX_ARRAY + (opcode - CP_CMD_LOAD_INDX_A) / 0x08;
      const u16 srcArrayIdx = reader.read<u16>();
      const u16 addrLen = reader.read<u16>();

      const u16 len = (addrLen >> 12) + 1;
      const u16 dstAddr = addrLen & 0x0FFF;
      auto const& array = g_gxState.arrays[arrayType];
      const u32 srcOffset = static_cast<u32>(srcArrayIdx) * array.stride;
      const u32 srcSize = static_cast<u32>(len) * sizeof(u32);
      AURORA_ASSERT(array.data != nullptr, "indexed XF load from unmapped array {}", arrayType);
      AURORA_ASSERT(srcOffset <= array.size && srcSize <= array.size - srcOffset,
                    "indexed XF load outside array {}: offset={}, size={}, array size={}", arrayType, srcOffset,
                    srcSize, array.size);
      auto const* srcData = static_cast<const u8*>(array.data) + srcOffset;
      if (!copy_xf_data(dstAddr, srcData, len, array.le ? std::endian::little : std::endian::big)) {
#ifndef NDEBUG
        Log.debug("Unimplemented indexed XF load (opcode 0x{:02X}, dstAddr=%04x)", opcode, dstAddr);
#endif
      }
      break;
    }

    case CP_CMD_CALL_DL: {
      // Call display list: 8 bytes (address + size)
      Log.warn("Ignoring nested GX_CMD_CALL_DL");
      reader.skip(8);
      break;
    }

    case CP_CMD_INVAL_VTX: {
      // Invalidate vertex cache. strikers-quest: the game writes this as it flips its double-buffered
      // frame memory (glplatFrameAllocNextFrame), after every send of a game frame: decoded vertices
      // keyed on array addresses must not outlive it.
      sDecodeCache.clear();
      sDecodeScope = false;
      break;
    }

    case GX_AURORA: {
      handle_aurora(reader);
      break;
    }

    // Draw commands: 0x80-0xBF
    case GX_DRAW_QUADS:
    case GX_DRAW_TRIANGLES:
    case GX_DRAW_TRIANGLE_STRIP:
    case GX_DRAW_TRIANGLE_FAN:
    case GX_DRAW_LINES:
    case GX_DRAW_LINE_STRIP:
    case GX_DRAW_POINTS: {
      handle_draw(cmd, reader);
      break;
    }

    default:
      // Check if it's a draw command (0x80-0xBF range)
      if (cmd >= 0x80) {
        // smstrikers-port: prepare_idx_buffer aborts on an opcode it cannot
        // build indices for, with the Reader already past the evidence. Reject
        // it here, where the byte's offset is still in hand.
        if (!is_supported_prim(opcode))
          UNLIKELY { report_prim_desync(cmd, reader, streamPos); }
        handle_draw(cmd, reader);
      } else {
        // Hex dump surrounding bytes for debugging
        {
          const size_t pos = reader.offset();
          size_t dumpStart = (pos > 17) ? pos - 17 : 0;
          size_t dumpEnd = (pos + 16 < size) ? pos + 16 : size;
          std::string hex;
          for (size_t i = dumpStart; i < dumpEnd; i++) {
            if (i == pos - 1)
              hex += fmt::format("[{:02x}]", data[i]);
            else
              hex += fmt::format(" {:02x}", data[i]);
          }
          Log.error("  hex dump (pos {}-{}):{}", dumpStart, dumpEnd - 1, hex);
        }
        FATAL("command_processor: unknown opcode 0x{:02X} at pos {}", cmd, reader.offset() - 1);
      }
      break;
    }
  }
  return {size, false};
}

[[noreturn]] static void handle_draw_overrun(size_t totalVtxBytes, const Reader& reader) noexcept {
  // Hex dump around the draw command for debugging
  const size_t pos = reader.offset();
  const size_t size = reader.size();
  const u8* data = reader.data();
  size_t cmdPos = pos - 2 - 1; // opcode byte position (before vtxCount and pos++)
  size_t dumpStart = (cmdPos > 16) ? cmdPos - 16 : 0;
  size_t dumpEnd = (cmdPos + 32 < size) ? cmdPos + 32 : size;
  std::string hex;
  for (size_t i = dumpStart; i < dumpEnd; i++) {
    if (i == cmdPos)
      hex += fmt::format("[{:02x}]", data[i]);
    else
      hex += fmt::format(" {:02x}", data[i]);
  }
  Log.error("  hex dump around draw cmd (pos {}-{}):{}", dumpStart, dumpEnd - 1, hex);
  FATAL("draw vertex data overrun: need {} bytes at pos {}, have {}", totalVtxBytes, pos, reader.remaining());
}

static u32 calc_vtx_size(GXVtxFmt fmt) noexcept {
  u32 vtxSize = 0;
  const auto& vtxFmt = g_gxState.vtxFmts[fmt];
  for (int i = GX_VA_PNMTXIDX; i <= GX_VA_TEX7; ++i) {
    const auto& attrFmt = vtxFmt.attrs[i];
    switch (g_gxState.vtxDesc[i]) {
    case GX_NONE:
      break;
    case GX_DIRECT: {
      const auto attr = static_cast<GXAttr>(i);
      vtxSize += comp_type_size(attr, attrFmt.type) * comp_cnt_count(attr, attrFmt.cnt);
      break;
    }
    case GX_INDEX8:
      vtxSize += i == GX_VA_NRM && attrFmt.cnt == GX_NRM_NBT3 ? 3 : 1;
      break;
    case GX_INDEX16:
      vtxSize += i == GX_VA_NRM && attrFmt.cnt == GX_NRM_NBT3 ? 6 : 2;
      break;
    }
  }
  g_gxState.lastVtxFmt = fmt;
  g_gxState.lastVtxSize = vtxSize;
  return vtxSize;
}

// smstrikers-port: where the rejected byte sits inside the display list it came
// from. Mid-payload means the reader desynced, and the offset over the vertex
// count is the stride the data really had. Diagnostic only, on an aborting path.
[[noreturn]] static void report_prim_desync(const u8 cmd, const Reader& reader, const uint64_t streamPos) noexcept {
  const u8* data = reader.data();
  const size_t size = reader.size();
  const size_t pos = reader.offset() - 1; // the rejected byte itself
  const uint64_t absPos = streamPos + pos;

  Log.error("[prim248] rejected opcode 0x{:02X} (primitive {}, vat {}) at chunk offset {} of {}, stream position {}",
            cmd, static_cast<u32>(cmd & CP_OPCODE_MASK), static_cast<u32>(cmd & CP_VAT_MASK), pos, size, absPos);

  // Where the byte came from.
  const auto* splice = find_display_list(absPos);
  size_t listOffset = 0;
  const u8* listData = nullptr;
  u32 listCount = 0;
  if (splice == nullptr) {
    Log.error("[prim248] not inside any called display list: this is the immediate-mode stream");
  } else {
    listOffset = static_cast<size_t>(absPos - splice->streamPos);
    Log.error("[prim248] display list src={} size={} -> offset {} within it, {} bytes before its end", fmt::ptr(splice->src),
              splice->size, listOffset, splice->size - listOffset);
    // Prefer the copy in the FIFO: it is the bytes actually being read. Fall
    // back to the caller's buffer when the list began in an earlier chunk.
    if (splice->streamPos >= streamPos && splice->streamPos - streamPos + 3 <= size) {
      listData = data + (splice->streamPos - streamPos);
    } else {
      listData = static_cast<const u8*>(splice->src);
    }
  }

  // The list's own header, which is what dlMakeDisplayList committed to.
  if (listData != nullptr) {
    const u8 hdr = listData[0];
    listCount = static_cast<u32>(listData[1]) << 8 | listData[2];
    Log.error("[prim248] list header: opcode 0x{:02X} (primitive {}, vat {}), {} vertices", hdr,
              static_cast<u32>(hdr & CP_OPCODE_MASK), static_cast<u32>(hdr & CP_VAT_MASK), listCount);
    if (listCount != 0 && listOffset > 3) {
      const size_t payload = listOffset - 3;
      Log.error("[prim248] payload consumed {} bytes over {} vertices -> implied stride {}, remainder {}", payload,
                listCount, payload / listCount, payload % listCount);
      Log.error("[prim248] list size {} implies a written stride of about {} (padded to 32)", splice->size,
                (splice->size - 3) / listCount);
    }
  }

  // What the reader believed the vertices looked like.
  if (sLastDraw.valid) {
    Log.error("[prim248] last draw: primitive {} vat {}, {} vertices x {} bytes = {}, payload at chunk offset {}..{}",
              static_cast<u32>(sLastDraw.prim), static_cast<u32>(sLastDraw.fmt), sLastDraw.vtxCount, sLastDraw.vtxSize,
              sLastDraw.vtxCount * sLastDraw.vtxSize, sLastDraw.payloadStart,
              sLastDraw.payloadStart + sLastDraw.vtxCount * sLastDraw.vtxSize);
  } else {
    Log.error("[prim248] no draw has been read in this chunk");
  }
  {
    std::string desc;
    u32 stride = 0;
    const auto& vtxFmt = g_gxState.vtxFmts[sLastDraw.valid ? static_cast<GXVtxFmt>(sLastDraw.fmt) : g_gxState.lastVtxFmt];
    for (int i = GX_VA_PNMTXIDX; i <= GX_VA_TEX7; ++i) {
      const auto attr = static_cast<GXAttr>(i);
      u32 sz = 0;
      const char* mode = nullptr;
      switch (g_gxState.vtxDesc[i]) {
      case GX_NONE:
        continue;
      case GX_DIRECT:
        mode = "direct";
        sz = comp_type_size(attr, vtxFmt.attrs[i].type) * comp_cnt_count(attr, vtxFmt.attrs[i].cnt);
        break;
      case GX_INDEX8:
        mode = "idx8";
        sz = i == GX_VA_NRM && vtxFmt.attrs[i].cnt == GX_NRM_NBT3 ? 3 : 1;
        break;
      case GX_INDEX16:
        mode = "idx16";
        sz = i == GX_VA_NRM && vtxFmt.attrs[i].cnt == GX_NRM_NBT3 ? 6 : 2;
        break;
      default:
        mode = "?";
        break;
      }
      desc += fmt::format(" attr{}={}({})", i, mode, sz);
      stride += sz;
    }
    Log.error("[prim248] descriptor stride {}:{}", stride, desc);
    Log.error("[prim248] PNMTXIDX is {}; a list built for the other answer desyncs one byte per vertex",
              g_gxState.vtxDesc[GX_VA_PNMTXIDX] == GX_DIRECT ? "direct (stitched)" : "absent (unstitched)");
  }

  // The commands immediately before it, oldest first.
  {
    const size_t seen = std::min(sCmdTraceCount, kCmdTraceLen);
    std::string trace;
    for (size_t i = seen; i-- > 1;) {
      const auto& entry = sCmdTrace[(sCmdTraceCount - 1 - i) % kCmdTraceLen];
      trace += splice != nullptr
                   ? fmt::format(" 0x{:02X}@{}(list+{})", entry.cmd, entry.offset,
                                 static_cast<int64_t>(streamPos + entry.offset) - static_cast<int64_t>(splice->streamPos))
                   : fmt::format(" 0x{:02X}@{}", entry.cmd, entry.offset);
    }
    Log.error("[prim248] preceding commands (oldest first):{}", trace);
  }

  Log.error("[prim248] hex around the rejected byte {}", hex_window(data, size, pos, 32, 32));
  if (listData != nullptr && listData >= data && listData < data + size) {
    Log.error("[prim248] hex at the list header {}", hex_window(data, size, static_cast<size_t>(listData - data), 0, 32));
  }

  // Last, because it dereferences a pointer the game owns and may have freed.
  // Both model loaders patch indices into their lists in place, so a difference
  // here is late mutation rather than corruption.
  if (splice != nullptr && splice->src != nullptr && listData != nullptr && listData != splice->src) {
    const auto* src = static_cast<const u8*>(splice->src);
    u32 differing = 0;
    for (u32 i = 0; i < splice->size; ++i) {
      if (src[i] != listData[i]) {
        ++differing;
      }
    }
    Log.error("[prim248] {} of {} bytes differ between the FIFO copy and the caller's list", differing, splice->size);
  }

  FATAL("unsupported primitive type {}", static_cast<u32>(cmd & CP_OPCODE_MASK));
}

// strikers-quest: for a decode candidate whose decoded pipeline is built, point the draw at this
// frame's decoded copy of its vertices (decoding them now on a cache miss). False = pull as before.
static bool try_decode(const u8* vtxData, u16 vtxCount, DrawImmediateData& immediates,
                       gfx::PipelineRef& pipelineRef) noexcept {
  auto& cache = sDrawCache;
  if (!cache.decodeChecked) {
    cache.decodeChecked = true;
    cache.decodedReady = false;
    cache.decodeLayout = vertex_decode::layout_for(cache.config.shaderConfig);
    if (cache.decodeLayout.valid) {
      PipelineConfig decodedConfig = cache.config;
      decodedConfig.shaderConfig.decodedVtx = true;
      cache.decodedRef = gfx::pipeline_ref(decodedConfig);  // queues the build on first sight
    }
  }
  if (!cache.decodeLayout.valid) {
    sDecodeStats.ineligible.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  if (!cache.decodedReady) {
    // Until it is built the draw keeps the pulling pipeline: never a missing player.
    cache.decodedReady = gfx::pipeline_ready(cache.decodedRef);
    if (!cache.decodedReady) {
      sDecodeStats.fallback.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
  }

  const auto& sc = cache.config.shaderConfig;
  struct KeyArray {
    u64 data;
    u32 size;
    u8 stride;
    u8 le;
    u16 pad;
  };
  static_assert(sizeof(KeyArray) == 16);
  std::array<KeyArray, MaxVtxAttr> keyArrays{};
  std::array<vertex_decode::ArraySource, MaxVtxAttr> sources{};
  for (int i = GX_VA_POS; i <= GX_VA_TEX7; ++i) {
    if (sc.attrs[i].attrType != GX_INDEX8 && sc.attrs[i].attrType != GX_INDEX16) {
      continue;
    }
    const auto& array = g_gxState.arrays[i];
    keyArrays[i].data = reinterpret_cast<uintptr_t>(array.data);
    keyArrays[i].size = array.size;
    keyArrays[i].stride = array.stride;
    keyArrays[i].le = array.le ? 1 : 0;
    sources[i] = {static_cast<const u8*>(array.data), array.size};
  }
  const size_t vtxBytes = static_cast<size_t>(vtxCount) * sc.vtxStride;
  const u64 formatHash = XXH3_64bits(sc.attrs.data(), sizeof(sc.attrs)) ^ (static_cast<u64>(sc.vtxStride) << 56);
  const u64 key = XXH3_64bits_withSeed(vtxData, vtxBytes, XXH3_64bits_withSeed(keyArrays.data(), sizeof(keyArrays),
                                                                                   formatHash)) ^
                  vtxCount;

  auto [it, inserted] = sDecodeCache.try_emplace(key);
  if (!inserted) {
    if (!it->second.ok) {
      sDecodeStats.ineligible.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    immediates.decodedBase = it->second.base;
    pipelineRef = cache.decodedRef;
    sDecodeStats.reused.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  const u32 words = cache.decodeLayout.strideWords;
  const size_t decodedBytes = static_cast<size_t>(vtxCount) * words * sizeof(u32) + 15;  // + alignment pad
  {
    uint32_t used[1] = {0};
    aurora_gfx_pool_usage(used, 1);
    if (static_cast<double>(used[0]) + static_cast<double>(decodedBytes) >
        static_cast<double>(gfx::VertexBufferSize) * kDecodePoolLimit) {
      sDecodeCache.erase(it);  // later sends check again (cheap) and keep pulling
      sDecodeStats.fallback.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
  }
  const auto t0 = std::chrono::steady_clock::now();
  sDecodeScratch.resize(static_cast<size_t>(vtxCount) * words);
  if (!vertex_decode::decode(sc, cache.decodeLayout, vtxData, vtxCount, sources, sDecodeScratch.data())) {
    it->second.ok = false;  // an index past its array: keep pulling, as before
    sDecodeStats.ineligible.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const auto range = gfx::push_verts(reinterpret_cast<const uint8_t*>(sDecodeScratch.data()),
                                     sDecodeScratch.size() * sizeof(u32), 16);
  it->second = {range.offset / 16, true};
  sDecodedBytesTotal.fetch_add(decodedBytes, std::memory_order_relaxed);
  sDecodedBytesFrame.fetch_add(decodedBytes, std::memory_order_relaxed);
  immediates.decodedBase = range.offset / 16;
  pipelineRef = cache.decodedRef;
  sDecodeStats.decoded.fetch_add(1, std::memory_order_relaxed);
  sDecodeStats.vertices.fetch_add(vtxCount, std::memory_order_relaxed);
  sDecodeStats.micros.fetch_add(
      static_cast<u32>(
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count()),
      std::memory_order_relaxed);
  return true;
}

// strikers-quest r43: in a single-pass stereo pass, point the draw at the stereo twin of the pipeline
// it would use (pulling or decoded) once that is built. False = draw with the mono pipeline: both
// halves then show the centre view of this draw, which only lasts until the twin compiles.
static bool try_stereo(bool decoded, gfx::PipelineRef& pipelineRef) noexcept {
  auto& cache = sDrawCache;
  if (!cache.stereoFits) {
    return false;
  }
  if (decoded) {
    if (!cache.stereoDecodedQueued) {
      cache.stereoDecodedQueued = true;
      PipelineConfig config = cache.config;
      config.shaderConfig.decodedVtx = true;
      config.shaderConfig.stereo = true;
      config.shaderConfig.xrClip = g_gxState.xrClip.enabled;  // r59
      cache.stereoDecodedRef = gfx::pipeline_ref(config);  // queues the build on first sight
    }
    if (!cache.stereoDecodedReady) {
      cache.stereoDecodedReady = gfx::pipeline_ready(cache.stereoDecodedRef);
      if (!cache.stereoDecodedReady) {
        return false;
      }
    }
    pipelineRef = cache.stereoDecodedRef;
    return true;
  }
  if (!cache.stereoReady) {
    cache.stereoReady = gfx::pipeline_ready(cache.stereoRef);
    if (!cache.stereoReady) {
      return false;
    }
  }
  pipelineRef = cache.stereoRef;
  return true;
}

static void push_gx_draw(GXPrimitive prim, GXVtxFmt fmt, u16 vtxCount, gfx::Range vertRange, gfx::Range idxRange,
                         u32 numIndices, const u8* vtxData) noexcept {
  auto& state = g_gxState;
  auto& cache = sDrawCache;

  DrawImmediateData immediates{.vtxStart = vertRange.offset, .currentPnMtx = state.currentPnMtx};

  const u8 lineMode = line_mode_for_prim(prim);
  const bool pipelineValid = cache.hasPipeline && (state.dirty & DirtyPipeline) == 0 && cache.fmt == fmt &&
                             cache.lineMode == lineMode && cache.config.msaaSamples == gfx::get_sample_count();
  if (!pipelineValid) {
    const bool hadPipeline = cache.hasPipeline;
    const auto prevSampledTextures = cache.shaderInfo.sampledTextures;
    const auto prevSampledIndTextures = cache.shaderInfo.sampledIndTextures;
    populate_pipeline_config(cache.config, prim, fmt);
    cache.shaderInfo = build_shader_info(cache.config.shaderConfig);
    cache.pipelineRef = gfx::pipeline_ref(cache.config);
    cache.fmt = fmt;
    cache.lineMode = lineMode;
    cache.hasPipeline = true;
    cache.decodeChecked = false;
    cache.stereoChecked = false;
    state.dirty = (state.dirty & ~DirtyPipeline) | DirtyUniform;
    if (!hadPipeline || prevSampledTextures != cache.shaderInfo.sampledTextures ||
        prevSampledIndTextures != cache.shaderInfo.sampledIndTextures) {
      cache.bindGeneration = 0;
    }
  }

  // strikers-quest: decoded draws read their own vertex copy and need no arrays in the storage pool.
  gfx::PipelineRef pipelineRef = cache.pipelineRef;
  const bool decoded =
      vtxData != nullptr && vtxCount != 0 && decode_candidate(prim) && try_decode(vtxData, vtxCount, immediates, pipelineRef);
  sLastDrawDecoded = decoded;

  // strikers-quest r43: single-pass stereo. Every draw in the eye pass makes it a stereo pass (encoded
  // once per half); triangle draws whose uniform block has room carry the stereo tail and, once built,
  // use the stereo pipeline. Lines and points stay mono (their expansion owns the instance index).
  bool stereoTail = false;
  bool stereoDraw = false;
  if (stereo_active()) {
    gfx::mark_current_pass_stereo();
    if (lineMode == 0) {
      if (!cache.stereoChecked) {
        cache.stereoChecked = true;
        cache.stereoFits = stereo_uniform_fits(cache.shaderInfo, state.xrClip.enabled);
        cache.stereoReady = false;
        cache.stereoDecodedQueued = false;
        cache.stereoDecodedReady = false;
        if (cache.stereoFits) {
          PipelineConfig config = cache.config;
          config.shaderConfig.stereo = true;
          config.shaderConfig.xrClip = state.xrClip.enabled;  // r59
          cache.stereoRef = gfx::pipeline_ref(config);  // queues the build on first sight
        }
      }
      stereoTail = cache.stereoFits;
      stereoDraw = try_stereo(decoded, pipelineRef);
    }
  }
  // strikers-quest r59: a clipped draw is only right through its clipping stereo pipeline; until that
  // is built (or for lines, points and draws whose uniforms leave no room) it is left out.
  if (state.xrClip.enabled && stereo_active() && !stereoDraw) {
    return;
  }

  if (!decoded) {
    for (int i = GX_VA_POS; i <= GX_VA_TEX7; ++i) {
      if (state.vtxDesc[i] != GX_INDEX8 && state.vtxDesc[i] != GX_INDEX16) {
        continue;
      }
      auto& array = state.arrays[i];
      if (array.cachedRange.size == 0) {
        array.cachedRange = gfx::push_storage(static_cast<const uint8_t*>(array.data), array.size);
      }
      immediates.arrayStart[i - GX_VA_POS] = array.cachedRange.offset;
    }
  }

  const bool bindGroupsValid =
      (state.dirty & DirtyTextures) == 0 && cache.bindGeneration == texture::current_bind_generation();
  if (!bindGroupsValid) {
    const auto prevBindGroup = cache.bindGroups.textureBindGroup;
    resolve_sampled_textures(cache.shaderInfo);
    cache.bindGroups = build_bind_groups(cache.shaderInfo);
    cache.bindGeneration = texture::current_bind_generation();
    state.dirty &= ~DirtyTextures;
    // For texture_size_bias uniform
    if (cache.bindGroups.textureBindGroup != prevBindGroup) {
      state.dirty |= DirtyUniform;
    }
  }

  const bool uniformValid = (state.dirty & DirtyUniform) == 0 && cache.uniformRange.size != 0;
  if (!uniformValid) {
    if (stereoTail) {
      // strikers-quest r43: the mono layout plus the stereo tail, so the block suits either pipeline.
      ShaderInfo info = cache.shaderInfo;
      info.stereo = true;
      info.uniformSize += StereoUniformSize;
      if (state.xrClip.enabled) {  // r59: and the clip planes the clipping pipeline reads
        info.xrClip = true;
        info.uniformSize += XrClipUniformSize;
      }
      cache.uniformRange = build_uniform(info);
    } else {
      cache.uniformRange = build_uniform(cache.shaderInfo);
    }
    state.dirty &= ~DirtyUniform;
  }
  if (cache.config.shaderConfig.fogRangeEnabled) {
    const auto key = fog_range_lut_key();
    if (!cache.hasFogRange || cache.fogRangeKey != key) {
      cache.fogRange = push_fog_range_lut(key);
      cache.fogRangeKey = key;
      cache.hasFogRange = true;
    }
  }
  immediates.fogRangeBase = cache.fogRange.offset / sizeof(u32);

  state.dirty &= ~DirtyImmediates;

  uint32_t instanceCount = 1;
  if (prim == GX_LINES) {
    instanceCount = vtxCount / 2;
  } else if (prim == GX_LINESTRIP) {
    instanceCount = vtxCount - 1;
  } else if (prim == GX_POINTS) {
    instanceCount = vtxCount;
  }
  cache.lastDrawFmt = fmt;
  gfx::push_draw_command(DrawData{
      .pipeline = pipelineRef,
      .vertRange = vertRange,
      .idxRange = idxRange,
      .uniformRange = cache.uniformRange,
      .immediateData = immediates,
      .bindGroups = cache.bindGroups,
      .vtxCount = vtxCount,
      .indexCount = numIndices,
      .instanceCount = instanceCount,
      // strikers-quest: the coverage mode lives in the pipeline config (gx.hpp g_xrCoverage).
      .dstAlpha = is_xr_coverage(cache.config.dstAlpha) ? cache.config.dstAlpha : state.dstAlpha,
      .stereo = stereoDraw ? 1u : 0u,
  });
}

static void handle_draw_unmerged(GXPrimitive prim, GXVtxFmt fmt, u16 vtxCount, gfx::Range vertRange,
                                 const u8* vtxData) noexcept {
  ZoneScoped;
  u32 numIndices = 0;
  gfx::Range idxRange;

  if (prim != GX_TRIANGLES) {
    ZoneScopedN("build idx buffer");
    static ByteBuffer idxBuf;
    numIndices = prepare_idx_buffer(idxBuf, prim, 0, vtxCount);
    idxRange = gfx::push_indices(idxBuf.data(), idxBuf.size(), 4);
    idxBuf.clear();
  }

  push_gx_draw(prim, fmt, vtxCount, vertRange, idxRange, numIndices, vtxData);
}


// smstrikers-port: report indexed reads that fall outside their array. Console
// GXSetArray takes no length, so an index past what Aurora copied to the GPU
// reads the rest of the storage buffer and only the positions are wrong.
// AURORA_DIAG_IDX=1 names the first two dozen.
static void diag_check_indices(const uint8_t* verts, u32 vtxCount, GXVtxFmt fmt, u32 vtxSize) noexcept {
  static const bool enabled = getenv("AURORA_DIAG_IDX") != nullptr;
  static int reported = 0;
  if (!enabled || vtxCount == 0 || verts == nullptr || reported >= 24) {
    return;
  }
  const auto& vtxFmt = g_gxState.vtxFmts[fmt];
  u32 off = 0;
  for (int i = GX_VA_PNMTXIDX; i <= GX_VA_TEX7; ++i) {
    const auto attr = static_cast<GXAttr>(i);
    const auto& attrFmt = vtxFmt.attrs[i];
    const auto type = g_gxState.vtxDesc[i];
    u32 sz = 0;
    bool idx8 = false;
    bool idx16 = false;
    switch (type) {
    case GX_NONE:
      continue;
    case GX_DIRECT:
      sz = comp_type_size(attr, attrFmt.type) * comp_cnt_count(attr, attrFmt.cnt);
      break;
    case GX_INDEX8:
      sz = (i == GX_VA_NRM && attrFmt.cnt == GX_NRM_NBT3) ? 3 : 1;
      idx8 = true;
      break;
    case GX_INDEX16:
      sz = (i == GX_VA_NRM && attrFmt.cnt == GX_NRM_NBT3) ? 6 : 2;
      idx16 = true;
      break;
    default:
      return;
    }
    if ((idx8 || idx16) && i >= GX_VA_POS) {
      const auto& array = g_gxState.arrays[i];
      u32 maxIdx = 0;
      for (u32 v = 0; v < vtxCount; ++v) {
        const uint8_t* p = verts + static_cast<size_t>(v) * vtxSize + off;
        const u32 val = idx16 ? (static_cast<u32>(p[0]) << 8 | p[1]) : p[0];
        if (val > maxIdx) {
          maxIdx = val;
        }
      }
      const u32 capacity = array.stride != 0 ? array.size / array.stride : 0;
      if (maxIdx >= capacity) {
        ++reported;
        Log.error("[idxdiag] attr {} maxIdx {} >= capacity {} (array size {} stride {}, {} vertices)", i, maxIdx,
                  capacity, array.size, array.stride, vtxCount);
      }
    }
    off += sz;
  }
}

static void draw_prim(GXPrimitive prim, GXVtxFmt fmt, u16 vtxCount, Reader& reader) noexcept {
  ZoneScoped;
  u32 vtxSize;
  if (g_gxState.lastVtxFmt == fmt)
    LIKELY { vtxSize = g_gxState.lastVtxSize; }
  else
    UNLIKELY { vtxSize = calc_vtx_size(fmt); }

  u32 totalVtxBytes = vtxCount * vtxSize;
  // smstrikers-port: see report_prim_desync.
  sLastDraw = {.cmdOffset = reader.offset() >= 3 ? reader.offset() - 3 : 0,
               .payloadStart = reader.offset(),
               .vtxCount = vtxCount,
               .vtxSize = vtxSize,
               .prim = static_cast<u8>(prim),
               .fmt = static_cast<u8>(fmt),
               .valid = true};
  if (totalVtxBytes > reader.remaining())
    UNLIKELY { handle_draw_overrun(totalVtxBytes, reader); }

  // strikers-quest: a draw that will decode stays its own draw (its vertices are decoded per draw),
  // and nothing merges into a decoded draw (its vertex range is followed by its decoded copy). With a
  // clean state the cache holds this draw's pipeline, so "will decode" is known here.
  const bool willDecode = decode_candidate(prim) && sDrawCache.decodeChecked && sDrawCache.decodeLayout.valid &&
                          sDrawCache.decodedReady;
  const bool cleanState = g_gxState.dirty == 0 && fmt == sDrawCache.lastDrawFmt && sDrawCache.lineMode == 0 &&
                          prim != GX_LINES && prim != GX_LINESTRIP && prim != GX_POINTS && !sLastDrawDecoded &&
                          !willDecode;
  auto* lastDraw = cleanState ? gfx::get_last_draw_command<DrawData>() : nullptr;
  const bool canMerge = lastDraw != nullptr && lastDraw->instanceCount == 1;

  // Push raw vertex data to buffer. Merged draws must remain contiguous with the previous range.
  const auto vertexData = reader.take(totalVtxBytes);
  diag_check_indices(vertexData.data(), vtxCount, fmt, vtxSize);   // smstrikers-port
  gfx::Range vertRange = gfx::push_verts(vertexData.data(), vertexData.size(), canMerge ? 0 : 4);

  // Try to merge with previous draw call
  if (canMerge) {
    u32 numIndices = 0;
    gfx::Range idxRange;
    static ByteBuffer idxBuf;
    const bool hadIndexRange = lastDraw->idxRange.size != 0;
    if (lastDraw->indexCount == 0 && prim != GX_TRIANGLES) {
      // Generate triangle index buffer for previous draw
      lastDraw->indexCount = prepare_idx_buffer(idxBuf, GX_TRIANGLES, 0, lastDraw->vtxCount);
    }
    if (lastDraw->indexCount != 0) {
      numIndices += prepare_idx_buffer(idxBuf, prim, lastDraw->vtxCount, vtxCount);
      idxRange = gfx::push_indices(idxBuf.data(), idxBuf.size(), hadIndexRange ? 0 : 4);
      idxBuf.clear();
    }
    CHECK(lastDraw->vertRange.offset + lastDraw->vertRange.size == vertRange.offset,
          "Non-consecutive vertex ranges ({} < {})", lastDraw->vertRange.offset + lastDraw->vertRange.size,
          vertRange.offset);
    if (hadIndexRange) {
      CHECK(lastDraw->idxRange.offset + lastDraw->idxRange.size == idxRange.offset,
            "Non-consecutive index ranges ({} < {})", lastDraw->idxRange.offset + lastDraw->idxRange.size,
            idxRange.offset);
    }
    lastDraw->vertRange.size += vertRange.size;
    if (lastDraw->idxRange.size == 0) {
      lastDraw->idxRange = idxRange;
    } else {
      lastDraw->idxRange.size += idxRange.size;
    }
    lastDraw->vtxCount += vtxCount;
    lastDraw->indexCount += numIndices;
    gfx::detail::increment_merged_draw_count();
    return;
  }

  handle_draw_unmerged(prim, fmt, vtxCount, vertRange, vertexData.data());
}

static void handle_draw(u8 cmd, Reader& reader) noexcept {
  const auto fmt = static_cast<GXVtxFmt>(cmd & CP_VAT_MASK);
  const auto prim = static_cast<GXPrimitive>(cmd & CP_OPCODE_MASK);
  draw_prim(prim, fmt, reader.read<u16>(), reader);
}

void handle_aurora(Reader& reader) noexcept {
  ZoneScoped;
  const u16 subCmd = reader.read<u16>();

  if (subCmd == GX_AURORA_LOAD_VIEWPORT_RENDER) {
    const f32 left = reader.read<f32>();
    const f32 top = reader.read<f32>();
    const f32 width = reader.read<f32>();
    const f32 height = reader.read<f32>();
    const f32 nearZ = reader.read<f32>();
    const f32 farZ = reader.read<f32>();
    set_render_viewport({
        .left = left,
        .top = top,
        .width = width,
        .height = height,
        .znear = nearZ,
        .zfar = farZ,
    });
  } else if (subCmd == GX_AURORA_LOAD_SCISSOR_RENDER) {
    const s32 left = reader.read<s32>();
    const s32 top = reader.read<s32>();
    const s32 width = reader.read<s32>();
    const s32 height = reader.read<s32>();
    set_render_scissor({left, top, width, height});
  } else if (subCmd == GX_AURORA_LOAD_PROJECTION_FULL) {
    auto& proj = g_gxState.proj;
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        proj[r][c] = reader.read<f32>();
      }
    }
    // Invalidate projection XF regs
    for (u32 reg = 0x20; reg <= 0x26; ++reg) {
      g_gxState.xfRegValid.reset(reg);
    }
    g_gxState.dirty |= DirtyUniform;
  } else if (subCmd >= GX_AURORA_LOAD_ARRAYBASE && subCmd <= (GX_AURORA_LOAD_ARRAYBASE | 0x0f)) {
    const u32 attrIdx = subCmd - GX_AURORA_LOAD_ARRAYBASE + GX_VA_POS;
    const u64 arrayAddr = reader.read<u64>();
    const u32 arraySize = reader.read<u32>();
    const bool le = reader.read<u8>() == 1;

    auto& array = g_gxState.arrays[attrIdx];
    const auto newData = reinterpret_cast<void*>(arrayAddr);
    if (array.data != newData || array.size != arraySize || array.le != le) {
      if (array.le != le) {
        // Endianness is baked into the shader
        g_gxState.dirty |= DirtyPipeline;
      }
      array.data = newData;
      array.size = arraySize;
      array.le = le;
      array.cachedRange = {};
      g_gxState.dirty |= DirtyImmediates;
    }
  } else if (subCmd == GX_AURORA_LOAD_TEXOBJ) {
    const auto texMapId = reader.read<u8>();
    CHECK(texMapId < MaxTextures, "invalid texture map id {}", texMapId);
    auto& slot = g_gxState.loadedTextures[texMapId];
    const auto newData = reinterpret_cast<const void*>(reader.read<u64>());
    const u32 newWidth = reader.read<u32>();
    const u32 newHeight = reader.read<u32>();
    const auto newFormat = static_cast<GXTexFmt>(reader.read<u32>());
    const auto newTlut = static_cast<GXTlut>(reader.read<u32>());
    u8 newFlags = slot.flags & ~0x80u; // Reset no-cache flag
    if (reader.read<u8>() != 0) {
      newFlags |= 1u;
    } else {
      newFlags &= ~1u;
    }
    const u32 newTexObjId = reader.read<u32>();
    const u32 newTexDataVersion = reader.read<u32>();
    if (slot.data != newData || slot.mWidth != newWidth || slot.mHeight != newHeight ||
        slot.mFormat != static_cast<u32>(newFormat) || slot.tlut != newTlut || slot.flags != newFlags ||
        slot.texObjId != newTexObjId || slot.texDataVersion != newTexDataVersion) {
      slot.data = newData;
      slot.mWidth = newWidth;
      slot.mHeight = newHeight;
      slot.mFormat = newFormat;
      slot.tlut = newTlut;
      slot.flags = newFlags;
      slot.texObjId = newTexObjId;
      slot.texDataVersion = newTexDataVersion;
      g_gxState.dirty |= DirtyTextures;
    }
  } else if (subCmd == GX_AURORA_LOAD_TLUT) {
    const auto idx = reader.read<u8>();
    CHECK(idx < MaxTluts, "invalid tlut slot {}", idx);
    auto& slot = g_gxState.loadedTluts[idx];
    const auto newData = reinterpret_cast<const void*>(reader.read<u64>());
    const auto newFormat = static_cast<GXTlutFmt>(reader.read<u32>());
    const u16 newNumEntries = reader.read<u16>();
    const u32 newTlutObjId = reader.read<u32>();
    const u32 newTlutDataVersion = reader.read<u32>();
    const u8 newFlags = slot.flags & ~0x80u; // Reset no-cache flag
    if (slot.data != newData || slot.format != newFormat || slot.numEntries != newNumEntries ||
        slot.tlutObjId != newTlutObjId || slot.tlutDataVersion != newTlutDataVersion || slot.flags != newFlags) {
      if (slot.tlutObjId != newTlutObjId || slot.tlutDataVersion != newTlutDataVersion) {
        texture::invalidate_bindings();
      }
      slot.data = newData;
      slot.format = newFormat;
      slot.numEntries = newNumEntries;
      slot.tlutObjId = newTlutObjId;
      slot.tlutDataVersion = newTlutDataVersion;
      slot.flags = newFlags;
      g_gxState.dirty |= DirtyTextures;
    }
  } else if (subCmd == GX2_SET_POLYGON_OFFSET) {
    const f32 frontOffset = reader.read<f32>();
    const f32 frontScale = reader.read<f32>();
    const f32 backOffset = reader.read<f32>();
    const f32 backScale = reader.read<f32>();
    const f32 clamp = reader.read<f32>();
    if (g_gxState.frontOffset != frontOffset || g_gxState.frontScale != frontScale ||
        g_gxState.backOffset != backOffset || g_gxState.backScale != backScale || g_gxState.clamp != clamp) {
      g_gxState.frontOffset = frontOffset;
      g_gxState.frontScale = frontScale;
      g_gxState.backOffset = backOffset;
      g_gxState.backScale = backScale;
      g_gxState.clamp = clamp;
      g_gxState.dirty |= DirtyPipeline;
    }
  } else if (subCmd == GX_AURORA_LOAD_COPY_SRC) {
    const s32 left = reader.read<s32>();
    const s32 top = reader.read<s32>();
    const s32 width = reader.read<s32>();
    const s32 height = reader.read<s32>();
    g_gxState.texCopySrc = {left, top, width, height};
  } else if (subCmd == GX_AURORA_LOAD_COPY_DST) {
    g_gxState.texCopyDstWidth = reader.read<u32>();
    g_gxState.texCopyDstHeight = reader.read<u32>();
    g_gxState.texCopyFmt = static_cast<GXTexFmt>(reader.read<u32>());
    reader.skip(1); // mipmap is not implemented, but remains part of the command payload
    g_gxState.texCopyDstWide = true;
  } else if (subCmd == GX_AURORA_LOAD_COPY_DEST) {
    g_gxState.texCopyDest = reinterpret_cast<const void*>(reader.read<u64>());
  } else if (subCmd == GX_AURORA_REQUEST_DEPTH_SNAPSHOT) {
    gfx::depth_peek::request_snapshot();
  } else if (subCmd == GX_AURORA_BEGIN_OFFSCREEN) {
    const u32 width = reader.read<u32>();
    const u32 height = reader.read<u32>();
    gfx::begin_offscreen(width, height);
  } else if (subCmd == GX_AURORA_END_OFFSCREEN) {
    gfx::end_offscreen();
  } else if (subCmd == GX_AURORA_DESTROY_TEXOBJ) {
    evict_texture_object(reader.read<u32>());
  } else if (subCmd == GX_AURORA_DESTROY_TLUT) {
    evict_tlut_object(reader.read<u32>());
  } else if (subCmd == GX_AURORA_DESTROY_COPY_TEX) {
    evict_copy_texture(reinterpret_cast<const void*>(reader.read<u64>()));
  } else if (subCmd == GX_AURORA_DRAW_SIZED) {
    const u8 cmd = reader.read<u8>();
    const u32 byteLen = reader.read<u32>();
    const GXVtxFmt fmt = static_cast<GXVtxFmt>(cmd & CP_VAT_MASK);
    const GXPrimitive prim = static_cast<GXPrimitive>(cmd & CP_OPCODE_MASK);
    if (byteLen != 0) {
      u32 vtxSize;
      if (g_gxState.lastVtxFmt == fmt) {
        vtxSize = g_gxState.lastVtxSize;
      } else {
        vtxSize = calc_vtx_size(fmt);
      }
      AURORA_ASSERT(vtxSize != 0 && byteLen % vtxSize == 0,
                    "GX_AURORA_DRAW_SIZED: {} bytes is not a whole number of size-{} vertices", byteLen, vtxSize);
      u32 vtxCount = byteLen / vtxSize;
      AURORA_ASSERT(vtxCount <= 0xFFFF, "GX_AURORA_DRAW_SIZED: too many vertices ({})", vtxCount);
      draw_prim(prim, fmt, static_cast<u16>(vtxCount), reader);
    }
  } else if (subCmd == GX_AURORA_DRAW_INDEXED) {
    ZoneScopedN("DRAW_INDEXED");
    const u8 cmd = reader.read<u8>();
    const u16 vtxCount = reader.read<u16>();
    const u32 indexCount = reader.read<u32>();
    const GXVtxFmt fmt = static_cast<GXVtxFmt>(cmd & CP_VAT_MASK);
    const GXPrimitive prim = static_cast<GXPrimitive>(cmd & CP_OPCODE_MASK);
    AURORA_ASSERT(prim == GX_TRIANGLES, "GX_AURORA_DRAW_INDEXED: primitive must be GX_TRIANGLES, got {}",
                  static_cast<u32>(prim));
    const size_t idxBytes = static_cast<size_t>(indexCount) * sizeof(u16);
    // Index data is always host-endian; push it to the GPU buffer as-is
    const auto indexData = reader.take(idxBytes);
    const gfx::Range idxRange = gfx::push_indices(indexData.data(), indexData.size(), 4);
    u32 vtxSize;
    if (g_gxState.lastVtxFmt == fmt) {
      vtxSize = g_gxState.lastVtxSize;
    } else {
      vtxSize = calc_vtx_size(fmt);
    }
    const u32 totalVtxBytes = vtxCount * vtxSize;
    const auto vertexData = reader.take(totalVtxBytes);
    diag_check_indices(vertexData.data(), vtxCount, fmt, vtxSize);   // smstrikers-port
    const gfx::Range vertRange = gfx::push_verts(vertexData.data(), vertexData.size(), 4);
    if (indexCount != 0) {
      push_gx_draw(prim, fmt, vtxCount, vertRange, idxRange, indexCount, vertexData.data());
    }
  } else if (subCmd == GX_AURORA_DEBUG_GROUP_PUSH) {
    auto label = reader.read_string();
    gfx::push_debug_group(std::move(label));
  } else if (subCmd == GX_AURORA_DEBUG_GROUP_POP) {
    pop_debug_group();
  } else if (subCmd == GX_AURORA_DEBUG_MARKER_INSERT) {
    auto label = reader.read_string();
    gfx::insert_debug_marker(std::move(label));
  } else if (subCmd == GX_AURORA_DECODE_SCOPE) {
    sDecodeScope = reader.read<u8>() != 0;
  } else if (subCmd == GX_AURORA_SET_STEREO) {
    // strikers-quest r43: see GXState::stereo.
    auto& stereo = g_gxState.stereo;
    const bool enabled = reader.read<u8>() != 0;
    if (enabled) {
      for (int eye = 0; eye < 2; ++eye) {
        std::array<Vec4<float>, 3> rows;
        for (auto& row : rows) {
          for (int c = 0; c < 4; ++c) {
            row[c] = reader.read<f32>();
          }
        }
        stereo.t[eye] = Mat3x4<float>{rows[0], rows[1], rows[2]};
      }
      for (int eye = 0; eye < 2; ++eye) {
        for (int c = 0; c < 4; ++c) {
          stereo.clip[eye][c] = reader.read<f32>();
        }
      }
    }
    stereo.enabled = enabled;
    // The uniform block grows or loses its stereo tail; merging must not cross the switch.
    g_gxState.dirty |= DirtyUniform;
  } else if (subCmd == GX_AURORA_XR_EYE_PASS) {
    // strikers-quest r43: the headset's eye pass, in-band (gfx.hpp register_xr_eye_target).
    if (reader.read<u8>() != 0) {
      const u8 slot = reader.read<u8>();
      const u8 samples = reader.read<u8>();
      const u8 coverage = reader.read<u8>();  // r43b: 0 off, 1 over passthrough, 2 over a drawn layer
      gfx::set_offscreen_uses_native_logical_size(true);
      if (gfx::begin_xr_eye_pass(slot, samples)) {
        aurora_gx_set_xr_coverage_mode(coverage);
      } else {
        gfx::set_offscreen_uses_native_logical_size(false);
      }
    } else {
      gfx::end_xr_eye_pass();
      aurora_gx_set_xr_coverage_mode(0);
      gfx::set_offscreen_uses_native_logical_size(false);
      if (g_gxState.xrClip.enabled) {  // r59: never left on past its pass
        g_gxState.xrClip.enabled = false;
        g_gxState.dirty |= DirtyPipeline | DirtyUniform;
      }
    }
  } else if (subCmd == GX_AURORA_XR_COVERAGE) {
    // strikers-quest r44e: the next layer's coverage inside the open eye pass (the life-size scorer
    // over passthrough counts its blended, depth-writing surfaces as solid).
    aurora_gx_set_xr_coverage_mode(reader.read<u8>());
  } else if (subCmd == GX_AURORA_SET_XR_CLIP) {
    // strikers-quest r59: see GXState::xrClip. The draws after it need the clipping (or plain)
    // stereo pipeline and a uniform block with (or without) the planes.
    auto& clip = g_gxState.xrClip;
    const bool enabled = reader.read<u8>() != 0;
    if (enabled) {
      for (auto& plane : clip.planes) {
        for (int c = 0; c < 4; ++c) {
          plane[c] = reader.read<f32>();
        }
      }
    }
    clip.enabled = enabled;
    g_gxState.dirty |= DirtyPipeline | DirtyUniform;
  }

  else {
    Log.error("Unknown Aurora subcommand: {:04X}", subCmd);
  }
}

void clear_draw_cache() noexcept {
  sDrawCache.bindGeneration = 0;
  sDrawCache.uniformRange = {};
  sDrawCache.fogRange = {};
  sDrawCache.hasFogRange = false;
  // strikers-quest: decoded copies live in this frame's vertex pool.
  sDecodeCache.clear();
  sDecodeScope = false;
  sLastDrawDecoded = false;
  sDecodedBytesLastFrame.store(sDecodedBytesFrame.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
}

} // namespace aurora::gx::fifo

extern "C" unsigned long long aurora_xr_decoded_bytes(void) {
  return aurora::gx::fifo::sDecodedBytesTotal.load(std::memory_order_relaxed);
}

extern "C" unsigned long long aurora_xr_decoded_bytes_last_frame(void) {
  return aurora::gx::fifo::sDecodedBytesLastFrame.load(std::memory_order_relaxed);
}

extern "C" void aurora_xr_set_vertex_decode(int mode) {
  aurora::gx::fifo::sDecodeMode.store(mode < 0 ? 0 : (mode > 2 ? 2 : mode), std::memory_order_relaxed);
}

extern "C" void aurora_xr_take_decode_stats(u32 out[6]) {
  auto& s = aurora::gx::fifo::sDecodeStats;
  out[0] = s.decoded.exchange(0, std::memory_order_relaxed);
  out[1] = s.reused.exchange(0, std::memory_order_relaxed);
  out[2] = s.vertices.exchange(0, std::memory_order_relaxed);
  out[3] = s.fallback.exchange(0, std::memory_order_relaxed);
  out[4] = s.ineligible.exchange(0, std::memory_order_relaxed);
  out[5] = s.micros.exchange(0, std::memory_order_relaxed);
}
