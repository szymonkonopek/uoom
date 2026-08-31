/* test_port.c -- host-side tests for the platform-independent port layers.
 *
 * These are the two pieces of UOOM that carry real logic and that can be
 * verified without a watch: the palette/resample path and the button state
 * machine. Build and run with tests/run.sh.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "uoom_video.h"
#include "uoom_input.h"

static int gFail;

#define CHECK(cond) do {                                                    \
        if (!(cond)) {                                                      \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++gFail;                                                        \
        }                                                                   \
    } while (0)

/* ------------------------------------------------------------------- video */

static uint8_t gPal[768];

static void make_ramp_palette(void)
{
    int i;

    for (i = 0; i < 256; ++i) {
        gPal[i * 3 + 0] = (uint8_t)i;    /* red ramp   */
        gPal[i * 3 + 1] = (uint8_t)i;    /* green ramp */
        gPal[i * 3 + 2] = (uint8_t)i;    /* blue ramp  */
    }
}

static void test_pack_unpack(void)
{
    uint8_t r, g, b;
    uint8_t px;

    printf("test_pack_unpack\n");

    px = uoom_video_pack_rgb(0, 0, 0);
    uoom_video_unpack_rgb(px, &r, &g, &b);
    CHECK(r == 0 && g == 0 && b == 0);

    px = uoom_video_pack_rgb(255, 255, 255);
    uoom_video_unpack_rgb(px, &r, &g, &b);
    CHECK(r == 255 && g == 255 && b == 255);

    /* alpha must always read as opaque, or the compositor drops our frame */
    CHECK(((uoom_video_pack_rgb(10, 20, 30) >> UOOM_PIX_A_SHIFT) & 3u) == 3u);

    /* pure channels must not bleed into each other */
    px = uoom_video_pack_rgb(255, 0, 0);
    uoom_video_unpack_rgb(px, &r, &g, &b);
    CHECK(r == 255 && g == 0 && b == 0);
    px = uoom_video_pack_rgb(0, 255, 0);
    uoom_video_unpack_rgb(px, &r, &g, &b);
    CHECK(r == 0 && g == 255 && b == 0);
    px = uoom_video_pack_rgb(0, 0, 255);
    uoom_video_unpack_rgb(px, &r, &g, &b);
    CHECK(r == 0 && g == 0 && b == 255);
}

static void test_blit_bounds(void)
{
    static uint8_t src[UOOM_DOOM_W * UOOM_DOOM_H];
    static uint8_t dst[UOOM_PANEL_BYTES + 16];
    int i;
    int touched = 0;

    printf("test_blit_bounds\n");

    make_ramp_palette();
    uoom_video_set_palette(gPal);
    uoom_video_init();

    memset(src, 200, sizeof(src));
    memset(dst, 0xAA, sizeof(dst));

    uoom_video_blit(src, dst);

    /* nothing past the framebuffer may be touched */
    for (i = UOOM_PANEL_BYTES; i < (int)sizeof(dst); ++i) {
        CHECK(dst[i] == 0xAA);
    }
    /* and every pixel inside it must have been written */
    for (i = 0; i < UOOM_PANEL_BYTES; ++i) {
        if (dst[i] != 0xAA) {
            ++touched;
        }
    }
    CHECK(touched == UOOM_PANEL_BYTES);
}

