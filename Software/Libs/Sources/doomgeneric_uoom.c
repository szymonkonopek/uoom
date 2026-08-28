/* doomgeneric_uoom.c -- the doomgeneric backend for the UNA Watch
 *
 * This is the file that makes DOOM a UNA app. It owns:
 *   - the single 240x240 panel buffer,
 *   - the frame loop (kernel-paced, not DOOM-paced),
 *   - the translation from abstract input actions to DOOM keycodes,
 *   - the two hooks the patched i_video.c calls instead of building a 32-bit
 *     framebuffer nobody wants.
 *
 * Deliberately *not* a copy of doomgeneric_soso.c. Upstream's backends all
 * expand DOOM's 8-bit output into a 32-bit DG_ScreenBuffer and then hand it to
 * a windowing system. On this hardware the panel is itself 8 bits per pixel,
 * so that intermediate buffer is 230-256KB of pure waste on a budget of a few
 * hundred kilobytes. We keep upstream's key-queue idea and throw the rest away.
 */

#include <string.h>

#include "uoom_config.h"
#include "uoom_plat.h"
#include "uoom_video.h"
#include "uoom_input.h"
#include "uoom_file.h"
#include "uoom_sys.h"
#include "uoom_text.h"

#include "doomtype.h"
#include "doomkeys.h"
#include "doomgeneric.h"
#include "doomstat.h"
#include "z_zone.h"

/* DOOM globals we read. `menuactive` is the only one that matters: without it
 * the same four buttons cannot both drive the player and navigate the menu.
 * Declared here rather than including m_menu.h / doomstat.h to keep this file
 * from dragging in the whole engine's header graph. */
extern boolean menuactive;
extern boolean automapactive;

/* Rebound at startup instead of patched: upstream leaves key_nextweapon
 * unbound (m_controls.c), so we can claim any unused keycode. 0xB0 sits just
 * past the KEY_* block in doomkeys.h. */
extern int key_nextweapon;
extern int key_speed;
extern int key_menu_confirm;
extern int key_menu_abort;
extern boolean precache;
extern boolean M_CurrentItemIsSlider(void);

#define UOOM_KEY_NEXTWEAPON  0xB0
#define UOOM_NUMKEYS         256     /* g_game.c's gamekeydown[] size */

/* ---------------------------------------------------------------- key queue
 *
 * Upstream's ring, with two fixes it needs on this platform:
 *   - power-of-two mask instead of `%`, and a single store per index update,
 *     so an interrupt-fed producer cannot observe a half-updated index;
 *   - 64 slots instead of 16. i_input.c's drain loop stops after the first
 *     key-up it sees in a tic, so codes queue up faster than DOOM consumes
 *     them, and an overwritten key-up means a key stuck down forever.
 */

#define KEYQUEUE_SIZE 64        /* must stay a power of two */

static volatile uint16_t sKeyQueue[KEYQUEUE_SIZE];
static volatile uint32_t sKeyWrite;
static volatile uint32_t sKeyRead;

static void key_push(int pressed, unsigned char doomKey)
{
    uint32_t w = sKeyWrite;

    sKeyQueue[w & (KEYQUEUE_SIZE - 1u)] =
        (uint16_t)(((pressed != 0) << 8) | doomKey);
    sKeyWrite = w + 1u;
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    uint32_t r = sKeyRead;

    if (r == sKeyWrite) {
        return 0;
    }
    {
        uint16_t v = sKeyQueue[r & (KEYQUEUE_SIZE - 1u)];

        *pressed = (v >> 8) & 1;
        *doomKey = (unsigned char)(v & 0xFFu);
    }
    sKeyRead = r + 1u;
    return 1;
}

/* --------------------------------------------------------- action -> keycode */

