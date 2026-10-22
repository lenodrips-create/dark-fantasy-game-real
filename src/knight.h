#ifndef KNIGHT_H
#define KNIGHT_H

#include "raylib.h"
#include "player.h"
#include <stdbool.h>

// The armored halberd knight (characters/Armor_Halberd.obj). The model has no skeleton, so its vertices
// are split into body parts at load time and posed with joint rotations every frame.
typedef struct Knight {
    Model model;
    float *restPositions;   // Model-space pose from the file, 3 floats per vertex
    float *restNormals;
    unsigned char *part;    // Body part of each vertex
    float scale;            // Model units to world units, so the knight is PLAYER_HEIGHT tall
    float footY;            // Lowest point of the model, placed on the ground
} Knight;

bool KnightLoad(Knight *knight, const char *path, Shader shader);
void KnightUnload(Knight *knight);

// Poses the knight from the character's walk cycle and draws it
void KnightDraw(Knight *knight, const Player *player);

#endif
