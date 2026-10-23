#ifndef MENU_H
#define MENU_H

#include "raylib.h"
#include <stdbool.h>

// Title screen: the opening scene (menu/opening.glb) with a falling waterfall, the camera easing in on the
// treasure chest, the game's name above it and a START button
typedef struct Menu {
    Model scene;
    int waterfall;          // Mesh whose texture scrolls down
    float *waterfallUV;     // Its texture coordinates as loaded, 2 floats per vertex
    float time;
} Menu;

bool MenuLoad(Menu *menu, const char *path);
void MenuUnload(Menu *menu);

// Runs one frame of the menu; returns true when the player starts the game
bool MenuUpdateDraw(Menu *menu);

#endif
