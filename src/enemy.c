#include "enemy.h"
#include "defines.h"
#include "draw.h"
#include "map.h"
#include "fixedmath.h"
#include "game.h"
#include "player.h"
#include "projectile.h"
#include "gamedata.h"
#include "platform.h"
#include "particle.h"

Enemy enemies[MAX_ENEMIES];

static const EnemyArchetype archetypes[EnemyType_NumEnemyTypes] = {
    {   /* Skeleton */
        skeletonSpriteData,
        50,                 /* hp */
        4,                  /* speed */
        20,                 /* attackStrength */
        3,                  /* attackDuration */
        2,                  /* stunDuration */
        0,                  /* isRanged */
        96,                 /* sprite scale */
        AnchorType_Floor
    },
    {   /* Mage */
        mageSpriteData,
        30, 5, 20, 3, 2, 1, 96, AnchorType_Floor
    },
    {   /* Bat */
        batSpriteData,
        20, 7, 10, 2, 0, 0, 80, AnchorType_Center
    },
    {   /* Spider */
        spiderSpriteData,
        10, 7, 5, 1, 0, 0, 50, AnchorType_Floor
    }
};

const EnemyArchetype *Enemy_GetArchetype(const Enemy *e)
{
    if (e->type == EnemyType_None) return 0;
    return &archetypes[e->type];
}

void Enemy_Init(Enemy *e, EnemyType initType, s16 initX, s16 initY)
{
    e->state = EnemyState_Idle;
    e->type = initType;
    e->x = initX;
    e->y = initY;
    e->frameDelay = 0;
    e->targetCellX = (u8)(e->x / CELL_SIZE);
    e->targetCellY = (u8)(e->y / CELL_SIZE);
    e->hp = Enemy_GetArchetype(e)->hp;
}

void Enemy_Damage(Enemy *e, u8 amount)
{
    if (amount >= e->hp) {
        stats.enemyKills[e->type]++;
        e->type = EnemyType_None;
        Platform_PlaySound(Sound_Kill);
        ParticleSystemManager_CreateExplosion(e->x, e->y, 1);
    } else {
        e->hp -= amount;
        Platform_PlaySound(Sound_Hit);
        e->state = EnemyState_Stunned;
        e->frameDelay = Enemy_GetArchetype(e)->stunDuration;
    }
}

static s16 Clamp(s16 x, s16 minv, s16 maxv)
{
    if (x < minv) return minv;
    if (x > maxv) return maxv;
    return x;
}

static u8 Enemy_TryPickCell(Enemy *e, s8 newX, s8 newY)
{
    u8 n;

    if (Map_IsBlocked((u8)newX, (u8)newY)) return 0;
    if (Map_IsBlocked(e->targetCellX, (u8)newY)) return 0;
    if (Map_IsBlocked((u8)newX, e->targetCellY)) return 0;

    for (n = 0; n < MAX_ENEMIES; n++) {
        Enemy *other = &enemies[n];
        if (e != other && Enemy_IsValid(other)) {
            if (other->targetCellX == (u8)newX && other->targetCellY == (u8)newY) return 0;
        }
    }

    e->targetCellX = (u8)newX;
    e->targetCellY = (u8)newY;
    return 1;
}

static u8 Enemy_TryPickCells(Enemy *e, s8 deltaX, s8 deltaY)
{
    return (u8)(Enemy_TryPickCell(e, (s8)(e->targetCellX + deltaX), (s8)(e->targetCellY + deltaY))
             || Enemy_TryPickCell(e, (s8)(e->targetCellX + deltaX), (s8)(e->targetCellY))
             || Enemy_TryPickCell(e, (s8)(e->targetCellX),          (s8)(e->targetCellY + deltaY))
             || Enemy_TryPickCell(e, (s8)(e->targetCellX - deltaX), (s8)(e->targetCellY + deltaY))
             || Enemy_TryPickCell(e, (s8)(e->targetCellX + deltaX), (s8)(e->targetCellY - deltaY)));
}

static u8 Enemy_GetPlayerCellDistance(const Enemy *e)
{
    u8 dx = (u8)(ABS(player.x - e->x) / CELL_SIZE);
    u8 dy = (u8)(ABS(player.y - e->y) / CELL_SIZE);
    return (u8)(dx > dy ? dx : dy);
}

