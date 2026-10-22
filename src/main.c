#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "player.h"
#include "world.h"

#include <math.h>

#define LEVEL_PATH "levels/GroundWithHills.glb"

// Middle aisle of the graveyard, between the rows of graves, facing north toward the cliff gap
static const Vector3 SPAWN_POINT = { 0.0f, 0.0f, 14.0f };
#define SPAWN_YAW PI

#define MOUSE_SENSITIVITY 0.0035f
#define CAMERA_HEIGHT     2.3f      // Orbit point above the feet, around the head
#define CAMERA_MIN_PITCH  -0.25f
#define CAMERA_MAX_PITCH  1.35f
#define CAMERA_MIN_DIST   2.5f
#define CAMERA_MAX_DIST   16.0f

// Third person camera orbiting the player, steered with the mouse
typedef struct OrbitCamera {
    float yaw;          // Camera sits at (sin(yaw), 0, cos(yaw)) from the player
    float pitch;
    float distance;     // Wanted distance (mouse wheel)
    float current;      // Actual distance after pulling in for walls
} OrbitCamera;

static void UpdateOrbitCamera(Camera3D *camera, OrbitCamera *orbit, const World *world, Vector3 playerPosition, float dt)
{
    Vector3 target = { playerPosition.x, playerPosition.y + CAMERA_HEIGHT, playerPosition.z };
    Vector3 dir = {
        sinf(orbit->yaw)*cosf(orbit->pitch),
        sinf(orbit->pitch),
        cosf(orbit->yaw)*cosf(orbit->pitch)
    };

    // Pull in right away when a cliff or house gets between us and the player, ease back out after
    float allowed = orbit->distance;
    RayCollision hit = WorldRaycast(world, (Ray){ target, dir }, orbit->distance + 0.3f);
    if (hit.hit) allowed = fmaxf(hit.distance - 0.3f, 0.5f);

    if (allowed < orbit->current) orbit->current = allowed;
    else orbit->current += (allowed - orbit->current)*fminf(1.0f, 6.0f*dt);

    camera->target = target;
    camera->position = Vector3Add(target, Vector3Scale(dir, orbit->current));
}

static void DrawHud(void)
{
    static const char *lines[] = {
        "WASD - move",
        "Mouse - look around",
        "Shift - sprint",
        "Space - jump",
        "Mouse wheel - zoom",
        "R - back to spawn",
        "Esc - free mouse / quit",
    };
    const int count = sizeof(lines)/sizeof(lines[0]);

    DrawRectangle(10, 10, 230, 16 + count*20, Fade(BLACK, 0.45f));
    for (int i = 0; i < count; i++) DrawText(lines[i], 20, 18 + i*20, 18, (Color){ 210, 205, 190, 255 });
    DrawFPS(GetScreenWidth() - 90, 10);

    if (!IsCursorHidden())
    {
        const char *text = "Click to look around  -  Esc again to quit";
        int width = MeasureText(text, 24);
        DrawText(text, (GetScreenWidth() - width)/2, GetScreenHeight()/2 - 12, 24, RAYWHITE);
    }
}

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "Dark Fantasy");
    SetExitKey(KEY_NULL);   // Esc frees the mouse first, see below
    SetTargetFPS(144);

    // Works whether we're started from the repo root or from next to the executable
    if (!FileExists(LEVEL_PATH)) ChangeDirectory(GetApplicationDirectory());

    BeginDrawing();
    ClearBackground(WORLD_SKY_COLOR);
    DrawText("Loading...", 20, GetScreenHeight() - 40, 20, RAYWHITE);
    EndDrawing();

    World world;
    if (!WorldLoad(&world, LEVEL_PATH))
    {
        while (!WindowShouldClose() && !IsKeyPressed(KEY_ESCAPE))
        {
            BeginDrawing();
            ClearBackground(BLACK);
            DrawText("Could not load " LEVEL_PATH, 20, 20, 20, RED);
            DrawText("Run the game from the repository folder, or keep the levels folder next to the executable.", 20, 50, 20, RAYWHITE);
            EndDrawing();
        }
        CloseWindow();
        return 1;
    }

    Stickman stickman = StickmanLoad(world.shader);
    Player player;
    PlayerInit(&player, &world, SPAWN_POINT, SPAWN_YAW);

    OrbitCamera orbit = { SPAWN_YAW + PI, 0.3f, 7.0f, 7.0f };
    Camera3D camera = { 0 };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    rlSetClipPlanes(0.05, 2000.0);

    DisableCursor();
    bool ignoreMouseDelta = true;   // The first delta after capturing the cursor can be a big jump

    while (!WindowShouldClose())
    {
        float dt = fminf(GetFrameTime(), 0.05f);

        if (IsKeyPressed(KEY_ESCAPE))
        {
            if (IsCursorHidden()) EnableCursor();
            else break;
        }
        if (!IsCursorHidden() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            DisableCursor();
            ignoreMouseDelta = true;
        }

        if (IsCursorHidden())
        {
            Vector2 mouse = GetMouseDelta();
            if (ignoreMouseDelta) mouse = (Vector2){ 0 };
            ignoreMouseDelta = false;

            orbit.yaw -= mouse.x*MOUSE_SENSITIVITY;
            orbit.pitch = Clamp(orbit.pitch + mouse.y*MOUSE_SENSITIVITY, CAMERA_MIN_PITCH, CAMERA_MAX_PITCH);
        }
        orbit.distance = Clamp(orbit.distance - GetMouseWheelMove()*0.8f, CAMERA_MIN_DIST, CAMERA_MAX_DIST);

        // WASD is relative to where the camera is looking
        Vector3 forward = { -sinf(orbit.yaw), 0.0f, -cosf(orbit.yaw) };
        Vector3 right = { cosf(orbit.yaw), 0.0f, -sinf(orbit.yaw) };
        Vector3 move = { 0 };
        if (IsKeyDown(KEY_W)) move = Vector3Add(move, forward);
        if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, forward);
        if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
        if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
        if (Vector3Length(move) > 1.0f) move = Vector3Normalize(move);

        if (IsKeyPressed(KEY_R)) PlayerRespawn(&player, &world);
        bool sprint = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        PlayerUpdate(&player, &world, move, sprint, IsKeyPressed(KEY_SPACE), dt);
        UpdateOrbitCamera(&camera, &orbit, &world, player.position, dt);

        BeginDrawing();
            ClearBackground(WORLD_SKY_COLOR);
            BeginMode3D(camera);
                WorldBeginFrame(&world, camera);
                WorldDraw(&world);
                PlayerDraw(&player, &stickman);
            EndMode3D();
            DrawHud();
        EndDrawing();
    }

    StickmanUnload(&stickman);
    WorldUnload(&world);
    CloseWindow();
    return 0;
}
