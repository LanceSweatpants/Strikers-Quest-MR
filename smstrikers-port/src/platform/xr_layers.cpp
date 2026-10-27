// See include/port/xr_layers.h.

#include "port/xr_layers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unordered_map>

#include "Game/Drawable/DrawableObj.h"

extern "C" int PortXrStereoWorld(void);

namespace {

// The field's footprint in world units: the pitch is 41.2 x 24.2 centred on the origin (Field.cpp
// mv3FieldPosition is the half extent), goals on +-X, ground at z = 0. The margin takes in the goals,
// nets and the electric fence around the pitch; anything reaching past it, or taller, is stadium.
const float kFieldHalfX = 27.0f;
const float kFieldHalfY = 17.0f;
// Goals reach 2.7 and the benches 2.3; the signs and rigs at the field's edge (4-6.5) are stadium.
const float kFieldTop = 3.5f;
// Flat stand-ins far under the ground (z = -30 on The Palace) are not part of the field.
const float kFieldBottom = -2.0f;
// Objects centred past this on the camera side (the fence is at about -13.6) are stadium.
const float kCameraSideCutY = -14.5f;
// r58: the pitch itself (Field.cpp half extents) and a low ground piece covering part of it. r59: any
// size up to a sanity bound (Konga's main floor is one piece, x +-27, y +-33.7; its outer quarters
// reach x 43): the part past the field box is cut off on the table (PortXrTableClip).
const float kPitchHalfX = 20.6f;
const float kPitchHalfY = 12.1f;
const float kGroundHalfX = 60.0f;
const float kGroundHalfY = 45.0f;
const float kGroundTop = 1.2f;
const float kGroundBottom = -0.5f;

struct Key
{
    const DrawableObject* object;
    unsigned long hash;
    bool operator==(const Key& o) const { return object == o.object && hash == o.hash; }
};
struct KeyHash
{
    size_t operator()(const Key& k) const { return (size_t)k.object ^ ((size_t)k.hash << 1); }
};

// Per object, decided once: stadium or field, and its world bounds at that time with the
// translation they went with (a moved object's bounds are shifted by how far it moved).
struct Info
{
    bool stadium;
    bool crowd;
    bool tallField; // a field object standing up (goals, benches, fence), not the flat pitch
    bool clip;      // r59: field ground reaching past the field box, cut to it on the table
    bool tableThin; // r98: a small Konga border piece left off the table (PortXrSkipPacketDraw)
    float mn[3];
    float mx[3];
    float t0[3];
};
std::unordered_map<Key, Info, KeyHash> s_info;

// This frame's stadium matrix handles and their bounds (unbounded: never culled).
struct Mark
{
    float mn[3];
    float mx[3];
    bool bounded;
    bool crowd;
    unsigned cullSerial; // the cull camera the cached answer belongs to
    bool culled;
};
std::unordered_map<uintptr_t, Mark> s_stadium;
// This frame's field (non-stadium) handles, for the self-test switch that drops them from the
// stadium layer.
std::unordered_map<uintptr_t, bool> s_field;
// r59: this frame's handles of field ground to cut to the field box on the table.
std::unordered_map<uintptr_t, bool> s_clipField;
// r98: this frame's handles of small Konga border pieces left off the table, and how many such objects
// this match (logged once).
std::unordered_map<uintptr_t, bool> s_tableThin;
int s_tableThinObjects = 0;
// The view matrix of the view being sent (row-vector, as s_V), kept whatever STRIKERS_XR_CULL says,
// and the table clip planes made from it (eye space), for the cull serial they were made for.
float s_sendV[4][4];
bool s_sendVValid = false;
float s_clipPlanes[16];
unsigned s_clipSerial = 0;
// Camera-side objects and whether the game hides them from its own camera (DoTranslucency), by
// object (kept across frames: the game may draw an object before deciding its translucency for the
// frame, so the latest decision is used, one frame late at worst), and this frame's matrix handles
// of the hidden ones: drawn for the life-size stadium, left out of the normal send and the table.
std::unordered_map<const DrawableObject*, bool> s_cameraSideHidden;
std::unordered_map<uintptr_t, bool> s_normalSkip;
bool s_tableLayer = false;
bool s_eyeSend = false;
// Self-test switches (xr_tune): leave a category out of the eye sends to measure what it costs.
// Self-test probe (xr_tune "probe"): logs where each field object lands in the next table-layer
// view, to name things seen in a snapshot.
struct ProbeInfo
{
    unsigned long hash;
    unsigned long creation;
    unsigned long objectFlags;
    float c[3];
    float ext[3];
};
std::unordered_map<uintptr_t, ProbeInfo> s_probeField;
int s_probeState = 0; // 0 off, 1 armed (collect this frame), 2 log at the table send
bool s_tuneSkipCrowd = false;
bool s_tuneSkipStands = false;
bool s_tuneSkipStadiumField = false;
// Stadium layer economies (PortXrSetStadiumEconomy): stadium objects smaller than this many
// pixels across in an eye are left out (0 = off), and the life-size goals/benches/fence too.
float s_lodMinPx = 0.0f;
float s_eyeHalfWidthPx = 600.0f;
bool s_skipTallField = false;
bool s_bothLayers = false;  // this frame sends the table layer as well (Tabletop · Stadium)
const DrawableObject* s_ball = NULL;  // r43b: PortXrSetBall
int s_logged = 0;
int s_stadiumId = -1;  // r53: PortXrLayersNewMatch
// r53 draw census: [send][view], send 0 broadcast, 1 table, 2 stadium, 3 scorer.
enum { kCensusSends = 4, kCensusViews = 0x22 };
unsigned long s_census[kCensusSends][kCensusViews];
bool s_scorerLayer = false;  // r42 (declared here for the census; set by PortXrSetScorerLayer)

// The camera of the view being sent, for culling stadium objects it cannot see. Row-vector view
// (v' = v * V, the game's nlMatrix4) and a GX projection (clip = P * v, rows as stored).
bool s_cullOn = false;
float s_V[4][4];
float s_P[4][4];
unsigned s_cullSerial = 1;
unsigned long s_culledThisFrame = 0;
unsigned long s_culledTotal = 0;
// r58: why the stats line has read "culled 0/frame" in every 3D log since at least r51b (r9-r42b:
// 400-500 a frame): the eye sends' stadium draws reaching the cull test with culling off, unbounded,
// or tested (and culled), shown with the draw census; and per match the first few tested far objects
// with their corners against each frustum side.
unsigned long s_cullDiag[4];  // off, unbounded, tested, culled
int s_cullSamples = 0;

bool cull_enabled()
{
    static int s_enabled = -1;
    if (s_enabled < 0)
    {
        const char* e = getenv("STRIKERS_XR_CULL");
        s_enabled = (e != NULL && e[0] == '0') ? 0 : 1;
    }
    return s_enabled != 0;
}

void world_bounds(const DrawableObject* object, float mn[3], float mx[3])
{
    AABBDimensions dims;
    object->GetAABBDimensions(dims, false);
    const nlMatrix4& m = object->GetWorldMatrix();
    for (int k = 0; k < 3; k++)
    {
        mn[k] = 1e30f;
        mx[k] = -1e30f;
    }
    for (int c = 0; c < 8; c++)
    {
        const float x = (c & 1) ? dims.mMax.x : dims.mMin.x;
        const float y = (c & 2) ? dims.mMax.y : dims.mMin.y;
        const float z = (c & 4) ? dims.mMax.z : dims.mMin.z;
        // Row vector times the game's nlMatrix4: translation in the fourth row.
        for (int k = 0; k < 3; k++)
        {
            const float w = x * m.e2[0][k] + y * m.e2[1][k] + z * m.e2[2][k] + m.e2[3][k];
            if (w < mn[k]) mn[k] = w;
            if (w > mx[k]) mx[k] = w;
        }
    }
}

Info classify(const DrawableObject* object)
{
    Info info = {};
    world_bounds(object, info.mn, info.mx);
    const nlMatrix4& m = object->GetWorldMatrix();
    for (int k = 0; k < 3; k++)
        info.t0[k] = m.e2[3][k];

    const unsigned long creation = object->m_uObjectCreationFlags;
    info.crowd = (creation & 0x20000) != 0;
    // r43b: the game ball is on the field wherever it happens to be at first sight (PortXrSetBall).
    if (object == s_ball)
    {
        info.stadium = false;
        return info;
    }
    // Crowd, skybox and jumbotron are stadium whatever their bounds say.
    if (creation & (0x20000 | 0x100 | 0x10000))
    {
        info.stadium = true;
        return info;
    }
    // Behind the camera-side touchline (game y < 0 is the TV camera's side) is the camera-side bench
    // area the game hides from its own camera (bench 0x2000); the pieces left there (hash 1e1d7fae/f,
    // x +-10, y -15.8, found with xr_tune "probe" in r20) came out as black sawtooth silhouettes
    // beside the Tabletop field in the room. They belong to the stadium, life-size.
    const bool behindCameraTouchline = 0.5f * (info.mn[1] + info.mx[1]) < kCameraSideCutY;
    const bool inFieldBox = info.mn[0] >= -kFieldHalfX && info.mx[0] <= kFieldHalfX && info.mn[1] >= -kFieldHalfY &&
                            info.mx[1] <= kFieldHalfY && info.mx[2] <= kFieldTop && info.mn[2] >= kFieldBottom;
    // r58 (Lance, Konga Coliseum: "the see through field"): some stadiums lay the ground in big flat
    // pieces reaching past the field box; by the box they were stadium, so the table had no floor and
    // the life-size stadium floor showed through it. Low ground covering part of the pitch is field.
    // r59: r58 capped its size and missed Konga's main floor (one piece, x +-27, y +-33.7) and outer
    // quarters (x to 43); now any size, cut to the field box on the table (PortXrTableClip), on either
    // side of the pitch (the camera-side cut is for the bench area, not the ground).
    const bool pitchGround = info.mn[2] >= kGroundBottom && info.mx[2] <= kGroundTop && info.mn[0] < kPitchHalfX &&
                             info.mx[0] > -kPitchHalfX && info.mn[1] < kPitchHalfY && info.mx[1] > -kPitchHalfY &&
                             info.mn[0] >= -kGroundHalfX && info.mx[0] <= kGroundHalfX &&
                             info.mn[1] >= -kGroundHalfY && info.mx[1] <= kGroundHalfY;
    const bool field = (!behindCameraTouchline && inFieldBox) || pitchGround;
    info.clip = field && !inFieldBox;
    // r53: per match (PortXrLayersNewMatch), field objects always (they are what the table shows; a
    // player saw "extra stuff from the stadium" on it), the first 120 stadium ones. r58: and every
    // stadium one over the pitch (the r57 log stopped before Konga's middle floor).
    const bool overPitch = info.mn[0] < kPitchHalfX && info.mx[0] > -kPitchHalfX && info.mn[1] < kPitchHalfY &&
                           info.mx[1] > -kPitchHalfY;
    if (field || overPitch || s_logged < 120)
    {
        if (!field && !overPitch)
            s_logged++;
        fprintf(stderr,
                "[xr-layer] %s stadium=%d hash=%08lx flags=%08lx aabb x[%.1f,%.1f] y[%.1f,%.1f] z[%.1f,%.1f]\n",
                field ? (info.clip ? "GROUND " : "FIELD  ") : "STADIUM", s_stadiumId,
                (unsigned long)object->m_uHashID, creation, info.mn[0], info.mx[0], info.mn[1], info.mx[1],
                info.mn[2], info.mx[2]);
    }
    info.stadium = !field;
    info.tallField = field && (info.mx[2] - info.mn[2]) > 0.4f;
    // r98 (Lance, "yeah try the konga thing": Tabletop at Konga Coliseum 45 fps, elsewhere 72): Konga lays 314
    // field pieces on the table (Phenac 56), 308 of them round the pitch's border, 163 under a unit across
    // (4 cm on the table). Those small border pieces - centre off the pitch, under 1 unit - stay off the table
    // there; the pitch, goals, fence and every bigger piece stay, and the life-size stadium keeps them all.
    const float ext = fmaxf(info.mx[0] - info.mn[0], fmaxf(info.mx[1] - info.mn[1], info.mx[2] - info.mn[2]));
    const float cx = 0.5f * (info.mn[0] + info.mx[0]), cy = 0.5f * (info.mn[1] + info.mx[1]);
    info.tableThin = field && s_stadiumId == 2 && !info.clip && ext < 1.0f &&
                     (fabsf(cx) > kPitchHalfX || fabsf(cy) > kPitchHalfY);
    if (info.tableThin && ++s_tableThinObjects % 20 == 0)
    {
        fprintf(stderr, "[xr-layer] r98: %d small Konga border pieces left off the table so far\n", s_tableThinObjects);
    }
    return info;
}

// How many of the box's (padded) corners lie outside each side of the current view's frustum: left,
// right, bottom, top, behind.
void frustum_counts(const float mn[3], const float mx[3], int out[5]);

// True when the box is wholly outside one side of the current view's frustum. Homogeneous
// half-space tests, so corners behind the camera are handled.
bool outside_frustum(const float mn[3], const float mx[3])
{
    int out[5];
    frustum_counts(mn, mx, out);
    return out[0] == 8 || out[1] == 8 || out[2] == 8 || out[3] == 8 || out[4] == 8;
}

void frustum_counts(const float mn[3], const float mx[3], int out[5])
{
    // A little margin: an object grazing the edge is drawn.
    const float pad[3] = { (mx[0] - mn[0]) * 0.05f + 0.5f, (mx[1] - mn[1]) * 0.05f + 0.5f,
                           (mx[2] - mn[2]) * 0.05f + 0.5f };
    int outLeft = 0, outRight = 0, outBottom = 0, outTop = 0, outBehind = 0;
    for (int c = 0; c < 8; c++)
    {
        const float x = (c & 1) ? mx[0] + pad[0] : mn[0] - pad[0];
        const float y = (c & 2) ? mx[1] + pad[1] : mn[1] - pad[1];
        const float z = (c & 4) ? mx[2] + pad[2] : mn[2] - pad[2];
        float e[4];
        for (int k = 0; k < 4; k++)
            e[k] = x * s_V[0][k] + y * s_V[1][k] + z * s_V[2][k] + s_V[3][k];
        const float cx = s_P[0][0] * e[0] + s_P[0][1] * e[1] + s_P[0][2] * e[2] + s_P[0][3] * e[3];
        const float cy = s_P[1][0] * e[0] + s_P[1][1] * e[1] + s_P[1][2] * e[2] + s_P[1][3] * e[3];
        const float cw = s_P[3][0] * e[0] + s_P[3][1] * e[1] + s_P[3][2] * e[2] + s_P[3][3] * e[3];
        outLeft += (cx + cw < 0.0f);
        outRight += (cw - cx < 0.0f);
        outBottom += (cy + cw < 0.0f);
        outTop += (cw - cy < 0.0f);
        outBehind += (cw <= 0.0f);
    }
    out[0] = outLeft;
    out[1] = outRight;
    out[2] = outBottom;
    out[3] = outTop;
    out[4] = outBehind;
}

// r58 diagnostic: one tested object, its corners per side and the camera it was tested against.
void log_cull_sample(const Mark& mark)
{
    int out[5];
    frustum_counts(mark.mn, mark.mx, out);
    fprintf(stderr,
            "[xr-cull] %s aabb x[%.1f,%.1f] y[%.1f,%.1f] z[%.1f,%.1f] out l%d r%d b%d t%d behind%d | V row3 (%.2f %.2f "
            "%.2f %.2f) col2 (%.2f %.2f %.2f) | P %.3f %.3f %.3f %.3f %.3f %.3f\n",
            s_tableLayer ? "table" : "stadium", mark.mn[0], mark.mx[0], mark.mn[1], mark.mx[1], mark.mn[2], mark.mx[2],
            out[0], out[1], out[2], out[3], out[4], s_V[3][0], s_V[3][1], s_V[3][2], s_V[3][3], s_V[0][2], s_V[1][2],
            s_V[2][2], s_P[0][0], s_P[1][1], s_P[2][2], s_P[2][3], s_P[3][2], s_P[3][3]);
}

// True when the box would be smaller than s_lodMinPx across in the current eye: far-off details.
bool too_small(const float mn[3], const float mx[3])
{
    const float c[3] = { 0.5f * (mn[0] + mx[0]), 0.5f * (mn[1] + mx[1]), 0.5f * (mn[2] + mx[2]) };
    const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
    const float radius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
    float e[3];
    for (int k = 0; k < 3; k++)
        e[k] = c[0] * s_V[0][k] + c[1] * s_V[1][k] + c[2] * s_V[2][k] + s_V[3][k];
    const float depth = -e[2] - radius; // nearest point, along the view direction
    if (depth <= 0.5f)
        return false;
    const float px = 2.0f * radius / depth * s_P[0][0] * s_eyeHalfWidthPx;
    return px < s_lodMinPx;
}

} // namespace

