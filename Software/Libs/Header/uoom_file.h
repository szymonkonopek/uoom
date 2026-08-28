/* uoom_file.h -- WAD access and savegame I/O without stdio
 *
 * Two jobs:
 *
 * 1. A `wad_file_class_t` for DOOM's own W_ file abstraction. That abstraction
 *    is exactly three functions wide (open / close / read-at-absolute-offset),
 *    which makes it the cleanest seam in the entire engine. We export it under
 *    the name `stdc_wad_file` so that w_file.c links against us **unmodified**
 *    -- upstream's dispatcher hardcodes that symbol, and matching it is
 *    cheaper than carrying a patch.
 *
 * 2. A minimal buffered FILE replacement, because the savegame serialiser in
 *    p_saveg.c reads and writes one byte at a time through a FILE*. Handing
 *    that straight to a FATFS-backed filesystem would be pathological.
 */
#ifndef UOOM_FILE_H
#define UOOM_FILE_H

#include <stdint.h>
#include <stddef.h>

#include "uoom_config.h"

/* ------------------------------------------------------------ IWAD lookup */

/* Find an IWAD on the watch's storage. Returns a static path string, or NULL
 * if nothing was found. Search order is in the .c file. */
const char *uoom_find_iwad(void);

/* ---------------------------------------------------------------- metrics */

typedef struct {
    uint32_t reads;         /* W_Read calls that reached storage */
    uint32_t bytes;         /* total bytes pulled from the WAD */
    uint32_t worstMs;       /* slowest single read */
    uint32_t totalMs;       /* time spent in storage reads */
} uoom_io_stats_t;

const uoom_io_stats_t *uoom_io_stats(void);
void uoom_io_stats_reset(void);

/* --------------------------------------------------------------- log file
 *
 * The kernel's logger goes to a debug UART Tx line -- the SDK's deployment doc
 * says so outright -- which means reading it needs a hardware debug adapter
 * clipped to the watch. That is not a reasonable first step for anyone.
 *
 * So everything the port prints also goes to a file on the watch's own
 * storage, which comes back over USB mass storage with no cable but the
 * charging one. Buffered, because the alternative is a filesystem call per
 * printf and DOOM prints a lot at startup.
 */
void uoom_log_open(const char *path);
void uoom_log_puts(const char *s);
void uoom_log_flush(void);
void uoom_log_close(void);

/* ------------------------------------------------- buffered file, for saves */

typedef struct uoom_FILE uoom_FILE;

/* `mode` is "rb" or "wb"; anything else is refused. */
uoom_FILE *uoom_fopen(const char *path, const char *mode);
size_t     uoom_fread(void *ptr, size_t size, size_t nmemb, uoom_FILE *f);
size_t     uoom_fwrite(const void *ptr, size_t size, size_t nmemb, uoom_FILE *f);
long       uoom_ftell(uoom_FILE *f);
int        uoom_fclose(uoom_FILE *f);

#endif /* UOOM_FILE_H */
