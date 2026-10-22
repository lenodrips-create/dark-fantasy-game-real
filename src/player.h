#ifndef PLAYER_H
#define PLAYER_H

#include "raylib.h"
#include "world.h"
#include <stdbool.h>

// Standing height of the characters, about half the tallest gravestones (2.5 units)
#define PLAYER_HEIGHT 1.4f

// Movement, collision and walk cycle shared by every character; how they look is drawn on top of this
typedef struct Player {
    Vector3 spawn;
    float spawnYaw;
    Vector3 position;   // Feet
    float yaw;          // Facing direction: forward = (sin(yaw), 0, cos(yaw))
    float velocityY;
    float groundY;      // Surface under the feet, for the shadow
    bool grounded;
    bool sprinting;
    float walkPhase;    // Advances with distance travelled, drives the walk cycle
    float stride;       // Smoothed swing amplitude: 0 standing still, larger when sprinting
} Player;

// GPU resources for drawing the stickman: limbs are stretched cylinders, joints and head are spheres
typedef struct Stickman {
    Mesh limb;
    Mesh joint;
    Material material;
} Stickman;

void PlayerInit(Player *player, const World *world, Vector3 spawn, float yaw);
void PlayerRespawn(Player *player, const World *world);

// move is a world-space direction on the XZ plane with length 0..1
void PlayerUpdate(Player *player, const World *world, Vector3 move, bool sprint, bool jump, float dt);

Stickman StickmanLoad(Shader shader);
void StickmanUnload(Stickman *stickman);
void StickmanDraw(const Player *player, const Stickman *stickman);

// Blob shadow under a character, radius in world units
void PlayerDrawShadow(const Player *player, float radius);

#endif
