// strikers-quest r197 (Lance: "can I scan the room I'm going to be in and have [the weather etc] occur with real
// collision - record a living room space in my headset, then have Strikers use that space as my environment?"):
// YOUR ROOM SCAN as a cheap collision model for the MR room effects (xr_fx's room weather, confetti, embers, xr_goalfx's
// debris). Built once from the headset's own scene data (xr_scene.cpp reads it through XR_FB_scene: the floor, ceiling and
// wall planes, furniture planes / boxes, and the room's triangle mesh when there is one) and queried every frame:
//   - a grid of the highest up-facing surface per cell (table tops, couch seats, shelves, sills; the floor under it),
//   - the walls as vertical segments (inward normals), the ceiling height, the floor outline,
//   - snow-pile spots (corners, along the walls, on furniture tops).
// LOCAL space (meters, Y up) as it was when built; set_frame() follows a recenter (a yaw + a move) without rebuilding.
// Pure C++ with no library calls (only compiler builtins): the same file is compiled for the PC tests
// (tools/roomscan197/test, wasm32 + node) that prove it. Fixed-size arrays: no allocation after the Room itself.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace xrroom {

// Meta's semantic labels (XR_FB_scene "TABLE,DESK" ...), as bits
enum : uint16_t {
  kLabFloor = 1u << 0,
  kLabCeiling = 1u << 1,
  kLabWall = 1u << 2,
  kLabTable = 1u << 3,
  kLabCouch = 1u << 4,
  kLabWindow = 1u << 5,
  kLabDoor = 1u << 6,
  kLabStorage = 1u << 7,
  kLabBed = 1u << 8,
  kLabScreen = 1u << 9,
  kLabLamp = 1u << 10,
  kLabPlant = 1u << 11,
  kLabArt = 1u << 12,
  kLabMesh = 1u << 13,     // GLOBAL_MESH (the room's triangle mesh)
  kLabInvisible = 1u << 14,  // INVISIBLE_WALL_FACE (an open side of the room: a boundary, nothing to pile against)
  kLabOther = 1u << 15,
};
// "TABLE,DESK" / "COUCH" ... -> bits (unknown words -> kLabOther); null / empty -> 0
uint16_t parse_labels(const char* csv);
// the label's word for a log line ("table", "couch", ... the first bit set)
const char* label_name(uint16_t labels);

