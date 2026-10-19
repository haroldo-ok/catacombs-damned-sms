/* ---------------------------------------------------------------------------
 * Renderer - a direct port of Arduboy3D's Draw.cpp.
 *
 * Differences from the original, all forced by the narrower display:
 *   - every hard coded 128 (framebuffer page stride) is DISPLAY_WIDTH
 *   - `(y & 0x38) << 4` page offsets become pageOffsets[y >> 3]
 *   - DrawBackground fills DISPLAY_WIDTH*4 bytes per half instead of 512
 * The maths, the clipping, the sorting and the texturing are untouched.
 * ------------------------------------------------------------------------- */
#include "defines.h"
#include "draw.h"
#include "platform.h"
#include "fixedmath.h"
#include "fastmath.h"
#include "gamedata.h"
#include "map.h"
#include "game.h"
#include "player.h"
#include "enemy.h"
#include "projectile.h"
#include "particle.h"
#include "font.h"

#if WITH_SPRITE_OUTLINES
#define DrawScaledInner DrawScaledOutline
#else
#define DrawScaledInner DrawScaledNoOutline
#endif

Camera camera;
/* These two live at a fixed, page aligned address because the assembly
 * column loop in src/hot.s forms  &wBuffer[xi]  as  (0xD2 << 8) | xi  and
 * &horizonBuffer[xi] as the same page plus 0x80.  horizonBuffer MUST stay at
 * wBuffer + 0x80 and both must stay inside one 256 byte page; the link rule
 * in the Makefile checks the two addresses after every build. */
/* Inputs and outputs of WallColumnLoop (src/hot.s).  Globals rather than
 * arguments: the loop needs eleven live values and SDCC would spill them all
 * into the IX frame.  Set WALL_COLUMN_LOOP_IN_C below to build the original
 * C loop instead - the two are kept behaviourally identical and the golden
 * screenshots in tests/golden.txt compare them pixel for pixel. */
#define WALL_COLUMN_LOOP_IN_C 0
u8  wcXi, wcW, wcDw, wcXEnd, wcBlankL, wcBlankR, wcShade;
u8  wcClipL, wcClipR, wcCapLim, wcHorizon, wcWbv;
s8  wcWstep;
s16 wcWerror, wcDx;
void WallColumnLoop(void);

u8 __at(0xD200) wBuffer[DISPLAY_WIDTH];
s8 __at(0xD280) horizonBuffer[DISPLAY_WIDTH];
u8 globalRenderFrame = 0;
u8 numBufferSlicesFilled = 0;   /* read/written by src/hot.s */
static QueuedDrawable queuedDrawables[MAX_QUEUED_DRAWABLES];
static u8 numQueuedDrawables = 0;

