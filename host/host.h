/* host.h -- the laptop harness's own knobs, kept out of the port layers */
#ifndef UOOM_HOST_H
#define UOOM_HOST_H

#include <stdint.h>

void host_set_root(const char *dir);

/* A scripted run uses a virtual clock that advances a fixed amount per frame,
 * so two runs of the same script produce byte-identical frames. */
int  host_deterministic(void);
void host_advance_clock(uint32_t ms);

void host_frame_begin(void);
int  host_should_quit(void);
void host_present(const uint8_t *fb);
int  host_poll_key(uint8_t *code);
void host_haptic(uint8_t strength);

#endif /* UOOM_HOST_H */
