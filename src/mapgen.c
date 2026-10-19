/* ---------------------------------------------------------------------------
 * Dungeon generator - port of MapGenerator.cpp.
 *
 * One algorithmic change: the original "find any big open spaces" pass is a
 * five deep loop nest (O(w*h^4), millions of GetCellSafe calls) which the AVR
 * only survives because it runs once per level.  It computes, for every cell,
 * the largest all-empty square anchored there, and keeps the first maximum in
 * (i ascending, j ascending) order.  Here that is done with the classic
 * largest-empty-square dynamic program over a single rolling column, which is
 * O(w*h) and provably returns the same (space, x, y) triple.
 * ------------------------------------------------------------------------- */
#include "mapgen.h"
#include "map.h"
#include "fixedmath.h"
#include "enemy.h"
#include "game.h"
#include "menu.h"


/* NeighbourInfo, as a plain mask + count */
#define NB_HAS_NORTH      0x01
#define NB_HAS_EAST       0x02
#define NB_HAS_SOUTH      0x04
#define NB_HAS_WEST       0x08
#define NB_DEMOLISH_NORTH 0x10
#define NB_DEMOLISH_EAST  0x20
#define NB_DEMOLISH_SOUTH 0x40
#define NB_DEMOLISH_WEST  0x80

typedef struct {
    u8 mask;
    u8 count;
} NeighbourInfo;

static u8 NeighbourInfo_IsCorner(const NeighbourInfo *info)
{
    u8 m = info->mask;
    if (info->count != 2) return 0;
    if ((m & NB_HAS_NORTH) && (m & NB_HAS_EAST)) return 1;
    if ((m & NB_HAS_EAST) && (m & NB_HAS_SOUTH)) return 1;
    if ((m & NB_HAS_SOUTH) && (m & NB_HAS_WEST)) return 1;
    if ((m & NB_HAS_WEST) && (m & NB_HAS_NORTH)) return 1;
    return 0;
}

/* The original walks rings outwards from (x,y) until it finds `cellType`,
 * scanning the entire map when there is none - 4092 cell reads for a single
 * miss, once per spawn attempt.  That was most of the 40 million cycles the
 * level generator used to cost: eleven seconds of apparently frozen
 * "Entering floor 1".
 *
 * Every caller only ever asked "is the nearest one further away than N", so
 * the distance is not needed at all: this is the same predicate over the
 * (2N+1) square neighbourhood, walked row by row straight through the packed
 * map bytes instead of through 4 bounds-checked accessor calls per cell. */
static u8 IsCellTypeNear(u8 cx, u8 cy, CellType cellType, u8 radius)
{
    u8 x0 = (u8)(cx > radius ? cx - radius : 0);
    u8 y0 = (u8)(cy > radius ? cy - radius : 0);
    u8 x1 = (u8)(cx + radius);
    u8 y1 = (u8)(cy + radius);
    u8 y;

    if (x1 > MAP_WIDTH - 1)  x1 = MAP_WIDTH - 1;
    if (y1 > MAP_HEIGHT - 1) y1 = MAP_HEIGHT - 1;

    /* Two cells live in each byte, so the row is walked a byte at a time and
     * both nibbles are tested at once; `match` is the type in both halves and
     * the xor makes a nibble zero exactly when that cell matches. */
    {
        u8 match = (u8)(cellType | (cellType << 4));
        u8 b0 = (u8)(x0 >> 1);
        u8 b1 = (u8)(x1 >> 1);
        u8 firstMask = (u8)((x0 & 1) ? 0xf0 : 0xff);   /* skip the low cell  */
        u8 lastMask  = (u8)((x1 & 1) ? 0xff : 0x0f);   /* skip the high cell */

        for (y = y0; y <= y1; y++) {
            const u8 *row = mapLevel + ((u16)y << 4) + b0;
            u8 b = b0;

            for (;;) {
                u8 d = (u8)(*row++ ^ match);

                if (b == b0) d |= (u8)~firstMask;
                if (b == b1) d |= (u8)~lastMask;

                if (!(d & 0x0f) || !(d & 0xf0)) return 1;
                if (b == b1) break;
                b++;
            }
        }
    }

    return 0;
}

