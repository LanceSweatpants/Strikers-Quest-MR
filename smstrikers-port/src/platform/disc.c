// See include/port/disc.h for what this is and which formats it takes.

#include "port/disc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef STRIKERS_ZLIB
#include <zlib.h>
#endif

#ifdef STRIKERS_ZSTD
#include <zstd.h>  // strikers-quest: .rvz (extern/zstd)
#endif

#if defined(STRIKERS_ZLIB) || defined(PORT_DISC_INFLATE_HOOK)
#define DISC_CAN_INFLATE 1
#else
#define DISC_CAN_INFLATE 0
#endif

// 64-bit seek. A GameCube image is 1.36 GB so a 32-bit `long` would in fact reach every byte of
// one, but only just, and being one bad image away from a silent wrap is not worth the four lines
// saved.
#ifdef _WIN32
#define disc_seek_raw(f, off) _fseeki64((f), (__int64)(off), SEEK_SET)
#else
#define disc_seek_raw(f, off) fseeko((f), (off_t)(off), SEEK_SET)
#endif

// Every seek clears the stream's flags first, and that is not tidiness.
static int disc_seek(FILE* f, unsigned long long off)
{
    clearerr(f);
    return disc_seek_raw(f, off);
}

#define DISC_RAW 0
#define DISC_CISO 1
#define DISC_GCZ 2
#define DISC_RVZ 3

struct Rvz;
static void rvz_free(struct Rvz* r);

// The disc header's own magic, big-endian at 0x1C, on every GameCube disc.
#define GC_MAGIC 0xC2339F3Du
#define WII_MAGIC 0x5D1C9EA3u
#define GCZ_MAGIC 0xB10BC001u

#define CISO_HEADER 0x8000
#define CISO_MAP (CISO_HEADER - 8)

struct PortDisc
{
    FILE* f;
    int kind;
    const char* name;

    unsigned blockSize;

    // CISO: logical block -> stored block, or -1 for a hole.
    int32_t* map;
    unsigned mapCount;

    // GCZ: one offset per block, top bit meaning "stored uncompressed".
    uint64_t* off;
    unsigned blockCount;
    uint64_t dataStart;
    uint64_t compressedSize;
    unsigned char* scratch;   // one compressed block, read before inflating

    // Decoded-block cache; see the note above cache_get.
    unsigned char** cache;
    unsigned* cacheBlock;     // which block each slot holds, ~0u for empty
    unsigned* cacheLen;       // and how many bytes of it are real
    unsigned* cacheAge;
    unsigned cacheSlots;
    unsigned cacheClock;

    // RVZ: its tables, buffers and junk generator (below).
    struct Rvz* rvz;

    // strikers-quest 1.14 (the guest converter, port_disc_open_any): may be a Wii disc. A Wii .rvz / .wia is
    // read as its game partition's data (stored decrypted there), so reads and the file walk see that
    // partition like a GameCube disc (its file table's offsets are in 4-byte units: walkShift 2).
    int allowWii;
    int wii;
    unsigned walkShift;
};

