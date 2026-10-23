#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "knight.h"
#include "menu.h"
#include "player.h"
#include "wanderer.h"
#include "world.h"

#include <math.h>

#if defined(__EMSCRIPTEN__)
    #include <emscripten/emscripten.h>
    #include <emscripten/html5.h>
#endif

#define LEVEL_PATH "levels/GroundWithHills.glb"
#define SKY_PATH   "levels/sky.jpg"
#define KNIGHT_PATH "characters/Armor_Halberd.obj"
#define MENU_PATH   "menu/opening.glb"
#define BOND_PATH   "characters/Bond_Tuxedo.glb"

// Bond stands at the start of the dock, off to one side of the walkway, looking back toward the field
static const Vector3 BOND_SPOT = { 245.8f, 0.0f, -252.9f };
#define BOND_YAW    -1.369f
#define BOND_HEIGHT 1.6f
#define BOND_RADIUS 0.3f
#define HOODED_FIGURE_MATERIAL "PoeCollector"   // The purple hooded figure in the map, found by its Blender material

// South end of the middle aisle, facing north toward the dead tree, the graves and the fenced entrance
static const Vector3 SPAWN_POINT = { 0.0f, 0.0f, 24.0f };
#define SPAWN_YAW PI

// The hooded figure strolls around the graveyard inside the fence (x, z, width, depth). The fence is at z = 3 and
// every destination is on this side of it, so walking straight to one never leads out through the gap.
static const Rectangle GRAVEYARD_AREA = { -13.0f, 4.0f, 26.0f, 22.0f };
static const Vector3 HOODED_FIGURE_START = { 1.5f, 0.0f, 20.2f };     // Beside the dead tree, on the side facing the spawn

#define KNIGHT_RADIUS        0.35f
#define HOODED_FIGURE_RADIUS 0.6f   // Its robe; the outstretched arms are left out

#define MOUSE_SENSITIVITY 0.0035f
#define CAMERA_HEIGHT     1.2f      // Orbit point above the feet, around the head
#define CAMERA_MIN_PITCH  -0.6f     // Low enough to look up at the sky over the cliffs
#define CAMERA_MAX_PITCH  1.35f
#define CAMERA_MIN_DIST   1.5f
#define CAMERA_MAX_DIST   12.0f

// Third person camera orbiting the player, steered with the mouse
typedef struct OrbitCamera {
    float yaw;          // Camera sits at (sin(yaw), 0, cos(yaw)) from the player
    float pitch;
    float distance;     // Wanted distance (mouse wheel)
    float current;      // Actual distance after pulling in for walls
} OrbitCamera;

typedef struct Game {
    bool loaded;
    bool quit;
    World world;
    Knight knight;
    Menu menu;
    Model bond;
    float bondScale;
    bool hasBond;
    Player bondBody;        // Only for his spot on the ground, his shadow and bumping into him
    bool inMenu;            // Title screen first, the graveyard after START
    Player player;
    Wanderer hoodedFigure;
    OrbitCamera orbit;
    Camera3D camera;
    bool ignoreMouseDelta;  // The first delta after capturing the cursor can be a big jump
} Game;

// Global so the browser's frame callback can reach it
static Game game = { 0 };

#if defined(__EMSCRIPTEN__)
// While the pointer is locked raylib only keeps the last mouse move of each frame, so add them all up here
static Vector2 webMouseMotion = { 0 };

static EM_BOOL OnWebMouseMove(int eventType, const EmscriptenMouseEvent *event, void *userData)
{
    (void)eventType;
    (void)userData;
    webMouseMotion.x += (float)event->movementX;
    webMouseMotion.y += (float)event->movementY;
    return EM_FALSE;    // Let raylib see the event too
}
#endif

static Vector2 GetMouseLook(void)
{
#if defined(__EMSCRIPTEN__)
    Vector2 motion = webMouseMotion;
    webMouseMotion = (Vector2){ 0 };
    return motion;
#else
    return GetMouseDelta();
#endif
}

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
    if (hit.hit) allowed = fmaxf(hit.distance - 0.3f, 0.3f);

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
#if defined(__EMSCRIPTEN__)
        "Esc - free mouse",
#else
        "Esc - free mouse / quit",
