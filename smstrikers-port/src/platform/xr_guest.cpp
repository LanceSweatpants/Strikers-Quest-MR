// strikers-quest r65: guest characters, see include/port/xr_guest.h.
//
// guests.txt (STRIKERS_HOME): one line per Strikers class to replace, "<class> <pack> [scale]",
// e.g. "toad boo". The pack is STRIKERS_HOME/guests/<pack>.gpk (tools/outside/guestpack.py).
//
// Per frame, for a character whose class has a guest: the Strikers animation it is playing (its
// controller's clip, by name hash, and the clip's normalised time) picks the guest's own clip of
// the same name (or the nearest kind of move), which poses the guest's skeleton (its own bone
// lengths); the root follows the Strikers character's root, so it stands, runs and faces where the
// game puts it. Skinned on the CPU (4 weights a vertex, the guest's inverse binds) into one strip
// model per packet, each given the Strikers character's prepared material (program, lights, dirt)
// with the guest's own texture. Rendering only: nothing the match runs on is touched.

#include "port/xr_guest.h"
#include "port/franchise.h"
#include "port/xr_net.h"
#include "port/guest_validation.h"
#include "port/lineup.h"  // r78: the co-captain
#include <errno.h>

#include "Game/Character.h"
#include "Game/CharacterTweaks.h"
#include "Game/FE/feHelpFuncs.h"
#include "Game/GameInfo.h"
#include "Game/Player.h"
#include "Game/Team.h"
#include "NL/nlLocalization.h"
#include "Game/GL/GLSkinMesh.h"
#include "Game/GL/ShaderSkinMesh.h"
#include "Game/SHierarchy.h"
#include "Game/GL/gluMeshWriter.h"
#include "Game/PoseAccumulator.h"
#include "Game/SAnim.h"
#include "Game/SAnim/pnSAnimController.h"
#include "Game/SAnim/pnBlender.h"            // r102: the pose trees a cut-scene / replay draws from
#include "Game/SAnim/pnFeather.h"
#include "Game/SAnim/pnSingleAxisBlender.h"
#include "NL/gc/gcSwizzler.h"
#include "NL/gl/glMatrix.h"
#include "NL/gl/glModel.h"
#include "NL/gl/glUserData.h"
#include "NL/glx/glxTexture.h"
#include "NL/nlMemory.h"
#include "NL/nlString.h"
#include "NL/platvmath.h"  // nlInvertMatrix

#include <math.h>
#include <cmath>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <string>
#include <strings.h>
#include <mutex>
#include <time.h>  // r73: the flame flipbook's clock
#include <unordered_map>
#include <vector>

extern unsigned int g_portGlFrame;  // glxMemory.cpp: +1 at each frame memory flip

namespace
{

const int kMaxNodes = 160;  // Melee's Zelda has 118 joints

struct GNode
{
    u32 id;
    int parent;
    float trans[3];
    bool hasInv;
    float inv[16];  // row vectors: v * inv * world
    // r66, retarget packs (GPK2, Melee): the Strikers bone this one follows (its nlStringLowerHash,
    // 0 none), its bind pose in model space, and that relative to its parent's.
    u32 map;
    float bindWorld[16];
    float bindLocal[16];
};

// r66: for a retarget guest standing in for one Strikers class, that class's bones it follows:
// the Strikers node index, the inverse of its bind rotation and its bind position (model space).
struct HostRig
{
    bool ready;
    int hostNode[kMaxNodes];
    float hostInvBindRot[kMaxNodes][9];
    float hostBindPos[kMaxNodes][3];
    bool rootLike[kMaxNodes];  // bip01 / bip01 pelvis: placed where the Strikers one is, scaled
    float k[kMaxNodes];        // guest bind height / Strikers bind height there
    float hostHipZ[kMaxNodes]; // r69: that Strikers height (its standing hips' when hostref has them)
    // r66e: the guest bone's own scale in its bind (each axis; Melee Pokemon joints carry 0.8-4.6)
    // and the Strikers bone's (its scale now is applied relative to it).
    float guestScale[kMaxNodes][3];
    float hostBindScale[kMaxNodes];
    // r68: hangs off the hips and follows nothing (a tail): keeps its bind turn to the ground
    bool hipChild[kMaxNodes];
    // r101 (Lance: Poliwag "distorted af" in his Super Strike and its replay; "fix this for alllll guest
    // characters"): a Strikers character tumbling or jumping high tore bodies apart (Poliwag, Magmar 25x stretched,
    // Weezing, Gengar, Dratini: tools/outside on the PC, all 87 guests measured). The body's own hips (bip01 / pelvis
    // with no other followed bone above them: placed where the Strikers hips are).
    bool hipsBody[kMaxNodes];
    int hipsTop;  // the top one of those placed where the Strikers hips are: they all move by its factors
};
// r101: a tail stays upright (r68) while the hips are within this of their standing turn (every move of normal
// play), goes with them fully past kTailFollow (a Super Strike, a bomb, a tackle tumble the body 120-160 degrees),
// blended between
const float kTailUpright = 45.0f * 3.14159265f / 180.0f;
const float kTailFollow = 100.0f * 3.14159265f / 180.0f;

// r68: guests/hostref.txt (tools/outside/hostref.py): each Strikers class's standing pose (its idle
// clip's first key) for the trunk bones, relative to the hierarchy's root node: the rotation (row
// vectors) and the position. The retarget turns a guest's trunk the way the Strikers trunk turns from
// that, not from the skin's bind (a T pose; DK's hunch went on top of Charizard's own: flat on his
// belly), and scales the guest's hip height from the standing hips (the bind puts Luigi's hips 0.05 m
// up and Peach's and Daisy's under the floor: guests floated metres up or sank there).
struct HostRef
{
    float rot[9];
    float pos[3];
};
std::unordered_map<u32, HostRef> s_hostRef[12];
bool s_hostRefRead = false;

struct GVert
{
    float pos[3];
    float nrm[3];
    float uv[2];
    u8 node[4];
    float w[4];
};

struct GPacket
{
    u32 texHash;
    PlatTexture* texture;
    std::vector<GVert> verts;
    std::vector<u16> strip;
    std::vector<s16> uvs;  // r66e: the coordinates down the strip (s16 / 1024), the same every frame
    // r66e: its texture has see-through texels (alpha bits in its header, as glxLoadModel reads
    // them): drawn with the alpha test the game's own loader gives such textures (> 0x40), not as
    // solid cards. The Strikers character's raster state turned that way, kept for the next frame.
    bool alphaTest;
    bool soft;  // r69: gas / mist: alpha blended, no depth write (see the raster block)
    int flameFrame;  // r73: a flame flipbook frame (0-2) drawn only in its turn, -1 not a flame
    // r101 (Lance: "get the smoke around gastly to be more SMOKE like and not like a solid object"): a gas card
    // (soft, not a flame): not drawn; where they are says where the gas is, and real smoke puffs rise there
    // (smoke_model)
    bool gas;
    mutable u32 rasterFrom;
    mutable u32 rasterTo;
};

struct GAnim
{
    u32 hash;  // nlStringLowerHash of its name, as cSAnim::GetHashID
    std::string name;
    u32 keys;
    std::vector<float> data;  // keys x nodes x (q xyzw, t xyz)
    std::vector<u8> hasTrans;
};

struct Guest
{
    std::string name;
    float scale;
    float height;
    std::vector<GNode> nodes;
    std::vector<GPacket> packets;
    std::vector<GAnim> anims;
    std::unordered_map<u32, int> byHash;
    int idle, run, pass, kick, hit, wave;
    int celebrate;      // r77: its own goal celebration clip (the scorer show), -1 none
    u32 gasRgb = 0;     // r101: its gas's colour (0xFFRRGGBB, its gas cards' picture), 0 no gas
    bool retarget;      // r66: moves with the Strikers character's own animations (GPK2)
    u32 mode;           // r69: GPK2 mode bits: 1 retarget, 2 flies (flapping wings, bob), 4 floats (bob)
    HostRig rig[12];    // per Strikers class it stands in for
    float armScale;     // r97: its arms' size (guests/limbs.txt), 1 = as built
};

struct CharState
{
    bool started;
    int anim;
    float local[kMaxNodes][7];
    float from[kMaxNodes][7];
    float blend;  // 1 -> 0 over the cross-fade from `from`
    // r102: how fast its body travels (pace_clip): last place and when, speed (units a second, smoothed),
    // ground covered (the run's stride clock), whether it runs now, the frame this was taken
    float paceX, paceY, speed, travel;
    double paceT;
    bool running;
    unsigned int paceFrame;
};

void mul(const float* a, const float* b, float* out);

const char* kClassNames[12] = {"birdo", "daisy", "donkeykong", "hammerbros", "koopa", "luigi",
                               "mario", "peach", "toad", "waluigi", "wario", "yoshi"};

bool s_configRead = false;
unsigned int s_recoveryFrame = 0xFFFFFFFFu;
unsigned int s_recoveryFrames = 0;
bool s_recoveryPending = false;
Guest* s_guestFor[12] = {};
std::unordered_map<const cCharacter*, CharState> s_state;
// r77: the life-size scorer's copy being drawn (PortGuestScorerPose): what it was posed with, and its
// own cross-fade state (the field character's stays its own)
bool s_scorerActive = false;
const cSAnim* s_scorerClip = NULL;
float s_scorerNorm = 0.0f, s_scorerCelebrate = 0.0f;
CharState s_scorerState;
std::unordered_map<u32, u32> s_alias;  // Strikers clip hash -> the guest clip it stands for
std::vector<glModel*> s_frameModels;
std::unordered_map<u32, bool> s_unmatchedLogged;

// r102 (Lance: Bowser Jr. "gliding across the field upon goals scored"): what a drawn character's pose
// (its DrawableCharacter's accumulator) was evaluated from. Live play draws the character's own
// controller; the goal cut-scene (Nis::Render) and replays draw a clip of their own while the live
// controller sits paused, so a guest posed from its own clips (Charged) held that paused frame while the
// cut-scene carried it across the field. A fixed table (no rehash under a draw): the snapshots' few.
struct PoseNote
{
    const void* acc;
    const cSAnim* anim;  // null with tree false: the live character's own controller
    float t;
    bool tree;
};
PoseNote s_notes[64];
int s_numNotes = 0;

PoseNote* note_for(const void* acc, bool add)
{
    for (int i = 0; i < s_numNotes; i++)
        if (s_notes[i].acc == acc)
            return &s_notes[i];
    if (!add || acc == NULL)
        return NULL;
    PoseNote* n = &s_notes[s_numNotes < 64 ? s_numNotes++ : (int)(((uintptr_t)acc >> 4) % 64)];
    n->acc = acc;
    n->anim = NULL;
    n->t = 0.0f;
    n->tree = false;
    return n;
}

// The clip with the most weight in a pose tree, as its nodes weigh their children (cPN_Blender's
// smoothstep, cPN_SingleAxisBlender's pair; a cPN_Feather's base child carries the legs).
void heaviest_clip(cPoseNode* node, float w, const cPN_SAnimController** best, float* bestW, int depth)
{
    if (node == NULL || depth > 12 || w <= 0.0f)
        return;
    switch (node->GetType())
    {
    case 2:
    {
        const cPN_SAnimController* c = static_cast<const cPN_SAnimController*>(node);
        if (c->m_pSAnim != NULL && w > *bestW)
        {
            *best = c;
            *bestW = w;
        }
        return;
    }
    case 0:
    {
        const float b = static_cast<cPN_Blender*>(node)->m_fBlendTime;
        const float f = b * (b * (-2.0f * b + 3.0f));
        heaviest_clip(node->GetChild(0), w * (1.0f - f), best, bestW, depth + 1);
        heaviest_clip(node->GetChild(1), w * f, best, bestW, depth + 1);
        return;
    }
    case 1:
        heaviest_clip(node->GetChild(0) != NULL ? node->GetChild(0) : node->GetChild(1), w, best, bestW, depth + 1);
        return;
    case 3:
    {
        const cPN_SingleAxisBlender* s = static_cast<cPN_SingleAxisBlender*>(node);
        const int n = node->GetNumChildren();
        if (n <= 0)
            return;
        if (s->m_fSmoothedWeight >= 1.0f || n == 1)
        {
            heaviest_clip(node->GetChild(n - 1), w, best, bestW, depth + 1);
            return;
        }
        const float scaled = (s->m_fSmoothedWeight < 0.0f ? 0.0f : s->m_fSmoothedWeight) * (float)(n - 1);
        const int i = (int)scaled;
        const float frac = scaled - (float)i;
        heaviest_clip(node->GetChild(i), w * (1.0f - frac), best, bestW, depth + 1);
        heaviest_clip(node->GetChild(i + 1), w * frac, best, bestW, depth + 1);
        return;
    }
    default:
        return;
    }
}

// r102: a standing clip (idle, a wave, its celebration) on a body that travels (the goal cut-scene's runs,
// a replay, any clip no guest has) slid it along the grass: its run instead, a stride per its run's length
// of travel. Speed in field units a second, taken once a frame (the eyes and the screen draw it twice).
const float kPaceStart = 1.0f, kPaceStop = 0.6f;  // units a second (a run is about 6)
const float kPaceRunSpeed = 6.0f;                 // the speed a guest's run clip is paced for

int pace_clip(const Guest* g, CharState& st, int clip, const float* rootM, float* t)
{
    if (st.paceFrame != g_portGlFrame)
    {
        st.paceFrame = g_portGlFrame;
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const float x = rootM[12], y = rootM[13];
        const double dt = now - st.paceT;
        if (st.paceT > 0.0 && dt > 0.0005 && dt < 0.25)
        {
            const float d = sqrtf((x - st.paceX) * (x - st.paceX) + (y - st.paceY) * (y - st.paceY));
            if (d < 3.0f)  // more is a cut (the replay's start, a reset): no speed from it
            {
                st.speed += (d / (float)dt - st.speed) * 0.3f;
                st.travel += d;
            }
        }
        else
            st.speed = 0.0f;
        st.paceX = x;
        st.paceY = y;
        st.paceT = now;
    }
    const bool standing = clip == g->idle || clip == g->wave || clip == g->celebrate;
    if (!standing || g->run < 0)
    {
        st.running = false;
        return clip;
    }
    const bool was = st.running;
    st.running = st.running ? st.speed > kPaceStop : st.speed > kPaceStart;
    if (!st.running)
        return clip;
    static std::unordered_map<const Guest*, bool> s_logged;
    if (!was && !s_logged[g])
    {
        s_logged[g] = true;
        fprintf(stderr, "[guest] %s: travelling %.1f units a second on \"%s\": runs instead\n", g->name.c_str(),
                st.speed, g->anims[clip].name.c_str());
        fflush(stderr);
    }
    const GAnim& run = g->anims[g->run];
    const float stride = kPaceRunSpeed * (run.keys > 1 ? (float)run.keys / 30.0f : 0.6f);
    const float n = st.travel / stride;
    *t = n - floorf(n);
    return g->run;
}

std::string home_dir()
{
    const char* home = getenv("STRIKERS_HOME");
    return home != NULL && *home != '\0' ? home : "/storage/emulated/0/Strikers";
}

// Recovery applies to roster picks as well as legacy class replacements.
bool guests_enabled()
{
    static bool checked = false;
    static bool enabled = true;
    if (!checked)
    {
        checked = true;
        const std::string path = home_dir() + "/guests.lock";
        if (FILE* f = fopen(path.c_str(), "r"))
        {
            fclose(f);
            remove(path.c_str());
            enabled = false;
            fprintf(stderr, "[guest] guests disabled for this run after an interrupted guest load/draw\n");
        }
    }
    return enabled;
}

void mark_guest_load()
{
    if (FILE* f = fopen((home_dir() + "/guests.lock").c_str(), "w"))
        fclose(f);
    s_recoveryFrames = 0;
    s_recoveryPending = true;
}

u32 rd32(const u8*& p)
{
    u32 v;
    memcpy(&v, p, 4);
    p += 4;
    return v;
}

float rdf(const u8*& p)
{
    float v;
    memcpy(&v, p, 4);
    p += 4;
    return v;
}

u32 be32(const u8* p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

u16 be16(const u8* p)
{
    return (u16)((p[0] << 8) | p[1]);
}

// A Charged texture record (GXTextureHeader + GX data + palette, big-endian, as the game's own
// bundles hold them) as a texture of our own: never in the game's inventory, so no match unload
// takes it away.
PlatTexture* make_texture(const u8* rec, u32 size)
{
    if (size < 0x20)
        return NULL;
    const u32 levels = be32(rec + 0);
    const u32 fmt = be32(rec + 4);
    const u16 w = be16(rec + 0x0E);
    const u16 h = be16(rec + 0x10);
    const u32 npal = be32(rec + 0x14);
    if (fmt >= (u32)GXTex_Num || w == 0 || h == 0 || w > 1024 || h > 1024 || levels == 0 || levels > 12
        || npal > 256 || (fmt == GXTex_CI8 && npal == 0))
        return NULL;
    const u32 texSize = GCTextureSize((eGXTextureFormat)fmt, w, h, (int)levels, (unsigned long)-1);
    if ((size_t)texSize > size - 0x20 || (size_t)npal * 2 > size - 0x20 - (size_t)texSize)
        return NULL;
    void* data = malloc(texSize);
    if (data == NULL)
        return NULL;
    u16* palette = npal ? (u16*)malloc((size_t)npal * 2) : NULL;
    if (npal && palette == NULL)
    {
        free(data);
        return NULL;
    }
    memcpy(data, rec + 0x20, texSize);
    // r66f: the texture object from the game's own allocator: glx_GetTex takes a raw PlatTexture
    // pointer only inside the port's region (port_region_owns + "PTEX"); a malloc'd one was never
    // found, so guests drew with no texture (Lance's Charizard: purple / pink / grey flashing).
    void* mem = nlMalloc(sizeof(PlatTexture), 32, false);
    if (mem == NULL)
    {
        free(palette);
        free(data);
        return NULL;
    }
    PlatTexture* t = new (mem) PlatTexture();
    t->CreateWithMemory(w, h, (eGXTextureFormat)fmt, (int)levels, data);
    memcpy(t->m_Bits, rec + 8, 4);
    t->m_bMissingTexture = rec[0x0C] != 0;
    if (npal != 0)
    {
        t->m_PaletteData = palette;
        memcpy(t->m_PaletteData, rec + 0x20 + texSize, npal * 2);
        t->m_nPaletteEntries = (s16)npal;
    }
    t->Prepare();
    return t;
}

// r97 (Lance: "make pikachu's arms smaller that shit looks CRAZY"): guests/limbs.txt, "<pack> arms <scale>"
// lines (a shiny pack takes its normal pack's): the mapped upper arms, forearms and hands drawn that much
// smaller round the shoulder (retarget_pose), what hangs off them following. Colosseum's Pikachu has long
// thin arms the Strikers arm moves let hang to its knees (PC check, retargetsim RETARGET_ARMS: 0.55).
float arm_scale_for(const std::string& pack)
{
    static std::unordered_map<std::string, float> s_limbs;
    static bool s_read = false;
    if (!s_read)
    {
        s_read = true;
        if (FILE* f = fopen((home_dir() + "/guests/limbs.txt").c_str(), "r"))
        {
            char line[160];
            while (fgets(line, sizeof line, f) != NULL)
            {
                char key[64], what[16];
                float v = 1.0f;
                if (line[0] != '#' && sscanf(line, "%63s %15s %f", key, what, &v) == 3 && strcmp(what, "arms") == 0 &&
                    v > 0.1f && v <= 2.0f)
                    s_limbs[key] = v;
            }
            fclose(f);
            fprintf(stderr, "[guest] limbs.txt: %d guests with their own arm size\n", (int)s_limbs.size());
            fflush(stderr);
        }
    }
    std::string base = pack;
    const size_t shiny = base.find("_shiny");
    if (shiny != std::string::npos)
        base.resize(shiny);
    std::unordered_map<std::string, float>::const_iterator it = s_limbs.find(base);
    return it != s_limbs.end() ? it->second : 1.0f;
}

Guest* load_pack(const std::string& name, float scale)
{
    if (!guests_enabled())
        return NULL;
    const std::string path = home_dir() + "/guests/" + name + ".gpk";
    FILE* f = fopen(path.c_str(), "rb");
    if (f == NULL)
    {
        fprintf(stderr, "[guest] %s: no pack at %s\n", name.c_str(), path.c_str()); fflush(stderr);
        return NULL;
    }
    mark_guest_load();
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || (unsigned long)size > portguest::kMaxPackBytes)
    {
        fclose(f);
        return NULL;
    }
    std::vector<u8> buf((size_t)size);
    const bool ok = size > 64 && fread(buf.data(), 1, buf.size(), f) == buf.size();
    fclose(f);
    const bool v2 = ok && memcmp(buf.data(), "GPK2", 4) == 0;
    if (!ok || !portguest::valid_pack(buf.data(), buf.size()))
    {
        fprintf(stderr, "[guest] %s: %s is not a guest pack\n", name.c_str(), path.c_str()); fflush(stderr);
        return NULL;
    }
    const u8* p = buf.data() + 4;
    const u8* end = buf.data() + buf.size();
    rd32(p);  // version
    const u32 nNodes = rd32(p), nPackets = rd32(p), nTex = rd32(p), nAnims = rd32(p);
    Guest* g = new Guest();
    g->name = name;
    g->scale = scale;
    g->armScale = arm_scale_for(name);  // r97
    g->height = rdf(p);
    p += 32;  // name
    g->mode = v2 ? rd32(p) : 0;
    g->retarget = (g->mode & 1) != 0;
    memset(g->rig, 0, sizeof g->rig);
    if (nNodes == 0 || nNodes > (u32)kMaxNodes)
    {
        fprintf(stderr, "[guest] %s: %u nodes, more than this build handles\n", name.c_str(), nNodes); fflush(stderr);
        delete g;
        return NULL;
    }
    g->nodes.resize(nNodes);
    for (u32 i = 0; i < nNodes; i++)
    {
        GNode& n = g->nodes[i];
        n.id = rd32(p);
        n.parent = (int)rd32(p);
        for (int k = 0; k < 3; k++)
            n.trans[k] = rdf(p);
        n.hasInv = rd32(p) != 0;
        for (int k = 0; k < 16; k++)
            n.inv[k] = rdf(p);
        n.map = 0;
        memset(n.bindWorld, 0, sizeof n.bindWorld);
        n.bindWorld[0] = n.bindWorld[5] = n.bindWorld[10] = n.bindWorld[15] = 1.0f;
        if (v2)
        {
            n.map = rd32(p);
            for (int k = 0; k < 16; k++)
                n.bindWorld[k] = rdf(p);
        }
        if (n.parent >= (int)i)
            n.parent = -1;  // parents come first; anything else is no tree we can walk
    }
    // r66: each node's bind pose relative to its parent's (row vectors: world * inverse(parent world)).
    for (u32 i = 0; i < nNodes; i++)
    {
        GNode& n = g->nodes[i];
        if (n.parent < 0)
        {
            memcpy(n.bindLocal, n.bindWorld, sizeof n.bindLocal);
            continue;
        }
        nlMatrix4 pw, pinv;
        memcpy(&pw, g->nodes[n.parent].bindWorld, 64);
        nlInvertMatrix(pinv, pw);
        float pi[16];
        memcpy(pi, &pinv, 64);
        mul(n.bindWorld, pi, n.bindLocal);
    }
    std::unordered_map<u32, PlatTexture*> tex;
    std::unordered_map<u32, u32> texAvg;  // r101: an RGBA8 picture's average colour (0xRRGGBB) where it shows
    for (u32 i = 0; i < nTex && p + 8 <= end; i++)
    {
        const u32 hash = rd32(p), sz = rd32(p);
        if (p + sz > end)
            break;
        PlatTexture* t = make_texture(p, sz);
        if (t != NULL)
            tex[hash] = t;
        if (sz > 32 && p[7] == 3)
        {
            // (GX RGBA8 tiles after the 32-byte header: 32 bytes AR, then 32 GB, per 4 x 4)
            double r = 0, gg = 0, b = 0, wsum = 0;
            for (u32 o = 32; o + 64 <= sz; o += 64)
                for (int k = 0; k < 16; k++)
                {
                    const double a = p[o + k * 2] / 255.0;
                    r += a * p[o + k * 2 + 1];
                    gg += a * p[o + 32 + k * 2];
                    b += a * p[o + 32 + k * 2 + 1];
                    wsum += a;
                }
            if (wsum > 1.0)
                texAvg[hash] = ((u32)(r / wsum) << 16) | ((u32)(gg / wsum) << 8) | (u32)(b / wsum);
        }
        p += (sz + 3) & ~3u;
    }
    g->packets.resize(nPackets);
    for (u32 i = 0; i < nPackets && p + 12 <= end; i++)
    {
        GPacket& pk = g->packets[i];
        pk.texHash = rd32(p);
        u32 nv = rd32(p);
        const u32 ni = rd32(p);
        // r69: top bit = a soft packet (Gastly's gas puffs): blended, see-through, drawn after the body.
        // r73: 0x40000000 = a flame (the Charmander line's tail fire, tools/outside/colo_flame.py): soft
        // too, one of a flipbook whose frame (0-2) is in 0x30000000, shown in turn.
        pk.soft = (nv & 0xC0000000u) != 0;
        pk.flameFrame = (nv & 0x40000000u) != 0 ? (int)((nv >> 28) & 3u) : -1;
        pk.gas = (nv & 0x80000000u) != 0 && (nv & 0x40000000u) == 0;
        if (pk.gas && g->gasRgb == 0)
            g->gasRgb = texAvg.count(pk.texHash) ? texAvg[pk.texHash] | 0xFF000000u : 0xFF5C38C8u;
        nv &= 0x0FFFFFFFu;
        pk.texture = tex.count(pk.texHash) ? tex[pk.texHash] : NULL;
        pk.alphaTest = pk.texture != NULL && pk.texture->m_Bits[3] != 0;  // 0xFF (unknown) included
        pk.rasterFrom = 0xFFFFFFFFu;
        pk.rasterTo = 0;
        pk.verts.resize(nv);
        for (u32 v = 0; v < nv; v++)
        {
            GVert& gv = pk.verts[v];
            memcpy(gv.pos, p, 32);
            p += 32;
            // r82 (Lance: "characters are still showing up darker or almost see through"): the Melee and
            // Colosseum packs (GPK2, tools/outside/meleepack.py) carry normals made from the triangles
            // as cross(b - a, c - a), but GX triangles turn the other way round, so they point INTO the
            // body (posed and ray-tested on the PC: 120 GPK2 packs average 25% outward - Pichu 17%,
            // Machop 22%, Fox 20%, Young Link 32% - the Charged GPK1 ones 62%). The game lights from the
            // normals (the light ramp is read from the lit colour), so the sides facing the lights came
            // out in shadow. Turned round here.
            if (v2)
            {
                gv.nrm[0] = -gv.nrm[0];
                gv.nrm[1] = -gv.nrm[1];
                gv.nrm[2] = -gv.nrm[2];
            }
            memcpy(gv.node, p, 4);
            p += 4;
            memcpy(gv.w, p, 16);
            p += 16;
            for (int k = 0; k < 4; k++)
                if (gv.node[k] >= nNodes)
                    gv.w[k] = 0.0f;
        }
        pk.strip.resize(ni);
        memcpy(pk.strip.data(), p, ni * 2);
        p += (ni * 2 + 3) & ~3u;
        for (u32 k = 0; k < ni; k++)
            if (pk.strip[k] >= nv)
                pk.strip[k] = 0;
        pk.uvs.resize((size_t)ni * 2);
        for (u32 k = 0; k < ni && nv > 0; k++)
        {
            for (int c = 0; c < 2; c++)
            {
                float f = 1024.0f * pk.verts[pk.strip[k]].uv[c];
                f = f < -32768.0f ? -32768.0f : (f > 32767.0f ? 32767.0f : f);
                pk.uvs[(size_t)k * 2 + c] = (s16)f;
            }
        }
    }
    const size_t keyBytes = (size_t)nNodes * 7 * 4;
    for (u32 a = 0; a < nAnims && p + 52 <= end; a++)
    {
        GAnim an;
        char nm[49] = {};
        memcpy(nm, p, 48);
        p += 48;
        an.name = nm;
        an.hash = nlStringLowerHash(nm);
        an.keys = rd32(p);
        if (an.keys == 0 || p + an.keys * keyBytes > end)
            break;
        an.data.resize((size_t)an.keys * nNodes * 7);
        memcpy(an.data.data(), p, an.keys * keyBytes);
        p += an.keys * keyBytes;
        an.hasTrans.assign(p, p + nNodes);
        p += (nNodes + 3) & ~3u;
        g->byHash[an.hash] = (int)g->anims.size();
        g->anims.push_back(an);
    }
    auto find = [&](const char* n) {
        auto it = g->byHash.find(nlStringLowerHash(n));
        return it != g->byHash.end() ? it->second : -1;
    };
    g->idle = find("idle");
    g->run = find("run_with_ball");
    g->pass = find("pass_600");
    g->kick = find("windup_kick");
    g->hit = find("hit_react_1_600");
    g->wave = find("wave");
    // r77: its own celebration for the life-size scorer show (the first clip named like one)
    g->celebrate = -1;
    for (const char* want : {"celebrat", "victory", "cheer", "win"})
    {
        for (size_t a = 0; a < g->anims.size() && g->celebrate < 0; a++)
        {
            std::string lower = g->anims[a].name;
            for (char& ch : lower)
                ch = (char)tolower((unsigned char)ch);
            if (lower.find(want) != std::string::npos)
                g->celebrate = (int)a;
        }
        if (g->celebrate >= 0)
            break;
    }
    if (g->idle < 0 && !g->anims.empty())
        g->idle = 0;
    fprintf(stderr, "[guest] %s: %u nodes, %u packets, %zu of %u textures, %zu clips, height %.2f, scale %.2f\n",
            name.c_str(), nNodes, nPackets, tex.size(), nTex, g->anims.size(), g->height, scale); fflush(stderr);
    return g;
}

void add_alias(const char* strikers, const char* guest)
{
    s_alias[nlStringLowerHash(strikers)] = nlStringLowerHash(guest);
}

void read_config()
{
    s_configRead = true;
    // Strikers clips with no clip of the same name in Charged: the closest move there.
    add_alias("hit_react_1200", "hit_react_1_1200");
    add_alias("hit_react_300", "hit_react_1_300");
    add_alias("hit_react_600", "hit_react_1_600");
    add_alias("idle_turn_300", "idle_to_run_turn_300");
    add_alias("idle_turn_600", "idle_to_run_turn_600");
    add_alias("slide_tackle_up_fail", "slide_tackle_react_1200");
    add_alias("strafe_hard_stop_right", "strafe_to_idle");
    add_alias("sts_hit_react", "hit_react_3_1200");
    add_alias("turbo_run", "run_with_ball");
    add_alias("turbo_run_lean_left", "run_with_ball_lean_left");
    add_alias("turbo_run_lean_right", "run_with_ball_lean_right");
    add_alias("turbo_turn_with_ball_300", "hard_stop_turn_with_ball");
    add_alias("turbo_turn_with_ball_600", "hard_stop_turn_with_ball");
    add_alias("turbo_with_ball_level_1", "run_with_ball");
    add_alias("turbo_with_ball_level_2", "run_with_ball");
    add_alias("turbo_with_ball_level_3", "run_with_ball");
    add_alias("windup_powershot", "windup");

    if (!guests_enabled())
        return;
    const std::string path = home_dir() + "/guests.txt";
    FILE* f = fopen(path.c_str(), "r");
    if (f == NULL)
        return;
    std::unordered_map<std::string, Guest*> loaded;
    char line[256];
    while (fgets(line, sizeof line, f) != NULL)
    {
        char cls[64] = {}, pack[64] = {};
        float scale = 1.0f;
        if (line[0] == '#' || sscanf(line, "%63s %63s %f", cls, pack, &scale) < 2)
            continue;
        for (char* c = cls; *c; c++)
            if (*c >= 'A' && *c <= 'Z')
                *c = (char)(*c - 'A' + 'a');
        int index = -1;
        for (int i = 0; i < 12; i++)
            if (strcmp(cls, kClassNames[i]) == 0)
                index = i;
        if (index < 0)
        {
            fprintf(stderr, "[guest] guests.txt: no Strikers class called \"%s\"\n", cls); fflush(stderr);
            continue;
        }
        if (!(scale > 0.05f && scale < 20.0f))
            scale = 1.0f;
        Guest*& g = loaded[std::string(pack)];
        if (g == NULL)
            g = load_pack(pack, scale);
        s_guestFor[index] = g;
        if (g != NULL)
            fprintf(stderr, "[guest] every %s is drawn as %s\n", kClassNames[index], pack); fflush(stderr);
    }
    fclose(f);
}

// r67: the roster and the pick screens' picks (include/port/xr_guest.h).
struct RosterEntry
{
    std::string pack;
    std::string name;  // UTF-8
    std::string game;  // where it is from ("Melee", "Charged", "Melee Pokemon")
    int role;          // PORT_GUEST_CAPTAIN / PORT_GUEST_SIDEKICK
    int base;          // eTeamID / eSidekickID it plays as
    unsigned long locKey;
    unsigned long descKey;  // r68b: "From <its game>", where the game describes its captain
    Guest* guest;
    bool tried;
    // r70c (Lance: evolved forms "locked" to everyone, a silhouette on the menu, until you evolve them in
    // franchise): an evolved Pokemon (roster.txt's 6th field "franchise"), the form it evolves from (7th).
    bool franchiseOnly;
    std::string evolvesFrom;
};
std::vector<RosterEntry> s_roster;
std::vector<int> s_byRole[2];
bool s_rosterRead = false;
int s_pick[GameInfoManager::GM_NUM_MODES][8][2];
bool s_picksRead = false;

int team_of(const char* n)
{
    static const char* kTeams[8] = {"daisy", "donkeykong", "luigi", "mario", "peach", "waluigi", "wario", "yoshi"};
    for (int i = 0; i < 8; i++)
        if (strcmp(n, kTeams[i]) == 0)
            return i;
    return -1;
}

int sidekick_of(const char* n)
{
    static const char* kSidekicks[4] = {"toad", "koopa", "hammerbros", "birdo"};
    for (int i = 0; i < 4; i++)
        if (strcmp(n, kSidekicks[i]) == 0)
            return i;
    return -1;
}

void read_roster()
{
    if (s_rosterRead)
        return;
    s_rosterRead = true;
    FILE* f = fopen((home_dir() + "/guests/roster.txt").c_str(), "r");
    if (f == NULL)
        return;
    char line[512];
    while (fgets(line, sizeof line, f) != NULL)
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        // pack \t role \t base \t name \t game [\t franchise \t evolves from]
        char* fields[7] = {};
        int nf = 0;
        char* p = line;
        while (nf < 7 && p != NULL)
        {
            fields[nf++] = p;
            p = strchr(p, '\t');
            if (p != NULL)
                *p++ = '\0';
        }
        if (nf < 4)
            continue;
        for (char* e = fields[nf - 1]; *e; e++)
            if (*e == '\n' || *e == '\r')
                *e = '\0';
        RosterEntry r;
        r.pack = fields[0];
        r.role = strcmp(fields[1], "sidekick") == 0 ? PORT_GUEST_SIDEKICK : PORT_GUEST_CAPTAIN;
        r.base = r.role == PORT_GUEST_CAPTAIN ? team_of(fields[2]) : sidekick_of(fields[2]);
        r.name = fields[3];
        r.game = nf >= 5 ? fields[4] : "";
        r.locKey = nlStringLowerHash(("guest name " + r.pack).c_str());
        r.descKey = nlStringLowerHash(("guest desc " + r.pack).c_str());
        r.guest = NULL;
        r.tried = false;
        r.franchiseOnly = nf >= 6 && strcmp(fields[5], "franchise") == 0;
        r.evolvesFrom = nf >= 7 ? fields[6] : "";
        if (r.base < 0 || r.pack.empty())
            continue;
        s_byRole[r.role].push_back((int)s_roster.size());
        s_roster.push_back(r);
    }
    fclose(f);
    fprintf(stderr, "[guest] roster: %zu captains, %zu sidekicks\n", s_byRole[0].size(), s_byRole[1].size());
    fflush(stderr);
}

