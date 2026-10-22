#ifndef WORLD_H
#define WORLD_H

#include "raylib.h"
#include <stdbool.h>

// Night sky; the fog fades into it so the far end of the map dissolves into darkness
#define WORLD_SKY_COLOR (Color){ 24, 26, 36, 255 }

// The level loaded from a Blender .glb export, plus what we need to collide with it.
typedef struct World {
    Model model;
    Shader shader;          // Moonlit + fog shader shared by the level and the player
    int locViewPos;

    // Collision: world-space triangles (raylib bakes the Blender node transforms into the meshes)
    // bucketed into a grid over the ground plane. Foliage is left out so leaves never block anything.
    Vector3 *triangles;     // Three corners per triangle
    int triangleCount;
    int *cellStart;         // Triangles of cell c are cellItems[cellStart[c]] .. cellItems[cellStart[c + 1] - 1]
    int *cellItems;
    int gridWidth, gridDepth;
    float gridMinX, gridMinZ, cellSize;
} World;

bool WorldLoad(World *world, const char *path);
void WorldUnload(World *world);

// Call once per frame inside BeginMode3D() before drawing anything with the world shader.
void WorldBeginFrame(const World *world, Camera3D camera);
void WorldDraw(const World *world);

// Closest solid hit along the ray, up to maxDistance.
RayCollision WorldRaycast(const World *world, Ray ray, float maxDistance);

// Height of the first solid surface straight below (x, fromY, z). False if there is nothing below.
bool WorldGroundHeight(const World *world, float x, float z, float fromY, float *outY);

#endif