struct Pose {
  float p[3] = {0.0f, 0.0f, 0.0f};
  float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // x y z w
};
// A plane (XR_FB_scene: BOUNDED_2D): it lies in its pose's x/y plane, its normal +Z. rect = bounding box 2D (x, y, width,
// height); boundary = its outline (x, y pairs, plane space), optional.
struct InPlane {
  Pose pose;
  float rect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float* boundary = nullptr;
  int boundaryCount = 0;  // vertices
  uint16_t labels = 0;
};
// A box (BOUNDED_3D): offset x y z + extent w h d in its pose's space.
struct InVolume {
  Pose pose;
  float box[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  uint16_t labels = 0;
};
// A triangle mesh (XR_META_spatial_entity_mesh: the GLOBAL_MESH), vertices in its pose's space.
struct InMesh {
  Pose pose;
  const float* verts = nullptr;
  int vertCount = 0;
  const uint32_t* idx = nullptr;
  int idxCount = 0;
};
struct Input {
  const InPlane* planes = nullptr;
  int planeCount = 0;
  const InVolume* volumes = nullptr;
  int volumeCount = 0;
  const InMesh* meshes = nullptr;
  int meshCount = 0;
  float head[3] = {0.0f, 1.6f, 0.0f};  // where you are (LOCAL): the room you stand in, which way walls face
};

constexpr int kMaxOutline = 64, kMaxWalls = 48, kMaxBoxes = 64, kMaxFeatures = 32, kMaxSpots = 64;
constexpr int kGrid = 160;  // cells a side at most (5 cm cells up to an 8 m room; bigger rooms get bigger cells)
constexpr float kNoTop = -1.0e30f;

struct Wall {
  float x0, z0, x1, z1;  // ends (xz)
  float dx, dz, len;     // unit along it, its length
  float nx, nz;          // into the room
  float y0, y1;
  uint16_t labels;
};
struct Box {  // furniture: a footprint turned about +Y (centre, unit axis u, half sizes along u / across), heights
  float cx, cz, ux, uz, hu, hv, y0, y1;
  uint16_t labels;
};
struct Feature {  // a window / door / picture on a wall (xz ends, heights, its normal)
  float x0, z0, x1, z1, y0, y1, nx, nz;
  uint16_t labels;
};
// Where snow piles up: kind 0 a corner, 1 along a wall, 2 on a furniture top, 3 a window sill. (nx, nz): toward the wall
// it leans on (kinds 0, 1, 3; 0 for a top). weight: how fast it fills (0..1). size: how wide it grows (m).
struct Spot {
  float x, y, z, nx, nz, weight, size;
  uint8_t kind;
  uint16_t labels;
};

struct Room {
  bool valid = false;
  float floorY = 0.0f, ceilY = 2.5f;
  bool hasCeiling = false, hasMesh = false;
  int outlineN = 0;
  float outline[kMaxOutline][2] = {};
  int wallN = 0, boxN = 0, featureN = 0, spotN = 0;
  Wall walls[kMaxWalls] = {};
  Box boxes[kMaxBoxes] = {};
  Feature features[kMaxFeatures] = {};
  Spot spots[kMaxSpots] = {};
  // the grid (build frame): cell (i, j) covers x gx0 + i * cell .., z gz0 + j * cell ..
  float gx0 = 0.0f, gz0 = 0.0f, cell = 0.05f;
  int gw = 0, gh = 0;
  float top[kGrid * kGrid];       // the highest up-facing surface over the floor there (kNoTop: none)
  uint16_t topLab[kGrid * kGrid];
  // the mesh's own heights (build scratch): where the mesh saw something, it wins over a box's flat top (a couch box
  // is as high as its back; the mesh has the seat)
  float meshTop[kGrid * kGrid];
  // a second, lower surface under the top where the mesh saw open space between (a table's lower shelf, a sill under a
  // curtain rod); kNoTop: the top is solid down to the floor
  float top2[kGrid * kGrid];
  uint8_t inside[kGrid * kGrid];  // 1: in the floor outline
  // counts for the status line / the log
  int nWalls = 0, nTables = 0, nCouches = 0, nStorage = 0, nBeds = 0, nOther = 0, nWindows = 0, nDoors = 0;
  int meshTris = 0, meshTopTris = 0, topCells = 0, insideCells = 0;
  // build frame -> now: x' = c x + s z + t.x, y' = y + t.y, z' = -s x + c z + t.z (set_frame)
  float fc = 1.0f, fs = 0.0f, ft[3] = {0.0f, 0.0f, 0.0f};
};

// Builds `r` from the scan (r.valid false when there is no floor to stand on). ~1-3 ms for a room with a mesh.
void build(Room& r, const Input& in);
// A recenter moved LOCAL: the build frame shows in the new LOCAL turned by (cos, sin) of a yaw about +Y and moved by t.
void set_frame(Room& r, float c, float s, float tx, float ty, float tz);

// ---- queries (LOCAL now) ----
float floor_y(const Room& r);
float ceiling_y(const Room& r);
// in the scanned floor outline (inside the walls)
bool inside(const Room& r, float x, float z);
// the surface under (x, z) that something at height y comes down onto: the furniture top there when y is above it,
// else the floor. *labels (optional): what it is (kLabTable, kLabCouch, kLabFloor ...)
float surface_below(const Room& r, float x, float y, float z, uint16_t* labels);
// the furniture top at (x, z) (kNoTop: just floor there)
float top_at(const Room& r, float x, float z);
// A moving piece (sphere `radius`) against the room, from `prev` to `p` this step: pushed out and its velocity bounced
// (`bounce` of the speed into the surface back, `friction` of the speed along it kept). 0 nothing, 1 it landed on the
// floor / a top (p[1] on it), 2 a wall, 3 the ceiling, 4 a furniture side. *labels: what it hit.
enum { kHitNone = 0, kHitTop = 1, kHitWall = 2, kHitCeiling = 3, kHitSide = 4 };
// what to collide with (bits; kCollAll every one): pieces with their own landing rules take the walls / sides only
enum : unsigned { kCollWalls = 1, kCollCeiling = 2, kCollSides = 4, kCollTops = 8, kCollAll = 15 };
int collide(const Room& r, float p[3], float v[3], const float prev[3], float radius, float bounce, float friction,
            uint16_t* labels, unsigned what = kCollAll);
// spot i in LOCAL now (x y z, and its lean nx nz)
void spot_now(const Room& r, int i, float out[5]);
// "4 WALLS, FLOOR, CEILING, 1 TABLE, 1 COUCH, 2 MORE, MESH" (for the pause row and the log)
void summary(const Room& r, char* buf, size_t n);

}  // namespace xrroom
