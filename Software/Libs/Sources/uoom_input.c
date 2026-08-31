/* uoom_input.c -- see uoom_input.h and docs/05-input.md */

#include "uoom_input.h"

/* ------------------------------------------------------------------ tuning
 *
 * Scaled for the watch's *measured* 10Hz frame tick, not the 30-60 the SDK's
 * TouchGFX document claims. That matters more than it sounds: input is sampled
 * once per frame, so a 100ms tick is the quantum for everything below, and
 * thresholds picked for a 50ms frame land between samples.
 *
 * Two consequences worth stating:
 *   - A tap has to be forgiving. At 100ms sampling a human tap is one or two
 *     samples, and a slightly slow one would otherwise read as a hold and walk
 *     the player forward instead of opening the door.
 *   - A synthesised pulse only has to outlast one frame, not one DOOM tic.
 *     DOOM runs 35 tics/s against a 10Hz display, so TryRunTics executes about
 *     three or four tics per frame -- a key held for one frame is already seen
 *     by several of them.
 */

#ifndef UOOM_TAP_MS
#define UOOM_TAP_MS         320u   /* press shorter than this counts as a tap */
#endif
#ifndef UOOM_CHORD_HOLD_MS
#define UOOM_CHORD_HOLD_MS  350u   /* L1+L2 held this long -> walk backward */
#endif
#ifndef UOOM_WEAPON_HOLD_MS
#define UOOM_WEAPON_HOLD_MS 500u   /* R2 held this long -> next weapon.
                                    * Must stay clear of UOOM_CLICK_PULSE_MS
                                    * plus one frame: on click-only firmware a
                                    * click is a synthetic 300ms hold, and it
                                    * has to read as a shot, not a switch. */
#endif
#ifndef UOOM_PULSE_MS
#define UOOM_PULSE_MS       200u   /* how long a synthesised tap is held down.
                                    * Two frames at 10Hz: one would work, but
                                    * only if the release did not land on the
                                    * very next sample. */
#endif
#ifndef UOOM_REPEAT_DELAY_MS
#define UOOM_REPEAT_DELAY_MS 500u
#endif
#ifndef UOOM_REPEAT_MS
#define UOOM_REPEAT_MS      250u
#endif
#ifndef UOOM_CLICK_PULSE_MS
#define UOOM_CLICK_PULSE_MS 300u   /* click-only fallback: how long a click
                                    * pretends to be a hold */
#endif

#ifndef UOOM_SLIDER_HOLD_MS
#define UOOM_SLIDER_HOLD_MS 400u   /* R1 held this long on a slider -> lower */
#endif

#define QUEUE_LEN   32
#define PULSE_LEN   6

/* ------------------------------------------------------------------- state */

typedef struct {
    uint8_t  down;
    uint8_t  consumed;      /* claimed by a chord; ignore until released */
    uint32_t tDown;
    uint32_t tUp;
    uint32_t tRepeat;       /* next auto-repeat due (menu context) */
    uint32_t synthUp;       /* click-only fallback: synthetic release due */
} btn_state_t;

typedef struct {
    uoom_action_t act;
    uint8_t       pressed;
} event_t;

typedef struct {
    uoom_action_t act;
    uint32_t      dueMs;
    uint8_t       active;
} pulse_t;

static btn_state_t   sBtn[UOOM_BTN_COUNT];
static event_t       sQueue[QUEUE_LEN];
static unsigned      sHead, sTail;
static pulse_t       sPulse[PULSE_LEN];
static uint8_t       sDown[UOOM_ACT_COUNT];
static int           sCtx;            /* uoom_context_t, or -1 before the
                                       * first tick -- adopting the initial
                                       * context must not look like a flip */
static uint8_t       sChordActive;
static uint32_t      sChordStart;
static uint8_t       sChordWalked;    /* chord already turned into BACKWARD */
static uint8_t       sR2WasDown;      /* R2 press still waiting for its release */
static uint8_t       sR1WasDown;      /* same, for R1 on a slider row */
static uint8_t       sSeenPressCode;  /* kernel really does send press codes */
static uint8_t       sInited;

/* ------------------------------------------------------------------- queue */

