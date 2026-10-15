// strikers-quest: the online 1v1 (include/port/xr_net.h), the side xr_quest.cpp drives.
#pragma once

#include <cstdint>

namespace xrnet {

// One pad state as it travels between the headsets (PADStatus without the error/analog fields).
struct NetPad {
  uint16_t buttons = 0;
  int8_t stickX = 0, stickY = 0, substickX = 0, substickY = 0;
  uint8_t triggerLeft = 0, triggerRight = 0;
};
static_assert(sizeof(NetPad) == 8, "net pad layout");

// Every headset frame: talk to the room (connects on first use).
void poll();

// True from the online match's first game frame until it ends: the game frames are gated by step().
bool lockstep();

// A game frame is due. Takes this headset's pad for the frame `delay` frames ahead, and returns
// true with both pads (home = pad 0, away = pad 1) when this frame can run, false to wait for the
// friend's input. strikers-quest r39b: the frame is used up only by PortNetCommitFrame (main.cpp,
// once the game frame really runs); until then every step() hands out that same frame again.
bool step(const NetPad& local, NetPad* pad0, NetPad* pad1);

// The pad this headset's player uses (0 offline or host, 1 guest).
int local_pad();

// strikers-quest r47: the match start is waiting for the friend (the loading screen): 0 not
// waiting, 1 nobody else in the room yet, 2 the friend is in the room but hasn't started a Friendly.
int waiting_for_friend();
// The player stops waiting: this match goes ahead offline, against the computer.
void cancel_wait();
// The room this headset is in ("" none).
const char* room_code();

}  // namespace xrnet