void PortXrSetStadiumEconomy(float minPx, float eyeHalfWidthPx, int skipTallField, int bothLayers)
{
    s_lodMinPx = minPx;
    s_eyeHalfWidthPx = eyeHalfWidthPx;
    s_skipTallField = skipTallField != 0;
    s_bothLayers = bothLayers != 0;
}

void PortXrClassifyAndMark(const DrawableObject* object, uintptr_t matrixHandle)
{
    if (object == NULL || !PortXrStereoWorld())
        return;
    if (!s_cameraSideHidden.empty())
    {
        std::unordered_map<const DrawableObject*, bool>::const_iterator hidden = s_cameraSideHidden.find(object);
        if (hidden != s_cameraSideHidden.end() && hidden->second)
            s_normalSkip[matrixHandle] = true;
    }
    const Key key = { object, object->m_uHashID };
    std::unordered_map<Key, Info, KeyHash>::iterator it = s_info.find(key);
    if (it == s_info.end())
        it = s_info.insert(std::make_pair(key, classify(object))).first;
    const Info& info = it->second;
    if (!info.stadium)
    {
        if (s_tuneSkipStadiumField || (s_skipTallField && info.tallField))
            s_field[matrixHandle] = true;
        if (info.clip)
            s_clipField[matrixHandle] = true;
        if (info.tableThin)  // r98
            s_tableThin[matrixHandle] = true;
        if (s_probeState != 0)
        {
            ProbeInfo p;
            p.hash = object->m_uHashID;
            p.creation = object->m_uObjectCreationFlags;
            p.objectFlags = object->m_uObjectFlags;
            for (int k = 0; k < 3; k++)
            {
                p.c[k] = 0.5f * (info.mn[k] + info.mx[k]);
                p.ext[k] = info.mx[k] - info.mn[k];
            }
            s_probeField[matrixHandle] = p;
        }
        return;
    }
    const nlMatrix4& m = object->GetWorldMatrix();
    Mark mark;
    mark.crowd = info.crowd;
    for (int k = 0; k < 3; k++)
    {
        const float d = m.e2[3][k] - info.t0[k];
        mark.mn[k] = info.mn[k] + d;
        mark.mx[k] = info.mx[k] + d;
    }
    mark.bounded = true;
    mark.cullSerial = 0;
    mark.culled = false;
    s_stadium[matrixHandle] = mark;
}

