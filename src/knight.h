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
    bool halberdPutAway;    // Something else is in his hand (the halberd is always put away while climbing)
    Matrix rightHand;       // Set by KnightDraw(): world transform of the right hand, see KnightHandTransform()
} Knight;

bool KnightLoad(Knight *knight, const char *path, Shader shader);
void KnightUnload(Knight *knight);

// Poses the knight from the character's walk cycle and draws it
void KnightDraw(Knight *knight, const Player *player);

// Where to draw something held in his right hand, the way he holds the halberd: its local +Y runs back along his arm
// through the hand and local Y = gripY sits in the hand. Valid after KnightDraw().
Matrix KnightHandTransform(const Knight *knight, float gripY);

#endif