static void emit(uoom_action_t act, int pressed)
{
    unsigned next;

    if (act <= UOOM_ACT_NONE || act >= UOOM_ACT_COUNT) {
        return;
    }
    /* collapse redundant transitions -- DOOM does not care, but a stuck
     * key-down with no matching up does */
    if (sDown[act] == (uint8_t)(pressed != 0)) {
        return;
    }
    sDown[act] = (uint8_t)(pressed != 0);

    next = (sHead + 1u) % QUEUE_LEN;
    if (next == sTail) {
        /* full: drop the oldest, never the newest -- a lost key-up is worse
         * than a lost key-down */
        sTail = (sTail + 1u) % QUEUE_LEN;
    }
    sQueue[sHead].act     = act;
    sQueue[sHead].pressed = (uint8_t)(pressed != 0);
    sHead = next;
}

static void pulse(uoom_action_t act, uint32_t nowMs, uint32_t lenMs)
{
    int i;

    emit(act, 1);
    for (i = 0; i < PULSE_LEN; ++i) {
        if (!sPulse[i].active) {
            sPulse[i].active = 1;
            sPulse[i].act    = act;
            sPulse[i].dueMs  = nowMs + lenMs;
            return;
        }
    }
    /* no slot: release immediately rather than leak a held key */
    emit(act, 0);
}

static void run_pulses(uint32_t nowMs)
{
    int i;

    for (i = 0; i < PULSE_LEN; ++i) {
        if (sPulse[i].active && (int32_t)(nowMs - sPulse[i].dueMs) >= 0) {
            sPulse[i].active = 0;
            emit(sPulse[i].act, 0);
        }
    }
}

/* --------------------------------------------------------------- lifecycle */

void uoom_input_init(void)
{
    int i;

    for (i = 0; i < UOOM_BTN_COUNT; ++i) {
        sBtn[i].down = sBtn[i].consumed = 0;
        sBtn[i].tDown = sBtn[i].tUp = sBtn[i].tRepeat = 0;
        sBtn[i].synthUp = 0;
    }
    for (i = 0; i < PULSE_LEN; ++i) {
        sPulse[i].active = 0;
    }
    for (i = 0; i < UOOM_ACT_COUNT; ++i) {
        sDown[i] = 0;
    }
    sHead = sTail = 0;
    sCtx = -1;
    sChordActive = sChordWalked = 0;
    sChordStart = 0;
    sR2WasDown = sR1WasDown = 0;
    sSeenPressCode = 0;
    sInited = 1;
}

void uoom_input_release_all(void)
{
    int i;

    for (i = 1; i < UOOM_ACT_COUNT; ++i) {
        if (sDown[i]) {
            emit((uoom_action_t)i, 0);
        }
    }
    for (i = 0; i < PULSE_LEN; ++i) {
        sPulse[i].active = 0;
    }
    for (i = 0; i < UOOM_BTN_COUNT; ++i) {
        sBtn[i].down = 0;
        sBtn[i].consumed = 0;
    }
    sChordActive = sChordWalked = 0;
    /* A press that was still waiting for its release must not come back as a
     * shot -- or, on a slider, as a tap -- when the app resumes. */
    sR2WasDown = 0;
    sR1WasDown = 0;
}

int uoom_input_pop(uoom_action_t *act, int *pressed)
{
    if (sTail == sHead) {
        return 0;
    }
    *act     = sQueue[sTail].act;
    *pressed = sQueue[sTail].pressed;
    sTail    = (sTail + 1u) % QUEUE_LEN;
    return 1;
}

int uoom_input_is_down(uoom_action_t act)
{
    if (act <= UOOM_ACT_NONE || act >= UOOM_ACT_COUNT) {
        return 0;
    }
    return sDown[act] != 0;
}

/* ------------------------------------------------------------- raw decoding */

void uoom_input_feed_button(uoom_btn_t btn, int pressed, uint32_t nowMs)
{
    btn_state_t *b;

    if (!sInited) {
        uoom_input_init();
    }
    if (btn < 0 || btn >= UOOM_BTN_COUNT) {
        return;
    }
    b = &sBtn[btn];

    if (pressed) {
        if (b->down) {
            return;             /* repeat press, ignore */
        }
        b->down     = 1;
        b->consumed = 0;
        b->synthUp  = 0;
        b->tDown    = nowMs;
        b->tRepeat  = nowMs + UOOM_REPEAT_DELAY_MS;
    } else {
        if (!b->down) {
            return;
        }
        b->down  = 0;
        b->tUp   = nowMs;
    }
}

