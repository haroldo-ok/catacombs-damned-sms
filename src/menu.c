/* ---------------------------------------------------------------------------
 * Menus / title / game over - port of Menu.cpp.
 *
 * The text layout is re-flowed for the 104 pixel wide view (26 characters at
 * the 4x8 font instead of 32); the strings, the sprites and the scoring are
 * the originals.
 * ------------------------------------------------------------------------- */
#include "defines.h"
#include "platform.h"
#include "menu.h"
#include "font.h"
#include "game.h"
#include "fixedmath.h"
#include "draw.h"
#include "gamedata.h"
#include "enemy.h"

u16 menuState;          /* selection | timer | fizzleFade, as in the original */

void Menu_Init(void)
{
    menuState = 0;
}

void Menu_Draw(void)
{
    const u16 *torchSprite;

    Platform_FillScreen(COLOUR_BLACK);
    /* the 23 character title does not fit between the two torches at this
     * width, so it is set on two lines */
    Font_PrintString("CATACOMBS", 1, 34, COLOUR_WHITE);
    Font_PrintString("OF THE DAMNED", 2, 26, COLOUR_WHITE);
    Font_PrintString("by @jameshhoward", 7, 20, COLOUR_WHITE);
    Font_PrintString("Start", 4, 24, COLOUR_WHITE);
    Font_PrintString("Sound:", 5, 24, COLOUR_WHITE);

    if (Platform_IsAudioEnabled()) Font_PrintString("on", 5, 52, COLOUR_WHITE);
    else Font_PrintString("off", 5, 52, COLOUR_WHITE);

    Font_PrintString(">", (u8)(4 + (u8)menuState), 16, COLOUR_WHITE);

    torchSprite = (globalTickFrame & 4) ? torchSpriteData1 : torchSpriteData2;
    Renderer_DrawScaled(torchSprite, 0, 10, 9, 255, 0);
    Renderer_DrawScaled(torchSprite, DISPLAY_WIDTH - 18, 10, 9, 255, 0);
}

void Menu_Tick(void)
{
    static u8 lastInput = 0;
    u8 input;

    /* Spin the RNG to have a unique(ish) starting level */
    Random();

    input = Platform_GetInput();

    if ((input & INPUT_DOWN) && !(lastInput & INPUT_DOWN)) menuState = (u16)(!menuState);
    if ((input & INPUT_UP) && !(lastInput & INPUT_UP)) menuState = (u16)(!menuState);

    if ((input & (INPUT_A | INPUT_B)) && !(lastInput & (INPUT_A | INPUT_B))) {
        switch ((u8)menuState) {
        case 0:
            Game_StartGame();
            break;
        case 1:
            Platform_SetAudioEnabled((u8)(!Platform_IsAudioEnabled()));
            break;
        }
    }

    lastInput = input;
}

void Menu_ResetTimer(void)
{
    menuState = 0;
}

void Menu_TickEnteringLevel(void)
{
    const u8 showTime = 16;

    if (menuState < showTime) {
        menuState++;
        return;
    }

    /* The original also waited here for every button to be released, so that
     * the press that started the game could not immediately cast a spell.
     * One game frame on the SMS lasts about a second and a half, so a player
     * who keeps the button down - or taps it because the screen looks stuck -
     * held this state forever.  The buttons are swallowed instead, by
     * Platform_ConsumeInput() at the end of Game_StartLevel(). */
    Game_StartLevel();
}

void Menu_DrawEnteringLevel(void)
{
    Platform_FillScreen(COLOUR_BLACK);
    Font_PrintString("Entering floor", 3, 16, COLOUR_WHITE);
    Font_PrintInt(gameFloor, 3, 76, COLOUR_WHITE);
}

/* ---------------------------------------------------------------------------
 * Loading indicator.
 *
 * Generating a dungeon takes a noticeable fraction of a second, and the first
 * rendered frame after it takes longer still.  Without feedback that reads as
 * a crash, so a sweeping bar is drawn under the "Entering floor N" caption and
 * pushed to the VDP as the generator works.  Only the two tile rows the bar
 * lives in are dirty, so each update is cheap.
 * ------------------------------------------------------------------------- */
