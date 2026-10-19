#include "defines.h"
#include "map.h"
#include "fastmath.h"
#include "platform.h"
#include "game.h"
#include "draw.h"
#include "enemy.h"
#include "player.h"

u8 mapLevel[MAP_WIDTH * MAP_HEIGHT / 2];

/* Map_IsBlocked and Map_IsSolid are hand written in src/hot.s. */

/* Two cells per byte.  MAP_WIDTH is 32, so the byte index is simply
 * y * 16 + x / 2 - which keeps SDCC away from both the 16 bit multiply and
 * the signed division by two that the literal transcription generated.
 * These are the most called functions in the whole program (the generator
 * alone used to run a hundred thousand of them). */
/* Map_GetCell and Map_GetCellSafe are hand written in src/hot.s. */

void Map_SetCell(u8 x, u8 y, CellType type)
{
    s16 index;

    if (x >= MAP_WIDTH || y >= MAP_HEIGHT) return;

    index = (s16)(((u16)y << 4) | (u8)(x >> 1));

    /* NOTE: the original tests `x & 1` here but `index & 1` in GetCell.  Both
     * agree because the map width is even; kept verbatim. */
    if (x & 1) mapLevel[index] = (u8)((mapLevel[index] & 0x0f) | (type << 4));
    else       mapLevel[index] = (u8)((mapLevel[index] & 0xf0) | (type & 0x0f));
}

/* (v * 256) / d, truncated toward zero like the C division it replaces, and
 * saturated to the +/-32767 the original clamped to. */
static s16 ScaleStep(s16 v, u16 d)
{
    u16 a = (u16)(v < 0 ? -v : v);
    u16 q;

    if (d == 0 || (u16)(a >> 8) >= d) q = 0x7fff;   /* would not fit in 16 bits */
    else {
        q = Div16Shift8(a, d);
        if (q > 0x7fff) q = 0x7fff;
    }

    return (s16)(v < 0 ? (s16)(-(s16)q) : (s16)q);
}

/* (v * p) >> 8 with p >= 0, truncated toward zero (MulShift8 floors, which
 * differs from C division by one unit for negative products). */
static s16 MulTrunc8(s16 v, s16 p)
{
    if (v < 0) return (s16)(-MulShift8((s16)(-v), p));
    return MulShift8(v, p);
}

/* Bresenham-style line of sight, cell by cell.  Identical to the original
 * except that the two `((int32_t)delta * CELL_SIZE) / deltafrac` divisions -
 * SDCC's __divslong, about 10800 cycles each - are now a 32/16 divide in
 * assembly, and the per-step `frac / CELL_SIZE` is a byte select.  Every
 * idle enemy runs one of these every tick, so this was a sixth of the whole
 * frame time.  Coordinates are always inside the 8192x6144 map, i.e. never
 * negative, which is what lets the shifts replace signed divisions. */
u8 Map_IsClearLine(s16 x1, s16 y1, s16 x2, s16 y2)
{
    s16 cellX1 = (s16)((u16)x1 >> 8);
    s16 cellX2 = (s16)((u16)x2 >> 8);
    s16 cellY1 = (s16)((u16)y1 >> 8);
    s16 cellY2 = (s16)((u16)y2 >> 8);

    s16 partial, step, frac;
    s16 x, y;
    s8 dir;

    if (cellX1 != cellX2) {
        if (cellX2 > cellX1) {
            partial = (s16)((s16)((cellX1 + 1) << 8) - x1);
            dir = 1;
        } else {
            partial = (s16)(x1 - (s16)(cellX1 << 8));
            dir = -1;
        }

        step = ScaleStep((s16)(y2 - y1), (u16)ABS((s16)(x2 - x1)));
        frac = (s16)(y1 + MulTrunc8(step, partial));

        x = (s16)(cellX1 + dir);
        cellX2 = (s16)(cellX2 + dir);
        do {
            y = (s16)((u16)frac >> 8);
            frac = (s16)(frac + step);

            if (Map_IsSolid((u8)x, (u8)y)) return 0;

            x = (s16)(x + dir);
        } while (x != cellX2);
    }

    if (cellY1 != cellY2) {
        if (cellY2 > cellY1) {
            partial = (s16)((s16)((cellY1 + 1) << 8) - y1);
            dir = 1;
        } else {
            partial = (s16)(y1 - (s16)(cellY1 << 8));
            dir = -1;
        }

        step = ScaleStep((s16)(x2 - x1), (u16)ABS((s16)(y2 - y1)));
        frac = (s16)(x1 + MulTrunc8(step, partial));

        y = (s16)(cellY1 + dir);
        cellY2 = (s16)(cellY2 + dir);
        do {
            x = (s16)((u16)frac >> 8);
            frac = (s16)(frac + step);

            if (Map_IsSolid((u8)x, (u8)y)) return 0;

            y = (s16)(y + dir);
        } while (y != cellY2);
    }

    return 1;
}

void Map_DebugDraw(void)
{
    s16 x, y;
    u8 n;

    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            Platform_PutPixel((u8)x, (u8)y,
                (u8)(Map_GetCell((u8)x, (u8)y) == CellType_BrickWall ? 1 : 0));

            if (x == camera.cellX && y == camera.cellY && (globalTickFrame & 8) != 0)
                Platform_PutPixel((u8)x, (u8)y, 1);
        }
    }

    if ((globalTickFrame & 2) != 0) {
        for (n = 0; n < MAX_ENEMIES; n++) {
            Enemy *enemy = &enemies[n];
            if (Enemy_IsValid(enemy))
                Platform_PutPixel((u8)(enemy->x / CELL_SIZE), (u8)(enemy->y / CELL_SIZE), 1);
        }
    }
}

void Map_DrawMinimap(void)
{
    const u8 minimapWidth = 24;
    const u8 minimapHeight = 18;
    u8 playerCellX = (u8)(player.x / CELL_SIZE);
    u8 playerCellY = (u8)(player.y / CELL_SIZE);
    u8 startCellX = (u8)(playerCellX - minimapWidth / 2);
    u8 startCellY = (u8)(playerCellY - minimapHeight / 2);

    u8 outX = 0;
    u8 cellX = startCellX;
    u8 x, y;

    for (x = 0; x < minimapWidth; x++) {
        u8 outY = 0;
        u8 cellY = startCellY;

        for (y = 0; y < minimapHeight; y++) {
            if (cellX == playerCellX && cellY == playerCellY) {
                Platform_PutPixel(outX, outY,
                    (u8)((globalTickFrame & 3) ? COLOUR_BLACK : COLOUR_WHITE));
            } else {
                Platform_PutPixel(outX, outY,
                    (u8)(cellX < MAP_WIDTH && cellY < MAP_HEIGHT && Map_IsSolid(cellX, cellY)
                         ? COLOUR_BLACK : COLOUR_WHITE));
            }
            outY++;
            cellY++;
        }
        outX++;
        cellX++;
    }
}
