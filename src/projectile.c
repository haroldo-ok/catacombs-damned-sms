#include "defines.h"
#include "projectile.h"
#include "map.h"
#include "fixedmath.h"
#include "particle.h"
#include "enemy.h"
#include "gamedata.h"
#include "platform.h"
#include "draw.h"
#include "game.h"
#include "player.h"

Projectile projectiles[MAX_PROJECTILES];

Projectile *ProjectileManager_FireProjectile(Entity *owner, s16 x, s16 y, u8 angle)
{
    u8 i;
    for (i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *p = &projectiles[i];
        if (p->life == 0) {
            if (owner == (Entity *)&player) {
                p->ownerId = PROJECTILE_PLAYER_OWNER_ID;
            } else {
                u8 n;
                for (n = 0; n < MAX_ENEMIES; n++) {
                    if ((Entity *)&enemies[n] == owner) { p->ownerId = n; break; }
                }
            }

            p->life = 255;
            p->x = x;
            p->y = y;
            p->angle = angle;
            return p;
        }
    }
    return 0;
}

Entity *Projectile_GetOwner(const Projectile *p)
{
    if (p->ownerId == PROJECTILE_PLAYER_OWNER_ID) return (Entity *)&player;
    return (Entity *)&enemies[p->ownerId];
}

void ProjectileManager_Update(void)
{
    u8 i;
    for (i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *p = &projectiles[i];
        if (p->life > 0) {
            s16 deltaX, deltaY;
            u8 hitAnything = 0;
            Entity *owner;

            p->life--;

            deltaX = (s16)(FixedCos(p->angle) / 4);
            deltaY = (s16)(FixedSin(p->angle) / 4);

            p->x = (s16)(p->x + deltaX);
            p->y = (s16)(p->y + deltaY);

            owner = Projectile_GetOwner(p);

            if (Map_IsBlockedAtWorldPosition(p->x, p->y)) {
                u8 cellX = (u8)(p->x / CELL_SIZE);
                u8 cellY = (u8)(p->y / CELL_SIZE);

                if (Map_GetCellSafe(cellX, cellY) == CellType_Urn) {
                    Map_SetCell(cellX, cellY, CellType_Empty);
                    ParticleSystemManager_CreateExplosion(
                        (s16)(cellX * CELL_SIZE + CELL_SIZE / 2),
                        (s16)(cellY * CELL_SIZE + CELL_SIZE / 2), 1);

                    switch (Random() % 5) {
                    case 0:
                        EnemyManager_Spawn(EnemyType_Spider,
                                           (s16)(cellX * CELL_SIZE + CELL_SIZE / 2),
                                           (s16)(cellY * CELL_SIZE + CELL_SIZE / 2));
                        break;
                    case 1: Map_SetCell(cellX, cellY, CellType_Potion); break;
                    case 2: Map_SetCell(cellX, cellY, CellType_Coins); break;
                    default: break;
                    }
                    Platform_PlaySound(Sound_Kill);
                } else {
                    Platform_PlaySound(Sound_Hit);
                }

                hitAnything = 1;
            } else {
                if (owner == (Entity *)&player) {
                    Enemy *overlappingEnemy = EnemyManager_GetOverlappingEnemyAt(p->x, p->y);
                    if (overlappingEnemy) {
                        Enemy_Damage(overlappingEnemy, PLAYER_ATTACK_STRENGTH);
                        hitAnything = 1;
                    }
                } else if (Entity_IsOverlappingPoint((Entity *)&player, p->x, p->y)) {
                    const EnemyArchetype *enemyArchetype = Enemy_GetArchetype((Enemy *)owner);
                    if (enemyArchetype) {
                        Player_Damage(enemyArchetype->attackStrength);
                        if (player.hp == 0) stats.killedBy = Enemy_GetType((Enemy *)owner);
                    }
                    hitAnything = 1;
                }
            }

            if (hitAnything) {
                ParticleSystemManager_CreateExplosion((s16)(p->x - deltaX), (s16)(p->y - deltaY), 0);
                p->life = 0;
            }
        }
    }
}

void ProjectileManager_Init(void)
{
    u8 i;
    for (i = 0; i < MAX_PROJECTILES; i++) projectiles[i].life = 0;
}

void ProjectileManager_Draw(void)
{
    u8 i;
    for (i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *p = &projectiles[i];
        if (p->life > 0) {
            Renderer_DrawObject(
                p->ownerId == PROJECTILE_PLAYER_OWNER_ID ? projectileSpriteData
                                                         : enemyProjectileSpriteData,
                p->x, p->y, 32, AnchorType_BelowCenter, 0);
        }
    }
}
