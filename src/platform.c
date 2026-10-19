/* ---------------------------------------------------------------------------
 * Platform layer for the Sega Master System.
 *
 * Everything the game needs from the hardware lives here:
 *   - the 1bpp software framebuffer and its drawing primitives
 *     (identical semantics to the Arduboy2 originals)
 *   - VDP / palette / name table setup
 *   - controller reading
 *   - an ArduboyTones-compatible sound-effect player driven onto the PSG
 * ------------------------------------------------------------------------- */
#include "SMSlib.h"
#include "platform.h"
#include "fastmath.h"

/* The spread table used by blit.s must sit at a 256 byte aligned address.
 * 0xC000 is reserved for it; --data-loc starts the rest of RAM after it. */
__at(0xC000) u8 spreadTable[16];

u8 screenBuffer[SCREEN_BUFFER_SIZE];

/* blit.s owns prevBuffer and blitTileCount */
extern u8 prevBuffer[SCREEN_BUFFER_SIZE];
extern u16 blitTileCount;

const u16 pageOffsets[DISPLAY_PAGES] = {
    0 * DISPLAY_WIDTH, 1 * DISPLAY_WIDTH, 2 * DISPLAY_WIDTH, 3 * DISPLAY_WIDTH,
    4 * DISPLAY_WIDTH, 5 * DISPLAY_WIDTH, 6 * DISPLAY_WIDTH, 7 * DISPLAY_WIDTH
};
const u8 pixelMasks[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

static const u8 topmask_[8]    = { 0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80 };
static const u8 bottommask_[8] = { 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff };

/* ------------------------------------------------------------------------ */
/* VRAM map                                                                  */
/* ------------------------------------------------------------------------ */
#define TILE_VIEW_BASE  0                                     /* 416 tiles  */
#define TILE_BLANK      (VIEW_TILES_X * VIEW_TILES_Y)         /* 416        */
#define TILE_FRAME      (TILE_BLANK + 1)                      /* 417        */

/* ------------------------------------------------------------------------ */
/* Input                                                                     */
/*                                                                           */
/* A rendered frame here costs well over a second, so sampling the pad once  */
/* per game frame threw most presses away: a normal 0.2s tap fell between    */
/* two samples and the game never saw it.  The pad is therefore read 60      */
/* times a second from the frame interrupt:                                  */
/*                                                                           */
/*   irqHeld     - what is down right now                                    */
/*   irqPressed  - every button seen down since the last game frame, so a    */
/*                 tap that began and ended inside one slow frame still      */
/*                 reaches the game                                          */
/*   irqReleased - every button seen up, which is how a button retires from  */
/*                 the ignore mask below                                     */
/*                                                                           */
/* `inputIgnore` holds the buttons that were down when the game last changed */
/* state.  They stay masked out until they are physically released, so the   */
/* press that picks "Start" cannot also throw a fireball on the first frame  */
/* of the level - and, crucially, a player who holds or mashes a button can  */
/* never stall a state transition (that is what used to wedge the "Entering  */
/* floor" screen).                                                           */
/* ------------------------------------------------------------------------ */
__sfr __at 0xDC JoypadPort1;        /* active low: 1 = released */

static volatile u8 irqHeld;
static volatile u8 irqPressed;
static volatile u8 irqReleased;

u8 inputState;                      /* what the game sees for the whole frame */
static u8 inputIgnore;

/* interrupt context - keep it short */
static void Platform_ReadPad(void)
{
    u8 raw = (u8)~JoypadPort1;      /* now 1 = pressed */
    u8 keys = 0;

    if (raw & 0x01) keys |= INPUT_UP;
    if (raw & 0x02) keys |= INPUT_DOWN;
    if (raw & 0x04) keys |= INPUT_LEFT;
    if (raw & 0x08) keys |= INPUT_RIGHT;
    /* Button 1 is the primary action (cast a fireball, confirm in menus),
     * button 2 is the strafe modifier - the Arduboy had these the other way
     * round but B/1 as "fire" is what an SMS pad wants. */
    if (raw & 0x10) keys |= INPUT_B;
    if (raw & 0x20) keys |= INPUT_A;

    irqHeld = keys;
    irqPressed |= keys;
    irqReleased |= (u8)~keys;
}

void Platform_PollInput(void)
{
    u8 held, pressed, released;

    __critical {
        held = irqHeld;
        pressed = irqPressed;
        released = irqReleased;
        irqPressed = 0;
        irqReleased = 0;
    }

    inputIgnore &= (u8)~released;
    inputState = (u8)((held | pressed) & (u8)~inputIgnore);
}

u8 Platform_GetInput(void)
{
    return inputState;
}

/* Swallow whatever is held right now; it only counts again once released. */
void Platform_ConsumeInput(void)
{
    __critical {
        inputIgnore = (u8)(irqHeld | irqPressed);
        irqPressed = 0;
        irqReleased = 0;
    }
    inputState = 0;
}

void Platform_SetLED(u8 r, u8 g, u8 b)
{
    /* No RGB LED on the SMS.  The damage flash is already drawn on screen. */
    (void)r; (void)g; (void)b;
}

/* ------------------------------------------------------------------------ */
/* Sound - ArduboyTones compatible streams played on PSG channel 2           */
/*                                                                           */
/* A stream is a list of (frequency, duration-in-ms) pairs terminated by      */
/* TONES_END (0x8000).  The player is ticked from the line/frame interrupt    */
/* so sound timing is independent of the (slow) game frame rate.             */
/* ------------------------------------------------------------------------ */
#define TONES_END   0x8000

__sfr __at 0x7F PSGPort;

static const u16 *tonePtr;
static u16 toneTimer;
u8 audioEnabled = 1;
static u8  currentChannelOn;

static void PSGSilence(void)
{
    PSGPort = 0xDF;         /* channel 2 volume off */
    currentChannelOn = 0;
}

static void PSGSetTone(u16 freq)
{
    /* PSG tone register = 3579545 / (32 * freq).  PsgPeriod is the re-entrant
     * assembly divide - this runs inside the interrupt handler. */
    u16 n;
    if (freq < 55) { PSGSilence(); return; }
    n = PsgPeriod(freq);
    if (n > 1023) n = 1023;
    PSGPort = (u8)(0xC0 | (n & 0x0F));          /* latch ch2 tone, low bits */
    PSGPort = (u8)((n >> 4) & 0x3F);            /* high bits                */
    PSGPort = 0xD0;                             /* ch2 volume = max         */
    currentChannelOn = 1;
}

void Platform_PlaySound(const u16 *audioPattern)
{
    if (!audioEnabled) return;
    tonePtr = audioPattern;
    toneTimer = 0;
}

u8 Platform_IsAudioEnabled(void)
{
    return audioEnabled;
}

void Platform_SetAudioEnabled(u8 enabled)
{
    audioEnabled = enabled;
    if (!enabled) { tonePtr = 0; PSGSilence(); }
}

/* Called 4 times per video frame (3 line interrupts + 1 frame interrupt),
 * i.e. roughly every 4 ms. */
#define AUDIO_TICK_MS 4
void Platform_AudioTick(void)
{
    u16 freq;
    if (!tonePtr) return;

    if (toneTimer > AUDIO_TICK_MS) {
        toneTimer = (u16)(toneTimer - AUDIO_TICK_MS);
        return;
    }
    toneTimer = 0;

    /* advance to the next tone, skipping any that already expired */
    for (;;) {
        freq = *tonePtr;
        if (freq == TONES_END) {
            tonePtr = 0;
            PSGSilence();
            return;
        }
        toneTimer = tonePtr[1];
        tonePtr += 2;
        if (toneTimer) break;
    }

    if (freq & 0x8000) freq &= 0x7FFF;      /* TONE_HIGH_VOLUME flag */
    if (freq == 0) PSGSilence();
    else PSGSetTone(freq);
}

void Platform_ExpectLoadDelay(void)
{
    /* Nothing to do: there is no frame-time watchdog on this platform. */
}

/* ------------------------------------------------------------------------ */
/* Framebuffer primitives                                                    */
/* ------------------------------------------------------------------------ */
void Platform_FillScreen(u8 col)
{
    u8 *p = screenBuffer;
    u8 v = col ? 0xff : 0x00;
    u16 n = SCREEN_BUFFER_SIZE;
    while (n--) *p++ = v;
}

void Platform_PutPixel(u8 x, u8 y, u8 colour)
{
    u8 *p;
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return;
    p = screenBuffer + pageOffsets[y >> 3] + x;
    if (colour) *p |= pixelMasks[y & 7];
    else        *p &= ~pixelMasks[y & 7];
}

/* Adapted from the Arduboy build (originally from a1k0n/arduboy3d). */
void Platform_DrawVLine(u8 x, s8 y0_, s8 y1_, u8 pattern)
{
    u8 *screenptr = screenBuffer + x;
    u8 *page0;
    u8 *page1;
    u8 y0, y1, mask;

    if (y1_ < y0_ || y1_ < 0 || y0_ > (DISPLAY_HEIGHT - 1)) return;

    y0 = (u8)y0_;
    y1 = (u8)y1_;
    if (y0_ < 0) y0 = 0;
    if (y1_ > (DISPLAY_HEIGHT - 1)) y1 = DISPLAY_HEIGHT - 1;

    page0 = screenptr + pageOffsets[y0 >> 3];
    page1 = screenptr + pageOffsets[y1 >> 3];

    if (page0 == page1) {
        mask = topmask_[y0 & 7] & bottommask_[y1 & 7];
        *page0 = (u8)((*page0 & ~mask) | (pattern & mask));
    } else {
        mask = topmask_[y0 & 7];
        *page0 = (u8)((*page0 & ~mask) | (pattern & mask));
        page0 += DISPLAY_WIDTH;
        while (page0 != page1) {
            *page0 = pattern;
            page0 += DISPLAY_WIDTH;
        }
        mask = bottommask_[y1 & 7];
        *page0 = (u8)((*page0 & ~mask) | (pattern & mask));
    }
}

/* Arduboy2 Sprites::drawPlusMask - bitmap is  w, h, then for every page
 * row: w pairs of (image, mask) bytes, repeated for each frame. */
void Platform_DrawSprite(s16 x, s16 y, const u8 *bitmap, u8 frame)
{
    u8 w = bitmap[0];
    u8 h = bitmap[1];
    u8 pages = (u8)((h + 7) >> 3);
    const u8 *data = bitmap + 2 + (u16)frame * pages * w * 2;
    u8 p, c;
    s16 sx, sy;

    for (p = 0; p < pages; p++) {
        sy = y + (s16)(p << 3);
        if (sy <= -8 || sy >= DISPLAY_HEIGHT) { data += (u16)w * 2; continue; }
        for (c = 0; c < w; c++) {
            u8 img = *data++;
            u8 msk = *data++;
            u16 simg, smsk;
            s16 page;
            u8 shift;
            sx = x + c;
            if (sx < 0 || sx >= DISPLAY_WIDTH) continue;
            if (!msk) continue;

            if (sy >= 0) { page = sy >> 3; shift = (u8)(sy & 7); }
            else         { page = -1;      shift = (u8)((sy + 8) & 7); }

            simg = (u16)img << shift;
            smsk = (u16)msk << shift;

            if (page >= 0 && page < DISPLAY_PAGES) {
                u8 *dst = screenBuffer + pageOffsets[page] + sx;
                *dst = (u8)((*dst & ~(u8)smsk) | (u8)simg);
            }
            if (shift && (page + 1) >= 0 && (page + 1) < DISPLAY_PAGES) {
                u8 *dst = screenBuffer + pageOffsets[page + 1] + sx;
                *dst = (u8)((*dst & ~(u8)(smsk >> 8)) | (u8)(simg >> 8));
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Interrupts                                                                */
/* ------------------------------------------------------------------------ */
volatile u16 vblankCounter;

static void FrameHandler(void)
{
    vblankCounter++;
    Platform_ReadPad();
    Platform_AudioTick();
}

static void LineHandler(void)
{
    Platform_AudioTick();
}

void Platform_WaitFrame(void)
{
    SMS_waitForVBlank();
}

/* ------------------------------------------------------------------------ */
/* Setup                                                                     */
/* ------------------------------------------------------------------------ */
static u16 nameRow[32];

void Platform_Init(void)
{
    u8 i, x, y;

    SMS_displayOff();

    /* spread table: nibble b0b1b2b3 -> b0b0 b1b1 b2b2 b3b3 */
    for (i = 0; i < 16; i++) {
        u8 v = 0;
        if (i & 8) v |= 0xC0;
        if (i & 4) v |= 0x30;
        if (i & 2) v |= 0x0C;
        if (i & 1) v |= 0x03;
        spreadTable[i] = v;
    }

    /* Clear all of VRAM: every tile starts out with all four planes zero, and
     * from then on the blitter only ever rewrites plane 0. */
    SMS_VRAMmemset(0x0000, 0x00, 16384);

    /* The frame tile is solid colour 2 (plane 1 set on every row). */
    {
        u8 frameTile[32];
        for (i = 0; i < 32; i++) frameTile[i] = (u8)(((i & 3) == 1) ? 0xFF : 0x00);
        SMS_loadTiles(frameTile, TILE_FRAME, 32);
    }

    /* Name table: the 26x16 view window, a one tile bezel, blank elsewhere. */
    for (y = 0; y < 24; y++) {
        for (x = 0; x < 32; x++) {
            u16 entry = TILE_BLANK;
            if (x >= VIEW_ORIGIN_TX && x < VIEW_ORIGIN_TX + VIEW_TILES_X &&
                y >= VIEW_ORIGIN_TY && y < VIEW_ORIGIN_TY + VIEW_TILES_Y) {
                entry = (u16)((y - VIEW_ORIGIN_TY) * VIEW_TILES_X + (x - VIEW_ORIGIN_TX));
            } else if (x >= VIEW_ORIGIN_TX - 1 && x <= VIEW_ORIGIN_TX + VIEW_TILES_X &&
                       y >= VIEW_ORIGIN_TY - 1 && y <= VIEW_ORIGIN_TY + VIEW_TILES_Y) {
                entry = TILE_FRAME;
            }
            nameRow[x] = entry;
        }
        SMS_loadTileMap(0, y, nameRow, 64);
    }

    /* Palette: colour 0 black (everything the blitter leaves alone),
     * colour 1 the phosphor white the picture is drawn in,
     * colour 2 the bezel. */
    SMS_setBGPaletteColor(0, RGB(0, 0, 0));
    SMS_setBGPaletteColor(1, RGB(3, 3, 3));
    SMS_setBGPaletteColor(2, RGB(1, 1, 1));
    for (i = 3; i < 16; i++) SMS_setBGPaletteColor(i, RGB(3, 3, 3));
    SMS_setSpritePaletteColor(0, RGB(0, 0, 0));
    SMS_setBackdropColor(0);

    SMS_setBGScrollX(0);
    SMS_setBGScrollY(0);
    SMS_initSprites();
    SMS_copySpritestoSAT();

    /* PSG: silence every channel, then arm channel 2 for effects. */
    PSGPort = 0x9F;
    PSGPort = 0xBF;
    PSGPort = 0xDF;
    PSGPort = 0xFF;

    Platform_FillScreen(0);
    for (i = 0; i < DISPLAY_PAGES; i++) {
        u8 j;
        for (j = 0; j < DISPLAY_WIDTH; j++) prevBuffer[pageOffsets[i] + j] = 0xAA;
    }

    SMS_setLineInterruptHandler(LineHandler);
    SMS_setLineCounter(63);
    SMS_setFrameInterruptHandler(FrameHandler);
    SMS_enableLineInterrupt();
    SMS_displayOn();
}
