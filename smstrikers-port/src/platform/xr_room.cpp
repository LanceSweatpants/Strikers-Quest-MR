// strikers-quest r197: your room scan as a collision model (xr_room.h). No library calls: compiler builtins only, so the
// PC test (tools/roomscan197/test: wasm32 + node) runs this very file.
#include "xr_room.h"

namespace xrroom {
namespace {

inline float fsqrt(float x) { return __builtin_sqrtf(x > 0.0f ? x : 0.0f); }
inline float fabs_(float x) { return __builtin_fabsf(x); }
inline float ffloor(float x) { return __builtin_floorf(x); }
inline float fmin_(float a, float b) { return a < b ? a : b; }
inline float fmax_(float a, float b) { return a > b ? a : b; }
inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }

// v' = q v q*
void rotate(const float q[4], const float v[3], float out[3]) {
  const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
  const float tx = 2.0f * (qy * v[2] - qz * v[1]);
  const float ty = 2.0f * (qz * v[0] - qx * v[2]);
  const float tz = 2.0f * (qx * v[1] - qy * v[0]);
  out[0] = v[0] + qw * tx + (qy * tz - qz * ty);
  out[1] = v[1] + qw * ty + (qz * tx - qx * tz);
  out[2] = v[2] + qw * tz + (qx * ty - qy * tx);
}
void transform(const Pose& p, const float v[3], float out[3]) {
  rotate(p.q, v, out);
  out[0] += p.p[0];
  out[1] += p.p[1];
  out[2] += p.p[2];
}

bool same_word(const char* a, const char* aEnd, const char* w) {
  while (a < aEnd && *w != 0) {
    char c = *a;
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c != *w) return false;
    ++a;
    ++w;
  }
  return a == aEnd && *w == 0;
}

// crossing test, polygon (n x 2)
bool in_poly(const float (*poly)[2], int n, float x, float z) {
  bool in = false;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    const float xi = poly[i][0], zi = poly[i][1], xj = poly[j][0], zj = poly[j][1];
    if ((zi > z) != (zj > z) && x < (xj - xi) * (z - zi) / (zj - zi) + xi) in = !in;
  }
  return in;
}

int cell_of(const Room& r, float bx, float bz) {
  const int i = static_cast<int>(ffloor((bx - r.gx0) / r.cell));
  const int j = static_cast<int>(ffloor((bz - r.gz0) / r.cell));
  if (i < 0 || j < 0 || i >= r.gw || j >= r.gh) return -1;
  return j * r.gw + i;
}

void to_build(const Room& r, float x, float z, float& bx, float& bz) {
  const float dx = x - r.ft[0], dz = z - r.ft[2];
  bx = r.fc * dx - r.fs * dz;
  bz = r.fs * dx + r.fc * dz;
}
void to_now(const Room& r, float bx, float bz, float& x, float& z) {
  x = r.fc * bx + r.fs * bz + r.ft[0];
  z = -r.fs * bx + r.fc * bz + r.ft[2];
}
void vec_to_build(const Room& r, float x, float z, float& bx, float& bz) {
  bx = r.fc * x - r.fs * z;
  bz = r.fs * x + r.fc * z;
}
void vec_to_now(const Room& r, float bx, float bz, float& x, float& z) {
  x = r.fc * bx + r.fs * bz;
  z = -r.fs * bx + r.fc * bz;
}

void put_top(Room& r, int c, float y, uint16_t lab) {
  if (y < r.floorY + 0.03f || y > r.ceilY - 0.05f) return;  // a rug / the ceiling itself: not a top
  if (y > r.top[c] + 0.01f) {
    r.top[c] = y;
    r.topLab[c] = lab;
  } else if (y > r.top[c] - 0.04f && (r.topLab[c] & kLabMesh) != 0 && (lab & kLabMesh) == 0) {
    r.topLab[c] = lab;  // the mesh and a labelled plane / box agree: keep the label (a table, not "mesh")
  }
}

// the plane's corners (rect) / its outline in LOCAL
int plane_points(const InPlane& pl, float (*out)[3], int cap) {
  int n = 0;
  if (pl.boundary != nullptr && pl.boundaryCount >= 3) {
    for (int i = 0; i < pl.boundaryCount && n < cap; ++i) {
      const float v[3] = {pl.boundary[2 * i], pl.boundary[2 * i + 1], 0.0f};
      transform(pl.pose, v, out[n++]);
    }
  } else if (pl.rect[2] > 0.0f && pl.rect[3] > 0.0f) {
    const float x0 = pl.rect[0], y0 = pl.rect[1], x1 = pl.rect[0] + pl.rect[2], y1 = pl.rect[1] + pl.rect[3];
    const float c[4][3] = {{x0, y0, 0.0f}, {x1, y0, 0.0f}, {x1, y1, 0.0f}, {x0, y1, 0.0f}};
    for (int i = 0; i < 4 && n < cap; ++i) transform(pl.pose, c[i], out[n++]);
  }
  return n;
}

void plane_normal(const InPlane& pl, float n[3]) {
  const float z[3] = {0.0f, 0.0f, 1.0f};
  rotate(pl.pose.q, z, n);
}

// a vertical plane -> its two ends along the horizontal and its heights
bool vertical_span(const InPlane& pl, float& x0, float& z0, float& x1, float& z1, float& y0, float& y1, float& nx, float& nz) {
  float pts[64][3];
  const int n = plane_points(pl, pts, 64);
  if (n < 2) return false;
  float nrm[3];
  plane_normal(pl, nrm);
  float hl = fsqrt(nrm[0] * nrm[0] + nrm[2] * nrm[2]);
  if (hl < 1e-4f) return false;
  nx = nrm[0] / hl;
  nz = nrm[2] / hl;
  const float ax = -nz, az = nx;  // along it
  float tMin = 1e30f, tMax = -1e30f;
  y0 = 1e30f;
  y1 = -1e30f;
  for (int i = 0; i < n; ++i) {
    const float t = pts[i][0] * ax + pts[i][2] * az;
    tMin = fmin_(tMin, t);
    tMax = fmax_(tMax, t);
    y0 = fmin_(y0, pts[i][1]);
    y1 = fmax_(y1, pts[i][1]);
  }
  // the plane's own offset across (its position along the normal)
  float off = 0.0f;
  for (int i = 0; i < n; ++i) off += pts[i][0] * nx + pts[i][2] * nz;
  off /= static_cast<float>(n);
  x0 = ax * tMin + nx * off;
  z0 = az * tMin + nz * off;
  x1 = ax * tMax + nx * off;
  z1 = az * tMax + nz * off;
  return tMax - tMin > 0.05f;
}