static void Enemy_PickNewTargetCell(Enemy *e)
{
    s8 deltaX = (s8)Clamp((s16)((player.x / CELL_SIZE) - e->targetCellX), -1, 1);
    s8 deltaY = (s8)Clamp((s16)((player.y / CELL_SIZE) - e->targetCellY), -1, 1);
    u8 dodgeChance = (u8)Random();

    if (Enemy_GetArchetype(e)->isRanged && Enemy_GetPlayerCellDistance(e) < 3) {
        deltaX = (s8)(-deltaX);
        deltaY = (s8)(-deltaY);
    }

    if (deltaX == 0) {
        if (dodgeChance < 64) deltaX = -1;
        else if (dodgeChance < 128) deltaX = 1;
    } else if (deltaY == 0) {
        if (dodgeChance < 64) deltaY = -1;
        else if (dodgeChance < 128) deltaY = 1;
    }

    Enemy_TryPickCells(e, deltaX, deltaY);
}

static void Enemy_StunMove(void)
{
    /* The original body is entirely commented out; kept for parity. */
}

static u8 Enemy_TryMove(Enemy *e)
{
    s16 targetX, targetY, maxDelta, deltaX, deltaY;

    if (Map_IsSolid(e->targetCellX, e->targetCellY)) return 0;

    targetX = (s16)((e->targetCellX * CELL_SIZE) + CELL_SIZE / 2);
    targetY = (s16)((e->targetCellY * CELL_SIZE) + CELL_SIZE / 2);

    maxDelta = Enemy_GetArchetype(e)->movementSpeed;

    deltaX = Clamp((s16)(targetX - e->x), (s16)(-maxDelta), maxDelta);
    deltaY = Clamp((s16)(targetY - e->y), (s16)(-maxDelta), maxDelta);

    e->x = (s16)(e->x + deltaX);
    e->y = (s16)(e->y + deltaY);

    if (Entity_IsOverlappingEntity((Entity *)e, (Entity *)&player)) {
        if (!Enemy_GetArchetype(e)->isRanged) {
            Player_Damage(Enemy_GetArchetype(e)->attackStrength);
            if (player.hp == 0) stats.killedBy = e->type;

            e->state = EnemyState_Attacking;
            e->frameDelay = Enemy_GetArchetype(e)->attackDuration;
        }

        e->x = (s16)(e->x - deltaX);
        e->y = (s16)(e->y - deltaY);
        return 0;
    }

    if (e->x == targetX && e->y == targetY) Enemy_PickNewTargetCell(e);
    return 1;
}

static u8 Enemy_FireProjectile(Enemy *e, u8 angle)
{
    return (u8)(ProjectileManager_FireProjectile((Entity *)e, e->x, e->y, angle) != 0);
}

static u8 Enemy_TryFireProjectile(Enemy *e)
{
    s8 deltaX = (s8)((player.x - e->x) / CELL_SIZE);
    s8 deltaY = (s8)((player.y - e->y) / CELL_SIZE);

    if (deltaX == 0) {
        if (deltaY < 0) return Enemy_FireProjectile(e, FIXED_ANGLE_270);
        else if (deltaY > 0) return Enemy_FireProjectile(e, FIXED_ANGLE_90);
    } else if (deltaY == 0) {
        if (deltaX < 0) return Enemy_FireProjectile(e, FIXED_ANGLE_180);
        else if (deltaX > 0) return Enemy_FireProjectile(e, 0);
    } else if (deltaX == deltaY) {
        if (deltaX > 0) return Enemy_FireProjectile(e, FIXED_ANGLE_45);
        else return Enemy_FireProjectile(e, FIXED_ANGLE_180 + FIXED_ANGLE_45);
    } else if (deltaX == -deltaY) {
        if (deltaX > 0) return Enemy_FireProjectile(e, FIXED_ANGLE_270 + FIXED_ANGLE_45);
        else return Enemy_FireProjectile(e, FIXED_ANGLE_90 + FIXED_ANGLE_45);
    }

    return 0;
}