/* Random cell coordinates for the spawn loops.  MAP_WIDTH is a power of two
 * so the mask is exact; MAP_HEIGHT is 24, so the few values above the map are
 * re-rolled. */
#define RandomCellX() ((u8)(Random() & (MAP_WIDTH - 1)))

static u8 RandomCellY(void)
{
    u8 v;
    do { v = (u8)(Random() & 31); } while (v >= MAP_HEIGHT);
    return v;
}

static NeighbourInfo GetCellNeighbourInfo(u8 x, u8 y)
{
    NeighbourInfo result;
    result.count = 0;
    result.mask = 0;

    if (Map_IsSolid(x, (u8)(y - 1))) { result.mask |= NB_HAS_NORTH; result.count++; }
    if (Map_IsSolid((u8)(x + 1), y)) { result.mask |= NB_HAS_EAST;  result.count++; }
    if (Map_IsSolid(x, (u8)(y + 1))) { result.mask |= NB_HAS_SOUTH; result.count++; }
    if (Map_IsSolid((u8)(x - 1), y)) { result.mask |= NB_HAS_WEST;  result.count++; }

    return result;
}

static NeighbourInfo GetRoomNeighbourMask(u8 x, u8 y, u8 w, u8 h)
{
    NeighbourInfo result;
    u8 hasNorthWall, hasEastWall, hasSouthWall, hasWestWall;
    s16 i, j;

    result.mask = 0;
    result.count = 0;

    if (y > 1) result.mask |= NB_DEMOLISH_NORTH;
    if (x > 1) result.mask |= NB_DEMOLISH_WEST;
    if (x + w + 1 < MAP_WIDTH - 1) result.mask |= NB_DEMOLISH_EAST;
    if (y + h + 1 < MAP_HEIGHT - 1) result.mask |= NB_DEMOLISH_SOUTH;

    /* Don't demolish walls if the neighbouring room has the same wall length */
    if (Map_GetCell((u8)(x - 1), (u8)(y - 2)) != CellType_Empty
        && Map_GetCell((u8)(x + w), (u8)(y - 2)) != CellType_Empty)
        result.mask &= (u8)~NB_DEMOLISH_NORTH;
    if (Map_GetCell((u8)(x - 2), (u8)(y - 1)) != CellType_Empty
        && Map_GetCell((u8)(x - 2), (u8)(y + h)) != CellType_Empty)
        result.mask &= (u8)~NB_DEMOLISH_WEST;
    if (Map_GetCell((u8)(x + w + 1), (u8)(y - 1)) != CellType_Empty
        && Map_GetCell((u8)(x + w), (u8)(y + h + 1)) != CellType_Empty)
        result.mask &= (u8)~NB_DEMOLISH_EAST;
    if (Map_GetCell((u8)(x - 1), (u8)(y + h + 1)) != CellType_Empty
        && Map_GetCell((u8)(x + w), (u8)(y + h + 1)) != CellType_Empty)
        result.mask &= (u8)~NB_DEMOLISH_SOUTH;

    /* Don't demolish wall if this will leave an unattached wall */
    if (Map_GetCell((u8)(x - 1), (u8)(y - 2)) == CellType_Empty
        && Map_GetCell((u8)(x - 2), (u8)(y - 1)) == CellType_Empty)
        result.mask &= (u8)~(NB_DEMOLISH_NORTH | NB_DEMOLISH_WEST);
    if (Map_GetCell((u8)(x + w), (u8)(y - 2)) == CellType_Empty
        && Map_GetCell((u8)(x + w + 1), (u8)(y - 1)) == CellType_Empty)
        result.mask &= (u8)~(NB_DEMOLISH_NORTH | NB_DEMOLISH_EAST);
    if (Map_GetCell((u8)(x + w), (u8)(y + h + 1)) == CellType_Empty
        && Map_GetCell((u8)(x + w + 1), (u8)(y + h)) == CellType_Empty)
        result.mask &= (u8)~(NB_DEMOLISH_SOUTH | NB_DEMOLISH_EAST);
    if (Map_GetCell((u8)(x - 1), (u8)(y + h + 1)) == CellType_Empty
        && Map_GetCell((u8)(x - 2), (u8)(y + h)) == CellType_Empty)
        result.mask &= (u8)~(NB_DEMOLISH_SOUTH | NB_DEMOLISH_WEST);

    hasNorthWall = (u8)(Map_GetCell(x, (u8)(y - 1)) != CellType_Empty
                     && Map_GetCell((u8)(x + w - 1), (u8)(y - 1)) != CellType_Empty);
    hasEastWall  = (u8)(Map_GetCell((u8)(x + w), y) != CellType_Empty
                     && Map_GetCell((u8)(x + w), (u8)(y + h - 1)) != CellType_Empty);
    hasSouthWall = (u8)(Map_GetCell(x, (u8)(y + h)) != CellType_Empty
                     && Map_GetCell((u8)(x + w - 1), (u8)(y + h)) != CellType_Empty);
    hasWestWall  = (u8)(Map_GetCell((u8)(x - 1), y) != CellType_Empty
                     && Map_GetCell((u8)(x - 1), (u8)(y + h - 1)) != CellType_Empty);

    if (!hasNorthWall)
        result.mask &= (u8)~(NB_DEMOLISH_NORTH | NB_DEMOLISH_EAST | NB_DEMOLISH_WEST);
    if (!hasEastWall)
        result.mask &= (u8)~(NB_DEMOLISH_NORTH | NB_DEMOLISH_EAST | NB_DEMOLISH_SOUTH);
    if (!hasSouthWall)
        result.mask &= (u8)~(NB_DEMOLISH_EAST | NB_DEMOLISH_SOUTH | NB_DEMOLISH_WEST);
    if (!hasWestWall)
        result.mask &= (u8)~(NB_DEMOLISH_NORTH | NB_DEMOLISH_SOUTH | NB_DEMOLISH_WEST);

    for (i = x; i < x + w; i++) {
        if (Map_GetCell((u8)i, (u8)(y - 1)) == CellType_Empty) {
            result.mask |= NB_HAS_NORTH; result.count++;
        }
        if (Map_GetCell((u8)i, (u8)(y + h)) == CellType_Empty) {
            result.mask |= NB_HAS_SOUTH; result.count++;
        }

        if (y > 1 && Map_GetCell((u8)i, (u8)(y - 2)) != CellType_Empty)
            result.mask &= (u8)~NB_DEMOLISH_NORTH;
        if (y + h + 1 < MAP_HEIGHT - 1 && Map_GetCell((u8)i, (u8)(y + h + 1)) != CellType_Empty)
            result.mask &= (u8)~NB_DEMOLISH_SOUTH;
    }
    for (j = y; j < y + h; j++) {
        if (Map_GetCell((u8)(x - 1), (u8)j) == CellType_Empty) {
            result.mask |= NB_HAS_WEST; result.count++;
        }
        if (Map_GetCell((u8)(x + w), (u8)j) == CellType_Empty) {
            result.mask |= NB_HAS_EAST; result.count++;
        }

        if (x > 1 && Map_GetCell((u8)(x - 2), (u8)j) != CellType_Empty)
            result.mask &= (u8)~NB_DEMOLISH_WEST;
        if (x + w + 1 < MAP_WIDTH - 1 && Map_GetCell((u8)(x + w + 1), (u8)j) != CellType_Empty)
            result.mask &= (u8)~NB_DEMOLISH_EAST;
    }

    return result;
}

