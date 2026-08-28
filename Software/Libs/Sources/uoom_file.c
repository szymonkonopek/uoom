/* uoom_file.c -- see uoom_file.h */

#include <string.h>

#include "uoom_file.h"
#include "uoom_plat.h"

#if !UOOM_SMOKE_TEST
/* DOOM headers. This is the one port file that knows about the engine's
 * internals, because it implements one of the engine's own interfaces. */
#include "doomtype.h"
#include "w_file.h"
#include "z_zone.h"
#endif

/* --------------------------------------------------------------- IWAD lookup */

const char *uoom_find_iwad(void)
{
    /* In the app's own directory on the watch (2:/Apps/UOOM/), then in a
     * "uoom" subdirectory, which is where the docs tell people to put it.
     * Freedoom last: if someone has both, they meant the real thing. */
    static const char *const kCandidates[] = {
        UOOM_WAD_DIR "/DOOM1.WAD",      /* shareware, 4MB -- the usual case */
        UOOM_WAD_DIR "/DOOM.WAD",       /* registered */
        UOOM_WAD_DIR "/DOOM2.WAD",
        UOOM_WAD_DIR "/freedoom1.wad",
        "DOOM1.WAD",
        "DOOM.WAD",
        "DOOM2.WAD",
        "freedoom1.wad",
        NULL
    };
    int i;

    for (i = 0; kCandidates[i] != NULL; ++i) {
        if (uoom_plat_exists(kCandidates[i])) {
            return kCandidates[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ metrics */

static uoom_io_stats_t sStats;

const uoom_io_stats_t *uoom_io_stats(void)
{
    return &sStats;
}

void uoom_io_stats_reset(void)
{
    memset(&sStats, 0, sizeof(sStats));
}

/* ------------------------------------------------------- DOOM's wad_file_class
 *
 * Note what is *not* here: a block cache. It would not pay for itself.
 * W_ReadLump issues exactly one W_Read per lump (w_wad.c), and DOOM already
 * caches whole lumps in the zone, so the read sizes arriving here are lump-
 * sized -- hundreds of bytes to tens of kilobytes -- not byte-sized. The cost
 * is per-call filesystem overhead, and another layer of 4KB blocks would not
 * reduce the number of calls.
 *
 * `mapped` stays NULL. If the WAD ever lived in memory-mapped flash rather
 * than on eMMC, setting it to the flash address would make every lump access a
 * zero-copy pointer and remove lump caching from the zone entirely -- by far
 * the biggest win available to this port. eMMC is a block device, so it is not
 * available here; see docs/03-memory-budget.md.
 */

#if !UOOM_SMOKE_TEST

typedef struct {
    wad_file_t         wad;
    uoom_plat_file_t  *handle;
} uoom_wad_file_t;

extern wad_file_class_t stdc_wad_file;

static wad_file_t *UoomOpenFile(char *path)
{
    uoom_wad_file_t *file;
    uoom_plat_file_t *handle;
    long size;

    size = uoom_plat_filesize(path);
    if (size <= 0) {
        return NULL;
    }

    handle = uoom_plat_open(path, 0);
    if (handle == NULL) {
        return NULL;
    }

    file = Z_Malloc(sizeof(uoom_wad_file_t), PU_STATIC, 0);
    file->wad.file_class = &stdc_wad_file;
    file->wad.mapped     = NULL;
    file->wad.length     = (unsigned int)size;
    file->handle         = handle;

    return &file->wad;
}

static void UoomCloseFile(wad_file_t *wad)
{
    uoom_wad_file_t *file = (uoom_wad_file_t *)wad;

    uoom_plat_close(file->handle);
    Z_Free(file);
}

static size_t UoomRead(wad_file_t *wad, unsigned int offset,
                       void *buffer, size_t buffer_len)
{
    uoom_wad_file_t *file = (uoom_wad_file_t *)wad;
    uint32_t t0;
    uint32_t dt;
    int got;

    t0 = uoom_plat_ticks_ms();
    got = uoom_plat_pread(file->handle, offset, buffer, (uint32_t)buffer_len);
    dt = uoom_plat_ticks_ms() - t0;

    ++sStats.reads;
    sStats.totalMs += dt;
    if (dt > sStats.worstMs) {
        sStats.worstMs = dt;
    }
    if (got > 0) {
        sStats.bytes += (uint32_t)got;
    }

    return (got < 0) ? 0 : (size_t)got;
}

wad_file_class_t stdc_wad_file = {
    UoomOpenFile,
    UoomCloseFile,
    UoomRead,
};

#endif /* !UOOM_SMOKE_TEST */

/* ------------------------------------------------------------------ log file
 *
 * Its own buffer and handle rather than the uoom_FILE slot below: the log is
 * open for the whole session, and sharing that single slot would mean the log
 * blocks savegames.
 */

#define UOOM_LOGBUF 512

static uoom_plat_file_t *sLogFile;
static char              sLogBuf[UOOM_LOGBUF];
static unsigned          sLogLen;
static uint8_t           sLogDead;

void uoom_log_open(const char *path)
{
    if (sLogFile != NULL || sLogDead) {
        return;
    }
    /* Truncating, not appending: a fresh log per run is what you want when
     * diagnosing a boot, and it cannot grow without bound on a watch. */
    sLogFile = uoom_plat_open(path, 1);
    if (sLogFile == NULL) {
        sLogDead = 1;       /* no storage; stop trying */
    }
}

void uoom_log_flush(void)
{
    if (sLogFile != NULL && sLogLen != 0u) {
        uoom_plat_write(sLogFile, sLogBuf, sLogLen);
        /* And push it out of the filesystem's cache. A write that is still
         * cached when the app dies is a write that never happened, which is
         * how this port's first on-device run produced an empty log. */
        uoom_plat_sync(sLogFile);
    }
    sLogLen = 0u;
}

void uoom_log_puts(const char *s)
{
    if (sLogFile == NULL || s == NULL) {
        return;
    }
    while (*s != '\0') {
        if (sLogLen >= UOOM_LOGBUF) {
            uoom_log_flush();
        }
        sLogBuf[sLogLen++] = *s++;
    }
    /* Flush on newline as well as on a full buffer: if the next thing the port
     * does is crash, the line that explains it has to already be on disk. */
    if (sLogLen != 0u && sLogBuf[sLogLen - 1u] == '\n') {
        uoom_log_flush();
    }
}

void uoom_log_close(void)
{
    if (sLogFile != NULL) {
        uoom_log_flush();
        uoom_plat_close(sLogFile);
        sLogFile = NULL;
    }
}

/* ------------------------------------------------- buffered file for savegames
 *
 * p_saveg.c serialises a savegame with one fread/fwrite per byte and uses
 * ftell to patch the length field afterwards. 512 bytes of buffer turns
 * ~180KB of single-byte calls into ~350 filesystem operations.
 */

#define UOOM_FBUF 512

struct uoom_FILE {
    uoom_plat_file_t *handle;
    uint32_t          pos;              /* logical file position */
    uint32_t          bufBase;          /* file offset of buf[0] (read mode) */
    uint16_t          bufLen;           /* valid bytes in buf */
    uint8_t           writing;
    uint8_t           bad;
    uint8_t           buf[UOOM_FBUF];
};

/* One static instance: DOOM never has two savegames open at once, and a
 * 540-byte allocation on this budget is not worth a malloc. */
static struct uoom_FILE sSaveFile;
static int              sSaveFileBusy;

uoom_FILE *uoom_fopen(const char *path, const char *mode)
{
    int writing;

    if (path == NULL || mode == NULL) {
        return NULL;
    }
    if (mode[0] == 'w') {
        writing = 1;
    } else if (mode[0] == 'r') {
        writing = 0;
    } else {
        return NULL;                    /* no append, no update */
    }

    if (sSaveFileBusy) {
        return NULL;
    }

    memset(&sSaveFile, 0, sizeof(sSaveFile));
    sSaveFile.handle = uoom_plat_open(path, writing);
    if (sSaveFile.handle == NULL) {
        return NULL;
    }
    sSaveFile.writing = (uint8_t)writing;
    sSaveFileBusy = 1;

    return &sSaveFile;
}

static int flush_write(uoom_FILE *f)
{
    int wrote;

    if (f->bufLen == 0) {
        return 1;
    }
    wrote = uoom_plat_write(f->handle, f->buf, f->bufLen);
    if (wrote != (int)f->bufLen) {
        f->bad = 1;
        return 0;
    }
    f->bufLen = 0;
    return 1;
}

size_t uoom_fwrite(const void *ptr, size_t size, size_t nmemb, uoom_FILE *f)
{
    const uint8_t *src = (const uint8_t *)ptr;
    size_t total;
    size_t done = 0;

    if (f == NULL || !f->writing || f->bad) {
        return 0;
    }
    total = size * nmemb;

    while (done < total) {
        size_t room = UOOM_FBUF - f->bufLen;
        size_t n    = total - done;

        if (n > room) {
            n = room;
        }
        memcpy(f->buf + f->bufLen, src + done, n);
        f->bufLen = (uint16_t)(f->bufLen + n);
        f->pos   += (uint32_t)n;
        done     += n;

        if (f->bufLen == UOOM_FBUF && !flush_write(f)) {
            break;
        }
    }

    return (size == 0) ? 0 : (done / size);
}

size_t uoom_fread(void *ptr, size_t size, size_t nmemb, uoom_FILE *f)
{
    uint8_t *dst = (uint8_t *)ptr;
    size_t total;
    size_t done = 0;

    if (f == NULL || f->writing || f->bad) {
        return 0;
    }
    total = size * nmemb;

    while (done < total) {
        size_t avail;

        /* refill when the request falls outside the buffered window */
        if (f->pos < f->bufBase || f->pos >= f->bufBase + f->bufLen) {
            int got = uoom_plat_pread(f->handle, f->pos, f->buf, UOOM_FBUF);

            if (got <= 0) {
                break;
            }
            f->bufBase = f->pos;
            f->bufLen  = (uint16_t)got;
        }

        avail = (size_t)(f->bufBase + f->bufLen - f->pos);
        if (avail > total - done) {
            avail = total - done;
        }
        memcpy(dst + done, f->buf + (f->pos - f->bufBase), avail);
        f->pos += (uint32_t)avail;
        done   += avail;
    }

    return (size == 0) ? 0 : (done / size);
}

long uoom_ftell(uoom_FILE *f)
{
    return (f == NULL) ? -1L : (long)f->pos;
}

int uoom_fclose(uoom_FILE *f)
{
    int ok;

    if (f == NULL) {
        return -1;
    }
    ok = 1;
    if (f->writing) {
        ok = flush_write(f);
    }
    uoom_plat_close(f->handle);
    f->handle = NULL;
    sSaveFileBusy = 0;

    return (ok && !f->bad) ? 0 : -1;
}
