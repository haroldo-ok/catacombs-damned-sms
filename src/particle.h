#ifndef PARTICLE_H
#define PARTICLE_H

#include "defines.h"

#define PARTICLE_GRAVITY 3
#define MAX_PARTICLE_SYSTEMS 3

typedef struct {
    s8 x, y;
    s8 velX, velY;
} Particle;

typedef struct {
    s16 worldX, worldY;
    u8 isWhite : 1;
    u8 life : 7;
    Particle particles[PARTICLES_PER_SYSTEM];
} ParticleSystem;

extern ParticleSystem particleSystems[MAX_PARTICLE_SYSTEMS];

#define Particle_IsActive(p)       ((p)->x != -128)
#define ParticleSystem_IsActive(s) ((s)->life > 0)

void ParticleSystem_Init(ParticleSystem *s);
void ParticleSystem_Step(ParticleSystem *s);
void ParticleSystem_Draw(ParticleSystem *s, s16 x, s16 halfScale);
void ParticleSystem_Explode(ParticleSystem *s);

void ParticleSystemManager_Init(void);
void ParticleSystemManager_Draw(void);
void ParticleSystemManager_Update(void);
void ParticleSystemManager_CreateExplosion(s16 x, s16 y, u8 isWhite);

#endif