static unsigned char action_to_key(uoom_action_t act)
{
    switch (act) {
    case UOOM_ACT_TURN_LEFT:    return KEY_LEFTARROW;
    case UOOM_ACT_TURN_RIGHT:   return KEY_RIGHTARROW;
    case UOOM_ACT_FORWARD:      return KEY_UPARROW;
    case UOOM_ACT_BACKWARD:     return KEY_DOWNARROW;
    case UOOM_ACT_FIRE:         return KEY_FIRE;
    case UOOM_ACT_USE:          return KEY_USE;
    case UOOM_ACT_WEAPON_NEXT:  return UOOM_KEY_NEXTWEAPON;
    case UOOM_ACT_MENU:         return KEY_ESCAPE;
    case UOOM_ACT_MENU_UP:      return KEY_UPARROW;
    case UOOM_ACT_MENU_DOWN:    return KEY_DOWNARROW;
    case UOOM_ACT_CONFIRM:      return KEY_ENTER;
    case UOOM_ACT_CANCEL:       return KEY_ESCAPE;
    case UOOM_ACT_MENU_LEFT:    return KEY_LEFTARROW;
    case UOOM_ACT_MENU_RIGHT:   return KEY_RIGHTARROW;
    case UOOM_ACT_AUTOMAP:      return KEY_TAB;
    default:                    return 0;
    }
}

static void pump_input(uint32_t nowMs)
{
    uoom_action_t act;
    uint8_t code;
    int pressed;

    /* Drain the whole kernel queue. The SDK's own TouchGFX button controller
     * samples exactly one code per frame tick, which at 10Hz would cap input
     * at ten events a second -- one physical press already expands to three
     * codes (press, click, release). Bypassing that is a large part of why
     * this port does not go through TouchGFX at all. */
    while (uoom_plat_poll_key(&code)) {
        uoom_input_feed_code(code, nowMs);
    }

    uoom_input_set_slider(menuactive && M_CurrentItemIsSlider());
    uoom_input_tick(nowMs, menuactive ? UOOM_CTX_MENU : UOOM_CTX_GAME);

    while (uoom_input_pop(&act, &pressed)) {
        unsigned char k = action_to_key(act);

        if (k != 0) {
            key_push(pressed, k);
        }
#if UOOM_ENABLE_HAPTIC_SFX
        if (act == UOOM_ACT_FIRE && pressed) {
            uoom_plat_haptic(40);
        }
#endif
    }
}

/* ------------------------------------------------------- releasing a level
 *
 * The full-screen graphics -- WIMAP0 for the intermission, TITLEPIC and CREDIT
 * for the attract loop -- are 68168 bytes each, and DOOM asks for them while
 * the level that just ended is still resident. Vanilla frees PU_LEVEL only in
 * the next P_SetupLevel, so those two events overlap by design.
 *
 * On a 512KB arena they do not fit together: the failure is a contiguous-block
 * failure, and dead level geometry interleaved with cache leaves no 68KB hole
 * however much of the cache the allocator purges.
 */

void UOOM_Quit(void)
{
    /* Reached from the menu, mid-frame, and I_Quit has already run every
     * atexit handler on the way here -- the sound system is shut down and the
     * demo state is torn down. So do not hand control back to the engine to
     * finish the tick: do uoom_run()'s tail here and go.
     *
     * Releasing the buttons matters because the kernel keeps its own key
     * state: a fire button still held when the process dies would otherwise
     * arrive at the watch face. */
    uoom_printf("UOOM: quit from the menu\n");
    uoom_input_release_all();
    uoom_printf("UOOM exit\n");
    uoom_log_close();
    uoom_plat_exit();
}

void UOOM_ReleaseLevel(void)
{
    int i;

    /* Anything pointing into PU_LEVEL that outlives the level has to be
     * cleared first. player_t::mo is the one that matters: doomgeneric_Tick
     * hands players[consoleplayer].mo to S_UpdateSounds on every tick,
     * including throughout the intermission. */
    for (i = 0; i < MAXPLAYERS; ++i) {
        players[i].mo = NULL;
        players[i].attacker = NULL;
    }

    Z_FreeTags(PU_LEVEL, PU_PURGELEVEL - 1);
}

/* -------------------------------------------------- hooks from i_video.c
 *
 * Patch 0001 replaces the bodies of I_SetPalette and I_FinishUpdate with calls
 * to these two. That patch is the heart of the port: it removes the
 * 8bpp -> 32bpp conversion, the DG_ScreenBuffer allocation, and the integer
 * `fb_scaling` logic -- which, incidentally, computes 240/320 == 0 on this
 * panel and would leave the screen black.
 */