#define MIN_ROOM_SIZE 3
#define MAX_ROOM_SIZE 8
#define DEMOLISH_WALL_CHANCE 20

static void SplitMap(u8 x, u8 y, u8 w, u8 h, u8 doorX, u8 doorY)
{
    u8 splitVertical = 0;
    u8 splitHorizontal = 0;

    if (doorX != 0 && doorY != 0) Map_SetCell(doorX, doorY, CellType_Empty);

    if (w > MAX_ROOM_SIZE || h > MAX_ROOM_SIZE) {
        if (w < h) splitVertical = 1;
        else splitHorizontal = 1;
    }

    if (splitVertical) {
        u8 splitSize;
        u8 splitAttempts = 255;
        do {
            splitSize = (u8)(RandomBelow((u8)(h - 2 * MIN_ROOM_SIZE)) + MIN_ROOM_SIZE);
            splitAttempts--;
        } while (splitAttempts > 0
                 && (Map_GetCell((u8)(x - 1), (u8)(y + splitSize)) == CellType_Empty
                  || Map_GetCell((u8)(x + w), (u8)(y + splitSize)) == CellType_Empty
                  || Map_GetCell((u8)(x - 1), (u8)(y + splitSize - 1)) == CellType_Empty
                  || Map_GetCell((u8)(x + w), (u8)(y + splitSize - 1)) == CellType_Empty
                  || Map_GetCell((u8)(x - 1), (u8)(y + splitSize + 1)) == CellType_Empty
                  || Map_GetCell((u8)(x + w), (u8)(y + splitSize + 1)) == CellType_Empty));

        if (splitAttempts > 0) {
            u8 splitDoorX = (u8)(x + RandomBelow((u8)(w - 2)) + 1);
            u8 splitDoorY = (u8)(y + splitSize);
            u8 i;

            for (i = x; i < x + w; i++) Map_SetCell(i, (u8)(y + splitSize), CellType_BrickWall);

            SplitMap(x, (u8)(y + splitSize + 1), w, (u8)(h - splitSize - 1), splitDoorX, splitDoorY);
            SplitMap(x, y, w, splitSize, splitDoorX, splitDoorY);
            return;
        }
    } else if (splitHorizontal) {
        u8 splitSize;
        u8 splitAttempts = 255;
        do {
            splitSize = (u8)(RandomBelow((u8)(w - 2 * MIN_ROOM_SIZE)) + MIN_ROOM_SIZE);
            splitAttempts--;
        } while (splitAttempts > 0
                 && (Map_GetCell((u8)(x + splitSize), (u8)(y - 1)) == CellType_Empty
                  || Map_GetCell((u8)(x + splitSize), (u8)(y + h)) == CellType_Empty
                  || Map_GetCell((u8)(x + splitSize - 1), (u8)(y - 1)) == CellType_Empty
                  || Map_GetCell((u8)(x + splitSize - 1), (u8)(y + h)) == CellType_Empty
                  || Map_GetCell((u8)(x + splitSize + 1), (u8)(y - 1)) == CellType_Empty
                  || Map_GetCell((u8)(x + splitSize + 1), (u8)(y + h)) == CellType_Empty));

        if (splitAttempts > 0) {
            u8 splitDoorX = (u8)(x + splitSize);
            u8 splitDoorY = (u8)(y + RandomBelow((u8)(h - 2)) + 1);
            u8 j;

            for (j = y; j < y + h; j++) Map_SetCell((u8)(x + splitSize), j, CellType_BrickWall);

            SplitMap((u8)(x + splitSize + 1), y, (u8)(w - splitSize - 1), h, splitDoorX, splitDoorY);
            SplitMap(x, y, splitSize, h, splitDoorX, splitDoorY);
            return;
        }
    }

    {
        NeighbourInfo neighbours = GetRoomNeighbourMask(x, y, w, h);
        u8 i, j;

        if ((neighbours.mask & NB_DEMOLISH_NORTH) && RandomBelow(100) < DEMOLISH_WALL_CHANCE) {
            for (i = 0; i < w; i++) Map_SetCell((u8)(x + i), (u8)(y - 1), CellType_Empty);
        } else if ((neighbours.mask & NB_DEMOLISH_WEST) && RandomBelow(100) < DEMOLISH_WALL_CHANCE) {
            for (j = 0; j < h; j++) Map_SetCell((u8)(x - 1), (u8)(y + j), CellType_Empty);
        } else if ((neighbours.mask & NB_DEMOLISH_SOUTH) && RandomBelow(100) < DEMOLISH_WALL_CHANCE) {
            for (i = 0; i < w; i++) Map_SetCell((u8)(x + i), (u8)(y + h), CellType_Empty);
        } else if ((neighbours.mask & NB_DEMOLISH_EAST) && RandomBelow(100) < DEMOLISH_WALL_CHANCE) {
            for (j = 0; j < h; j++) Map_SetCell((u8)(x + w), (u8)(y + j), CellType_Empty);
        }

        /* Add four cornering columns in large square rooms */
        if (w == h && w >= 7 && h >= 7) {
            Map_SetCell((u8)(x + 1), (u8)(y + 1), CellType_BrickWall);
            Map_SetCell((u8)(x + w - 2), (u8)(y + 1), CellType_BrickWall);
            Map_SetCell((u8)(x + w - 2), (u8)(y + h - 2), CellType_BrickWall);
            Map_SetCell((u8)(x + 1), (u8)(y + h - 2), CellType_BrickWall);
        }
    }
}

