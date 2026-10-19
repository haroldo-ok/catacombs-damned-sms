#include "player.h"
#include "game.h"
#include "fixedmath.h"
#include "projectile.h"
#include "platform.h"
#include "draw.h"
#include "enemy.h"
#include "map.h"
#include "gamedata.h"
#include "particle.h"

#define USE_ROTATE_BOB 0
#define STRAFE_TILT 14
#define ROTATE_TILT 3

Player player;

static const char SignMessage1[] = "Abandon all hope ye who enter!";

void Player_NextLevel(void)
{
    player.x = CELL_SIZE * 1 + CELL_SIZE / 2;
    player.y = CELL_SIZE * 1 + CELL_SIZE / 2;
    player.angle = FIXED_ANGLE_45;
    player.mana = PLAYER_MAX_MANA;
    player.damageTime = 0;
    player.shakeTime = 0;
    player.reloadTime = 0;
    player.velocityX = 0;
    player.velocityY = 0;
    player.angularVelocity = 0;
}

void Player_Init(void)
{
    Player_NextLevel();
    player.hp = PLAYER_MAX_HP;
}

void Player_Fire(void)
{
    if (player.mana >= PLAYER_MANA_FIRE_COST) {
        s16 projectileX, projectileY;

        player.reloadTime = 8;
        player.shakeTime = 6;

        projectileX = (s16)(player.x + FixedCos(player.angle + FIXED_ANGLE_90 / 2) / 4);
        projectileY = (s16)(player.y + FixedSin(player.angle + FIXED_ANGLE_90 / 2) / 4);

        ProjectileManager_FireProjectile((Entity *)&player, projectileX, projectileY, player.angle);
        player.mana -= PLAYER_MANA_FIRE_COST;
        Platform_PlaySound(Sound_Attack);
    }
}

void Player_Tick(void)
{
    static s16 tiltTimer = 0;

    u8 input = Platform_GetInput();
    s8 turnDelta = 0;
    s8 targetTilt = 0;
    s8 moveDelta = 0;
    s8 strafeDelta = 0;
    s8 targetBob;
    s16 cosAngle, sinAngle, cos90Angle, sin90Angle;
    u8 cellX, cellY;

    if (input & INPUT_A) {
        if (input & INPUT_LEFT)  strafeDelta--;
        if (input & INPUT_RIGHT) strafeDelta++;
    } else {
        if (input & INPUT_LEFT)  turnDelta = (s8)(turnDelta - TURN_SPEED * 2);
        if (input & INPUT_RIGHT) turnDelta = (s8)(turnDelta + TURN_SPEED * 2);
    }

    if (player.reloadTime > 0) player.reloadTime--;
    else if (input & INPUT_B) Player_Fire();

    if (player.angularVelocity < turnDelta) player.angularVelocity++;
    else if (player.angularVelocity > turnDelta) player.angularVelocity--;

    player.angle = (u8)(player.angle + (player.angularVelocity >> 1));

    if (input & INPUT_UP) moveDelta++;
    if (input & INPUT_DOWN) moveDelta--;

    tiltTimer++;
#if USE_ROTATE_BOB
    if (moveDelta) targetTilt = (s8)(FixedSin((u8)(tiltTimer * 10)) / 32);
    else targetTilt = 0;
#else
    targetTilt = 0;
#endif

    targetTilt = (s8)(targetTilt + player.angularVelocity * ROTATE_TILT);
    targetTilt = (s8)(targetTilt + strafeDelta * STRAFE_TILT);
    targetBob = (s8)((moveDelta || strafeDelta) ? FixedSin((u8)(tiltTimer * 10)) / 128 : 0);

    if (player.shakeTime > 0) {
        player.shakeTime--;
        targetBob = (s8)(targetBob + (s8)((Random() & 3) - 1));
        targetTilt = (s8)(targetTilt + (s8)((Random() & 31) - 16));
    }

    if (camera.tilt < targetTilt) {
        camera.tilt = (s8)(camera.tilt + 6);
        if (camera.tilt > targetTilt) camera.tilt = targetTilt;
    } else if (camera.tilt > targetTilt) {
        camera.tilt = (s8)(camera.tilt - 6);
        if (camera.tilt < targetTilt) camera.tilt = targetTilt;
    }

    if (camera.bob < targetBob) {
        camera.bob = (s8)(camera.bob + 3);
        if (camera.bob > targetBob) camera.bob = targetBob;
    } else if (camera.bob > targetBob) {
        camera.bob = (s8)(camera.bob - 3);
        if (camera.bob < targetBob) camera.bob = targetBob;
    }

    cosAngle = FixedCos(player.angle);
    sinAngle = FixedSin(player.angle);
    cos90Angle = FixedCos(player.angle + FIXED_ANGLE_90);
    sin90Angle = FixedSin(player.angle + FIXED_ANGLE_90);

    player.velocityX = (s16)(player.velocityX + (moveDelta * cosAngle) / 24);
    player.velocityY = (s16)(player.velocityY + (moveDelta * sinAngle) / 24);

    player.velocityX = (s16)(player.velocityX + (strafeDelta * cos90Angle) / 24);
    player.velocityY = (s16)(player.velocityY + (strafeDelta * sin90Angle) / 24);

    Player_Move((s16)(player.velocityX / 4), (s16)(player.velocityY / 4));

    player.velocityX = (s16)((player.velocityX * 7) / 8);
    player.velocityY = (s16)((player.velocityY * 7) / 8);

    if (player.mana < PLAYER_MAX_MANA && player.reloadTime == 0)
        player.mana += PLAYER_MANA_RECHARGE;

    if (player.damageTime > 0) player.damageTime--;

    cellX = (u8)(player.x / CELL_SIZE);
    cellY = (u8)(player.y / CELL_SIZE);

    switch (Map_GetCellSafe(cellX, cellY)) {
    case CellType_Potion:
        if (player.hp < PLAYER_MAX_HP) {
            if (player.hp + PLAYER_POTION_STRENGTH > PLAYER_MAX_HP) player.hp = PLAYER_MAX_HP;
            else player.hp += PLAYER_POTION_STRENGTH;
            Map_SetCell(cellX, cellY, CellType_Empty);
            Platform_PlaySound(Sound_Pickup);
            Game_ShowMessage("Drank a potion of healing");
        }
        break;
    case CellType_Coins:
        Map_SetCell(cellX, cellY, CellType_Empty);
        Platform_PlaySound(Sound_Pickup);
        Game_ShowMessage("Found some gold coins");
        stats.coinsCollected++;
        break;
    case CellType_Crown:
        Map_SetCell(cellX, cellY, CellType_Empty);
        Platform_PlaySound(Sound_Pickup);
        Game_ShowMessage("Found a jewelled crown");
        stats.crownsCollected++;
        break;
    case CellType_Scroll:
        Map_SetCell(cellX, cellY, CellType_Empty);
        Platform_PlaySound(Sound_Pickup);
        Game_ShowMessage("Found an ancient scroll");
        stats.scrollsCollected++;
        break;
    default:
        break;
    }
}

