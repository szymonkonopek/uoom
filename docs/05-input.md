# Input: DOOM with four buttons

## What the hardware gives us

Four buttons. That is the entire input surface.

- The watch has a GT911 touch controller in the hardware list, but the
  TouchGFX port ships `STM32TouchController` as a **stub**
  ("*stub implementation - not implemented*") and states plainly:
  "*No Touch Support: Button-only input, no gesture recognition*".
- Buttons arrive as kernel `EVENT_BUTTON` messages carrying `SW1..SW4` and one
  of `PRESS / RELEASE / CLICK / LONG_PRESS / HOLD_1S / HOLD_5S / HOLD_10S`.
  The TouchGFX port forwards only the first three, translated to printable
  ASCII so the simulator can inject them from a keyboard:

  | Button | click | press | release |
  |---|---|---|---|
  | SW1 (L1, top-left) | `1` | `q` | `a` |
  | SW2 (R1, top-right) | `3` | `e` | `d` |
  | SW3 (L2, bottom-left) | `2` | `w` | `s` |
  | SW4 (R2, bottom-right) | `4` | `r` | `f` |

- Documented latency: "*~1-2ms*" of message processing, plus "*a 50-60ms
  debounce filter*", and -- the worrying part -- buttons "*react only upon
  release*".

That last clause is the single biggest risk in the whole port, because
**PRESS/RELEASE pairs are what make holding a button possible**, and holding is
how you walk in DOOM. The port handles it without needing to resolve the
ambiguity in advance; see "Click-only fallback" below.

## What DOOM needs

Turn left, turn right, move forward, move backward, fire, use/open, change
weapon, open the menu, navigate the menu, confirm, cancel. Eleven actions,
four buttons. The gap is closed with *timing* -- tap versus hold versus chord
-- not with more buttons.

## Which chords are safe

This is worth spelling out, because it is the constraint that shapes the whole
scheme. A chord is only usable if a player never presses those two buttons
together by accident:

| Chord | Verdict |
|---|---|
| L1+R2, L2+R2 | turn + fire -- **constant** in combat |
| L1+R1, L2+R1 | turn + walk -- **constant** in movement |
| R1+R2 | run and gun -- **constant** |
| **L1+L2** | "turn left and right at once" -- never meaningful. **Safe.** |

Exactly one chord is available, so it carries the two actions with nowhere
else to go.

## The scheme

**In game** (`UOOM_CTX_GAME`):

| Input | Action |
|---|---|
| L1 hold | turn left |
| L2 hold | turn right |
| R1 hold | move forward |
| R1 **tap** (<220 ms) | use / open (also nudges you forward -- see below) |
| R2 hold or tap | fire |
| R2 **double-tap** (<320 ms) | next weapon |
| L1+L2 **tap** | menu / escape |
| L1+L2 **hold** (>260 ms) | walk backward |

**In menu** (`UOOM_CTX_MENU`) -- no chords, no holds, taps with auto-repeat:

| Input | Action |
|---|---|
| L1 | up (repeats while held) |
| L2 | down (repeats while held) |
| R1 tap | confirm; on a slider row, one notch up |
| R1 **hold** | on a slider row only, one notch down, repeating |
| R2 | back / escape |

### Sliders

DOOM's sliders -- screen size, the volumes, mouse sensitivity -- move on
`key_menu_left` and `key_menu_right`, and this watch has neither. `KEY_ENTER`
reaches them through `key_menu_forward`, which only ever takes a slider *up*.
So they could be raised and never lowered.

R1 therefore does both jobs, but only on a slider row: a tap raises, a hold
lowers with auto-repeat. On a slider row R1 is decided on release rather than
on the press, which costs one press worth of latency -- acceptable there,
because the value moves continuously as you hold and the feedback is the hold
itself rather than the first step. On every other row R1 still fires on the
press, since a menu item that waits for your finger to come up feels broken,
and a hold means nothing on an ordinary item anyway.

`M_CurrentItemIsSlider` (patch 0021) answers the one question the input layer
needs, so nothing in `uoom_input.c` has to know about DOOM. `status == 2` is
DOOM's own marker for a slider; see `M_Responder`'s `key_menu_left` case.