/* Largest all-empty square anchored at each cell, rolling column DP.
 * Returns the size and writes the anchor; equivalent to the original quintuple
 * loop including its "first maximum in (i,j) order wins" tie break. */
static u8 FindLargestOpenSquare(u8 *outX, u8 *outY)
{
    static u8 col[MAP_HEIGHT + 2];
    s8 i, j;
    u8 best = 0, bestX = 0, bestY = 0;
    u8 belowVal;

    for (i = 0; i < (s8)(MAP_HEIGHT + 2); i++) col[i] = 0;

    for (i = MAP_WIDTH - 1; i >= 0; i--) {
        belowVal = 0;                      /* S[i][MAP_HEIGHT] is out of bounds */
        for (j = MAP_HEIGHT - 1; j >= 0; j--) {
            u8 v;

            if (Map_GetCellSafe((u8)i, (u8)j) != CellType_Empty) {
                v = 0;
            } else {
                u8 right = col[j];         /* S[i+1][j]   */
                u8 diag  = col[j + 1];     /* S[i+1][j+1] */
                u8 m = belowVal;           /* S[i][j+1]   */
                if (right < m) m = right;
                if (diag < m) m = diag;
                v = (u8)(m + 1);
                if (v > MAP_HEIGHT - 1) v = MAP_HEIGHT - 1;
            }

            col[j] = v;
            belowVal = v;

            /* The original only considers i in [1, width-2], j in [0, height-2] */
            if (i >= 1 && i <= MAP_WIDTH - 2 && j <= MAP_HEIGHT - 2) {
                if (v >= best && v > 0) { best = v; bestX = (u8)i; bestY = (u8)j; }
            }
        }
    }

    *outX = bestX;
    *outY = bestY;
    return best;
}