void UOOM_SetPalette(const uint8_t *pal768)
{
    uoom_video_set_palette(pal768);
}

#if UOOM_HUD_DIAG
static uint32_t sFrameMs;
static uint32_t sFrames;

static void draw_diag(uint8_t *fb)
{
    const uoom_io_stats_t *io = uoom_io_stats();
    const uint8_t fg = uoom_video_pack_rgb(255, 255, 0);
    char line[32];
    unsigned i = 0;

    /* No snprintf here: this runs every frame and vsnprintf on newlib-nano is
     * not cheap. Hand-rolled, three fields, fixed widths. */
    line[i++] = 'M'; line[i++] = 'S';
    line[i++] = (char)('0' + (sFrameMs / 100) % 10);
    line[i++] = (char)('0' + (sFrameMs / 10) % 10);
    line[i++] = (char)('0' + sFrameMs % 10);
    line[i++] = ' ';
    line[i++] = 'I'; line[i++] = 'O';
    line[i++] = (char)('0' + (io->reads / 1000) % 10);
    line[i++] = (char)('0' + (io->reads / 100) % 10);
    line[i++] = (char)('0' + (io->reads / 10) % 10);
    line[i++] = (char)('0' + io->reads % 10);
    line[i] = '\0';

    uoom_text_draw(fb, 2, 2, line, 1, fg);
}
#endif

void UOOM_FinishUpdate(const uint8_t *videoBuffer)
{
#if UOOM_HUD_DIAG
    static uint32_t last;
    uint32_t now = uoom_plat_ticks_ms();

    if (last != 0u) {
        sFrameMs = now - last;
    }
    last = now;
    ++sFrames;
#endif

    uint8_t *panel = uoom_present_buffer();

    uoom_video_blit(videoBuffer, panel);
#if UOOM_HUD_DIAG
    draw_diag(panel);
#endif
    uoom_plat_present(panel);
}

/* ------------------------------------------------------- doomgeneric hooks */

void DG_Init(void)
{
    uoom_video_init();
    uoom_input_init();

    /* Always run. g_game.c computes `speed = key_speed >= NUMKEYS ||
     * gamekeydown[key_speed]`, so parking key_speed past the array is an
     * upstream escape hatch for permanent run -- no key traffic, no patch.
     * On a 240px screen you want to cover ground. */
#if UOOM_AUTORUN
    key_speed = UOOM_NUMKEYS;
#endif
    key_nextweapon = UOOM_KEY_NEXTWEAPON;

    /* Do not preload the level's graphics.
     *
     * R_PrecacheLevel caches every flat, texture patch and sprite the map uses
     * in one pass at load time, which on a PC costs nothing and buys smooth
     * play. Here it is the difference between loading E1M2 and not: the arena
     * holds the level (184KB of geometry) or the level's full texture set, but
     * not both, and the failure lands on a 17KB wall patch inside that loop.
     *
     * With it off, lumps are cached on first use and PU_CACHE purging keeps the
     * working set to what is actually on screen. The cost is re-reading a
     * texture from the WAD now and then, which is a handful of eMMC reads --
     * and DOOM already re-reads lumps whenever the zone is tight, so this is
     * the behaviour the allocator is designed around rather than a new risk.
     *
     * Runtime flag, so no engine patch: precache is a plain boolean. */
    precache = false;

    /* DOOM asks yes/no questions -- "Quit Game", "overwrite this save" -- and
     * answers them with 'y' and 'n', which are characters this port cannot
     * produce: there are four buttons and no keyboard. Rebound to the keys the
     * menu already uses, so R1 confirms and R2 cancels exactly as everywhere
     * else. Runtime variables, so no patch. */
    key_menu_confirm = KEY_ENTER;
    key_menu_abort   = KEY_ESCAPE;
}

/* Never called: patch 0001 replaces the I_FinishUpdate body that used to call
 * it. Kept so i_video.c links if the patch is ever reverted. */
