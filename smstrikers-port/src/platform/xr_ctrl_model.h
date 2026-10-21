// strikers-quest r54: the headset's own controller models (XR_FB_render_model), for the controllers
// drawn in your hands (xr_fx.cpp). Lance on r53's built ones: "fat as hell and looked like a fake ass
// controller ... just make em look like fancy quest 3 controllers, themed around whoevers team".
//
// The runtime hands out each connected controller's model as binary glTF with its origin at the grip
// pose. A thread here loads both (retrying until they are connected), saves them beside the log
// (xr_ctrl_left.glb / xr_ctrl_right.glb) and turns them into one triangle list each, in grip space,
// split into parts by glTF node (or, for a skinned model, by each vertex's strongest joint): the body
// and the stick, face buttons, trigger and grip, which move with what you press.
//
// Include after the OpenXR platform defines, like xr_fx.h.
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <string>
#include <vector>

#include "xr_fx_mesh.h"

namespace xrctrl {

enum PartKind : uint8_t { kBody, kStick, kButton1, kButton2, kTrigger, kGrip, kMenu, kPartKinds };

struct Part {
  PartKind kind = kBody;
  uint32_t firstIndex = 0, indexCount = 0;
  float pivot[3] = {0.0f, 0.0f, 0.0f};    // grip space: where it turns about (its joint's origin)
  float centre[3] = {0.0f, 0.0f, 0.0f};   // grip space: the middle of its vertices
  // r54b: fully pressed, from the model's own animation (the key farthest from rest): a turn about
  // the pivot and a move, both in grip space. hasPress false = not found (xr_fx falls back).
  bool hasPress = false;
  float pressTurn[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float pressMove[3] = {0.0f, 0.0f, 0.0f};
};

struct Model {
  std::string name;  // the runtime's model name ("Meta Quest Touch Plus Controller Right", ...)
  std::vector<xrfx::mesh::Vertex> verts;
  std::vector<uint32_t> indices;
  std::vector<Part> parts;
  float faceUp[3] = {0.0f, 0.0f, -1.0f};  // grip space: out of the face (a face button's press, reversed)
  float faceFwd[3] = {0.0f, -1.0f, 0.0f}; // grip space: across the face toward the trigger (stick "up")
};

// True when the runtime offers XR_FB_render_model (create_instance enables it then).
const char* extension_name();

// After the session began: starts the loader thread (once). `systemSupports` from
// XrSystemRenderModelPropertiesFB.
void start(XrInstance instance, XrSession session, bool systemSupports);
void stop();

// A model for hand 0 (left) / 1 (right) loaded since the last call: moved into `out`, true once.
bool take(int hand, Model* out);

}  // namespace xrctrl