void add_spot(Room& r, float x, float y, float z, float nx, float nz, float weight, float size, uint8_t kind, uint16_t lab) {
  if (r.spotN < kMaxSpots) {
    r.spots[r.spotN++] = Spot{x, y, z, nx, nz, weight, size, kind, lab};
    return;
  }
  int worst = 0;  // full: replace the lightest one if this one is heavier
  for (int i = 1; i < r.spotN; ++i)
    if (r.spots[i].weight < r.spots[worst].weight) worst = i;
  if (r.spots[worst].weight < weight) r.spots[worst] = Spot{x, y, z, nx, nz, weight, size, kind, lab};
}

bool near_door(const Room& r, float x, float z) {
  for (int i = 0; i < r.featureN; ++i) {
    const Feature& f = r.features[i];
    if ((f.labels & kLabDoor) == 0) continue;
    const float dx = f.x1 - f.x0, dz = f.z1 - f.z0, l2 = dx * dx + dz * dz;
    if (l2 < 1e-6f) continue;
    float t = ((x - f.x0) * dx + (z - f.z0) * dz) / l2;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float px = f.x0 + dx * t, pz = f.z0 + dz * t;
    if ((x - px) * (x - px) + (z - pz) * (z - pz) < 0.45f * 0.45f) return true;  // the doorway and a step either side
  }
  return false;
}

float cell_top(const Room& r, float bx, float bz) {
  const int c = cell_of(r, bx, bz);
  return c < 0 ? kNoTop : r.top[c];
}

}  // namespace

uint16_t parse_labels(const char* csv) {
  if (csv == nullptr) return 0;
  uint16_t bits = 0;
  const char* s = csv;
  while (*s != 0) {
    while (*s == ',' || *s == ' ') ++s;
    const char* e = s;
    while (*e != 0 && *e != ',') ++e;
    const char* w = e;
    while (w > s && w[-1] == ' ') --w;
    if (w > s) {
      if (same_word(s, w, "FLOOR")) bits |= kLabFloor;
      else if (same_word(s, w, "CEILING")) bits |= kLabCeiling;
      else if (same_word(s, w, "WALL_FACE") || same_word(s, w, "INNER_WALL_FACE")) bits |= kLabWall;
      else if (same_word(s, w, "INVISIBLE_WALL_FACE")) bits |= kLabWall | kLabInvisible;
      else if (same_word(s, w, "TABLE") || same_word(s, w, "DESK")) bits |= kLabTable;
      else if (same_word(s, w, "COUCH") || same_word(s, w, "SOFA")) bits |= kLabCouch;
      else if (same_word(s, w, "WINDOW_FRAME")) bits |= kLabWindow;
      else if (same_word(s, w, "DOOR_FRAME")) bits |= kLabDoor;
      else if (same_word(s, w, "STORAGE")) bits |= kLabStorage;
      else if (same_word(s, w, "BED")) bits |= kLabBed;
      else if (same_word(s, w, "SCREEN")) bits |= kLabScreen;
      else if (same_word(s, w, "LAMP")) bits |= kLabLamp;
      else if (same_word(s, w, "PLANT")) bits |= kLabPlant;
      else if (same_word(s, w, "WALL_ART")) bits |= kLabArt;
      else if (same_word(s, w, "GLOBAL_MESH")) bits |= kLabMesh;
      else bits |= kLabOther;
    }
    s = e;
  }
  return bits;
}

const char* label_name(uint16_t l) {
  if (l & kLabTable) return "table";
  if (l & kLabCouch) return "couch";
  if (l & kLabBed) return "bed";
  if (l & kLabStorage) return "storage";
  if (l & kLabScreen) return "screen";
  if (l & kLabLamp) return "lamp";
  if (l & kLabPlant) return "plant";
  if (l & kLabWindow) return "window";
  if (l & kLabDoor) return "door";
  if (l & kLabArt) return "picture";
  if (l & kLabInvisible) return "open side";
  if (l & kLabWall) return "wall";
  if (l & kLabCeiling) return "ceiling";
  if (l & kLabFloor) return "floor";
  if (l & kLabMesh) return "mesh";
  if (l & kLabOther) return "other";
  return "";
}

