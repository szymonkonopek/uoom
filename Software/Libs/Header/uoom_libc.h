/* uoom_libc.h -- the C library the watch does not have
 *
 * The UNA linker script does `/DISCARD/ { libc.a(*) libm.a(*) libgcc.a(*) }`
 * and instead binds a fixed list of 336 symbols to absolute addresses in the
 * kernel's own flash-resident newlib. Anything not on that list compiles
 * (the toolchain headers still declare it) and then fails to link.
 *
 * DOOM needs eight things that are not on the list. Six are trivial; two are
 * not, and one of those is in the renderer's hot path:
 *
 *   abs, atoi, puts, putchar, strcasecmp, strncasecmp   -- trivial
 *   __aeabi_ldivmod / __aeabi_uldivmod                  -- no libgcc at all
 *
 * The last pair matters because m_fixed.c's FixedDiv does
 * `((int64_t) a << 16) / b`, a 64-by-32 division, per column of the frame.
 * Cortex-M33 has hardware 32-bit UDIV/SDIV but nothing for 64-bit, so the
 * compiler emits a call to a helper that does not exist here.
 */
#ifndef UOOM_LIBC_H
#define UOOM_LIBC_H

#include <stdint.h>

/* 64-by-32 signed division, quotient only -- exactly what FixedDiv wants.
 *
 * Faster than a general __aeabi_ldivmod would be even if we had one: the
 * divisor is known to be 32 bits, so this is two hardware 32-bit divides and
 * a correction rather than a 64-step shift-subtract loop. Patch 0011 points
 * FixedDiv at it directly. */
int32_t uoom_div64_32(int64_t num, int32_t den);

#endif /* UOOM_LIBC_H */
