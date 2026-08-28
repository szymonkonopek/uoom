/* uoom_video.c -- palette collapse + resample, see docs/04-video.md */

#include <stddef.h>

#include "uoom_video.h"

/* ----------------------------------------------------------- panel buffer */

static uint8_t sPanel[UOOM_PANEL_BYTES];

uint8_t *uoom_present_buffer(void)
{
    return sPanel;
}

/* ---------------------------------------------------------------- palette --
 *
 * The panel gives us 2 bits per channel: 4 levels, 64 colours. DOOM's palette
 * has 256, and more to the point it has *gradients* -- the light diminishing
 * ramps that make a corridor read as a corridor. Snapping each channel to the
 * nearest of 4 levels turns those ramps into hard bands.
 *
 * So we keep four palette tables instead of one, each biased by a different
 * 2x2 ordered-dither threshold, and select between them by pixel position.
 * A gradient then alternates between the two nearest levels in a fixed
 * checker, which at this pixel density reads as an intermediate shade.
 *
 * Cost: 1KB of table and one extra pointer in the inner loop.
 */

#define NLEVELS 4u
#define LSTEP   85u   /* 255 / (NLEVELS-1) */

#if UOOM_DITHER
/* 2x2 Bayer matrix, in dither-phase order: (y&1)*2 + (x&1) -> matrix value
 * [[0,2],[3,1]]  ->  phase 0=0, 1=2, 2=3, 3=1 */
static const uint8_t kBayer2x2[4] = { 0u, 2u, 3u, 1u };

/* Sub-step bias for each Bayer value: (2*b+1) * 255 / (2*NLEVELS) */
static const uint16_t kBias[4] = { 32u, 96u, 159u, 223u };
#endif

/* palLut[phase][index] -- ABGR2222 pixel */
static uint8_t palLut[4][256];

static inline uint8_t quant_channel(uint8_t v, uint16_t bias)
{
    unsigned q = ((unsigned)v * (NLEVELS - 1u) + bias) / 255u;
    if (q > NLEVELS - 1u) {
        q = NLEVELS - 1u;
    }
    return (uint8_t)q;
}

static inline uint8_t pack_levels(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint8_t)((UOOM_PIX_OPAQUE_A << UOOM_PIX_A_SHIFT)
                   | ((unsigned)b << UOOM_PIX_B_SHIFT)
                   | ((unsigned)g << UOOM_PIX_G_SHIFT)
                   | ((unsigned)r << UOOM_PIX_R_SHIFT));
}

void uoom_video_set_palette(const uint8_t *pal768)
{
    int phase;
    int i;

    for (phase = 0; phase < 4; ++phase) {
#if UOOM_DITHER
        const uint16_t bias = kBias[kBayer2x2[phase]];
#else
        const uint16_t bias = 127u;   /* plain round-to-nearest */
#endif
        for (i = 0; i < 256; ++i) {
            const uint8_t *c = &pal768[i * 3];
            palLut[phase][i] = pack_levels(quant_channel(c[0], bias),
                                           quant_channel(c[1], bias),
                                           quant_channel(c[2], bias));
        }
    }
}

uint8_t uoom_video_pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return pack_levels(quant_channel(r, 127u),
                       quant_channel(g, 127u),
                       quant_channel(b, 127u));
}

void uoom_video_unpack_rgb(uint8_t px, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)(((px >> UOOM_PIX_R_SHIFT) & 3u) * LSTEP);
    *g = (uint8_t)(((px >> UOOM_PIX_G_SHIFT) & 3u) * LSTEP);
    *b = (uint8_t)(((px >> UOOM_PIX_B_SHIFT) & 3u) * LSTEP);
}

/* --------------------------------------------------------------- resample --
 *
 * Nearest neighbour through precomputed index tables. Bilinear would look
 * better on paper, but four palette lookups and a blend per pixel is 4x the
 * work, and you cannot interpolate *indices* -- you would have to interpolate
 * in RGB and then re-quantise to 2 bits, which the dither already handles.
 */

#if UOOM_RENDER_MODE == UOOM_RENDER_SCALED

static uint16_t xMap[UOOM_PANEL_W];
static uint16_t yMap[UOOM_PANEL_H];
static int      yFirst;             /* first panel row that has image */
static int      yLast;              /* one past the last */
static int      xFirst;             /* first panel column that has image */
static int      xLast;              /* one past the last */