#endif
    };
    const int count = sizeof(lines)/sizeof(lines[0]);

    DrawRectangle(10, 10, 230, 16 + count*20, Fade(BLACK, 0.45f));
    for (int i = 0; i < count; i++) DrawText(lines[i], 20, 18 + i*20, 18, (Color){ 210, 205, 190, 255 });
    DrawFPS(GetScreenWidth() - 90, 10);

    if (!IsCursorHidden())
    {
#if defined(__EMSCRIPTEN__)
        const char *text = "Click to play";
#else
        const char *text = "Click to look around  -  Esc again to quit";
#endif
        int width = MeasureText(text, 24);
        DrawRectangle(0, GetScreenHeight()/2 - 24, GetScreenWidth(), 48, Fade(BLACK, 0.45f));
        DrawText(text, (GetScreenWidth() - width)/2, GetScreenHeight()/2 - 12, 24, RAYWHITE);
    }
}

static void DrawBond(void)
{
    const Player *body = &game.bondBody;
    float s = game.bondScale;
    Matrix transform = MatrixMultiply(MatrixScale(s, s, s), MatrixRotateY(body->yaw));
    transform = MatrixMultiply(transform, MatrixTranslate(body->position.x, body->position.y, body->position.z));
    rlDisableBackfaceCulling();
    for (int m = 0; m < game.bond.meshCount; m++) DrawMesh(game.bond.meshes[m], game.bond.materials[game.bond.meshMaterial[m]], transform);
    rlEnableBackfaceCulling();
    PlayerDrawShadow(body, 0.35f);
}

// Keeps two characters from walking through each other by pushing the first one out
static void PushApart(Player *mover, float moverRadius, const Player *other, float otherRadius)
{
    Vector2 offset = { mover->position.x - other->position.x, mover->position.z - other->position.z };
    float distance = Vector2Length(offset);
    float overlap = moverRadius + otherRadius - distance;
    if ((overlap <= 0.0f) || (distance < 0.0001f)) return;

    mover->position.x += offset.x/distance*overlap;
    mover->position.z += offset.y/distance*overlap;
}

// The hooded figure walks on its two thin legs, swinging its arms against them
static void DrawHoodedFigure(const Wanderer *figure, const World *world)
{
    if (!world->hasActor) return;

    const Player *body = &figure->body;
    float swing = body->stride;
    float legSwing = sinf(body->walkPhase)*swing*1.3f;
    float bob = fabsf(sinf(legSwing))*0.2f;      // Its long feet tip down as a leg swings back, so lift it to keep the toes up
    float lean = swing*0.12f;
    float roll = sinf(body->walkPhase)*0.06f*swing;
    // The staff hand (right) swings less so the staff doesn't flail
    float limbs[ACTOR_LIMBS] = { legSwing, -legSwing, -legSwing*0.6f, legSwing*0.35f };
    WorldDrawActor(world, (Vector3){ body->position.x, body->position.y + bob, body->position.z }, body->yaw, lean, roll, limbs);
    PlayerDrawShadow(body, 0.8f);
}