void uoom_input_feed_code(uint8_t code, uint32_t nowMs)
{
    /* Kernel mapping, from the UNA TouchGFX port:
     *   button   click  press  release
     *   SW1 (L1)   '1'    'q'     'a'
     *   SW2 (R1)   '3'    'e'     'd'
     *   SW3 (L2)   '2'    'w'     's'
     *   SW4 (R2)   '4'    'r'     'f'
     */
    switch (code) {
    case 'q': sSeenPressCode = 1; uoom_input_feed_button(UOOM_BTN_L1, 1, nowMs); return;
    case 'e': sSeenPressCode = 1; uoom_input_feed_button(UOOM_BTN_R1, 1, nowMs); return;
    case 'w': sSeenPressCode = 1; uoom_input_feed_button(UOOM_BTN_L2, 1, nowMs); return;
    case 'r': sSeenPressCode = 1; uoom_input_feed_button(UOOM_BTN_R2, 1, nowMs); return;

    case 'a': uoom_input_feed_button(UOOM_BTN_L1, 0, nowMs); return;
    case 'd': uoom_input_feed_button(UOOM_BTN_R1, 0, nowMs); return;
    case 's': uoom_input_feed_button(UOOM_BTN_L2, 0, nowMs); return;
    case 'f': uoom_input_feed_button(UOOM_BTN_R2, 0, nowMs); return;

    /* The kernel has one built-in chord of its own: SDK/GUI/Button.hpp
     * defines L1R2 = 'z'. UOOM's own chord is L1+L2, so this code is not
     * wanted -- but it must not be *lost* either. If the kernel emits 'z'
     * instead of the individual codes, silently dropping it would eat a turn
     * or a shot in the middle of combat, which is where L1+R2 happens
     * constantly. So it is expanded into both buttons, on the same
     * self-correcting terms as clicks below. */
    case 'z':
        if (!sSeenPressCode) {
            uoom_input_feed_button(UOOM_BTN_L1, 1, nowMs);
            uoom_input_feed_button(UOOM_BTN_R2, 1, nowMs);
            sBtn[UOOM_BTN_L1].synthUp = nowMs + UOOM_CLICK_PULSE_MS;
            sBtn[UOOM_BTN_R2].synthUp = nowMs + UOOM_CLICK_PULSE_MS;
        }
        return;

    /* Clicks are redundant when press/release arrive, and the kernel sends
     * all three for one physical press. But the port docs warn that buttons
     * are debounced and "react only upon release" -- if that turns out to mean
     * press codes never come, hold-based control would be dead. So: honour
     * clicks as a short synthetic hold, but only until we have proof that
     * real press codes exist. Self-correcting, no build flag needed. */
    case '1': case '2': case '3': case '4':
        if (!sSeenPressCode) {
            uoom_btn_t b = (code == '1') ? UOOM_BTN_L1
                         : (code == '3') ? UOOM_BTN_R1
                         : (code == '2') ? UOOM_BTN_L2
                                         : UOOM_BTN_R2;
            uoom_input_feed_button(b, 1, nowMs);
            sBtn[b].synthUp = nowMs + UOOM_CLICK_PULSE_MS;
        }
        return;

    default:
        return;
    }
}

/* ------------------------------------------------------------- state machine */

