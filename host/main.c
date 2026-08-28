/* main.c -- run UOOM on a laptop
 *
 * Usage:
 *   uoom-host --wad DIR|IWAD [--frames N] [--dump DIR] [--every K]
 *             [--keys "120:e,150:d,200:q"] [--realtime]
 *
 * The default is a deterministic run: a virtual clock that ticks a fixed
 * UOOM_TARGET_FPS-worth of milliseconds per frame, so the same script yields
 * the same frames every time and a dumped PPM can be diffed. --realtime uses
 * the wall clock instead, for eyeballing the thing at speed.
 *
 * The frame dumps are the point. They are the only way to see what this port
 * actually renders without a watch on the bench, and they catch the whole
 * class of bug -- wrong pixel layout, inverted channels, off-by-one in the
 * resample -- that a unit test on the scaler cannot.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "host.h"
#include "uoom_config.h"
#include "uoom_video.h"

void uoom_run(void);                    /* doomgeneric_uoom.c */

/* DOOM's own zone accounting. The high-water mark this reports is the number
 * that decides whether the port fits on the watch at all, so the harness
 * tracks it rather than guessing from the crash messages. */
int          Z_FreeMemory(void);
unsigned int Z_ZoneSize(void);

/* Non-zero once uoom_zone_base() has run. Both functions above walk the zone's
 * block list from a pointer Z_Init sets, so calling them earlier dereferences
 * null -- which the no-IWAD path does, since DOOM never initialises there. */
extern uint32_t uoom_zone_got;

/* ------------------------------------------------------------------ options */

#define MAX_KEYS 64

typedef struct {
    uint32_t frame;
    uint8_t  code;
} scripted_key_t;

static int             gFrames = 240;
static int             gEvery;
static const char     *gDumpDir;
static int             gRealtime;
static uint32_t        gFrame;
static scripted_key_t  gKeys[MAX_KEYS];
static int             gKeyCount;
static int             gKeyNext;
static uint32_t        gPresents;
static int             gZoneMinFree = -1;
static uint32_t        gLastHash;

static uint32_t frame_hash(const uint8_t *fb);

int host_deterministic(void)
{
    return !gRealtime;
}

void host_frame_begin(void)
{
    ++gFrame;
    if (host_deterministic()) {
        host_advance_clock(1000u / UOOM_TARGET_FPS);
    }
}

int host_should_quit(void)
{
    return (gFrames > 0) && ((int)gFrame >= gFrames);
}

int host_poll_key(uint8_t *code)
{
    if (gKeyNext < gKeyCount && gKeys[gKeyNext].frame <= gFrame) {
        *code = gKeys[gKeyNext].code;
        ++gKeyNext;
        return 1;
    }
    return 0;
}

void host_haptic(uint8_t strength)
{
    (void)strength;
}

/* --------------------------------------------------------------- PPM output */

