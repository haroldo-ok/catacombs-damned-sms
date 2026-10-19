/* ---------------------------------------------------------------------------
 * Catacombs of the Damned!  -  Sega Master System port
 *
 * Original game (c) James Howard - https://github.com/jhhoward/Arduboy3D
 * SMS port built on devkitSMS (SMSlib / PSGlib).
 * ------------------------------------------------------------------------- */
#include "SMSlib.h"
#include "platform.h"
#include "game.h"
#include "draw.h"
#include "fixedmath.h"

void Platform_PollInput(void);

/* The Arduboy runs its game logic at 30 Hz.  A rendered frame here costs many
 * video frames, so the logic is stepped once per two elapsed video frames
 * (capped, so a very heavy frame cannot snowball).  `logicTicksPerFrame`
 * holds the value actually used for the last frame and is readable from the
 * headless playtest. */
#define MAX_LOGIC_TICKS 6
u8 logicTicksPerFrame = 1;
extern volatile u16 vblankCounter;

/* exposed for the headless playtest */
u16 frameCounter = 0;

void main(void)
{
    u8 t;
    u16 lastVBlank = 0;

    Platform_Init();
    Game_Init();

    for (;;) {
        u16 now = vblankCounter;
        u16 elapsed = (u16)(now - lastVBlank);
        lastVBlank = now;

        logicTicksPerFrame = (u8)(elapsed >> 1);
        if (logicTicksPerFrame < 1) logicTicksPerFrame = 1;
        if (logicTicksPerFrame > MAX_LOGIC_TICKS) logicTicksPerFrame = MAX_LOGIC_TICKS;

        Platform_PollInput();

        for (t = 0; t < logicTicksPerFrame; t++) {
            Game_Tick();
            if (gameState != GameState_InGame) break;
        }

        Game_Draw();
        Platform_Present();

        frameCounter++;
    }
}

SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER(1, 0, 2026, 10, 18, "jhhoward / SMS port", "Catacombs of the Damned!",
                      "1-bit textured raycaster dungeon crawler, ported from the Arduboy");
