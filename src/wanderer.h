#ifndef WANDERER_H
#define WANDERER_H

#include "raylib.h"
#include "player.h"
#include "world.h"

// A character that strolls between random spots inside an area, pausing at each one
typedef struct Wanderer {
    Player body;
    Rectangle area;     // Where it may go, on the ground plane: x = world X, y = world Z
    Vector2 target;     // Current destination (world X, Z)
    float wait;         // Seconds left standing still before heading to a new spot
    float stuck;        // Seconds spent walking without getting anywhere
} Wanderer;

void WandererInit(Wanderer *wanderer, const World *world, Vector3 start, Rectangle area);
void WandererUpdate(Wanderer *wanderer, const World *world, float dt);

#endif
