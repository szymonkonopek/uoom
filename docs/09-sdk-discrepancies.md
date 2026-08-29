# Where the SDK's documentation and the SDK disagree

Everything here cost time, and each one was found the same way: the documented
behaviour was assumed, the result made no sense, and the truth turned out to be
elsewhere. They are written down so the next person spends the time on
something else.

Checked against `una-sdk` at `sdk-v1.4.0` and kernel firmware as shipped on the
watch in August 2026. Line numbers are from that checkout.

---

## 1. `waitForFrameTick()` does not deliver custom messages

**The docs say** — `Docs/TouchGFX-Port-Architecture.md:1091`:

```cpp
case EVENT_GUI_TICK:
    // Process queued custom messages before rendering
    callCustomMessageHandler();
    return false; // Allow TouchGFX to render
```

and at line 362, that custom messages "are queued and processed at the
appropriate time."

**The SDK does** — `Libs/Source/Port/TouchGFX/TouchGFXCommandProcessor.cpp`,
the real `waitForFrameTick()`: on `EVENT_GUI_TICK` it releases the message,
calls `onFrame()` and returns. It never calls `callCustomMessageHandler()`.
Application-specific messages are pushed onto `mUserQueue` and the loop
`continue`s. Nothing in the SDK ever drains that queue.

The same document shows a *second*, different `waitForFrameTick()` at line 450,
which also does not call it. So the file contradicts both itself and the code.

**Cost.** The service process sent four zone grants; the GUI received none, and
the app died with "could not allocate" while the memory sat in a queue. The fix
is one line — the app must call `callCustomMessageHandler()` itself, every
tick, right after `waitForFrameTick()`.

## 2. The kernel tick is 10 Hz, not 30–60

**The docs say** — `Docs/TouchGFX-Port-Architecture.md:692`: "**Frame Rate**:
Limited by kernel tick frequency (typically 30-60 FPS)", and line 1515:
`TARGET_FRAME_TIME = 33; // ~30 FPS`.

**The watch does** — 10.0 Hz, measured by counting `waitForFrameTick()` returns
against the millisecond clock over 20 and 25 ticks, on two different builds.
One run measured 11.1 Hz.

**Cost.** Every performance decision in this port was made against the wrong
number until it was measured. Three to six times wrong is the difference
between "budget a millisecond per scanline" and "you have a hundred".

## 3. `fs` reaches outside the app's directory

**The docs say** — `Docs/app-config-fields.md:27`: "`SDK::Kernel::fs` |
Sandbox-rooted file access; `\"/\"` is the app's own directory", and at line 118,
that keeping paths bare "stops a malformed or hostile package from reaching
outside the app's own directory."

**The watch does** — `fs.dir("..")` opens and lists a real directory outside the
sandbox: the volume root, holding `gps/`, `logo_222.bmp` and
`UnaWatch-Kernel_1.0.2.gld`.

With a caveat that matters as much as the finding. It is a **collapse, not
traversal**: `..`, `../`, `/..`, `../gps`, `../logo_222.bmp` and the control
`../nonexistent-xyz` all returned the *identical* four-entry listing. Every path
containing `..` resolves to that one directory, so there is no way to name a
sibling app's directory and no way to reach one. Drive-letter spellings
(`0:/` through `3:/`) answer nothing.

So the sandbox holds in the sense that matters for isolation, and does not hold
in the sense the sentence claims.

## 4. Both documented `app-manifest.json` examples are rejected

**The docs say** — two shapes, in one file and one tutorial:

- `Docs/app-config-json.md`, the annotated example: carries `description` and
  `previews`, plus every `supports*` field.
- `Docs/Tutorials/Waypoint/Output/app-manifest.json`: carries `description`,
  and **no** `supports*` fields at all.

**The store's validator does** — reject both:

```
must have required property 'supportsLaps' ... 'supportsDistance' ...
'supportsTrack' ... 'supportsHeartbeat' ... 'supportsElevation' ...
'supportsStep' ... 'supportsSpeed' ... 'customMeasures' ... 'stravaExport',
(root) must NOT have additional properties
```

Every `supports*` key is required **even for a `utility` app that has no
activity to support**, and anything outside the set is refused — which rules
out `description` and `previews`, both of which the documentation shows.