static uint32_t rd_be32(const unsigned char* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t rd_le32(const unsigned char* p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

static uint64_t rd_le64(const unsigned char* p)
{
    return (uint64_t)rd_le32(p) | ((uint64_t)rd_le32(p + 4) << 32);
}

int port_disc_looks_like_image(const char* path)
{
    static const char* const kExt[] = { ".iso", ".gcm", ".ciso", ".gcz",
                                        ".rvz", ".wia", ".nkit" };
    size_t n = strlen(path);
    size_t i;
    for (i = 0; i < sizeof kExt / sizeof kExt[0]; i++)
    {
        size_t e = strlen(kExt[i]);
        if (n > e && strcmpi(path + n - e, kExt[i]) == 0)
            return 1;
    }
    return 0;
}

const char* port_disc_format(const PortDisc* disc)
{
    return disc != NULL && disc->name != NULL ? disc->name : "?";
}

void port_disc_close(PortDisc* disc)
{
    unsigned i;
    if (disc == NULL)
        return;
    if (disc->f)
        fclose(disc->f);
    free(disc->map);
    free(disc->off);
    free(disc->scratch);
    for (i = 0; i < disc->cacheSlots; i++)
        free(disc->cache[i]);
    free(disc->cache);
    free(disc->cacheBlock);
    free(disc->cacheLen);
    free(disc->cacheAge);
    rvz_free(disc->rvz);
    free(disc);
}

static PortDisc* fail(PortDisc* d, char* err, size_t errsize, const char* text)
{
    snprintf(err, errsize, "%s", text);
    port_disc_close(d);
    return NULL;
}

static int gcz_load(PortDisc* d, unsigned block, unsigned char* out, unsigned* outLen);
static int rvz_load(PortDisc* d, unsigned group, unsigned char* out, unsigned* outLen);

static int block_load(PortDisc* d, unsigned block, unsigned char* out, unsigned* outLen)
{
    return d->kind == DISC_RVZ ? rvz_load(d, block, out, outLen) : gcz_load(d, block, out, outLen);
}

static const unsigned char* cache_get(PortDisc* d, unsigned block, unsigned* len)
{
    unsigned i, victim = 0;
    unsigned oldest = ~0u;

    for (i = 0; i < d->cacheSlots; i++)
        if (d->cacheBlock[i] == block)
        {
            d->cacheAge[i] = ++d->cacheClock;
            *len = d->cacheLen[i];
            return d->cache[i];
        }

    for (i = 0; i < d->cacheSlots; i++)
        if (d->cacheAge[i] < oldest)
        {
            oldest = d->cacheAge[i];
            victim = i;
        }

    if (d->cache[victim] == NULL)
    {
        d->cache[victim] = (unsigned char*)malloc(d->blockSize);
        if (d->cache[victim] == NULL)
            return NULL;
    }
    d->cacheBlock[victim] = ~0u;
    if (block_load(d, block, d->cache[victim], len) != 0)
        return NULL;
    d->cacheBlock[victim] = block;
    d->cacheLen[victim] = *len;
    d->cacheAge[victim] = ++d->cacheClock;
    return d->cache[victim];
}

static int cache_init(PortDisc* d)
{
    // Sixteen slots, not a setting: the 1011 reads between launch and kickoff cost 29.8 ms at one
    // slot and at sixty-four alike, because the reads run forward.
    d->cacheSlots = 16u;
    d->cache = (unsigned char**)calloc(d->cacheSlots, sizeof *d->cache);
    d->cacheBlock = (unsigned*)malloc(d->cacheSlots * sizeof *d->cacheBlock);
    d->cacheLen = (unsigned*)calloc(d->cacheSlots, sizeof *d->cacheLen);
    d->cacheAge = (unsigned*)calloc(d->cacheSlots, sizeof *d->cacheAge);
    if (d->cache == NULL || d->cacheBlock == NULL || d->cacheLen == NULL
        || d->cacheAge == NULL)
        return -1;
    memset(d->cacheBlock, 0xFF, d->cacheSlots * sizeof *d->cacheBlock);
    return 0;
}

static int gcz_load(PortDisc* d, unsigned block, unsigned char* out, unsigned* outLen)
{
    const uint64_t kStored = 1ull << 63;
    uint64_t raw, next, clen;
    int stored;

    *outLen = 0;
    if (block >= d->blockCount)
        return -1;

    stored = (d->off[block] & kStored) != 0;
    raw = d->off[block] & ~kStored;
    next = block + 1 < d->blockCount ? (d->off[block + 1] & ~kStored)
                                     : d->compressedSize;
    if (next < raw)
        return -1;
    clen = next - raw;

    if (disc_seek(d->f, d->dataStart + raw) != 0)
        return -1;

    if (stored)
    {
        // Bigger compressed than raw, so the writer kept it as it was.
        size_t want = clen < d->blockSize ? (size_t)clen : d->blockSize;
        size_t got = fread(out, 1, want, d->f);
        *outLen = (unsigned)got;
        return got == want ? 0 : -1;
    }

#if DISC_CAN_INFLATE
    if (clen > d->blockSize + (d->blockSize >> 3) + 256)
        return -1;   // a compressed block larger than zlib's worst case
    if (fread(d->scratch, 1, (size_t)clen, d->f) != (size_t)clen)
        return -1;
    {
#ifdef STRIKERS_ZLIB
        uLongf dst = d->blockSize;
        if (uncompress(out, &dst, d->scratch, (uLong)clen) != Z_OK)
            return -1;
#else
        unsigned long dst = d->blockSize;
        if (port_disc_inflate(out, &dst, d->scratch, (unsigned long)clen) != 0)
            return -1;
#endif
        *outLen = (unsigned)dst;
    }
    return 0;
#else
    (void)clen;
    return -1;
#endif
}

static PortDisc* gcz_open(PortDisc* d, const unsigned char* head, char* err,
                          size_t errsize)
{
    unsigned char* table;
    size_t tableBytes;

#if !DISC_CAN_INFLATE
    return fail(d, err, errsize,
                "That is a .gcz disc image, and this build of the game was "
                "compiled without zlib,\nso it cannot decompress one.\n\n"
                "Use a plain .iso, a .gcm or a .ciso instead.");
#else
    d->kind = DISC_GCZ;
    d->name = "GCZ";
    d->compressedSize = rd_le64(head + 8);
    d->blockSize = rd_le32(head + 24);
    d->blockCount = rd_le32(head + 28);

    if (d->blockSize < 0x400 || d->blockSize > (16u << 20) || d->blockCount == 0
        || d->blockCount > (64u << 20))
        return fail(d, err, errsize,
                    "That .gcz file's header does not make sense (block size or "
                    "block count out of\nrange). It is probably damaged; "
                    "re-create it, or use a plain .iso.");

    tableBytes = (size_t)d->blockCount * 8;
    table = (unsigned char*)malloc(tableBytes);
    d->off = (uint64_t*)malloc((size_t)d->blockCount * sizeof *d->off);
    d->scratch = (unsigned char*)malloc(d->blockSize + (d->blockSize >> 3) + 256);
    if (table == NULL || d->off == NULL || d->scratch == NULL)
    {
        free(table);
        return fail(d, err, errsize, "Out of memory reading the .gcz block table.");
    }
    if (disc_seek(d->f, 32) != 0 || fread(table, 1, tableBytes, d->f) != tableBytes)
    {
        free(table);
        return fail(d, err, errsize,
                    "That .gcz file ends inside its block table, it is "
                    "truncated.");
    }
    {
        unsigned i;
        for (i = 0; i < d->blockCount; i++)
            d->off[i] = rd_le64(table + (size_t)i * 8);
    }
    free(table);

    // Header, then the offsets, then one adler32 per block.
    d->dataStart = 32 + (uint64_t)d->blockCount * 12;

    if (cache_init(d) != 0)
        return fail(d, err, errsize, "Out of memory sizing the .gcz block cache.");
    return d;
#endif
}

static PortDisc* ciso_open(PortDisc* d, const unsigned char* head, char* err,
                           size_t errsize)
{
    unsigned char map[CISO_MAP];
    unsigned i;
    int32_t stored = 0;

    d->kind = DISC_CISO;
    d->name = "CISO";
    d->blockSize = rd_le32(head + 4);
    if (d->blockSize < 0x800 || d->blockSize > (64u << 20))
        return fail(d, err, errsize,
                    "That .ciso file's block size is not a sane one, so this is "
                    "not a GameCube CISO.\n\nThe PSP uses a different format "
                    "with the same four-letter magic. Convert the image\nto a "
                    "plain .iso.");

    if (disc_seek(d->f, 8) != 0 || fread(map, 1, sizeof map, d->f) != sizeof map)
        return fail(d, err, errsize, "That .ciso file ends inside its header.");

    d->map = (int32_t*)malloc(sizeof(int32_t) * CISO_MAP);
    if (d->map == NULL)
        return fail(d, err, errsize, "Out of memory reading the .ciso map.");
    d->mapCount = CISO_MAP;
    for (i = 0; i < CISO_MAP; i++)
    {
        // One byte per block, 1 for stored and 0 for a hole, and nothing else.
        if (map[i] > 1)
            return fail(d, err, errsize,
                        "That .ciso file's block map is not one byte per block, "
                        "so it is not a GameCube\nCISO, the PSP's format uses "
                        "the same magic. Convert the image to a plain .iso.");
        d->map[i] = map[i] ? stored++ : -1;
    }
    if (d->map[0] < 0)
        return fail(d, err, errsize,
                    "That .ciso file does not store its first block, which "
                    "holds the disc header.\nIt is not a disc image this can "
                    "read.");
    return d;
}


// ---------------------------------------------------------------------------------------------
// RVZ (strikers-quest): Dolphin's compressed format, GameCube discs only. The disc's first 0x80
// bytes sit in the file's header; the rest is "raw data" cut into groups of chunk_size bytes,
// each zstd-compressed (or stored as is), and each "packed": runs of real bytes, and runs of the
// junk the disc was padded with, kept only as the seed of the generator that made it. Layout and
// generator after Dolphin (docs/WiaAndRvz.md, DiscIO/WIABlob.cpp, DiscIO/WIACompression.cpp,
// DiscIO/LaggedFibonacciGenerator.cpp, the last one CC0). Because it follows Dolphin's code, this
// RVZ section is under Dolphin's license, GPL-2.0-or-later (../../LICENSE-GPL-2.0.txt).
// ---------------------------------------------------------------------------------------------

#define RVZ_BLOCK 0x8000u
#define LFG_K 521
#define LFG_J 32
#define LFG_SEED 17

typedef struct
{
    uint64_t offset, size;  // on the disc
    uint32_t group, groups; // its first group and how many
} RvzRaw;

typedef struct
{
    uint64_t fileOffset;
    uint32_t size;          // as stored, 0 for a group of zeroes
    uint32_t packed;        // size of the packed stream once decompressed, 0 when not packed
    uint32_t outSize;       // bytes of disc it holds
    unsigned char compressed;
    unsigned char inPart;   // 1.14: Wii partition data (hash exception lists first, 0x7C00 a sector)
} RvzGroup;

// 1.14: a Wii partition's data entry: its sectors (0x8000 on the disc, 0x7C00 of data each) and groups.
typedef struct
{
    uint32_t first, count, group, groups;
} RvzPart;

#define WII_SECTOR 0x8000u
#define WII_SECTOR_DATA 0x7C00u

#ifdef STRIKERS_LZMA_BZIP2
// src/platform/disc_codecs.c (r41).
long port_disc_unlzma(const unsigned char* props, size_t propsLen, int lzma2, const unsigned char* in,
                      size_t inLen, unsigned char* out, size_t outCap);
long port_disc_unbzip2(const unsigned char* in, size_t inLen, unsigned char* out, size_t outCap);
#endif

enum
{
    WIA_NONE = 0,
    WIA_PURGE = 1, // .wia only
    WIA_BZIP2 = 2,
    WIA_LZMA = 3,
    WIA_LZMA2 = 4,
    WIA_ZSTD = 5,  // .rvz only
};

struct Rvz
{
    unsigned char discHeader[0x80];
    uint64_t isoSize;
    uint32_t chunkSize;
    uint32_t compression;   // WIA_*
    unsigned char isWia;    // r41: .wia, the older format (8-byte group entries, no packing)
    unsigned char comprData[7]; // the LZMA properties (5 bytes) or LZMA2 dictionary byte
    unsigned comprLen;
    RvzRaw* raw;
    unsigned rawCount;
    RvzGroup* group;
    unsigned groupCount;
    unsigned char* comp;    // one group as stored
    size_t compCap;
    unsigned char* packed;  // one group's packed stream
    size_t packedCap;
#ifdef STRIKERS_ZSTD
    ZSTD_DCtx* dctx;
#endif
    uint32_t lfg[LFG_K];
    size_t lfgPos;
    RvzPart part[2];        // 1.14: the Wii game partition's two data entries (count 0: unused)
    unsigned char* plain;   // 1.14: one partition group decompressed (its exception lists, then its data)
    size_t plainCap;
};

static void rvz_free(struct Rvz* r)
{
    if (r == NULL)
        return;
    free(r->raw);
    free(r->group);
    free(r->comp);
    free(r->packed);
    free(r->plain);
#ifdef STRIKERS_ZSTD
    if (r->dctx != NULL)
        ZSTD_freeDCtx(r->dctx);
#endif
    free(r);
}

static uint64_t rd_be64(const unsigned char* p)
{
    return ((uint64_t)rd_be32(p) << 32) | rd_be32(p + 4);
}

static uint32_t bswap32u(uint32_t x)
{
    return (x >> 24) | ((x >> 8) & 0xFF00u) | ((x << 8) & 0xFF0000u) | (x << 24);
}

// The junk generator: a lagged Fibonacci generator, its output read as bytes of the word buffer
// (little-endian hosts, as Dolphin's is).
static void lfg_step(uint32_t* b)
{
    size_t i;
    for (i = 0; i < LFG_J; i++)
        b[i] ^= b[i + LFG_K - LFG_J];
    for (i = LFG_J; i < LFG_K; i++)
        b[i] ^= b[i - LFG_J];
}

static void lfg_seed(struct Rvz* r, const unsigned char* seed)
{
    uint32_t* b = r->lfg;
    size_t i;
    r->lfgPos = 0;
    for (i = 0; i < LFG_SEED; i++)
        b[i] = rd_be32(seed + i * 4);
    for (i = LFG_SEED; i < LFG_K; i++)
        b[i] = (b[i - 17] << 23) ^ (b[i - 16] >> 9) ^ b[i - 1];
    for (i = 0; i < LFG_K; i++)
        b[i] = bswap32u((b[i] & 0xFF00FFFFu) | ((b[i] >> 2) & 0x00FF0000u));
    for (i = 0; i < 4; i++)
        lfg_step(b);
}

static void lfg_skip(struct Rvz* r, size_t n)
{
    r->lfgPos += n;
    while (r->lfgPos >= LFG_K * 4)
    {
        lfg_step(r->lfg);
        r->lfgPos -= LFG_K * 4;
    }
}

static void lfg_bytes(struct Rvz* r, unsigned char* out, size_t n)
{
    while (n > 0)
    {
        size_t len = LFG_K * 4 - r->lfgPos;
        if (len > n)
            len = n;
        memcpy(out, (const unsigned char*)r->lfg + r->lfgPos, len);
        r->lfgPos += len;
        out += len;
        n -= len;
        if (r->lfgPos == LFG_K * 4)
        {
            lfg_step(r->lfg);
            r->lfgPos = 0;
        }
    }
}

// .wia's purge: runs of real bytes at given offsets, zeroes between them, a SHA-1 at the end (not
// checked here). Fills all of `out`. Bytes produced, or -1.
static long wia_unpurge(const unsigned char* in, size_t inLen, unsigned char* out, size_t outCap)
{
    size_t pos = 0, end;
    if (inLen < 20)
        return -1;
    end = inLen - 20;
    memset(out, 0, outCap);
    while (pos + 8 <= end)
    {
        const uint32_t at = rd_be32(in + pos);
        const uint32_t n = rd_be32(in + pos + 4);
        pos += 8;
        if (n > end - pos || at > outCap || n > outCap - at)
            return -1;
        memcpy(out + at, in + pos, n);
        pos += n;
    }
    return (long)outCap;
}

// Stored bytes into `out`: decompressed with the image's method, or copied when stored as is.
// Bytes produced, or -1.
static long rvz_unpack_bytes(struct Rvz* r, const unsigned char* in, size_t inLen, unsigned char* out,
                             size_t outCap, int compressed)
{
    if (!compressed || r->compression == WIA_NONE)
    {
        if (inLen > outCap)
            inLen = outCap;
        memcpy(out, in, inLen);
        return (long)inLen;
    }
    switch (r->compression)
    {
    case WIA_PURGE:
        return wia_unpurge(in, inLen, out, outCap);
#ifdef STRIKERS_LZMA_BZIP2
    case WIA_BZIP2:
        return port_disc_unbzip2(in, inLen, out, outCap);
    case WIA_LZMA:
    case WIA_LZMA2:
        return port_disc_unlzma(r->comprData, r->comprLen, r->compression == WIA_LZMA2, in, inLen, out,
                                outCap);
#endif
#ifdef STRIKERS_ZSTD
    case WIA_ZSTD:
    {
        const size_t got = ZSTD_decompressDCtx(r->dctx, out, outCap, in, inLen);
        return ZSTD_isError(got) ? -1 : (long)got;
    }
#endif
    default:
        return -1;
    }
}

static int rvz_grow(unsigned char** buf, size_t* cap, size_t want)
{
    unsigned char* p;
    if (*cap >= want)
        return 0;
    p = (unsigned char*)realloc(*buf, want);
    if (p == NULL)
        return -1;
    *buf = p;
    *cap = want;
    return 0;
}

// 1.14: past `lists` hash exception lists (a u16 count, then 22 bytes an exception) at pos.
static size_t skip_lists(const unsigned char* p, size_t len, size_t pos, unsigned lists)
{
    unsigned i;
    for (i = 0; i < lists && pos + 2 <= len; i++)
        pos += 2 + (size_t)((p[pos] << 8) | p[pos + 1]) * 22;
    return pos;
}

// 1.14: a Wii partition group, already read into r->comp (tools/outside/rvzread.py's _group): its hash
// exception lists (one a 2 MiB of the chunk; before the data when the group is stored as it is, inside the
// decompressed bytes otherwise), then the data, packed or not. Junk runs come out as zeros: no file sits in one.
static int rvz_load_part(PortDisc* d, const RvzGroup* e, unsigned char* out, unsigned* outLen)
{
    struct Rvz* r = d->rvz;
    const unsigned lists = (r->chunkSize + 0x1FFFFFu) / 0x200000u;
    const int plainLists = r->compression == WIA_NONE || !e->compressed;
    const unsigned char* data = r->comp;
    size_t len = e->size, pos = 0;

    if (plainLists)
    {
        pos = skip_lists(data, len, 0, lists);
        pos = (pos + 3) & ~(size_t)3;
        if (pos > len)
            return -1;
        data += pos;
        len -= pos;
    }
    if (e->compressed && r->compression != WIA_NONE)
    {
        // (how big it decompresses isn't stored: room for the lists at their largest and the data)
        const size_t cap = (size_t)lists * (2 + 0xFFFFu * 22) + (e->packed > e->outSize ? e->packed : e->outSize);
        long got;
        if (rvz_grow(&r->plain, &r->plainCap, cap) != 0)
            return -1;
        got = rvz_unpack_bytes(r, data, len, r->plain, cap, 1);
        if (got < 0)
            return -1;
        data = r->plain;
        len = (size_t)got;
        if (!plainLists)
        {
            pos = skip_lists(data, len, 0, lists);
            if (pos > len)
                return -1;
            data += pos;
            len -= pos;
        }
    }
    memset(out, 0, e->outSize);
    if (e->packed == 0)
        memcpy(out, data, len < e->outSize ? len : e->outSize);
    else
    {
        size_t p = 0, w = 0;
        while (p + 4 <= len && w < e->outSize)
        {
            const uint32_t s = rd_be32(data + p);
            size_t n = s & 0x7FFFFFFFu;
            p += 4;
            if (n > e->outSize - w)
                n = e->outSize - w;
            if ((s & 0x80000000u) != 0)
                p += LFG_SEED * 4;  // the junk's generator seed: zeros instead
            else
            {
                if (p + n > len)
                    return -1;
                memcpy(out + w, data + p, n);
                p += n;
            }
            w += n;
        }
    }
    *outLen = e->outSize;
    return 0;
}

static int rvz_load(PortDisc* d, unsigned g, unsigned char* out, unsigned* outLen)
{
    struct Rvz* r = d->rvz;
    const RvzGroup* e;
    const unsigned char* data;
    size_t dataLen;

    *outLen = 0;
    if (r == NULL || g >= r->groupCount)
        return -1;
    e = &r->group[g];
    if (e->size == 0)
    {
        memset(out, 0, e->outSize);
        *outLen = e->outSize;
        return 0;
    }
    if (rvz_grow(&r->comp, &r->compCap, e->size) != 0)
        return -1;
    if (disc_seek(d->f, e->fileOffset) != 0 || fread(r->comp, 1, e->size, d->f) != e->size)
        return -1;
    if (e->inPart)
        return rvz_load_part(d, e, out, outLen);
    data = r->comp;
    dataLen = e->size;

    if (e->compressed)
    {
        unsigned char* dst = out;
        size_t cap = e->outSize;
        long got;
        if (e->packed != 0)
        {
            if (rvz_grow(&r->packed, &r->packedCap, e->packed) != 0)
                return -1;
            dst = r->packed;
            cap = e->packed;
        }
        got = rvz_unpack_bytes(r, r->comp, e->size, dst, cap, 1);
        if (got < 0)
            return -1;
        data = dst;
        dataLen = (size_t)got;
    }

    if (e->packed == 0)
    {
        // A plain group: the disc bytes themselves.
        if (data != out)
            memcpy(out, data, dataLen < e->outSize ? dataLen : e->outSize);
        if (dataLen < e->outSize)
            memset(out + dataLen, 0, e->outSize - dataLen);
        *outLen = e->outSize;
        return 0;
    }

    {
        // Packed: a 32-bit big-endian size per run, top bit set for junk (then the generator's
        // seed follows), clear for that many real bytes.
        size_t pos = 0, written = 0;
        while (written < e->outSize && pos + 4 <= dataLen)
        {
            const uint32_t size = rd_be32(data + pos);
            size_t n = size & 0x7FFFFFFFu;
            pos += 4;
            if (n > e->outSize - written)
                n = e->outSize - written;
            if ((size & 0x80000000u) != 0)
            {
                if (pos + LFG_SEED * 4 > dataLen)
                    return -1;
                lfg_seed(r, data + pos);
                pos += LFG_SEED * 4;
                // Groups start on 0x8000 boundaries, so this is the disc offset's place in its block.
                lfg_skip(r, written % RVZ_BLOCK);
                lfg_bytes(r, out + written, n);
            }
            else
            {
                if (pos + n > dataLen)
                    return -1;
                memcpy(out + written, data + pos, n);
                pos += n;
            }
            written += n;
        }
        if (written < e->outSize)
            memset(out + written, 0, e->outSize - written);
        *outLen = e->outSize;
        return 0;
    }
}

// One of the two tables (raw data entries, group entries): read and unpacked into `out`.
static int rvz_table(PortDisc* d, uint64_t at, uint32_t storedSize, unsigned char* out, size_t outSize)
{
    struct Rvz* r = d->rvz;
    unsigned char* in;
    long got;
    if (outSize == 0)
        return 0;
    in = (unsigned char*)malloc(storedSize ? storedSize : 1);
    if (in == NULL)
        return -1;
    if (disc_seek(d->f, at) != 0 || fread(in, 1, storedSize, d->f) != storedSize)
    {
        free(in);
        return -1;
    }
    got = rvz_unpack_bytes(r, in, storedSize, out, outSize, r->compression != 0);
    free(in);
    return got == (long)outSize ? 0 : -1;
}

static PortDisc* rvz_open(PortDisc* d, char* err, size_t errsize)
{
#ifndef STRIKERS_ZSTD
    return fail(d, err, errsize,
                "That is a .rvz disc image, and this build of the game was compiled without zstd,\n"
                "so it cannot read one. Convert it to a plain .iso (Dolphin: Convert File Format).");
#else
    unsigned char h1[0x48];
    unsigned char h2[0xDC];
    uint32_t h2size, nRaw, rawSize, nGroups, groupSize;
    uint64_t rawAt, groupAt;
    unsigned char* table = NULL;
    struct Rvz* r;
    unsigned i, k;

    memset(h2, 0, sizeof h2);
    if (disc_seek(d->f, 0) != 0 || fread(h1, 1, sizeof h1, d->f) != sizeof h1)
        return fail(d, err, errsize, "That .rvz/.wia file ends inside its header.");
    h2size = rd_be32(h1 + 0x0C);
    if (h2size < sizeof h2 - 7 || h2size > 0x10000)
        return fail(d, err, errsize, "That .rvz/.wia file's header does not make sense; it may be damaged.");
    if (fread(h2, 1, h2size < sizeof h2 ? h2size : sizeof h2, d->f) == 0)
        return fail(d, err, errsize, "That .rvz/.wia file ends inside its header.");

    r = (struct Rvz*)calloc(1, sizeof *r);
    if (r == NULL)
        return fail(d, err, errsize, "Out of memory.");
    d->rvz = r;
    d->kind = DISC_RVZ;
    // r41: .wia is the same layout with 8-byte group entries, no packing, purge and no zstd.
    r->isWia = memcmp(h1, "WIA\x01", 4) == 0;
    d->name = r->isWia ? "WIA" : "RVZ";
    r->isoSize = rd_be64(h1 + 0x24);
    r->compression = rd_be32(h2 + 0x04);
    r->chunkSize = rd_be32(h2 + 0x0C);
    memcpy(r->discHeader, h2 + 0x10, sizeof r->discHeader);
    r->comprLen = h2[0xD4] <= sizeof r->comprData ? h2[0xD4] : (unsigned)sizeof r->comprData;
    memcpy(r->comprData, h2 + 0xD5, sizeof r->comprData);

    // 1.14: a Wii disc only for the guest converter (port_disc_open_any): Mario Strikers Charged
    d->wii = d->allowWii && rd_be32(h2 + 0x00) == 2 && rd_be32(h2 + 0x90) != 0;
    if (!d->wii && (rd_be32(h2 + 0x00) != 1 || rd_be32(h2 + 0x90) != 0))
        return fail(d, err, errsize,
                    "That .rvz/.wia holds a Wii disc. Super Mario Strikers is a GameCube game; its disc\n"
                    "has a code beginning G4Q.");
    if (d->wii && r->compression == WIA_PURGE)
        return fail(d, err, errsize,
                    "That .wia Wii image uses purge, which this can't read. Convert it to .rvz in Dolphin\n"
                    "(right-click the game, Convert File Format, RVZ).");
    {
        const uint32_t c = r->compression;
        int ok = c == WIA_NONE || c == (r->isWia ? WIA_PURGE : WIA_ZSTD);
#ifdef STRIKERS_LZMA_BZIP2
        ok = ok || c == WIA_BZIP2 || c == WIA_LZMA || c == WIA_LZMA2;
#endif
        if (!ok)
            return fail(d, err, errsize,
                        "That .rvz/.wia uses a compression this game cannot read. Convert it to a plain\n"
                        ".iso (Dolphin: right-click the game, Convert File Format).");
    }
    if (r->chunkSize < RVZ_BLOCK || r->chunkSize > (32u << 20) || r->chunkSize % RVZ_BLOCK != 0)
        return fail(d, err, errsize, "That .rvz/.wia file's block size is not one this can read.");

    r->dctx = ZSTD_createDCtx();
    if (r->dctx == NULL)
        return fail(d, err, errsize, "Out of memory.");

    nRaw = rd_be32(h2 + 0xB4);
    rawAt = rd_be64(h2 + 0xB8);
    rawSize = rd_be32(h2 + 0xC0);
    nGroups = rd_be32(h2 + 0xC4);
    groupAt = rd_be64(h2 + 0xC8);
    groupSize = rd_be32(h2 + 0xD0);
    if (nRaw == 0 || nRaw > 4096 || nGroups == 0 || nGroups > (1u << 22))
        return fail(d, err, errsize, "That .rvz file's tables do not make sense; it may be damaged.");

    // The raw data entries: where on the disc each run of groups goes.
    table = (unsigned char*)malloc((size_t)nRaw * 24);
    r->raw = (RvzRaw*)calloc(nRaw, sizeof *r->raw);
    if (table == NULL || r->raw == NULL || rvz_table(d, rawAt, rawSize, table, (size_t)nRaw * 24) != 0)
    {
        free(table);
        return fail(d, err, errsize, "That .rvz/.wia file's tables could not be read; it may be damaged.");
    }
    r->rawCount = nRaw;
    for (i = 0; i < nRaw; i++)
    {
        const unsigned char* p = table + (size_t)i * 24;
        r->raw[i].offset = rd_be64(p);
        r->raw[i].size = rd_be64(p + 8);
        r->raw[i].group = rd_be32(p + 16);
        r->raw[i].groups = rd_be32(p + 20);
    }
    free(table);

    // The group entries: where each group sits in the file, and how it is stored. .wia's have no
    // per-group flag (every group uses the image's compression) and no packed size.
    {
        const size_t entry = r->isWia ? 8 : 12;
        table = (unsigned char*)malloc((size_t)nGroups * entry);
        r->group = (RvzGroup*)calloc(nGroups, sizeof *r->group);
        if (table == NULL || r->group == NULL
            || rvz_table(d, groupAt, groupSize, table, (size_t)nGroups * entry) != 0)
        {
            free(table);
            return fail(d, err, errsize, "That .rvz/.wia file's tables could not be read; it may be damaged.");
        }
        r->groupCount = nGroups;
        for (i = 0; i < nGroups; i++)
        {
            const unsigned char* p = table + (size_t)i * entry;
            const uint32_t size = rd_be32(p + 4);
            r->group[i].fileOffset = (uint64_t)rd_be32(p) << 2;
            if (r->isWia)
            {
                r->group[i].size = size;
                r->group[i].compressed = r->compression != WIA_NONE;
                r->group[i].packed = 0;
            }
            else
            {
                r->group[i].size = size & 0x7FFFFFFFu;
                r->group[i].compressed = (size & 0x80000000u) != 0 && r->compression != WIA_NONE;
                r->group[i].packed = rd_be32(p + 8);
            }
        }
        free(table);
    }

    // How much disc each group holds: a whole chunk, less for a run's last one. A run starts on
    // a 0x8000 boundary (the bytes before its first offset come along).
    for (i = 0; i < nRaw; i++)
    {
        const uint64_t skipped = r->raw[i].offset % RVZ_BLOCK;
        const uint64_t size = r->raw[i].size + skipped;
        for (k = 0; k < r->raw[i].groups; k++)
        {
            const uint32_t g = r->raw[i].group + k;
            const uint64_t at = (uint64_t)k * r->chunkSize;
            if (g >= nGroups || at >= size)
                break;
            r->group[g].outSize = (uint32_t)(size - at < r->chunkSize ? size - at : r->chunkSize);
        }
    }

    if (d->wii)
    {
        // 1.14: the partition entries (key, then two data entries: first sector, sectors, first group, groups).
        // The game's is the biggest one; its groups hold 0x7C00 bytes of data a sector.
        const uint32_t nPart = rd_be32(h2 + 0x90), partSize = rd_be32(h2 + 0x94);
        const uint64_t partAt = rd_be64(h2 + 0x98);
        const uint32_t spc = r->chunkSize / WII_SECTOR;
        uint64_t best = 0;
        unsigned char ent[0x30];
        if (nPart == 0 || nPart > 64 || partSize < sizeof ent)
            return fail(d, err, errsize, "That Wii .rvz/.wia's partition table does not make sense.");
        for (i = 0; i < nPart; i++)
        {
            RvzPart pd[2];
            uint64_t total = 0;
            if (disc_seek(d->f, partAt + (uint64_t)i * partSize) != 0 || fread(ent, 1, sizeof ent, d->f) != sizeof ent)
                return fail(d, err, errsize, "That Wii .rvz/.wia's partition table could not be read.");
            for (k = 0; k < 2; k++)
            {
                pd[k].first = rd_be32(ent + 0x10 + 0x10 * k);
                pd[k].count = rd_be32(ent + 0x14 + 0x10 * k);
                pd[k].group = rd_be32(ent + 0x18 + 0x10 * k);
                pd[k].groups = rd_be32(ent + 0x1C + 0x10 * k);
                total += pd[k].count;
            }
            if (total > best)
            {
                best = total;
                memcpy(r->part, pd, sizeof pd);
            }
        }
        for (k = 0; k < 2; k++)
        {
            const RvzPart* p = &r->part[k];
            unsigned j;
            for (j = 0; j < p->groups; j++)
            {
                const uint32_t g = p->group + j;
                const uint64_t gsector = (uint64_t)p->first + (uint64_t)j * spc;
                uint64_t nsec;
                if (g >= nGroups || gsector >= (uint64_t)p->first + p->count)
                    break;
                nsec = (uint64_t)p->first + p->count - gsector;
                if (nsec > spc)
                    nsec = spc;
                r->group[g].outSize = (uint32_t)(nsec * WII_SECTOR_DATA);
                r->group[g].inPart = 1;
            }
        }
        d->walkShift = 2;
        d->name = r->isWia ? "WIA (Wii)" : "RVZ (Wii)";
    }

    d->blockSize = r->chunkSize;
    if (cache_init(d) != 0)
        return fail(d, err, errsize, "Out of memory sizing the .rvz block cache.");
    return d;
#endif
}

static long rvz_read(PortDisc* d, unsigned char* out, size_t len, unsigned long long offset)
{
    const struct Rvz* r = d->rvz;
    size_t done = 0;

    while (done < len)
    {
        const unsigned long long abs = offset + done;
        const RvzRaw* e = NULL;
        unsigned i;
        if (abs >= r->isoSize)
            break;
        if (abs < sizeof r->discHeader)
        {
            size_t n = sizeof r->discHeader - (size_t)abs;
            if (n > len - done)
                n = len - done;
            memcpy(out + done, r->discHeader + abs, n);
            done += n;
            continue;
        }
        for (i = 0; i < r->rawCount; i++)
            if (abs >= r->raw[i].offset && abs < r->raw[i].offset + r->raw[i].size)
            {
                e = &r->raw[i];
                break;
            }
        if (e == NULL)
        {
            // Not stored anywhere: nothing the game reads.
            memset(out + done, 0, 1);
            done += 1;
            continue;
        }
        {
            const uint64_t start = e->offset - e->offset % RVZ_BLOCK;
            const unsigned k = (unsigned)((abs - start) / r->chunkSize);
            const unsigned within = (unsigned)((abs - start) % r->chunkSize);
            const uint64_t entryEnd = e->offset + e->size;
            unsigned have = 0;
            const unsigned char* p;
            size_t want;
            if (k >= e->groups)
                break;
            p = cache_get(d, e->group + k, &have);
            if (p == NULL)
                return done > 0 ? (long)done : -1;
            if (within >= have)
                break;
            want = have - within;
            if (want > len - done)
                want = len - done;
            if (abs + want > entryEnd)
                want = (size_t)(entryEnd - abs);
            memcpy(out + done, p + within, want);
            done += want;
        }
    }
    return (long)done;
}

// 1.14: a Wii disc's game partition data at `offset` (rvzread.py's read_part): its sectors hold 0x7C00 bytes each.
static long rvz_read_part(PortDisc* d, unsigned char* out, size_t len, unsigned long long offset)
{
    const struct Rvz* r = d->rvz;
    const uint32_t spc = r->chunkSize / WII_SECTOR;
    const uint64_t base = r->part[0].first;
    size_t done = 0;

    while (done < len)
    {
        const unsigned long long off = offset + done;
        const uint64_t sector = base + off / WII_SECTOR_DATA;
        const RvzPart* p = NULL;
        unsigned k;
        for (k = 0; k < 2; k++)
            if (r->part[k].count != 0 && sector >= r->part[k].first && sector < (uint64_t)r->part[k].first + r->part[k].count)
            {
                p = &r->part[k];
                break;
            }
        if (p == NULL)
            break;
        {
            const uint32_t rel = (uint32_t)((sector - p->first) / spc);
            const uint64_t gsector = (uint64_t)p->first + (uint64_t)rel * spc;
            const unsigned long long gstart = (gsector - base) * WII_SECTOR_DATA;
            const unsigned within = (unsigned)(off - gstart);
            unsigned have = 0;
            const unsigned char* data;
            size_t want;
            if (rel >= p->groups)
                break;
            data = cache_get(d, p->group + rel, &have);
            if (data == NULL)
                return done > 0 ? (long)done : -1;
            if (within >= have)
                break;
            want = have - within;
            if (want > len - done)
                want = len - done;
            memcpy(out + done, data + within, want);
            done += want;
        }
    }
    return (long)done;
}

long port_disc_read(PortDisc* d, void* dst, size_t len, unsigned long long offset)
{
    unsigned char* out = (unsigned char*)dst;
    size_t done = 0;

    if (d == NULL || dst == NULL)
        return -1;

    if (d->kind == DISC_RVZ && d->wii)
        return rvz_read_part(d, out, len, offset);
    if (d->kind == DISC_RVZ)
        return rvz_read(d, out, len, offset);

    if (d->kind == DISC_RAW)
    {
        if (disc_seek(d->f, offset) != 0)
            return -1;
        return (long)fread(out, 1, len, d->f);
    }

    while (done < len)
    {
        unsigned long long abs = offset + done;
        unsigned block = (unsigned)(abs / d->blockSize);
        unsigned within = (unsigned)(abs % d->blockSize);
        size_t want = d->blockSize - within;
        if (want > len - done)
            want = len - done;

        if (d->kind == DISC_CISO)
        {
            if (block >= d->mapCount)
                break;
            if (d->map[block] < 0)
            {
                // A hole: padding the writer dropped, which was zeroes on the disc.
                memset(out + done, 0, want);
            }
            else
            {
                unsigned long long at = (unsigned long long)CISO_HEADER
                                        + (unsigned long long)d->map[block] * d->blockSize
                                        + within;
                size_t got;
                if (disc_seek(d->f, at) != 0)
                    return done > 0 ? (long)done : -1;
                got = fread(out + done, 1, want, d->f);
                done += got;
                if (got != want)
                    break;
                continue;
            }
        }
        else
        {
            unsigned have = 0;
            const unsigned char* p = cache_get(d, block, &have);
            if (p == NULL)
                return done > 0 ? (long)done : -1;
            if (within >= have)
                break;
            if (want > have - within)
                want = have - within;
            memcpy(out + done, p + within, want);
        }
        done += want;
    }
    return (long)done;
}

void port_disc_selftest(const char* path)
{
    char err[2048];
    unsigned char h[0x440];
    unsigned char* fst = NULL;
    unsigned char* buf = NULL;
    PortDisc* d = port_disc_open(path, err, sizeof err);
    uint32_t dolOff, fstOff, fstSize, count, i;
    unsigned long long bytes = 0;
    unsigned files = 0, failures = 0;
    uint32_t hash = 2166136261u;
    clock_t t0 = clock();

    if (d == NULL)
    {
        fprintf(stderr, "[port] disc selftest: %s: %s\n", path, err);
        return;
    }
    if (port_disc_read(d, h, sizeof h, 0) != (long)sizeof h)
    {
        fprintf(stderr, "[port] disc selftest: cannot read the disc header\n");
        port_disc_close(d);
        return;
    }
    dolOff = rd_be32(h + 0x420);
    fstOff = rd_be32(h + 0x424);
    fstSize = rd_be32(h + 0x428);
    fprintf(stderr, "[port] disc selftest: %s, %.6s \"%.40s\", dol at %08x, fst at %08x (%u bytes)\n",
            port_disc_format(d), (const char*)h, (const char*)h + 0x20, dolOff, fstOff, fstSize);
    {
        unsigned char dol[0x100];
        if (port_disc_read(d, dol, sizeof dol, dolOff) == (long)sizeof dol)
        {
            const uint32_t textAddr = rd_be32(dol + 0x48);
            fprintf(stderr, "[port] disc selftest: dol text0 at %08x (%s)\n", textAddr,
                    textAddr >= 0x80000000u && textAddr < 0x81800000u ? "sane" : "NOT SANE");
        }
    }
    if (fstSize < 12 || fstSize > (16u << 20) || (fst = (unsigned char*)malloc(fstSize)) == NULL
        || (buf = (unsigned char*)malloc(1u << 20)) == NULL
        || port_disc_read(d, fst, fstSize, fstOff) != (long)fstSize)
    {
        fprintf(stderr, "[port] disc selftest: cannot read the file table\n");
        free(fst);
        free(buf);
        port_disc_close(d);
        return;
    }
    count = rd_be32(fst + 8);
    for (i = 1; i < count && (unsigned long long)i * 12 + 12 <= fstSize; i++)
    {
        const unsigned char* e = fst + (size_t)i * 12;
        uint32_t off, len, done = 0;
        if (e[0] != 0)
            continue;  // a directory
        off = rd_be32(e + 4);
        len = rd_be32(e + 8);
        while (done < len)
        {
            const uint32_t want = len - done < (1u << 20) ? len - done : (1u << 20);
            const long got = port_disc_read(d, buf, want, (unsigned long long)off + done);
            uint32_t k;
            if (got != (long)want)
            {
                ++failures;
                break;
            }
            for (k = 0; k < want; k += 4096)
            {
                hash ^= buf[k];
                hash *= 16777619u;
            }
            done += want;
        }
        if (files < 4 && len >= 4)
        {
            unsigned char m[4];
            if (port_disc_read(d, m, 4, off) == 4)
                fprintf(stderr, "[port] disc selftest: file %u at %08x, %u bytes, starts %02x%02x%02x%02x\n",
                        i, off, len, m[0], m[1], m[2], m[3]);
        }
        ++files;
        bytes += len;
    }
    fprintf(stderr, "[port] disc selftest: %u files, %llu bytes, %u failed, sample hash %08x, %.1f s\n", files,
            bytes, failures, hash, (double)(clock() - t0) / CLOCKS_PER_SEC);
    free(fst);
    free(buf);
    port_disc_close(d);
}

// NKit is *not* a refusal, and working out why cost a wrong one first.
static void note_nkit(const unsigned char* head, size_t got)
{
    if (got >= 0x204 && memcmp(head + 0x200, "NKIT", 4) == 0)
        fprintf(stderr,
                "[port] disc: NKit-processed image, the disc's padding was "
                "removed and rebuilt.\n"
                "[port] disc: the filesystem is complete, so it is read as an "
                "ordinary image.\n");
}

static PortDisc* disc_open(const char* path, int allowWii, char* err, size_t errsize);

PortDisc* port_disc_open(const char* path, char* err, size_t errsize)
{
    return disc_open(path, 0, err, errsize);
}

PortDisc* port_disc_open_any(const char* path, char* err, size_t errsize)
{
    return disc_open(path, 1, err, errsize);
}

int port_disc_is_wii(const PortDisc* disc)
{
    return disc != NULL && disc->wii;
}

static PortDisc* disc_open(const char* path, int allowWii, char* err, size_t errsize)
{
    unsigned char head[0x220];
    size_t got;
    PortDisc* d;

    err[0] = '\0';
    d = (PortDisc*)calloc(1, sizeof *d);
    if (d == NULL)
    {
        snprintf(err, errsize, "Out of memory.");
        return NULL;
    }
    d->allowWii = allowWii;

    d->f = fopen(path, "rb");
    if (d->f == NULL)
    {
        snprintf(err, errsize,
                 "That file could not be opened:\n\n  %s\n\nCheck the path and "
                 "that the file is readable.", path);
        free(d);
        return NULL;
    }

    got = fread(head, 1, sizeof head, d->f);
    if (got < 0x20)
        return fail(d, err, errsize,
                    "That file is too small to be a disc image.");

    // strikers-quest: Dolphin's .rvz, and (r41) its older .wia, read as they are.
    if (memcmp(head, "RVZ\x01", 4) == 0 || memcmp(head, "WIA\x01", 4) == 0)
    {
        d = rvz_open(d, err, errsize);
        if (d == NULL)
            return NULL;
        if (port_disc_read(d, head, sizeof head, 0) < 0x204)
            return fail(d, err, errsize,
                        "That .rvz/.wia could not be read past its own header, it is "
                        "truncated or damaged.");
        if (d->wii ? rd_be32(head + 0x18) != WII_MAGIC : rd_be32(head + 0x1C) != GC_MAGIC)
            return fail(d, err, errsize,
                        "That .rvz/.wia decompresses to something that is not a GameCube "
                        "disc. It is either damaged or an image of something else.");
        return d;
    }

    if (rd_be32(head + 0x1C) == GC_MAGIC)
    {
        d->kind = DISC_RAW;
        d->name = "raw";
        note_nkit(head, got);
        return d;
    }

    if (rd_be32(head + 0x18) == WII_MAGIC && allowWii)
        return fail(d, err, errsize,
                    "That is a Wii disc image whose data is encrypted (.iso / .wbfs). Convert it to .rvz\n"
                    "in Dolphin (right-click the game, Convert File Format, RVZ) and use that file.");
    if (rd_be32(head + 0x18) == WII_MAGIC)
        return fail(d, err, errsize,
                    "That is a Wii disc image. Super Mario Strikers is a "
                    "GameCube game, the disc\nthis needs has a code beginning "
                    "G4Q.");

    if (memcmp(head, "CISO", 4) == 0)
        d = ciso_open(d, head, err, errsize);
    else if (rd_le32(head) == GCZ_MAGIC)
        d = gcz_open(d, head, err, errsize);
    else
        return fail(d, err, errsize,
                    "That file is not a disc image this port recognises.\n\n"
                    "It reads .iso and .gcm (a plain disc image, NKit-processed "
                    "or not), .ciso and\n.gcz. It names .rvz and .wia and says "
                    "to convert them; anything else has no\nheader it knows.");

    if (d == NULL)
        return NULL;

    // A container this build can read, holding something that is not this console's disc.
    if (port_disc_read(d, head, sizeof head, 0) < 0x204)
        return fail(d, err, errsize,
                    "That image could not be read past its own header, it is "
                    "truncated or damaged.");
    if (rd_be32(head + 0x1C) != GC_MAGIC)
        return fail(d, err, errsize,
                    "That image decompresses to something that is not a "
                    "GameCube disc: the disc\nmagic is missing from its header. "
                    "It is either damaged or an image of\nsomething else.");
    note_nkit(head, sizeof head);
    return d;
}

static unsigned be32u(const unsigned char* p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

// Depth of the disc's directory tree. This game's is three deep; sixteen is there so a malformed
// FST cannot walk off the stack rather than because any disc needs it.
#define DISC_FST_DEPTH 16

static int walk_fail(unsigned char* fst, char* err, size_t errsize, const char* text)
{
    free(fst);
    snprintf(err, errsize, "%s", text);
    return -1;
}

// The 32-bit walk over the 64-bit one (a GameCube disc's offsets fit; a Wii partition's may not).
struct Walk32
{
    PortDiscVisit visit;
    void* user;
};

static int walk32_visit(void* user, const char* path, unsigned long long offset, unsigned length, int isDir)
{
    const struct Walk32* w = (const struct Walk32*)user;
    return w->visit(w->user, path, (unsigned)offset, length, isDir);
}

int port_disc_walk(PortDisc* d, PortDiscVisit visit, void* user, char* err, size_t errsize)
{
    struct Walk32 w;
    w.visit = visit;
    w.user = user;
    return port_disc_walk64(d, walk32_visit, &w, err, errsize);
}

int port_disc_walk64(PortDisc* d, PortDiscVisit64 visit, void* user, char* err, size_t errsize)
{
    unsigned char hdr[0x440];
    unsigned char* fst = NULL;
    unsigned long long fstOff;
    unsigned fstSize, nent, strOff, i;
    const unsigned shift = d != NULL ? d->walkShift : 0;  // 1.14: a Wii partition's table counts in 4 bytes
    char dir[1024];
    char full[1024];
    unsigned endStack[DISC_FST_DEPTH];
    int lenStack[DISC_FST_DEPTH];
    int sp = 0, dirLen = 0;

    err[0] = '\0';
    if (port_disc_read(d, hdr, sizeof hdr, 0) != (long)sizeof hdr)
        return walk_fail(fst, err, errsize, "That image is too short to hold a disc header.");

    fstOff = (unsigned long long)be32u(hdr + 0x424) << shift;
    fstSize = be32u(hdr + 0x428) << shift;
    if (fstSize < 12 || fstSize > (16u << 20))
        return walk_fail(fst, err, errsize,
                         "That image's file table is not a sane size, so it is "
                         "damaged or is not a\nGameCube disc.");

    fst = (unsigned char*)malloc(fstSize);
    if (fst == NULL)
        return walk_fail(fst, err, errsize, "Out of memory reading that image's file table.");
    if (port_disc_read(d, fst, fstSize, fstOff) != (long)fstSize)
        return walk_fail(fst, err, errsize,
                         "That image ends inside its file table: it is truncated. "
                         "A part-downloaded\nimage looks exactly like this.");

    // The entry count is read off the disc, so it is checked before it is multiplied: nent * 12 on
    // a damaged image can wrap a 32-bit product back to a small number and walk straight past the
    // bounds test below it.
    nent = be32u(fst + 8);
    if (nent == 0 || nent > fstSize / 12)
        return walk_fail(fst, err, errsize,
                         "That image's file table claims more entries than it "
                         "contains. It is damaged.");
    strOff = nent * 12;
    if (strOff >= fstSize)
        return walk_fail(fst, err, errsize,
                         "That image's file table claims more entries than it "
                         "contains. It is damaged.");

    endStack[0] = nent;
    lenStack[0] = 0;
    dir[0] = '\0';

    for (i = 1; i < nent; i++)
    {
        const unsigned char* e = fst + (size_t)i * 12;
        unsigned nameOff = be32u(e) & 0xFFFFFF;
        const char* name;
        unsigned limit;

        while (sp > 0 && i >= endStack[sp])
        {
            dirLen = lenStack[sp];
            dir[dirLen] = '\0';
            sp--;
        }

        if (strOff + nameOff >= fstSize)
            continue;   // a name outside the table; skip it rather than read it
        name = (const char*)fst + strOff + nameOff;
        for (limit = strOff + nameOff; limit < fstSize && fst[limit]; limit++)
            ;
        if (limit >= fstSize)
            continue;   // unterminated name at the end of the table

        if (e[0] & 1)
        {
            if (sp + 1 < DISC_FST_DEPTH)
            {
                sp++;
                endStack[sp] = be32u(e + 8);
                lenStack[sp] = dirLen;
                dirLen += snprintf(dir + dirLen, sizeof dir - (size_t)dirLen,
                                   "%s%s", dirLen ? "/" : "", name);
                if (dirLen >= (int)sizeof dir)
                    return walk_fail(fst, err, errsize,
                                     "That image has a path too long to be this game's disc.");
                if (visit(user, dir, 0, 0, 1) != 0)
                    break;
            }
            continue;
        }

        snprintf(full, sizeof full, "%s%s%s", dir, dirLen ? "/" : "", name);
        if (visit(user, full, (unsigned long long)be32u(e + 4) << shift, be32u(e + 8), 0) != 0)
            break;
    }

    free(fst);
    return 0;
}