Guest* roster_guest(int index)
{
    read_roster();
    if (index < 0 || index >= (int)s_roster.size())
        return NULL;
    RosterEntry& r = s_roster[index];
    if (!r.tried)
    {
        r.tried = true;
        r.guest = load_pack(r.pack, 1.0f);
    }
    return r.guest;
}

// r70c: a franchise club's shiny Pokemon captain plays in its shiny colours (guests/<pack>_shiny.gpk,
// tools/outside/colo_shiny.py); without that file, its usual look.
bool s_shinySide[2] = {false, false};
// r78: each side's co-captain's guest (include/port/lineup.h): its roster index, -1 none (the Strikers
// captain itself), and whether it's shiny
int s_coPick[2] = {-1, -1};
bool s_coShiny[2] = {false, false};
std::unordered_map<int, Guest*> s_shinyGuest;  // roster index -> its shiny pack (NULL: none)

Guest* roster_guest_shiny(int index)
{
    std::unordered_map<int, Guest*>::const_iterator it = s_shinyGuest.find(index);
    if (it != s_shinyGuest.end())
        return it->second;
    Guest* g = NULL;
    const std::string path = home_dir() + "/guests/" + s_roster[index].pack + "_shiny.gpk";
    if (FILE* f = fopen(path.c_str(), "rb"))
    {
        fclose(f);
        g = load_pack(s_roster[index].pack + "_shiny", 1.0f);
    }
    s_shinyGuest[index] = g;
    return g;
}

std::string picks_path() { return home_dir() + "/guest-picks.txt"; }

void read_picks()
{
    if (s_picksRead)
        return;
    s_picksRead = true;
    for (auto& mode : s_pick)
        for (auto& team : mode)
            for (int& pick : team)
                pick = PORT_GUEST_INHERIT;
    read_roster();
    FILE* f = fopen(picks_path().c_str(), "r");
    if (f == NULL)
        return;
    int mode, team, role;
    char pack[64];
    while (fscanf(f, "%d %d %d %63s", &mode, &team, &role, pack) == 4)
    {
        if (mode < 0 || mode >= GameInfoManager::GM_NUM_MODES || team < 0 || team >= 8 || role < 0 || role > 1)
            continue;
        if (strcmp(pack, "@original") == 0)
        {
            s_pick[mode][team][role] = -1;
            continue;
        }
        for (size_t i = 0; i < s_roster.size(); i++)
            if (s_roster[i].pack == pack && s_roster[i].role == role)
                s_pick[mode][team][role] = (int)i;
    }
    fclose(f);
}

void write_picks()
{
    const std::string temporary = picks_path() + ".tmp";
    FILE* f = fopen(temporary.c_str(), "w");
    if (f == NULL)
        return;
    for (int m = 0; m < GameInfoManager::GM_NUM_MODES; m++)
        for (int t = 0; t < 8; t++)
            for (int r = 0; r < 2; r++)
                if (s_pick[m][t][r] == -1)
                    fprintf(f, "%d %d %d @original\n", m, t, r);
                else if (s_pick[m][t][r] >= 0 && s_pick[m][t][r] < (int)s_roster.size())
                    fprintf(f, "%d %d %d %s\n", m, t, r, s_roster[s_pick[m][t][r]].pack.c_str());
    const bool failed = ferror(f) != 0;
    const int closed = fclose(f);
    if (!failed && closed == 0)
        rename(temporary.c_str(), picks_path().c_str());
}

int current_mode()
{
    const int m = (int)GameInfoManager::Instance()->GetCurrentMode();
    return m >= 0 && m < GameInfoManager::GM_NUM_MODES ? m : -1;
}

// The roster index of the guest playing class `cls` for `side` this match, or -1.
int pick_for_side(int side, int cls)
{
    read_picks();
    const int mode = current_mode();
    if (mode < 0 || side < 0 || side > 1)
        return PORT_GUEST_INHERIT;
    GameInfoManager* gi = GameInfoManager::Instance();
    const int team = (int)gi->GetTeam((short)side);
    if (team < 0 || team >= 8)  // the Super Team has no guests
        return PORT_GUEST_INHERIT;
    int role = -1;
    if ((int)ConvertToCharacterClass((eTeamID)team) == cls)
        role = PORT_GUEST_CAPTAIN;
    else if (PortSecondCaptain(side) == cls)
        return s_coPick[side];  // r78: the co-captain's own (a guest, or -1: the Strikers captain itself)
    else if ((int)ConvertToCharacterClass(gi->GetSidekick((short)side)) == cls)
        role = PORT_GUEST_SIDEKICK;
    return role < 0 ? PORT_GUEST_INHERIT : s_pick[mode][team][role];
}

// The cSAnim's name (cIdentifier keeps it protected); for the log only.
struct NameOf : cSAnim
{
    static const char* get(const cSAnim* a) { return static_cast<const NameOf*>(a)->m_szName; }
};

int pick_clip(Guest* g, const cSAnim* host)
{
    if (host == NULL)
        return g->idle;
    u32 h = host->GetHashID();
    auto it = g->byHash.find(h);
    if (it != g->byHash.end())
        return it->second;
    auto al = s_alias.find(h);
    if (al != s_alias.end())
    {
        it = g->byHash.find(al->second);
        if (it != g->byHash.end())
            return it->second;
    }
    // A clip of the Strikers character's own (a captain's Super Strike, a celebration): the nearest
    // ordinary move of the guest's. A Super Strike is the guest's normal shot: no Mega Strikes here.
    const char* n = NameOf::get(host);
    int pick = g->idle;
    if (n != NULL)
    {
        if (strstr(n, "sts") || strstr(n, "super") || strstr(n, "shoot") || strstr(n, "kick") || strstr(n, "shot"))
            pick = g->kick;
        else if (strstr(n, "run") || strstr(n, "turbo") || strstr(n, "jog"))
            pick = g->run;
        else if (strstr(n, "pass"))
            pick = g->pass;
        else if (strstr(n, "hit") || strstr(n, "react") || strstr(n, "fall"))
            pick = g->hit;
        else if (strstr(n, "celeb") || strstr(n, "win") || strstr(n, "victory") || strstr(n, "wave") || strstr(n, "cheer"))
            pick = g->wave;
    }
    if (!s_unmatchedLogged[h])
    {
        s_unmatchedLogged[h] = true;
        fprintf(stderr, "[guest] %s: no clip \"%s\" of its own, playing \"%s\"\n", g->name.c_str(), n ? n : "?",
                pick >= 0 ? g->anims[pick].name.c_str() : "nothing"); fflush(stderr);
    }
    return pick >= 0 ? pick : g->idle;
}