static void tick_game(uint32_t nowMs)
{
    btn_state_t *l1 = &sBtn[UOOM_BTN_L1];
    btn_state_t *l2 = &sBtn[UOOM_BTN_L2];
    btn_state_t *r1 = &sBtn[UOOM_BTN_R1];
    btn_state_t *r2 = &sBtn[UOOM_BTN_R2];

    /* --- chord: both left buttons ------------------------------------------
     * The only chord that is safe on this watch. Every left+right pair is a
     * normal combat or movement combination (turn+fire, turn+walk), and R1+R2
     * is run-and-gun. L1+L2 is "turn both ways at once", which is never
     * something a player means. So it carries the two actions that have
     * nowhere else to go: tap = menu, hold = walk backward. */
    if (l1->down && l2->down) {
        if (!sChordActive) {
            sChordActive = 1;
            sChordWalked = 0;
            sChordStart  = (l1->tDown > l2->tDown) ? l1->tDown : l2->tDown;
            emit(UOOM_ACT_TURN_LEFT, 0);
            emit(UOOM_ACT_TURN_RIGHT, 0);
        }
        if (!sChordWalked && (nowMs - sChordStart) >= UOOM_CHORD_HOLD_MS) {
            sChordWalked = 1;
            emit(UOOM_ACT_BACKWARD, 1);
        }
        l1->consumed = l2->consumed = 1;
    } else if (sChordActive) {
        sChordActive = 0;
        if (sChordWalked) {
            emit(UOOM_ACT_BACKWARD, 0);
        } else {
            pulse(UOOM_ACT_MENU, nowMs, UOOM_PULSE_MS);
        }
        sChordWalked = 0;
        /* keep both consumed until physically released, so lifting one finger
         * out of the chord does not start a turn */
        if (l1->down) { l1->consumed = 1; }
        if (l2->down) { l2->consumed = 1; }
    }

    if (!l1->down) { l1->consumed = 0; }
    if (!l2->down) { l2->consumed = 0; }

    /* --- turning ---------------------------------------------------------- */
    emit(UOOM_ACT_TURN_LEFT,  l1->down && !l1->consumed);
    emit(UOOM_ACT_TURN_RIGHT, l2->down && !l2->consumed);

    /* --- R1: hold = forward, tap = use -----------------------------------
     * Deciding tap-vs-hold on release would put UOOM_TAP_MS of latency on
     * every step. So forward starts on the press, unconditionally, and a
     * short press *also* fires USE on release. Walking a few centimetres
     * while opening a door is exactly what a player does anyway. */
    if (r1->down) {
        emit(UOOM_ACT_FORWARD, 1);
    } else if (sDown[UOOM_ACT_FORWARD]) {
        emit(UOOM_ACT_FORWARD, 0);
        if ((r1->tUp - r1->tDown) < UOOM_TAP_MS) {
            pulse(UOOM_ACT_USE, nowMs, UOOM_PULSE_MS);
        }
    }

    /* --- R2: tap = fire, hold = next weapon ------------------------------
     * The one action decided on release, and for the opposite reason to R1:
     * firing on the press and switching later if the finger stays down would
     * spend a shot on every weapon change. Ammo is scarcer than the frame or
     * two this costs, and there is no other button left to put the weapon on.
     *
     * One switch per hold, not a repeating cycle: DOOM has to lower and raise
     * the weapon between changes, so a repeat faster than that animation
     * simply gets dropped, and one slower than it is no quicker than letting
     * go and pressing again. */
    if (r2->down) {
        sR2WasDown = 1;
        if (!r2->consumed && (nowMs - r2->tDown) >= UOOM_WEAPON_HOLD_MS) {
            r2->consumed = 1;           /* committed: this press is a switch */
            pulse(UOOM_ACT_WEAPON_NEXT, nowMs, UOOM_PULSE_MS);
        }
    } else {
        if (sR2WasDown && !r2->consumed) {
            pulse(UOOM_ACT_FIRE, nowMs, UOOM_PULSE_MS);
        }
        sR2WasDown   = 0;
        r2->consumed = 0;
    }
}

/* Set from the mapping layer each frame; see uoom_input_set_slider(). */
static uint8_t sOnSlider;

/* --- R1 on a slider ------------------------------------------------------
 *
 * DOOM's sliders are driven by left and right, and this watch has neither.
 * KEY_ENTER reaches them through key_menu_forward, which only ever raises the
 * value one notch -- so screen size and the volumes could be turned up and
 * never down.
 *
 * So on a slider, and only there, R1 is decided on release: a tap raises, a
 * hold lowers with auto-repeat. Everywhere else it still fires on the press,
 * because a menu item that waits for your finger to come up feels broken, and
 * because a hold means nothing on an ordinary item anyway.
 *
 * The cost of deciding on release is one press worth of latency, and only on
 * slider rows -- where the value moves as you hold it, so the feedback is the
 * hold itself rather than the first step. */