#define LOAD_BAR_X     20
#define LOAD_BAR_W     64
#define LOAD_BAR_PAGE  5                 /* framebuffer rows 40..47          */
#define LOAD_BAR_ROWS  0x30              /* rows 44 and 45 inside that page  */
#define LOAD_BLOCK_W   12

extern volatile u16 vblankCounter;

static u8 loadPos;
static u8 loadDir;
static u16 lastLoadFrame;
/* Diagnostic counter: how many times the loading bar actually redrew while
 * generating a floor.  tests/loadtime.txt reads it to check the throttle
 * (14 redraws for floor 1); a Platform_Present costs ~130 k cycles whatever
 * happens, so this number is the bar's whole price. */
u16 loadStepCount;

void Menu_BeginLoading(void)
{
    loadPos = 0;
    loadDir = 1;
    lastLoadFrame = vblankCounter;
    Menu_LoadingStep();
}

/* Called from deep inside the generator's loops.  A Platform_Present costs
 * ~130k cycles however little changed (it diffs all 416 tiles), so the bar is
 * redrawn about two and a half times a second - enough to read as "working",
 * cheap enough to stay under a tenth of the load. */
void Menu_LoadingPoll(void)
{
    u16 now = vblankCounter;

    if ((u16)(now - lastLoadFrame) >= 24) {
        lastLoadFrame = now;
        Menu_LoadingStep();
    }
}

void Menu_LoadingStep(void)
{
    u8 *row = screenBuffer + LOAD_BAR_PAGE * DISPLAY_WIDTH + LOAD_BAR_X;
    u8 i;

    for (i = 0; i < LOAD_BAR_W; i++) {
        u8 on = (u8)(i >= loadPos && i < (u8)(loadPos + LOAD_BLOCK_W));
        row[i] = (u8)(on ? LOAD_BAR_ROWS : 0x00);
    }

    if (loadDir) {
        loadPos += 6;
        if ((u8)(loadPos + LOAD_BLOCK_W) >= LOAD_BAR_W) loadDir = 0;
    } else {
        if (loadPos < 6) loadDir = 1;
        else loadPos -= 6;
    }

    loadStepCount++;
    Platform_Present();
}

void Menu_TickGameOver(void)
{
    const u8 minShowTime = 20;

    if (menuState < minShowTime) {
        menuState++;
        return;
    }

    /* Same story as Menu_TickEnteringLevel: react to the press itself and
     * swallow the held button rather than waiting for a release that a
     * button-mashing player may never give us. */
    if (Platform_GetInput() & (INPUT_A | INPUT_B)) {
        Platform_ConsumeInput();
        Game_SwitchState(GameState_Menu);
    }
}