bool PortXrLayersActive() { return PortXrStereoWorld() != 0; }

void PortXrMarkStadiumHandle(uintptr_t matrixHandle)
{
    if (!PortXrStereoWorld())
        return;
    Mark mark;
    memset(&mark, 0, sizeof(mark));
    mark.bounded = false;
    s_stadium[matrixHandle] = mark;
}

void PortXrSetEyeSend(bool eyeSend) { s_eyeSend = eyeSend; }

void PortXrSetBall(const DrawableObject* ball) { s_ball = ball; }

void PortXrTuneSkip(int crowd, int stands, int stadiumField)
{
    s_tuneSkipCrowd = crowd != 0;
    s_tuneSkipStands = stands != 0;
    s_tuneSkipStadiumField = stadiumField != 0;
}

void PortXrSetCullCamera(const float* view16, const float* proj16)
{
    // r59: the table clip needs the view whether or not culling is on.
    s_sendVValid = view16 != NULL;
    if (view16 != NULL)
    {
        memcpy(s_sendV, view16, sizeof(s_sendV));
        ++s_clipSerial;
    }
    if (view16 == NULL || proj16 == NULL || !cull_enabled())
    {
        s_cullOn = false;
        return;
    }
    memcpy(s_V, view16, sizeof(s_V));
    memcpy(s_P, proj16, sizeof(s_P));
    s_cullOn = true;
    ++s_cullSerial;
}

