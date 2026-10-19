/* Compact 4x6 font, as in the original (VGAtonic tiny font, MIT). */
#include "font.h"
#include "platform.h"
#include "gamedata.h"

static void DrawChar(u8 *screenPtr, char c, u8 xorMask)
{
    const u8 index = (u8)((u8)c - FONT_FIRST_GLYPH);
    const u8 *fontPtr = fontPageData + FONT_GLYPH_WIDTH * index;

    screenPtr[0] = (u8)(xorMask ^ fontPtr[0]);
    screenPtr[1] = (u8)(xorMask ^ fontPtr[1]);
    screenPtr[2] = (u8)(xorMask ^ fontPtr[2]);
    screenPtr[3] = (u8)(xorMask ^ fontPtr[3]);
}

void Font_PrintString(const char *str, u8 line, u8 x, u8 colour)
{
    u8 *screenPtr = screenBuffer;
    u8 xorMask = (u8)(colour == COLOUR_BLACK ? 0 : 0xff);
    screenPtr += DISPLAY_WIDTH * line + x;

    for (;;) {
        char c = *str++;
        if (!c) break;
        DrawChar(screenPtr, c, xorMask);
        screenPtr += FONT_GLYPH_WIDTH;
    }
}

void Font_PrintInt(u16 val, u8 line, u8 x, u8 colour)
{
    u8 *screenPtr = screenBuffer;
    u8 xorMask = (u8)(colour == COLOUR_BLACK ? 0 : 0xff);
    char buffer[5];
    s8 bufCount = 0;
    s8 n;

    screenPtr += DISPLAY_WIDTH * line + x;

    if (val == 0) {
        DrawChar(screenPtr, '0', xorMask);
        return;
    }

    for (n = 0; n < 5 && val != 0; n++) {
        u8 c = (u8)(val % 10);
        buffer[bufCount++] = (char)('0' + c);
        val = val / 10;
    }

    for (n = (s8)(bufCount - 1); n >= 0; n--) {
        DrawChar(screenPtr, buffer[n], xorMask);
        screenPtr += FONT_GLYPH_WIDTH;
    }
}
