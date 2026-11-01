// strikers-quest r96 (Lance: "i want to watch that shit occur in 3D"): a Pokemon's 3D figure for the
// franchise evolution, standing in your room in the effects layer (xr_fx.h Scene::models). There is no
// match running then, so no Strikers character to pose it from: its pack (STRIKERS_HOME/guests/<pack>.gpk,
// the shiny pack's pictures for a shiny one) is posed by its standing idle baked on the PC
// (guests/evo/<pack>.gev, tools/outside/evo_pose.py: the game's retarget on its Strikers slot's idle).
//
// Include after the Vulkan/OpenXR platform defines (as xr_fx.h).
#pragma once

#include "xr_fx.h"

#include <string>
#include <vector>

namespace xrevo {

struct Figure;

// r119: a move in a home figure's loop file (frames start..start+frames-1): kind 0 idle, 1 greeting, 2 big move
// (celebration, power, combo), 3 reaction (you touched it), 4 goodbye
struct Segment {
  uint32_t start = 0, frames = 0, kind = 0;
};

// Reads a figure (any thread): `pack` its pictures and mesh, `posePack` its pose file's name (the
// normal pack's: a shiny one shares it). Fills `model` for xrfx::set_model. nullptr when a file is
// missing or not a retarget pack (GPK2).
Figure* load(const std::string& pack, const std::string& posePack, xrfx::Model* model);
// r117: the same from a pack file and a pose file named in full (the home corner's own, guests/home/)
Figure* load_files(const std::string& packPath, const std::string& posePath, xrfx::Model* model);
// r117: how long its pose file runs (seconds)
float duration(const Figure* f);
void release(Figure* f);

// Standing, in metres (Strikers units): its height and how far it reaches round its middle.
float height(const Figure* f);
float radius(const Figure* f);
// How far it reaches out in front of where it stands (its head held forward, a snout), metres.
float front(const Figure* f);

// Posed `t` seconds into its idle (looped), standing at `feet` (LOCAL), facing `yaw` (radians about +Y;
// 0 faces +X), `scale` times its size, its middle `lift` metres higher (a flier's bob): 6 floats a vertex
// (position, normal) into `out`.
// r119: its head (and what's under it) turned lookYaw / lookPitch (radians, its own: 0 = straight ahead) toward you.
void pose(const Figure* f, float t, const XrVector3f& feet, float yaw, float scale, float lift, std::vector<float>* out,
          float lookYaw = 0.0f, float lookPitch = 0.0f);

// r119: its moves (none: a plain loop), frames a second, and whether it can look at you
const std::vector<Segment>& segments(const Figure* f);
float fps(const Figure* f);
bool looks(const Figure* f);

}  // namespace xrevo
