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
| R1 | confirm |
| R2 | back / escape |

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
