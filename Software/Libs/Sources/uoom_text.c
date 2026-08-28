/* uoom_text.c -- see uoom_text.h */

#include <stdio.h>
#include <string.h>

#include "uoom_text.h"
#include "uoom_font.h"
#include "uoom_video.h"
#include "uoom_plat.h"

#define ADVANCE(scale)  ((UOOM_FONT_COLS + 1) * (scale))

static int glyph_index(char c)
{
    const char *p;

    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    p = strchr(uoom_font_chars, c);
    return (p == NULL) ? -1 : (int)(p - uoom_font_chars);
}

int uoom_text_width(const char *str, int scale)
{
    int n = 0;

    while (*str != '\0') {
        ++n;
        ++str;
    }
    return n * ADVANCE(scale);
}

void uoom_text_draw(uint8_t *fb, int x, int y, const char *str,
                    int scale, uint8_t px)
{
    if (scale < 1) {
        scale = 1;
    }

    for (; *str != '\0'; ++str, x += ADVANCE(scale)) {
        int gi = glyph_index(*str);
        int col;

        if (gi < 0) {
            continue;
        }
        for (col = 0; col < UOOM_FONT_COLS; ++col) {
            uint8_t bits = uoom_font_cols[gi][col];
            int row;

            for (row = 0; row < UOOM_FONT_ROWS; ++row) {
                int sx;

                if ((bits & (1u << row)) == 0u) {
                    continue;
                }
                for (sx = 0; sx < scale; ++sx) {
                    int sy;

                    for (sy = 0; sy < scale; ++sy) {
                        int fx = x + col * scale + sx;
                        int fy = y + row * scale + sy;

                        if (fx >= 0 && fx < UOOM_PANEL_W
                            && fy >= 0 && fy < UOOM_PANEL_H) {
                            fb[(size_t)fy * UOOM_PANEL_PITCH + fx] = px;
                        }
                    }
                }
            }
        }
    }
}

int uoom_text_block(uint8_t *fb, int x, int y, int wPix, const char *str,
                    int scale, uint8_t px)
{
    const int adv     = ADVANCE(scale);
    const int perLine = (wPix > adv) ? (wPix / adv) : 1;
    const int lineH   = (UOOM_FONT_ROWS + 2) * scale;
    char      line[64];

    while (*str != '\0') {
        int take = 0;
        int brk  = 0;
        int i;

        /* how much fits, breaking at the last space if there is one */
        while (str[take] != '\0' && take < perLine && take < (int)sizeof(line) - 1) {
            if (str[take] == ' ') {
                brk = take;
            }
            ++take;
        }
        if (str[take] != '\0' && brk > 0) {
            take = brk;
        }

        for (i = 0; i < take; ++i) {
            line[i] = str[i];
        }
        line[take] = '\0';

        uoom_text_draw(fb, x, y, line, scale, px);
        y += lineH;

        str += take;
        while (*str == ' ') {
            ++str;
        }
    }

    return y;
}

/* ------------------------------------------------------------------ screens */

static void fill(uint8_t *fb, uint8_t px)
{
    memset(fb, px, UOOM_PANEL_BYTES);
}

void uoom_text_error_screen(const char *msg)
{
    uint8_t *fb = uoom_present_buffer();
    const uint8_t bg   = uoom_video_pack_rgb(80, 0, 0);
    const uint8_t fg   = uoom_video_pack_rgb(255, 255, 255);
    int y;

    if (fb == NULL) {
        return;
    }
    fill(fb, bg);
    uoom_text_draw(fb, 8, 12, "UOOM ERROR", 3, fg);
    y = uoom_text_block(fb, 8, 48, UOOM_PANEL_W - 16, msg, 2, fg);
    (void)y;
    uoom_plat_present(fb);
}

void uoom_text_boot_report(const char *wad, uint32_t heapLargest,
                           uint32_t zoneGot, uint32_t tickHz10)
{
    uint8_t *fb = uoom_present_buffer();
    const uint8_t bg   = uoom_video_pack_rgb(16, 16, 24);
    const uint8_t fg   = uoom_video_pack_rgb(255, 255, 255);
    const uint8_t dim  = uoom_video_pack_rgb(160, 140, 100);
    const uint8_t warn = uoom_video_pack_rgb(255, 80, 0);
    char line[40];
    int y;

    if (fb == NULL) {
        return;
    }
    fill(fb, bg);
    uoom_text_draw(fb, 10, 10, "UOOM", 4, fg);

    y = 46;

    /* The kernel's heap size is the number the SDK documents nowhere, and this
     * is the only place it becomes visible without a debug adapter. */
    if (heapLargest != 0u) {
        snprintf(line, sizeof(line), "HEAP %uK", (unsigned)(heapLargest / 1024u));
    } else {
        snprintf(line, sizeof(line), "HEAP UNKNOWN");
    }
    uoom_text_draw(fb, 10, y, line, 2, dim);
    y += 18;

    snprintf(line, sizeof(line), "ZONE %uK", (unsigned)(zoneGot / 1024u));
    uoom_text_draw(fb, 10, y, line, 2,
                   (zoneGot < (uint32_t)UOOM_ZONE_BYTES) ? warn : dim);
    y += 18;

    if (tickHz10 != 0u) {
        snprintf(line, sizeof(line), "TICK %u.%u HZ",
                 (unsigned)(tickHz10 / 10u), (unsigned)(tickHz10 % 10u));
        uoom_text_draw(fb, 10, y, line, 2, dim);
    }
    y += 18;

    if (wad != NULL) {
        /* Just the basename; the path is in uoom.log. */
        const char *base = wad;
        const char *p;

        for (p = wad; *p != '\0'; ++p) {
            if (*p == '/') {
                base = p + 1;
            }
        }
        uoom_text_draw(fb, 10, y, base, 2, fg);
    }

    y += 26;
    uoom_text_block(fb, 10, y, UOOM_PANEL_W - 20,
                    "LOG IN " UOOM_LOG_PATH " OVER USB", 1, dim);

    uoom_plat_present(fb);
}

void uoom_text_no_wad_screen(void)
{
    uint8_t *fb = uoom_present_buffer();
    const uint8_t bg = uoom_video_pack_rgb(16, 16, 24);
    const uint8_t fg = uoom_video_pack_rgb(255, 255, 255);
    const uint8_t dim = uoom_video_pack_rgb(160, 140, 100);
    int y;

    if (fb == NULL) {
        return;
    }
    fill(fb, bg);
    uoom_text_draw(fb, 10, 14, "NO WAD", 4, fg);
    y = 56;
    y = uoom_text_block(fb, 10, y, UOOM_PANEL_W - 20,
                        "COPY AN IWAD OVER USB TO", 2, dim);
    y = uoom_text_block(fb, 10, y + 4, UOOM_PANEL_W - 20,
                        "2:/APPS/UOOM/" UOOM_WAD_DIR "/DOOM1.WAD", 2, fg);
    y = uoom_text_block(fb, 10, y + 10, UOOM_PANEL_W - 20,
                        "FREEDOOM1.WAD ALSO WORKS.", 2, dim);
    uoom_plat_present(fb);
}