void sample(const Guest* g, const GAnim& a, float t, float (*out)[7])
{
    const int n = (int)g->nodes.size();
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    const float f = t * (float)(a.keys - 1);
    int k0 = (int)f;
    if (k0 > (int)a.keys - 1)
        k0 = (int)a.keys - 1;
    const int k1 = k0 + 1 < (int)a.keys ? k0 + 1 : k0;
    const float fr = f - (float)k0;
    const float* p0 = &a.data[(size_t)k0 * n * 7];
    const float* p1 = &a.data[(size_t)k1 * n * 7];
    for (int i = 0; i < n; i++)
    {
        const float* q0 = p0 + i * 7;
        const float* q1 = p1 + i * 7;
        float d = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3];
        const float s1 = d < 0.0f ? -fr : fr;
        float len = 0.0f;
        for (int c = 0; c < 4; c++)
        {
            out[i][c] = q0[c] * (1.0f - fr) + q1[c] * s1;
            len += out[i][c] * out[i][c];
        }
        len = len > 1e-12f ? 1.0f / sqrtf(len) : 1.0f;
        for (int c = 0; c < 4; c++)
            out[i][c] *= len;
        if (a.hasTrans[i])
        {
            for (int c = 0; c < 3; c++)
                out[i][4 + c] = q0[4 + c] * (1.0f - fr) + q1[4 + c] * fr;
        }
        else
        {
            for (int c = 0; c < 3; c++)
                out[i][4 + c] = g->nodes[i].trans[c];
        }
    }
}

void blend_into(float (*dst)[7], const float (*from)[7], float w, int n)
{
    for (int i = 0; i < n; i++)
    {
        float d = 0.0f;
        for (int c = 0; c < 4; c++)
            d += dst[i][c] * from[i][c];
        const float sgn = d < 0.0f ? -1.0f : 1.0f;
        float len = 0.0f;
        for (int c = 0; c < 4; c++)
        {
            dst[i][c] = dst[i][c] * (1.0f - w) + from[i][c] * sgn * w;
            len += dst[i][c] * dst[i][c];
        }
        len = len > 1e-12f ? 1.0f / sqrtf(len) : 1.0f;
        for (int c = 0; c < 4; c++)
            dst[i][c] *= len;
        for (int c = 4; c < 7; c++)
            dst[i][c] = dst[i][c] * (1.0f - w) + from[i][c] * w;
    }
}

// Row-vector matrices (v' = v * M), translation in the last row, as nlMatrix4.
void local_matrix(const float* q, float* m)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1 - 2 * (y * y + z * z);
    m[1] = 2 * (x * y + z * w);
    m[2] = 2 * (x * z - y * w);
    m[3] = 0;
    m[4] = 2 * (x * y - z * w);
    m[5] = 1 - 2 * (x * x + z * z);
    m[6] = 2 * (y * z + x * w);
    m[7] = 0;
    m[8] = 2 * (x * z + y * w);
    m[9] = 2 * (y * z - x * w);
    m[10] = 1 - 2 * (x * x + y * y);
    m[11] = 0;
    m[12] = q[4];
    m[13] = q[5];
    m[14] = q[6];
    m[15] = 1;
}

void mul(const float* a, const float* b, float* out)
{
    float r[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] + a[i * 4 + 2] * b[2 * 4 + j] +
                           a[i * 4 + 3] * b[3 * 4 + j];
    memcpy(out, r, sizeof r);
}

// Rows 0-2 of a row-vector matrix, each made unit length (what is left is its rotation), and the
// first row's length (its scale).
float rot_of(const float* m, float* r)
{
    float s0 = 1.0f;
    for (int row = 0; row < 3; row++)
    {
        const float* v = m + row * 4;
        float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (row == 0)
            s0 = len;
        const float inv = len > 1e-12f ? 1.0f / len : 0.0f;
        for (int c = 0; c < 3; c++)
            r[row * 3 + c] = v[c] * inv;
    }
    return s0;
}

void mul3(const float* a, const float* b, float* out)
{
    float r[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r[i * 3 + j] = a[i * 3 + 0] * b[0 * 3 + j] + a[i * 3 + 1] * b[1 * 3 + j] + a[i * 3 + 2] * b[2 * 3 + j];
    memcpy(out, r, sizeof r);
}

// r101 (Lance: Poliwag "distorted af" in his Super Strike and its replay, "fix this for alllll guest characters"):
// turns compared and blended. Row-vector 3x3 rotations; quaternions (x, y, z, w) of their column form.
void transpose3(const float* a, float* out)
{
    float r[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r[i * 3 + j] = a[j * 3 + i];
    memcpy(out, r, sizeof r);
}

// The angle (radians) of the turn from b to a.
float turn_between(const float* a, const float* b)
{
    float bt[9], m[9];
    transpose3(b, bt);
    mul3(a, bt, m);
    const float c = (m[0] + m[4] + m[8] - 1.0f) * 0.5f;
    return acosf(c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c));
}

void rot_to_quat(const float* m, float* q)
{
    // column form r = transpose of m: r(i, j) = m[j * 3 + i]
    const float r00 = m[0], r11 = m[4], r22 = m[8];
    const float r21 = m[5], r12 = m[7], r02 = m[6], r20 = m[2], r10 = m[1], r01 = m[3];
    const float tr = r00 + r11 + r22;
    if (tr > 0.0f)
    {
        const float s = sqrtf(tr + 1.0f) * 2.0f;
        q[0] = (r21 - r12) / s, q[1] = (r02 - r20) / s, q[2] = (r10 - r01) / s, q[3] = 0.25f * s;
    }
    else if (r00 > r11 && r00 > r22)
    {
        const float s = sqrtf(1.0f + r00 - r11 - r22) * 2.0f;
        q[0] = 0.25f * s, q[1] = (r01 + r10) / s, q[2] = (r02 + r20) / s, q[3] = (r21 - r12) / s;
    }
    else if (r11 > r22)
    {
        const float s = sqrtf(1.0f + r11 - r00 - r22) * 2.0f;
        q[0] = (r01 + r10) / s, q[1] = 0.25f * s, q[2] = (r12 + r21) / s, q[3] = (r02 - r20) / s;
    }
    else
    {
        const float s = sqrtf(1.0f + r22 - r00 - r11) * 2.0f;
        q[0] = (r02 + r20) / s, q[1] = (r12 + r21) / s, q[2] = 0.25f * s, q[3] = (r10 - r01) / s;
    }
    const float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int k = 0; k < 4; k++)
        q[k] = n > 1e-12f ? q[k] / n : (k == 3 ? 1.0f : 0.0f);
}

void quat_to_rot(const float* q, float* m)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                        2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                        2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
    transpose3(r, m);  // back to row vectors
}

// f of the way from rotation a to b (the short way round), into out.
void slerp_rot(const float* a, const float* b, float f, float* out)
{
    if (f <= 0.0f)
    {
        memcpy(out, a, 9 * sizeof(float));
        return;
    }
    if (f >= 1.0f)
    {
        memcpy(out, b, 9 * sizeof(float));
        return;
    }
    float qa[4], qb[4], q[4];
    rot_to_quat(a, qa);
    rot_to_quat(b, qb);
    float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    if (d < 0.0f)
    {
        for (int k = 0; k < 4; k++)
            qb[k] = -qb[k];
        d = -d;
    }
    float s0 = 1.0f - f, s1 = f;
    if (d < 0.9995f)
    {
        const float th = acosf(d), sn = sinf(th);
        s0 = sinf((1.0f - f) * th) / sn;
        s1 = sinf(f * th) / sn;
    }
    float n = 0.0f;
    for (int k = 0; k < 4; k++)
    {
        q[k] = s0 * qa[k] + s1 * qb[k];
        n += q[k] * q[k];
    }
    n = sqrtf(n);
    for (int k = 0; k < 4; k++)
        q[k] /= n;
    quat_to_rot(q, out);
}

// A world matrix's rotation rows replaced by r, each row keeping its own length (the bone's scale).
void set_rot_keep_scale(float* w, const float* r)
{
    for (int row = 0; row < 3; row++)
    {
        float* v = w + row * 4;
        const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (int c = 0; c < 3; c++)
            v[c] = r[row * 3 + c] * len;
    }
}

// r66: a retarget guest (Melee) posed from the Strikers character it stands in for. Each guest
// bone that follows a Strikers bone turns, in the world, the way that bone has turned from its
// bind pose (D = inverse(bind rotation) x now, row vectors); its position comes from the guest's
// own bone lengths down the chain, except the hips (bip01 / bip01 pelvis), which go where the
// Strikers hips are, their offset from the character's root scaled by the two bind heights.
// Bones that follow nothing keep their bind pose relative to their parent.
void read_hostref()
{
    if (s_hostRefRead)
        return;
    s_hostRefRead = true;
    FILE* f = fopen((home_dir() + "/guests/hostref.txt").c_str(), "r");
    if (f == NULL)
    {
        fprintf(stderr, "[guest] no guests/hostref.txt: guests turn from the Strikers bind poses\n");
        fflush(stderr);
        return;
    }
    char line[512];
    int count = 0;
    while (fgets(line, sizeof line, f) != NULL)
    {
        if (line[0] == '#')
            continue;
        char cls[32];
        unsigned id = 0;
        HostRef r;
        float* v = r.rot;
        const int got = sscanf(line, "%31s %x %f %f %f %f %f %f %f %f %f %f %f %f", cls, &id, &v[0], &v[1], &v[2], &v[3],
                               &v[4], &v[5], &v[6], &v[7], &v[8], &r.pos[0], &r.pos[1], &r.pos[2]);
        if (got != 14)
            continue;
        bool ok = true;
        for (int e = 0; e < 9; e++)
            ok = ok && std::isfinite(r.rot[e]) && fabsf(r.rot[e]) <= 1.01f;
        for (int e = 0; e < 3; e++)
            ok = ok && std::isfinite(r.pos[e]) && fabsf(r.pos[e]) < 10.0f;
        if (!ok)
            continue;
        for (int i = 0; i < 12; i++)
            if (strcmp(cls, kClassNames[i]) == 0)
            {
                s_hostRef[i][(u32)id] = r;
                count++;
            }
    }
    fclose(f);
    fprintf(stderr, "[guest] hostref: %d standing-pose bones\n", count);
    fflush(stderr);
}

