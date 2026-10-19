#ifndef FIXEDMATH_H
#define FIXEDMATH_H

#include "defines.h"
#include "gamedata.h"

#define FIXED_SHIFT 8
#define FIXED_ONE (1 << FIXED_SHIFT)
#define INT_TO_FIXED(x) ((x) * FIXED_ONE)
#define FIXED_TO_INT(x) ((x) >> 8)

#define FIXED_ANGLE_WRAP(x) ((x) & 255)
#define FIXED_ANGLE_90 (FIXED_ANGLE_MAX / 4)
#define FIXED_ANGLE_45 (FIXED_ANGLE_90 / 2)
#define FIXED_ANGLE_180 (FIXED_ANGLE_90 * 2)
#define FIXED_ANGLE_270 (FIXED_ANGLE_90 * 3)

#define FixedSin(angle) (sinTable[(u8)(angle)])
#define FixedCos(angle) (sinTable[(u8)(FIXED_ANGLE_90 - (angle))])

extern u16 randState;
u16 Random(void);
u8 RandomBelow(u8 n);           /* uniform-ish 0..n-1, no 16 bit division */
void SeedRandom(u16 seed);

#endif