int PortXrTableClip(uintptr_t matrixHandle, float planes16[16])
{
    // r59: in the table layer's eye send, field ground reaching past the field box is cut to it.
    if (!s_eyeSend || !s_tableLayer || !s_sendVValid || s_clipField.empty() || s_clipField.count(matrixHandle) == 0)
        return 0;
    static unsigned s_planesSerial = 0;
    if (s_planesSerial != s_clipSerial)
    {
        s_planesSerial = s_clipSerial;
        // Row-vector view: eye = world * V, V = [A 0; T 1]. world = (eye - T) * A^-1, so a world plane
        // n.w + d >= 0 is, in eye space, (A^-1 n) . eye + (d - (T A^-1) . n) >= 0.
        const float(*V)[4] = s_sendV;
        float inv[3][3];
        inv[0][0] = V[1][1] * V[2][2] - V[1][2] * V[2][1];
        inv[0][1] = V[0][2] * V[2][1] - V[0][1] * V[2][2];
        inv[0][2] = V[0][1] * V[1][2] - V[0][2] * V[1][1];
        inv[1][0] = V[1][2] * V[2][0] - V[1][0] * V[2][2];
        inv[1][1] = V[0][0] * V[2][2] - V[0][2] * V[2][0];
        inv[1][2] = V[0][2] * V[1][0] - V[0][0] * V[1][2];
        inv[2][0] = V[1][0] * V[2][1] - V[1][1] * V[2][0];
        inv[2][1] = V[0][1] * V[2][0] - V[0][0] * V[2][1];
        inv[2][2] = V[0][0] * V[1][1] - V[0][1] * V[1][0];
        const float det = V[0][0] * inv[0][0] + V[0][1] * inv[1][0] + V[0][2] * inv[2][0];
        if (fabsf(det) < 1e-12f)
            return 0;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                inv[r][c] /= det;
        float tInv[3];  // T * A^-1
        for (int c = 0; c < 3; c++)
            tInv[c] = V[3][0] * inv[0][c] + V[3][1] * inv[1][c] + V[3][2] * inv[2][c];
        // The field box's four sides, inner side positive: x >= -X, x <= X, y >= -Y, y <= Y.
        const float world[4][4] = {
            {1.0f, 0.0f, 0.0f, kFieldHalfX},
            {-1.0f, 0.0f, 0.0f, kFieldHalfX},
            {0.0f, 1.0f, 0.0f, kFieldHalfY},
            {0.0f, -1.0f, 0.0f, kFieldHalfY},
        };
        for (int p = 0; p < 4; p++)
        {
            const float* n = world[p];
            for (int j = 0; j < 3; j++)
                s_clipPlanes[p * 4 + j] = inv[j][0] * n[0] + inv[j][1] * n[1] + inv[j][2] * n[2];
            s_clipPlanes[p * 4 + 3] = n[3] - (tInv[0] * n[0] + tInv[1] * n[1] + tInv[2] * n[2]);
        }
    }
    memcpy(planes16, s_clipPlanes, sizeof(s_clipPlanes));
    return 1;
}