static const u8 scaleDrawWriteMasks[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

static const u16 scaleDrawReadMasks[16] = {
    1, 2, 4, 8, 16, 32, 64, 128,
    256, 512, 1024, 2048, 4096, 8192, 16384, 32768u
};

u8   isFrustrumClipped(u8 x, u8 y);     /* src/hot.s */
void DrawBackground(void);
void DrawWeapon(void);
void DrawHUD(void);
void DrawDamageIndicator(void);
void DrawBar(u8 *screenPtr, const u8 *iconData, u8 amount, u8 max);
void DrawCells(void);
void DrawCell(u8 x, u8 y);
void RenderQueuedDrawables(void);
void QueueSprite(const u16 *data, s8 x, s8 y, u8 halfSize,
                        u8 inverseCameraDistance, u8 invert);

/* ------------------------------------------------------------------------ */
#if WITH_VECTOR_TEXTURES
/* col was always COLOUR_BLACK; the parameter is kept for call compatibility
 * with the original and ignored - the plot goes through Platform_PutPixelBlack. */
void DrawWallLine(s16 x1, s16 y1, s16 x2, s16 y2,
                         u8 clipLeft, u8 clipRight, u8 col)
{
    (void)col;
    s16 dx, yerror, y, dy;
    s8 ystep;
    s16 x;

    if (x1 > x2) return;

    if (y1 < 0) {
        if (y2 < 0) return;
        if (y2 != y1) x1 = (s16)(x1 + (s16)((0 - y1) * (x2 - x1) / (y2 - y1)));
        y1 = 0;
    }
    if (y2 > DISPLAY_HEIGHT - 1) {
        if (y1 > DISPLAY_HEIGHT - 1) return;
        if (y2 != y1)
            x2 = (s16)(x2 + (s16)((((DISPLAY_HEIGHT - 1) - y2) * (x1 - x2)) / (y1 - y2)));
        y2 = DISPLAY_HEIGHT - 1;
    }

    if (x1 < clipLeft) {
        if (x2 != x1) y1 = (s16)(y1 + (s16)(((clipLeft - x1) * (y2 - y1)) / (x2 - x1)));
        x1 = clipLeft;
    }

    /* The column walk below indexes with a u8, so bail out on the two cases
     * where x1/x2 do not fit in one.  Neither draws anything: PutPixelBlack
     * rejects x >= DISPLAY_WIDTH and the walk stops at clipRight anyway, so
     * this is exactly what the s16 version drew - but a wrapped x1 (352 -> 96
     * for a wall edge far off to the right) would put a bogus column inside
     * the loop range, and with dx == 0 the inner Bresenham loop, which only
     * ever adds dx to yerror, would spin forever. */
    if (x1 >= DISPLAY_WIDTH) return;
    if (x2 < 0) return;

    dx = (s16)(x2 - x1);
    yerror = dx / 2;
    y = y1;

    if (y1 < y2) { dy = (s16)(y2 - y1); ystep = 1; }
    else         { dy = (s16)(y1 - y2); ystep = -1; }

    /* same story as DrawWallSegment: x is >= 0 and < 104 here */
    {
        u8 xi = (u8)x1;
        const s8 *hb = &horizonBuffer[xi];
        const u8 xStop = (u8)(x2 < (s16)clipRight ? (u8)x2 : clipRight);
        const u8 xEnd = (u8)((x2 >= 0 && x2 < DISPLAY_WIDTH) ? (u8)x2 : 0xff);

        for (; xi <= xStop; xi++, hb++) {
            const s8 horizon = (s8)(*hb - HORIZON);

            Platform_PutPixelBlack(xi, (u8)(horizon + y));

            yerror = (s16)(yerror - dy);

            while (yerror < 0) {
                y = (s16)(y + ystep);
                yerror = (s16)(yerror + dx);

                if (yerror < 0) Platform_PutPixelBlack(xi, (u8)(horizon + y));
                if (xi == xEnd && y == y2) break;
            }
        }
    }
}
#endif

/* ------------------------------------------------------------------------ */
void DrawWallSegment(const u8 *texture, s16 x1, s16 w1, s16 x2, s16 w2,
                            u8 u1clip, u8 u2clip, u8 edgeLeft, u8 edgeRight,
                            u8 shadeEdge)
{
    s16 dx, werror;
    u8 w, dw;
    s8 wstep;
    u8 segmentClipLeft, segmentClipRight;
    s16 x;

    if (x1 < 0) {
#if WITH_TEXTURES
        mdMul = (s16)((s16)u2clip - (s16)u1clip);
        u1clip = (u8)(u1clip + MulDivS((s16)(0 - x1), (s16)(x2 - x1)));
#endif
        mdMul = (s16)(w2 - w1);
        w1 = (s16)(w1 + MulDivS((s16)(0 - x1), (s16)(x2 - x1)));
        x1 = 0;
        edgeLeft = 0;
    }

    /* w is a half wall height in pixels: w = 6656 / viewZ with viewZ clamped
     * to CLIP_PLANE (32) by DrawWall, so it cannot exceed 208.  Keeping it in
     * a byte takes all of the column loop's height arithmetic off SDCC's
     * 16 bit paths.  The two clamps below can never fire; they are there so a
     * future change to the clip plane degrades instead of wrapping. */
    if (w1 > 255) w1 = 255;
    if (w2 > 255) w2 = 255;

    dx = (s16)(x2 - x1);
    werror = dx / 2;
    w = (u8)w1;

    if (w1 < w2) { dw = (u8)(w2 - w1); wstep = 1; }
    else         { dw = (u8)(w1 - w2); wstep = -1; }

    segmentClipLeft = (u8)x1;
    segmentClipRight = (u8)(x2 < DISPLAY_WIDTH ? (u8)x2 : DISPLAY_WIDTH - 1);

    /* x1 is >= 0 by now (it was clamped above), so the column index fits in a
     * byte and the two per-column arrays can be walked with pointers - SDCC
     * was spilling the 16-bit index and recomputing both addresses for every
     * access, which was most of this function's cost. */
    {
        u8 xi = (u8)x1;
        const u8 xLeft = xi;
        /* the column where the segment ends, or 0xff when it ends off-screen */
        const u8 xEnd = (u8)((x2 >= 0 && x2 < DISPLAY_WIDTH) ? (u8)x2 : 0xff);
        /* the two columns that get a blanked slice, or 0xff for "no such
         * column" - xi only ever reaches 103, so 0xff never matches */
        const u8 blankL = edgeLeft ? xLeft : 0xff;
        const u8 blankR = edgeRight ? xEnd : 0xff;

#if WALL_COLUMN_LOOP_IN_C
        u8 *wb = &wBuffer[xi];
        const s8 *hb = &horizonBuffer[xi];

        for (; xi < DISPLAY_WIDTH; xi++, wb++, hb++) {
            const u8 wbv = *wb;
            const u8 drawSlice = (u8)(wbv < w);
            const s8 horizon = *hb;

            if (drawSlice) {
                u8 sliceMask = 0xff;
                s8 extent;

                if (xi == blankL || xi == blankR) sliceMask = 0x00;
                else if (shadeEdge && (xi & 1) == 0) sliceMask = 0x55;

                extent = (s8)(w > 64 ? 64 : w);
                wallPattern = sliceMask;
                Platform_DrawWallColumn(xi,
                    (u16)(((u16)(u8)(horizon + extent) << 8) | (u8)(horizon - extent)));

                if (wbv == 0) numBufferSlicesFilled++;

                *wb = w;
            } else {
                if (xi == segmentClipLeft) {
                    segmentClipLeft++;
                } else if (xi < segmentClipRight) {
                    segmentClipRight = xi;
                    break;
                }
            }

            if (xi == xEnd) break;

            werror = (s16)(werror - dw);

            while (werror < 0) {
                w = (u8)(w + wstep);
                werror = (s16)(werror + dx);

                if (drawSlice && werror < 0 && w <= DISPLAY_HEIGHT / 2) {
                    Platform_PutPixelBlack(xi, (u8)(horizon + w - 1));
                    Platform_PutPixelBlack(xi, (u8)(horizon - w));
                }
            }
        }
#else
        wcXi = xi;          wcW = w;             wcDw = dw;
        wcWstep = wstep;    wcWerror = werror;   wcDx = dx;
        wcXEnd = xEnd;      wcBlankL = blankL;   wcBlankR = blankR;
        wcShade = shadeEdge;
        wcClipL = segmentClipLeft;               wcClipR = segmentClipRight;

        WallColumnLoop();

        segmentClipLeft = wcClipL;
        segmentClipRight = wcClipR;
#endif
    }

    if (segmentClipLeft == segmentClipRight) return;

#if WITH_VECTOR_TEXTURES
    {
        const u8 *texPtr;
        u8 numLines;

        if (w1 < MIN_TEXTURE_DISTANCE || w2 < MIN_TEXTURE_DISTANCE || !texture) return;
        if (u1clip == u2clip) return;

        texPtr = texture;
        numLines = *texPtr++;
        while (numLines) {
            u8 u1, v1, u2, v2;
            s16 outU1, outU2, interpw1, interpw2, outV1, outV2;

            numLines--;

            u1 = *texPtr++;
            v1 = *texPtr++;
            u2 = *texPtr++;
            v2 = *texPtr++;

            if (u2 < u1clip || u1 > u2clip) continue;

            if (u1 < u1clip) {
                if (u2 != u1) v1 = (u8)(v1 + (s16)((u1clip - u1) * (v2 - v1) / (u2 - u1)));
                u1 = u1clip;
            }
            if (u2 > u2clip) {
                if (u2 != u1) v2 = (u8)(v2 + (s16)((u2clip - u2) * (v1 - v2) / (u1 - u2)));
                u2 = u2clip;
            }

            u1 = (u8)((128 * (u1 - u1clip)) / (u2clip - u1clip));
            u2 = (u8)((128 * (u2 - u1clip)) / (u2clip - u1clip));

            /* dx is the *unclipped* segment width and can be far wider than
             * the screen, so this product needs 32 bits; u1,u2 and dx are all
             * non negative here, so a truncating divide matches the original
             * arithmetic shift exactly. */
            /* (u * dx) >> 7  ==  ((u * 2) * dx) >> 8, and dx is the unclipped
             * segment width so the product really does need 32 bits */
            outU1 = (s16)(MulShift8((s16)((s16)u1 << 1), dx) + x1);
            outU2 = (s16)(MulShift8((s16)((s16)u2 << 1), dx) + x1);

            interpw1 = (s16)(((u1 * (w2 - w1)) >> 7) + w1);
            interpw2 = (s16)(((u2 * (w2 - w1)) >> 7) + w1);

            outV1 = (s16)((interpw1 * v1) >> 6);
            outV2 = (s16)((interpw2 * v2) >> 6);

            DrawWallLine(outU1, (s16)(HORIZON - interpw1 + outV1),
                         outU2, (s16)(HORIZON - interpw2 + outV2),
                         segmentClipLeft, segmentClipRight, COLOUR_BLACK);
        }
    }
#endif
}

/* ------------------------------------------------------------------------ */
/* isFrustrumClipped runs for all 256 cells every frame and did four 16-bit
 * multiplies each - over 10% of the frame in SDCC's __mulint.  Both operands
 * of every product are known in advance: clipCos/clipSin are fixed for the
 * frame and the cell offset is always within +/-32, so the products are
 * tabulated once per frame by repeated addition. */
#define CLIP_TAB_BIAS 32
s16 clipCosTab[CLIP_TAB_BIAS * 2];   /* read by src/hot.s */
s16 clipSinTab[CLIP_TAB_BIAS * 2];

void BuildClipTables(void)
{
    s16 c = 0, s = 0;
    u8 n;
    for (n = CLIP_TAB_BIAS; n < CLIP_TAB_BIAS * 2; n++) {
        clipCosTab[n] = c; c = (s16)(c + camera.clipCos);
        clipSinTab[n] = s; s = (s16)(s + camera.clipSin);
    }
    c = 0; s = 0;
    n = CLIP_TAB_BIAS;
    while (n--) {
        c = (s16)(c - camera.clipCos);
        s = (s16)(s - camera.clipSin);
        clipCosTab[n] = c;
        clipSinTab[n] = s;
    }
}

/* isFrustrumClipped is hand written in src/hot.s (A = x, L = y). */

/* Wall corners always sit on cell boundaries, so  rotCos * (256*cx - camX)
 * splits exactly into  rotCos*cx  plus a constant for the frame (the first
 * term is a multiple of 256, so the >>8 distributes without rounding error).
 * Both parts are tabulated once per frame, turning the four multiplies of a
 * corner transform into four table reads. */
/* Four tables, read by TransformCorner in src/hot.s:
 *     viewZ = cornerAx[cx] - cornerBy[cy]
 *     viewX = cornerCx[cx] + cornerDy[cy]
 * The constant camera terms are folded in here (everything is mod 2^16, so
 * the result is bit-identical to adding them at transform time).  Returning
 * through two globals instead of two out-pointers, and from assembly, took a
 * corner transform from ~990 cycles to ~190. */
s16 cornerAx[MAP_WIDTH + 1];
s16 cornerBy[MAP_WIDTH + 1];
s16 cornerCx[MAP_WIDTH + 1];
s16 cornerDy[MAP_WIDTH + 1];
s16 tcvX, tcvZ;
void TransformCorner(u8 cx, u8 cy);

static void BuildCornerTables(void)
{
    s16 c = 0, s = 0;
    s16 k1, k2, k3, k4;
    u8 n;

    k1 = MulShift8(camera.rotCos, (s16)(-camera.x));
    k2 = MulShift8(camera.rotSin, (s16)(-camera.y));
    k3 = MulShift8(camera.rotSin, (s16)(-camera.x));
    k4 = MulShift8(camera.rotCos, (s16)(-camera.y));

    for (n = 0; n <= MAP_WIDTH; n++) {
        cornerAx[n] = (s16)(c + k1);
        cornerDy[n] = (s16)(c + k4);
        cornerBy[n] = (s16)(s + k2);
        cornerCx[n] = (s16)(s + k3);
        c = (s16)(c + camera.rotCos);
        s = (s16)(s + camera.rotSin);
    }
}

void TransformToViewSpace(s16 x, s16 y, s16 *outX, s16 *outY)
{
    s16 relX = (s16)(x - camera.x);
    s16 relY = (s16)(y - camera.y);
    *outY = (s16)(MulShift8(camera.rotCos, relX) - MulShift8(camera.rotSin, relY));
    *outX = (s16)(MulShift8(camera.rotSin, relX) + MulShift8(camera.rotCos, relY));
}

void TransformToScreenSpace(s16 viewX, s16 viewZ, s16 *outX, s16 *outW)
{
    mdMul = NEAR_PLANE * CAMERA_SCALE;
    *outX = MulDivS(viewX, viewZ);
    *outW = (s16)((u16)(CELL_SIZE / 2 * NEAR_PLANE * CAMERA_SCALE) / (u16)viewZ);
    *outX = (s16)((DISPLAY_WIDTH / 2) + *outX);
}

void DrawWall(const u8 *texture, u8 cx1, u8 cy1, u8 cx2, u8 cy2,
                     u8 edgeLeft, u8 edgeRight, u8 shadeEdge)
{
    s16 viewX1, viewZ1, viewX2, viewZ2;
    s16 vx1, vx2, sx1, sx2, w1, w2;
    u8 u1 = 0, u2 = 128;

    TransformCorner(cx1, cy1);
    viewX1 = tcvX; viewZ1 = tcvZ;
    TransformCorner(cx2, cy2);
    viewX2 = tcvX; viewZ2 = tcvZ;

    /* Frustum cull */
    if (viewX2 < 0 && -2 * viewZ2 > viewX2) return;
    if (viewX1 > 0 && 2 * viewZ1 < viewX1) return;

    /* clip to the front pane */
    if ((viewZ1 < CLIP_PLANE) && (viewZ2 < CLIP_PLANE)) return;

    if (viewZ1 < CLIP_PLANE) {
        u1 = (u8)(u1 + (CLIP_PLANE - viewZ1) * (u2 - u1) / (viewZ2 - viewZ1));
        viewX1 = (s16)(viewX1 + (CLIP_PLANE - viewZ1) * (viewX2 - viewX1) / (viewZ2 - viewZ1));
        viewZ1 = CLIP_PLANE;
        edgeLeft = 0;
    } else if (viewZ2 < CLIP_PLANE) {
        u2 = (u8)(u2 + (CLIP_PLANE - viewZ2) * (u1 - u2) / (viewZ1 - viewZ2));
        viewX2 = (s16)(viewX2 + (CLIP_PLANE - viewZ2) * (viewX1 - viewX2) / (viewZ1 - viewZ2));
        viewZ2 = CLIP_PLANE;
        edgeRight = 0;
    }

    mdMul = NEAR_PLANE * CAMERA_SCALE;
    vx1 = MulDivS(viewX1, viewZ1);
    vx2 = MulDivS(viewX2, viewZ2);

    sx1 = (s16)((DISPLAY_WIDTH / 2) + vx1);
    sx2 = (s16)((DISPLAY_WIDTH / 2) + vx2 - 1);

    if (sx1 >= sx2 || sx2 <= 0 || sx1 >= DISPLAY_WIDTH) return;

    w1 = (s16)((u16)(CELL_SIZE / 2 * NEAR_PLANE * CAMERA_SCALE) / (u16)viewZ1);
    w2 = (s16)((u16)(CELL_SIZE / 2 * NEAR_PLANE * CAMERA_SCALE) / (u16)viewZ2);

    DrawWallSegment(texture, sx1, w1, sx2, w2, u1, u2, edgeLeft, edgeRight, shadeEdge);
}

/* ------------------------------------------------------------------------ */
#define SWAP16(a, b) do { s16 t_ = (a); (a) = (b); (b) = t_; } while (0)

void DrawFloorLineInner(s16 x0, s16 y0, s16 x1, s16 y1)
{
    u8 color = COLOUR_BLACK;
    u8 steep = (u8)(ABS(y1 - y0) > ABS(x1 - x0));
    s16 dx, dy, err;
    s8 ystep;

    if (steep) { SWAP16(x0, y0); SWAP16(x1, y1); }
    if (x0 > x1) { SWAP16(x0, x1); SWAP16(y0, y1); }

    dx = (s16)(x1 - x0);
    dy = ABS(y1 - y0);
    err = dx / 2;
    ystep = (s8)((y0 < y1) ? 1 : -1);

    for (; x0 <= x1; x0++) {
        if (steep) {
            if (y0 >= 0 && y0 < DISPLAY_WIDTH && x0 >= 0 && x0 < DISPLAY_HEIGHT
                && x0 > Renderer_GetHorizon(y0) + wBuffer[y0]
                && x0 > Renderer_GetHorizon(y0) + 8) {
                Platform_PutPixel((u8)y0, (u8)(x0 + horizonBuffer[y0] - HORIZON), color);
            }
        } else {
            if (x0 >= 0 && x0 < DISPLAY_WIDTH && y0 >= 0 && y0 < DISPLAY_HEIGHT
                && y0 > Renderer_GetHorizon(x0) + wBuffer[x0]
                && y0 > Renderer_GetHorizon(x0) + 8) {
                Platform_PutPixel((u8)x0, (u8)(y0 + horizonBuffer[x0] - HORIZON), color);
            }
        }

        err = (s16)(err - dy);
        if (err < 0) { y0 = (s16)(y0 + ystep); err = (s16)(err + dx); }
    }
}

void DrawFloorLine(s16 x1, s16 y1, s16 x2, s16 y2)
{
    s16 viewX1, viewZ1, viewX2, viewZ2;
    s16 vx1, vx2, sx1, sx2, w1, w2;

    TransformToViewSpace(x1, y1, &viewX1, &viewZ1);
    TransformToViewSpace(x2, y2, &viewX2, &viewZ2);

    if ((viewZ1 < CLIP_PLANE) && (viewZ2 < CLIP_PLANE)) return;

    if (viewZ1 < CLIP_PLANE) {
        viewX1 = (s16)(viewX1 + (CLIP_PLANE - viewZ1) * (viewX2 - viewX1) / (viewZ2 - viewZ1));
        viewZ1 = CLIP_PLANE;
    } else if (viewZ2 < CLIP_PLANE) {
        viewX2 = (s16)(viewX2 + (CLIP_PLANE - viewZ2) * (viewX1 - viewX2) / (viewZ1 - viewZ2));
        viewZ2 = CLIP_PLANE;
    }

    mdMul = NEAR_PLANE * CAMERA_SCALE;
    vx1 = MulDivS(viewX1, viewZ1);
    vx2 = MulDivS(viewX2, viewZ2);

    sx1 = (s16)((DISPLAY_WIDTH / 2) + vx1);
    sx2 = (s16)((DISPLAY_WIDTH / 2) + vx2 - 1);

    w1 = (s16)((u16)(CELL_SIZE / 2 * NEAR_PLANE * CAMERA_SCALE) / (u16)viewZ1);
    w2 = (s16)((u16)(CELL_SIZE / 2 * NEAR_PLANE * CAMERA_SCALE) / (u16)viewZ2);

    DrawFloorLineInner(sx1, (s16)(HORIZON + w1), sx2, (s16)(HORIZON + w2));
}

void Renderer_DrawFloorLines(void)
{
    s16 baseX = (s16)((player.x - CELL_SIZE * 10 / 2) & 0xff00);
    s16 baseY = (s16)((player.y - CELL_SIZE * 10 / 2) & 0xff00);
    s16 n;

    for (n = 0; n < 10; n++) {
        DrawFloorLine(baseX, (s16)(baseY + n * CELL_SIZE),
                      (s16)(baseX + CELL_SIZE * 10 - n * CELL_SIZE),
                      (s16)(baseY + CELL_SIZE * 10));
        DrawFloorLine(baseX, (s16)(baseY + n * CELL_SIZE),
                      (s16)(baseX + n * CELL_SIZE), baseY);
    }

    for (n = 1; n < 10; n++) {
        DrawFloorLine((s16)(baseX + n * CELL_SIZE), baseY,
                      (s16)(baseX + CELL_SIZE * 10),
                      (s16)(baseY + CELL_SIZE * 10 - n * CELL_SIZE));
        DrawFloorLine((s16)(baseX + n * CELL_SIZE), (s16)(baseY + 10 * CELL_SIZE),
                      (s16)(baseX + 10 * CELL_SIZE), (s16)(baseY + n * CELL_SIZE));
    }
}

/* ------------------------------------------------------------------------ */
/* DrawCell is called for all 256 cells of the draw window every frame, and
 * most of them are empty or behind the camera.  The cheap tests therefore run
 * in this wrapper, which needs no stack frame at all, and the body - which
 * SDCC gives a fat IX frame - is only entered for a cell that has something
 * in it.  The frustum test comes first because it rejects more cells than it
 * costs, and the old `Map_IsSolid(x, y)` before the wall code is now just a
 * compare on the cell type we already have. */
static void DrawCellContents(u8 x, u8 y, CellType cellType);

void DrawCell(u8 x, u8 y)
{
    CellType cellType;

    if (isFrustrumClipped(x, y)) return;

    cellType = Map_GetCellSafe(x, y);

    /* Empty and Monster draw nothing here (enemies are drawn separately) */
    if (cellType < CellType_Torch) return;

    DrawCellContents(x, y, cellType);
}

static void DrawCellContents(u8 x, u8 y, CellType cellType)
{
    s16 x1, y1, x2, y2;
    u8 blockedLeft, blockedRight, blockedUp, blockedDown;
    const u8 *texture;

    switch (cellType) {
    case CellType_Torch:
    {
        const u16 *torchSprite = (globalTickFrame & 4) ? torchSpriteData1 : torchSpriteData2;
        const u8 torchScale = 75;

        if (Map_IsSolid((u8)(x - 1), y))
            Renderer_DrawObject(torchSprite, (s16)(x * CELL_SIZE + CELL_SIZE / 7),
                                (s16)(y * CELL_SIZE + CELL_SIZE / 2), torchScale, AnchorType_Center, 0);
        else if (Map_IsSolid((u8)(x + 1), y))
            Renderer_DrawObject(torchSprite, (s16)(x * CELL_SIZE + 6 * CELL_SIZE / 7),
                                (s16)(y * CELL_SIZE + CELL_SIZE / 2), torchScale, AnchorType_Center, 0);
        else if (Map_IsSolid(x, (u8)(y - 1)))
            Renderer_DrawObject(torchSprite, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                                (s16)(y * CELL_SIZE + CELL_SIZE / 7), torchScale, AnchorType_Center, 0);
        else if (Map_IsSolid(x, (u8)(y + 1)))
            Renderer_DrawObject(torchSprite, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                                (s16)(y * CELL_SIZE + 6 * CELL_SIZE / 7), torchScale, AnchorType_Center, 0);
    }
        return;
    case CellType_Entrance:
        Renderer_DrawObject(entranceSpriteData, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                            (s16)(y * CELL_SIZE + CELL_SIZE / 2), 96, AnchorType_Ceiling, 0);
        return;
    case CellType_Exit:
        Renderer_DrawObject(exitSpriteData, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                            (s16)(y * CELL_SIZE + CELL_SIZE / 2), 96, AnchorType_Floor, 0);
        return;
    case CellType_Urn:
        Renderer_DrawObject(urnSpriteData, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                            (s16)(y * CELL_SIZE + CELL_SIZE / 2), 80, AnchorType_Floor, 0);
        return;
    case CellType_Potion:
        Renderer_DrawObject(potionSpriteData, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                            (s16)(y * CELL_SIZE + CELL_SIZE / 2), 64, AnchorType_Floor, 0);
        return;
    case CellType_Scroll:
        Renderer_DrawObject(scrollSpriteData, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                            (s16)(y * CELL_SIZE + CELL_SIZE / 2), 64, AnchorType_Floor, 0);
        return;
    case CellType_Coins:
        Renderer_DrawObject(coinsSpriteData, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                            (s16)(y * CELL_SIZE + CELL_SIZE / 2), 64, AnchorType_Floor, 0);
        return;
    case CellType_Crown:
        Renderer_DrawObject(crownSpriteData, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                            (s16)(y * CELL_SIZE + CELL_SIZE / 2), 64, AnchorType_Floor, 0);
        return;
    case CellType_Sign:
        Renderer_DrawObject(signSpriteData, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                            (s16)(y * CELL_SIZE + CELL_SIZE / 2), 80, AnchorType_Floor, 0);
        return;
    case CellType_Chest:
        Renderer_DrawObject(chestSpriteData, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                            (s16)(y * CELL_SIZE + CELL_SIZE / 2), 75, AnchorType_Floor, 0);
        return;
    case CellType_ChestOpened:
        Renderer_DrawObject(chestOpenSpriteData, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                            (s16)(y * CELL_SIZE + CELL_SIZE / 2), 75, AnchorType_Floor, 0);
        return;
    default:
        break;
    }

    if (cellType < CellType_FirstSolidCell) return;
    if (numBufferSlicesFilled >= DISPLAY_WIDTH) return;

    x1 = (s16)(x * CELL_SIZE);
    y1 = (s16)(y * CELL_SIZE);
    x2 = (s16)(x1 + CELL_SIZE);
    y2 = (s16)(y1 + CELL_SIZE);
    /* cell indices for the corner transform */
#define CX1 (x)
#define CY1 (y)
#define CX2 ((u8)(x + 1))
#define CY2 ((u8)(y + 1))

    blockedLeft  = Map_IsSolid((u8)(x - 1), y);
    blockedRight = Map_IsSolid((u8)(x + 1), y);
    blockedUp    = Map_IsSolid(x, (u8)(y - 1));
    blockedDown  = Map_IsSolid(x, (u8)(y + 1));

    texture = vectorTexture0;

    if (!blockedLeft && camera.x < x1)
        DrawWall(texture, CX1, CY1, CX1, CY2,
                 (u8)(!blockedUp && camera.y > y1), (u8)(!blockedDown && camera.y < y2), 1);

    if (!blockedDown && camera.y > y2)
        DrawWall(texture, CX1, CY2, CX2, CY2,
                 (u8)(!blockedLeft && camera.x > x1), (u8)(!blockedRight && camera.x < x2), 0);

    if (!blockedRight && camera.x > x2)
        DrawWall(texture, CX2, CY2, CX2, CY1,
                 (u8)(!blockedDown && camera.y < y2), (u8)(!blockedUp && camera.y > y1), 1);

    if (!blockedUp && camera.y < y1)
        DrawWall(texture, CX2, CY1, CX1, CY1,
                 (u8)(!blockedRight && camera.x < x2), (u8)(!blockedLeft && camera.x > x1), 0);
#undef CX1
#undef CY1
#undef CX2
#undef CY2
}

void DrawCells(void)
{
    const s8 MAP_BUFFER_WIDTH = 16;
    const s8 MAP_BUFFER_HEIGHT = 16;

    s16 cosAngle = FixedCos(camera.angle);
    s16 sinAngle = FixedSin(camera.angle);

    s8 bufferX = (s8)(((s16)(camera.x + cosAngle * 7) >> 8) - MAP_BUFFER_WIDTH / 2);
    s8 bufferY = (s8)(((s16)(camera.y + sinAngle * 7) >> 8) - MAP_BUFFER_WIDTH / 2);

    s8 xd, yd, x1, y1, x2, y2, x, y;

    if (bufferX < 0) bufferX = 0;
    if (bufferY < 0) bufferY = 0;
    if (bufferX > MAP_WIDTH - MAP_BUFFER_WIDTH) bufferX = MAP_WIDTH - MAP_BUFFER_WIDTH;
    if (bufferY > MAP_HEIGHT - MAP_BUFFER_HEIGHT) bufferY = MAP_HEIGHT - MAP_BUFFER_HEIGHT;

    if (camera.rotCos > 0) {
        x1 = bufferX;
        x2 = (s8)(x1 + MAP_BUFFER_WIDTH);
        xd = 1;
    } else {
        x2 = (s8)(bufferX - 1);
        x1 = (s8)(x2 + MAP_BUFFER_WIDTH);
        xd = -1;
    }
    if (camera.rotSin < 0) {
        y1 = bufferY;
        y2 = (s8)(y1 + MAP_BUFFER_HEIGHT);
        yd = 1;
    } else {
        y2 = (s8)(bufferY - 1);
        y1 = (s8)(y2 + MAP_BUFFER_HEIGHT);
        yd = -1;
    }

    if (ABS(camera.rotCos) < ABS(camera.rotSin)) {
        for (y = y1; y != y2; y = (s8)(y + yd))
            for (x = x1; x != x2; x = (s8)(x + xd))
                DrawCell((u8)x, (u8)y);
    } else {
        for (x = x1; x != x2; x = (s8)(x + xd))
            for (y = y1; y != y2; y = (s8)(y + yd))
                DrawCell((u8)x, (u8)y);
    }
}

/* ------------------------------------------------------------------------ */
/* ---------------------------------------------------------------------------
 * Scaled sprite blitter with 1px outlines.
 *
 * Semantically identical to the original per-pixel DrawScaledOutline, but
 * restructured to work a *framebuffer byte at a time*.  The original inner
 * loop costs ~160 Z80 cycles per pixel (SDCC spills every live variable to the
 * IX frame, and `lut[j >> shiftAmount]` is a run-time variable shift), which
 * made a close-up sprite ~1.1M cycles - 70% of the whole frame.
 *
 * For each screen column the three 16-bit source words (transparency, colour,
 * outline) are expanded once into bit arrays laid out exactly like the
 * framebuffer pages, and the outline rule is then evaluated eight pixels at a
 * time with plain byte logic:
 *
 *     set   =  opaque &  white
 *     clear = (opaque & ~white) | (~opaque & (outline | set<<1 | set>>1))
 *
 * The vertical neighbour taps are a 1-bit shift of the whole column, and the
 * horizontal ones are already folded into `leftRightOutlineColumn` by the
 * caller, exactly as before.
 * ------------------------------------------------------------------------- */
static u8 dsRun[16 * DISPLAY_PAGES];  /* source row -> framebuffer-page bits */
static u8 dsRunLo[16], dsRunHi[16];   /* byte range each source row touches  */
static u8 dsValid[DISPLAY_PAGES];     /* rows actually covered by the sprite */
static u8 dsOp[DISPLAY_PAGES], dsWh[DISPLAY_PAGES], dsOl[DISPLAY_PAGES];
static u8 dsUp[DISPLAY_PAGES], dsDn[DISPLAY_PAGES];

/* ---------------------------------------------------------------------------
 * Per-source-column caches.
 *
 * A sprite only ever has 16 distinct source columns, however many screen
 * columns it covers, so the (expensive) resampling of the 16 bit source words
 * into framebuffer-page bit arrays is done at most 16 times per sprite and
 * then reused.  Index 16 is a permanent all-transparent column, which removes
 * every "is there a neighbour" branch from the inner loop.
 *
 *   spSet[u] = opaque & white          -> pixels forced to 1
 *   spClr[u] = (opaque & ~white) | (~opaque & (set<<1 | set>>1))
 *                                      -> pixels forced to 0 by this column
 *   spNop[u] = ~opaque                 -> where a neighbour's outline shows
 * ------------------------------------------------------------------------- */
#define DSC_ZERO 16
static u8 spSet[17][DISPLAY_PAGES];
static u8 spClr[17][DISPLAY_PAGES];
static u8 spNop[17][DISPLAY_PAGES];
static u8 spReady[17];
static u8 dsTmpA[DISPLAY_PAGES], dsTmpB[DISPLAY_PAGES];
static u8 dsOpT[DISPLAY_PAGES], dsWhT[DISPLAY_PAGES];

static void ResampleWord(u16 w, u8 *out, u8 nBytes)
{
    const u8 *runRow = dsRun;
    const u8 *lo = dsRunLo;
    const u8 *hi = dsRunHi;
    u8 b;

    for (b = 0; b < nBytes; b++) out[b] = 0;

    for (;;) {
        if (w & 1) {
            u8 b0 = *lo;
            u8 e = *hi;
            if (b0 <= e) {
                const u8 *mp = runRow + b0;
                u8 *op = out + b0;
                u8 n = (u8)(e - b0 + 1);
                do { *op++ |= *mp++; } while (--n);
            }
        }
        w >>= 1;
        if (!w) break;
        runRow += DISPLAY_PAGES;
        lo++;
        hi++;
    }
}

static void EnsureColumn(u8 u, const u16 *data, u16 invertMask, u8 nBytes)
{
    u8 *pset, *pclr, *pnop;
    u8 b, carry;

    if (spReady[u]) return;
    spReady[u] = 1;

    pset = spSet[u];
    pclr = spClr[u];
    pnop = spNop[u];

    ResampleWord(data[u * 2], dsOpT, nBytes);
    ResampleWord((u16)(data[u * 2 + 1] ^ invertMask), dsWhT, nBytes);

    for (b = 0; b < nBytes; b++) {
        pset[b] = (u8)(dsOpT[b] & dsWhT[b]);
        pnop[b] = (u8)~dsOpT[b];
    }

    carry = 0;
    for (b = 0; b < nBytes; b++) {
        u8 v = pset[b];
        dsTmpA[b] = (u8)((v << 1) | carry);
        carry = (u8)(v >> 7);
    }
    carry = 0;
    b = nBytes;
    while (b--) {
        u8 v = pset[b];
        dsTmpB[b] = (u8)((v >> 1) | carry);
        carry = (u8)(v << 7);
    }

    for (b = 0; b < nBytes; b++)
        pclr[b] = (u8)((u8)(dsOpT[b] & ~dsWhT[b])
                     | (u8)(pnop[b] & (u8)(dsTmpA[b] | dsTmpB[b])));
}

static void BlendColumn(u8 *dst, u8 nBytes, u8 uMid, u8 uLeft, u8 uRight)
{
    const u8 *pset = spSet[uMid];
    const u8 *pclr = spClr[uMid];
    const u8 *pnop = spNop[uMid];
    const u8 *pl = spSet[uLeft];
    const u8 *pr = spSet[uRight];
    const u8 *pva = dsValid;

    do {
        u8 set = *pset++;
        u8 clr = (u8)((*pclr++ | (u8)(*pnop++ & (u8)(*pl++ | *pr++))) & *pva++);
        *dst = (u8)((*dst & ~(u8)(set | clr)) | set);
        dst += DISPLAY_WIDTH;
    } while (--nBytes);
}

void DrawScaledOutline(const u16 *data, s8 x, s8 y, u8 halfSize,
                              u8 inverseCameraDistance, u8 shiftAmount, u8 invert)
{
    u8 size = (u8)(2 * halfSize);
    const u8 *lut = scaleLUT + (((halfSize - 1) >> shiftAmount) * ((halfSize - 1) >> shiftAmount));

    u8 i0, i1, j0, j1;

    s8 outX = (s8)(x >= 0 ? x : 0);
    u8 outY0 = (u8)(y >= 0 ? y : 0);
    u16 invertMask = (u16)(invert ? 0xffff : 0);

    u8 uLeft = DSC_ZERO, uMid = DSC_ZERO, uRight = DSC_ZERO;
    u8 wasVisible = 0;
    u8 iLimit;
    u8 p0, nBytes, i, r, b;

    /* Reject sprites that lie entirely off-screen *before* doing any unsigned
     * clipping arithmetic.  Without this, y > DISPLAY_HEIGHT-1 makes
     * (DISPLAY_HEIGHT - y) underflow to a large u8, p0 becomes >= DISPLAY_PAGES
     * and BlendColumn writes past the end of screenBuffer (it used to land on
     * top of the player struct and teleport the player out of the map). */
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return;
    if ((s16)x + (s16)size <= 0 || (s16)y + (s16)size <= 0) return;

    i0 = (u8)(x < 0 ? -x : 0);
    i1 = (u8)(x + size > DISPLAY_WIDTH ? DISPLAY_WIDTH - x : size);
    j0 = (u8)(y < 0 ? -y : 0);
    j1 = (u8)(y + size > DISPLAY_HEIGHT ? DISPLAY_HEIGHT - y : size);
    iLimit = (u8)(i1 >= 2 ? i1 - 2 : 0);

    if (j1 <= j0 || i1 <= i0) return;

    /* ---- per sprite: build the row resampling table ---------------------- */
    p0 = (u8)(outY0 >> 3);
    nBytes = (u8)(((outY0 + (j1 - j0) - 1) >> 3) - p0 + 1);
    if (nBytes > (u8)(DISPLAY_PAGES - p0)) nBytes = (u8)(DISPLAY_PAGES - p0);

    {
        u8 *p = dsRun;
        u8 q = 16;
        do {
            u8 w = DISPLAY_PAGES;
            do { *p++ = 0; } while (--w);
        } while (--q);
        for (r = 0; r < 16; r++) { dsRunLo[r] = 1; dsRunHi[r] = 0; spReady[r] = 0; }
        for (b = 0; b < DISPLAY_PAGES; b++) {
            dsValid[b] = 0;
            spSet[DSC_ZERO][b] = 0;
            spClr[DSC_ZERO][b] = 0;
            spNop[DSC_ZERO][b] = 0xff;
        }
        spReady[DSC_ZERO] = 1;
    }
    {
        u8 row;
        for (row = outY0; row < (u8)(outY0 + (j1 - j0)); row++)
            dsValid[(row >> 3) - p0] |= pixelMasks[row & 7];
    }
    if (j1 >= (u8)(j0 + 3)) {
        u8 k;
        for (k = j0; k <= (u8)(j1 - 3); k++) {
            u8 v = lut[k >> shiftAmount];
            u8 row = (u8)(y + k + 1);
            u8 bb = (u8)((row >> 3) - p0);
            dsRun[((u16)v << 3) + bb] |= pixelMasks[row & 7];
            if (dsRunLo[v] > dsRunHi[v]) { dsRunLo[v] = bb; dsRunHi[v] = bb; }
            else if (bb > dsRunHi[v]) dsRunHi[v] = bb;
        }
    }

    /* ---- per column ------------------------------------------------------ */
    for (i = i0; i < i1; i++) {
        const u8 isVisible = (u8)(wBuffer[outX] < inverseCameraDistance);

        if (isVisible) {
            if (i >= iLimit) {
                if (wasVisible) {
                    uLeft = uMid;
                    uMid = uRight;
                    uRight = DSC_ZERO;
                } else {
                    break;
                }
            } else {
                const u8 u = lut[i >> shiftAmount];
                EnsureColumn(u, data, invertMask, nBytes);
                if (wasVisible) {
                    uLeft = uMid;
                    uMid = uRight;
                    uRight = u;
                } else {
                    uLeft = DSC_ZERO;
                    uMid = u;
                    uRight = u;
                }
            }

            BlendColumn(screenBuffer + outX + pageOffsets[p0], nBytes, uMid, uLeft, uRight);
        }

        outX++;
        wasVisible = isVisible;
    }
}

void Renderer_DrawScaled(const u16 *data, s8 x, s8 y, u8 halfSize,
                         u8 inverseCameraDistance, u8 invert)
{
    if (halfSize > MAX_SPRITE_SIZE * 2) {
        return;
    } else if (halfSize > MAX_SPRITE_SIZE) {
        DrawScaledInner(data, x, y, halfSize, inverseCameraDistance, 2, invert);
    } else if (halfSize * 2 > MAX_SPRITE_SIZE) {
        DrawScaledInner(data, x, y, halfSize, inverseCameraDistance, 1, invert);
    } else if (halfSize > 2) {
        DrawScaledInner(data, x, y, halfSize, inverseCameraDistance, 0, invert);
    } else if (halfSize == 2) {
        if (x >= 0 && x < DISPLAY_WIDTH && wBuffer[x] < inverseCameraDistance) {
            Platform_PutPixel((u8)x, (u8)y, COLOUR_BLACK);
            Platform_PutPixel((u8)x, (u8)(y + 1), COLOUR_BLACK);
        }
        if (x + 1 >= 0 && x + 1 < DISPLAY_WIDTH && wBuffer[x + 1] < inverseCameraDistance) {
            Platform_PutPixel((u8)(x + 1), (u8)y, COLOUR_BLACK);
            Platform_PutPixel((u8)(x + 1), (u8)(y + 1), COLOUR_BLACK);
        }
    } else {
        if (x >= 0 && x < DISPLAY_WIDTH && wBuffer[x] < inverseCameraDistance) {
            Platform_PutPixel((u8)x, (u8)y, COLOUR_BLACK);
        }
    }
}

/* ------------------------------------------------------------------------ */
QueuedDrawable *Renderer_CreateQueuedDrawable(u8 inverseCameraDistance)
{
    u8 insertionPoint = MAX_QUEUED_DRAWABLES;
    u8 n, i;

    for (n = 0; n < numQueuedDrawables; n++) {
        if (inverseCameraDistance < queuedDrawables[n].inverseCameraDistance) {
            if (numQueuedDrawables < MAX_QUEUED_DRAWABLES) {
                insertionPoint = n;
                numQueuedDrawables++;

                for (i = (u8)(numQueuedDrawables - 1); i > n; i--)
                    queuedDrawables[i] = queuedDrawables[i - 1];
            } else {
                if (n == 0) return 0;

                for (i = 0; i < n - 1; i++)
                    queuedDrawables[i] = queuedDrawables[i + 1];

                insertionPoint = (u8)(n - 1);
            }
            break;
        }
    }

    if (insertionPoint == MAX_QUEUED_DRAWABLES) {
        if (numQueuedDrawables < MAX_QUEUED_DRAWABLES) {
            insertionPoint = numQueuedDrawables;
            numQueuedDrawables++;
        } else if (inverseCameraDistance > queuedDrawables[numQueuedDrawables - 1].inverseCameraDistance) {
            for (i = 0; i < numQueuedDrawables - 1; i++)
                queuedDrawables[i] = queuedDrawables[i + 1];
            insertionPoint = (u8)(numQueuedDrawables - 1);
        } else {
            return 0;
        }
    }

    return &queuedDrawables[insertionPoint];
}

void QueueSprite(const u16 *data, s8 x, s8 y, u8 halfSize,
                        u8 inverseCameraDistance, u8 invert)
{
    QueuedDrawable *drawable;

    if (x < -(s16)halfSize * 2) return;

    drawable = Renderer_CreateQueuedDrawable(inverseCameraDistance);

    if (drawable) {
        drawable->type = DrawableType_Sprite;
        drawable->ptr = (const void *)data;
        drawable->x = x;
        drawable->y = y;
        drawable->halfSize = halfSize;
        drawable->inverseCameraDistance = inverseCameraDistance;
        drawable->invert = invert ? 1 : 0;
    }
}

void RenderQueuedDrawables(void)
{
    u8 n;
    for (n = 0; n < numQueuedDrawables; n++) {
        QueuedDrawable *drawable = &queuedDrawables[n];

        if (drawable->type == DrawableType_Sprite) {
            Renderer_DrawScaled((const u16 *)drawable->ptr, drawable->x, drawable->y,
                                drawable->halfSize, drawable->inverseCameraDistance,
                                drawable->invert);
        } else {
            ParticleSystem_Draw((ParticleSystem *)drawable->ptr,
                                drawable->x, drawable->inverseCameraDistance);
        }
    }
}

s8 Renderer_GetHorizon(s16 x)
{
    if (x < 0) x = 0;
    if (x >= DISPLAY_WIDTH) x = DISPLAY_WIDTH - 1;
    return horizonBuffer[x];
}

u8 Renderer_TransformAndCull(s16 worldX, s16 worldY, s16 *outScreenX, s16 *outScreenW)
{
    s16 relX, relZ;
    TransformToViewSpace(worldX, worldY, &relX, &relZ);

    if (relZ < CLIP_PLANE) return 0;
    if (relX < 0 && -2 * relZ > relX) return 0;
    if (relX > 0 && 2 * relZ < relX) return 0;

    TransformToScreenSpace(relX, relZ, outScreenX, outScreenW);
    return 1;
}

void Renderer_DrawObject(const u16 *spriteData, s16 x, s16 y, u8 scale,
                         AnchorType anchor, u8 invert)
{
    s16 screenX, screenW;

    if (Renderer_TransformAndCull(x, y, &screenX, &screenW)) {
        u8 inverseCameraDistance = (u8)(screenW + 1);
        s16 spriteSize = (s16)((screenW * scale) / 128);
        s8 outY = Renderer_GetHorizon(screenX);

        switch (anchor) {
        case AnchorType_Floor:       outY = (s8)(outY + screenW - 2 * spriteSize); break;
        case AnchorType_Center:      outY = (s8)(outY - spriteSize); break;
        case AnchorType_BelowCenter: break;
        case AnchorType_Ceiling:     outY = (s8)(outY - screenW); break;
        }

        QueueSprite(spriteData, (s8)(screenX - spriteSize), outY,
                    (u8)spriteSize, inverseCameraDistance, invert);
    }
}

/* ------------------------------------------------------------------------ */
void DrawWeapon(void)
{
    s16 x = (s16)(DISPLAY_WIDTH / 2 + 22 + camera.tilt / 4);
    s16 y = (s16)(DISPLAY_HEIGHT - 21 - camera.bob);
    u8 reloadTime = player.reloadTime;

    if (reloadTime > 0) {
        Platform_DrawSprite((s16)(x - reloadTime / 3 - 1), (s16)(y - reloadTime / 3 - 1),
                            handSpriteData2, 0);
    } else {
        Platform_DrawSprite((s16)(x + 2), (s16)(y + 2), handSpriteData1, 0);
    }
}

void DrawBackground(void)
{
    u8 *ptr = screenBuffer;
    u8 counter = DISPLAY_WIDTH * (DISPLAY_PAGES / 2) / 4;

    while (counter--) {
        *ptr++ = 0x55; *ptr++ = 0x00; *ptr++ = 0xaa; *ptr++ = 0x00;
    }

    counter = DISPLAY_WIDTH * (DISPLAY_PAGES / 2) / 4;
    while (counter--) {
        *ptr++ = 0x55; *ptr++ = 0xff; *ptr++ = 0xaa; *ptr++ = 0xff;
    }
}

void DrawBar(u8 *screenPtr, const u8 *iconData, u8 amount, u8 max)
{
    const u8 iconWidth = 8;
    const u8 barWidth = 32;
    const u8 unfilledBar = 0xfe;
    const u8 filledBar = 0xc6;

    u8 fillAmount = (u8)(((u16)amount * barWidth) / max);
    u8 x = 0;

    while (x < iconWidth) { screenPtr[x] = iconData[x]; x++; }
    while (fillAmount--) screenPtr[x++] = filledBar;
    while (x < barWidth + iconWidth) screenPtr[x++] = unfilledBar;

    screenPtr[x++] = unfilledBar;
    screenPtr[x] = 0;
}

void DrawDamageIndicator(void)
{
    u8 *upper = screenBuffer;
    u8 *lower = upper + DISPLAY_WIDTH * (DISPLAY_PAGES - 1);
    u8 *ptr;
    s16 x, y;

    for (x = 1; x < DISPLAY_WIDTH - 1; x++) {
        upper[x] = (u8)(upper[x] & 0xfe);
        lower[x] = (u8)(lower[x] & 0x7f);
    }

    ptr = screenBuffer;
    for (y = 0; y < DISPLAY_PAGES; y++) {
        *ptr = 0;
        ptr += (DISPLAY_WIDTH - 1);
        *ptr = 0;
        ptr++;
    }
}

void DrawHUD(void)
{
    DrawBar(screenBuffer + DISPLAY_WIDTH * (DISPLAY_PAGES - 1), heartSpriteData,
            player.hp, PLAYER_MAX_HP);
    DrawBar(screenBuffer + DISPLAY_WIDTH * (DISPLAY_PAGES - 2), manaSpriteData,
            player.mana, PLAYER_MAX_MANA);

    if (player.damageTime > 0) DrawDamageIndicator();

    if (displayMessage) Font_PrintString(displayMessage, 0, 0, COLOUR_BLACK);
}

void Renderer_Render(void)
{
    u8 n;

    globalRenderFrame++;

    DrawBackground();

    numBufferSlicesFilled = 0;
    numQueuedDrawables = 0;

    /* horizonBuffer[n] = HORIZON + ((DISPLAY_WIDTH/2 - n) * tilt >> 8) + bob.
     * The product walks by -tilt per column, so one multiply per frame does
     * the job instead of 104 calls to SDCC's __mulint - and with no tilt at
     * all (the usual case) the whole row is one constant. */
    {
        const s8 tilt = camera.tilt;
        const s8 bob = camera.bob;

        for (n = 0; n < DISPLAY_WIDTH; n++) wBuffer[n] = 0;

        if (tilt == 0) {
            const s8 h = (s8)(HORIZON + bob);
            for (n = 0; n < DISPLAY_WIDTH; n++) horizonBuffer[n] = h;
        } else {
            s16 acc = (s16)((DISPLAY_WIDTH / 2) * tilt);
            const s8 base = (s8)(HORIZON + bob);
            for (n = 0; n < DISPLAY_WIDTH; n++) {
                horizonBuffer[n] = (s8)(base + (s8)(acc >> 8));
                acc = (s16)(acc - tilt);
            }
        }
    }

    camera.cellX = (u8)(camera.x / CELL_SIZE);
    camera.cellY = (u8)(camera.y / CELL_SIZE);

    camera.rotCos = FixedCos(-camera.angle);
    camera.rotSin = FixedSin(-camera.angle);
    camera.clipCos = FixedCos(-camera.angle + CLIP_ANGLE);
    camera.clipSin = FixedSin(-camera.angle + CLIP_ANGLE);

    BuildClipTables();
    BuildCornerTables();

    DrawCells();

    EnemyManager_Draw();
    ProjectileManager_Draw();
    ParticleSystemManager_Draw();

    RenderQueuedDrawables();

    DrawWeapon();

    DrawHUD();
}