void Menu_DrawGameOver(void)
{
    const u8 firstRow = 21;
    const u8 secondRow = 38;
    u8 offset;
    u16 finalScore = 0;

    Platform_FillScreen(COLOUR_BLACK);
    Font_PrintString("GAME OVER", 0, 34, COLOUR_WHITE);

    switch (stats.killedBy) {
    case EnemyType_None:
        Font_PrintString("You escaped the catacombs!", 1, 0, COLOUR_WHITE);
        break;
    case EnemyType_Mage:
        Font_PrintString("Killed by a mage, lvl", 1, 2, COLOUR_WHITE);
        Font_PrintInt(gameFloor, 1, 88, COLOUR_WHITE);
        break;
    case EnemyType_Skeleton:
        Font_PrintString("Killed by a knight, lvl", 1, 2, COLOUR_WHITE);
        Font_PrintInt(gameFloor, 1, 96, COLOUR_WHITE);
        break;
    case EnemyType_Bat:
        Font_PrintString("Killed by a bat, lvl", 1, 2, COLOUR_WHITE);
        Font_PrintInt(gameFloor, 1, 84, COLOUR_WHITE);
        break;
    case EnemyType_Spider:
        Font_PrintString("Killed by a spider, lvl", 1, 2, COLOUR_WHITE);
        Font_PrintInt(gameFloor, 1, 96, COLOUR_WHITE);
        break;
    }

    Font_PrintString("LOOT:", 2, 0, COLOUR_WHITE);

    Renderer_DrawScaled(chestSpriteData, 0, firstRow, 9, 255, 0);
    Font_PrintInt(stats.chestsOpened, 4, 18, COLOUR_WHITE);

    Renderer_DrawScaled(crownSpriteData, 0, secondRow, 9, 255, 0);
    Font_PrintInt(stats.crownsCollected, 6, 18, COLOUR_WHITE);

    Renderer_DrawScaled(scrollSpriteData, 26, firstRow, 9, 255, 0);
    Font_PrintInt(stats.scrollsCollected, 4, 44, COLOUR_WHITE);

    Renderer_DrawScaled(coinsSpriteData, 26, secondRow, 9, 255, 0);
    Font_PrintInt(stats.coinsCollected, 6, 44, COLOUR_WHITE);

    offset = (u8)((globalTickFrame & 8) == 0 ? 32 : 0);
    Font_PrintString("KILLS:", 2, 52, COLOUR_WHITE);

    Renderer_DrawScaled(skeletonSpriteData + offset, 52, firstRow, 9, 255, 0);
    Font_PrintInt(stats.enemyKills[EnemyType_Skeleton], 4, 70, COLOUR_WHITE);

    Renderer_DrawScaled(mageSpriteData + offset, 52, secondRow, 9, 255, 0);
    Font_PrintInt(stats.enemyKills[EnemyType_Mage], 6, 70, COLOUR_WHITE);

    Renderer_DrawScaled(batSpriteData + offset, 78, firstRow, 9, 255, 1);
    Font_PrintInt(stats.enemyKills[EnemyType_Bat], 4, 96, COLOUR_WHITE);

    Renderer_DrawScaled(spiderSpriteData + offset, 78, secondRow, 9, 255, 0);
    Font_PrintInt(stats.enemyKills[EnemyType_Spider], 6, 96, COLOUR_WHITE);

    /* Final score */
    finalScore = (u16)(finalScore + (gameFloor - 1) * 20);

    if (stats.killedBy == EnemyType_None) finalScore = (u16)(finalScore + 500);
    finalScore = (u16)(finalScore + stats.chestsOpened * 15);
    finalScore = (u16)(finalScore + stats.crownsCollected * 10);
    finalScore = (u16)(finalScore + stats.scrollsCollected * 8);
    finalScore = (u16)(finalScore + stats.coinsCollected * 4);
    finalScore = (u16)(finalScore + stats.enemyKills[EnemyType_Skeleton] * 10);
    finalScore = (u16)(finalScore + stats.enemyKills[EnemyType_Mage] * 10);
    finalScore = (u16)(finalScore + stats.enemyKills[EnemyType_Bat] * 5);
    finalScore = (u16)(finalScore + stats.enemyKills[EnemyType_Spider] * 4);

    Font_PrintString("FINAL SCORE:", 7, 8, COLOUR_WHITE);
    Font_PrintInt(finalScore, 7, 58, COLOUR_WHITE);
}

void Menu_FadeOut(void)
{
    const u16 toggleMask = 0x1f6e;
    const u16 startValue = 1;
    s16 n;

    if (menuState == 0) menuState = startValue;

    for (n = 0; n < 512; n++) {
        u8 lsb = (u8)((menuState & 1) != 0);
        menuState >>= 1;
        if (lsb) menuState ^= toggleMask;

        Platform_PutPixel((u8)(menuState & 0x7f), (u8)(menuState >> 7), COLOUR_BLACK);

        if (menuState == startValue) {
            Game_SwitchState(GameState_GameOver);
            return;
        }
    }
}
