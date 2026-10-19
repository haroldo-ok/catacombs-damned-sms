/* ---------------------------------------------------------------------------
 * Catacombs of the Damned! - Sega Master System port
 *
 * Ported from James Howard's Arduboy3D (https://github.com/jhhoward/Arduboy3D)
 *
 * Global configuration / constants.  Mirrors the original Defines.h but with
 * the display geometry re-derived for the SMS (see platform.h for how the
 * 1bpp software framebuffer reaches the VDP).
 * ------------------------------------------------------------------------- */
#ifndef DEFINES_H
#define DEFINES_H

typedef unsigned char  u8;
typedef signed char    s8;
typedef unsigned int   u16;
typedef signed int     s16;
typedef unsigned long  u32;
typedef signed long    s32;

/* SDCC has no <stdint.h> habits from the AVR port; map the original names. */
typedef u8  uint8_t;
typedef s8  int8_t;
typedef u16 uint16_t;
typedef s16 int16_t;
typedef u32 uint32_t;
typedef s32 int32_t;

/* The original is AVR, where const data must be reached through pgm_read_*.
 * The Z80 has a single address space, so these all collapse to plain reads. */
#define PROGMEM
#define PSTR(x) (x)
#define pgm_read_byte(x) (*((const u8 *)(x)))
#define pgm_read_word(x) (*((const u16 *)(x)))
#define pgm_read_ptr(x)  (*((const void *const *)(x)))

/* ---------------------------------------------------------------------------
 * Display geometry
 *
 * The Arduboy screen is 128x64 1bpp.  On the SMS there is no framebuffer, so
 * the software framebuffer is converted to 8x8 4bpp tiles every frame.  VRAM
 * holds at most 448 tiles once the name table and sprite table are accounted
 * for, which caps a 2x-magnified bitmap at 7168 source pixels.  104x64 fits
 * (26x16 = 416 tiles) and leaves slots for the blank/border tiles, so the view
 * is drawn at 104x64 and displayed at 208x128 - a little narrower than the
 * Arduboy, with the same field of view (NEAR_PLANE scales with the width).
 * ------------------------------------------------------------------------- */
#define DISPLAY_WIDTH   104
#define DISPLAY_HEIGHT  64
#define DISPLAY_PAGES   (DISPLAY_HEIGHT / 8)

#define TARGET_FRAMERATE 30
#define DEV_MODE 0

#define INPUT_LEFT  1
#define INPUT_RIGHT 2
#define INPUT_UP    4
#define INPUT_DOWN  8
#define INPUT_A     16
#define INPUT_B     32

#define COLOUR_WHITE 1
#define COLOUR_BLACK 0

#define FIXED_ANGLE_MAX 256

#define CAMERA_SCALE 1
#define CLIP_PLANE 32
#define CLIP_ANGLE 32
#define NEAR_PLANE_MULTIPLIER 130
#define NEAR_PLANE (DISPLAY_WIDTH * NEAR_PLANE_MULTIPLIER / 256)
#define HORIZON (DISPLAY_HEIGHT / 2)

#define CELL_SIZE 256
#define PARTICLES_PER_SYSTEM 8
#define BASE_SPRITE_SIZE 16
#define MAX_SPRITE_SIZE (DISPLAY_HEIGHT / 2)
#define MIN_TEXTURE_DISTANCE 4
#define MAX_QUEUED_DRAWABLES 12

#define TURN_SPEED 3

#define ABS(x) (((x) < 0) ? -(x) : (x))

#endif /* DEFINES_H */
