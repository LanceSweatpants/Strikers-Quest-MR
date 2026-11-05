// strikers-quest r143: the life-size goal scorer's posed body, from the game thread to the effects layer.
//
// Lance (r142): Arbok had "a see through invisible pole in the back of him ... included for everyone", and the
// characters looked see-through because what falls behind them was drawn over them. The effects layer (xr_fx.cpp)
// is composited over the scorer's layer, and it only knew the scorer as an upright cylinder (r42: collision; r44e: a
// ten-sided depth column). Now it gets the body itself: every game frame the copy is drawn (xr_scorer.cpp) its
// posed vertices in the copy's game space (feet at the origin, facing +X, Z up: the space xr_quest.cpp's
// scorer_model() puts in the room) and its solid triangles - the Strikers character's skin posed on the CPU from
// the same bone matrices its draw uses, or a guest's own CPU-skinned vertices (xr_guest.cpp). xr_fx draws them
// depth-only (what falls behind the body is hidden by it, per pixel) and voxelizes them for the pieces to bounce off
// and land on.
#ifndef PORT_XR_BODY_H
#define PORT_XR_BODY_H

#include <stdint.h>

#include <vector>

// Game thread, while the scorer copy is drawn (PortGuestScorerPose active): a guest drawn in its place hands over
// its skinned vertices (x y z, `stride` floats apart) and its solid triangles (3 indices each into those vertices;
// no gas, no flames, nothing blended). `key` names the guest's mesh: the triangles are only taken when it changes.
void PortXrBodyGuest(const float* xyz, int stride, int numVerts, const uint32_t* tris, int numTris, uintptr_t key);

// Render thread: the body posed since `*gen` (0 = none yet), copied out: its vertices always, its triangles only
// when its mesh is not `*mesh` any more (a serial, never 0 for a real mesh). `postedNs`: when it was posed
// (CLOCK_MONOTONIC). False when nothing newer was posed.
bool PortXrBodyTake(uint64_t* gen, uint32_t* mesh, std::vector<float>* xyz, std::vector<uint32_t>* tris,
                    uint64_t* postedNs);

// r144 (Lance: Arbok "getting too fucking close to me like he was gonna eat me", the banner "right over the goal
// scorers faces"): the posed body's reach, worked out as it is posted, in the copy's game space (+X its front, +Y its
// left, Z up, its feet at the origin): how far it reaches along 8 directions round it (reach[k]: k x 45 degrees from
// its front toward its left: the most of x cos + y sin over its vertices), how high it stands (top: the most z), and
// how high it rises toward its front for the slopes in kPortXrBodySlopes (rise[k]: the most z + slope x), from which
// xr_quest.cpp keeps it out of your face and puts the banner over it as you see it.
constexpr int kPortXrBodySlopeCount = 8;
constexpr float kPortXrBodySlopes[kPortXrBodySlopeCount] = {-0.25f, -0.125f, 0.0f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f};
struct PortXrBodyShape {
  float reach[8];
  float top;
  // rise and side are along the direction you see it from (`view`, PortXrBodySetView: v = x cos + y sin, w = -x sin +
  // y cos; at 0, v = x its front and w = y its left): rise[k] the most z + slope v; side[0][k] the most w + slope v,
  // side[1][k] the most -w + slope v (how far it reaches to each side toward you, for a banner put beside it)
  float rise[kPortXrBodySlopeCount];
  float side[2][kPortXrBodySlopeCount];
  float view;
  uint64_t gen;       // the pose it is of (PortXrBodyTake's)
  uint64_t postedNs;  // when (CLOCK_MONOTONIC)
};
// Any thread: the last posed body's shape. False before the first.
bool PortXrBodyShapeNow(PortXrBodyShape* out);
// Any thread (xr_quest.cpp, once a display frame): the direction you see the body from in its own space (radians from
// its front toward its left: 0 = it faces you; a long body turned side-on by phi is seen from -phi).
void PortXrBodySetView(float angle);

#endif  // PORT_XR_BODY_H