static void test_dither_breaks_bands(void)
{
    static uint8_t src[UOOM_DOOM_W * UOOM_DOOM_H];
    static uint8_t dst[UOOM_PANEL_BYTES];
    int x, y;
    int distinct;
    unsigned char seen[256];

    printf("test_dither_breaks_bands\n");

    make_ramp_palette();
    uoom_video_set_palette(gPal);
    uoom_video_init();

    /* a horizontal grey ramp: index == column, so the whole 0..255 range */
    for (y = 0; y < UOOM_DOOM_H; ++y) {
        for (x = 0; x < UOOM_DOOM_W; ++x) {
            src[y * UOOM_DOOM_W + x] = (uint8_t)((x * 255) / (UOOM_DOOM_W - 1));
        }
    }
    uoom_video_blit(src, dst);

    memset(seen, 0, sizeof(seen));
    for (x = 0; x < UOOM_PANEL_BYTES; ++x) {
        seen[dst[x]] = 1;
    }
    distinct = 0;
    for (x = 0; x < 256; ++x) {
        distinct += seen[x];
    }
    /* 2 bits per channel means at most 4 grey levels without dithering.
     * With the 2x2 ordered dither a ramp must still only produce greys, but
     * neighbouring rows must differ -- that is what buys the extra shades. */
    CHECK(distinct >= 4);
#if UOOM_DITHER
    {
        int rowsDiffer = 0;

        for (y = 0; y + 1 < UOOM_PANEL_H; ++y) {
            if (memcmp(dst + y * UOOM_PANEL_PITCH,
                       dst + (y + 1) * UOOM_PANEL_PITCH,
                       UOOM_PANEL_W) != 0) {
                rowsDiffer = 1;
                break;
            }
        }
        CHECK(rowsDiffer == 1);
    }
#endif
}

/* ------------------------------------------------------------------- input */

/* press/release codes from the UNA kernel mapping */
#define P_L1 'q'
#define R_L1 'a'
#define P_R1 'e'
#define R_R1 'd'
#define P_L2 'w'
#define R_L2 's'
#define P_R2 'r'
#define R_R2 'f'

static int count_events(uoom_action_t want, int wantPressed)
{
    uoom_action_t act;
    int pressed;
    int n = 0;

    while (uoom_input_pop(&act, &pressed)) {
        if (act == want && pressed == wantPressed) {
            ++n;
        }
    }
    return n;
}

static void drain(void)
{
    uoom_action_t act;
    int pressed;

    while (uoom_input_pop(&act, &pressed)) {
        /* discard */
    }
}

static void test_hold_turns(void)
{
    printf("test_hold_turns\n");

    uoom_input_init();
    uoom_input_feed_code(P_L1, 1000);
    uoom_input_tick(1000, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_TURN_LEFT));
    CHECK(count_events(UOOM_ACT_TURN_LEFT, 1) == 1);

    /* still held several frames later -- no spurious re-trigger */
    uoom_input_tick(1200, UOOM_CTX_GAME);
    CHECK(count_events(UOOM_ACT_TURN_LEFT, 1) == 0);
    CHECK(uoom_input_is_down(UOOM_ACT_TURN_LEFT));

    uoom_input_feed_code(R_L1, 1400);
    uoom_input_tick(1400, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_TURN_LEFT));
    CHECK(count_events(UOOM_ACT_TURN_LEFT, 0) == 1);
}

static void test_r1_tap_is_use_hold_is_forward(void)
{
    printf("test_r1_tap_is_use_hold_is_forward\n");

    /* tap */
    uoom_input_init();
    uoom_input_feed_code(P_R1, 0);
    uoom_input_tick(0, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_FORWARD));   /* no latency on movement */
    drain();

    uoom_input_feed_code(R_R1, 80);
    uoom_input_tick(80, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_FORWARD));
    CHECK(uoom_input_is_down(UOOM_ACT_USE));       /* short press -> use */
    drain();

    /* the USE pulse must be long enough to survive a DOOM tic, then release */
    uoom_input_tick(120, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_USE));
    uoom_input_tick(400, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_USE));

    /* long hold: forward, and no USE on release */
    uoom_input_init();
    uoom_input_feed_code(P_R1, 0);
    uoom_input_tick(0, UOOM_CTX_GAME);
    uoom_input_tick(500, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_FORWARD));
    drain();
    uoom_input_feed_code(R_R1, 900);
    uoom_input_tick(900, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_FORWARD));
    CHECK(count_events(UOOM_ACT_USE, 1) == 0);
}

