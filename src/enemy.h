#ifndef ENEMY_H
#define ENEMY_H

#include "defines.h"
#include "entity.h"
#include "draw.h"

/* enum class EnemyType */
#define EnemyType_Skeleton      0
#define EnemyType_Mage          1
#define EnemyType_Bat           2
#define EnemyType_Spider        3
#define EnemyType_NumEnemyTypes 4
#define EnemyType_None          EnemyType_NumEnemyTypes
typedef u8 EnemyType;

/* enum class EnemyState */
#define EnemyState_Idle      0
#define EnemyState_Moving    1
#define EnemyState_Attacking 2
#define EnemyState_Stunned   3
#define EnemyState_Dying     4
#define EnemyState_Dead      5
typedef u8 EnemyState;

typedef struct {
    const u16 *spriteData;
    u8 hp;
    u8 movementSpeed;
    u8 attackStrength;
    u8 attackDuration;
    u8 stunDuration;
    u8 isRanged;
    u8 spriteScale;
    AnchorType spriteAnchor;
} EnemyArchetype;

typedef struct {
    s16 x, y;              /* must stay first: Entity base */
    u8 type : 3;
    u8 state : 3;
    u8 frameDelay : 2;
    u8 hp;
    u8 targetCellX, targetCellY;
} Enemy;

#define MAX_ENEMIES 24
extern Enemy enemies[MAX_ENEMIES];

#define Enemy_IsValid(e)  ((e)->type != EnemyType_None)
#define Enemy_Clear(e)    ((e)->type = EnemyType_None)
#define Enemy_GetState(e) ((e)->state)
#define Enemy_GetType(e)  ((e)->type)

void Enemy_Init(Enemy *e, EnemyType type, s16 x, s16 y);
void Enemy_Tick(Enemy *e);
void Enemy_Damage(Enemy *e, u8 amount);
const EnemyArchetype *Enemy_GetArchetype(const Enemy *e);

void EnemyManager_Spawn(EnemyType enemyType, s16 x, s16 y);
void EnemyManager_SpawnEnemies(void);
Enemy *EnemyManager_GetOverlappingEntity(Entity *entity);
Enemy *EnemyManager_GetOverlappingEnemyAt(s16 x, s16 y);
void EnemyManager_Init(void);
void EnemyManager_Draw(void);
void EnemyManager_Update(void);

#endif