The one artifact that matches the live schema is
`Docs/Tutorials/Files/Output/app-manifest.json`, the output of another tutorial.
Copy its key set; ignore the prose.

**Consequence.** The listing text cannot travel in the package. It goes into the
store's web form.

## 5. Custom ELF sections are dropped with a warning

**The packer does** — `Utilities/Scripts/app_packer/app_packer.py:200` copies a
fixed list: `.text`, `.preinit_array`, `.init_array`, `.fini_array`, `.plt`,
`.data`, `.got`, `.bss`, `.stack`. Anything else that is loadable and non-empty
produces `logging.warning("ELF file has unhandled sections: ...")` and is left
out of the image.

A build that places data in its own section therefore **succeeds**, links
clean, packs with a warning in a wall of build output, and arrives on the watch
as garbage.

`.rodata` survives only because `Libs/Source/AppSystem/linker/Main/Sections.ld`
folds it into `.text`, not because the packer knows about it.

## 6. `RequestMemoryInfo` is never answered

`SDK::Message::RequestMemoryInfo` is declared in
`Libs/Header/SDK/Messages/CommandMessages.hpp:263` with a full set of response
fields — `totalHeap`, `freeHeap`, `largestFreeBlock`, `fragmentation`. On this
firmware the request goes out and no response comes back. This port asked at
every boot and logged "RequestMemoryInfo unanswered" every time, which is why
the boot report no longer has a heap row.

## 7. Every build ships as `0.0.0-dev`

`una_app_setup_version` derives the version from an `apps-*` git tag and falls
back to `0.0.0-dev` when there is none. Nothing warns. The package filename
carries that version, so every build of an untagged project is written to the
same path, and a stale `Output/` is indistinguishable from a fresh one — which
is exactly the confusion it caused here. Define `BUILD_VERSION` before calling
it, or tag.

## 8. The container has no GUI size field

`Utilities/Scripts/app_merging/app_merging.py:232` documents the header as
`[AppID u64][AppVersion u32][LibCVersion u32][service_size u32][flags u32]
[AppName char[16]][normal_icon_size u32][small_icon_size u32]`, and the body as
`header + icons + service + gui`, with a CRC32 of all of it in the last four
bytes.

There is a `service_size`. There is **no** `gui_size`. The GUI image's length
can only be "whatever is left", which is undocumented and constrains anything
that might want to append to the package.

## 9. The loader's RAM ceilings are undocumented

Nothing states how much RAM an app may claim. Measured with ballast builds that
grow `.bss` until the loader refuses: **per image (878, 1009] KB**, and
**combined [1147, 1262) KB** across the service and GUI processes. Both
constraints are needed to explain all nine measurements — neither alone fits.

See [`03-memory-budget.md`](03-memory-budget.md).

## 10. The installer renames a colliding package

Uploading a `.uapp` whose name already exists in the app's directory does not
overwrite it: the file arrives as `UOOM-smoke_0.0.0-dev (1).uapp`. An app that
wants to find its own package on disk therefore cannot assume its name and has
to enumerate the directory.

---

## Appendix: the same class of surprise, in doomgeneric

Not the SDK's documentation, but found the same way and worth the same warning.

- **`I_Quit()` does nothing.** Its `exit(0)` sits inside `#if ORIGCODE`, which
  `config.h` never defines, so it runs the `atexit` handlers and returns. "Quit
  Game" was unreachable for a second reason too: `M_QuitResponse` only acts on
  `key_menu_confirm`, which is `'y'`.
- **`R_CheckPlane` has no bounds check.** `R_FindPlane` checks `MAXVISPLANES`;
  `R_CheckPlane`, which also allocates one, does not. A too-small limit
  corrupts the zone silently and runs for hundreds of frames before dying
  somewhere unrelated.
- **`WI_End` is called after the next level loads.** `G_Ticker` processes
  `gameaction` — and so `P_SetupLevel` — at the top of the function, and only
  afterwards releases the intermission's ~150 KB of graphics.
- **`Z_Malloc` gives up after one pass.** It purges cache blocks as it scans,
  merging them into whatever precedes them, so it can walk off the end of the
  list having itself created the block it needed.