void build(Room& r, const Input& in) {
  r.valid = false;
  r.hasCeiling = r.hasMesh = false;
  r.outlineN = r.wallN = r.boxN = r.featureN = r.spotN = 0;
  r.nWalls = r.nTables = r.nCouches = r.nStorage = r.nBeds = r.nOther = r.nWindows = r.nDoors = 0;
  r.meshTris = r.meshTopTris = r.topCells = r.insideCells = 0;
  r.fc = 1.0f;
  r.fs = 0.0f;
  r.ft[0] = r.ft[1] = r.ft[2] = 0.0f;
  r.gw = r.gh = 0;
  const float hx = in.head[0], hz = in.head[2];

  // ---- the floor: the FLOOR plane you stand over (else the first; else the lowest up-facing plane)
  int floorPl = -1;
  for (int i = 0; i < in.planeCount; ++i) {
    const InPlane& pl = in.planes[i];
    if ((pl.labels & kLabFloor) == 0) continue;
    float pts[kMaxOutline][3];
    const int n = plane_points(pl, pts, kMaxOutline);
    float poly[kMaxOutline][2];
    for (int k = 0; k < n; ++k) {
      poly[k][0] = pts[k][0];
      poly[k][1] = pts[k][2];
    }
    if (floorPl < 0 || (n >= 3 && in_poly(poly, n, hx, hz))) floorPl = i;
  }
  if (floorPl < 0) {
    for (int i = 0; i < in.planeCount; ++i) {
      float nrm[3];
      plane_normal(in.planes[i], nrm);
      if (nrm[1] > 0.9f && (floorPl < 0 || in.planes[i].pose.p[1] < in.planes[floorPl].pose.p[1])) floorPl = i;
    }
  }
  if (floorPl < 0) return;  // nothing to stand on: no room
  {
    const InPlane& fl = in.planes[floorPl];
    r.floorY = fl.pose.p[1];
    float pts[kMaxOutline][3];
    const int n = plane_points(fl, pts, kMaxOutline);
    if (n >= 3) {
      for (int k = 0; k < n; ++k) {
        r.outline[k][0] = pts[k][0];
        r.outline[k][1] = pts[k][2];
      }
      r.outlineN = n;
    }
  }
  r.ceilY = r.floorY + 2.5f;
  for (int i = 0; i < in.planeCount; ++i) {
    if ((in.planes[i].labels & kLabCeiling) != 0 && in.planes[i].pose.p[1] > r.floorY + 1.6f) {
      r.ceilY = in.planes[i].pose.p[1];
      r.hasCeiling = true;
      break;
    }
  }
  // a middle for "inward": the outline's, else your head
  float midX = hx, midZ = hz;
  if (r.outlineN >= 3) {
    midX = midZ = 0.0f;
    for (int k = 0; k < r.outlineN; ++k) {
      midX += r.outline[k][0];
      midZ += r.outline[k][1];
    }
    midX /= static_cast<float>(r.outlineN);
    midZ /= static_cast<float>(r.outlineN);
  }

  // ---- walls + what's on them; furniture tops given as planes
  float topPolys[16][kMaxOutline][2];
  float topPolyY[16];
  int topPolyN[16];
  uint16_t topPolyLab[16];
  int topPolys_ = 0;
  for (int i = 0; i < in.planeCount; ++i) {
    const InPlane& pl = in.planes[i];
    if (i == floorPl || (pl.labels & (kLabFloor | kLabCeiling)) != 0) continue;
    float nrm[3];
    plane_normal(pl, nrm);
    if (fabs_(nrm[1]) < 0.5f) {
      float x0, z0, x1, z1, y0, y1, nx, nz;
      if (!vertical_span(pl, x0, z0, x1, z1, y0, y1, nx, nz)) continue;
      const float mx = 0.5f * (x0 + x1), mz = 0.5f * (z0 + z1);
      if ((midX - mx) * nx + (midZ - mz) * nz < 0.0f) {
        nx = -nx;
        nz = -nz;
      }
      if ((pl.labels & kLabWall) != 0) {
        if (r.wallN < kMaxWalls) {
          Wall& w = r.walls[r.wallN++];
          w.x0 = x0;
          w.z0 = z0;
          w.x1 = x1;
          w.z1 = z1;
          w.len = fsqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
          w.dx = (x1 - x0) / w.len;
          w.dz = (z1 - z0) / w.len;
          w.nx = nx;
          w.nz = nz;
          w.y0 = fmin_(y0, r.floorY);
          w.y1 = fmax_(y1, r.ceilY);
          w.labels = pl.labels;
          if ((pl.labels & kLabInvisible) == 0) ++r.nWalls;
        }
      } else if (r.featureN < kMaxFeatures) {
        r.features[r.featureN++] = Feature{x0, z0, x1, z1, y0, y1, nx, nz, pl.labels};
        if (pl.labels & kLabWindow) ++r.nWindows;
        if (pl.labels & kLabDoor) ++r.nDoors;
      }
    } else if (nrm[1] > 0.7f && topPolys_ < 16) {
      float pts[kMaxOutline][3];
      const int n = plane_points(pl, pts, kMaxOutline);
      if (n < 3) continue;
      for (int k = 0; k < n; ++k) {
        topPolys[topPolys_][k][0] = pts[k][0];
        topPolys[topPolys_][k][1] = pts[k][2];
      }
      topPolyY[topPolys_] = pl.pose.p[1];
      topPolyN[topPolys_] = n;
      topPolyLab[topPolys_] = pl.labels;
      ++topPolys_;
    }
  }
  // no floor outline: from the walls' ends, round the middle (by angle, no trig: a pseudo-angle)
  if (r.outlineN < 3 && r.wallN >= 3) {
    float pts[2 * kMaxWalls][2];
    float key[2 * kMaxWalls];
    int n = 0;
    for (int i = 0; i < r.wallN && n + 1 < 2 * kMaxWalls; ++i) {
      if (r.walls[i].labels & kLabInvisible) continue;
      pts[n][0] = r.walls[i].x0;
      pts[n][1] = r.walls[i].z0;
      ++n;
      pts[n][0] = r.walls[i].x1;
      pts[n][1] = r.walls[i].z1;
      ++n;
    }
    for (int i = 0; i < n; ++i) {
      const float dx = pts[i][0] - midX, dz = pts[i][1] - midZ;
      const float p = dx / (fabs_(dx) + fabs_(dz) + 1e-6f);  // -1..1
      key[i] = dz < 0.0f ? 3.0f + p : 1.0f - p;              // 0..4 round the circle
    }
    for (int i = 1; i < n; ++i) {  // sort by key
      for (int j = i; j > 0 && key[j] < key[j - 1]; --j) {
        const float k = key[j];
        key[j] = key[j - 1];
        key[j - 1] = k;
        const float a = pts[j][0], b = pts[j][1];
        pts[j][0] = pts[j - 1][0];
        pts[j][1] = pts[j - 1][1];
        pts[j - 1][0] = a;
        pts[j - 1][1] = b;
      }
    }
    for (int i = 0; i < n && r.outlineN < kMaxOutline; ++i) {
      if (r.outlineN > 0) {  // the shared corner of two walls: once
        const float dx = pts[i][0] - r.outline[r.outlineN - 1][0], dz = pts[i][1] - r.outline[r.outlineN - 1][1];
        if (dx * dx + dz * dz < 0.15f * 0.15f) continue;
      }
      r.outline[r.outlineN][0] = pts[i][0];
      r.outline[r.outlineN][1] = pts[i][1];
      ++r.outlineN;
    }
  }
  if (r.outlineN < 3) return;  // a floor without an edge: nothing to collide with
  // walls from the outline's edges when the scan gave none
  if (r.wallN == 0) {
    for (int k = 0; k < r.outlineN && r.wallN < kMaxWalls; ++k) {
      const float* a = r.outline[k];
      const float* b = r.outline[(k + 1) % r.outlineN];
      const float len = fsqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));
      if (len < 0.05f) continue;
      Wall& w = r.walls[r.wallN++];
      w.x0 = a[0];
      w.z0 = a[1];
      w.x1 = b[0];
      w.z1 = b[1];
      w.len = len;
      w.dx = (b[0] - a[0]) / len;
      w.dz = (b[1] - a[1]) / len;
      w.nx = -w.dz;
      w.nz = w.dx;
      const float mx = 0.5f * (a[0] + b[0]), mz = 0.5f * (a[1] + b[1]);
      if ((midX - mx) * w.nx + (midZ - mz) * w.nz < 0.0f) {
        w.nx = -w.nx;
        w.nz = -w.nz;
      }
      w.y0 = r.floorY;
      w.y1 = r.ceilY;
      w.labels = kLabWall;
      ++r.nWalls;
    }
  }

  // ---- furniture boxes (gravity-aligned: the box axis nearest +Y is up)
  for (int i = 0; i < in.volumeCount && r.boxN < kMaxBoxes; ++i) {
    const InVolume& v = in.volumes[i];
    if ((v.labels & (kLabFloor | kLabCeiling | kLabWall)) != 0) continue;
    if (v.box[3] <= 0.0f || v.box[4] <= 0.0f || v.box[5] <= 0.0f) continue;
    const float ax[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    float w[3][3];
    int up = 0;
    for (int a = 0; a < 3; ++a) {
      rotate(v.pose.q, ax[a], w[a]);
      if (fabs_(w[a][1]) > fabs_(w[up][1])) up = a;
    }
    const int ua = up == 0 ? 1 : 0, va = up == 2 ? 1 : 2;
    const float local[3] = {v.box[0] + 0.5f * v.box[3], v.box[1] + 0.5f * v.box[4], v.box[2] + 0.5f * v.box[5]};
    float c[3];
    transform(v.pose, local, c);
    const float ul = fsqrt(w[ua][0] * w[ua][0] + w[ua][2] * w[ua][2]);
    if (ul < 1e-4f) continue;
    Box& b = r.boxes[r.boxN++];
    b.cx = c[0];
    b.cz = c[2];
    b.ux = w[ua][0] / ul;
    b.uz = w[ua][2] / ul;
    b.hu = 0.5f * v.box[3 + ua];
    b.hv = 0.5f * v.box[3 + va];
    b.y0 = c[1] - 0.5f * v.box[3 + up];
    b.y1 = c[1] + 0.5f * v.box[3 + up];
    b.labels = v.labels;
    if (v.labels & kLabTable) ++r.nTables;
    else if (v.labels & kLabCouch) ++r.nCouches;
    else if (v.labels & kLabStorage) ++r.nStorage;
    else if (v.labels & kLabBed) ++r.nBeds;
    else ++r.nOther;
  }
  // a table / couch given only as a plane counts too
  for (int t = 0; t < topPolys_; ++t) {
    bool boxed = false;
    for (int i = 0; i < r.boxN && !boxed; ++i) {
      const Box& b = r.boxes[i];
      float cx = 0.0f, cz = 0.0f;
      for (int k = 0; k < topPolyN[t]; ++k) {
        cx += topPolys[t][k][0];
        cz += topPolys[t][k][1];
      }
      cx /= static_cast<float>(topPolyN[t]);
      cz /= static_cast<float>(topPolyN[t]);
      const float du = (cx - b.cx) * b.ux + (cz - b.cz) * b.uz, dv = -(cx - b.cx) * b.uz + (cz - b.cz) * b.ux;
      boxed = fabs_(du) < b.hu + 0.05f && fabs_(dv) < b.hv + 0.05f;
    }
    if (boxed) continue;
    if (topPolyLab[t] & kLabTable) ++r.nTables;
    else if (topPolyLab[t] & kLabCouch) ++r.nCouches;
    else if (topPolyLab[t] & kLabStorage) ++r.nStorage;
    else if (topPolyLab[t] & kLabBed) ++r.nBeds;
    else ++r.nOther;
  }

  // ---- the grid over the outline (+ furniture poking past it)
  float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
  for (int k = 0; k < r.outlineN; ++k) {
    x0 = fmin_(x0, r.outline[k][0]);
    x1 = fmax_(x1, r.outline[k][0]);
    z0 = fmin_(z0, r.outline[k][1]);
    z1 = fmax_(z1, r.outline[k][1]);
  }
  x0 -= 0.1f;
  z0 -= 0.1f;
  x1 += 0.1f;
  z1 += 0.1f;
  r.cell = fmax_(0.05f, fmax_(x1 - x0, z1 - z0) / static_cast<float>(kGrid));
  r.gx0 = x0;
  r.gz0 = z0;
  r.gw = imin(kGrid, static_cast<int>((x1 - x0) / r.cell) + 1);
  r.gh = imin(kGrid, static_cast<int>((z1 - z0) / r.cell) + 1);
  for (int j = 0; j < r.gh; ++j) {
    for (int i = 0; i < r.gw; ++i) {
      const int c = j * r.gw + i;
      const float cx = r.gx0 + (static_cast<float>(i) + 0.5f) * r.cell, cz = r.gz0 + (static_cast<float>(j) + 0.5f) * r.cell;
      r.top[c] = kNoTop;
      r.meshTop[c] = kNoTop;
      r.top2[c] = kNoTop;
      r.topLab[c] = 0;
      r.inside[c] = in_poly(r.outline, r.outlineN, cx, cz) ? 1 : 0;
      r.insideCells += r.inside[c];
    }
  }
  // boxes: their tops over their footprints
  for (int bi = 0; bi < r.boxN; ++bi) {
    const Box& b = r.boxes[bi];
    const float ex = fabs_(b.ux) * b.hu + fabs_(b.uz) * b.hv, ez = fabs_(b.uz) * b.hu + fabs_(b.ux) * b.hv;
    const int i0 = imax(0, static_cast<int>((b.cx - ex - r.gx0) / r.cell)), i1 = imin(r.gw - 1, static_cast<int>((b.cx + ex - r.gx0) / r.cell));
    const int j0 = imax(0, static_cast<int>((b.cz - ez - r.gz0) / r.cell)), j1 = imin(r.gh - 1, static_cast<int>((b.cz + ez - r.gz0) / r.cell));
    for (int j = j0; j <= j1; ++j) {
      for (int i = i0; i <= i1; ++i) {
        const float cx = r.gx0 + (static_cast<float>(i) + 0.5f) * r.cell - b.cx, cz = r.gz0 + (static_cast<float>(j) + 0.5f) * r.cell - b.cz;
        const float du = cx * b.ux + cz * b.uz, dv = -cx * b.uz + cz * b.ux;
        if (fabs_(du) <= b.hu && fabs_(dv) <= b.hv) put_top(r, j * r.gw + i, b.y1, b.labels);
      }
    }
  }
  // furniture tops given as planes
  for (int t = 0; t < topPolys_; ++t) {
    float px0 = 1e30f, pz0 = 1e30f, px1 = -1e30f, pz1 = -1e30f;
    for (int k = 0; k < topPolyN[t]; ++k) {
      px0 = fmin_(px0, topPolys[t][k][0]);
      px1 = fmax_(px1, topPolys[t][k][0]);
      pz0 = fmin_(pz0, topPolys[t][k][1]);
      pz1 = fmax_(pz1, topPolys[t][k][1]);
    }
    const int i0 = imax(0, static_cast<int>((px0 - r.gx0) / r.cell)), i1 = imin(r.gw - 1, static_cast<int>((px1 - r.gx0) / r.cell));
    const int j0 = imax(0, static_cast<int>((pz0 - r.gz0) / r.cell)), j1 = imin(r.gh - 1, static_cast<int>((pz1 - r.gz0) / r.cell));
    for (int j = j0; j <= j1; ++j)
      for (int i = i0; i <= i1; ++i) {
        const float cx = r.gx0 + (static_cast<float>(i) + 0.5f) * r.cell, cz = r.gz0 + (static_cast<float>(j) + 0.5f) * r.cell;
        if (in_poly(topPolys[t], topPolyN[t], cx, cz)) put_top(r, j * r.gw + i, topPolyY[t], topPolyLab[t]);
      }
  }
  // the room's mesh: its up-facing triangles (sills, seat cushions, shelves, what the boxes miss)
  for (int m = 0; m < in.meshCount; ++m) {
    const InMesh& me = in.meshes[m];
    if (me.verts == nullptr || me.idx == nullptr || me.idxCount < 3) continue;
    r.hasMesh = true;
    // which way its triangles wind: the floor's own triangles must face up (by area: the floor is big, the undersides
    // of things standing on it are small)
    float upVotes = 0.0f, downVotes = 0.0f;
    for (int t = 0; t + 2 < me.idxCount; t += 3) {
      float a[3], b[3], c[3];
      const uint32_t ia = me.idx[t], ib = me.idx[t + 1], ic = me.idx[t + 2];
      if (ia >= static_cast<uint32_t>(me.vertCount) || ib >= static_cast<uint32_t>(me.vertCount) || ic >= static_cast<uint32_t>(me.vertCount)) continue;
      transform(me.pose, me.verts + 3 * ia, a);
      transform(me.pose, me.verts + 3 * ib, b);
      transform(me.pose, me.verts + 3 * ic, c);
      const float ex1 = b[0] - a[0], ey1 = b[1] - a[1], ez1 = b[2] - a[2];
      const float ex2 = c[0] - a[0], ey2 = c[1] - a[1], ez2 = c[2] - a[2];
      const float nx = ey1 * ez2 - ez1 * ey2, ny = ez1 * ex2 - ex1 * ez2, nz = ex1 * ey2 - ey1 * ex2;
      const float cy = (a[1] + b[1] + c[1]) / 3.0f;
      // the floor's own surface: faces up into the room
      if (fabs_(a[1] - r.floorY) < 0.015f && fabs_(b[1] - r.floorY) < 0.015f && fabs_(c[1] - r.floorY) < 0.015f) {
        if (ny > 0.0f) upVotes += ny;
        else downVotes -= ny;
        continue;
      }
      // a wall's own surface (on its scanned plane): faces into the room
      if (fabs_(ny) > 0.3f * fsqrt(nx * nx + ny * ny + nz * nz)) continue;
      const float cx = (a[0] + b[0] + c[0]) / 3.0f, cz = (a[2] + b[2] + c[2]) / 3.0f;
      for (int wi = 0; wi < r.wallN; ++wi) {
        const Wall& w = r.walls[wi];
        if (w.labels & kLabInvisible) continue;
        const float d = (cx - w.x0) * w.nx + (cz - w.z0) * w.nz;
        const float along = (cx - w.x0) * w.dx + (cz - w.z0) * w.dz;
        if (fabs_(d) > 0.03f || along < 0.0f || along > w.len || cy < w.y0 || cy > w.y1) continue;
        const float into = nx * w.nx + nz * w.nz;  // (twice the area) x how much it faces in
        if (into > 0.0f) upVotes += into;
        else downVotes -= into;
        break;
      }
    }
    const float sign = downVotes > upVotes ? -1.0f : 1.0f;
    const bool known = upVotes + downVotes >= 2.0f * 0.5f;  // (twice the area) at least 0.5 m2 of floor seen
    for (int t = 0; t + 2 < me.idxCount; t += 3) {
      ++r.meshTris;
      float a[3], b[3], c[3];
      const uint32_t ia = me.idx[t], ib = me.idx[t + 1], ic = me.idx[t + 2];
      if (ia >= static_cast<uint32_t>(me.vertCount) || ib >= static_cast<uint32_t>(me.vertCount) || ic >= static_cast<uint32_t>(me.vertCount)) continue;
      transform(me.pose, me.verts + 3 * ia, a);
      transform(me.pose, me.verts + 3 * ib, b);
      transform(me.pose, me.verts + 3 * ic, c);
      const float ex1 = b[0] - a[0], ey1 = b[1] - a[1], ez1 = b[2] - a[2];
      const float ex2 = c[0] - a[0], ey2 = c[1] - a[1], ez2 = c[2] - a[2];
      const float nx = ey1 * ez2 - ez1 * ey2, ny = ez1 * ex2 - ex1 * ez2, nz = ex1 * ey2 - ey1 * ex2;
      const float nl = fsqrt(nx * nx + ny * ny + nz * nz);
      if (nl < 1e-9f) continue;
      const float up = sign * ny / nl;
      if (known ? up < 0.75f : fabs_(up) < 0.75f) continue;
      const float ymin = fmin_(a[1], fmin_(b[1], c[1]));
      if (ymin < r.floorY + 0.03f) continue;  // the floor (it's already the floor)
      ++r.meshTopTris;
      // cells whose middle is in the triangle's footprint, at its height there
      const float bx0 = fmin_(a[0], fmin_(b[0], c[0])), bx1 = fmax_(a[0], fmax_(b[0], c[0]));
      const float bz0 = fmin_(a[2], fmin_(b[2], c[2])), bz1 = fmax_(a[2], fmax_(b[2], c[2]));
      const int i0 = imax(0, static_cast<int>(ffloor((bx0 - r.gx0) / r.cell - 0.5f))), i1 = imin(r.gw - 1, static_cast<int>((bx1 - r.gx0) / r.cell));
      const int j0 = imax(0, static_cast<int>(ffloor((bz0 - r.gz0) / r.cell - 0.5f))), j1 = imin(r.gh - 1, static_cast<int>((bz1 - r.gz0) / r.cell));
      const float det = ex1 * ez2 - ex2 * ez1;
      if (fabs_(det) < 1e-9f) continue;
      for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
          const int cc = j * r.gw + i;
          if (!r.inside[cc]) continue;
          const float px = r.gx0 + (static_cast<float>(i) + 0.5f) * r.cell - a[0], pz = r.gz0 + (static_cast<float>(j) + 0.5f) * r.cell - a[2];
          const float u = (px * ez2 - ex2 * pz) / det, w = (ex1 * pz - px * ez1) / det;
          if (u < -0.02f || w < -0.02f || u + w > 1.02f) continue;
          const float h = a[1] + u * ey1 + w * ey2;
          if (h < r.floorY + 0.03f || h > r.ceilY - 0.05f) continue;
          // two layers: the top, and the highest surface 20 cm or more under it
          float& t1 = r.meshTop[cc];
          float& t2 = r.top2[cc];
          if (h > t1 + 0.2f) {
            if (t1 > kNoTop && t1 > t2) t2 = t1;
            t1 = h;
          } else if (h > t1 - 0.2f) {
            if (h > t1) t1 = h;
          } else if (h > t2) {
            t2 = h;
          }
        }
    }
  }
  // the mesh wins where it saw a top (a box keeps its label there); boxes / planes fill where it saw none
  if (r.hasMesh) {
    for (int c = 0; c < r.gw * r.gh; ++c) {
      if (r.meshTop[c] <= kNoTop) continue;
      if (r.top[c] <= kNoTop) r.topLab[c] = kLabMesh;
      r.top[c] = r.meshTop[c];
    }
  }
  // no mesh: a sill under each window (a 6 cm ledge; with a mesh the real sill is in it)
  if (!r.hasMesh) {
    for (int k = 0; k < r.featureN; ++k) {
      const Feature& f = r.features[k];
      if ((f.labels & kLabWindow) == 0 || f.y0 < r.floorY + 0.3f) continue;
      const float dx = f.x1 - f.x0, dz = f.z1 - f.z0, len = fsqrt(dx * dx + dz * dz);
      if (len < 0.1f) continue;
      for (float t = 0.0f; t <= len; t += r.cell * 0.5f)
        for (float d = 0.0f; d <= 0.06f; d += r.cell * 0.5f) {
          const int c = cell_of(r, f.x0 + dx * t / len + f.nx * d, f.z0 + dz * t / len + f.nz * d);
          if (c >= 0) put_top(r, c, f.y0, kLabWindow);
        }
    }
  }
  for (int c = 0; c < r.gw * r.gh; ++c)
    if (r.top[c] > kNoTop) ++r.topCells;

  // ---- where snow piles up
  // corners (the outline's inside corners), then along the walls every 0.7 m, then on the tops, then on sills
  for (int k = 0; k < r.outlineN; ++k) {
    const float* p = r.outline[k];
    const float* a = r.outline[(k + r.outlineN - 1) % r.outlineN];
    const float* b = r.outline[(k + 1) % r.outlineN];
    float ax = a[0] - p[0], az = a[1] - p[1], bx = b[0] - p[0], bz = b[1] - p[1];
    const float al = fsqrt(ax * ax + az * az), bl = fsqrt(bx * bx + bz * bz);
    if (al < 0.2f || bl < 0.2f) continue;
    ax /= al;
    az /= al;
    bx /= bl;
    bz /= bl;
    float sx = ax + bx, sz = az + bz;  // the bisector
    const float sl = fsqrt(sx * sx + sz * sz);
    if (sl < 0.3f) continue;  // nearly straight: not a corner
    sx /= sl;
    sz /= sl;
    const float x = p[0] + sx * 0.14f, z = p[1] + sz * 0.14f;
    if (!in_poly(r.outline, r.outlineN, x, z) || near_door(r, x, z)) continue;  // an outside corner / a doorway
    if (cell_top(r, x, z) > kNoTop) continue;  // furniture / a curtain in the corner: nothing piles on the floor there
    add_spot(r, x, r.floorY, z, -sx, -sz, 1.0f, 0.5f, 0, kLabFloor);
  }
  for (int i = 0; i < r.wallN; ++i) {
    const Wall& w = r.walls[i];
    if (w.labels & kLabInvisible) continue;
    const int n = static_cast<int>(w.len / 0.7f);
    for (int k = 1; k < n; ++k) {
      const float t = w.len * static_cast<float>(k) / static_cast<float>(n);
      const float x = w.x0 + w.dx * t + w.nx * 0.09f, z = w.z0 + w.dz * t + w.nz * 0.09f;
      if (!in_poly(r.outline, r.outlineN, x, z) || near_door(r, x, z)) continue;
      if (cell_top(r, x, z) > kNoTop) continue;  // furniture against the wall there
      add_spot(r, x, r.floorY, z, -w.nx, -w.nz, 0.5f + 0.1f * static_cast<float>((k * 7) % 3), 0.36f, 1, kLabFloor);
    }
  }
  for (int bi = 0; bi < r.boxN; ++bi) {
    const Box& b = r.boxes[bi];
    if ((b.labels & (kLabTable | kLabCouch | kLabStorage | kLabBed | kLabOther | kLabScreen)) == 0) continue;
    if (b.y1 > r.ceilY - 0.2f || b.hu < 0.12f || b.hv < 0.12f) continue;
    // 1-4 spots over its top (more on a bigger top)
    const int nu = b.hu > 0.45f ? 2 : 1, nv = b.hv > 0.45f ? 2 : 1;
    for (int a = 0; a < nu; ++a)
      for (int c = 0; c < nv; ++c) {
        const float du = nu == 1 ? 0.0f : (a == 0 ? -0.5f : 0.5f) * b.hu, dv = nv == 1 ? 0.0f : (c == 0 ? -0.5f : 0.5f) * b.hv;
        const float x = b.cx + b.ux * du - b.uz * dv, z = b.cz + b.uz * du + b.ux * dv;
        const float t = cell_top(r, x, z);
        // (a dusting this wide at most: it stays on its top)
        add_spot(r, x, t > kNoTop ? t : b.y1, z, 0.0f, 0.0f, 0.4f, fmin_(0.38f, 0.85f * fmin_(b.hu / static_cast<float>(nu), b.hv / static_cast<float>(nv))), 2, b.labels);
      }
  }
  for (int k = 0; k < r.featureN; ++k) {
    const Feature& f = r.features[k];
    if ((f.labels & kLabWindow) == 0 || f.y0 < r.floorY + 0.3f) continue;
    // small drifts along the sill (it's narrow), every 30 cm
    const float len = fsqrt((f.x1 - f.x0) * (f.x1 - f.x0) + (f.z1 - f.z0) * (f.z1 - f.z0));
    const int n = imax(1, static_cast<int>(len / 0.3f));
    for (int k = 0; k < n; ++k) {
      const float u = (static_cast<float>(k) + 0.5f) / static_cast<float>(n);
      const float x = f.x0 + (f.x1 - f.x0) * u + f.nx * 0.05f, z = f.z0 + (f.z1 - f.z0) * u + f.nz * 0.05f;
      const int c = cell_of(r, x, z);
      if (c < 0) continue;
      const float t = fabs_(r.top[c] - f.y0) < 0.12f ? r.top[c] : (fabs_(r.top2[c] - f.y0) < 0.12f ? r.top2[c] : kNoTop);
      if (t > kNoTop) add_spot(r, x, t, z, -f.nx, -f.nz, 0.7f, 0.1f, 3, kLabWindow);
    }
  }
  r.valid = true;
}