static void UpdateDrawFrame(void)
{
    if (!game.loaded)
    {
        if (IsKeyPressed(KEY_ESCAPE)) game.quit = true;
        BeginDrawing();
            ClearBackground(BLACK);
            DrawText("Could not load " LEVEL_PATH " or " KNIGHT_PATH, 20, 20, 20, RED);
            DrawText("Run the game from the repository folder, or keep the levels and characters folders next to the executable.", 20, 50, 20, RAYWHITE);
        EndDrawing();
        return;
    }

    if (game.inMenu)
    {
#if !defined(__EMSCRIPTEN__)
        if (IsKeyPressed(KEY_ESCAPE)) game.quit = true;
#endif
        if (MenuUpdateDraw(&game.menu))
        {
            game.inMenu = false;
            DisableCursor();
            game.ignoreMouseDelta = true;
        }
        return;
    }

    float dt = fminf(GetFrameTime(), 0.05f);
    OrbitCamera *orbit = &game.orbit;

    if (IsKeyPressed(KEY_ESCAPE))
    {
        if (IsCursorHidden()) EnableCursor();
#if !defined(__EMSCRIPTEN__)
        else game.quit = true;
#endif
    }
    if (!IsCursorHidden() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        DisableCursor();
        game.ignoreMouseDelta = true;
    }

    Vector2 mouse = GetMouseLook();
    if (IsCursorHidden())
    {
        if (game.ignoreMouseDelta) mouse = (Vector2){ 0 };
        game.ignoreMouseDelta = false;

        orbit->yaw -= mouse.x*MOUSE_SENSITIVITY;
        orbit->pitch = Clamp(orbit->pitch + mouse.y*MOUSE_SENSITIVITY, CAMERA_MIN_PITCH, CAMERA_MAX_PITCH);
    }
    orbit->distance = Clamp(orbit->distance - GetMouseWheelMove()*0.5f, CAMERA_MIN_DIST, CAMERA_MAX_DIST);

    // WASD is relative to where the camera is looking
    Vector3 forward = { -sinf(orbit->yaw), 0.0f, -cosf(orbit->yaw) };
    Vector3 right = { cosf(orbit->yaw), 0.0f, -sinf(orbit->yaw) };
    Vector3 move = { 0 };
    if (IsKeyDown(KEY_W)) move = Vector3Add(move, forward);
    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, forward);
    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
    if (Vector3Length(move) > 1.0f) move = Vector3Normalize(move);

    if (IsKeyPressed(KEY_R)) PlayerRespawn(&game.player, &game.world);
    bool sprint = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    PlayerUpdate(&game.player, &game.world, move, sprint, IsKeyPressed(KEY_SPACE), dt);
    if (game.world.hasActor)
    {
        WandererUpdate(&game.hoodedFigure, &game.world, dt);
        PushApart(&game.player, KNIGHT_RADIUS, &game.hoodedFigure.body, HOODED_FIGURE_RADIUS);
    }
    if (game.hasBond) PushApart(&game.player, KNIGHT_RADIUS, &game.bondBody, BOND_RADIUS);
    UpdateOrbitCamera(&game.camera, orbit, &game.world, game.player.position, dt);

    BeginDrawing();
        ClearBackground(WORLD_SKY_COLOR);
        BeginMode3D(game.camera);
            WorldDraw(&game.world, game.camera);
            KnightDraw(&game.knight, &game.player);
            DrawHoodedFigure(&game.hoodedFigure, &game.world);
            if (game.hasBond) DrawBond();
        EndMode3D();
        DrawHud();
    EndDrawing();
}

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "Dark Fantasy");
    SetExitKey(KEY_NULL);   // Esc frees the mouse first, see UpdateDrawFrame()

    // Works whether we're started from the repo root or from next to the executable
    if (!FileExists(LEVEL_PATH)) ChangeDirectory(GetApplicationDirectory());

    BeginDrawing();
    ClearBackground(WORLD_SKY_COLOR);
    DrawText("Loading...", 20, GetScreenHeight() - 40, 20, RAYWHITE);
    EndDrawing();

    game.loaded = WorldLoad(&game.world, LEVEL_PATH, SKY_PATH, HOODED_FIGURE_MATERIAL);
    if (game.loaded && !KnightLoad(&game.knight, KNIGHT_PATH, game.world.shader))
    {
        WorldUnload(&game.world);
        game.loaded = false;
    }
    if (game.loaded)
    {
        PlayerInit(&game.player, &game.world, SPAWN_POINT, SPAWN_YAW);
        WandererInit(&game.hoodedFigure, &game.world, HOODED_FIGURE_START, GRAVEYARD_AREA);

        game.orbit = (OrbitCamera){ SPAWN_YAW + PI, 0.2f, 4.5f, 4.5f };
        game.camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        game.camera.fovy = 60.0f;
        game.camera.projection = CAMERA_PERSPECTIVE;
        rlSetClipPlanes(0.05, 2000.0);

        game.hasBond = FileExists(BOND_PATH);
        if (game.hasBond)
        {
            game.bond = LoadModel(BOND_PATH);
            for (int i = 0; i < game.bond.materialCount; i++) game.bond.materials[i].shader = game.world.shader;
            BoundingBox box = GetModelBoundingBox(game.bond);
            game.bondScale = BOND_HEIGHT/fmaxf(box.max.y - box.min.y, 0.001f);
            PlayerInit(&game.bondBody, &game.world, BOND_SPOT, BOND_YAW);
        }

        game.inMenu = MenuLoad(&game.menu, MENU_PATH);     // Straight into the game if the menu is missing
    }

#if defined(__EMSCRIPTEN__)
    // Browsers only allow grabbing the mouse after a click, which the "Click to play" prompt asks for
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, NULL, 1, OnWebMouseMove);
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    if (game.loaded && !game.inMenu) DisableCursor();
    game.ignoreMouseDelta = true;
    SetTargetFPS(144);

    while (!WindowShouldClose() && !game.quit) UpdateDrawFrame();

    if (game.loaded)
    {
        if (game.inMenu) MenuUnload(&game.menu);
        if (game.hasBond) UnloadModel(game.bond);
        KnightUnload(&game.knight);
        WorldUnload(&game.world);
    }
    CloseWindow();
#endif

    return 0;
}