void DG_DrawFrame(void)
{
    UOOM_FinishUpdate((const uint8_t *)0);
}

void DG_SleepMs(uint32_t ms)
{
    uoom_plat_delay_ms(ms);
}

uint32_t DG_GetTicksMs(void)
{
    return uoom_plat_ticks_ms();
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

/* -------------------------------------------------------------- the run loop */

void uoom_run(void)
{
    /* d_iwad's own search calls M_FileExists, i.e. fopen. Patch 0006 points
     * that at the platform layer, but naming the file outright is one less
     * thing to go wrong. */
    static char  argv0[] = "uoom";
    static char  argv1[] = "-iwad";
    static char *argv[4];
    const char  *wad;

    /* Opened before anything else prints, so the log carries DOOM's whole
     * startup and not just what happens after it succeeds. */
    uoom_log_open(UOOM_LOG_PATH);
    uoom_printf("UOOM start\n");

    /* Pump before anything that might need to draw. writeDisplayFrameBuffer is
     * a silent no-op until a COMMAND_APP_GUI_RESUME has been dequeued, and the
     * only thing that dequeues it is waitForFrameTick -- so an error screen
     * drawn before the first tick is invisible. That is exactly how this
     * port's first on-device run managed to fail with no output at all. */
    uoom_plat_frame_wait();
    uoom_printf("UOOM first tick received\n");
    uoom_plat_keep_awake();

    wad = uoom_find_iwad();
    if (wad == NULL) {
        uoom_printf("UOOM: no IWAD found\n");
        /* Do not exit: a black screen tells the user nothing. Hold the
         * instruction screen until they leave, redrawing on each tick so the
         * kernel's backlight timeout does not swallow it. */
        while (!uoom_plat_should_quit()) {
            uoom_plat_frame_wait();
            uoom_text_no_wad_screen();
            uoom_plat_keep_awake();
        }
        return;
    }

    uoom_printf("UOOM: iwad %s\n", wad);
    uoom_printf("UOOM: entering doomgeneric_Create\n");

    argv[0] = argv0;
    argv[1] = argv1;
    argv[2] = (char *)wad;
    argv[3] = 0;

    doomgeneric_Create(3, argv);
    uoom_sys_end_init();
    uoom_printf("UOOM: doomgeneric_Create returned\n");

#if UOOM_BOOT_REPORT
    /* Hold a report on screen while measuring the thing it reports: each
     * frame_wait is one kernel tick, so counting them against the clock gives
     * the real tick rate -- which the SDK's own documents disagree about. */
    {
        const uint32_t t0 = uoom_plat_ticks_ms();
        uint32_t ticks = 0;
        uint32_t hz10 = 0;

        while (ticks < (uint32_t)UOOM_BOOT_REPORT_TICKS
               && !uoom_plat_should_quit()) {
            uoom_plat_frame_wait();
            ++ticks;
            {
                const uint32_t dt = uoom_plat_ticks_ms() - t0;

                if (dt > 300u) {
                    hz10 = (ticks * 10000u) / dt;
                }
            }
            uoom_text_boot_report(wad, uoom_heap_largest, uoom_zone_got, hz10);
            uoom_plat_keep_awake();
        }
        uoom_printf("UOOM tick: %u.%u Hz measured over %u ticks\n",
                    (unsigned)(hz10 / 10u), (unsigned)(hz10 % 10u),
                    (unsigned)ticks);
    }
#endif

    while (!uoom_plat_should_quit()) {
        uint32_t now;

        /* The kernel owns the clock, not DOOM. Blocking here is what keeps the
         * app scheduled and the watch's power budget intact. */
        uoom_plat_frame_wait();

        now = uoom_plat_ticks_ms();
        pump_input(now);

        doomgeneric_Tick();     /* -> D_Display -> I_FinishUpdate -> present */

        uoom_plat_keep_awake();
    }

    /* Released so a stuck fire button does not follow the player back into the
     * watch face if the app is relaunched. */
    uoom_input_release_all();
    uoom_printf("UOOM exit\n");
    uoom_log_close();
}
