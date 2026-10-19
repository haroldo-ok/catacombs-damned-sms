#ifndef ENTITY_H
#define ENTITY_H

#include "defines.h"

#define ENTITY_SIZE 192

/* The original derives Player/Enemy/Projectile from Entity, whose only data
 * are the world coordinates.  In C the same effect is obtained by making
 * `x, y` the first two members of every entity struct and casting. */
typedef struct {
    s16 x, y;
} Entity;

u8 Entity_IsOverlappingEntity(const Entity *a, const Entity *b);
u8 Entity_IsOverlappingPoint(const Entity *e, s16 pointX, s16 pointY);

#endif
