#include "particle.h"
#include "fixedmath.h"
#include "platform.h"
#include "draw.h"

ParticleSystem particleSystems[MAX_PARTICLE_SYSTEMS];

void ParticleSystem_Init(ParticleSystem *s)
{
    s->life = 0;
}

void ParticleSystem_Step(ParticleSystem *s)
{
    u8 i;
    for (i = 0; i < PARTICLES_PER_SYSTEM; i++) {
        Particle *p = &s->particles[i];
        if (Particle_IsActive(p)) {
            p->velY = (s8)(p->velY + PARTICLE_GRAVITY);

            if (p->x + p->velX < -127 || p->x + p->velX > 127 || p->y + p->velY < -127) {
                p->x = -128;
                continue;
            }

            if (p->y + p->velY >= 128) {
                p->velY = p->velX = 0;
                p->y = 127;
            }

            p->x = (s8)(p->x + p->velX);
            p->y = (s8)(p->y + p->velY);
        }
    }

    s->life--;
}

void ParticleSystem_Draw(ParticleSystem *s, s16 x, s16 halfScale)
{
    s16 scale = (s16)(2 * halfScale);
    s8 horizon = Renderer_GetHorizon(x);
    u8 colour = (u8)(s->isWhite ? COLOUR_WHITE : COLOUR_BLACK);
    u8 i;

    for (i = 0; i < PARTICLES_PER_SYSTEM; i++) {
        Particle *p = &s->particles[i];
        if (Particle_IsActive(p)) {
            s16 outX = (s16)(x + ((p->x * scale) >> 8));
            s16 outY = (s16)(horizon + ((p->y * scale) >> 8));

            if (outX >= 0 && outY >= 0 && outX < DISPLAY_WIDTH - 1 && outY < DISPLAY_HEIGHT - 1
                && halfScale >= wBuffer[outX]) {
                Platform_PutPixel((u8)outX, (u8)outY, colour);
                Platform_PutPixel((u8)(outX + 1), (u8)outY, colour);
                Platform_PutPixel((u8)(outX + 1), (u8)(outY + 1), colour);
                Platform_PutPixel((u8)outX, (u8)(outY + 1), colour);
            }
        }
    }
}

void ParticleSystem_Explode(ParticleSystem *s)
{
    u8 i;
    for (i = 0; i < PARTICLES_PER_SYSTEM; i++) {
        Particle *p = &s->particles[i];
        p->x = (s8)((Random() & 31) - 16);
        p->y = (s8)((Random() & 31) - 16);
        p->velX = (s8)((Random() & 31) - 16);
        p->velY = (s8)((Random() & 31) - 25);
    }
    s->life = 22;
}

void ParticleSystemManager_Draw(void)
{
    u8 i;
    for (i = 0; i < MAX_PARTICLE_SYSTEMS; i++) {
        ParticleSystem *system = &particleSystems[i];
        if (ParticleSystem_IsActive(system)) {
            s16 screenX, screenW;

            if (Renderer_TransformAndCull(system->worldX, system->worldY, &screenX, &screenW)) {
                QueuedDrawable *drawable = Renderer_CreateQueuedDrawable((u8)screenW);
                if (drawable) {
                    drawable->type = DrawableType_ParticleSystem;
                    drawable->x = (s8)screenX;
                    drawable->inverseCameraDistance = (u8)screenW;
                    drawable->ptr = (const void *)system;
                }
            }
        }
    }
}

void ParticleSystemManager_Init(void)
{
    u8 i;
    for (i = 0; i < MAX_PARTICLE_SYSTEMS; i++) ParticleSystem_Init(&particleSystems[i]);
}

void ParticleSystemManager_Update(void)
{
    u8 i;
    for (i = 0; i < MAX_PARTICLE_SYSTEMS; i++) {
        ParticleSystem *system = &particleSystems[i];
        if (ParticleSystem_IsActive(system)) ParticleSystem_Step(system);
    }
}

void ParticleSystemManager_CreateExplosion(s16 worldX, s16 worldY, u8 isWhite)
{
    ParticleSystem *newSystem = 0;
    u8 i;

    for (i = 0; i < MAX_PARTICLE_SYSTEMS; i++) {
        if (!ParticleSystem_IsActive(&particleSystems[i])) {
            newSystem = &particleSystems[i];
            break;
        }
    }

    if (!newSystem) {
        newSystem = &particleSystems[0];
        for (i = 1; i < MAX_PARTICLE_SYSTEMS; i++) {
            if (particleSystems[i].life < newSystem->life) newSystem = &particleSystems[i];
        }
    }

    newSystem->worldX = worldX;
    newSystem->worldY = worldY;
    newSystem->isWhite = isWhite ? 1 : 0;
    ParticleSystem_Explode(newSystem);
}