static u8 Enemy_ShouldFireProjectile(Enemy *e)
{
    u8 distance = Enemy_GetPlayerCellDistance(e);
    u8 chance = (u8)(16 / (distance > 0 ? distance : 1));

    return (u8)(Enemy_GetArchetype(e)->isRanged
                && (Random() & 0xff) < chance
                && Map_IsClearLine(e->x, e->y, player.x, player.y));
}

void Enemy_Tick(Enemy *e)
{
    if (e->state == EnemyState_Stunned) Enemy_StunMove();

    if (e->frameDelay > 0) {
        if ((globalTickFrame & 0xf) == 0) e->frameDelay--;
        return;
    }

    switch (e->state) {
    case EnemyState_Idle:
        if (Map_IsClearLine(e->x, e->y, player.x, player.y)) {
            Platform_PlaySound(Sound_SpotPlayer);
            e->state = EnemyState_Moving;
        }
        break;
    case EnemyState_Moving:
        Enemy_TryMove(e);

        if (Enemy_ShouldFireProjectile(e)) {
            if (Enemy_TryFireProjectile(e)) {
                Platform_PlaySound(Sound_Shoot);
                e->state = EnemyState_Attacking;
                e->frameDelay = Enemy_GetArchetype(e)->attackDuration;
            }
        }
        break;
    case EnemyState_Attacking:
        e->state = EnemyState_Moving;
        break;
    case EnemyState_Stunned:
        e->state = EnemyState_Moving;
        break;
    default:
        break;
    }
}

void EnemyManager_Init(void)
{
    u8 n;
    for (n = 0; n < MAX_ENEMIES; n++) Enemy_Clear(&enemies[n]);
}

void EnemyManager_Update(void)
{
    u8 n;
    for (n = 0; n < MAX_ENEMIES; n++) {
        if (Enemy_IsValid(&enemies[n])) Enemy_Tick(&enemies[n]);
    }
}

void EnemyManager_Draw(void)
{
    u8 n;
    for (n = 0; n < MAX_ENEMIES; n++) {
        Enemy *enemy = &enemies[n];
        if (Enemy_IsValid(enemy)) {
            u8 invert = (u8)(enemy->state == EnemyState_Stunned && (globalRenderFrame & 1));
            u8 frameOffset = (u8)(((enemy->type == EnemyType_Bat || enemy->state == EnemyState_Moving)
                                   && (globalTickFrame & 8) == 0) ? 32 : 0);
            const EnemyArchetype *archetype = Enemy_GetArchetype(enemy);

            Renderer_DrawObject(archetype->spriteData + frameOffset, enemy->x, enemy->y,
                                archetype->spriteScale, archetype->spriteAnchor, invert);
        }
    }
}

void EnemyManager_Spawn(EnemyType enemyType, s16 x, s16 y)
{
    u8 n;
    for (n = 0; n < MAX_ENEMIES; n++) {
        if (!Enemy_IsValid(&enemies[n])) {
            Enemy_Init(&enemies[n], enemyType, x, y);
            return;
        }
    }
}

void EnemyManager_SpawnEnemies(void)
{
    u8 x, y;
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            if (Map_GetCellSafe(x, y) == CellType_Monster) {
                EnemyType type = (EnemyType)(Random() % EnemyType_NumEnemyTypes);
                EnemyManager_Spawn(type, (s16)(x * CELL_SIZE + CELL_SIZE / 2),
                                   (s16)(y * CELL_SIZE + CELL_SIZE / 2));
                Map_SetCell(x, y, CellType_Empty);
                break;
            }
        }
    }
}

Enemy *EnemyManager_GetOverlappingEntity(Entity *entity)
{
    u8 n;
    for (n = 0; n < MAX_ENEMIES; n++) {
        Enemy *enemy = &enemies[n];
        if (Enemy_IsValid(enemy) && Entity_IsOverlappingEntity((Entity *)enemy, entity))
            return enemy;
    }
    return 0;
}

Enemy *EnemyManager_GetOverlappingEnemyAt(s16 x, s16 y)
{
    u8 n;
    for (n = 0; n < MAX_ENEMIES; n++) {
        Enemy *enemy = &enemies[n];
        if (Enemy_IsValid(enemy) && Entity_IsOverlappingPoint((Entity *)enemy, x, y))
            return enemy;
    }
    return 0;
}