void set_frame(Room& r, float c, float s, float tx, float ty, float tz) {
  r.fc = c;
  r.fs = s;
  r.ft[0] = tx;
  r.ft[1] = ty;
  r.ft[2] = tz;
}

float floor_y(const Room& r) { return r.floorY + r.ft[1]; }
float ceiling_y(const Room& r) { return r.ceilY + r.ft[1]; }

bool inside(const Room& r, float x, float z) {
  float bx, bz;
  to_build(r, x, z, bx, bz);
  const int c = cell_of(r, bx, bz);
  return c >= 0 && r.inside[c] != 0;
}

float top_at(const Room& r, float x, float z) {
  float bx, bz;
  to_build(r, x, z, bx, bz);
  const float t = cell_top(r, bx, bz);
  return t > kNoTop ? t + r.ft[1] : kNoTop;
}

float surface_below(const Room& r, float x, float y, float z, uint16_t* labels) {
  float bx, bz;
  to_build(r, x, z, bx, bz);
  const int c = cell_of(r, bx, bz);
  const float by = y - r.ft[1];
  if (c >= 0 && r.top[c] > kNoTop && r.top[c] <= by + 0.02f) {
    if (labels) *labels = r.topLab[c];
    return r.top[c] + r.ft[1];
  }
  if (c >= 0 && r.top2[c] > kNoTop && r.top2[c] <= by + 0.02f) {  // under an overhang: the lower surface
    if (labels) *labels = r.topLab[c];
    return r.top2[c] + r.ft[1];
  }
  if (labels) *labels = kLabFloor;
  return r.floorY + r.ft[1];
}

