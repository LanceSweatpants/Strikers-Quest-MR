// Reading a GameCube disc image, so the game data can be a file somebody already has. dvd.c reads
// an extracted directory and that stays the default: it is what a release archive ships.

#ifndef PORT_DISC_H
#define PORT_DISC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortDisc PortDisc;

// Open `path` as a disc image. Returns NULL if it is not one this build can read, having written a
// finished, user-facing explanation into `err`; which names the format when it recognised one, and
// says what to do about it.
PortDisc* port_disc_open(const char* path, char* err, size_t errsize);
long port_disc_read(PortDisc* disc, void* dst, size_t len,
                    unsigned long long offset);

// "raw", "CISO" or "GCZ", for the startup line. Never NULL.
const char* port_disc_format(const PortDisc* disc);
int port_disc_looks_like_image(const char* path);

typedef int (*PortDiscVisit)(void* user, const char* path, unsigned offset,
                             unsigned length, int isDir);

// Names are not validated: a caller that turns one into a host path has to.
int port_disc_walk(PortDisc* disc, PortDiscVisit visit, void* user, char* err,
                   size_t errsize);

// strikers-quest 1.14 (the guest converter, android/.../guests): any GameCube disc (Melee, Pokemon Colosseum
// ...) and a Wii .rvz / .wia (Mario Strikers Charged), read as its game partition's data (decrypted there). The
// game itself keeps port_disc_open (Strikers' GameCube disc only).
PortDisc* port_disc_open_any(const char* path, char* err, size_t errsize);
int port_disc_is_wii(const PortDisc* disc);
typedef int (*PortDiscVisit64)(void* user, const char* path, unsigned long long offset,
                               unsigned length, int isDir);
// As port_disc_walk with 64-bit offsets (a Wii partition's files can sit past 4 GB).
int port_disc_walk64(PortDisc* disc, PortDiscVisit64 visit, void* user, char* err,
                     size_t errsize);

// Supplied by the host when built with PORT_DISC_INFLATE_HOOK instead of STRIKERS_ZLIB.
int port_disc_inflate(unsigned char* dst, unsigned long* dstLen,
                      const unsigned char* src, unsigned long srcLen);

void port_disc_close(PortDisc* disc);

// strikers-quest: opens `path`, reads every file in its filesystem and logs counts, a hash and
// the time taken (STRIKERS_DISC_SELFTEST, src/platform/dvd.c). A format check, nothing else.
void port_disc_selftest(const char* path);

// strikers-quest r46: the image the game is reading (src/platform/dvd.c), or NULL when it reads an
// extracted folder instead.
const char* port_dvd_image_path(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_DISC_H