void PortXrProbe() { s_probeState = 1; }

// At a table-layer draw with the probe armed: where this field object's centre lands (NDC).
static void probe_field(uintptr_t matrixHandle)
{
    std::unordered_map<uintptr_t, ProbeInfo>::iterator it = s_probeField.find(matrixHandle);
    if (it == s_probeField.end())
        return;
    const ProbeInfo& p = it->second;
    float e[4];
    for (int k = 0; k < 4; k++)
        e[k] = p.c[0] * s_V[0][k] + p.c[1] * s_V[1][k] + p.c[2] * s_V[2][k] + s_V[3][k];
    const float cx = s_P[0][0] * e[0] + s_P[0][1] * e[1] + s_P[0][2] * e[2] + s_P[0][3] * e[3];
    const float cy = s_P[1][0] * e[0] + s_P[1][1] * e[1] + s_P[1][2] * e[2] + s_P[1][3] * e[3];
    const float cw = s_P[3][0] * e[0] + s_P[3][1] * e[1] + s_P[3][2] * e[2] + s_P[3][3] * e[3];
    fprintf(stderr,
            "[xr-probe] hash=%08lx flags=%08lx objflags=%08lx centre (%.1f %.1f %.1f) size (%.1f %.1f %.1f) ndc "
            "(%.2f %.2f)\n",
            p.hash, p.creation, p.objectFlags, p.c[0], p.c[1], p.c[2], p.ext[0], p.ext[1], p.ext[2],
            cw != 0.0f ? cx / cw : 0.0f, cw != 0.0f ? cy / cw : 0.0f);
    s_probeField.erase(it); // once per object
}

