#ifndef MAP_H
#define MAP_H

#include "defines.h"

/* CellType - was an `enum class uint8_t` */
#define CellType_Empty        0
#define CellType_Monster      1
#define CellType_Torch        2
#define CellType_Entrance     3
#define CellType_Exit         4
#define CellType_Potion       5
#define CellType_Coins        6
#define CellType_Crown        7
#define CellType_Scroll       8
#define CellType_Urn          9
#define CellType_Chest        10
#define CellType_ChestOpened  11
#define CellType_Sign         12
#define CellType_BrickWall    13

#define CellType_FirstCollidableCell CellType_Urn
#define CellType_FirstSolidCell      CellType_BrickWall

typedef u8 CellType;

#define MAP_WIDTH  32
#define MAP_HEIGHT 24

extern u8 mapLevel[MAP_WIDTH * MAP_HEIGHT / 2];

CellType Map_GetCell(u8 x, u8 y);
CellType Map_GetCellSafe(u8 x, u8 y);
void     Map_SetCell(u8 x, u8 y, CellType type);
u8       Map_IsSolid(u8 x, u8 y);
u8       Map_IsBlocked(u8 x, u8 y);
u8       Map_IsClearLine(s16 x1, s16 y1, s16 x2, s16 y2);
void     Map_DrawMinimap(void);
void     Map_DebugDraw(void);

#define Map_IsBlockedAtWorldPosition(x, y) Map_IsBlocked((u8)((x) >> 8), (u8)((y) >> 8))

#endif
