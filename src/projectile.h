#ifndef PROJECTILE_H
#define PROJECTILE_H

#include "defines.h"
#include "entity.h"

#define PROJECTILE_PLAYER_OWNER_ID 0xff
#define MAX_PROJECTILES 4

typedef struct {
    s16 x, y;              /* must stay first: Entity base */
    u8  angle;
    u8  life;
    u8  ownerId;
} Projectile;

extern Projectile projectiles[MAX_PROJECTILES];

Entity *Projectile_GetOwner(const Projectile *p);
Projectile *ProjectileManager_FireProjectile(Entity *owner, s16 x, s16 y, u8 angle);
void ProjectileManager_Init(void);
void ProjectileManager_Draw(void);
void ProjectileManager_Update(void);

#endif
