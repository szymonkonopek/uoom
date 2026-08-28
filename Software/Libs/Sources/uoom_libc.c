/* uoom_libc.c -- see uoom_libc.h */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "uoom_libc.h"
#include "uoom_sys.h"

/* ------------------------------------------------------- 64-by-32 division
 *
 * Reduce a 64/32 division to hardware 32-bit divides. Classic long-division
 * over two halves: divide the high word, carry the remainder into the low
 * word. Both divides fit in UDIV because each partial numerator is kept
 * below 2^32 by construction.
 */

static uint32_t udiv64_32(uint64_t num, uint32_t den)
{
    uint32_t hi = (uint32_t)(num >> 32);
    uint32_t lo = (uint32_t)num;
    uint32_t q;
    uint32_t rem;
    int i;

    if (den == 0u) {
        return 0xFFFFFFFFu;         /* DOOM never divides by zero; be inert */
    }
    if (hi == 0u) {
        return lo / den;            /* the common case: one UDIV */
    }
    if (hi >= den) {
        return 0xFFFFFFFFu;         /* quotient would not fit in 32 bits */
    }

    /* Restoring division of the low word with the high word as the initial
     * remainder. 32 iterations, each a shift and a conditional subtract --
     * this path is only taken when the result genuinely needs it. */
    rem = hi;
    q = 0u;
    for (i = 31; i >= 0; --i) {
        uint32_t bit = (lo >> i) & 1u;
        uint32_t carry = rem >> 31;

        rem = (rem << 1) | bit;
        if (carry != 0u || rem >= den) {
            rem -= den;
            q |= 1u << i;
        }
    }
    return q;
}

int32_t uoom_div64_32(int64_t num, int32_t den)
{
    int neg = 0;
    uint64_t n;
    uint32_t d;
    uint32_t q;

    if (num < 0) {
        n = (uint64_t)(-num);
        neg = !neg;
    } else {
        n = (uint64_t)num;
    }
    if (den < 0) {
        d = (uint32_t)(-den);
        neg = !neg;
    } else {
        d = (uint32_t)den;
    }

    q = udiv64_32(n, d);

    /* DOOM's FixedDiv saturates rather than wrapping; match that. */
    if (q > 0x7FFFFFFFu) {
        return neg ? (int32_t)0x80000000 : (int32_t)0x7FFFFFFF;
    }
    return neg ? -(int32_t)q : (int32_t)q;
}

/* ---------------------------------------------------------- ABI helpers
 *
 * Provided for anything the compiler emits that we did not anticipate. The
 * ABI wants the 64-bit quotient in r0:r1 and the remainder in r2:r3, which C
 * cannot express, so these return a struct of both -- the form GCC and clang
 * accept for these two names.
 */

#if defined(UOOM_ON_WATCH)

typedef struct { uint64_t q; uint64_t r; } uoom_lldiv_t;

uoom_lldiv_t __aeabi_uldivmod(uint64_t n, uint64_t d);
uoom_lldiv_t __aeabi_ldivmod(int64_t n, int64_t d);

uoom_lldiv_t __aeabi_uldivmod(uint64_t n, uint64_t d)
{
    uoom_lldiv_t out;
    uint64_t q = 0u;
    uint64_t r = 0u;
    int i;

    if (d == 0u) {
        out.q = 0xFFFFFFFFFFFFFFFFull;
        out.r = 0u;
        return out;
    }
    if (d <= 0xFFFFFFFFull && (n >> 32) < (uint32_t)d) {
        /* fits the fast path */
        q = udiv64_32(n, (uint32_t)d);
        out.q = q;
        out.r = n - q * d;
        return out;
    }
    for (i = 63; i >= 0; --i) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= 1ull << i;
        }
    }
    out.q = q;
    out.r = r;
    return out;
}

uoom_lldiv_t __aeabi_ldivmod(int64_t n, int64_t d)
{
    uoom_lldiv_t out;
    int negQ = 0;
    int negR = 0;
    uint64_t un;
    uint64_t ud;

    if (n < 0) { un = (uint64_t)(-n); negQ = !negQ; negR = 1; } else { un = (uint64_t)n; }
    if (d < 0) { ud = (uint64_t)(-d); negQ = !negQ; }              else { ud = (uint64_t)d; }

    out = __aeabi_uldivmod(un, ud);
    if (negQ) { out.q = (uint64_t)(-(int64_t)out.q); }
    if (negR) { out.r = (uint64_t)(-(int64_t)out.r); }
    return out;
}

/* ------------------------------------------------------- the trivial six */

int abs(int v);
int atoi(const char *s);
int puts(const char *s);
int putchar(int c);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);

int abs(int v)
{
    return (v < 0) ? -v : v;
}

int atoi(const char *s)
{
    int sign = 1;
    int v = 0;

    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    if (*s == '-') { sign = -1; ++s; }
    else if (*s == '+') { ++s; }

    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        ++s;
    }
    return v * sign;
}

/* DOOM's startup banner uses these; route them where every other diagnostic
 * goes. Line-buffered because the kernel logger takes whole strings. */
static char sOutBuf[128];
static unsigned sOutLen;

static void out_flush(void)
{
    if (sOutLen != 0u) {
        sOutBuf[sOutLen] = '\0';
        uoom_printf("%s", sOutBuf);
        sOutLen = 0u;
    }
}

int putchar(int c)
{
    if (c == '\n' || sOutLen + 2u >= sizeof(sOutBuf)) {
        if (c == '\n' && sOutLen + 2u < sizeof(sOutBuf)) {
            sOutBuf[sOutLen++] = '\n';
        }
        out_flush();
        return c;
    }
    sOutBuf[sOutLen++] = (char)c;
    return c;
}

int puts(const char *s)
{
    out_flush();
    uoom_printf("%s\n", s);
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    return uoom_strcasecmp(a, b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    return uoom_strncasecmp(a, b, n);
}


/* --------------------------------------------------------------- ctype table
 *
 * newlib's <ctype.h> implements isspace/toupper/tolower as macros that index
 * a global `_ctype_` table, and that symbol is not among the 336 the kernel
 * exports -- so w_wad.c's name comparisons and m_menu's text entry would fail
 * to link. The table is plain data; GENERATED by the snippet in this file's
 * commit message, laid out newlib-style with the EOF slot first and character
 * c at index c+1.
 */
const char _ctype_[257] = {
    0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x28, 0x28, 0x28, 0x28, 0x28, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x88, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x10, 0x10, 0x10, 0x10,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
};

/* --------------------------------------------------------------------- strdup
 *
 * Used by d_iwad's path building and by M_StringDuplicate. malloc here is the
 * kernel's allocator. */
char *strdup(const char *s);

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1u;
    char *p;

    p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

#endif /* UOOM_ON_WATCH */
