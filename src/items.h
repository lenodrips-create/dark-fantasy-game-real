#ifndef ITEMS_H
#define ITEMS_H

#include "raylib.h"
#include <stdbool.h>

// What the knight can carry, in item bar order
typedef enum { ITEM_SWORD, ITEM_SHOVEL, ITEM_SLOTS = 3 } Item;

// The low-poly shovel and the item bar in the top right corner
typedef struct Items {
    Mesh handle, grip, blade;
    Material wood, metal;
    Texture2D bar;          // ui/hotbar.png: the slot frame
} Items;

void ItemsLoad(Items *items, const char *barPath, Shader shader);
void ItemsUnload(Items *items);

// Draws the shovel with its blade's bottom at the origin of transform and its handle along +Y (1.1 units long)
void ItemsDrawShovel(const Items *items, Matrix transform);

// Draws the item bar in the top right corner: the sword in the first slot, the shovel in the second once found
void ItemsDrawBar(const Items *items, Item selected, bool hasShovel);

#endif
