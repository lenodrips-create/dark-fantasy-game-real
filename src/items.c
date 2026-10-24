#include "items.h"

#include "raymath.h"
#include "rlgl.h"

#define SHOVEL_LENGTH 1.1f      // Blade tip to the top of the grip, in world units
#define BAR_SCALE     1.6f      // Item bar size at 720 pixels high

// Slots in ui/hotbar.png (pixels): the transparent holes the items show through
static const Rectangle SLOTS[ITEM_SLOTS] = { { 46, 13, 49, 54 }, { 124, 21, 47, 47 }, { 197, 21, 50, 47 } };

void ItemsLoad(Items *items, const char *barPath, Shader shader)
{
    // Handle, a crossbar grip at the top and a flat blade at the bottom; centred on X and Z, blade tip at Y = 0
    items->handle = GenMeshCylinder(0.025f, 0.78f, 6);
    items->grip = GenMeshCube(0.16f, 0.04f, 0.04f);
    items->blade = GenMeshCube(0.22f, 0.3f, 0.025f);
    items->wood = LoadMaterialDefault();
    items->wood.shader = shader;
    items->wood.maps[MATERIAL_MAP_ALBEDO].color = (Color){ 120, 82, 46, 255 };
    items->metal = LoadMaterialDefault();
    items->metal.shader = shader;
    items->metal.maps[MATERIAL_MAP_ALBEDO].color = (Color){ 120, 122, 128, 255 };
    items->bar = LoadTexture(barPath);
    SetTextureFilter(items->bar, TEXTURE_FILTER_BILINEAR);
}

void ItemsUnload(Items *items)
{
    UnloadMesh(items->handle);
    UnloadMesh(items->grip);
    UnloadMesh(items->blade);
    items->wood.shader.id = rlGetShaderIdDefault();     // The shader belongs to the world
    items->metal.shader.id = rlGetShaderIdDefault();
    UnloadMaterial(items->wood);
    UnloadMaterial(items->metal);
    if (items->bar.id > 0) UnloadTexture(items->bar);
}

void ItemsDrawShovel(const Items *items, Matrix transform)
{
    rlDisableBackfaceCulling();
    DrawMesh(items->blade, items->metal, MatrixMultiply(MatrixTranslate(0.0f, 0.15f, 0.0f), transform));
    DrawMesh(items->handle, items->wood, MatrixMultiply(MatrixTranslate(0.0f, 0.28f, 0.0f), transform));
    DrawMesh(items->grip, items->wood, MatrixMultiply(MatrixTranslate(0.0f, SHOVEL_LENGTH - 0.02f, 0.0f), transform));
    rlEnableBackfaceCulling();
}

// Little flat icons, drawn into a slot's rectangle
static void DrawSwordIcon(Rectangle r)
{
    Vector2 tip = { r.x + r.width*0.82f, r.y + r.height*0.16f };
    Vector2 guard = { r.x + r.width*0.36f, r.y + r.height*0.62f };
    Vector2 pommel = { r.x + r.width*0.18f, r.y + r.height*0.82f };
    float w = r.width/9.0f;
    DrawLineEx(guard, tip, w*1.6f + 2, BLACK);
    DrawLineEx(guard, tip, w*1.6f, (Color){ 205, 210, 220, 255 });
    DrawLineEx((Vector2){ guard.x - w*2.2f, guard.y - w*2.2f }, (Vector2){ guard.x + w*2.2f, guard.y + w*2.2f }, w*1.3f, (Color){ 200, 160, 60, 255 });
    DrawLineEx(guard, pommel, w*1.2f, (Color){ 110, 70, 40, 255 });
    DrawCircleV(pommel, w, (Color){ 200, 160, 60, 255 });
}

static void DrawShovelIcon(Rectangle r)
{
    Vector2 top = { r.x + r.width*0.82f, r.y + r.height*0.16f };
    Vector2 neck = { r.x + r.width*0.38f, r.y + r.height*0.6f };
    float w = r.width/9.0f;
    DrawLineEx(top, neck, w*1.1f, (Color){ 140, 95, 50, 255 });
    DrawLineEx((Vector2){ top.x - w*1.4f, top.y - w*1.4f }, (Vector2){ top.x + w*1.4f, top.y + w*1.4f }, w*1.1f, (Color){ 140, 95, 50, 255 });
    // The blade: a rounded spade below the neck
    Vector2 c = { r.x + r.width*0.27f, r.y + r.height*0.72f };
    DrawCircleV(c, w*2.3f, (Color){ 150, 155, 165, 255 });
    DrawTriangle((Vector2){ neck.x + w*0.4f, neck.y - w*1.6f }, (Vector2){ c.x - w*2.2f, c.y - w*0.6f }, (Vector2){ c.x + w*1.6f, c.y + w*1.5f }, (Color){ 150, 155, 165, 255 });
}

void ItemsDrawBar(const Items *items, Item selected, bool hasShovel)
{
    if (items->bar.id == 0) return;
    float scale = BAR_SCALE*GetScreenHeight()/720.0f;
    Vector2 origin = { GetScreenWidth() - items->bar.width*scale, 0.0f };

    for (int i = 0; i < ITEM_SLOTS; i++)
    {
        Rectangle slot = { origin.x + SLOTS[i].x*scale, origin.y + SLOTS[i].y*scale, SLOTS[i].width*scale, SLOTS[i].height*scale };
        // Dark glass behind each slot, lighter for the one in hand
        DrawRectangleRec(slot, (i == (int)selected)? Fade((Color){ 230, 225, 200, 255 }, 0.35f) : Fade(BLACK, 0.45f));
        Rectangle icon = { slot.x + slot.width*0.1f, slot.y + slot.height*0.1f, slot.width*0.8f, slot.height*0.8f };
        if (i == ITEM_SWORD) DrawSwordIcon(icon);
        if ((i == ITEM_SHOVEL) && hasShovel) DrawShovelIcon(icon);
        char key[2] = { (char)('1' + i), 0 };
        DrawText(key, (int)(slot.x + 4), (int)(slot.y + slot.height - 16*scale/BAR_SCALE), (int)(14*scale/BAR_SCALE), (Color){ 220, 215, 200, 200 });
    }
    DrawTextureEx(items->bar, origin, 0.0f, scale, WHITE);

    // A bright ring around the slot in hand
    Rectangle s = SLOTS[selected];
    DrawRectangleRoundedLinesEx((Rectangle){ origin.x + s.x*scale - 2, origin.y + s.y*scale - 2, s.width*scale + 4, s.height*scale + 4 }, 0.35f, 6, 3.0f, (Color){ 240, 230, 190, 255 });
}
