#include "entity.h"

u8 Entity_IsOverlappingEntity(const Entity *a, const Entity *b)
{
    return (u8)(a->x >= b->x - ENTITY_SIZE && a->x <= b->x + ENTITY_SIZE
             && a->y >= b->y - ENTITY_SIZE && a->y <= b->y + ENTITY_SIZE);
}

u8 Entity_IsOverlappingPoint(const Entity *e, s16 pointX, s16 pointY)
{
    return (u8)(pointX >= e->x - ENTITY_SIZE / 2 && pointX <= e->x + ENTITY_SIZE / 2
             && pointY >= e->y - ENTITY_SIZE / 2 && pointY <= e->y + ENTITY_SIZE / 2);
}
