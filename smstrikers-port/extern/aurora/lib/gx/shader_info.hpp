#pragma once

#include "gx.hpp"

namespace aurora::gx {
ShaderInfo build_shader_info(const ShaderConfig& config) noexcept;
gfx::Range build_uniform(const ShaderInfo& info) noexcept;
bool stereo_uniform_fits(const ShaderInfo& monoInfo, bool xrClip) noexcept;
u8 color_channel(GXChannelID id) noexcept;
}; // namespace aurora::gx