int collide(const Room& r, float p[3], float v[3], const float prev[3], float radius, float bounce, float friction,
            uint16_t* labels, unsigned what) {
  if (!r.valid) return kHitNone;
  float bp[3], bv[3], bq[3];
  to_build(r, p[0], p[2], bp[0], bp[2]);
  bp[1] = p[1] - r.ft[1];
  to_build(r, prev[0], prev[2], bq[0], bq[2]);
  bq[1] = prev[1] - r.ft[1];
  vec_to_build(r, v[0], v[2], bv[0], bv[2]);
  bv[1] = v[1];
  int hit = kHitNone;
  uint16_t lab = 0;
  // the walls (only from the room's side: something already behind a wall is in the next room)
  for (int i = 0; (what & kCollWalls) != 0 && i < r.wallN; ++i) {
    const Wall& w = r.walls[i];
    const float d = (bp[0] - w.x0) * w.nx + (bp[2] - w.z0) * w.nz;
    if (d >= radius) continue;
    const float dPrev = (bq[0] - w.x0) * w.nx + (bq[2] - w.z0) * w.nz;
    if (dPrev < -0.05f) continue;
    const float along = (bp[0] - w.x0) * w.dx + (bp[2] - w.z0) * w.dz;
    if (along < -radius || along > w.len + radius || bp[1] < w.y0 - radius || bp[1] > w.y1 + radius) continue;
    bp[0] += (radius - d) * w.nx;
    bp[2] += (radius - d) * w.nz;
    const float vn = bv[0] * w.nx + bv[2] * w.nz;
    if (vn < 0.0f) {
      bv[0] -= (1.0f + bounce) * vn * w.nx;
      bv[2] -= (1.0f + bounce) * vn * w.nz;
      const float vt = bv[0] * w.dx + bv[2] * w.dz;  // along the wall: friction
      bv[0] -= (1.0f - friction) * vt * w.dx;
      bv[2] -= (1.0f - friction) * vt * w.dz;
      bv[1] *= friction;
    }
    hit = kHitWall;
    lab = w.labels;
  }
  // the ceiling
  if ((what & kCollCeiling) != 0 && bp[1] + radius > r.ceilY) {
    bp[1] = r.ceilY - radius;
    if (bv[1] > 0.0f) bv[1] = -bv[1] * bounce;
    hit = kHitCeiling;
    lab = kLabCeiling;
  }
  // furniture: from above it lands on the top, from the side it bounces off
  const int c = cell_of(r, bp[0], bp[2]);
  float t = c >= 0 ? r.top[c] : kNoTop;
  // open space under the top (the mesh saw a lower surface): below the top it's the lower one that counts
  if (t > kNoTop && r.top2[c] > kNoTop && bq[1] - radius < t - 0.03f) t = r.top2[c];
  if (t > kNoTop && bp[1] - radius < t) {
    if (bq[1] - radius >= t - 0.03f) {
      if ((what & kCollTops) != 0) {  // (without kCollTops, coming down onto it is the caller's landing)
        bp[1] = t + radius;
        if (bv[1] < 0.0f) bv[1] = bv[1] < -0.3f ? -bv[1] * bounce : 0.0f;
        bv[0] *= friction;
        bv[2] *= friction;
        hit = kHitTop;
        lab = r.topLab[c];
      }
    } else if ((what & kCollSides) != 0) {
      const float tq = cell_top(r, bq[0], bq[2]);
      if (tq <= kNoTop || bq[1] - radius >= tq - 0.03f) {  // it came in from outside the furniture: back out
        bp[0] = bq[0];
        bp[2] = bq[2];
        bv[0] = -bv[0] * bounce;
        bv[2] = -bv[2] * bounce;
        hit = kHitSide;
        lab = r.topLab[c];
      }
    }
  }
  if ((what & kCollTops) != 0 && bp[1] - radius < r.floorY) {
    bp[1] = r.floorY + radius;
    if (bv[1] < 0.0f) bv[1] = bv[1] < -0.3f ? -bv[1] * bounce : 0.0f;
    bv[0] *= friction;
    bv[2] *= friction;
    hit = kHitTop;
    lab = kLabFloor;
  }
  if (hit != kHitNone) {
    to_now(r, bp[0], bp[2], p[0], p[2]);
    p[1] = bp[1] + r.ft[1];
    vec_to_now(r, bv[0], bv[2], v[0], v[2]);
    v[1] = bv[1];
    if (labels) *labels = lab;
  }
  return hit;
}

