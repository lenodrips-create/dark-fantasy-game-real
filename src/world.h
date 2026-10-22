#ifndef WORLD_H
#define WORLD_H

#include "raylib.h"
#include <stdbool.h>

// Background behind the sky image, close to its darkest purple
#define WORLD_SKY_COLOR (Color){ 14, 6, 34, 255 }

// The level loaded from a Blender .glb export, plus what we need to collide with it.
typedef struct World {
    Model model;
    Shader shader;          // Moonlight shader shared by the level and the player
    Texture2D skyTexture;
    Mesh skyMesh;
    Material skyMaterial;

    // Collision: world-space triangles (raylib bakes the Blender node transforms into the meshes)
    // bucketed into a grid over the ground plane. Foliage is left out so leaves never block anything.
    Vector3 *triangles;     // Three corners per triangle
    int triangleCount;
    int *cellStart;         // Triangles of cell c are cellItems[cellStart[c]] .. cellItems[cellStart[c + 1] - 1]
    int *cellItems;
    int gridWidth, gridDepth;
    float gridMinX, gridMinZ, cellSize;
} World;

bool WorldLoad(World *world, const char *path, const char *skyPath);
void WorldUnload(World *world);

// Draws the sky and the level; call first inside BeginMode3D()
void WorldDraw(const World *world, Camera3D camera);

// Closest solid hit along the ray, up to maxDistance.
RayCollision WorldRaycast(const World *world, Ray ray, float maxDistance);

// Height of the first solid surface straight below (x, fromY, z). False if there is nothing below.
bool WorldGroundHeight(const World *world, float x, float z, float fromY, float *outY);

#endif