void PortXrNoteCameraSide(const DrawableObject* object, bool hiddenForCamera)
{
    s_cameraSideHidden[object] = hiddenForCamera;
}

// r42: the life-size scorer's handle this frame (whether its eye pass is being sent: s_scorerLayer).
static uintptr_t s_scorerHandle = 0;

void PortXrCountDraw(int view)
{
    if (view < 0 || view >= kCensusViews)
        return;
    const int send = !s_eyeSend ? 0 : (s_scorerLayer ? 3 : (s_tableLayer ? 1 : 2));
    s_census[send][view]++;
}

void PortXrTakeDrawCensus(char* out, unsigned long size, unsigned long frames)
{
    static const char* kSend[kCensusSends] = {"broadcast", "table", "stadium", "scorer"};
    if (out == NULL || size == 0)
        return;
    out[0] = '\0';
    const double n = frames > 0 ? (double)frames : 1.0;
    unsigned long used = 0;
    for (int s = 0; s < kCensusSends; s++)
    {
        unsigned long total = 0;
        for (int v = 0; v < kCensusViews; v++)
            total += s_census[s][v];
        if (total == 0)
            continue;
        int w = snprintf(out + used, size - used, "%s%s %.0f (", used ? " | " : "", kSend[s], total / n);
        if (w < 0 || (unsigned long)w >= size - used)
            break;
        used += (unsigned long)w;
        // The four biggest views by draws.
        bool taken[kCensusViews] = {};
        for (int k = 0; k < 4; k++)
        {
            int best = -1;
            for (int v = 0; v < kCensusViews; v++)
                if (!taken[v] && s_census[s][v] > 0 && (best < 0 || s_census[s][v] > s_census[s][best]))
                    best = v;
            if (best < 0)
                break;
            taken[best] = true;
            w = snprintf(out + used, size - used, "%sv%d %.0f", k ? " " : "", best, s_census[s][best] / n);
            if (w < 0 || (unsigned long)w >= size - used)
                break;
            used += (unsigned long)w;
        }
        w = snprintf(out + used, size - used, ")");
        if (w < 0 || (unsigned long)w >= size - used)
            break;
        used += (unsigned long)w;
    }
    // r58: the eye sends' stadium cull test (see s_cullDiag).
    if (s_cullDiag[0] + s_cullDiag[1] + s_cullDiag[2] > 0 && used < size)
        snprintf(out + used, size - used, " | eye cull: tested %.0f culled %.0f off %.0f unbounded %.0f",
                 s_cullDiag[2] / n, s_cullDiag[3] / n, s_cullDiag[0] / n, s_cullDiag[1] / n);
    memset(s_census, 0, sizeof(s_census));
    memset(s_cullDiag, 0, sizeof(s_cullDiag));
}

