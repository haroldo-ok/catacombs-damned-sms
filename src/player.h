#ifndef PLAYER_H
#define PLAYER_H

#include "defines.h"
#include "entity.h"

#define PLAYER_MAX_HP          100
#define PLAYER_MAX_MANA        100
#define PLAYER_MANA_FIRE_COST  20
#define PLAYER_MANA_RECHARGE   1
#define PLAYER_ATTACK_STRENGTH 10
#define PLAYER_COLLISION_SIZE  48
#define PLAYER_LOOKAHEAD       60
#define PLAYER_POTION_STRENGTH 25

typedef struct {
    s16 x, y;              /* must stay first: Entity base */
    u8  angle;
    s16 velocityX, velocityY;
    s8  angularVelocity;
    u8  shakeTime;
    u8  damageTime;
    u8  reloadTime;
    u8  hp;
    u8  mana;
} Player;

extern Player player;

void Player_Init(void);
void Player_NextLevel(void);
void Player_Tick(void);
void Player_Fire(void);
void Player_Move(s16 deltaX, s16 deltaY);
u8   Player_CheckCollisions(void);
void Player_Damage(u8 amount);
u8   Player_IsWorldColliding(void);

#endif