bool retarget_pose(Guest* g, int cls, const cCharacter& c, cPoseAccumulator* acc, const float* rootM,
                   float (*world)[16])
{
    HostRig& rig = g->rig[cls];
    const int n = (int)g->nodes.size();
    if (!rig.ready)
    {
        rig.ready = true;
        cSHierarchy* h = acc->m_BaseSHierarchy;
        std::unordered_map<u32, int> hostIndex;
        if (h != NULL)
            for (int i = 0; i < h->m_nNumNodes; i++)
                hostIndex[h->GetNodeID(i)] = i;
        ShaderSkinMesh* skin = (ShaderSkinMesh*)c.GetSkinMesh();
        const u32 hBip = nlStringLowerHash("bip01"), hPelvis = nlStringLowerHash("bip01 pelvis");
        int mapped = 0, wanted = 0;
        for (int i = 0; i < n; i++)
        {
            rig.hostNode[i] = -1;
            rig.rootLike[i] = false;
            rig.k[i] = 1.0f;
            const u32 m = g->nodes[i].map;
            if (m == 0)
                continue;
            wanted++;
            std::unordered_map<u32, int>::const_iterator it = hostIndex.find(m);
            SkinMatrix* sm = skin != NULL ? skin->boneMatrices.FindGet(m) : NULL;
            if (it == hostIndex.end() || sm == NULL)
                continue;
            nlMatrix4 inv, bind;
            sm->Get(inv);
            nlInvertMatrix(bind, inv);
            float bd[16];
            memcpy(bd, &bind, 64);
            float br[9];
            const float hbs = rot_of(bd, br);
            rig.hostBindScale[i] = hbs > 1e-6f ? hbs : 1.0f;
            for (int r = 0; r < 3; r++)
            {
                const float* gw = g->nodes[i].bindWorld + r * 4;
                rig.guestScale[i][r] = sqrtf(gw[0] * gw[0] + gw[1] * gw[1] + gw[2] * gw[2]);
            }
            for (int r = 0; r < 3; r++)
                for (int cc = 0; cc < 3; cc++)
                    rig.hostInvBindRot[i][r * 3 + cc] = br[cc * 3 + r];  // a rotation's inverse: its transpose
            rig.hostBindPos[i][0] = bd[12];
            rig.hostBindPos[i][1] = bd[13];
            rig.hostBindPos[i][2] = bd[14];
            rig.hostNode[i] = it->second;
            rig.rootLike[i] = m == hBip || m == hPelvis;
            rig.hostHipZ[i] = bd[14];
            if (rig.rootLike[i] && fabsf(bd[14]) > 1e-4f)
                rig.k[i] = g->nodes[i].bindWorld[14] / bd[14];
            mapped++;
        }
        // r68: the class's standing pose for the trunk (see HostRef): K x inverse(standing, relative
        // to the root) in place of the inverse bind, so a guest standing still stands in its own bind
        // pose turned as its root is; the hips' height scales from the standing hips.
        read_hostref();
        const std::unordered_map<u32, HostRef>& refs = s_hostRef[cls];
        int fromStanding = 0;
        for (int i = 0; i < n; i++)
        {
            rig.hipChild[i] = false;
            if (rig.hostNode[i] < 0)
                continue;
            std::unordered_map<u32, HostRef>::const_iterator ref = refs.find(g->nodes[i].map);
            if (ref == refs.end())
                continue;
            static const float kK3[9] = {0, 1, 0, -1, 0, 0, 0, 0, 1};  // kBindToClip's turn
            float inv[9];
            for (int r = 0; r < 3; r++)
                for (int cc = 0; cc < 3; cc++)
                    inv[r * 3 + cc] = ref->second.rot[cc * 3 + r];
            mul3(kK3, inv, rig.hostInvBindRot[i]);
            if (rig.rootLike[i] && fabsf(ref->second.pos[2]) > 1e-3f)
            {
                rig.k[i] = g->nodes[i].bindWorld[14] / ref->second.pos[2];
                rig.hostHipZ[i] = ref->second.pos[2];
            }
            fromStanding++;
        }
        // r101: which hips are the body's: no other followed bone above them (Weezing's small ball, mapped as hips,
        // hangs under its big one and goes with it, not to where the Strikers hips are); the top one of them (they
        // all move by its factors); and which joints off them are r68's tails: chains that follow nothing all the way
        // down. One joint there (a crater, a gas jet, a fin) is part of the body, and a chain leading to followed
        // bones is the body's frame (the way to the spine or the pelvis: Magmar's and Poliwag's halves hang off
        // bip01 through such joints, and an upright one pulled them apart when the body tumbled).
        {
            bool isHips[kMaxNodes], hasKid[kMaxNodes] = {};
            for (int i = 0; i < n; i++)
            {
                isHips[i] = rig.rootLike[i];
                if (g->nodes[i].parent >= 0)
                    hasKid[g->nodes[i].parent] = true;
            }
            rig.hipsTop = -1;
            for (int i = 0; i < n; i++)
            {
                rig.hipsBody[i] = false;
                if (!isHips[i])
                    continue;
                bool body = true;
                for (int a = g->nodes[i].parent; a >= 0 && body; a = g->nodes[a].parent)
                    body = rig.hostNode[a] < 0 || isHips[a];
                rig.hipsBody[i] = body;
                rig.rootLike[i] = body;
                if (body && g->nodes[i].parent >= 0 && rig.hipsTop < 0)
                    rig.hipsTop = i;
            }
            for (int i = 0; i < n; i++)
            {
                const int par = g->nodes[i].parent;
                rig.hipChild[i] = false;
                if (fromStanding == 0 || rig.hostNode[i] >= 0 || par < 0 || rig.hostNode[par] < 0 || !isHips[par] ||
                    !rig.hipsBody[par])
                    continue;
                // a chain (a joint with a grandchild) with no followed bone anywhere below it
                bool grand = false, followed = false;
                std::vector<int> stack(1, i);
                while (!stack.empty() && !followed)
                {
                    const int j = stack.back();
                    stack.pop_back();
                    for (int k = 0; k < n; k++)
                    {
                        if (g->nodes[k].parent != j)
                            continue;
                        if (j == i && hasKid[k])
                            grand = true;
                        if (rig.hostNode[k] >= 0)
                            followed = true;
                        stack.push_back(k);
                    }
                }
                rig.hipChild[i] = grand && !followed;
            }
        }
        // r66e: arms point the way the Strikers arms point, not only turn as they turn: Strikers' bind
        // is a T pose, a Pokemon's arms often hang or reach forward in theirs, and the turn from a T
        // to arms down put those through the body. Each upper arm and forearm first turns from its
        // own bind direction (to the next bone along) onto the Strikers one's; the hand as its forearm.
        // Legs keep their bends (four-legged hind legs).
        {
            const char* sides[2] = {"l", "r"};
            for (int sd = 0; sd < 2; sd++)
            {
                char nm[3][40];
                snprintf(nm[0], sizeof nm[0], "bip01 %s upperarm", sides[sd]);
                snprintf(nm[1], sizeof nm[1], "bip01 %s forearm", sides[sd]);
                snprintf(nm[2], sizeof nm[2], "bip01 %s hand", sides[sd]);
                const u32 hs[3] = {nlStringLowerHash(nm[0]), nlStringLowerHash(nm[1]), nlStringLowerHash(nm[2])};
                int gi[3] = {-1, -1, -1};
                for (int i = 0; i < n; i++)
                    for (int k = 0; k < 3; k++)
                        if (g->nodes[i].map == hs[k] && rig.hostNode[i] >= 0 && gi[k] < 0)
                            gi[k] = i;
                float lastA[9];
                bool haveLast = false;
                for (int k = 0; k < 3; k++)
                {
                    if (gi[k] < 0)
                        continue;
                    float a[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
                    bool aligned = false;
                    if (k < 2 && gi[k + 1] >= 0 && skin != NULL && skin->boneMatrices.FindGet(hs[k + 1]) != NULL)
                    {
                        nlMatrix4 cinv, cbind;
                        skin->boneMatrices.FindGet(hs[k + 1])->Get(cinv);
                        nlInvertMatrix(cbind, cinv);
                        float cb[16];
                        memcpy(cb, &cbind, 64);
                        float dh[3], dg[3];
                        for (int c = 0; c < 3; c++)
                        {
                            dh[c] = cb[12 + c] - rig.hostBindPos[gi[k]][c];
                            dg[c] = g->nodes[gi[k + 1]].bindWorld[12 + c] - g->nodes[gi[k]].bindWorld[12 + c];
                        }
                        const float lh = sqrtf(dh[0] * dh[0] + dh[1] * dh[1] + dh[2] * dh[2]);
                        const float lg = sqrtf(dg[0] * dg[0] + dg[1] * dg[1] + dg[2] * dg[2]);
                        if (lh > 1e-5f && lg > 1e-5f)
                        {
                            for (int c = 0; c < 3; c++)
                            {
                                dh[c] /= lh;
                                dg[c] /= lg;
                            }
                            // the turn taking dg onto dh (column form I + [v]x + [v]x^2 / (1 + c)),
                            // stored transposed: row vectors, dg * a = dh
                            const float v[3] = {dg[1] * dh[2] - dg[2] * dh[1], dg[2] * dh[0] - dg[0] * dh[2],
                                                dg[0] * dh[1] - dg[1] * dh[0]};
                            const float cs = dg[0] * dh[0] + dg[1] * dh[1] + dg[2] * dh[2];
                            if (cs > -0.95f)
                            {
                                const float kx[9] = {0, -v[2], v[1], v[2], 0, -v[0], -v[1], v[0], 0};
                                float kk[9];
                                mul3(kx, kx, kk);
                                const float f = 1.0f / (1.0f + cs);
                                float col[9];
                                for (int e = 0; e < 9; e++)
                                    col[e] = (e % 4 == 0 ? 1.0f : 0.0f) + kx[e] + kk[e] * f;
                                for (int r = 0; r < 3; r++)
                                    for (int c = 0; c < 3; c++)
                                        a[r * 3 + c] = col[c * 3 + r];
                                aligned = true;
                            }
                        }
                    }
                    if (!aligned && k == 2 && haveLast)
                    {
                        memcpy(a, lastA, sizeof a);
                        aligned = true;
                    }
                    if (aligned)
                    {
                        float out[9];
                        mul3(a, rig.hostInvBindRot[gi[k]], out);
                        memcpy(rig.hostInvBindRot[gi[k]], out, sizeof out);
                        memcpy(lastA, a, sizeof lastA);
                        haveLast = true;
                    }
                    else
                    {
                        haveLast = false;
                    }
                }
            }
        }
        fprintf(stderr, "[guest] %s on %s: %d of %d bones follow the Strikers skeleton (%d nodes there)\n",
                g->name.c_str(), kClassNames[cls], mapped, wanted, h != NULL ? h->m_nNumNodes : 0);
        fflush(stderr);
        // A guest with no bone that follows one (a Pokemon with no legs) still goes where the
        // character goes, turning with it: its root follows the root.
    }
    // r66e: Strikers' bind poses face -Y and its clips +X (all 12 classes, checked on the PC:
    // tools/outside/retargetsim.py): a guest's root turns the same quarter turn about Z, or a guest
    // with no bones following (a Pokemon) runs sideways.
    static const float kBindToClip[16] = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    // r69 (Lance: "flying pokemon fly, so dont have them running"): a flyer's wings beat about the
    // body's forward line at their roots (left up as right goes up: mirrored turns), and fliers and
    // floaters rise and fall a little; their legs follow nothing (they hang). Wall-clock time, a phase
    // per character so a team doesn't beat in step.
    static const u32 hFlapL = nlStringLowerHash("guest flap l"), hFlapR = nlStringLowerHash("guest flap r");
    static const u32 hSwirl = nlStringLowerHash("guest swirl");
    const bool flies = (g->mode & 2) != 0, floats = (g->mode & 4) != 0;
    float flapNow = 0.0f, bobNow = 0.0f, fwd[3] = {0.0f, 0.0f, 0.0f};
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double phase = (double)(((uintptr_t)&c >> 4) % 997) * 0.37;
    // r69 (Lance: "it cant be hard to have a cloud follow a player at all times"): a gas cloud's joint
    // (Gastly's) turns slowly round the vertical through itself: its puffs drift round the body
    const float swirlNow = (float)fmod(secs * 0.7 + phase, 2.0 * 3.14159265358979);
    auto swirl = [&](int i) {
        if (g->nodes[i].map != hSwirl)
            return;
        const float cs = cosf(swirlNow), sn = sinf(swirlNow);
        float* w = world[i];
        for (int row = 0; row < 3; row++)
        {
            const float x = w[row * 4], y = w[row * 4 + 1];
            w[row * 4] = x * cs - y * sn;
            w[row * 4 + 1] = x * sn + y * cs;
        }
    };
    if (flies || floats)
    {
        if (flies)
        {
            const double beat = secs * 2.0 * 3.14159265358979 * 2.2 + phase;
            flapNow = 0.15f + 0.5f * (float)sin(beat);
            bobNow = 0.035f * (float)cos(beat);
        }
        else
        {
            bobNow = 0.04f * (float)sin(secs * 2.0 * 3.14159265358979 * 0.7 + phase);
        }
    }
    int rootIdx = -1;
    auto flap = [&](int i) {
        const u32 m = g->nodes[i].map;
        if (!flies || (m != hFlapL && m != hFlapR) || rootIdx < 0)
            return;
        if (fwd[0] == 0.0f && fwd[1] == 0.0f && fwd[2] == 0.0f)
        {
            // the guest's forward: the clip's +X (its model -Y through kBindToClip), as the character
            // faces now: rootM's first row. (Not the root joint's own axes: Colosseum roots carry their
            // file's Y-up turn, which made the wings sweep sideways - the g5 PC check.)
            const float l = sqrtf(rootM[0] * rootM[0] + rootM[1] * rootM[1] + rootM[2] * rootM[2]);
            if (l < 1e-6f)
                return;
            for (int k = 0; k < 3; k++)
                fwd[k] = rootM[k] / l;
        }
        const float th = m == hFlapL ? flapNow : -flapNow;
        const float cs = cosf(th), sn = sinf(th), ax = fwd[0], ay = fwd[1], az = fwd[2];
        // row vectors: v' = v M, M = transpose of the column-form turn about `fwd` by th
        const float M[9] = {cs + (1 - cs) * ax * ax, (1 - cs) * ax * ay + sn * az, (1 - cs) * ax * az - sn * ay,
                            (1 - cs) * ay * ax - sn * az, cs + (1 - cs) * ay * ay, (1 - cs) * ay * az + sn * ax,
                            (1 - cs) * az * ax + sn * ay, (1 - cs) * az * ay - sn * ax, cs + (1 - cs) * az * az};
        float* w = world[i];
        for (int row = 0; row < 3; row++)
        {
            const float x = w[row * 4], y = w[row * 4 + 1], z = w[row * 4 + 2];
            for (int cc = 0; cc < 3; cc++)
                w[row * 4 + cc] = x * M[cc] + y * M[3 + cc] + z * M[6 + cc];
        }
    };
    for (int i = 0; i < n; i++)
    {
        const GNode& gn = g->nodes[i];
        const int par = gn.parent;
        if (par < 0)
        {
            float turned[16];
            mul(gn.bindWorld, kBindToClip, turned);
            mul(turned, rootM, world[i]);
            if (rootIdx < 0)
                rootIdx = i;
            continue;
        }
        const int hn = rig.hostNode[i];
        if (rig.hipChild[i])
        {
            // r68: a tail keeps its own bind turn to the ground, not the hips' (the Strikers hips rock
            // in a run: Charizard's tail went half a metre through the floor); it stays on at the hips.
            float turned[16];
            mul(gn.bindWorld, kBindToClip, turned);
            mul(turned, rootM, world[i]);
            {
                // r101: ... but once the body tumbles (a Super Strike, a bomb, a tackle: the hips 120-160 degrees
                // from upright) it goes with the hips, or it tore off the body (Poliwag's tail, Dratini's long one)
                float follow[16], rr[9], rf[9], r[9];
                mul(gn.bindLocal, world[par], follow);
                rot_of(world[i], rr);
                rot_of(follow, rf);
                const float th = turn_between(rf, rr);
                if (th > kTailUpright)
                {
                    float u = (th - kTailUpright) / (kTailFollow - kTailUpright);
                    u = u > 1.0f ? 1.0f : u;
                    slerp_rot(rr, rf, u * u * (3.0f - 2.0f * u), r);
                    set_rot_keep_scale(world[i], r);
                }
            }
            const float* tl = gn.bindLocal + 12;
            const float* pw = world[par];
            for (int cc = 0; cc < 3; cc++)
                world[i][12 + cc] = tl[0] * pw[0 + cc] + tl[1] * pw[4 + cc] + tl[2] * pw[8 + cc] + pw[12 + cc];
            flap(i);
            swirl(i);
            continue;
        }
        if (hn < 0 || hn >= acc->GetNumNodes())
        {
            mul(gn.bindLocal, world[par], world[i]);
            flap(i);
            swirl(i);
            continue;
        }
        float H[16];
        memcpy(H, &acc->GetNodeMatrix(hn), 64);
        float hr[9], gr[9], d[9], r[9];
        const float hs = rot_of(H, hr);
        rot_of(gn.bindWorld, gr);
        mul3(rig.hostInvBindRot[i], hr, d);
        mul3(gr, d, r);
        float* w = world[i];
        float scaleNow = hs / rig.hostBindScale[i];
        // r97: its own arm size (limbs.txt): the arm bones smaller round their joints, so the forearm and
        // hand (placed from them) come in toward the shoulder too
        if (g->armScale != 1.0f)
        {
            static u32 s_arm[6] = {};
            if (s_arm[0] == 0)
            {
                const char* names[6] = {"bip01 l upperarm", "bip01 l forearm", "bip01 l hand",
                                        "bip01 r upperarm", "bip01 r forearm", "bip01 r hand"};
                for (int k = 0; k < 6; k++)
                    s_arm[k] = nlStringLowerHash(names[k]);
            }
            for (int k = 0; k < 6; k++)
                if (gn.map == s_arm[k])
                    scaleNow *= g->armScale;
        }
        for (int row = 0; row < 3; row++)
        {
            for (int cc = 0; cc < 3; cc++)
                w[row * 4 + cc] = r[row * 3 + cc] * rig.guestScale[i][row] * scaleNow;
            w[row * 4 + 3] = 0.0f;
        }
        if (rig.rootLike[i])
        {
            // r101: every hip bone of the body moves by the top one's factors (each keeps its own standing height):
            // Magmar's bip01 and pelvis rose by different amounts in Mario's Super Strike jump and his body
            // stretched into a pole between them. (One hip bone: its own, as before.)
            const float ka = rig.hipsTop >= 0 ? rig.k[rig.hipsTop] : rig.k[i];
            for (int cc = 0; cc < 3; cc++)
                w[12 + cc] = rootM[12 + cc] + (H[12 + cc] - rootM[12 + cc]) * ka;
            // r69: the guest's hips stand at its own height, but rise and fall no more than the
            // Strikers hips do (Wario's 0.25 m run bounce times k threw Geodude a metre up; the PC
            // mapping pass found it: Graveler, Voltorb, Primeape and Machoke hopped too)
            const float kb = ka < 1.0f ? ka : 1.0f;
            w[14] = rootM[14] + rig.hostHipZ[i] * rig.k[i] + (H[14] - rootM[14] - rig.hostHipZ[i]) * kb;
        }
        else
        {
            const float* tl = gn.bindLocal + 12;
            const float* pw = world[par];
            for (int cc = 0; cc < 3; cc++)
                w[12 + cc] = tl[0] * pw[0 + cc] + tl[1] * pw[4 + cc] + tl[2] * pw[8 + cc] + pw[12 + cc];
        }
        w[15] = 1.0f;
    }
    if (bobNow != 0.0f)
        for (int i = 0; i < n; i++)
            world[i][14] += bobNow;
    return true;
}
struct Skinned
{
    nlVector3 p;
    nlVector3 n;
};

// r66e: a guest's model and per-frame vertex data come from memory of our own, three frames deep
// (the game's frame memory lives two): the game's frame vertex memory is 7 MB, and running out of
// it aborts (glx_FrameAlloc -> nlBreak), which a team of Melee fighters' strips would.
struct Arena
{
    std::vector<std::pair<u8*, size_t> > chunks;
    size_t chunk;
    size_t used;
};
Arena s_arena[3];
unsigned int s_arenaFrame = 0xFFFFFFFFu;
int s_arenaSlot = 0;

void arena_frame()
{
    if (s_arenaFrame == g_portGlFrame)
        return;
    s_arenaFrame = g_portGlFrame;
    s_arenaSlot = (s_arenaSlot + 1) % 3;
    s_arena[s_arenaSlot].chunk = 0;
    s_arena[s_arenaSlot].used = 0;
}

void* arena_alloc(size_t n)
{
    n = (n + 31) & ~(size_t)31;
    Arena& a = s_arena[s_arenaSlot];
    for (; a.chunk < a.chunks.size(); a.chunk++, a.used = 0)
    {
        if (a.used + n <= a.chunks[a.chunk].second)
        {
            u8* p = a.chunks[a.chunk].first + a.used;
            a.used += n;
            return p;
        }
    }
    const size_t size = n > ((size_t)4 << 20) ? n : ((size_t)4 << 20);
    u8* raw = (u8*)malloc(size + 32);
    if (raw == NULL)
        return NULL;
    u8* base = (u8*)(((uintptr_t)raw + 31) & ~(uintptr_t)31);
    a.chunks.push_back(std::make_pair(base, size));
    a.chunk = a.chunks.size() - 1;
    a.used = n;
    return base;
}

// GLMeshWriter's stride for each stream (gluMeshWriter.cpp's gl_stream_stride).
const u8 kStride[GLStream_Num] = {12, 3, 4, 4, 4, 4, 4, 4, 4, 12, 12, 12, 1, 16, 16};

// r94 (Lance, a picture of the DS games' shiny sparkle: "can we just put those around the pokemon when
// they become a character??? i mean even the ones that have proper shiny functionality as is"): a
// shiny guest twinkles all the time. Four-point stars (a white core in a pale blue glow) pop up around
// it, grow, drift up a little and fade, each in its own turn somewhere new. A model of their own after
// the body's: DrawableCharacter's shadow takes the first model only, and the game's effect skins
// (shocked, frozen) leave them out (PortGuestIsSparkleModel).
float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

u32 sparkle_hash(u32 a, u32 b)
{
    u32 h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

float sparkle_rand(u32 a, u32 b, u32 k) { return (float)(sparkle_hash(a, b * 7u + k) & 0xFFFFFFu) / 16777216.0f; }

PlatTexture* sparkle_texture()
{
    static PlatTexture* s_tex = NULL;
    static bool s_made = false;
    if (s_made)
        return s_tex;
    s_made = true;
    const int W = 128;
    std::vector<u8> rgba((size_t)W * W * 4);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
        {
            const float u = (((x + 0.5f) / W) * 2.0f - 1.0f) / 0.96f, v = (((y + 0.5f) / W) * 2.0f - 1.0f) / 0.96f;
            // the star: an astroid (sqrt|u| + sqrt|v| < 1), long thin points, a soft edge
            const float a = sqrtf(fabsf(u)) + sqrtf(fabsf(v));
            const float star = clamp01((1.0f - a) / 0.2f);
            const float core = clamp01((0.6f - a) / 0.35f);
            const float glow = 0.5f * expf(-(u * u + v * v) * 7.0f);
            const float alpha = star > glow ? star : glow;
            u8* o = &rgba[((size_t)y * W + x) * 4];
            o[0] = (u8)(255.0f * (0.62f + 0.38f * core));
            o[1] = (u8)(255.0f * (0.88f + 0.12f * core));
            o[2] = 255;
            o[3] = (u8)(255.0f * clamp01(alpha));
        }
    // a texture record as meleepack.texture_record writes one: GXTextureHeader (big-endian: 1 level,
    // RGBA8, bits 8 8 8 + see-through, w, h, no palette), then GX RGBA8 tiles (4x4: 32 bytes AR, 32 GB)
    std::vector<u8> rec(32 + (size_t)W * W * 4, 0);
    rec[3] = 1;
    rec[7] = 3;
    rec[8] = rec[9] = rec[10] = 8;
    rec[11] = 1;
    rec[14] = (u8)(W >> 8);
    rec[15] = (u8)W;
    rec[16] = (u8)(W >> 8);
    rec[17] = (u8)W;
    u8* o = &rec[32];
    for (int by = 0; by < W; by += 4)
        for (int bx = 0; bx < W; bx += 4)
        {
            for (int k = 0; k < 16; k++)
            {
                const u8* t = &rgba[((size_t)(by + k / 4) * W + bx + k % 4) * 4];
                o[k * 2] = t[3];
                o[k * 2 + 1] = t[0];
                o[32 + k * 2] = t[1];
                o[32 + k * 2 + 1] = t[2];
            }
            o += 64;
        }
    s_tex = make_texture(rec.data(), (u32)rec.size());
    return s_tex;
}

const glModel* s_lastSparkle = NULL;  // the last PortGuestFrameModels call's (PortGuestIsSparkleModel)

glModel* sparkle_model(const Skinned* sk, size_t nSk, const float* rootM, const glModelPacket& hostPacket,
                       const eGLStream* ids, int nIds, u32 seed, float now)
{
    PlatTexture* tex = sparkle_texture();
    if (tex == NULL || nSk == 0)
        return NULL;
    // The body's box this frame in the root's own axes (row 0 faces, row 1 to the side, row 2 up).
    float ax[3][3];
    for (int r = 0; r < 3; r++)
    {
        const float* m = rootM + r * 4;
        const float l = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        if (l < 1e-6f)
            return NULL;
        for (int c = 0; c < 3; c++)
            ax[r][c] = m[c] / l;
    }
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (size_t v = 0; v < nSk; v++)
    {
        const float d[3] = {sk[v].p.x - rootM[12], sk[v].p.y - rootM[13], sk[v].p.z - rootM[14]};
        for (int r = 0; r < 3; r++)
        {
            const float h = d[0] * ax[r][0] + d[1] * ax[r][1] + d[2] * ax[r][2];
            lo[r] = h < lo[r] ? h : lo[r];
            hi[r] = h > hi[r] ? h : hi[r];
        }
    }
    const float H = hi[2] - lo[2];
    if (!(H > 1e-3f) || H > 100.0f)
        return NULL;
    const float cf = 0.5f * (lo[0] + hi[0]), cs = 0.5f * (lo[1] + hi[1]);
    const float ring = 0.85f * 0.25f * ((hi[0] - lo[0]) + (hi[1] - lo[1])) + 0.12f * H;
    // 18 stars, each lit for the first half of its 1.5 s turn (about 9 at once), three crossed
    // squares a star so it shows from the side, the table's view from above and the stands alike.
    const int kStars = 18;
    const float kTurn = 1.5f;
    float pos[kStars * 3][4][3];
    int nQuads = 0;
    for (int i = 0; i < kStars; i++)
    {
        const float ph = (float)i * 0.618034f + (float)(seed & 0xFFu) / 256.0f;
        const float tt = now / kTurn + ph;
        const float turnF = floorf(tt);
        const float life = (tt - turnF) / 0.5f;
        if (life >= 1.0f)
            continue;
        const u32 turn = (u32)(long long)turnF;
        const u32 key = seed * 31u + (u32)i;
        const float ang = 6.2831853f * sparkle_rand(key, turn, 1);
        const float up = lo[2] + H * (-0.02f + 1.1f * sparkle_rand(key, turn, 2)) + 0.08f * H * life;
        const float rad = ring * (0.75f + 0.45f * sparkle_rand(key, turn, 3));
        const float big = 0.55f + 0.45f * sparkle_rand(key, turn, 4);
        const float grow = sinf(3.14159265f * life);
        const float half = 0.5f * 0.2f * H * big * grow;
        if (half < 1e-4f)
            continue;
        const float spin = 6.2831853f * sparkle_rand(key, turn, 5) + 0.7f * life;
        const float lf = cf + rad * cosf(ang), ls = cs + rad * sinf(ang);
        float c[3];
        for (int k = 0; k < 3; k++)
            c[k] = rootM[12 + k] + lf * ax[0][k] + ls * ax[1][k] + up * ax[2][k];
        // the three squares: (face, up), (side, up), (face, side)
        const int planes[3][2] = {{0, 2}, {1, 2}, {0, 1}};
        const float cr = cosf(spin), sr = sinf(spin);
        for (int q = 0; q < 3; q++)
        {
            const float* e0 = ax[planes[q][0]];
            const float* e1 = ax[planes[q][1]];
            float a[3], b[3];
            for (int k = 0; k < 3; k++)
            {
                a[k] = half * (cr * e0[k] + sr * e1[k]);
                b[k] = half * (-sr * e0[k] + cr * e1[k]);
            }
            const float corner[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
            for (int k = 0; k < 4; k++)
                for (int j = 0; j < 3; j++)
                    pos[nQuads][k][j] = c[j] + corner[k][0] * a[j] + corner[k][1] * b[j];
            nQuads++;
        }
    }
    if (nQuads == 0)
        return NULL;
    const int nv = nQuads * 4 + (nQuads - 1) * 2;
    u8* p = (u8*)arena_alloc((size_t)nv * 12);
    s8* nrm = (s8*)arena_alloc((size_t)nv * 3);
    s16* ramp = (s16*)arena_alloc((size_t)nv * 4);
    s16* uv = (s16*)arena_alloc((size_t)nv * 4);
    float* basis = (float*)arena_alloc((size_t)nv * 12);
    glModelStream* streams = (glModelStream*)arena_alloc(sizeof(glModelStream) * nIds);
    glModel* model = (glModel*)arena_alloc(sizeof(glModel));
    glModelPacket* out = (glModelPacket*)arena_alloc(sizeof(glModelPacket));
    if (p == NULL || nrm == NULL || ramp == NULL || uv == NULL || basis == NULL || streams == NULL || model == NULL ||
        out == NULL)
        return NULL;
    static const s16 kUv[4][2] = {{0, 1024}, {1024, 1024}, {0, 0}, {1024, 0}};
    int k = 0;
    for (int q = 0; q < nQuads; q++)
    {
        // strip: the four corners, joined to the next square by repeating its last and the next's first
        int order[6], n = 0;
        if (q > 0)
            order[n++] = 0;
        for (int c = 0; c < 4; c++)
            order[n++] = c;
        if (q + 1 < nQuads)
            order[n++] = 3;
        for (int j = 0; j < n; j++, k++)
        {
            const int c = order[j];
            memcpy(p + (size_t)k * 12, pos[q][c], 12);
            for (int a = 0; a < 3; a++)
            {
                nrm[k * 3 + a] = (s8)(64.0f * ax[2][a]);
                basis[k * 3 + a] = ax[2][a];
            }
            // the light ramp near its lit end, whatever way the square faces: the stars shine
            ramp[k * 2 + 0] = 940;
            ramp[k * 2 + 1] = 512;
            uv[k * 2 + 0] = kUv[c][0];
            uv[k * 2 + 1] = kUv[c][1];
        }
    }
    static std::vector<u8> s_white((size_t)4096 * 4, 0xFF);
    for (int s = 0; s < nIds; s++)
    {
        const int id = ids[s];
        glModelStream& st = streams[s];
        st.id = (u8)id;
        st.stride = kStride[id];
        st.beData = 0;
        st.dataSize = (u32)nv * kStride[id];
        if (id == GLStream_Position)
            st.address = (uintptr_t)p;
        else if (id == GLStream_Normal)
            st.address = (uintptr_t)nrm;
        else if (id == GLStream_Colour)
            st.address = (uintptr_t)s_white.data();
        else if (id == GLStream_BumpLocal)
            st.address = (uintptr_t)ramp;
        else if (id >= GLStream_Diffuse && id <= GLStream_Gloss)
            st.address = (uintptr_t)uv;
        else
            st.address = (uintptr_t)basis;
    }
    memset(out, 0, sizeof *out);
    out->numVertices = (u16)nv;
    out->primType = (u8)GLP_TriStrip;
    out->numStreams = (u8)nIds;
    out->streams = streams;
    out->state = hostPacket.state;
    out->state.texture[GLTT_Diffuse] = (uintptr_t)tex;
    out->state.texture[GLTT_Detail] = (uintptr_t)tex;
    u32 r = out->state.raster;
    glSetRasterState(r, GLS_AlphaBlend, 1);
    glSetRasterState(r, GLS_DepthWrite, 0);
    glSetRasterState(r, GLS_AlphaTest, 1);
    glSetRasterState(r, GLS_AlphaTestRef, 0x08);
    glSetRasterState(r, GLS_Culling, 0);
    out->state.raster = r;
    memset(model, 0, sizeof *model);
    model->numPackets = 1;
    model->packets = out;
    return model;
}

// r101 (Lance: "can we actually get the smoke around gastly to be more SMOKE like and not like a solid object around
// him???"): his gas cards (flat crossed sheets, a solid shape seen from most sides) give way to smoke. Soft puffs
// (a wispy round blob each, no edge) start inside the cloud the cards made, drift out and up as they swirl round
// him, swell and thin out, each in its own turn somewhere new. A puff fades by its picture: kSmokeLevels pictures from
// thick to faint (the guests' material takes no vertex alpha), the puffs drawn as one packet per level.
const int kSmokeLevels = 6;

float smoke_noise(float x, float y, u32 seed)
{
    // value noise, 4 octaves, 0..1
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < 4; o++)
    {
        const int xi = (int)floorf(x), yi = (int)floorf(y);
        const float fx = x - xi, fy = y - yi;
        const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
        auto h = [&](int a, int b) { return sparkle_rand((u32)(a * 73856093) ^ seed, (u32)(b * 19349663) + (u32)o, 9); };
        const float v = (h(xi, yi) * (1 - sx) + h(xi + 1, yi) * sx) * (1 - sy) + (h(xi, yi + 1) * (1 - sx) + h(xi + 1, yi + 1) * sx) * sy;
        sum += v * amp;
        norm += amp;
        amp *= 0.5f;
        x *= 2.03f;
        y *= 2.03f;
    }
    return sum / norm;
}

PlatTexture* smoke_texture(int level, u32 rgb)
{
    static std::unordered_map<u32, PlatTexture*> s_tex;
    const u32 key = rgb * (u32)kSmokeLevels + (u32)level;
    std::unordered_map<u32, PlatTexture*>::const_iterator it = s_tex.find(key);
    if (it != s_tex.end())
        return it->second;
    const int W = 64;
    const float opacity = 0.85f * (float)(kSmokeLevels - level) / (float)kSmokeLevels;
    const float cr = ((rgb >> 16) & 255) / 255.0f, cg = ((rgb >> 8) & 255) / 255.0f, cb = (rgb & 255) / 255.0f;
    std::vector<u8> rec(32 + (size_t)W * W * 4, 0);
    rec[3] = 1;
    rec[7] = 3;
    rec[8] = rec[9] = rec[10] = 8;
    rec[11] = 1;
    rec[15] = (u8)W;
    rec[17] = (u8)W;
    std::vector<u8> rgba((size_t)W * W * 4);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
        {
            const float u = ((x + 0.5f) / W) * 2.0f - 1.0f, v = ((y + 0.5f) / W) * 2.0f - 1.0f;
            const float n = smoke_noise(u * 2.2f + 7.0f, v * 2.2f + 3.0f, 0x51C0u);
            // a round soft blob, broken up by the noise at its rim (wisps), fading to nothing at the edge
            const float rr = sqrtf(u * u + v * v) * (0.82f + 0.36f * n);
            const float body = clamp01(1.0f - rr);
            const float a = body * body * (3.0f - 2.0f * body) * (0.55f + 0.45f * n);
            const float shade = 0.78f + 0.3f * n;  // lighter and darker swirls inside
            u8* o = &rgba[((size_t)y * W + x) * 4];
            o[0] = (u8)(255.0f * clamp01(cr * shade + 0.08f));
            o[1] = (u8)(255.0f * clamp01(cg * shade + 0.06f));
            o[2] = (u8)(255.0f * clamp01(cb * shade + 0.1f));
            o[3] = (u8)(255.0f * clamp01(a * opacity));
        }
    u8* o = &rec[32];
    for (int by = 0; by < W; by += 4)
        for (int bx = 0; bx < W; bx += 4)
        {
            for (int k = 0; k < 16; k++)
            {
                const u8* t = &rgba[((size_t)(by + k / 4) * W + bx + k % 4) * 4];
                o[k * 2] = t[3];
                o[k * 2 + 1] = t[0];
                o[32 + k * 2] = t[1];
                o[32 + k * 2 + 1] = t[2];
            }
            o += 64;
        }
    PlatTexture* t = make_texture(rec.data(), (u32)rec.size());
    s_tex[key] = t;
    return t;
}

const glModel* s_lastSmoke = NULL;  // the last PortGuestFrameModels call's (PortGuestIsSparkleModel too)

// The smoke round the gas: centre and size this frame (from where the gas cards are), the root's axes.
glModel* smoke_model(const float* centre, float radius, const float* rootM, const glModelPacket& hostPacket,
                     const eGLStream* ids, int nIds, u32 seed, float now, u32 rgb)
{
    float ax[3][3];
    for (int r = 0; r < 3; r++)
    {
        const float* m = rootM + r * 4;
        const float l = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        if (l < 1e-6f)
            return NULL;
        for (int c = 0; c < 3; c++)
            ax[r][c] = m[c] / l;
    }
    if (!(radius > 1e-3f) || radius > 50.0f)
        return NULL;
    // 26 puffs, each a 2.4 s turn (all alive at once, at different ages); three crossed squares a puff
    const int kPuffs = 26;
    const float kTurn = 2.4f;
    struct Quad
    {
        float p[4][3];
        int level;
    };
    static Quad quads[kPuffs * 3];
    int nQuads = 0;
    for (int i = 0; i < kPuffs; i++)
    {
        const float ph = (float)i * 0.618034f + (float)(seed & 0xFFu) / 256.0f;
        const float tt = now / kTurn + ph;
        const float turnF = floorf(tt);
        const float life = tt - turnF;  // 0..1
        const u32 turn = (u32)(long long)turnF;
        const u32 key = seed * 131u + (u32)i;
        // born inside the cloud, drifting out and up while it swirls round
        const float ang0 = 6.2831853f * sparkle_rand(key, turn, 1), elev = 1.2f * (sparkle_rand(key, turn, 2) - 0.45f);
        const float start = radius * 0.55f * sparkle_rand(key, turn, 3);
        const float out = start + radius * (0.25f + 0.35f * sparkle_rand(key, turn, 4)) * life;
        const float ang = ang0 + 1.1f * life * (sparkle_rand(key, turn, 5) > 0.5f ? 1.0f : -1.0f);
        const float rise = radius * (0.15f + 0.5f * life) * (0.6f + 0.4f * sparkle_rand(key, turn, 6));
        const float d[3] = {cosf(ang) * cosf(elev), sinf(ang) * cosf(elev), sinf(elev)};
        float c[3];
        for (int k = 0; k < 3; k++)
            c[k] = centre[k] + out * (d[0] * ax[0][k] + d[1] * ax[1][k] + d[2] * ax[2][k]) + rise * ax[2][k];
        // swells as it goes; thick soon after it starts, thinning to nothing
        const float half = radius * (0.38f + 0.55f * life) * (0.75f + 0.5f * sparkle_rand(key, turn, 7));
        const float thick = clamp01(life / 0.18f) * (1.0f - life * life);
        const int level = kSmokeLevels - 1 - (int)(thick * (float)kSmokeLevels * 0.999f);
        if (thick < 0.08f)
            continue;
        const float spin = 6.2831853f * sparkle_rand(key, turn, 8) + 0.6f * life;
        const int planes[3][2] = {{0, 2}, {1, 2}, {0, 1}};
        const float cr = cosf(spin), sr = sinf(spin);
        for (int q = 0; q < 3; q++)
        {
            const float* e0 = ax[planes[q][0]];
            const float* e1 = ax[planes[q][1]];
            float a[3], b[3];
            for (int k = 0; k < 3; k++)
            {
                a[k] = half * (cr * e0[k] + sr * e1[k]);
                b[k] = half * (-sr * e0[k] + cr * e1[k]);
            }
            const float corner[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
            for (int k = 0; k < 4; k++)
                for (int j = 0; j < 3; j++)
                    quads[nQuads].p[k][j] = c[j] + corner[k][0] * a[j] + corner[k][1] * b[j];
            quads[nQuads].level = level;
            nQuads++;
        }
    }
    if (nQuads == 0)
        return NULL;
    // one packet per thickness level (each its own picture)
    int perLevel[kSmokeLevels] = {};
    int nPackets = 0;
    for (int q = 0; q < nQuads; q++)
        if (perLevel[quads[q].level]++ == 0)
            nPackets++;
    glModel* model = (glModel*)arena_alloc(sizeof(glModel));
    glModelPacket* outs = (glModelPacket*)arena_alloc(sizeof(glModelPacket) * nPackets);
    if (model == NULL || outs == NULL)
        return NULL;
    static std::vector<u8> s_white((size_t)4096 * 4, 0xFF);
    static const s16 kUv[4][2] = {{0, 1024}, {1024, 1024}, {0, 0}, {1024, 0}};
    int np = 0;
    for (int lv = 0; lv < kSmokeLevels; lv++)
    {
        const int nq = perLevel[lv];
        if (nq == 0)
            continue;
        PlatTexture* tex = smoke_texture(lv, rgb & 0xFFFFFFu);
        if (tex == NULL)
            continue;
        const int nv = nq * 4 + (nq - 1) * 2;
        u8* p = (u8*)arena_alloc((size_t)nv * 12);
        s8* nrm = (s8*)arena_alloc((size_t)nv * 3);
        s16* ramp = (s16*)arena_alloc((size_t)nv * 4);
        s16* uv = (s16*)arena_alloc((size_t)nv * 4);
        float* basis = (float*)arena_alloc((size_t)nv * 12);
        glModelStream* streams = (glModelStream*)arena_alloc(sizeof(glModelStream) * nIds);
        if (p == NULL || nrm == NULL || ramp == NULL || uv == NULL || basis == NULL || streams == NULL)
            return NULL;
        int k = 0, done = 0;
        for (int q = 0; q < nQuads; q++)
        {
            if (quads[q].level != lv)
                continue;
            int order[6], n = 0;
            if (done > 0)
                order[n++] = 0;
            for (int c = 0; c < 4; c++)
                order[n++] = c;
            if (done + 1 < nq)
                order[n++] = 3;
            for (int j = 0; j < n; j++, k++)
            {
                const int c = order[j];
                memcpy(p + (size_t)k * 12, quads[q].p[c], 12);
                for (int a = 0; a < 3; a++)
                {
                    nrm[k * 3 + a] = (s8)(64.0f * ax[2][a]);
                    basis[k * 3 + a] = ax[2][a];
                }
                // the light ramp a little short of its lit end: smoke, not a glow
                ramp[k * 2 + 0] = 800;
                ramp[k * 2 + 1] = 512;
                uv[k * 2 + 0] = kUv[c][0];
                uv[k * 2 + 1] = kUv[c][1];
            }
            done++;
        }
        for (int s = 0; s < nIds; s++)
        {
            const int id = ids[s];
            glModelStream& st = streams[s];
            st.id = (u8)id;
            st.stride = kStride[id];
            st.beData = 0;
            st.dataSize = (u32)nv * kStride[id];
            if (id == GLStream_Position)
                st.address = (uintptr_t)p;
            else if (id == GLStream_Normal)
                st.address = (uintptr_t)nrm;
            else if (id == GLStream_Colour)
                st.address = (uintptr_t)s_white.data();
            else if (id == GLStream_BumpLocal)
                st.address = (uintptr_t)ramp;
            else if (id >= GLStream_Diffuse && id <= GLStream_Gloss)
                st.address = (uintptr_t)uv;
            else
                st.address = (uintptr_t)basis;
        }
        glModelPacket* out = &outs[np++];
        memset(out, 0, sizeof *out);
        out->numVertices = (u16)nv;
        out->primType = (u8)GLP_TriStrip;
        out->numStreams = (u8)nIds;
        out->streams = streams;
        out->state = hostPacket.state;
        out->state.texture[GLTT_Diffuse] = (uintptr_t)tex;
        out->state.texture[GLTT_Detail] = (uintptr_t)tex;
        u32 r = out->state.raster;
        glSetRasterState(r, GLS_AlphaBlend, 1);
        glSetRasterState(r, GLS_DepthWrite, 0);
        glSetRasterState(r, GLS_AlphaTest, 1);
        glSetRasterState(r, GLS_AlphaTestRef, 0x02);
        glSetRasterState(r, GLS_Culling, 0);
        out->state.raster = r;
    }
    if (np == 0)
        return NULL;
    memset(model, 0, sizeof *model);
    model->numPackets = (u32)np;
    model->packets = outs;
    return model;
}

} // namespace

bool PortGuestIsSparkleModel(const glModel* model)
{
    // (r101: Gastly's smoke too: the game's effect skins - shocked, frozen - leave both out)
    return model != NULL && (model == s_lastSparkle || model == s_lastSmoke);
}

void PortGuestNoteLive(const void* acc)
{
    PoseNote* n = note_for(acc, false);
    if (n != NULL)
    {
        n->tree = false;
        n->anim = NULL;
    }
}

void PortGuestNoteTree(const void* acc, const cPoseNode* tree)
{
    const cPN_SAnimController* best = NULL;
    float bestW = 0.0f;
    heaviest_clip(const_cast<cPoseNode*>(tree), 1.0f, &best, &bestW, 0);
    PoseNote* n = note_for(acc, best != NULL);
    if (n == NULL)
        return;
    n->tree = best != NULL;
    n->anim = best != NULL ? best->m_pSAnim : NULL;
    n->t = best != NULL ? best->m_fTime : 0.0f;
}

void PortGuestNoteBlend(const void* acc, const void* lhs, const void* rhs, float rhsWeight)
{
    const PoseNote* a = note_for(lhs, false);
    const PoseNote* b = note_for(rhs, false);
    const bool treeA = a != NULL && a->tree, treeB = b != NULL && b->tree;
    if (!treeA && !treeB)
    {
        PortGuestNoteLive(acc);
        return;
    }
    PoseNote* n = note_for(acc, true);
    if (n == NULL)
        return;
    const PoseNote* pick = !treeB || (treeA && rhsWeight < 0.5f) ? a : b;
    n->tree = true;
    n->anim = pick->anim;
    n->t = pick->t;
    // the same clip both sides, not across its loop: between them
    if (treeA && treeB && a->anim == b->anim && fabsf(b->t - a->t) < 0.5f)
        n->t = a->t + (b->t - a->t) * rhsWeight;
}

void PortGuestScorerPose(bool active, const cSAnim* clip, float normTime, float celebrateSeconds)
{
    s_scorerActive = active;
    s_scorerClip = clip;
    s_scorerNorm = normTime < 0.0f ? 0.0f : (normTime > 1.0f ? 1.0f : normTime);
    s_scorerCelebrate = celebrateSeconds < 0.0f ? 0.0f : celebrateSeconds;
}

glModel** PortGuestFrameModels(const cCharacter& character, cPoseAccumulator* acc, const glModel* hostModel,
                               int* numModels)
{
    *numModels = 0;
    s_lastSparkle = NULL;
    s_lastSmoke = NULL;
    if (!guests_enabled())
        return NULL;
    if (!s_configRead)
        read_config();
    const int cls = (int)character.m_eCharacterClass;
    if (cls < 0 || cls >= 12 || acc == NULL || hostModel == NULL || hostModel->numPackets == 0)
        return NULL;
    // r67: this team's pick (pick screens) first, else guests.txt's guest for the class.
    int side = -1;
    if (character.m_eClassType >= PLAYER)
    {
        const cTeam* team = static_cast<const cPlayer&>(character).m_pTeam;
        side = team != NULL ? (int)team->m_nSide : -1;
    }
    Guest* g = NULL;
    bool sparkle = false;  // r94: a shiny one twinkles (sparkle_model)
    const int picked = pick_for_side(side, cls);
    if (picked >= 0)
    {
        g = roster_guest(picked);
        // r70c: a shiny franchise captain; r78: a co-captain has its own shiny
        const bool coCaptain = side >= 0 && side < 2 && PortSecondCaptain(side) == cls;
        const bool shinyHere = side >= 0 && side < 2 && (coCaptain ? s_coShiny[side] : s_shinySide[side]);
        if (g != NULL && shinyHere && s_roster[picked].role == PORT_GUEST_CAPTAIN)
        {
            sparkle = true;
            Guest* shiny = roster_guest_shiny(picked);
            if (shiny != NULL)
                g = shiny;
        }
    }
    if (picked == PORT_GUEST_INHERIT)
        g = s_guestFor[cls];
    if (g == NULL)
        return NULL;
    const int n = (int)g->nodes.size();

    static float world[kMaxNodes][16];
    const nlMatrix4& root = acc->GetNodeMatrix(0);
    static_assert(sizeof(nlMatrix4) == 64, "nlMatrix4 is 16 floats");
    float rootM[16];
    memcpy(rootM, &root, sizeof rootM);
    const char* clipName = "";
    float t = 0.0f;
    if (g->retarget)
    {
        // r66: the Strikers character's own pose, bone for bone, on the guest's skeleton.
        if (!retarget_pose(g, cls, character, acc, rootM, world))
            return NULL;
        clipName = "(Strikers moves)";
    }
    else
    {
        // The guest's own clip for what the Strikers character is doing, cross-faded on a change.
        const cPN_SAnimController* ctrl = character.m_pCurrentAnimController;
        const cSAnim* hostClip = ctrl != NULL ? ctrl->m_pSAnim : NULL;
        t = ctrl != NULL ? ctrl->m_fTime : 0.0f;
        CharState* stp = &s_state[&character];
        int clip;
        if (s_scorerActive)
        {
            // r77: the life-size scorer's copy: the clip the show posed it with (the jog, a wave), or
            // the guest's own celebration for the port's dances (looped, ~30 keys a second)
            stp = &s_scorerState;
            static const Guest* s_scorerGuest = NULL;
            if (s_scorerGuest != g)  // another guest's show: no cross-fade from the last one's pose
            {
                s_scorerGuest = g;
                s_scorerState.started = false;
            }
            if (s_scorerClip != NULL)
            {
                clip = pick_clip(g, s_scorerClip);
                t = s_scorerNorm;
            }
            else
            {
                clip = g->celebrate >= 0 ? g->celebrate : (g->wave >= 0 ? g->wave : g->idle);
                const float secs = clip >= 0 && g->anims[clip].keys > 1 ? (float)g->anims[clip].keys / 30.0f : 1.0f;
                const float n = s_scorerCelebrate / secs;
                t = n - floorf(n);
            }
        }
        else
        {
            // r102: the clip the drawn pose really came from (the goal cut-scene's, a replay's), else live
            const PoseNote* note = note_for(acc, false);
            if (note != NULL && note->tree && note->anim != NULL)
            {
                static std::unordered_map<const Guest*, bool> s_treeLogged;
                if (!s_treeLogged[g])
                {
                    s_treeLogged[g] = true;
                    fprintf(stderr, "[guest] %s: posed from the cut-scene / replay clip \"%s\" (live \"%s\")\n",
                            g->name.c_str(), NameOf::get(note->anim), hostClip ? NameOf::get(hostClip) : "none");
                    fflush(stderr);
                }
                hostClip = note->anim;
                t = note->t;
            }
            clip = pace_clip(g, *stp, pick_clip(g, hostClip), rootM, &t);
        }
        CharState& st = *stp;
        if (clip < 0)
            return NULL;
        clipName = g->anims[clip].name.c_str();
        if (st.started && st.anim != clip)
        {
            memcpy(st.from, st.local, sizeof st.from);
            st.blend = 1.0f;
        }
        st.started = true;
        st.anim = clip;
        sample(g, g->anims[clip], t, st.local);
        static float pose[kMaxNodes][7];
        memcpy(pose, st.local, sizeof(float) * 7 * n);
        if (st.blend > 0.0f)
        {
            blend_into(pose, st.from, st.blend, n);
            st.blend -= 1.0f / 6.0f;
        }
        // World matrices: the root where the Strikers character's root is (its place and facing),
        // scaled to the guest's size there; the rest from the guest's own pose.
        for (int i = 0; i < n; i++)
        {
            const int par = g->nodes[i].parent;
            if (par < 0)
            {
                float s[16] = {g->scale, 0, 0, 0, 0, g->scale, 0, 0, 0, 0, g->scale, 0, 0, 0, 0, 1};
                mul(s, rootM, world[i]);
                continue;
            }
            float l[16];
            local_matrix(pose[i], l);
            mul(l, world[par], world[i]);
        }
    }
    if (g->retarget && g->scale != 1.0f)
        for (int i = 0; i < n; i++)
        {
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++)
                    world[i][r * 4 + c] *= g->scale;
            for (int c = 0; c < 3; c++)
                world[i][12 + c] = rootM[12 + c] + (world[i][12 + c] - rootM[12 + c]) * g->scale;
        }
    static float skin[kMaxNodes][16];
    for (int i = 0; i < n; i++)
    {
        if (g->nodes[i].hasInv)
            mul(g->nodes[i].inv, world[i], skin[i]);
        else
            memcpy(skin[i], world[i], sizeof skin[i]);
    }

    // r65b: guests.lock while the first frames of guests are drawn (see read_config).
    if (s_recoveryPending && s_recoveryFrame != g_portGlFrame)
    {
        s_recoveryFrame = g_portGlFrame;
        if (++s_recoveryFrames == 900)
        {
            remove((home_dir() + "/guests.lock").c_str());
            s_recoveryPending = false;
        }
    }
    static bool s_logged[12] = {};
    if (!s_logged[cls])
    {
        s_logged[cls] = true;
        fprintf(stderr, "[guest] first %s drawn as %s: root at %.2f %.2f %.2f, clip \"%s\" t %.2f\n", kClassNames[cls],
                g->name.c_str(), rootM[12], rootM[13], rootM[14], clipName, t);
        fflush(stderr);
    }
    // r65b (r65 crashed: "[aurora::gfx::gx] unmapped vtx attr 14"): every stream the Strikers
    // character's material reads, its dirt layer's second set of coordinates too; a packet without
    // one is a shader Aurora will not build. The skin's own streams (indices, weights) are left out.
    eGLStream ids[GLStream_Num];
    int nIds = 0;
    bool have[GLStream_Num] = {};
    {
        const glModelPacket& hp = hostModel->packets[0];
        for (int s = 0; s < (int)hp.numStreams; s++)
        {
            const int id = hp.streams[s].id;
            if (id < 0 || id >= (int)GLStream_Num || id == GLStream_Indices || id == GLStream_Weights ||
                id == GLStream_Position4 || have[id])
                continue;
            have[id] = true;
            ids[nIds++] = (eGLStream)id;
        }
        // And always the position, normal and both coordinate sets (the dirt's second set is the
        // attribute r65 lacked), whatever the host packet lists.
        // r65c: and every texture slot's coordinates (ids 3-8): SendToGl adds the light ramp
        // (BumpLocal) after this, and each texconfig bit takes the next TEXn attribute (glxSend.cpp).
        const eGLStream always[8] = {GLStream_Position, GLStream_Normal, GLStream_Diffuse, GLStream_Detail,
                                     GLStream_Shadow, GLStream_SelfIllum, GLStream_Gloss, GLStream_BumpLocal};
        for (int s = 0; s < 8; s++)
        {
            if (!have[always[s]])
            {
                have[always[s]] = true;
                ids[nIds++] = always[s];
            }
        }
        static bool s_streamsLogged = false;
        if (!s_streamsLogged)
        {
            s_streamsLogged = true;
            char list[160] = {};
            for (int s = 0; s < (int)hp.numStreams; s++)
            {
                char one[24];
                snprintf(one, sizeof one, "%s%d/%d", s ? " " : "", (int)hp.streams[s].id, (int)hp.streams[s].stride);
                strncat(list, one, sizeof list - strlen(list) - 1);
            }
            fprintf(stderr, "[guest] %s's material reads streams (id/stride) %s, texconfig %02x\n", kClassNames[cls], list,
                    (unsigned)hp.state.texconfig); fflush(stderr);
            fflush(stderr);
        }
    }
    // r66e: one model of all the guest's packets (the shadow and anything else handed the model see
    // all of it), in our own memory (see arena_alloc).
    s_frameModels.clear();
    arena_frame();
    static std::vector<u8> s_white;
    if (s_white.empty())
        s_white.assign((size_t)65536 * 4, 0xFF);
    bool needBasis = false;
    for (int s = 0; s < nIds; s++)
        needBasis = needBasis || ids[s] == GLStream_BasisS || ids[s] == GLStream_BasisT || ids[s] == GLStream_BasisSxT;
    glModel* model = (glModel*)arena_alloc(sizeof(glModel));
    glModelPacket* outPackets = (glModelPacket*)arena_alloc(sizeof(glModelPacket) * (g->packets.size() + 1));
    if (model == NULL || outPackets == NULL)
        return NULL;
    memset(model, 0, sizeof(glModel));
    int nOut = 0;
    // r68: every packet skinned first (sk, from skStart[packet]), so the whole guest can be lifted
    // off the floor below.
    static std::vector<Skinned> sk;
    static std::vector<size_t> skStart;
    skStart.assign(g->packets.size(), 0);
    size_t skTotal = 0;
    for (size_t pi = 0; pi < g->packets.size(); pi++)
    {
        const GPacket& pk = g->packets[pi];
        if (pk.strip.size() < 3 || pk.strip.size() > 65535 || pk.verts.empty())
            continue;
        skStart[pi] = skTotal;
        skTotal += pk.verts.size();
    }
    sk.resize(skTotal);
    for (size_t pi = 0; pi < g->packets.size(); pi++)
    {
        const GPacket& pk = g->packets[pi];
        if (pk.strip.size() < 3 || pk.strip.size() > 65535 || pk.verts.empty())
            continue;
        Skinned* skp = &sk[skStart[pi]];
        for (size_t v = 0; v < pk.verts.size(); v++)
        {
            const GVert& gv = pk.verts[v];
            float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0, tw = 0;
            for (int k = 0; k < 4; k++)
            {
                const float w = gv.w[k];
                if (w <= 0.0f)
                    continue;
                const float* m = skin[gv.node[k]];
                px += w * (gv.pos[0] * m[0] + gv.pos[1] * m[4] + gv.pos[2] * m[8] + m[12]);
                py += w * (gv.pos[0] * m[1] + gv.pos[1] * m[5] + gv.pos[2] * m[9] + m[13]);
                pz += w * (gv.pos[0] * m[2] + gv.pos[1] * m[6] + gv.pos[2] * m[10] + m[14]);
                nx += w * (gv.nrm[0] * m[0] + gv.nrm[1] * m[4] + gv.nrm[2] * m[8]);
                ny += w * (gv.nrm[0] * m[1] + gv.nrm[1] * m[5] + gv.nrm[2] * m[9]);
                nz += w * (gv.nrm[0] * m[2] + gv.nrm[1] * m[6] + gv.nrm[2] * m[10]);
                tw += w;
            }
            if (tw <= 0.0f)
            {
                const float* m = world[0];
                px = gv.pos[0] * m[0] + gv.pos[1] * m[4] + gv.pos[2] * m[8] + m[12];
                py = gv.pos[0] * m[1] + gv.pos[1] * m[5] + gv.pos[2] * m[9] + m[13];
                pz = gv.pos[0] * m[2] + gv.pos[1] * m[6] + gv.pos[2] * m[10] + m[14];
                nx = gv.nrm[0];
                ny = gv.nrm[1];
                nz = gv.nrm[2];
                tw = 1.0f;
            }
            skp[v].p.x = px / tw;
            skp[v].p.y = py / tw;
            skp[v].p.z = pz / tw;
            const float nl = sqrtf(nx * nx + ny * ny + nz * nz);
            const float inv = nl > 1e-12f ? 1.0f / nl : 0.0f;
            skp[v].n.x = nx * inv;
            skp[v].n.y = ny * inv;
            skp[v].n.z = nz * inv;
        }
    }
    // r68: nobody through the floor. A guest posed from another character's moves can reach below
    // the ground that character stands on (Moltres' wings on Yoshi's hanging arms, a long tail, a
    // foot in a bigger stride): the whole guest goes up by the deepest dip under its root's ground
    // this frame. Hovering ones (above it) are left where they are.
    {
        const float* up = rootM + 8;  // the root's up (row 2)
        const float ul = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        if (ul > 1e-6f && skTotal > 0)
        {
            const float u[3] = {up[0] / ul, up[1] / ul, up[2] / ul};
            float lowest = 0.0f;
            for (size_t v = 0; v < skTotal; v++)
            {
                const float h = (sk[v].p.x - rootM[12]) * u[0] + (sk[v].p.y - rootM[13]) * u[1] + (sk[v].p.z - rootM[14]) * u[2];
                if (h < lowest)
                    lowest = h;
            }
            if (lowest < 0.0f)
                for (size_t v = 0; v < skTotal; v++)
                {
                    sk[v].p.x -= lowest * u[0];
                    sk[v].p.y -= lowest * u[1];
                    sk[v].p.z -= lowest * u[2];
                }
        }
    }
    // r73: the flame flipbook's frame now (12 a second)
    timespec flameTs;
    clock_gettime(CLOCK_MONOTONIC, &flameTs);
    const int flameNow = (int)(((long long)flameTs.tv_sec * 1000 + flameTs.tv_nsec / 1000000) / 83 % 3);
    for (size_t pi = 0; pi < g->packets.size(); pi++)
    {
        const GPacket& pk = g->packets[pi];
        if (pk.strip.size() < 3 || pk.strip.size() > 65535 || pk.verts.empty())
            continue;
        if (pk.flameFrame >= 0 && pk.flameFrame != flameNow)
            continue;
        if (pk.gas)  // r101: smoke instead (smoke_model, below)
            continue;
        const Skinned* skp = &sk[skStart[pi]];
        // Down the strip, as GLMeshWriter would write them: positions as floats, normals as s8 x 64,
        // the light ramp's coordinate from the normal, basis vectors the normal (floats). Colours
        // (white) and the guest's coordinates (every texture slot reads the same) don't change
        // from frame to frame and are shared.
        const int nv = (int)pk.strip.size();
        u8* pos = (u8*)arena_alloc((size_t)nv * 12);
        s8* nrm = (s8*)arena_alloc((size_t)nv * 3);
        s16* ramp = (s16*)arena_alloc((size_t)nv * 4);
        float* basis = needBasis ? (float*)arena_alloc((size_t)nv * 12) : NULL;
        glModelStream* streams = (glModelStream*)arena_alloc(sizeof(glModelStream) * nIds);
        if (pos == NULL || nrm == NULL || ramp == NULL || (needBasis && basis == NULL) || streams == NULL)
            break;
        for (int k = 0; k < nv; k++)
        {
            const Skinned& v = skp[pk.strip[k]];
            memcpy(pos + (size_t)k * 12, &v.p, 12);
            nrm[k * 3 + 0] = (s8)(64.0f * v.n.x);
            nrm[k * 3 + 1] = (s8)(64.0f * v.n.y);
            nrm[k * 3 + 2] = (s8)(64.0f * v.n.z);
            // How much the normal faces the light (above, in front).
            const float lit = 0.5f + 0.5f * (v.n.x * 0.3f + v.n.y * -0.4f + v.n.z * 0.866f);
            ramp[k * 2 + 0] = (s16)(1024.0f * lit);
            ramp[k * 2 + 1] = 512;
            if (basis != NULL)
                memcpy(basis + (size_t)k * 3, &v.n, 12);
        }
        for (int s = 0; s < nIds; s++)
        {
            const int id = ids[s];
            glModelStream& st = streams[s];
            st.id = (u8)id;
            st.stride = kStride[id];
            st.beData = 0;  // host order
            st.dataSize = (u32)nv * kStride[id];
            if (id == GLStream_Position)
                st.address = (uintptr_t)pos;
            else if (id == GLStream_Normal)
                st.address = (uintptr_t)nrm;
            else if (id == GLStream_Colour)
                st.address = (uintptr_t)s_white.data();
            else if (id == GLStream_BumpLocal)
                st.address = (uintptr_t)ramp;
            else if (id >= GLStream_Diffuse && id <= GLStream_Gloss)
                st.address = (uintptr_t)pk.uvs.data();
            else
                st.address = (uintptr_t)basis;  // the three basis streams
        }
        glModelPacket& out = outPackets[nOut++];
        memset(&out, 0, sizeof out);
        out.numVertices = (u16)nv;
        out.primType = (u8)GLP_TriStrip;
        out.numStreams = (u8)nIds;
        out.streams = streams;
        // The Strikers character's material for this frame (program, lights' ramp, dirt, culling);
        // the guest's texture, and world-space vertices.
        // r66c: the Strikers character's own matrix handle as well (its skin draws in world space
        // with the identity one; the life-size scorer has one of its own, which the XR layers sort
        // by): the guest lands where the character would, table, stadium or scorer pass alike.
        out.state = hostModel->packets[0].state;
        if (pk.texture != NULL)
            out.state.texture[GLTT_Diffuse] = (uintptr_t)pk.texture;
        // r81 (Lance: "not lit the way i want them. they had like shadows on them??? pichu looked a bit
        // see through, so did machop"): the characters' material (texconfig 0x23, glxSend.cpp) fades from
        // the picture to the Detail picture - the Strikers character's dirty kit - by how dirty it is
        // (DrawableCharacter: every tackle adds 0.5-0.9, gone over ~75 s, up to 0.2 kept), colour AND
        // alpha. Read with the guest's coordinates, that kit landed as dark patches, its empty corners
        // with alpha 0 (the room through the guest over passthrough). A guest has no dirty kit: its own
        // picture in that slot too (the fade then changes nothing).
        out.state.texture[GLTT_Detail] = out.state.texture[GLTT_Diffuse];
        // r68h: drawn solid whatever the Strikers character's first packet does (it can be a blended
        // one: Lance's Mewtwo on Waluigi's slot glowed, looked soft and showed the room through him as
        // the life-size scorer); the guest's see-through textures with the game's alpha test instead.
        if (pk.rasterFrom != out.state.raster)
        {
            pk.rasterFrom = out.state.raster;
            u32 r = out.state.raster;
            static bool s_rasterLogged[12] = {};
            if (!s_rasterLogged[cls])
            {
                s_rasterLogged[cls] = true;
                fprintf(stderr, "[guest] %s's first packet: blend %u, depth write %u, alpha test %u -> drawn solid\n",
                        kClassNames[cls], (unsigned)glGetRasterState(r, GLS_AlphaBlend),
                        (unsigned)glGetRasterState(r, GLS_DepthWrite), (unsigned)glGetRasterState(r, GLS_AlphaTest));
                fflush(stderr);
            }
            glSetRasterState(r, GLS_AlphaBlend, pk.soft ? 1 : 0);
            glSetRasterState(r, GLS_DepthWrite, pk.soft ? 0 : 1);
            glSetRasterState(r, GLS_AlphaTest, (pk.alphaTest || pk.soft) ? 1 : 0);
            if (pk.alphaTest || pk.soft)
                glSetRasterState(r, GLS_AlphaTestRef, pk.soft ? 0x08 : 0x40);
            // r68i: both sides drawn (GX_CULL_NONE): the other games' wings, ears, capes and flames
            // are single sheets their own engines drew two-sided (Lance: "charizard is basically
            // half 2D"); closed bodies look the same, their backs are behind their fronts.
            glSetRasterState(r, GLS_Culling, 0);
            pk.rasterTo = r;
        }
        out.state.raster = pk.rasterTo;
        out.userData = 0;
        out.indexBuffer = 0;
        out.materialset = 0;
    }
    if (nOut == 0)
        return NULL;
    model->numPackets = (u32)nOut;
    model->packets = outPackets;
    s_frameModels.push_back(model);
    s_lastSmoke = NULL;
    if (g->gasRgb != 0)
    {
        // r101: the gas's centre and size this frame, from where its cards are (posed and swirling as before)
        double c[3] = {0, 0, 0};
        size_t cnt = 0;
        for (size_t pi = 0; pi < g->packets.size(); pi++)
        {
            const GPacket& pk = g->packets[pi];
            if (!pk.gas || pk.strip.size() < 3 || pk.strip.size() > 65535 || pk.verts.empty())
                continue;
            for (size_t v = 0; v < pk.verts.size(); v++)
            {
                const Skinned& s = sk[skStart[pi] + v];
                c[0] += s.p.x, c[1] += s.p.y, c[2] += s.p.z;
                cnt++;
            }
        }
        if (cnt > 0)
        {
            const float centre[3] = {(float)(c[0] / cnt), (float)(c[1] / cnt), (float)(c[2] / cnt)};
            double r2 = 0.0;
            for (size_t pi = 0; pi < g->packets.size(); pi++)
            {
                const GPacket& pk = g->packets[pi];
                if (!pk.gas || pk.strip.size() < 3 || pk.strip.size() > 65535 || pk.verts.empty())
                    continue;
                for (size_t v = 0; v < pk.verts.size(); v++)
                {
                    const Skinned& s = sk[skStart[pi] + v];
                    const double dx = s.p.x - centre[0], dy = s.p.y - centre[1], dz = s.p.z - centre[2];
                    r2 += dx * dx + dy * dy + dz * dz;
                }
            }
            const float radius = (float)sqrt(r2 / cnt);
            const float now = (float)(flameTs.tv_sec % 86400) + (float)flameTs.tv_nsec * 1e-9f;
            const u32 seed = (u32)(cls * 2 + (side > 0 ? 1 : 0)) + 0x5100u;
            if (glModel* sm = smoke_model(centre, radius, rootM, hostModel->packets[0], ids, nIds, seed, now, g->gasRgb))
            {
                s_frameModels.push_back(sm);
                s_lastSmoke = sm;
            }
        }
    }
    if (sparkle)
    {
        const float now = (float)(flameTs.tv_sec % 86400) + (float)flameTs.tv_nsec * 1e-9f;
        const u32 seed = (u32)(cls * 2 + (side > 0 ? 1 : 0));
        if (glModel* sp = sparkle_model(sk.data(), skTotal, rootM, hostModel->packets[0], ids, nIds, seed, now))
        {
            s_frameModels.push_back(sp);
            s_lastSparkle = sp;
        }
    }
    *numModels = (int)s_frameModels.size();
    return s_frameModels.empty() ? NULL : s_frameModels.data();
}

// r67: roster and picks (include/port/xr_guest.h).

namespace
{
// The picker (include/port/xr_guest.h): offered by the pick screen on the game thread, used by
// xr_quest.cpp's input and panels (the same thread: its input sync runs inside the game's PADRead,
// its panels at the end of the game's frame). Kept simple; a mutex would cost nothing either.
struct Picker
{
    int offerSide = -1, offerRole = -1;
    unsigned offerFrame = 0;
    bool free[8] = {};
    int commitSide = -1, commitRole = -1, commitIndex = -1;  // made on the panel, not yet taken
    int pendingIndex[2][2] = {{-1, -1}, {-1, -1}};            // taken, waiting for the screen's confirm
    int pendingSlot[2][2] = {{-1, -1}, {-1, -1}};
};
Picker s_picker;

// The other captains a guest's slot falls back to, nearest first (size and build).
const int kSlotOrder[8][7] = {
    // daisy 0, donkeykong 1, luigi 2, mario 3, peach 4, waluigi 5, wario 6, yoshi 7
    {4, 7, 3, 2, 5, 6, 1},  // daisy
    {6, 5, 2, 3, 7, 4, 0},  // donkeykong
    {3, 5, 0, 4, 7, 6, 1},  // luigi
    {2, 7, 6, 0, 4, 5, 1},  // mario
    {0, 2, 3, 7, 5, 6, 1},  // peach
    {2, 1, 6, 3, 4, 0, 7},  // waluigi
    {1, 3, 5, 2, 7, 4, 0},  // wario
    {0, 3, 4, 2, 6, 5, 1},  // yoshi
};
}  // namespace

void PortGuestPickerOffer(int side, int role, const bool freeCaptains[8])
{
    if (side < 0 || side > 1 || role < 0 || role > 1)
        return;
    read_roster();
    if (s_byRole[role].empty())
        return;  // no guests of this kind on this headset: nothing to offer
    s_picker.offerSide = side;
    s_picker.offerRole = role;
    s_picker.offerFrame = g_portGlFrame;
    for (int t = 0; t < 8; t++)
        s_picker.free[t] = freeCaptains != NULL ? freeCaptains[t] : true;
}

bool PortGuestPickerOffered(int* side, int* role)
{
    if (s_picker.offerSide < 0 || g_portGlFrame - s_picker.offerFrame > 4)
        return false;
    if (side != NULL)
        *side = s_picker.offerSide;
    if (role != NULL)
        *role = s_picker.offerRole;
    return true;
}

void PortGuestPickerCommit(int side, int role, int index)
{
    s_picker.commitSide = side;
    s_picker.commitRole = role;
    s_picker.commitIndex = index;
}

bool PortGuestPickerTake(int side, int role, int* index)
{
    if (s_picker.commitIndex < 0 || s_picker.commitSide != side || s_picker.commitRole != role)
        return false;
    if (index != NULL)
        *index = s_picker.commitIndex;
    s_picker.commitIndex = -1;
    s_picker.commitSide = s_picker.commitRole = -1;
    return true;
}

bool PortGuestPickerCaptainFree(int team)
{
    return team >= 0 && team < 8 && s_picker.free[team];
}

int PortGuestSlotFor(int index, const bool freeCaptains[8])
{
    // r68e: a Strikers character from the menu: its own slot (a captain only while it's free)
    if (index >= PORT_GUEST_ORIGINAL_SIDEKICK && index < PORT_GUEST_ORIGINAL_SIDEKICK + 4)
        return index - PORT_GUEST_ORIGINAL_SIDEKICK;
    if (index >= PORT_GUEST_ORIGINAL && index < PORT_GUEST_ORIGINAL + 8)
        return freeCaptains[index - PORT_GUEST_ORIGINAL] ? index - PORT_GUEST_ORIGINAL : -1;
    read_roster();
    if (index < 0 || index >= (int)s_roster.size())
        return -1;
    const RosterEntry& r = s_roster[index];
    if (r.role == PORT_GUEST_SIDEKICK)
        return r.base;  // sidekicks are never taken from the other side
    if (r.base < 0 || r.base >= 8)
        return -1;
    if (freeCaptains[r.base])
        return r.base;
    for (int k = 0; k < 7; k++)
        if (freeCaptains[kSlotOrder[r.base][k]])
            return kSlotOrder[r.base][k];
    return -1;
}

// r79: Grudge Match co-captains (include/port/lineup.h), see xr_guest.h.
bool PortGuestCoCaptainOffered(int side)
{
    GameInfoManager* gi = GameInfoManager::Instance();
    if (gi == NULL || side < 0 || side > 1 || !gi->IsInFriendlyMode())
        return false;
    const int team = (int)gi->GetTeam((short)side);
    return team >= 0 && team < 8;  // not the Super Team
}

bool PortGuestCoCaptainFree(int side, int team)
{
    if (side < 0 || side > 1 || team < 0 || team >= 8)
        return false;
    GameInfoManager* gi = GameInfoManager::Instance();
    const int cls = (int)ConvertToCharacterClass((eTeamID)team);
    for (int t = 0; t < 2; t++)
    {
        const int captain = (int)gi->GetTeam((short)t);
        if (captain >= 0 && captain < 8 && (int)ConvertToCharacterClass((eTeamID)captain) == cls)
            return false;  // a team's captain
    }
    return PortPickedCoCaptain(1 - side, NULL) != cls;  // the other side's co-captain
}

int PortGuestCoCaptainSlotFor(int side, int pick)
{
    bool freeCaptains[8];
    for (int t = 0; t < 8; t++)
        freeCaptains[t] = PortGuestCoCaptainFree(side, t);
    if (pick >= PORT_GUEST_ORIGINAL_SIDEKICK)
        return -1;
    return PortGuestSlotFor(pick, freeCaptains);
}

bool PortGuestCoCaptainPick(int side, int pick)
{
    if (side < 0 || side > 1)
        return false;
    if (pick < 0)
    {
        PortPickCoCaptain(side, -1, -1);
        fprintf(stderr, "[guest] side %d: no co-captain\n", side); fflush(stderr);
        return true;
    }
    if (PortGuestRosterLocked(pick))
        return false;
    const int slot = PortGuestCoCaptainSlotFor(side, pick);
    if (slot < 0)
        return false;
    const int guest = pick >= PORT_GUEST_ORIGINAL ? -1 : pick;
    PortPickCoCaptain(side, (int)ConvertToCharacterClass((eTeamID)slot), guest);
    read_roster();
    fprintf(stderr, "[guest] side %d: co-captain %s in team %d's slot (class %d)\n", side,
            guest >= 0 && guest < (int)s_roster.size() ? s_roster[guest].pack.c_str() : "(Strikers)", slot,
            (int)ConvertToCharacterClass((eTeamID)slot));
    fflush(stderr);
    return true;
}

int PortGuestCoCaptainCurrent(int side)
{
    int guest = -1;
    const int cls = PortPickedCoCaptain(side, &guest);
    if (cls < 0)
        return -1;
    if (guest >= 0)
        return guest;
    for (int t = 0; t < 8; t++)
        if ((int)ConvertToCharacterClass((eTeamID)t) == cls)
            return PORT_GUEST_ORIGINAL + t;
    return -1;
}

int PortGuestPendingSlot(int side, int role)
{
    if (side < 0 || side > 1 || role < 0 || role > 1)
        return -1;
    return s_picker.pendingIndex[side][role] >= 0 ? s_picker.pendingSlot[side][role] : -1;
}

void PortGuestPickerHold(int side, int role, int index, int slot)
{
    if (side < 0 || side > 1 || role < 0 || role > 1)
        return;
    s_picker.pendingIndex[side][role] = index;
    s_picker.pendingSlot[side][role] = slot;
}

void PortGuestCaptainChosen(int side, int team)
{
    if (side < 0 || side > 1)
        return;
    int& pending = s_picker.pendingIndex[side][PORT_GUEST_CAPTAIN];
    // r68e: a Strikers captain picked on the menu counts as a plain pick
    const bool mine = pending >= 0 && pending < PORT_GUEST_ORIGINAL && s_picker.pendingSlot[side][PORT_GUEST_CAPTAIN] == team;
    PortGuestSetPick(team, PORT_GUEST_CAPTAIN, mine ? pending : -1);
    // A plain pick on the game's own grid: no sidekick guest held over from before for this team.
    if (!mine)
        PortGuestSetPick(team, PORT_GUEST_SIDEKICK, -1);
    pending = -1;
}

void PortGuestSidekickChosen(int side, int team, int sidekick)
{
    if (side < 0 || side > 1)
        return;
    int& pending = s_picker.pendingIndex[side][PORT_GUEST_SIDEKICK];
    const bool mine = pending >= 0 && s_picker.pendingSlot[side][PORT_GUEST_SIDEKICK] == sidekick;
    PortGuestSetPick(team, PORT_GUEST_SIDEKICK, mine ? pending : -1);
    pending = -1;
}

namespace
{
// The keys CharacterTweaks.cpp reads (S2S frames and item chances stay the slot class's: they go
// with its animations and its team).
struct StatKey
{
    const char* name;
    float FielderTweaks::*field;
};
const StatKey kStatKeys[PORT_GUEST_STATS] = {
        {"Jog Speed", &FielderTweaks::fJoggingSpeed},
        {"Running Speed", &FielderTweaks::fRunningSpeed},
        {"Capsule Height", &FielderTweaks::fPhysCapsuleHeight},
        {"Capsule Width", &FielderTweaks::fPhysCapsuleRadius},
        {"Running Turn Speed", &FielderTweaks::fRunningDirectionSeekSpeed},
        {"Pass Ground Speed Max", &FielderTweaks::fPassGroundSpeedMax},
        {"Pass Ground Speed Min", &FielderTweaks::fPassGroundSpeedMin},
        {"Pass Volley Speed Max", &FielderTweaks::fPassVolleySpeedMax},
        {"Pass Volley Speed Min", &FielderTweaks::fPassVolleySpeedMin},
        {"Strafe Turn Speed", &FielderTweaks::fRunningStrafeDirectionSeekSpeed},
        {"Strafe Speed", &FielderTweaks::fRunningStrafeSpeed},
        {"Run Backwards Speed", &FielderTweaks::fRunningBackwardsSpeed},
        {"Turbo Turn Speed", &FielderTweaks::fRunningTurboDirectionSeekSpeed},
        {"Turbo Speed", &FielderTweaks::fRunningTurboSpeed},
        {"WB Run Turn Speed", &FielderTweaks::fRunningWBDirectionSeekSpeed},
        {"WB Run Speed", &FielderTweaks::fRunningWBSpeed},
        {"WB Run Turbo Turn Speed", &FielderTweaks::fRunningWBTurboDirectionSeekSpeed},
        {"WB Run Turbo Level 1 Speed", &FielderTweaks::fRunningWBTurboSpeedLevel1},
        {"WB Run Turbo Level 2 Speed", &FielderTweaks::fRunningWBTurboSpeedLevel2},
        {"WB Run Turbo Level 3 Speed", &FielderTweaks::fRunningWBTurboSpeedLevel3},
        {"Aggression", &FielderTweaks::fAggression},
        {"Shooting", &FielderTweaks::fShooting},
        {"Passing", &FielderTweaks::fPassing},
        {"Dekeing", &FielderTweaks::fDekeing},
        {"Shot Min Speed", &FielderTweaks::fShotMinSpeed},
        {"Shot Max Speed", &FielderTweaks::fShotMaxSpeed},
        {"Chip Shot Min Speed", &FielderTweaks::fShotChipMinSpeed},
        {"Chip Shot Max Speed", &FielderTweaks::fShotChipMaxSpeed},
};

// Conservative content limits: dimensions in metres, ratings in [0,1], linear speeds <= 100 m/s,
// angular seek rates <= 1,000,000.
bool stat_ok(int i, float v)
{
    const bool dimension = i == 2 || i == 3;
    const bool rating = i >= 20 && i <= 23;
    const bool turn = strstr(kStatKeys[i].name, "Turn Speed") != NULL;
    const float limit = dimension ? 10.0f : rating ? 1.0f : turn ? 1000000.0f : 100.0f;
    return portguest::valid_stat(v, dimension, limit);
}

// The min <= max pairs (passes, shots) still in order.
bool stats_ordered(const float* v)
{
    return v[6] <= v[5] && v[8] <= v[7] && v[24] <= v[25] && v[26] <= v[27];
}

// Online (r67b): the stats each side's guests play on, as their own headset sent them; both
// headsets apply these and nothing from their own files. Mask 0 = none (the slot's).
// [side][role]: 0 captain, 1 sidekick; r78: 2 the side's co-captain (include/port/lineup.h)
float s_onlineStats[2][3][PORT_GUEST_STATS];
unsigned s_onlineMask[2][3] = {};

// A guest's own stats file: the keys it sets (mask bits) and their values; false when the file is
// missing or anything in it is malformed (then none of it is used).
bool read_guest_stats(int index, float* values, unsigned* mask)
{
    *mask = 0;
    if (index < 0 || index >= (int)s_roster.size())
        return false;
    const std::string path = home_dir() + "/guests/stats/" + s_roster[index].pack + ".ini";
    FILE* f = fopen(path.c_str(), "r");
    if (f == NULL)
        return false;
    bool valid = true;
    char line[256];
    while (fgets(line, sizeof line, f) != NULL)
    {
        char* hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';
        char* eq = strchr(line, '=');
        if (eq == NULL)
            continue;
        *eq = '\0';
        // trim the key
        char* k = line;
        while (*k == ' ' || *k == '\t')
            k++;
        char* ke = eq;
        while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t'))
            *--ke = '\0';
        for (int i = 0; i < PORT_GUEST_STATS; i++)
        {
            if (strcasecmp(k, kStatKeys[i].name) != 0)
                continue;
            char* endp = NULL;
            errno = 0;
            const float v = strtof(eq + 1, &endp);
            const bool parsed = endp != eq + 1;
            while (*endp == ' ' || *endp == '\t' || *endp == '\r' || *endp == '\n')
                endp++;
            if (!parsed || *endp != '\0' || errno == ERANGE || !stat_ok(i, v))
                valid = false;
            else
            {
                values[i] = v;
                *mask |= 1u << i;
            }
            break;
        }
    }
    valid = valid && !ferror(f);
    fclose(f);
    if (!valid)
    {
        fprintf(stderr, "[guest] invalid stats in %s; none of them used\n", path.c_str());
        fflush(stderr);
        *mask = 0;
    }
    return valid;
}
}  // namespace

int PortGuestStatsFor(int index, float* values, unsigned* mask)
{
    read_roster();
    if (values == NULL || mask == NULL || !guests_enabled())
        return 0;
    return read_guest_stats(index, values, mask) && *mask != 0 ? 1 : 0;
}

void PortGuestSetOnlineStats(int side, int role, const float* values, unsigned mask)
{
    if (side < 0 || side > 1 || role < 0 || role > 2)  // r78: 2 = the co-captain
        return;
    s_onlineMask[side][role] = 0;
    if (values == NULL)
        return;
    // From the network: each value checked again; anything off and that guest plays on its slot's.
    mask &= (1u << PORT_GUEST_STATS) - 1u;
    for (int i = 0; i < PORT_GUEST_STATS; i++)
    {
        if ((mask & (1u << i)) == 0)
            continue;
        if (!stat_ok(i, values[i]))
            return;
        s_onlineStats[side][role][i] = values[i];
    }
    s_onlineMask[side][role] = mask;
}

int PortGuestLoadingPicture(const char* name, void* buf, unsigned long size)
{
    if (name == NULL || buf == NULL || !guests_enabled())
        return 0;
    const char* base = strrchr(name, '/');
    base = base != NULL ? base + 1 : name;
    // r68b: the score bar's team banner (HUD "CAPTAIN SYMBOL", pause Change Sides):
    // captain_icons_<daisy|dk|luigi|mario|peach|waluigi|wario|yoshi|super>[_r] -> the guest
    // captain's own (guests/icons/<pack>[_r].gtx, tools/outside/iconres.py: its series emblem from
    // Melee's HUD on the Strikers banner).
    static const char kIcons[] = "captain_icons_";
    if (strncmp(base, kIcons, sizeof kIcons - 1) == 0)
    {
        char team[32];
        snprintf(team, sizeof team, "%s", base + sizeof kIcons - 1);
        const size_t len = strlen(team);
        const bool right = len > 2 && strcmp(team + len - 2, "_r") == 0;
        if (right)
            team[len - 2] = '\0';
        const int t = strcmp(team, "dk") == 0 ? team_of("donkeykong") : team_of(team);
        if (t < 0)
            return 0;
        const int pick = PortGuestGetPick(t, PORT_GUEST_CAPTAIN);
        if (pick < 0 || pick >= (int)s_roster.size())
            return 0;
        const std::string path = home_dir() + "/guests/icons/" + s_roster[pick].pack + (right ? "_r" : "") + ".gtx";
        FILE* f = fopen(path.c_str(), "rb");
        if (f == NULL)
            return 0;
        fseek(f, 0, SEEK_END);
        const long have = ftell(f);
        fseek(f, 0, SEEK_SET);
        bool ok = have > 0x20 && (unsigned long)have <= size;
        std::vector<u8> data(ok ? (size_t)have : 0);
        ok = ok && fread(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        if (!ok)
            return 0;
        memcpy(buf, data.data(), data.size());
        fprintf(stderr, "[guest] %s: %s's own banner\n", base, s_roster[pick].pack.c_str());
        fflush(stderr);
        return 1;
    }
    // <team>_<l|r>[_bg|_white]
    const char* us = strchr(base, '_');
    if (us == NULL || us - base >= 32 || (us[1] != 'l' && us[1] != 'r') || (us[2] != '\0' && us[2] != '_'))
        return 0;
    char team[32];
    memcpy(team, base, (size_t)(us - base));
    team[us - base] = '\0';
    for (char* c = team; *c; c++)
        if (*c >= 'A' && *c <= 'Z')
            *c = (char)(*c - 'A' + 'a');
    const int t = team_of(team);
    if (t < 0)
        return 0;
    const int pick = PortGuestGetPick(t, PORT_GUEST_CAPTAIN);
    if (pick < 0 || pick >= (int)s_roster.size())
        return 0;
    const std::string path = home_dir() + "/guests/loading/" + s_roster[pick].pack + us + ".gtx";
    FILE* f = fopen(path.c_str(), "rb");
    if (f == NULL)
        return 0;
    fseek(f, 0, SEEK_END);
    const long have = ftell(f);
    fseek(f, 0, SEEK_SET);
    // The same size as the picture it stands in for (or smaller): the game reads it with that size.
    bool ok = have > 0x20 && (unsigned long)have <= size;
    std::vector<u8> data(ok ? (size_t)have : 0);
    ok = ok && fread(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    if (!ok)
        return 0;
    memcpy(buf, data.data(), data.size());
    fprintf(stderr, "[guest] %s: %s's own picture\n", base, s_roster[pick].pack.c_str());
    fflush(stderr);
    return 1;
}

void PortGuestClearOnlineStats()
{
    memset(s_onlineMask, 0, sizeof s_onlineMask);
}

// r80: the stats each player's tweaks got from its guest (bit = kStatKeys index), for PortGuestOwnStats
static std::unordered_map<const void*, unsigned> s_ownStatMask;

// r103: the online stats slot (PortGuestSetOnlineStats' role) of class `cls` on `side`, the way pick_for_side
// sorts it, from the match itself (no roster needed): 0 its captain, 2 its co-captain, 1 its sidekicks; -1 none.
static int online_role(int side, int cls)
{
    GameInfoManager* gi = GameInfoManager::Instance();
    const int team = (int)gi->GetTeam((short)side);
    if (team < 0 || team >= 8)
        return -1;
    if ((int)ConvertToCharacterClass((eTeamID)team) == cls)
        return 0;
    if (PortSecondCaptain(side) == cls)
        return 2;
    if ((int)ConvertToCharacterClass(gi->GetSidekick((short)side)) == cls)
        return 1;
    return -1;
}

void PortGuestApplyTweaks(int side, int cls, FielderTweaks* t)
{
    if (t != NULL)
        s_ownStatMask.erase(t);  // (a new player: nothing of its own yet)
    if (t == NULL || side < 0 || side > 1)
        return;
    float own[PORT_GUEST_STATS];
    unsigned mask = 0;
    int picked = -1;
    const char* name = "a guest";
    if (PortNetOnline())
    {
        // Online both headsets run the very same match: the stats that side's own headset sent.
        // r103: whether or not this headset has that guest (a friend without its game, or with guests
        // off, draws the slot's character but plays it on the same stats; was: the slot's own = desync).
        const int role = online_role(side, cls);
        if (role < 0)
            return;
        mask = s_onlineMask[side][role];
        if (mask == 0)
            return;
        memcpy(own, s_onlineStats[side][role], sizeof own);
        picked = guests_enabled() ? pick_for_side(side, cls) : -1;
        if (picked < 0 || picked >= (int)s_roster.size())
            name = "a guest this headset doesn't have";
    }
    else
    {
        if (!guests_enabled())
            return;
        picked = pick_for_side(side, cls);
        if (picked < 0)
            return;
        if (!read_guest_stats(picked, own, &mask))
        {
            fprintf(stderr, "[guest] side %d %s: no usable stats file, playing on its slot's\n", side,
                    s_roster[picked].pack.c_str());
            fflush(stderr);
            return;
        }
    }
    if (picked >= 0 && picked < (int)s_roster.size())
        name = s_roster[picked].pack.c_str();
    float values[PORT_GUEST_STATS];
    for (int i = 0; i < PORT_GUEST_STATS; i++)
        values[i] = (mask & (1u << i)) ? own[i] : t->*kStatKeys[i].field;
    if (mask == 0 || !stats_ordered(values))
    {
        fprintf(stderr, "[guest] side %d %s: %s, playing on its slot's\n", side, name,
                mask == 0 ? "no stats for it" : "its stats out of order");
        fflush(stderr);
        return;
    }
    int applied = 0;
    for (int i = 0; i < PORT_GUEST_STATS; i++)
    {
        t->*kStatKeys[i].field = values[i];
        applied += (mask >> i) & 1u;
    }
    s_ownStatMask[t] = mask;
    fprintf(stderr, "[guest] side %d %s plays as itself%s: %d of its own stats (run %.2f, turbo %.2f, shot %.1f)\n", side,
            name, PortNetOnline() ? " (online, as its headset sent them)" : "", applied,
            t->fRunningSpeed, t->fRunningTurboSpeed, t->fShotMaxSpeed);
    fflush(stderr);
}

unsigned PortGuestOwnStats(const void* tweaks)
{
    const auto it = s_ownStatMask.find(tweaks);
    return it != s_ownStatMask.end() ? it->second : 0u;
}

int PortGuestRosterCount(int role)
{
    read_roster();
    return role == 0 || role == 1 ? (int)s_byRole[role].size() : 0;
}

int PortGuestRosterAt(int role, int i)
{
    read_roster();
    if (role < 0 || role > 1 || i < 0 || i >= (int)s_byRole[role].size())
        return -1;
    return s_byRole[role][i];
}

const char* PortGuestRosterPack(int index)
{
    read_roster();
    return index >= 0 && index < (int)s_roster.size() ? s_roster[index].pack.c_str() : NULL;
}

const char* PortGuestRosterName(int index)
{
    read_roster();
    return index >= 0 && index < (int)s_roster.size() ? s_roster[index].name.c_str() : NULL;
}

int PortGuestIndexFor(int side, int cls)
{
    if (!guests_enabled())
        return -1;
    const int picked = pick_for_side(side, cls);
    return picked >= 0 ? picked : -1;
}

int PortGuestCaptainFor(int side)
{
    if (!guests_enabled() || side < 0 || side > 1)
        return -1;
    const int team = (int)GameInfoManager::Instance()->GetTeam((short)side);
    if (team < 0 || team >= 8)
        return -1;
    return PortGuestIndexFor(side, (int)ConvertToCharacterClass((eTeamID)team));
}

// r68h (Lance: "ima need EVERY sidekick to have a generated name to differentiate between everyone"):
// each sidekick a name of its own, for good: worked out from who it is (its kind: Toad ... or a
// Charged guest sidekick), whose team (the captain: a Strikers one or a guest) and which of the three
// it is, so it is the same every match and on both headsets online, with no file to keep.
namespace
{
struct NamePool
{
    const char* kind;
    const char* first[24];
};
const NamePool kSidekickNames[] = {
    {"toad", {"Toadley", "Spore", "Shroomer", "Capper", "Fungo", "Puff", "Morel", "Truffo", "Porcini", "Enoki",
              "Shiitake", "Button", "Stemmy", "Gilly", "Myco", "Toadwin", "Spotty", "Cappy", "Bolete", "Russo",
              "Puffball", "Chanty", "Toadsby", "Sporeman"}},
    {"koopa", {"Shellby", "Kooper", "Shelldon", "Kip", "Koopley", "Scute", "Cara", "Terrance", "Kooz", "Shelton",
               "Hardy", "Snap", "Turtlo", "Krag", "Shellsea", "Plastra", "Shelvin", "Kopper", "Tank", "Domer",
               "Clamp", "Scoot", "Kurt", "Paddler"}},
    {"hammerbros", {"Hammond", "Mallet", "Sledge", "Peen", "Thud", "Rivet", "Bonk", "Ballpeen", "Tackett", "Nailer",
                    "Forge", "Anvil", "Brock", "Hamm", "Mauler", "Gavel", "Jack", "Spike", "Bolt", "Wrench", "Chip",
                    "Hatch", "Smithy", "Claw"}},
    {"birdo", {"Birda", "Egbert", "Eggsy", "Bowina", "Ovia", "Shelly", "Yolanda", "Plume", "Rosa", "Kiki", "Pippa",
               "Beaky", "Ova", "Nessa", "Pinky", "Robin", "Wren", "Lark", "Clutch", "Hatchie", "Bria", "Dotty",
               "Polka", "Gemma"}},
    {"boo", {"Boolio", "Spook", "Wisp", "Haunt", "Shade", "Specter", "Phantom", "Boozie", "Booker", "Murk", "Gloom",
             "Haze", "Misty", "Eerie", "Creep", "Chill", "Wail", "Shroud", "Grim", "Banshee", "Boodini", "Foggy",
             "Echo", "Peekaboo"}},
    {"drybones", {"Rattle", "Femur", "Bonaparte", "Tibia", "Ulna", "Clatter", "Marrow", "Skully", "Ribs", "Sternum",
                  "Cal", "Dusty", "Brittle", "Crypt", "Osso", "Jawbone", "Knuckles", "Patella", "Radius", "Vert",
                  "Coccyx", "Sacrum", "Hugh Merus", "Bonnie"}},
    {"montymole", {"Burrow", "Digby", "Tunnel", "Clod", "Digger", "Loam", "Gravel", "Pebble", "Rocky", "Sandy", "Clay",
                   "Shovel", "Mound", "Dirtbert", "Grub", "Delve", "Furrow", "Monte", "Montague", "Tiller", "Spade",
                   "Trench", "Silt", "Rooter"}},
    {"shyguy", {"Masky", "Shyler", "Bashful", "Timid", "Hush", "Peep", "Blush", "Mumbles", "Whisper", "Coy", "Meek",
                "Shush", "Sly", "Murmur", "Pip", "Skittish", "Shady", "Veil", "Robe", "Cloak", "Mumu", "Hiddy",
                "Stilts", "Propella"}},
};
const char* const kSurnames[32] = {"Kickworth", "Boots", "Netley", "Goalson", "Volley", "Strikerson", "Dribbles",
                                   "Cleats", "Hattrick", "Nutmeg", "Crossbar", "Header", "Slider", "Turf",
                                   "Pitchford", "Banks", "Rush", "Blitz", "Thunder", "Rocket", "Dash", "Pinball",
                                   "Postman", "Corner", "Chipper", "Topspin", "Bender", "Sweeper", "Wingman",
                                   "Offsides", "Penalty", "Golazo"};

u32 name_hash(const std::string& s)
{
    u32 h = 2166136261u;
    for (unsigned char ch : s)
        h = (h ^ ch) * 16777619u;
    return h;
}
}  // namespace

const char* PortGuestSidekickNameFor(const char* kind, const char* key)
{
    if (kind == NULL || key == NULL)
        return NULL;
    const NamePool* pool = &kSidekickNames[0];
    for (const NamePool& np : kSidekickNames)
        if (strcmp(kind, np.kind) == 0)
            pool = &np;
    static std::unordered_map<std::string, std::string> s_made;
    const std::string k = std::string(kind) + "#" + key;
    std::unordered_map<std::string, std::string>::const_iterator it = s_made.find(k);
    if (it != s_made.end())
        return it->second.c_str();
    const u32 h = name_hash(k);
    const std::string name = std::string(pool->first[h % 24u]) + " " + kSurnames[(h >> 8) % 32u];
    return s_made.emplace(k, name).first->second.c_str();
}

const char* PortGuestSidekickName(int side, int playerId)
{
    // r70: a franchise club's sidekicks keep their own names (dealt with the club, saved)
    if (const char* franchise = PortFranchiseSidekickName(side, playerId))
        return franchise;
    if (side < 0 || side > 1 || playerId < 1 || playerId > 4 || g_pTeams[side] == NULL)
        return NULL;
    const cPlayer* p = g_pTeams[side]->GetPlayer(playerId);
    if (p == NULL)
        return NULL;
    const int cls = (int)p->m_eCharacterClass;
    if (cls != TOAD && cls != KOOPA && cls != HAMMERBROS && cls != BIRDO)
        return NULL;  // a captain: its own name
    // its kind: a Charged guest sidekick playing this slot, or the Strikers one
    std::string kind = kClassNames[cls];
    const int guest = PortGuestIndexFor(side, cls);
    if (guest >= 0 && guest < (int)s_roster.size())
        kind = s_roster[guest].pack;
    // whose team: its captain (a guest, or the Strikers one)
    std::string team;
    const int captainGuest = PortGuestCaptainFor(side);
    if (captainGuest >= 0 && captainGuest < (int)s_roster.size())
        team = s_roster[captainGuest].pack;
    else
    {
        const int captainClass = (int)ConvertToCharacterClass((eTeamID)GameInfoManager::Instance()->GetTeam((short)side));
        team = std::string("strikers ") + (captainClass >= 0 && captainClass < 12 ? kClassNames[captainClass] : "superteam");
    }
    const NamePool* pool = &kSidekickNames[0];
    for (const NamePool& np : kSidekickNames)
        if (kind == np.kind)
            pool = &np;
    // the three of a team never share a first name
    static std::unordered_map<std::string, std::string> s_names;
    const std::string key = team + "|" + kind + "|" + std::to_string(playerId);
    std::unordered_map<std::string, std::string>::const_iterator it = s_names.find(key);
    if (it != s_names.end())
        return it->second.c_str();
    const u32 h = name_hash(key);
    bool used[24] = {};
    int first = 0;
    for (int slot = 1; slot <= playerId; slot++)  // the earlier ones' first names, in turn, then this one's
    {
        first = (int)(name_hash(team + "|" + kind + "|" + std::to_string(slot)) % 24u);
        while (used[first])
            first = (first + 7) % 24;
        used[first] = true;
    }
    const std::string name = std::string(pool->first[first]) + " " + kSurnames[(h >> 8) % 32u];
    return s_names.emplace(key, name).first->second.c_str();
}

int PortGuestThemeColors(int index, unsigned* main, unsigned* trim, unsigned* patch)
{
    static std::unordered_map<std::string, unsigned> s_cols[3];
    static bool s_read = false;
    if (!s_read)
    {
        s_read = true;
        if (FILE* f = fopen((home_dir() + "/guests/colors.txt").c_str(), "r"))
        {
            char line[160], pack[64];
            unsigned m, t, p;
            while (fgets(line, sizeof line, f) != NULL)
                if (line[0] != '#' && sscanf(line, "%63s %x %x %x", pack, &m, &t, &p) == 4)
                {
                    s_cols[0][pack] = m & 0xFFFFFF;
                    s_cols[1][pack] = t & 0xFFFFFF;
                    s_cols[2][pack] = p & 0xFFFFFF;
                }
            fclose(f);
        }
    }
    read_roster();
    if (index < 0 || index >= (int)s_roster.size())
        return 0;
    const std::string& pack = s_roster[index].pack;
    if (s_cols[0].count(pack) == 0)
        return 0;
    *main = s_cols[0][pack];
    *trim = s_cols[1][pack];
    *patch = s_cols[2][pack];
    return 1;
}

unsigned long PortGuestKitTexture(const cCharacter& c)
{
    if (!guests_enabled() || c.m_eClassType < PLAYER)
        return 0;
    const int cls = (int)c.m_eCharacterClass;
    const char* fam = cls == TOAD ? "toad"
                      : cls == KOOPA ? "koopa"
                      : cls == HAMMERBROS ? "hammer"
                      : cls == BIRDO ? "birdo"
                      : (cls >= DAISY_GOALIE && cls <= SUPERTEAM_GOALIE) ? "goalie"
                                                                         : NULL;
    const cTeam* team = static_cast<const cPlayer&>(c).m_pTeam;
    if (fam == NULL || team == NULL)
        return 0;
    const int pick = PortGuestCaptainFor((int)team->m_nSide);
    if (pick < 0 || pick >= (int)s_roster.size())
        return 0;
    // Made once and kept (two teams' worth a match; our own memory, never the game's inventory).
    static std::unordered_map<std::string, PlatTexture*> s_kits;
    const std::string key = std::string(fam) + "_" + s_roster[pick].pack;
    std::unordered_map<std::string, PlatTexture*>::const_iterator it = s_kits.find(key);
    if (it != s_kits.end())
        return (unsigned long)(uintptr_t)it->second;
    PlatTexture* tex = NULL;
    FILE* f = fopen((home_dir() + "/guests/kits/" + key + ".gtx").c_str(), "rb");
    if (f != NULL)
    {
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<u8> buf(size > 0 && size < (8L << 20) ? (size_t)size : 0);
        if (!buf.empty() && fread(buf.data(), 1, buf.size(), f) == buf.size())
            tex = make_texture(buf.data(), (u32)buf.size());
        fclose(f);
    }
    fprintf(stderr, "[guest] kit %s: %s\n", key.c_str(), tex != NULL ? "own colours" : "none (the slot's)");
    fflush(stderr);
    s_kits[key] = tex;
    return (unsigned long)(uintptr_t)tex;
}

bool PortGuestTeamNameHasGuestCaptain(const char* teamName)
{
    return teamName != NULL && PortGuestTeamHasGuestCaptain(team_of(teamName));
}

bool PortGuestTeamHasGuestCaptain(int captainTeam)
{
    if (!guests_enabled() || captainTeam < 0 || captainTeam >= 8)
        return false;
    if (PortGuestGetPick(captainTeam, PORT_GUEST_CAPTAIN) >= 0)
        return true;
    for (int s = 0; s < 2; s++)
        if (PortGuestPendingSlot(s, PORT_GUEST_CAPTAIN) == captainTeam &&
            s_picker.pendingIndex[s][PORT_GUEST_CAPTAIN] < PORT_GUEST_ORIGINAL)  // r68e: not a Strikers one
            return true;
    return false;
}

int PortGuestRosterFind(int role, const char* pack)
{
    read_roster();
    if (pack == NULL || pack[0] == '\0' || role < 0 || role > 1)
        return -1;
    for (int index : s_byRole[role])
        if (s_roster[index].pack == pack)
            return index;
    return -1;
}

// r70c: the evolved forms unlocked by evolving them in franchise mode (any career), for every mode:
// STRIKERS_HOME/franchise/unlocked.txt, a pack a line.
namespace
{
std::vector<std::string> s_unlocked;
bool s_unlockedRead = false;
std::mutex s_unlockMutex;  // the menu's drawing threads ask too

void read_unlocked()  // (under s_unlockMutex)
{
    if (s_unlockedRead)
        return;
    s_unlockedRead = true;
    FILE* f = fopen((home_dir() + "/franchise/unlocked.txt").c_str(), "r");
    if (f == NULL)
        return;
    char line[128];
    while (fgets(line, sizeof line, f) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0' && line[0] != '#')
            s_unlocked.push_back(line);
    }
    fclose(f);
}
}  // namespace

int PortGuestRosterFindAny(int role, const char* pack)
{
    read_roster();
    if (pack == NULL || pack[0] == '\0' || role < 0 || role > 1)
        return -1;
    for (size_t i = 0; i < s_roster.size(); i++)
        if (s_roster[i].role == role && s_roster[i].pack == pack)
            return (int)i;
    return -1;
}

void PortGuestSetShiny(int side, bool shiny)
{
    if (side >= 0 && side < 2)
        s_shinySide[side] = shiny;
}

void PortGuestSetCoCaptain(int side, int rosterIndex, bool shiny)
{
    if (side < 0 || side > 1)
        return;
    s_coPick[side] = rosterIndex >= 0 ? rosterIndex : -1;
    s_coShiny[side] = shiny;
}

bool PortGuestRosterEvolved(int index)
{
    read_roster();
    return index >= 0 && index < (int)s_roster.size() && s_roster[index].franchiseOnly;
}

const char* PortGuestRosterEvolvesFrom(int index)
{
    read_roster();
    return index >= 0 && index < (int)s_roster.size() ? s_roster[index].evolvesFrom.c_str() : "";
}

bool PortGuestRosterLocked(int index)
{
    if (!PortGuestRosterEvolved(index))
        return false;
    std::lock_guard<std::mutex> lock(s_unlockMutex);
    read_unlocked();
    return std::find(s_unlocked.begin(), s_unlocked.end(), s_roster[index].pack) == s_unlocked.end();
}

bool PortGuestUnlock(const char* pack)
{
    if (pack == NULL || pack[0] == '\0')
        return false;
    std::lock_guard<std::mutex> lock(s_unlockMutex);
    read_unlocked();
    if (std::find(s_unlocked.begin(), s_unlocked.end(), std::string(pack)) != s_unlocked.end())
        return false;
    s_unlocked.push_back(pack);
    FILE* f = fopen((home_dir() + "/franchise/unlocked.txt").c_str(), "a");
    if (f != NULL)
    {
        fprintf(f, "%s\n", pack);
        fclose(f);
    }
    fprintf(stderr, "[guest] unlocked %s for every mode\n", pack);
    fflush(stderr);
    return true;
}

const char* PortGuestRosterGame(int index)
{
    read_roster();
    return index >= 0 && index < (int)s_roster.size() ? s_roster[index].game.c_str() : NULL;
}

int PortGuestRosterBase(int index)
{
    read_roster();
    return index >= 0 && index < (int)s_roster.size() ? s_roster[index].base : -1;
}

int PortGuestGetPick(int captainTeam, int role)
{
    read_picks();
    const int mode = current_mode();
    if (mode < 0 || captainTeam < 0 || captainTeam >= 8 || role < 0 || role > 1)
        return -1;
    return s_pick[mode][captainTeam][role];
}

void PortGuestSetPick(int captainTeam, int role, int index)
{
    read_picks();
    const int mode = current_mode();
    if (mode < 0 || captainTeam < 0 || captainTeam >= 8 || role < 0 || role > 1)
        return;
    if (index < PORT_GUEST_INHERIT || index >= (int)s_roster.size() || (index >= 0 && s_roster[index].role != role))
        index = -1;
    if (s_pick[mode][captainTeam][role] == index)
        return;
    s_pick[mode][captainTeam][role] = index;
    fprintf(stderr, "[guest] pick: mode %d, team %d's %s = %s\n", mode, captainTeam, role ? "sidekick" : "captain",
            index >= 0 ? s_roster[index].pack.c_str() : "(themselves)");
    fflush(stderr);
    write_picks();
}

void PortGuestClearPicks()
{
    read_picks();
    const int mode = current_mode();
    if (mode < 0)
        return;
    bool any = false;
    for (int t = 0; t < 8; t++)
        for (int r = 0; r < 2; r++)
        {
            any = any || s_pick[mode][t][r] != -1;
            s_pick[mode][t][r] = -1;
        }
    if (any)
        write_picks();
}

unsigned long PortGuestLocName(int index)
{
    read_roster();
    return index >= 0 && index < (int)s_roster.size() ? s_roster[index].locKey : 0;
}

unsigned long PortGuestLocTeamName(int captainTeam)
{
    return guests_enabled() ? PortGuestLocName(PortGuestGetPick(captainTeam, PORT_GUEST_CAPTAIN)) : 0;
}

unsigned long PortGuestLocTeamDesc(int captainTeam)
{
    if (!guests_enabled())
        return 0;
    read_roster();
    const int pick = PortGuestGetPick(captainTeam, PORT_GUEST_CAPTAIN);
    return pick >= 0 && pick < (int)s_roster.size() ? s_roster[pick].descKey : 0;
}

unsigned long PortGuestLocSidekickName(int captainTeam, int sidekick)
{
    if (!guests_enabled())
        return 0;
    const int pick = PortGuestGetPick(captainTeam, PORT_GUEST_SIDEKICK);
    // A newly highlighted ordinary sidekick must not inherit the old selection's name.
    return pick >= 0 && s_roster[pick].base == sidekick ? PortGuestLocName(pick) : 0;
}

unsigned long PortGuestLocCoCaptainName(int side)
{
    if (!guests_enabled() || side < 0 || side > 1 || PortSecondCaptain(side) < 0)
        return 0;
    return PortGuestLocName(s_coPick[side]);
}

const char* PortGuestShownName(int side, int cls)
{
    if (!guests_enabled())
        return NULL;
    const int picked = pick_for_side(side, cls);
    if (picked >= 0)
        return s_roster[picked].name.c_str();
    return NULL;
}

void PortGuestLocInject(nlLocalization* loc)
{
    read_roster();
    if (loc == NULL || loc->m_pFile == NULL || loc->m_LookupTable == NULL || s_roster.empty())
        return;
    const u32 n = loc->m_pFile->StringCount;
    // The table's strings run from m_FirstString to the end of the last one.
    u32 used = 0;
    for (u32 i = 0; i < n; i++)
    {
        const unsigned short* s = loc->m_FirstString + loc->m_LookupTable[i].StringOffset;
        u32 len = 0;
        while (s[len] != 0)
            len++;
        const u32 end = loc->m_LookupTable[i].StringOffset + len + 1;
        if (end > used)
            used = end;
    }
    std::vector<nlLocalization::StringLookup> table(loc->m_LookupTable, loc->m_LookupTable + n);
    std::vector<unsigned short> strings(loc->m_FirstString, loc->m_FirstString + used);
    int added = 0;
    // Each guest's name, and (r68b) the line the cup / tournament screens show under a captain's
    // name (the Strikers captain's description): where the guest is from.
    std::vector<std::pair<u32, std::string> > texts;
    for (size_t r = 0; r < s_roster.size(); r++)
    {
        texts.push_back(std::make_pair((u32)s_roster[r].locKey, s_roster[r].name));
        const std::string& game = s_roster[r].game;
        const char* from = game == "Charged" ? "From Mario Strikers Charged"
                           : game == "Melee" ? "From Super Smash Bros. Melee"
                           : game == "Melee Pokemon" ? "A Pokemon from Super Smash Bros. Melee"
                           : game == "Colosseum" ? "A Pokemon from Pokemon Colosseum"
                                                     : "A guest from another game";
        texts.push_back(std::make_pair((u32)s_roster[r].descKey, std::string(from)));
    }
    for (size_t r = 0; r < texts.size(); r++)
    {
        const u32 key = texts[r].first;
        if (nlBSearch<nlLocalization::StringLookup, unsigned long>((unsigned long)key, loc->m_LookupTable, (int)n) != NULL)
            continue;  // already there (a second Load of the same table)
        nlLocalization::StringLookup e;
        e.hash = key;
        e.StringOffset = (u32)strings.size();
        // UTF-8 -> UTF-16 (the names are ASCII but for an accent or two)
        const unsigned char* p = (const unsigned char*)texts[r].second.c_str();
        while (*p)
        {
            unsigned c = *p++;
            if (c >= 0xC0 && (*p & 0xC0) == 0x80)
            {
                if (c < 0xE0)
                    c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
                else if ((p[1] & 0xC0) == 0x80)
                {
                    c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
                    p += 2;
                }
            }
            strings.push_back((unsigned short)c);
        }
        strings.push_back(0);
        table.push_back(e);
        added++;
    }
    if (added == 0)
        return;
    std::sort(table.begin(), table.end(),
              [](const nlLocalization::StringLookup& a, const nlLocalization::StringLookup& b) { return a.hash < b.hash; });
    const size_t bytes = sizeof(LOCHeader) + table.size() * sizeof(nlLocalization::StringLookup) + strings.size() * 2;
    LOCHeader* file = (LOCHeader*)nlMalloc((unsigned long)bytes, 32, false);
    if (file == NULL)
        return;
    memcpy(file, loc->m_pFile, sizeof(LOCHeader));
    file->StringCount = (u32)table.size();
    nlLocalization::StringLookup* lookup = (nlLocalization::StringLookup*)(file + 1);
    memcpy(lookup, table.data(), table.size() * sizeof(nlLocalization::StringLookup));
    unsigned short* first = (unsigned short*)(lookup + table.size());
    memcpy(first, strings.data(), strings.size() * 2);
    // The file as loaded goes (TransitionTask frees and reloads the table between front end and
    // match the same way); this is straight after Load, before anything has looked a string up.
    LOCHeader* old = loc->m_pFile;
    loc->m_pFile = file;
    loc->m_LookupTable = lookup;
    loc->m_FirstString = first;
    nlFree(old);
    fprintf(stderr, "[guest] %d guest names + lines added to the text table (%u strings now)\n", added, (unsigned)table.size());
    fflush(stderr);
}