static void test_chord_tap_menu_hold_backward(void)
{
    printf("test_chord_tap_menu_hold_backward\n");

    /* quick both-left tap -> menu */
    uoom_input_init();
    uoom_input_feed_code(P_L1, 0);
    uoom_input_feed_code(P_L2, 20);
    uoom_input_tick(20, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_TURN_LEFT));
    CHECK(!uoom_input_is_down(UOOM_ACT_TURN_RIGHT));
    drain();
    uoom_input_feed_code(R_L1, 100);
    uoom_input_feed_code(R_L2, 110);
    uoom_input_tick(110, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_MENU));
    CHECK(!uoom_input_is_down(UOOM_ACT_BACKWARD));

    /* held both-left -> walk backward, and no menu on release */
    uoom_input_init();
    uoom_input_feed_code(P_L1, 0);
    uoom_input_feed_code(P_L2, 10);
    uoom_input_tick(10, UOOM_CTX_GAME);
    uoom_input_tick(400, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_BACKWARD));
    drain();
    uoom_input_feed_code(R_L1, 800);
    uoom_input_feed_code(R_L2, 820);
    uoom_input_tick(820, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_BACKWARD));
    CHECK(count_events(UOOM_ACT_MENU, 1) == 0);

    /* lifting one finger out of a chord must not start a turn */
    uoom_input_init();
    uoom_input_feed_code(P_L1, 0);
    uoom_input_feed_code(P_L2, 10);
    uoom_input_tick(400, UOOM_CTX_GAME);
    uoom_input_feed_code(R_L2, 500);
    uoom_input_tick(500, UOOM_CTX_GAME);
    drain();
    uoom_input_tick(600, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_TURN_LEFT));
}

static void test_r2_tap_fire_hold_weapon(void)
{
    printf("test_r2_tap_fire_hold_weapon\n");

    /* Tap: nothing on the press, one shot when the finger comes up. */
    uoom_input_init();
    uoom_input_feed_code(P_R2, 0);
    uoom_input_tick(0, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_FIRE));
    uoom_input_feed_code(R_R2, 100);
    uoom_input_tick(100, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_FIRE));
    CHECK(count_events(UOOM_ACT_WEAPON_NEXT, 1) == 0);
    uoom_input_tick(400, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_FIRE));      /* pulse retired */

    /* Hold: the weapon changes while the button is still down, and letting go
     * afterwards must not also fire. */
    uoom_input_init();
    uoom_input_feed_code(P_R2, 0);
    uoom_input_tick(0, UOOM_CTX_GAME);
    uoom_input_tick(400, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_WEAPON_NEXT));   /* not yet */
    uoom_input_tick(600, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_WEAPON_NEXT));
    drain();
    uoom_input_feed_code(R_R2, 1500);
    uoom_input_tick(1500, UOOM_CTX_GAME);
    CHECK(count_events(UOOM_ACT_FIRE, 1) == 0);

    /* One switch per hold, however long it is held. */
    uoom_input_init();
    uoom_input_feed_code(P_R2, 0);
    uoom_input_tick(0, UOOM_CTX_GAME);
    uoom_input_tick(600, UOOM_CTX_GAME);
    uoom_input_tick(1200, UOOM_CTX_GAME);
    uoom_input_tick(1800, UOOM_CTX_GAME);
    uoom_input_feed_code(R_R2, 2000);
    uoom_input_tick(2000, UOOM_CTX_GAME);
    CHECK(count_events(UOOM_ACT_WEAPON_NEXT, 1) == 1);

    /* Click-only firmware: a click is a synthetic 300ms hold, which is short
     * enough to stay a shot rather than becoming a weapon switch. */
    uoom_input_init();
    uoom_input_feed_code('4', 0);
    uoom_input_tick(0, UOOM_CTX_GAME);
    uoom_input_tick(300, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_FIRE));
    CHECK(!uoom_input_is_down(UOOM_ACT_WEAPON_NEXT));
}

static void test_menu_context(void)
{
    printf("test_menu_context\n");

    uoom_input_init();
    uoom_input_feed_code(P_L1, 0);
    uoom_input_tick(0, UOOM_CTX_MENU);
    CHECK(uoom_input_is_down(UOOM_ACT_MENU_UP));
    CHECK(!uoom_input_is_down(UOOM_ACT_TURN_LEFT));
    drain();

    /* auto-repeat while held */
    uoom_input_tick(500, UOOM_CTX_MENU);
    uoom_input_tick(700, UOOM_CTX_MENU);
    CHECK(count_events(UOOM_ACT_MENU_UP, 1) >= 1);

    uoom_input_feed_code(R_L1, 800);
    uoom_input_tick(800, UOOM_CTX_MENU);
    drain();

    uoom_input_feed_code(P_R1, 900);
    uoom_input_tick(900, UOOM_CTX_MENU);
    CHECK(uoom_input_is_down(UOOM_ACT_CONFIRM));
}