static void write_ppm(const uint8_t *fb, uint32_t index)
{
    char  path[1024];
    FILE *fp;
    int   i;

    snprintf(path, sizeof(path), "%s/frame_%05u.ppm", gDumpDir, index);
    fp = fopen(path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    fprintf(fp, "P6\n%d %d\n255\n", UOOM_PANEL_W, UOOM_PANEL_H);
    for (i = 0; i < UOOM_PANEL_BYTES; ++i) {
        uint8_t r, g, b;

        uoom_video_unpack_rgb(fb[i], &r, &g, &b);
        fputc(r, fp);
        fputc(g, fp);
        fputc(b, fp);
    }
    fclose(fp);
    printf("wrote %s\n", path);
}

/* A cheap fingerprint of the frame. Two uses: a scripted run can assert the
 * screen is changing and is not uniform, and -- more valuable -- a refactor
 * that is supposed to be behaviour-preserving can be *proven* inert by running
 * the same key script before and after and comparing the final hash. That is
 * how the RAM diet in docs/03-memory-budget.md was verified. */
static uint32_t frame_hash(const uint8_t *fb)
{
    uint32_t h = 2166136261u;
    int i;

    for (i = 0; i < UOOM_PANEL_BYTES; ++i) {
        h ^= fb[i];
        h *= 16777619u;
    }
    return h;
}

void host_present(const uint8_t *fb)
{
    ++gPresents;
    gLastHash = frame_hash(fb);

    if (uoom_zone_got != 0u) {
        int freeNow = Z_FreeMemory();

        if (gZoneMinFree < 0 || freeNow < gZoneMinFree) {
            gZoneMinFree = freeNow;
        }
    }

    if (gDumpDir != NULL && gEvery > 0 && (gPresents % (uint32_t)gEvery) == 0u) {
        write_ppm(fb, gPresents);
    }

    /* One line per present, so a run's shape is visible in the log: a frame
     * that never changes hash means the game is stuck. */
    if ((gPresents % 30u) == 0u) {
        int distinct = 0;
        int seen[256];
        int i;

        memset(seen, 0, sizeof(seen));
        for (i = 0; i < UOOM_PANEL_BYTES; ++i) {
            if (!seen[fb[i]]) {
                seen[fb[i]] = 1;
                ++distinct;
            }
        }
        if (uoom_zone_got != 0u) {
            printf("[present %5u] hash=%08x colours=%d zonefree=%dK min=%dK\n",
                   gPresents, frame_hash(fb), distinct,
                   Z_FreeMemory() / 1024, gZoneMinFree / 1024);
        } else {
            printf("[present %5u] hash=%08x colours=%d\n",
                   gPresents, frame_hash(fb), distinct);
        }
    }
}

/* ------------------------------------------------------------------- keys */

static void parse_keys(char *spec)
{
    char *save = NULL;
    char *tok;

    for (tok = strtok_r(spec, ",", &save);
         tok != NULL && gKeyCount < MAX_KEYS;
         tok = strtok_r(NULL, ",", &save)) {
        char *colon = strchr(tok, ':');

        if (colon == NULL) {
            continue;
        }
        *colon = '\0';
        gKeys[gKeyCount].frame = (uint32_t)atoi(tok);
        gKeys[gKeyCount].code  = (uint8_t)colon[1];
        ++gKeyCount;
    }
}

/* --wad wants the directory the IWAD lives in, because it becomes the port's
 * whole filesystem root -- savegames and uoom.log land there too, exactly as
 * they do on the watch. But pointing at the WAD file itself is the obvious
 * reflex, and getting it wrong renders the port's "NO WAD" screen rather than
 * an error, which reads like a bug in the port. So accept either. */
static void set_wad_root(const char *arg)
{
    struct stat st;

    if (stat(arg, &st) == 0 && !S_ISDIR(st.st_mode)) {
        char  dir[1024];
        char *slash;

        snprintf(dir, sizeof(dir), "%s", arg);
        slash = strrchr(dir, '/');
        if (slash == NULL) {
            host_set_root(".");
        } else {
            *slash = '\0';
            host_set_root(dir[0] == '\0' ? "/" : dir);
        }
        return;
    }

    host_set_root(arg);
}

int main(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--wad") && i + 1 < argc) {
            set_wad_root(argv[++i]);
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            gFrames = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            gDumpDir = argv[++i];
            if (gEvery == 0) {
                gEvery = 60;
            }
        } else if (!strcmp(argv[i], "--every") && i + 1 < argc) {
            gEvery = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--keys") && i + 1 < argc) {
            parse_keys(argv[++i]);
        } else if (!strcmp(argv[i], "--realtime")) {
            gRealtime = 1;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    printf("UOOM host harness: panel %dx%d, doom %dx%d, render mode %d, "
           "dither %d, zone %d KB\n",
           UOOM_PANEL_W, UOOM_PANEL_H, UOOM_DOOM_W, UOOM_DOOM_H,
           UOOM_RENDER_MODE, UOOM_DITHER, UOOM_ZONE_BYTES / 1024);

    uoom_run();

    printf("\n%u frames, %u presents, final hash %08x\n",
           gFrame, gPresents, gLastHash);
    if (gZoneMinFree >= 0 && uoom_zone_got != 0u) {
        printf("zone: %uK total, %dK free at the worst point -> "
               "peak use %dK\n",
               Z_ZoneSize() / 1024, gZoneMinFree / 1024,
               (int)(Z_ZoneSize() / 1024) - gZoneMinFree / 1024);
    }
    return 0;
}

