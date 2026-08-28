/* uoom_text.h -- the port's own tiny text renderer
 *
 * Not for the game: DOOM draws its own fonts out of the WAD. This is for the
 * two things that have to work *before* or *outside* the game --
 *
 *   - telling the user no IWAD was found, and where to put one;
 *   - a frame-time / memory readout, because there is no debugger on a wrist.
 */
#ifndef UOOM_TEXT_H
#define UOOM_TEXT_H

#include <stdint.h>

#include "uoom_config.h"

/* Draw `str` at (x, y) in panel pixels, scaled `scale`x. Characters not in the
 * font are skipped. Clipped to the panel. `px` is a packed ABGR2222 pixel. */
void uoom_text_draw(uint8_t *fb, int x, int y, const char *str,
                    int scale, uint8_t px);

/* Width in pixels that uoom_text_draw would consume. */
int uoom_text_width(const char *str, int scale);

/* Word-wrapped block, returns the y just past the last line drawn. */
int uoom_text_block(uint8_t *fb, int x, int y, int wPix, const char *str,
                    int scale, uint8_t px);

/* One line, centred horizontally on the panel. */
void uoom_text_draw_center(uint8_t *fb, int y, const char *str,
                           int scale, uint8_t px);

/* Word-wrapped block with every line centred. Returns the y just past the
 * last line. Pass wPix 0 for the widest width that stays inside the panel's
 * visible circle at every row -- see uoom_text_safe_width(). */
int uoom_text_block_center(uint8_t *fb, int y, int wPix, const char *str,
                           int scale, uint8_t px);

/* The widest a block may be if every row of it must fall inside the circle,
 * whatever vertical position it ends up at: the side of the inscribed square.
 * Simpler than solving per row, and the difference does not matter for a few
 * lines near the middle. */
int uoom_text_safe_width(void);

/* Fill the panel and present a fatal-error screen. Safe to call before the
 * video tables are built. */
void uoom_text_error_screen(const char *msg);

/* The "put a WAD here" screen, shown when no IWAD is found. */
void uoom_text_no_wad_screen(void);

/* The boot report: what the port found and what it measured. `tickHz10` is the
 * measured tick rate times ten, or 0 while it is still being measured. */
void uoom_text_boot_report(const char *wad, uint32_t heapLargest,
                           uint32_t zoneGot, uint32_t tickHz10);

#endif /* UOOM_TEXT_H */