static void test_context_switch_releases_keys(void)
{
    printf("test_context_switch_releases_keys\n");

    uoom_input_init();
    uoom_input_feed_code(P_R1, 0);
    uoom_input_tick(0, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_FORWARD));
    drain();

    /* the menu opens while the player is still holding forward */
    uoom_input_tick(50, UOOM_CTX_MENU);
    CHECK(!uoom_input_is_down(UOOM_ACT_FORWARD));
    CHECK(count_events(UOOM_ACT_FORWARD, 0) == 1);
}

static void test_click_only_fallback(void)
{
    printf("test_click_only_fallback\n");

    /* If the kernel only ever delivers CLICK codes, a click must still move
     * the player -- otherwise the port is unplayable on that firmware. */
    uoom_input_init();
    uoom_input_feed_code('3', 0);               /* R1 click */
    uoom_input_tick(0, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_FORWARD));
    uoom_input_tick(500, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_FORWARD));

    /* ...but once a real press code shows up, clicks must be ignored so one
     * physical press is not counted twice */
    uoom_input_init();
    uoom_input_feed_code(P_R1, 0);
    uoom_input_tick(0, UOOM_CTX_GAME);
    uoom_input_feed_code(R_R1, 300);
    uoom_input_tick(300, UOOM_CTX_GAME);
    drain();
    uoom_input_feed_code('3', 310);             /* trailing click: ignored */
    uoom_input_tick(310, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_FORWARD));
}

static void test_kernel_chord_code(void)
{
    printf("test_kernel_chord_code\n");

    /* The kernel defines its own L1+R2 chord code ('z'). UOOM does not use
     * that chord, but dropping the code would eat a turn or a shot if the
     * firmware sends it in place of the individual ones. */
    uoom_input_init();
    uoom_input_feed_code('z', 0);
    uoom_input_tick(0, UOOM_CTX_GAME);
    CHECK(uoom_input_is_down(UOOM_ACT_TURN_LEFT));
    CHECK(!uoom_input_is_down(UOOM_ACT_FIRE));  /* fire waits for the release */
    uoom_input_tick(500, UOOM_CTX_GAME);        /* synthetic release lands here */
    CHECK(!uoom_input_is_down(UOOM_ACT_TURN_LEFT));
    CHECK(uoom_input_is_down(UOOM_ACT_FIRE));   /* ...and the shot with it */
    uoom_input_tick(800, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_FIRE));

    /* ...and once real press codes are seen, it is ignored like clicks. */
    uoom_input_init();
    uoom_input_feed_code(P_L1, 0);
    uoom_input_tick(0, UOOM_CTX_GAME);
    uoom_input_feed_code(R_L1, 200);
    uoom_input_tick(200, UOOM_CTX_GAME);
    drain();
    uoom_input_feed_code('z', 210);
    uoom_input_tick(210, UOOM_CTX_GAME);
    CHECK(!uoom_input_is_down(UOOM_ACT_FIRE));
}

int main(void)
{
    printf("UOOM port-layer tests (render mode %d, scale %d, dither %d)\n",
           UOOM_RENDER_MODE, UOOM_SCALE_MODE, UOOM_DITHER);

    test_pack_unpack();
    test_blit_bounds();
    test_dither_breaks_bands();

    test_hold_turns();
    test_r1_tap_is_use_hold_is_forward();
    test_chord_tap_menu_hold_backward();
    test_r2_tap_fire_hold_weapon();
    test_menu_context();
    test_context_switch_releases_keys();
    test_click_only_fallback();
    test_kernel_chord_code();

    if (gFail) {
        printf("\n%d check(s) FAILED\n", gFail);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
