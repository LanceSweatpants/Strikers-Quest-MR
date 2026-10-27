// strikers-quest r70b: franchise mode's playoff look in the stadium (Lance's OK'd presentation:
// "round logo on the centre circle, playoff banners round the pitch"). Drawn by the game's own renderer
// (GameRenderTask::Run, after the world), so it shows the same on the big screen, the Tabletop field
// and in the life-size stadium, behind the players and under the ball.
//
// The round's logo (STRIKERS_HOME/guests/portraits/<key>.png, tools/outside/franchise_data.py) is made
// into an RGBA8 texture of our own (nlMalloc'd: no match unload takes it away), once per logo.
#include "port/franchise.h"

#include "NL/nlMemory.h"
#include "NL/gl/glDraw3.h"
#include "NL/gl/glState.h"
#include "NL/glx/glxTexture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <string>
#include <unordered_map>

// Our own private copy of the PNG reader (xr_quest.cpp's is in strikers_xr, which the game's library
// can't link against).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "third_party/stb_image.h"

namespace
{

// World space: metres, Z up, the centre spot at the origin, the goals along X (half-length 20.6), the
// touchlines at Y = +-12.08; the TV camera is on the -Y side, so the far touchline (+Y) faces it.
const float kDecalHalf = 3.6f;      // the centre circle's logo, 7.2 m across
const float kDecalZ = 0.02f;        // just over the grass (the ball's shadow lies at 0.0156)
const float kBannerY = 13.2f;       // beyond the far touchline, in front of the stadium's own boards
const float kBannerSize = 1.4f;
const float kBannerX[6] = {-16.5f, -10.0f, -3.5f, 3.5f, 10.0f, 16.5f};

PlatTexture* load_logo(const std::string& key)
{
    static std::unordered_map<std::string, PlatTexture*> s_cache;
    std::unordered_map<std::string, PlatTexture*>::const_iterator it = s_cache.find(key);
    if (it != s_cache.end())
        return it->second;
    PlatTexture* tex = NULL;
    const char* home = getenv("STRIKERS_HOME");
    const std::string path = std::string(home != NULL ? home : ".") + "/guests/portraits/" + key + ".png";
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    // GX textures: a power of two each way, up to 1024
    const bool pow2 = w > 0 && h > 0 && (w & (w - 1)) == 0 && (h & (h - 1)) == 0 && w <= 1024 && h <= 1024;
    if (px != NULL && pow2)
    {
        void* mem = nlMalloc(sizeof(PlatTexture), 32, false);
        if (mem != NULL)
        {
            tex = new (mem) PlatTexture();
            tex->Create(w, h, GXTex_RGBA8, 1, true, true);
            if (tex->m_LinearData != NULL && tex->m_SwizzledData != NULL)
            {
                memcpy(tex->m_LinearData, px, (size_t)w * h * 4);
                tex->Swizzle(true);
                tex->Prepare();
            }
            else
                tex = NULL;  // (leaked: out of memory anyway)
        }
    }
    fprintf(stderr, "[franchise] pitch logo %s: %s (%dx%d)\n", key.c_str(), tex != NULL ? "ready" : "MISSING", w, h);
    fflush(stderr);
    if (px != NULL)
        stbi_image_free(px);
    s_cache[key] = tex;
    return tex;
}

void corner(glQuad3& q, int i, float x, float y, float z, float u, float v, unsigned char shade, unsigned char alpha)
{
    q.m_pos[i].x = x;
    q.m_pos[i].y = y;
    q.m_pos[i].z = z;
    q.m_uv[i].x = u;
    q.m_uv[i].y = v;
    q.m_colour[i].c[0] = shade;
    q.m_colour[i].c[1] = shade;
    q.m_colour[i].c[2] = shade;
    q.m_colour[i].c[3] = alpha;
}

void draw(glQuad3& q, PlatTexture* tex)
{
    glSetDefaultState(true);
    glSetRasterState(GLS_AlphaBlend, 1);
    glSetRasterState(GLS_Culling, 0);
    glSetRasterState(GLS_DepthWrite, 0);
    glSetCurrentRasterState(glHandleizeRasterState());
    glSetCurrentTexture((uintptr_t)tex, GLTT_Diffuse);
    glSetTextureState(GLTS_DiffuseWrap, 3);
    glSetCurrentTextureState(glHandleizeTextureState());
    q.Attach(GLV_Unshadowed, 0, true);
}

}  // namespace

void PortFranchiseRenderPitch()
{
    int round = 0, conf = 0, gameNo = 0, wins[2] = {0, 0};
    const char* key = NULL;
    if (!PortFranchisePlayoffMatch(&round, &conf, &gameNo, wins, &key) || key == NULL)
        return;
    PlatTexture* tex = load_logo(key);
    if (tex == NULL)
        return;
    // The centre circle: the logo painted on the grass, its top toward the far side (upright to the
    // TV camera), a little short of full white so it sits in the stadium's light like paint.
    {
        glQuad3 q;
        const float r = kDecalHalf;
        corner(q, 0, -r, r, kDecalZ, 0.0f, 0.0f, 225, 235);
        corner(q, 1, r, r, kDecalZ, 1.0f, 0.0f, 225, 235);
        corner(q, 2, r, -r, kDecalZ, 1.0f, 1.0f, 225, 235);
        corner(q, 3, -r, -r, kDecalZ, 0.0f, 1.0f, 225, 235);
        draw(q, tex);
    }
    // The banners standing beyond the far touchline, facing the field.
    for (int i = 0; i < 6; i++)
    {
        glQuad3 q;
        const float x0 = kBannerX[i] - 0.5f * kBannerSize, x1 = kBannerX[i] + 0.5f * kBannerSize;
        const float z0 = 0.1f, z1 = 0.1f + kBannerSize;
        corner(q, 0, x0, kBannerY, z1, 0.0f, 0.0f, 240, 255);
        corner(q, 1, x1, kBannerY, z1, 1.0f, 0.0f, 240, 255);
        corner(q, 2, x1, kBannerY, z0, 1.0f, 1.0f, 240, 255);
        corner(q, 3, x0, kBannerY, z0, 0.0f, 1.0f, 240, 255);
        draw(q, tex);
    }
}
