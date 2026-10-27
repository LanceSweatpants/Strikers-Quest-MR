// strikers-quest 1.14 (the guest converter, android/.../guests/DiscSource.java): the game's own disc reader
// (disc.c: .iso / .gcm / .ciso / .gcz / .rvz / .wia, NKit; Wii .rvz / .wia read as their game partition) over
// JNI, in a small library of its own (libguestdisc.so) because the launcher never loads the game's.

#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/disc.h"

typedef struct
{
    char* path;
    unsigned long long offset;
    unsigned length;
} Entry;

typedef struct
{
    PortDisc* disc;
    Entry* e;
    size_t n, cap;
} Handle;

static int visit(void* user, const char* path, unsigned long long offset, unsigned length, int isDir)
{
    Handle* h = (Handle*)user;
    if (isDir)
        return 0;
    if (h->n == h->cap)
    {
        const size_t cap = h->cap ? h->cap * 2 : 1024;
        Entry* e = (Entry*)realloc(h->e, cap * sizeof *e);
        if (e == NULL)
            return 1;
        h->e = e;
        h->cap = cap;
    }
    h->e[h->n].path = strdup(path);
    if (h->e[h->n].path == NULL)
        return 1;
    h->e[h->n].offset = offset;
    h->e[h->n].length = length;
    h->n++;
    return 0;
}

static void handle_free(Handle* h)
{
    size_t i;
    if (h == NULL)
        return;
    for (i = 0; i < h->n; i++)
        free(h->e[i].path);
    free(h->e);
    port_disc_close(h->disc);
    free(h);
}

// The image opened and its file table read; 0 with the reason in err[0].
JNIEXPORT jlong JNICALL Java_io_github_newcoke_strikers_guests_DiscSource_nOpen(JNIEnv* env, jclass cls, jstring jpath,
                                                                                jobjectArray err)
{
    char msg[2048];
    const char* path = (*env)->GetStringUTFChars(env, jpath, NULL);
    Handle* h = (Handle*)calloc(1, sizeof *h);
    (void)cls;
    msg[0] = '\0';
    if (h != NULL && path != NULL)
    {
        h->disc = port_disc_open_any(path, msg, sizeof msg);
        if (h->disc != NULL && port_disc_walk64(h->disc, visit, h, msg, sizeof msg) != 0)
        {
            handle_free(h);
            h = NULL;
        }
        else if (h->disc == NULL)
        {
            free(h);
            h = NULL;
        }
    }
    else
    {
        snprintf(msg, sizeof msg, "Out of memory.");
        free(h);
        h = NULL;
    }
    if (path != NULL)
        (*env)->ReleaseStringUTFChars(env, jpath, path);
    if (h == NULL && err != NULL && (*env)->GetArrayLength(env, err) > 0)
        (*env)->SetObjectArrayElement(env, err, 0, (*env)->NewStringUTF(env, msg));
    return (jlong)(intptr_t)h;
}

// The first 6 bytes of the disc (its game ID); a Wii disc's from its game partition.
JNIEXPORT jstring JNICALL Java_io_github_newcoke_strikers_guests_DiscSource_nId(JNIEnv* env, jclass cls, jlong jh)
{
    Handle* h = (Handle*)(intptr_t)jh;
    char id[7];
    (void)cls;
    memset(id, 0, sizeof id);
    if (h == NULL || port_disc_read(h->disc, id, 6, 0) != 6)
        return (*env)->NewStringUTF(env, "");
    return (*env)->NewStringUTF(env, id);
}

JNIEXPORT jobjectArray JNICALL Java_io_github_newcoke_strikers_guests_DiscSource_nPaths(JNIEnv* env, jclass cls, jlong jh)
{
    Handle* h = (Handle*)(intptr_t)jh;
    jclass str = (*env)->FindClass(env, "java/lang/String");
    jobjectArray out;
    size_t i;
    (void)cls;
    out = (*env)->NewObjectArray(env, h != NULL ? (jsize)h->n : 0, str, NULL);
    for (i = 0; h != NULL && i < h->n; i++)
    {
        jstring s = (*env)->NewStringUTF(env, h->e[i].path);
        (*env)->SetObjectArrayElement(env, out, (jsize)i, s);
        (*env)->DeleteLocalRef(env, s);
    }
    return out;
}

// File `index` (in nPaths' order) whole, or null when it can't be read.
JNIEXPORT jbyteArray JNICALL Java_io_github_newcoke_strikers_guests_DiscSource_nRead(JNIEnv* env, jclass cls, jlong jh,
                                                                                     jint index)
{
    Handle* h = (Handle*)(intptr_t)jh;
    jbyteArray out;
    unsigned char* buf;
    (void)cls;
    if (h == NULL || index < 0 || (size_t)index >= h->n)
        return NULL;
    buf = (unsigned char*)malloc(h->e[index].length ? h->e[index].length : 1);
    if (buf == NULL)
        return NULL;
    if (port_disc_read(h->disc, buf, h->e[index].length, h->e[index].offset) != (long)h->e[index].length)
    {
        free(buf);
        return NULL;
    }
    out = (*env)->NewByteArray(env, (jsize)h->e[index].length);
    if (out != NULL)
        (*env)->SetByteArrayRegion(env, out, 0, (jsize)h->e[index].length, (const jbyte*)buf);
    free(buf);
    return out;
}

JNIEXPORT void JNICALL Java_io_github_newcoke_strikers_guests_DiscSource_nClose(JNIEnv* env, jclass cls, jlong jh)
{
    (void)env;
    (void)cls;
    handle_free((Handle*)(intptr_t)jh);
}