## Three decisions worth defending

**R1 starts moving on the press, not on the release.** The obvious way to
distinguish tap-from-hold is to wait `UOOM_TAP_MS` and see what happens -- but
that puts 220 ms of latency on *every single step*, which is unplayable. So
forward begins immediately, and a short press *additionally* fires USE when it
ends. You take one small step while opening the door, which is what a player
does anyway.

**A double-tap on fire still fires both times.** Same reasoning: suppressing
the first shot until the double-tap window closes would add latency to the most
latency-sensitive action in the game. So a double-tap means "two shots *and*
switch weapon". Slightly odd on paper, invisible in play.

**USE and the other synthesised taps are held for 100 ms, not one frame.**
DOOM builds its command from event state once per 28.6 ms tic
(`TICRATE == 35`). A key that goes down and up inside one tic can be missed by
`G_BuildTiccmd` entirely. `UOOM_PULSE_MS` is deliberately longer than a tic.

## Click-only fallback

If the kernel really does deliver nothing but `CLICK`, every hold in the scheme
above collapses and the game is unplayable. Rather than pick a side, the input
layer resolves it at runtime: a `CLICK` code is turned into a synthetic press
plus a release `UOOM_CLICK_PULSE_MS` later, but *only until a real PRESS code
has been observed*. After that, clicks are ignored for the rest of the session
so one physical press is never counted twice.

No build flag, no configuration, self-correcting on whatever firmware it
lands on. Both branches are covered by `tests/test_port.c`.

## Saving without a keyboard

DOOM's save menu drops into a text field and waits for a name. There is no
keyboard here and no way to add one, so picking a slot saves immediately under
a name derived from where you are -- `E1M1 S3`, episode 1 map 1 skill 3, which
is what anyone would have typed anyway.

DOOM asks yes/no questions the same way -- "Quit Game", "overwrite this
save?" -- and answers them with the characters `y` and `n`, which this port
cannot produce either. `key_menu_confirm` and `key_menu_abort` are rebound at
runtime to Enter and Escape, so R1 confirms and R2 cancels exactly as they do
everywhere else in the menu. Without that, the quit confirmation could be
opened and never answered.

Quit Game then had a second problem: `I_Quit`'s `exit(0)` sits inside
`#if ORIGCODE`, which doomgeneric leaves undefined, so it ran DOOM's `atexit`
handlers and returned -- the menu item did nothing at all. It now calls
`UOOM_Quit`, which releases the buttons the kernel still thinks are held,
closes the log, and ends the process. It does not return into the engine to
finish the frame: by the time `I_Quit` reaches the port, the sound system and
demo state have already been torn down. The `D_Endoom` handler is unregistered
along the way -- it printed an 80x25 DOS text screen and then called `exit(0)`
itself, from inside the handler loop, which is what stranded the log unflushed.

The two "Read This!" screens are gone for a related reason: they are 320x200
diagrams of a PC keyboard, unreadable on a round 240x240 panel and wrong for
four buttons. Removing them also removed a 68 KB allocation spike that was
crashing the game when the menu was opened over a running demo.

## Not yet bound

- **Automap** -- there is genuinely no input left. Candidates: a third chord
  state (L1+L2 held past ~1.2 s, on top of tap=menu and hold=backward), or
  stealing R2's double-tap from weapon switching. Needs a play test to decide.
- **Strafing** -- possible but not essential; turning covers it.
- **Run** -- the port holds DOOM's run modifier permanently
  (`UOOM_AUTORUN`), because at 240x240 you want to cover ground.

## Testing without a watch

`uoom_input` emits abstract actions (`UOOM_ACT_*`), not DOOM keycodes, and
takes an explicit `nowMs`. It therefore has no dependency on DOOM, on the SDK
or on real time, and the whole scheme -- taps, holds, chords, repeats, context
switches, the click fallback -- is exercised by `tests/run.sh` on the host in
under a second. The DOOM-specific mapping from action to keycode lives in
`doomgeneric_uoom.c`.