void spot_now(const Room& r, int i, float out[5]) {
  const Spot& s = r.spots[i];
  to_now(r, s.x, s.z, out[0], out[2]);
  out[1] = s.y + r.ft[1];
  vec_to_now(r, s.nx, s.nz, out[3], out[4]);
}

namespace {
void put_str(char* buf, size_t n, size_t& at, const char* s) {
  while (*s != 0 && at + 1 < n) buf[at++] = *s++;
  if (n > 0) buf[at < n ? at : n - 1] = 0;
}
void put_int(char* buf, size_t n, size_t& at, int v) {
  char tmp[12];
  int k = 0;
  if (v == 0) tmp[k++] = '0';
  while (v > 0 && k < 11) {
    tmp[k++] = static_cast<char>('0' + v % 10);
    v /= 10;
  }
  while (k > 0 && at + 1 < n) buf[at++] = tmp[--k];
  if (n > 0) buf[at < n ? at : n - 1] = 0;
}
void put_count(char* buf, size_t n, size_t& at, int v, const char* one, const char* many) {
  if (v <= 0) return;
  if (at > 0) put_str(buf, n, at, ", ");
  put_int(buf, n, at, v);
  put_str(buf, n, at, " ");
  put_str(buf, n, at, v == 1 ? one : many);
}
}  // namespace

void summary(const Room& r, char* buf, size_t n) {
  size_t at = 0;
  if (n == 0) return;
  buf[0] = 0;
  if (!r.valid) {
    put_str(buf, n, at, "NO ROOM");
    return;
  }
  put_count(buf, n, at, r.nWalls, "WALL", "WALLS");
  put_str(buf, n, at, at > 0 ? ", FLOOR" : "FLOOR");
  if (r.hasCeiling) put_str(buf, n, at, ", CEILING");
  put_count(buf, n, at, r.nTables, "TABLE", "TABLES");
  put_count(buf, n, at, r.nCouches, "COUCH", "COUCHES");
  put_count(buf, n, at, r.nBeds, "BED", "BEDS");
  put_count(buf, n, at, r.nStorage + r.nOther, "MORE", "MORE");
  put_count(buf, n, at, r.nWindows, "WINDOW", "WINDOWS");
  if (r.hasMesh) put_str(buf, n, at, ", MESH");
}

}  // namespace xrroom
