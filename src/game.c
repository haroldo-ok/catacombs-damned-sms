#include "defines.h"
#include "game.h"
#include "fixedmath.h"
#include "draw.h"
#include "map.h"
#include "projectile.h"
#include "particle.h"
#include "mapgen.h"
#include "platform.h"
#include "entity.h"
#include "enemy.h"
#include "menu.h"
#include "player.h"


const char *displayMessage = 0;
u8 displayMessageTime = 0;
u8 gameState = GameState_Menu;
u8 gameFloor = 1;
u8 globalTickFrame = 0;
Stats stats;

void Game_Init(void)
{
    Menu_Init();
    ParticleSystemManager_Init();
    ProjectileManager_Init();
    EnemyManager_Init();
}

void Game_StartGame(void)
{
    gameFloor = 1;
    Stats_Reset();
    Player_Init();
    Game_SwitchState(GameState_EnteringLevel);
}

void Game_SwitchState(u8 newState)
{
    /* NOTE: `if (gameState != newState) { gameState = newState; ... }` written
     * directly makes SDCC 4.5.0 emit a destructive `sub a,(hl)` for the test
     * and then store the *difference* - see tools/checkasm.py.  Loading the
     * global into a temporary first makes it emit `cp` and keeps A intact. */
    u8 current = gameState;
    if (current != newState) {
        gameState = newState;
        Menu_ResetTimer();
    }
}

void Game_ShowMessage(const char *message)
{
    displayMessage = message;
    displayMessageTime = 90;
}

void Game_NextLevel(void)
{
    if (gameFloor == 10) {
        Game_GameOver();
    } else {
        gameFloor++;
        Game_SwitchState(GameState_EnteringLevel);
    }
}


void Game_StartLevel(void)
{
    /* Generating the dungeon and rendering the first view of it take a few
     * seconds on a 3.58 MHz Z80, so the "Entering floor N" caption gets a
     * sweeping bar underneath it instead of just freezing. */
    Menu_BeginLoading();

    ParticleSystemManager_Init();
    ProjectileManager_Init();
    EnemyManager_Init();
    MapGenerator_Generate();
    Menu_LoadingStep();
    EnemyManager_SpawnEnemies();
    Menu_LoadingStep();

    Player_NextLevel();

    Platform_ExpectLoadDelay();
    /* Whatever the player is leaning on right now belongs to the menu, not to
     * the level that is about to start. */
    Platform_ConsumeInput();
    Game_SwitchState(GameState_InGame);
}

void Game_Draw(void)
{
    switch (gameState) {
    case GameState_Menu:
        Menu_Draw();
        break;
    case GameState_EnteringLevel:
        Menu_DrawEnteringLevel();
        break;
    case GameState_InGame:
        camera.x = player.x;
        camera.y = player.y;
        camera.angle = player.angle;
        Renderer_Render();
        break;
    case GameState_GameOver:
        Menu_DrawGameOver();
        break;
    case GameState_FadeOut:
        Menu_FadeOut();
        break;
    }
}

static void Game_TickInGame(void)
{
    if (displayMessageTime > 0) {
        displayMessageTime--;
        if (displayMessageTime == 0) displayMessage = 0;
    }

    Player_Tick();

    ProjectileManager_Update();
    ParticleSystemManager_Update();
    EnemyManager_Update();

    if (Map_GetCellSafe((u8)(player.x / CELL_SIZE), (u8)(player.y / CELL_SIZE)) == CellType_Exit)
        Game_NextLevel();

    if (player.hp == 0) Game_GameOver();
}

/* Test hook: when set, the world stops advancing but rendering continues, so
 * a scripted screenshot depends only on the map and the player state and not
 * on how fast the build happens to run (tests/golden.txt). */
u8 freezeLogic;

void Game_Tick(void)
{
    if (freezeLogic) return;

    globalTickFrame++;

    switch (gameState) {
    case GameState_InGame:        Game_TickInGame();      return;
    case GameState_EnteringLevel: Menu_TickEnteringLevel(); return;
    case GameState_Menu:          Menu_Tick();            return;
    case GameState_GameOver:      Menu_TickGameOver();    return;
    }
}

void Game_GameOver(void)
{
    Game_SwitchState(GameState_FadeOut);
}

void Stats_Reset(void)
{
    u8 n;
    stats.killedBy = EnemyType_None;
    stats.chestsOpened = 0;
    stats.coinsCollected = 0;
    stats.crownsCollected = 0;
    stats.scrollsCollected = 0;
    for (n = 0; n < EnemyType_NumEnemyTypes; n++) stats.enemyKills[n] = 0;
}
