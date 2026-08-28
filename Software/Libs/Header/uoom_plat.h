/* uoom_plat.h -- the entire surface between UOOM and the machine it runs on.
 *
 * Two implementations exist:
 *   Software/Libs/Sources/uoom_una_platform.cpp   the watch (UNA SDK, C++)
 *   host/host_platform.c                          a laptop (stdio + PPM out)
 *
 * Everything above this line -- the palette collapse, the resample, the button
 * state machine, the WAD cache, the doomgeneric backend -- is plain C with no
 * knowledge of either. That is what makes the port testable.
 *
 * Deliberately narrow, and deliberately *offset-based* rather than
 * seek-and-read: whether the UNA SDK's IFile exposes a seek at all is an open
 * question (docs/07-open-questions.md), so the seam is drawn where a reopen-
 * and-skip fallback can hide behind it.
 */
#ifndef UOOM_PLAT_H
#define UOOM_PLAT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- storage */

typedef struct uoom_plat_file uoom_plat_file_t;

/* Open for reading (write == 0) or writing (write != 0, truncating).
 * Returns NULL on failure. Paths are relative to the app's own directory. */
uoom_plat_file_t *uoom_plat_open(const char *path, int write);

/* Length of a file that is not open, or -1 if it does not exist. */
long uoom_plat_filesize(const char *path);

/* Read `len` bytes from absolute `offset`. Returns bytes read, or -1.
 * This is the WAD hot path: everything DOOM reads from the IWAD comes through
 * here, via the block cache in uoom_file.c. */
int uoom_plat_pread(uoom_plat_file_t *f, uint32_t offset, void *buf, uint32_t len);

/* Sequential append-style write, for savegames and screenshots. */
int uoom_plat_write(uoom_plat_file_t *f, const void *buf, uint32_t len);

void uoom_plat_close(uoom_plat_file_t *f);

/* Commit buffered writes to storage.
 *
 * Not optional for the log: IFile::write leaves the bytes in the filesystem's
 * cache, so without this a crash takes with it exactly the line that would
 * have explained the crash. Learned the hard way. */
void uoom_plat_sync(uoom_plat_file_t *f);

int uoom_plat_exists(const char *path);
int uoom_plat_remove(const char *path);
int uoom_plat_rename(const char *oldPath, const char *newPath);
int uoom_plat_mkdir(const char *path);

/* ------------------------------------------------------------------- time */

uint32_t uoom_plat_ticks_ms(void);
void     uoom_plat_delay_ms(uint32_t ms);

/* ---------------------------------------------------------------- display */

/* Hand a finished 240x240 ABGR2222 frame to the panel.
 *
 * On the watch this blocks while the kernel reads the buffer (the display
 * message carries a raw pointer and is sent with a 1s timeout), so the buffer
 * must not be touched until this returns. */
void uoom_plat_present(const uint8_t *fb);

/* Keep the screen lit. The kernel's backlight auto-off is 4-5 seconds, which
 * would otherwise black out the game mid-corridor. Called every frame; the
 * implementation rate-limits. */
void uoom_plat_keep_awake(void);

/* ------------------------------------------------------------ frame pacing */

/* Block until the platform says it is time for the next frame.
 *
 * On the watch this is the kernel's EVENT_GUI_TICK, which is how a UNA app
 * gets scheduled at all -- and its rate is the port's biggest unknown: the
 * TouchGFX port docs say "typically 30-60 FPS" while two shipped example apps
 * state 10 Hz outright. See docs/07-open-questions.md. */
void uoom_plat_frame_wait(void);

/* Non-zero once the kernel has asked the app to stop, or the user quit. */
int uoom_plat_should_quit(void);

/* Ask for shutdown from inside the game -- what DOOM's "Quit Game" reaches.
 * Sets the flag the frame loop checks, for callers that can return to it. */
void uoom_plat_request_quit(void);

/* End the process. Does not return. DOOM's I_Quit has already run every
 * atexit handler by the time it reaches the port, so there is no half-shut-down
 * engine left to return into and finish a frame with. */
void uoom_plat_exit(void);

/* ------------------------------------------------------------------ input */

/* Pop one raw button code (the kernel's ASCII press/release/click encoding).
 * Returns 0 when the queue is empty. Drain it fully every frame: the kernel's
 * own TouchGFX button controller takes only one code per tick, and at a 10Hz
 * tick that would cap input at ten events a second. */
int uoom_plat_poll_key(uint8_t *code);

/* ---------------------------------------------------------------- feedback */

/* Haptic pulse, 0..100. Owned by the service process, so on the watch this
 * posts a message. No-op where there is no motor. */
void uoom_plat_haptic(uint8_t strength);

/* ------------------------------------------------------------------ output */

void uoom_plat_log(const char *msg);

/* Ask another process for the zone, if the platform has one that can hold it.
 *
 * Returns 1 and fills *addr / *size on success, 0 if the platform has no such
 * arrangement (the host) or the request went unanswered. Blocks while it waits,
 * pumping the frame tick, so it must be called after the first tick. */
int uoom_plat_zone_from_service(uint32_t *addr, uint32_t *size);

/* Ask the platform what memory it has, and log it. Called once before the zone
 * is allocated.
 *
 * On the watch this is the only way to learn the size of the kernel's heap --
 * where DOOM's zone lives, and a number the SDK documents nowhere. Writes the
 * largest allocatable block to *largestFree if it can find out, or 0 if it
 * cannot. */
void uoom_plat_report_memory(uint32_t *largestFree);

/* Report a fatal error and stop. Must not return. */
void uoom_plat_panic(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* UOOM_PLAT_H */
