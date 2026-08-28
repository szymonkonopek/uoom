/* uoom_input.h -- four hardware buttons -> DOOM
 *
 * The watch has SW1..SW4 and nothing else: no touch (the TouchGFX port ships a
 * stub touch controller), no crown, no accelerometer gestures wired to input.
 * DOOM wants turn/move/fire/use/menu/weapon -- at least seven actions. This
 * layer is the reconciliation, and it is deliberately a state machine over
 * *timing* (tap vs hold vs chord) rather than a flat key map.
 *
 * Emits abstract actions, not DOOM keycodes: the DOOM-specific mapping lives
 * in doomgeneric_uoom.c so this file stays testable on its own.
 *
 * See docs/05-input.md for the scheme and the reasoning.
 */
#ifndef UOOM_INPUT_H
#define UOOM_INPUT_H

#include <stdint.h>
#include "uoom_config.h"

/* Physical buttons, in the kernel's SW1..SW4 order.
 * Watch layout:  L1 = top-left     R1 = top-right
 *                L2 = bottom-left  R2 = bottom-right */
typedef enum {
    UOOM_BTN_L1 = 0,    /* SW1 */
    UOOM_BTN_R1,        /* SW2 */
    UOOM_BTN_L2,        /* SW3 */
    UOOM_BTN_R2,        /* SW4 */
    UOOM_BTN_COUNT
} uoom_btn_t;

typedef enum {
    UOOM_ACT_NONE = 0,
    UOOM_ACT_TURN_LEFT,
    UOOM_ACT_TURN_RIGHT,
    UOOM_ACT_FORWARD,
    UOOM_ACT_BACKWARD,
    UOOM_ACT_FIRE,
    UOOM_ACT_USE,
    UOOM_ACT_WEAPON_NEXT,
    UOOM_ACT_MENU,          /* open/close the menu (ESC) */
    UOOM_ACT_MENU_UP,
    UOOM_ACT_MENU_DOWN,
    UOOM_ACT_CONFIRM,
    UOOM_ACT_CANCEL,
    UOOM_ACT_AUTOMAP,
    UOOM_ACT_COUNT
} uoom_action_t;

/* Which action set to emit. The caller decides, because only the caller can
 * see DOOM's `menuactive` / `automapactive`. */
typedef enum {
    UOOM_CTX_GAME = 0,
    UOOM_CTX_MENU
} uoom_context_t;

void uoom_input_init(void);

/* Feed a raw ASCII code as delivered by the UNA kernel's EVENT_BUTTON
 * translation (press: q/e/w/r, release: a/d/s/f, click: 1/3/2/4).
 * Unknown codes are ignored. */
void uoom_input_feed_code(uint8_t code, uint32_t nowMs);

/* Feed a decoded button transition. Used by the host harness and by tests. */
void uoom_input_feed_button(uoom_btn_t btn, int pressed, uint32_t nowMs);

/* Advance the state machine: resolves holds, chords, repeats and click
 * pulses into queued action events. Call once per frame. */
void uoom_input_tick(uint32_t nowMs, uoom_context_t ctx);

/* Pop one queued action event. Returns 0 when the queue is empty.
 * `*pressed` is 1 for down, 0 for up. */
int uoom_input_pop(uoom_action_t *act, int *pressed);

/* True while the action is logically held. Lets the DOOM layer rebuild key
 * state after a suspend/resume without replaying the queue. */
int uoom_input_is_down(uoom_action_t act);

/* Drop every held action (emitting the matching key-ups). Call on suspend so
 * the player does not keep walking into a wall while the app is backgrounded. */
void uoom_input_release_all(void);

#endif /* UOOM_INPUT_H */
