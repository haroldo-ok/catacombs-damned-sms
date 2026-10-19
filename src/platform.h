/* ---------------------------------------------------------------------------
 * Platform layer - the only part of the game that knows about the SMS.
 * Mirrors the Platform:: interface of the original Arduboy build.
 * ------------------------------------------------------------------------- */
#ifndef PLATFORM_H
#define PLATFORM_H

#include "defines.h"

#define SCREEN_BUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_PAGES)

extern u8 screenBuffer[SCREEN_BUFFER_SIZE];

/* src/prims.s - hand written hot paths.  Platform_DrawWallColumn draws one
 * wall column (vertical line in wallPattern plus the two black cap pixels)
 * in a single call; see the comment at the top of prims.s. */
extern u8 wallPattern;
void Platform_PutPixelBlack(u8 x, u8 y);
void Platform_DrawWallColumn(u8 x, u16 y0y1);
extern u8 prevBuffer[SCREEN_BUFFER_SIZE];
extern u16 blitTileCount;

/* The 2x2 magnified picture: 26x16 tiles at screen tile (3,4) */
#define VIEW_TILES_X (DISPLAY_WIDTH / 4)
#define VIEW_TILES_Y (DISPLAY_HEIGHT / 4)
#define VIEW_ORIGIN_TX ((32 - VIEW_TILES_X) / 2)
#define VIEW_ORIGIN_TY ((24 - VIEW_TILES_Y) / 2)

void Platform_Init(void);
void Platform_Present(void);      /* push dirty tiles  (blit.s) */
void Platform_PresentFull(void);  /* push every tile   (blit.s) */
void Platform_WaitFrame(void);

void Platform_PollInput(void);    /* once per game frame, from main()  */
u8   Platform_GetInput(void);
void Platform_ConsumeInput(void); /* forget buttons held across a state change */
extern u8 inputState;
void Platform_SetLED(u8 r, u8 g, u8 b);
#define Platform_GetScreenBuffer() (screenBuffer)

void Platform_PlaySound(const u16 *pattern);
u8   Platform_IsAudioEnabled(void);
void Platform_SetAudioEnabled(u8 enabled);
void Platform_AudioTick(void);
void Platform_ExpectLoadDelay(void);

void Platform_FillScreen(u8 col);
void Platform_PutPixel(u8 x, u8 y, u8 colour);
void Platform_DrawSprite(s16 x, s16 y, const u8 *bitmap, u8 frame);
void Platform_DrawVLine(u8 x, s8 y0, s8 y1, u8 pattern);
void Platform_DrawBackground(void);

extern const u16 pageOffsets[DISPLAY_PAGES];
extern const u8  pixelMasks[8];

#endif /* PLATFORM_H */