void MapGenerator_Generate(void)
{
    u8 playerStartX = 1;
    u8 playerStartY = 1;
    s16 x, y;

    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            u8 isEdge = (u8)(x == 0 || y == 0 || x == MAP_WIDTH - 1 || y == MAP_HEIGHT - 1);
            Map_SetCell((u8)x, (u8)y, (u8)(isEdge ? CellType_BrickWall : CellType_Empty));
        }
    }

    SplitMap(1, 1, MAP_WIDTH - 2, MAP_HEIGHT - 2, 0, 0);

    /* Find any big open spaces and put a donut shaped wall in them */
    {
        u8 hasOpenSpaces = 1;

        while (hasOpenSpaces) {
            u8 sx, sy;
            u8 space = FindLargestOpenSquare(&sx, &sy);

            hasOpenSpaces = 0;

            if (space > 6) {
                u8 n;
                hasOpenSpaces = 1;
                Menu_LoadingStep();

                for (n = 2; n < space - 2; n++) {
                    Map_SetCell((u8)(sx + n), (u8)(sy + 2), CellType_BrickWall);
                    Map_SetCell((u8)(sx + 2), (u8)(sy + n), CellType_BrickWall);
                    Map_SetCell((u8)(sx + n), (u8)(sy + space - 3), CellType_BrickWall);
                    Map_SetCell((u8)(sx + space - 3), (u8)(sy + n), CellType_BrickWall);
                }
            }
        }
    }

    /* Add torches */
    {
        u8 attempts = 255;
        u8 toSpawn = 64;
        u8 minSpacing = 3;

        while (attempts > 0 && toSpawn > 0) {
            u8 cx = RandomCellX();
            u8 cy = RandomCellY();

            Menu_LoadingPoll();

            if (Map_GetCellSafe(cx, cy) == CellType_Empty) {
                NeighbourInfo info = GetCellNeighbourInfo(cx, cy);

                if (info.count == 1 && !IsCellTypeNear(cx, cy, CellType_Torch, minSpacing)) {
                    Map_SetCell(cx, cy, CellType_Torch);
                    toSpawn--;
                    attempts = 255;
                }
            }
            attempts--;
        }
    }

    /* Add monsters */
    {
        u8 attempts = 255;
        u8 monstersToSpawn = MAX_ENEMIES;
        CellType monsterType = CellType_Monster;
        u8 minSpacing = 3;

        while (attempts > 0 && monstersToSpawn > 0) {
            u8 cx = RandomCellX();
            u8 cy = RandomCellY();

            Menu_LoadingPoll();

            if (Map_GetCellSafe(cx, cy) == CellType_Empty
                && Map_IsClearLine((s16)(cx * CELL_SIZE + CELL_SIZE / 2),
                                   (s16)(cy * CELL_SIZE + CELL_SIZE / 2),
                                   (s16)(playerStartX * CELL_SIZE + CELL_SIZE / 2),
                                   (s16)(playerStartY * CELL_SIZE + CELL_SIZE / 2)) == 0) {
                NeighbourInfo info = GetCellNeighbourInfo(cx, cy);
                if (info.count == 0 && !IsCellTypeNear(cx, cy, monsterType, minSpacing)) {
                    Map_SetCell(cx, cy, monsterType);
                    monstersToSpawn--;
                    attempts = 255;
                }
            }
            attempts--;
        }
    }

    /* Add blocking decorations (urns) */
    {
        u8 attempts = 255;
        u8 toSpawn = 255;
        CellType cellType = CellType_Urn;
        u8 minSpacing = 3;

        while (attempts > 0 && toSpawn > 0) {
            u8 cx = RandomCellX();
            u8 cy = RandomCellY();

            Menu_LoadingPoll();

            if (Map_GetCellSafe(cx, cy) == CellType_Empty) {
                NeighbourInfo info = GetCellNeighbourInfo(cx, cy);

                if (NeighbourInfo_IsCorner(&info)
                    && !IsCellTypeNear(cx, cy, cellType, minSpacing)) {
                    Map_SetCell(cx, cy, cellType);
                    toSpawn--;
                    attempts = 255;
                }
            }
            attempts--;
        }
    }

    /* Add entrance and exit */
    Map_SetCell(1, 1, CellType_Entrance);
    Map_SetCell(MAP_WIDTH - 3, MAP_HEIGHT - 3, CellType_Exit);

    /* Add sign */
    if (gameFloor == 1) Map_SetCell(2, 2, CellType_Sign);

    /* Add treasure / items */
    {
        u16 attempts = 65535;
        u8 toSpawn = 8;
        CellType cellType = CellType_Chest;
        u8 minSpacing = 3;
        u8 minExitSpacing = 6;

        while (attempts > 0 && toSpawn > 0) {
            u8 cx = RandomCellX();
            u8 cy = RandomCellY();

            Menu_LoadingPoll();

            switch (RandomBelow(5)) {
            case 0: cellType = CellType_Potion; break;
            case 1: cellType = CellType_Coins;  break;
            case 2: cellType = CellType_Chest;  break;
            case 3: cellType = CellType_Crown;  break;
            case 4: cellType = CellType_Scroll; break;
            }

            if (Map_GetCellSafe(cx, cy) == CellType_Empty) {
                NeighbourInfo info = GetCellNeighbourInfo(cx, cy);

                if (info.count == 1
                    && !IsCellTypeNear(cx, cy, cellType, minSpacing)
                    && !IsCellTypeNear(cx, cy, CellType_Entrance, minExitSpacing)
                    && !IsCellTypeNear(cx, cy, CellType_Exit, minExitSpacing)) {
                    Map_SetCell(cx, cy, cellType);
                    toSpawn--;
                    attempts = 255;
                }
            }
            attempts--;
        }
    }
}
