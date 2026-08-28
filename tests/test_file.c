/* test_file.c -- the buffered FILE replacement the savegame path runs on
 *
 * p_saveg.c serialises a savegame with one fread/fwrite per byte and patches
 * the length with ftell, so uoom_file.c's 512-byte buffering is doing real
 * work on a path where a bug means a corrupted save rather than a crash.
 *
 * DOOM writing a save has been verified end to end on the host -- it produced
 * a 25 350 byte .dsg with a correct header. The read side is harder to reach
 * through DOOM's menus, so it is covered here directly, in the access pattern
 * p_saveg actually uses.
 *
 * The platform layer is stubbed over stdio in this file rather than linking
 * host_platform.c, which would drag in the whole harness.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uoom_plat.h"
#include "uoom_file.h"

static int gFail;

#define CHECK(cond) do {                                                    \
        if (!(cond)) {                                                      \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++gFail;                                                        \
        }                                                                   \
    } while (0)

/* ------------------------------------------------- platform stubs over stdio */

struct uoom_plat_file { FILE *fp; };

uoom_plat_file_t *uoom_plat_open(const char *path, int write)
{
    uoom_plat_file_t *f = calloc(1, sizeof(*f));

    if (f == NULL) {
        return NULL;
    }
    f->fp = fopen(path, write ? "wb" : "rb");
    if (f->fp == NULL) {
        free(f);
        return NULL;
    }
    return f;
}

int uoom_plat_pread(uoom_plat_file_t *f, uint32_t off, void *buf, uint32_t len)
{
    if (fseek(f->fp, (long)off, SEEK_SET) != 0) {
        return -1;
    }
    return (int)fread(buf, 1, len, f->fp);
}

int uoom_plat_write(uoom_plat_file_t *f, const void *buf, uint32_t len)
{
    return (int)fwrite(buf, 1, len, f->fp);
}

void uoom_plat_sync(uoom_plat_file_t *f)  { fflush(f->fp); }
void uoom_plat_close(uoom_plat_file_t *f) { fclose(f->fp); free(f); }

int uoom_plat_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

uint32_t uoom_plat_ticks_ms(void) { return 0; }
long uoom_plat_filesize(const char *p) { (void)p; return -1; }
int  uoom_plat_remove(const char *p) { return remove(p); }
int  uoom_plat_rename(const char *a, const char *b) { return rename(a, b); }
int  uoom_plat_mkdir(const char *p) { (void)p; return 1; }

/* --------------------------------------------------------------------- tests */

#define PATH   "tests/out/savetest.bin"
#define NBYTES 200000       /* bigger than a real savegame, and not a multiple
                             * of the 512-byte buffer */

static unsigned char pattern(size_t i)
{
    /* Deliberately not a constant: a buffering bug that duplicates or skips a
     * block has to change the bytes, not just their count. */
    return (unsigned char)((i * 31u + (i >> 8)) & 0xFFu);
}

static void test_byte_at_a_time(void)
{
    uoom_FILE *f;
    size_t i;
    long pos;

    printf("test_byte_at_a_time (the pattern p_saveg uses)\n");

    f = uoom_fopen(PATH, "wb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    for (i = 0; i < NBYTES; ++i) {
        unsigned char b = pattern(i);

        CHECK(uoom_fwrite(&b, 1, 1, f) == 1);
        if (gFail) {
            break;
        }
    }
    /* ftell has to track the logical position, not the buffer's */
    pos = uoom_ftell(f);
    CHECK(pos == (long)NBYTES);
    CHECK(uoom_fclose(f) == 0);

    f = uoom_fopen(PATH, "rb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    for (i = 0; i < NBYTES; ++i) {
        unsigned char b = 0;

        if (uoom_fread(&b, 1, 1, f) != 1) {
            printf("  FAIL short read at %zu\n", i);
            ++gFail;
            break;
        }
        if (b != pattern(i)) {
            printf("  FAIL byte %zu: got %02x want %02x\n", i, b, pattern(i));
            ++gFail;
            break;
        }
    }
    CHECK(uoom_ftell(f) == (long)NBYTES);

    /* and one past the end must report a short read, not invent data */
    {
        unsigned char b = 0xAA;

        CHECK(uoom_fread(&b, 1, 1, f) == 0);
        CHECK(b == 0xAA);
    }
    CHECK(uoom_fclose(f) == 0);
}

static void test_block_reads_across_boundaries(void)
{
    uoom_FILE *f;
    unsigned char buf[1500];
    size_t off = 0;

    printf("test_block_reads_across_boundaries\n");

    /* 1500 is three buffer-fills minus a bit, so every read but the first
     * starts mid-buffer -- the case a naive refill gets wrong. */
    f = uoom_fopen(PATH, "rb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    for (;;) {
        size_t got = uoom_fread(buf, 1, sizeof(buf), f);
        size_t k;

        for (k = 0; k < got; ++k) {
            if (buf[k] != pattern(off + k)) {
                printf("  FAIL byte %zu in block at %zu\n", k, off);
                ++gFail;
                uoom_fclose(f);
                return;
            }
        }
        off += got;
        if (got < sizeof(buf)) {
            break;
        }
    }
    CHECK(off == NBYTES);
    CHECK(uoom_fclose(f) == 0);
}

static void test_one_handle_at_a_time(void)
{
    uoom_FILE *a;
    uoom_FILE *b;

    printf("test_one_handle_at_a_time\n");

    /* The shim has a single static slot, which is fine because DOOM never has
     * two savegames open -- but it must refuse the second rather than hand
     * back the same one. */
    a = uoom_fopen(PATH, "rb");
    CHECK(a != NULL);
    b = uoom_fopen(PATH, "rb");
    CHECK(b == NULL);
    CHECK(uoom_fclose(a) == 0);

    /* and the slot must be reusable afterwards */
    a = uoom_fopen(PATH, "rb");
    CHECK(a != NULL);
    if (a != NULL) {
        CHECK(uoom_fclose(a) == 0);
    }
}

int main(void)
{
    printf("UOOM buffered-file tests\n");

    test_byte_at_a_time();
    test_block_reads_across_boundaries();
    test_one_handle_at_a_time();

    remove(PATH);

    if (gFail) {
        printf("\n%d check(s) FAILED\n", gFail);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