void uoom_video_init(void)
{
    int i;

    xFirst = 0;
    xLast  = UOOM_PANEL_W;
    for (i = 0; i < UOOM_PANEL_W; ++i) {
        xMap[i] = (uint16_t)(((uint32_t)i * UOOM_DOOM_W) / UOOM_PANEL_W);
    }

#if UOOM_SCALE_MODE == UOOM_SCALE_FILL
    yFirst = 0;
    yLast  = UOOM_PANEL_H;
    for (i = 0; i < UOOM_PANEL_H; ++i) {
        yMap[i] = (uint16_t)(((uint32_t)i * UOOM_DOOM_H) / UOOM_PANEL_H);
    }
#elif UOOM_SCALE_MODE == UOOM_SCALE_INSCRIBED
    /* The largest square that fits inside the visible circle has side
     * 2r/sqrt(2) = r*sqrt(2). Integer-only: 169 for r=120. */
    {
        const int side = (UOOM_PANEL_RADIUS * 46341) / 32768;   /* r * sqrt(2) */
        const int x0   = (UOOM_PANEL_W - side) / 2;

        yFirst = (UOOM_PANEL_H - side) / 2;
        yLast  = yFirst + side;

        for (i = 0; i < UOOM_PANEL_W; ++i) {
            int sx = i - x0;

            if (sx < 0)     { sx = 0; }
            if (sx >= side) { sx = side - 1; }
            xMap[i] = (uint16_t)(((uint32_t)sx * UOOM_DOOM_W) / side);
        }
        xFirst = x0;
        xLast  = x0 + side;

        for (i = 0; i < UOOM_PANEL_H; ++i) {
            int sy = i - yFirst;

            if (sy < 0)     { sy = 0; }
            if (sy >= side) { sy = side - 1; }
            yMap[i] = (uint16_t)(((uint32_t)sy * UOOM_DOOM_H) / side);
        }
    }
#else
    /* 320 -> 240 is x0.75; apply the same factor vertically so geometry is
     * untouched: 200 * 0.75 = 150 rows, centred in 240. */
    {
        const int h = (UOOM_DOOM_H * UOOM_PANEL_W) / UOOM_DOOM_W;   /* 150 */
        yFirst = (UOOM_PANEL_H - h) / 2;
        yLast  = yFirst + h;
        for (i = 0; i < UOOM_PANEL_H; ++i) {
            int s = i - yFirst;
            if (s < 0)  { s = 0; }
            if (s >= h) { s = h - 1; }
            yMap[i] = (uint16_t)(((uint32_t)s * UOOM_DOOM_H) / h);
        }
    }
#endif
}

void uoom_video_blit(const uint8_t *src, uint8_t *dst)
{
    const uint8_t blank = uoom_video_pack_rgb(0, 0, 0);
    int y;

    for (y = 0; y < UOOM_PANEL_H; ++y) {
        uint8_t *drow = dst + (size_t)y * UOOM_PANEL_PITCH;
        int x;

        if (y < yFirst || y >= yLast) {
            for (x = 0; x < UOOM_PANEL_W; ++x) {
                drow[x] = blank;
            }
            continue;
        }

        {
            const uint8_t *srow = src + (size_t)yMap[y] * UOOM_DOOM_W;
            const uint8_t *lutA = palLut[(y & 1) * 2 + 0];
            const uint8_t *lutB = palLut[(y & 1) * 2 + 1];

            for (x = 0; x < xFirst; ++x) {
                drow[x] = blank;
            }
            /* unrolled by two so the dither phase is a compile-time choice
             * of pointer instead of a per-pixel branch */
            for (; x + 1 < xLast; x += 2) {
                drow[x]     = lutA[srow[xMap[x]]];
                drow[x + 1] = lutB[srow[xMap[x + 1]]];
            }
            for (; x < xLast; ++x) {
                drow[x] = ((x & 1) ? lutB : lutA)[srow[xMap[x]]];
            }
            for (; x < UOOM_PANEL_W; ++x) {
                drow[x] = blank;
            }
        }
    }
}

#else  /* UOOM_RENDER_NATIVE -- 1:1, no resample */

void uoom_video_init(void)
{
}

void uoom_video_blit(const uint8_t *src, uint8_t *dst)
{
    int y;

    for (y = 0; y < UOOM_PANEL_H; ++y) {
        const uint8_t *srow = src + (size_t)y * UOOM_DOOM_W;
        uint8_t       *drow = dst + (size_t)y * UOOM_PANEL_PITCH;
        const uint8_t *lutA = palLut[(y & 1) * 2 + 0];
        const uint8_t *lutB = palLut[(y & 1) * 2 + 1];
        int x;

        for (x = 0; x + 1 < UOOM_PANEL_W; x += 2) {
            drow[x]     = lutA[srow[x]];
            drow[x + 1] = lutB[srow[x + 1]];
        }
        for (; x < UOOM_PANEL_W; ++x) {
            drow[x] = lutA[srow[x]];
        }
    }
}

#endif
