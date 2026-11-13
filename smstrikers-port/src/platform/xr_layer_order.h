// r242 (Lance 11-13: "the sportsbook arcade machine still shows when the game menu is in front of it"): the order of the two quads that can
// sit UNDER the effects layer together - the franchise hub's menu (when it's under it: its figures / the cards / the magazine / an evolution in
// front of it) and the SPORTSBOOK machine's screen. Quads carry no depth: the compositor paints them in list order, so the farther one goes in
// first and the nearer one last (from your head) - whichever is nearer covers the other. Pure (xr_quest.cpp's PortXrEndFrame; the PC proof).
#pragma once
#include <openxr/openxr.h>

namespace quad_order {
// the menu (at m) nearer your head (h) than the machine's screen (at b)
inline bool nearer(const XrVector3f& h, const XrVector3f& m, const XrVector3f& b) {
  const float mx = m.x - h.x, my = m.y - h.y, mz = m.z - h.z, bx = b.x - h.x, by = b.y - h.y, bz = b.z - h.z;
  return mx * mx + my * my + mz * mz < bx * bx + by * by + bz * bz;
}
// the order used: -1 neither under the effects layer, 0 the machine's screen only (the menu over everything), 1 the menu only, 2 the machine's
// screen then the menu (the menu nearer), 3 the menu then the machine's screen (the machine nearer)
inline int code(bool menuUnder, bool machine, bool menuNearer) {
  if (!menuUnder) return machine ? 0 : -1;
  if (!machine) return 1;
  return menuNearer ? 2 : 3;
}
inline const char* words(int order) {
  static const char* const kW[4] = {"MACHINE'S SCREEN, effects (the menu over everything)", "MENU, effects", "MACHINE'S SCREEN, MENU (nearer), effects",
                                    "MENU, MACHINE'S SCREEN (nearer), effects"};
  return order >= 0 && order < 4 ? kW[order] : "-";
}
}  // namespace quad_order
