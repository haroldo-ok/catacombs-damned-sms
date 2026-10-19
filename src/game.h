#ifndef GAME_H
#define GAME_H

#include "defines.h"
#include "enemy.h"

typedef struct {
    EnemyType killedBy;
    u8 enemyKills[EnemyType_NumEnemyTypes];
    u8 chestsOpened;
    u8 crownsCollected;
    u8 scrollsCollected;
    u8 coinsCollected;
} Stats;

/* enum class Game::State */
#define GameState_Menu          0
#define GameState_EnteringLevel 1
#define GameState_InGame        2
#define GameState_GameOver      3
#define GameState_FadeOut       4

extern u8 globalTickFrame;
extern u8 gameState;
extern u8 gameFloor;
extern Stats stats;
extern const char *displayMessage;
extern u8 displayMessageTime;

void Game_Init(void);
void Game_Tick(void);
void Game_Draw(void);
void Game_StartGame(void);
void Game_StartLevel(void);
void Game_NextLevel(void);
void Game_GameOver(void);
void Game_SwitchState(u8 newState);
void Game_ShowMessage(const char *message);
void Stats_Reset(void);

#endif
