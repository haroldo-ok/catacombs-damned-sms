#ifndef FASTMATH_H
#define FASTMATH_H

#include "defines.h"

/* Hand written replacements for SDCC's generic 32 bit helpers - see
 * src/fastmath.s.  `mdMul` holds the (compile time constant) multiplier used
 * by MulDivS so that only two arguments have to be passed in registers. */
extern s16 mdMul;

s16 MulShift8(s16 a, s16 b);        /* (a * b) >> 8                       */
s16 MulDivS(s16 a, s16 d);          /* (a * mdMul) / d, truncated toward 0 */
u16 Div16Shift8(u16 a, u16 d);      /* (a << 8) / d, needs (a >> 8) < d   */
u16 PsgPeriod(u16 freq);            /* 111861 / freq - safe to call from an ISR */

#endif
