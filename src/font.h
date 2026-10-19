#ifndef FONT_H
#define FONT_H

#include "defines.h"

#define FONT_GLYPH_WIDTH 4
#define FONT_GLYPH_HEIGHT 8
#define FONT_FIRST_GLYPH 32

void Font_PrintString(const char *str, u8 line, u8 x, u8 colour);
void Font_PrintInt(u16 value, u8 line, u8 x, u8 colour);

#endif
