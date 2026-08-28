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

int uoom_text_safe_width(void)
{
    /* r * sqrt(2), the inscribed square. Integer-only. */
    return (UOOM_PANEL_RADIUS * 46341) / 32768;
}

void uoom_text_draw_center(uint8_t *fb, int y, const char *str,
                           int scale, uint8_t px)
{
    const int w = uoom_text_width(str, scale);

    uoom_text_draw(fb, (UOOM_PANEL_W - w) / 2, y, str, scale, px);
}

/* Shared by the two block routines: walks `str` a line at a time, breaking at
 * the last space that fits. `emit` receives each line. */
static int block_lines(int wPix, const char *str, int scale,
                       void (*emit)(const char *line, int y, int scale,
                                    uint8_t px, uint8_t *fb),
                       int y, uint8_t px, uint8_t *fb)
{
    const int adv     = ADVANCE(scale);
    const int perLine = (wPix > adv) ? (wPix / adv) : 1;
    const int lineH   = (UOOM_FONT_ROWS + 2) * scale;
    char      line[64];

    while (*str != '\0') {
        int take = 0;
        int brk  = 0;
        int i;

        while (str[take] != '\0' && take < perLine
               && take < (int)sizeof(line) - 1) {
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

        if (emit != NULL && fb != NULL) {
            emit(line, y, scale, px, fb);
        }
        y += lineH;

        str += take;
        while (*str == ' ') {
            ++str;
        }
    }
    return y;
}

static void emit_center(const char *line, int y, int scale, uint8_t px,
                        uint8_t *fb)
{
    uoom_text_draw_center(fb, y, line, scale, px);
}

int uoom_text_block_center(uint8_t *fb, int y, int wPix, const char *str,
                           int scale, uint8_t px)
{
    if (wPix <= 0) {
        wPix = uoom_text_safe_width();
    }
    return block_lines(wPix, str, scale, emit_center, y, px, fb);
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

/* Everything below centres in both axes, because the panel is a circle of
 * radius UOOM_PANEL_RADIUS inside the 240x240 grid: text laid out from the
 * top-left corner starts outside the glass. Blocks are measured first, then
 * drawn from (240 - height) / 2, and every line is centred horizontally. */

#define LINE_H(scale)   ((UOOM_FONT_ROWS + 2) * (scale))

void uoom_text_boot_report(const char *wad, uint32_t heapLargest,
                           uint32_t zoneGot, uint32_t tickHz10)
{
    uint8_t *fb = uoom_present_buffer();
    const uint8_t bg   = uoom_video_pack_rgb(16, 16, 24);
    const uint8_t fg   = uoom_video_pack_rgb(255, 255, 255);
    const uint8_t dim  = uoom_video_pack_rgb(160, 140, 100);
    const uint8_t warn = uoom_video_pack_rgb(255, 80, 0);
    char zoneLine[24];
    char tickLine[24];
    const char *base = wad;
    int height;
    int y;

    /* heapLargest is no longer shown: RequestMemoryInfo goes unanswered on
     * this firmware, so the row said UNKNOWN every time and cost a line of a
     * screen that has few to spare. Still queried, and still logged if it ever
     * does answer. */
    (void)heapLargest;

    if (fb == NULL) {
        return;
    }
    fill(fb, bg);

    snprintf(zoneLine, sizeof(zoneLine), "ZONE %uK",
             (unsigned)(zoneGot / 1024u));
    if (tickHz10 != 0u) {
        snprintf(tickLine, sizeof(tickLine), "TICK %u.%u HZ",
                 (unsigned)(tickHz10 / 10u), (unsigned)(tickHz10 % 10u));
    } else {
        tickLine[0] = '\0';
    }
    if (wad != NULL) {
        const char *p;

        for (p = wad; *p != '\0'; ++p) {
            if (*p == '/') {
                base = p + 1;       /* the path is in the log; show the name */
            }
        }
    }

    height = LINE_H(4) + 6                  /* title */
           + LINE_H(2)                      /* zone */
           + (tickLine[0] != '\0' ? LINE_H(2) : 0)
           + (wad != NULL ? LINE_H(2) : 0)
           + 6 + LINE_H(1);                 /* the log hint */

    y = (UOOM_PANEL_H - height) / 2;

    uoom_text_draw_center(fb, y, "UOOM", 4, fg);
    y += LINE_H(4) + 6;

    uoom_text_draw_center(fb, y, zoneLine, 2,
                          (zoneGot < (uint32_t)UOOM_ZONE_BYTES) ? warn : dim);
    y += LINE_H(2);

    if (tickLine[0] != '\0') {
        uoom_text_draw_center(fb, y, tickLine, 2, dim);
        y += LINE_H(2);
    }
    if (wad != NULL) {
        uoom_text_draw_center(fb, y, base, 2, fg);
        y += LINE_H(2);
    }

    y += 6;
    uoom_text_draw_center(fb, y, "LOG: " UOOM_LOG_PATH, 1, dim);

    uoom_plat_present(fb);
}

void uoom_text_error_screen(const char *msg)
{
    uint8_t *fb = uoom_present_buffer();
    const uint8_t bg = uoom_video_pack_rgb(80, 0, 0);
    const uint8_t fg = uoom_video_pack_rgb(255, 255, 255);
    const int w = uoom_text_safe_width();
    int height;
    int y;

    if (fb == NULL) {
        return;
    }
    fill(fb, bg);

    /* Measure the wrapped message before drawing anything, so the whole block
     * can be centred rather than the title alone. */
    height = LINE_H(3) + 8
           + (uoom_text_block_center(NULL, 0, w, msg, 2, fg) - 0);

    y = (UOOM_PANEL_H - height) / 2;
    if (y < 4) {
        y = 4;                      /* a very long message starts at the top */
    }

    uoom_text_draw_center(fb, y, "UOOM ERROR", 3, fg);
    y += LINE_H(3) + 8;

    uoom_text_block_center(fb, y, w, msg, 2, fg);
    uoom_plat_present(fb);
}

void uoom_text_no_wad_screen(void)
{
    uint8_t *fb = uoom_present_buffer();
    const uint8_t bg  = uoom_video_pack_rgb(16, 16, 24);
    const uint8_t fg  = uoom_video_pack_rgb(255, 255, 255);
    const uint8_t dim = uoom_video_pack_rgb(160, 140, 100);
    const int w = uoom_text_safe_width();
    int height;
    int y;

    if (fb == NULL) {
        return;
    }
    fill(fb, bg);

    height = LINE_H(4) + 8
           + uoom_text_block_center(NULL, 0, w, "COPY AN IWAD TO", 2, dim)
           + 4
           + uoom_text_block_center(NULL, 0, w, UOOM_WAD_DIR "/DOOM1.WAD", 2, fg);

    y = (UOOM_PANEL_H - height) / 2;

    uoom_text_draw_center(fb, y, "NO WAD", 4, fg);
    y += LINE_H(4) + 8;
    y = uoom_text_block_center(fb, y, w, "COPY AN IWAD TO", 2, dim) + 4;
    y = uoom_text_block_center(fb, y, w, UOOM_WAD_DIR "/DOOM1.WAD", 2, fg);

    uoom_plat_present(fb);
}