void PortXrLayersNewMatch(int stadium)
{
    // Logging only: the classification itself stays as it was (objects seen before keep their answer).
    s_logged = 0;
    s_cullSamples = 0;
    s_tableThinObjects = 0;  // r98
    s_stadiumId = stadium;
    fprintf(stderr, "[xr-layer] new match, stadium %d\n", stadium);
}

void PortXrSetScorerHandle(uintptr_t matrixHandle) { s_scorerHandle = matrixHandle; }
void PortXrSetScorerLayer(bool scorer) { s_scorerLayer = scorer; }

bool PortXrSkipPacketDraw(uintptr_t matrixHandle)
{
    // r42: the scorer pass draws the life-size scorer and nothing else; every other send leaves it out.
    if (s_scorerLayer)
        return s_scorerHandle == 0 || matrixHandle != s_scorerHandle;
    if (s_scorerHandle != 0 && matrixHandle == s_scorerHandle)
        return true;
    if (s_probeState == 2 && s_eyeSend && s_tableLayer && s_cullOn)
        probe_field(matrixHandle);
    // Camera-side objects the game hides from its camera: kept for the life-size stadium only, left
    // out of the broadcast view and of the Tabletop field (r21 had the hidden centre-line sign back
    // on the table in Tabletop · Stadium).
    if ((!s_eyeSend || s_tableLayer) && !s_normalSkip.empty() && s_normalSkip.count(matrixHandle) != 0)
        return true;
    // r98: Konga's small border pieces, off the table (classify)
    if (s_eyeSend && s_tableLayer && !s_tableThin.empty() && s_tableThin.count(matrixHandle) != 0)
        return true;
    // The stadium layer's life-size field: all of it (self-test), or the standing parts when the
    // table carries the game anyway.
    if (s_eyeSend && !s_tableLayer && !s_field.empty() && (s_tuneSkipStadiumField || s_bothLayers) &&
        s_field.count(matrixHandle) != 0)
        return true;
    if (s_stadium.empty())
        return false;
    std::unordered_map<uintptr_t, Mark>::iterator it = s_stadium.find(matrixHandle);
    if (it == s_stadium.end())
        return false;
    if (s_tableLayer)
        return true;
    Mark& mark = it->second;
    if (s_eyeSend && ((mark.crowd && s_tuneSkipCrowd) || (!mark.crowd && s_tuneSkipStands)))
        return true;
    if (s_eyeSend && (!s_cullOn || !mark.bounded))
        ++s_cullDiag[!s_cullOn ? 0 : 1];
    if (!s_cullOn || !mark.bounded)
        return false;
    if (mark.cullSerial != s_cullSerial)
    {
        mark.cullSerial = s_cullSerial;
        mark.culled = outside_frustum(mark.mn, mark.mx) ||
                      (s_eyeSend && !s_tableLayer && s_lodMinPx > 0.0f && !mark.crowd && too_small(mark.mn, mark.mx));
        if (mark.culled)
            ++s_culledThisFrame;
        if (s_eyeSend)
        {
            ++s_cullDiag[2];
            if (mark.culled)
                ++s_cullDiag[3];
            // Far stands (camera side or across the pitch), which some eye usually has behind it.
            if (s_cullSamples < 4 && fabsf(0.5f * (mark.mn[1] + mark.mx[1])) > 20.0f)
            {
                ++s_cullSamples;
                log_cull_sample(mark);
            }
        }
    }
    return mark.culled;
}

void PortXrSetTableLayer(bool table) { s_tableLayer = table; }

unsigned long PortXrLayersTakeCulled()
{
    const unsigned long n = s_culledTotal;
    s_culledTotal = 0;
    return n;
}

void PortXrLayersEndFrame()
{
    s_scorerHandle = 0;  // r42: a frame-memory handle, someone else's next frame
    // The probe collects one frame's field objects, logs them at that frame's table send.
    if (s_probeState == 2)
    {
        s_probeState = 0;
        s_probeField.clear();
    }
    else if (s_probeState == 1)
    {
        s_probeState = 2;
    }
    s_stadium.clear();
    s_field.clear();
    s_clipField.clear();
    s_tableThin.clear();  // r98
    s_sendVValid = false;
    s_normalSkip.clear();
    if (!PortXrStereoWorld())
        s_cameraSideHidden.clear(); // objects come and go with matches; start fresh outside 3D
    s_eyeSend = false;
    s_tableLayer = false;
    s_cullOn = false;
    s_culledTotal += s_culledThisFrame;
    s_culledThisFrame = 0;
}