static void tick_slider_r1(uint32_t nowMs)
{
    btn_state_t *r1 = &sBtn[UOOM_BTN_R1];

    if (r1->down) {
        sR1WasDown = 1;

        if ((nowMs - r1->tDown) < UOOM_SLIDER_HOLD_MS) {
            return;                     /* could still turn out to be a tap */
        }
        if (!r1->consumed) {
            r1->consumed = 1;           /* committed: this is a hold */
            r1->tRepeat  = nowMs + UOOM_REPEAT_MS;
            pulse(UOOM_ACT_MENU_LEFT, nowMs, UOOM_PULSE_MS);
        } else if ((int32_t)(nowMs - r1->tRepeat) >= 0) {
            r1->tRepeat = nowMs + UOOM_REPEAT_MS;
            pulse(UOOM_ACT_MENU_LEFT, nowMs, UOOM_PULSE_MS);
        }
        return;
    }

    /* Up. If it went down and came back before the threshold, it was a tap;
     * `consumed` says a hold already claimed it. */
    if (sR1WasDown && !r1->consumed) {
        pulse(UOOM_ACT_MENU_RIGHT, nowMs, UOOM_PULSE_MS);
    }
    sR1WasDown   = 0;
    r1->consumed = 0;
}

void uoom_input_set_slider(int isSlider)
{
    if (!isSlider && sOnSlider) {
        /* Left the slider row -- or the menu -- possibly mid-hold. Drop the
         * press so it cannot arrive as a tap on whatever is highlighted now. */
        sR1WasDown = 0;
        sBtn[UOOM_BTN_R1].consumed = sBtn[UOOM_BTN_R1].down ? 1 : 0;
    }
    sOnSlider = isSlider ? 1u : 0u;
}

static void tick_menu(uint32_t nowMs)
{
    static const uoom_action_t kMenuAct[UOOM_BTN_COUNT] = {
        UOOM_ACT_MENU_UP,     /* L1 */
        UOOM_ACT_CONFIRM,     /* R1 */
        UOOM_ACT_MENU_DOWN,   /* L2 */
        UOOM_ACT_CANCEL       /* R2 */
    };
    int i;

    /* No chords here, and no holds except the slider case below: menu
     * navigation wants discrete taps with auto-repeat, which is the opposite
     * of the game context. */
    for (i = 0; i < UOOM_BTN_COUNT; ++i) {
        btn_state_t *b = &sBtn[i];

        if (i == UOOM_BTN_R1 && sOnSlider) {
            tick_slider_r1(nowMs);
            continue;
        }

        if (b->down && !b->consumed) {
            b->consumed = 1;                    /* fired for this press */
            pulse(kMenuAct[i], nowMs, UOOM_PULSE_MS);
        }
        /* auto-repeat only for the two navigation keys */
        if (b->down && (i == UOOM_BTN_L1 || i == UOOM_BTN_L2)
            && (int32_t)(nowMs - b->tRepeat) >= 0) {
            b->tRepeat = nowMs + UOOM_REPEAT_MS;
            pulse(kMenuAct[i], nowMs, UOOM_PULSE_MS);
        }
        if (!b->down) {
            b->consumed = 0;
        }
    }
}

void uoom_input_tick(uint32_t nowMs, uoom_context_t ctx)
{
    if (!sInited) {
        uoom_input_init();
    }

    if (sCtx < 0) {
        sCtx = (int)ctx;
    }

    /* A context flip mid-hold would otherwise leave a game key stuck down
     * behind the menu. */
    if ((int)ctx != sCtx) {
        int i;

        for (i = 1; i < UOOM_ACT_COUNT; ++i) {
            if (sDown[i]) {
                emit((uoom_action_t)i, 0);
            }
        }
        for (i = 0; i < UOOM_BTN_COUNT; ++i) {
            sBtn[i].consumed = sBtn[i].down;    /* do not re-fire on the flip */
        }
        sChordActive = sChordWalked = 0;
        sCtx = (int)ctx;
    }

    run_pulses(nowMs);

    /* retire click-only synthetic holds */
    {
        int i;

        for (i = 0; i < UOOM_BTN_COUNT; ++i) {
            if (sBtn[i].synthUp != 0u
                && (int32_t)(nowMs - sBtn[i].synthUp) >= 0) {
                sBtn[i].synthUp = 0u;
                uoom_input_feed_button((uoom_btn_t)i, 0, nowMs);
            }
        }
    }

    if (ctx == UOOM_CTX_MENU) {
        tick_menu(nowMs);
    } else {
        tick_game(nowMs);
    }
}
