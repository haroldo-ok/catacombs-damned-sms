#include "fixedmath.h"

/* non-static so the headless playtest can pin it for reproducible levels */
u16 randState = 1;

u16 Random(void)
{
    randState ^= (u16)(randState << 7);
    randState ^= (u16)(randState >> 9);
    randState ^= (u16)(randState << 8);
    return randState;
}

/* Random value in [0, n).  `Random() % n` compiles to SDCC's __moduint at
 * ~900 cycles and the dungeon generator runs thousands of them, so this masks
 * down to the next power of two and re-rolls the few values that fall outside
 * the range - exactly uniform, and about a third of the cost. */
u8 RandomBelow(u8 n)
{
    u8 mask = 1;
    u8 v;

    if (n < 2) return 0;

    while (mask < (u8)(n - 1)) mask = (u8)((mask << 1) | 1);

    do {
        v = (u8)((u8)Random() & mask);
    } while (v >= n);

    return v;
}

void SeedRandom(u16 seed)
{
    randState = seed | 1;
}
