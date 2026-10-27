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

// Reads a figure (any thread): `pack` its pictures and mesh, `posePack` its pose file's name (the
// normal pack's: a shiny one shares it). Fills `model` for xrfx::set_model. nullptr when a file is
// missing or not a retarget pack (GPK2).
Figure* load(const std::string& pack, const std::string& posePack, xrfx::Model* model);
void release(Figure* f);

// Standing, in metres (Strikers units): its height and how far it reaches round its middle.
float height(const Figure* f);
float radius(const Figure* f);
// How far it reaches out in front of where it stands (its head held forward, a snout), metres.
float front(const Figure* f);

// Posed `t` seconds into its idle (looped), standing at `feet` (LOCAL), facing `yaw` (radians about +Y;
// 0 faces +X), `scale` times its size, its middle `lift` metres higher (a flier's bob): 6 floats a vertex
// (position, normal) into `out`.
void pose(const Figure* f, float t, const XrVector3f& feet, float yaw, float scale, float lift, std::vector<float>* out);

}  // namespace xrevo
