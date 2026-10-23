#include "menu.h"

#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TITLE "MOONSHADE"
#define WATERFALL_SPEED 0.35f   // Texture lengths per second; the texture runs up the waterfall, so this pours it down
#define ZOOM_TIME       2.5f    // Seconds for the camera to ease in on the chest

static const Vector3 CHEST_TOP = { 0.0f, 0.75f, 0.0f };
static const Vector3 CAMERA_FAR = { 0.0f, 2.2f, 6.5f };
static const Vector3 CAMERA_NEAR = { 0.0f, 0.85f, 1.7f };
static const Vector3 LOOK_FAR = { 0.0f, 1.0f, -1.0f };
static const Vector3 LOOK_NEAR = { 0.0f, 0.42f, 0.0f };
static const Color TITLE_COLOR = { 225, 228, 255, 255 };
static const Color BUTTON_COLOR = { 40, 30, 70, 220 };
static const Color BUTTON_HOVER = { 90, 70, 150, 235 };

bool MenuLoad(Menu *menu, const char *path)
{
    memset(menu, 0, sizeof(*menu));
    if (!FileExists(path)) return false;

    menu->scene = LoadModel(path);
    if (menu->scene.meshCount == 0) return false;

    // The waterfall is the tallest piece of the scene
    float tallest = 0.0f;
    menu->waterfall = -1;
    for (int m = 0; m < menu->scene.meshCount; m++)
    {
        BoundingBox box = GetMeshBoundingBox(menu->scene.meshes[m]);
        float height = box.max.y - box.min.y;
        if ((height > tallest) && (menu->scene.meshes[m].texcoords != NULL)) { tallest = height; menu->waterfall = m; }
    }
    if (menu->waterfall >= 0)
    {
        const Mesh *mesh = &menu->scene.meshes[menu->waterfall];
        menu->waterfallUV = malloc(mesh->vertexCount*2*sizeof(float));
        memcpy(menu->waterfallUV, mesh->texcoords, mesh->vertexCount*2*sizeof(float));
    }

    return true;
}

void MenuUnload(Menu *menu)
{
    for (int i = 0; i < menu->scene.materialCount; i++)
    {
        Texture2D texture = menu->scene.materials[i].maps[MATERIAL_MAP_ALBEDO].texture;
        if ((texture.id > 0) && (texture.id != rlGetTextureIdDefault())) UnloadTexture(texture);
    }
    UnloadModel(menu->scene);
    free(menu->waterfallUV);
    memset(menu, 0, sizeof(*menu));
}

static void DrawTextOutlined(const char *text, int x, int y, int size, Color color, float alpha)
{
    int outline = (size/24 > 2)? size/24 : 2;
    for (int dx = -outline; dx <= outline; dx += outline)
    {
        for (int dy = -outline; dy <= outline; dy += outline) DrawText(text, x + dx, y + dy, size, Fade(BLACK, alpha));
    }
    DrawText(text, x, y, size, Fade(color, alpha));
}

bool MenuUpdateDraw(Menu *menu)
{
    menu->time += GetFrameTime();
    float t = menu->time;

    // Waterfall: slide its texture down
    if (menu->waterfall >= 0)
    {
        Mesh *mesh = &menu->scene.meshes[menu->waterfall];
        float shift = fmodf(t*WATERFALL_SPEED, 1.0f);
        for (int i = 0; i < mesh->vertexCount; i++)
        {
            mesh->texcoords[i*2] = menu->waterfallUV[i*2] + shift;
            mesh->texcoords[i*2 + 1] = menu->waterfallUV[i*2 + 1];
        }
        UpdateMeshBuffer(*mesh, 1, mesh->texcoords, mesh->vertexCount*2*sizeof(float), 0);
    }

    // Camera eases in on the chest, then sways a little
    float zoom = fminf(t/ZOOM_TIME, 1.0f);
    zoom = 1.0f - (1.0f - zoom)*(1.0f - zoom)*(1.0f - zoom);
    Camera3D camera = { 0 };
    camera.position = Vector3Lerp(CAMERA_FAR, CAMERA_NEAR, zoom);
    camera.position.x += sinf(t*0.4f)*0.12f*zoom;
    camera.position.y += sinf(t*0.7f)*0.03f*zoom;
    camera.target = Vector3Lerp(LOOK_FAR, LOOK_NEAR, zoom);
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 50.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    int w = GetScreenWidth();
    int h = GetScreenHeight();

    // START button below the chest; Enter works too
    int buttonWidth = (w/4 > 220)? w/4 : 220;
    int buttonHeight = (h/11 > 50)? h/11 : 50;
    Rectangle button = { (w - buttonWidth)/2.0f, h*0.82f - buttonHeight/2.0f, (float)buttonWidth, (float)buttonHeight };
    bool ready = (zoom > 0.6f);
    bool hover = ready && CheckCollisionPointRec(GetMousePosition(), button);
    bool start = ready && ((hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER));

    BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(camera);
            rlDisableBackfaceCulling();
            DrawModel(menu->scene, (Vector3){ 0 }, 1.0f, WHITE);
            rlEnableBackfaceCulling();
        EndMode3D();

        // Title just above the chest
        int titleSize = h/7;
        int titleWidth = MeasureText(TITLE, titleSize);
        Vector2 chestTop = GetWorldToScreen(CHEST_TOP, camera);
        int titleY = (int)chestTop.y - titleSize - h/40;
        if (titleY < h/30) titleY = h/30;
        DrawTextOutlined(TITLE, (w - titleWidth)/2, titleY, titleSize, TITLE_COLOR, fminf(t/1.5f, 1.0f));

        if (ready)
        {
            DrawRectangleRounded(button, 0.3f, 8, hover? BUTTON_HOVER : BUTTON_COLOR);
            DrawRectangleRoundedLinesEx(button, 0.3f, 8, 3.0f, TITLE_COLOR);
            int size = buttonHeight*3/5;
            DrawText("START", (int)(button.x + (button.width - MeasureText("START", size))/2), (int)(button.y + (button.height - size)/2), size, TITLE_COLOR);
        }
    EndDrawing();

    return start;
}
