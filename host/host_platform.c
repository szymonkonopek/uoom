/* host_platform.c -- uoom_plat for a laptop
 *
 * The point of this file is that it is boring. Every interesting decision in
 * the port lives above uoom_plat.h, so the same DOOM, the same resample, the
 * same palette collapse and the same button state machine that run on the
 * watch also run here -- against stdio and a PPM writer instead of FATFS and
 * a Sharp panel.
 *
 * That makes "does the port work" a question you can answer in two seconds on
 * a laptop, instead of a flashing cycle and a squint at a 1.4-inch screen.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

#include "uoom_plat.h"
#include "uoom_video.h"
#include "uoom_config.h"

#include "host.h"

struct uoom_plat_file {
    FILE *fp;
};

/* ---------------------------------------------------------------- storage */

static char gRoot[512] = ".";

void host_set_root(const char *dir)
{
    snprintf(gRoot, sizeof(gRoot), "%s", dir);
}

static const char *resolve(const char *path)
{
    static char buf[1024];

    if (path[0] == '/') {
        return path;
    }
    snprintf(buf, sizeof(buf), "%s/%s", gRoot, path);
    return buf;
}

uoom_plat_file_t *uoom_plat_open(const char *path, int write)
{
    uoom_plat_file_t *f = calloc(1, sizeof(*f));

    if (f == NULL) {
        return NULL;
    }
    f->fp = fopen(resolve(path), write ? "wb" : "rb");
    if (f->fp == NULL) {
        free(f);
        return NULL;
    }
    return f;
}

long uoom_plat_filesize(const char *path)
{
    FILE *fp = fopen(resolve(path), "rb");
    long n;

    if (fp == NULL) {
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fclose(fp);
    return n;
}

int uoom_plat_pread(uoom_plat_file_t *f, uint32_t offset, void *buf, uint32_t len)
{
    if (f == NULL || fseek(f->fp, (long)offset, SEEK_SET) != 0) {
        return -1;
    }
    return (int)fread(buf, 1, len, f->fp);
}

int uoom_plat_write(uoom_plat_file_t *f, const void *buf, uint32_t len)
{
    return (f == NULL) ? -1 : (int)fwrite(buf, 1, len, f->fp);
}

void uoom_plat_sync(uoom_plat_file_t *f)
{
    if (f != NULL) {
        fflush(f->fp);
    }
}

void uoom_plat_close(uoom_plat_file_t *f)
{
    if (f != NULL) {
        fclose(f->fp);
        free(f);
    }
}

int uoom_plat_exists(const char *path)
{
    FILE *fp = fopen(resolve(path), "rb");

    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

int uoom_plat_remove(const char *path)
{
    return remove(resolve(path));
}

int uoom_plat_rename(const char *oldPath, const char *newPath)
{
    char from[1024];

    snprintf(from, sizeof(from), "%s", resolve(oldPath));
    return rename(from, resolve(newPath));
}

int uoom_plat_list_dir(const char *path)
{
    DIR           *d = opendir(resolve(path));
    struct dirent *e;
    int            n = 0;

    if (d == NULL) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        printf("  %-24s %s\n", e->d_name, e->d_type == DT_DIR ? "<dir>" : "");
        ++n;
    }
    closedir(d);
    return n;
}

int uoom_plat_mkdir(const char *path)
{
    /* Really create it: DOOM puts savegames in a subdirectory and asks for it
     * to exist first, so a stub here would make the savegame path untestable
     * off-device -- which is exactly the path that most needs testing. */
    return (mkdir(resolve(path), 0777) == 0 || errno == EEXIST) ? 1 : 0;
}

/* ------------------------------------------------------------------- time */

static uint32_t gVirtualMs;      /* frame-locked clock, see host.h */
static int      gQuitRequested;  /* DOOM's "Quit Game" */

uint32_t uoom_plat_ticks_ms(void)
{
    if (host_deterministic()) {
        return gVirtualMs;
    }
    {
        struct timespec ts;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
    }
}

void host_advance_clock(uint32_t ms)
{
    gVirtualMs += ms;
}

void uoom_plat_delay_ms(uint32_t ms)
{
    if (host_deterministic()) {
        /* Never actually sleep in a scripted run: DOOM's wipe loop calls
         * I_Sleep(1) until the clock moves, so the clock has to move. */
        gVirtualMs += (ms == 0u) ? 1u : ms;
        return;
    }
    {
        struct timespec ts;

        ts.tv_sec  = ms / 1000u;
        ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
        nanosleep(&ts, NULL);
    }
}

/* ------------------------------------------------------------ frame pacing */

void uoom_plat_frame_wait(void)
{
    host_frame_begin();
}

int uoom_plat_should_quit(void)
{
    return gQuitRequested || host_should_quit();
}

void uoom_plat_request_quit(void)
{
    gQuitRequested = 1;
}

void uoom_plat_exit(void)
{
    /* The harness wants its summary, and its own exit path is the frame
     * counter, so here the flag is the exit. */
    gQuitRequested = 1;
}

/* ---------------------------------------------------------------- display */

void uoom_plat_present(const uint8_t *fb)
{
    host_present(fb);
}

void uoom_plat_keep_awake(void)
{
}

/* ------------------------------------------------------------------ input */

/* Two scripted codes the watch never sends, for reaching states a four-button
 * script cannot: 'X' ends the current level, 'Z' dumps the zone layout. The
 * E1M1 -> intermission -> E1M2 handover is where the port runs out of
 * contiguous zone, and there is no way to walk to an exit switch from a key
 * script. */
extern void G_ExitLevel(void);
extern void Z_DumpFailure(int wanted);

int uoom_plat_poll_key(uint8_t *code)
{
    if (!host_poll_key(code)) {
        return 0;
    }
    if (*code == 'X') {
        printf("host: forcing a level exit\n");
        G_ExitLevel();
        return 0;
    }
    if (*code == 'Z') {
        Z_DumpFailure(0);
        return 0;
    }
    return 1;
}

void uoom_plat_haptic(uint8_t strength)
{
    host_haptic(strength);
}

/* ----------------------------------------------------------------- output */

void uoom_plat_log(const char *msg)
{
    fputs(msg, stdout);
    fflush(stdout);   /* so a run that dies mid-frame still shows why */
}

int uoom_plat_zone_from_service(uint32_t *addr, uint32_t *size)
{
    /* One process here, and a real heap. */
    (void)addr;
    (void)size;
    return 0;
}

void uoom_plat_report_memory(uint32_t *largestFree)
{
    /* The host has a real heap and no interesting answer. */
    if (largestFree != NULL) {
        *largestFree = 0u;
    }
}

void uoom_plat_panic(const char *msg)
{
    fprintf(stderr, "\nUOOM PANIC: %s\n", msg);
    exit(2);
}