u8 Player_IsWorldColliding(void)
{
    return (u8)(
        Map_IsBlockedAtWorldPosition(player.x - PLAYER_COLLISION_SIZE, player.y - PLAYER_COLLISION_SIZE)
     || Map_IsBlockedAtWorldPosition(player.x + PLAYER_COLLISION_SIZE, player.y - PLAYER_COLLISION_SIZE)
     || Map_IsBlockedAtWorldPosition(player.x + PLAYER_COLLISION_SIZE, player.y + PLAYER_COLLISION_SIZE)
     || Map_IsBlockedAtWorldPosition(player.x - PLAYER_COLLISION_SIZE, player.y + PLAYER_COLLISION_SIZE));
}

u8 Player_CheckCollisions(void)
{
    s16 lookAheadX = (s16)(player.x + (FixedCos(player.angle) * PLAYER_LOOKAHEAD) / FIXED_ONE);
    s16 lookAheadY = (s16)(player.y + (FixedSin(player.angle) * PLAYER_LOOKAHEAD) / FIXED_ONE);
    u8 lookAheadCellX = (u8)(lookAheadX / CELL_SIZE);
    u8 lookAheadCellY = (u8)(lookAheadY / CELL_SIZE);

    CellType lookAheadCell = Map_GetCellSafe(lookAheadCellX, lookAheadCellY);
    switch (lookAheadCell) {
    case CellType_Chest:
        Map_SetCell(lookAheadCellX, lookAheadCellY, CellType_ChestOpened);
        ParticleSystemManager_CreateExplosion(lookAheadX, lookAheadY, 1);
        Platform_PlaySound(Sound_Pickup);
        Game_ShowMessage("Found a chest of treasure!");
        stats.chestsOpened++;
        break;
    case CellType_Sign:
        Game_ShowMessage(SignMessage1);
        break;
    default:
        break;
    }

    if (Player_IsWorldColliding()) return 1;
    if (EnemyManager_GetOverlappingEntity((Entity *)&player)) return 1;

    return 0;
}

void Player_Move(s16 deltaX, s16 deltaY)
{
    player.x = (s16)(player.x + deltaX);
    player.y = (s16)(player.y + deltaY);

    if (Player_CheckCollisions()) {
        player.y = (s16)(player.y - deltaY);
        if (Player_CheckCollisions()) {
            player.x = (s16)(player.x - deltaX);
            player.y = (s16)(player.y + deltaY);

            if (Player_CheckCollisions()) player.y = (s16)(player.y - deltaY);
        }
    }
}

void Player_Damage(u8 damageAmount)
{
    if (player.shakeTime < 6) player.shakeTime = 6;

    player.damageTime = 8;

    if (player.hp <= damageAmount) {
        Platform_PlaySound(Sound_PlayerDeath);
        player.hp = 0;
    } else {
        Platform_PlaySound(Sound_Ouch);
        player.hp -= damageAmount;
    }
}
