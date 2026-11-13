// strikers-quest r197: YOUR ROOM SCAN (Quest Settings > Physical space > Space setup) read through OpenXR - Meta's scene
// (XR_FB_spatial_entity + _query + _storage + _container, XR_FB_scene, XR_FB_scene_capture, XR_META_spatial_entity_mesh)
// - and turned into xr_room.h's collision model for the MR room effects. Asynchronous: the queries answer through
// xrPollEvent (on_event); the model is built on a worker thread and swapped in by frame(). Nothing here without the
// headset's scene permission (com.oculus.permission.USE_SCENE) or without a scan: room() stays null and every effect
// keeps the floor plane (as before r197).
#pragma once

#include <jni.h>
#include <openxr/openxr.h>

#include <cstddef>
#include <vector>

#include "xr_room.h"

namespace xrscene {

// the extensions to enable when the runtime lists them (xr_quest.cpp create_instance)
const char* const* wanted_extensions(int* count);
// create_instance: which of them were enabled
void set_extension_enabled(const char* name);

// after the session is created (and again harmlessly): the instance, session, LOCAL, the JVM + the game's activity
void start(XrInstance instance, XrSession session, XrSpace local, JavaVM* vm, jobject activity);
void stop();
// every xrPollEvent event: true when it was one of ours
bool on_event(const void* eventBuffer);
// the app has focus again (back from the Quest menu / Space setup): look again, the scan may have changed
void on_focus();
// once a display frame (the game thread): `use` = in MR with USE MY ROOM SCAN on; `head` (LOCAL) picks the room you're
// in. Follows a recenter.
void frame(XrTime displayTime, bool use, const float head[3]);
// r225: your real floor's height (LOCAL, the Guardian's STAGE) when the headset knows it: a scanned room whose floor
// isn't there (an old scan, another storey) is never used. Call before frame().
void floor_hint(float y);
// the room for this frame's effects (null: none / off / not ready). r225: every scanned room is built; this is the one
// you stand in (xr_roompick.h), kept through a "look again" until a new read replaces it.
const xrroom::Room* room();
// r225: how many scanned rooms are built, and which is in use (-1: none - you're outside every one)
int room_count();
int room_index();

enum State {
  kUnsupported = 0,  // the runtime has no scene extensions
  kNoPermission,     // spatial data not allowed (yet)
  kLooking,          // asked the headset, waiting
  kNoScan,           // no room set up
  kReady,            // a room is in use
  kFailed,           // the headset said no / the scan had no floor
};
// (r225: kReady while a room you're in is in use - also while it looks again; kNoScan when rooms are built but you're in
// none of them: "this room isn't scanned", A opens Space setup)
State state();
// the status line for SETTINGS > ROOM (upper case, short)
void status(char* buf, size_t n);
// the pause row's A: ask for the permission (no permission), open Space setup (no scan), else look again
void act();

}  // namespace xrscene
