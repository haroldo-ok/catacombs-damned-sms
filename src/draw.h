#ifndef DRAW_H
#define DRAW_H

#include "defines.h"

#define WITH_IMAGE_TEXTURES 0
#define WITH_VECTOR_TEXTURES 1
#define WITH_TEXTURES (WITH_IMAGE_TEXTURES || WITH_VECTOR_TEXTURES)
#define WITH_SPRITE_OUTLINES 1

typedef struct {
    s16 x, y;
    u8  angle;
    s16 rotCos, rotSin;
    s16 clipCos, clipSin;
    u8  cellX, cellY;
    s8  tilt;
    s8  bob;
    u8  shakeTime;
} Camera;

/* enum class DrawableType */
#define DrawableType_Sprite         0
#define DrawableType_ParticleSystem 1

/* enum class AnchorType */
#define AnchorType_Floor       0
#define AnchorType_Center      1
#define AnchorType_BelowCenter 2
#define AnchorType_Ceiling     3
typedef u8 AnchorType;

typedef struct {
    const void *ptr;        /* const u16* spriteData  or  ParticleSystem*     */
    u8 type : 1;
    u8 invert : 1;
    s8 x;
    s8 y;
    u8 halfSize;
    u8 inverseCameraDistance;
} QueuedDrawable;

extern Camera camera;
/* fixed, page aligned addresses - see the comment in src/draw.c */
extern u8 __at(0xD200) wBuffer[DISPLAY_WIDTH];
extern s8 __at(0xD280) horizonBuffer[DISPLAY_WIDTH];
extern u8 globalRenderFrame;

void Renderer_Render(void);
void Renderer_DrawObject(const u16 *spriteData, s16 x, s16 y, u8 scale,
                         AnchorType anchor, u8 invert);
QueuedDrawable *Renderer_CreateQueuedDrawable(u8 inverseCameraDistance);
s8   Renderer_GetHorizon(s16 x);
u8   Renderer_TransformAndCull(s16 worldX, s16 worldY, s16 *outScreenX, s16 *outScreenW);
void Renderer_DrawScaled(const u16 *data, s8 x, s8 y, u8 halfSize,
                         u8 inverseCameraDistance, u8 invert);

#endif
