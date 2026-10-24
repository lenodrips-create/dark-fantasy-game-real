#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "items.h"
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
#define CLOUD_PATH  "levels/cloud_scene.glb"
#define HOTBAR_PATH "ui/hotbar.png"

// The shovel stuck in the ground in the graveyard, a few steps from the spawn point. Walk up to it and press E.
static const Vector3 SHOVEL_SPOT = { 3.5f, 0.0f, 20.5f };
#define SHOVEL_REACH 1.4f       // How close you have to be to pick it up
#define SHOVEL_GRIP  0.75f      // Where along the shovel (from the blade's tip) the knight holds it

// Where the climb leads: the cloud with the giant's house and the ogre. You arrive near its front edge, facing the house.
static const Vector3 CLOUD_SPAWN = { 0.0f, 0.0f, 30.0f };
#define CLOUD_SPAWN_YAW PI
static const Vector3 OGRE_HEAD = { -10.2f, 5.6f, -60.0f };   // The sleeping ogre's head on the pillow, where its Zzz come from
#define SNORE_COUNT  3          // Z's in the air at once
#define SNORE_PERIOD 3.0f       // Seconds for one Z to rise and fade

// Bond stands at the start of the dock, off to one side of the walkway, looking back toward the field
static const Vector3 BOND_SPOT = { 245.8f, 0.0f, -252.9f };
#define BOND_YAW    -1.369f
#define BOND_HEIGHT 1.6f
#define BOND_RADIUS 0.3f
// The giant corn stalk in the field: walk into it to grab on and climb. A twentieth of the way up the screen turns white
// and you come out on the cloud.
static const Vector3 BEANSTALK_BASE = { -112.6f, 13.4f, -302.5f };
#define BEANSTALK_RADIUS   1.35f    // The stalk's surface, from its middle (its sides are 1.2 to 1.42 out)
#define BEANSTALK_HEIGHT   150.0f
#define BEANSTALK_GRAB     0.9f     // How close to its surface the player has to be to grab on
#define WHITEOUT_HEIGHT    (BEANSTALK_HEIGHT*0.05f)
#define WHITEOUT_TIME      2.0f     // Seconds for the screen to fade to white, and back from white on the cloud

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
    float whiteout;         // 0..1, how far the screen has faded to white at the top of the climb
    World cloud;            // The cloud level, loaded the first time you get there
    bool inClouds;          // On the cloud instead of the graveyard map
    Items items;
    Item selected;          // What the knight holds: the sword (halberd) or the shovel
    bool hasShovel;
    Vector3 shovelGround;   // Where the shovel stands in the graveyard, on the ground
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
    RayCollision hit = WorldRaycastVisible(world, (Ray){ target, dir }, orbit->distance + 0.3f);
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
        "E - pick up, 1 / 2 - choose item",
        "Walk into the beanstalk - climb",
        "W / S - up / down, Space - let go",
        "Mouse wheel - zoom",
        "R - back to spawn",
#if defined(__EMSCRIPTEN__)
        "Esc - free mouse",
#else
        "Esc - free mouse / quit",
#endif
    };
    const int count = sizeof(lines)/sizeof(lines[0]);

    DrawRectangle(10, 10, 330, 16 + count*20, Fade(BLACK, 0.45f));
    for (int i = 0; i < count; i++) DrawText(lines[i], 20, 18 + i*20, 18, (Color){ 210, 205, 190, 255 });
    DrawFPS(10, GetScreenHeight() - 30);

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

// Cartoon Zzz floating up from the sleeping ogre: each Z rises toward the room, swaying, growing and fading out.
// Drawn on screen over the 3D view, sized by distance, and left out when a wall is between the camera and it.
static void DrawSnores(const World *world, Camera3D camera)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    for (int i = 0; i < SNORE_COUNT; i++)
    {
        float t = fmodf((float)GetTime()/SNORE_PERIOD + (float)i/SNORE_COUNT, 1.0f);    // 0..1 through this Z's life
        Vector3 p = { OGRE_HEAD.x + 2.2f*t, OGRE_HEAD.y + 3.5f*t, OGRE_HEAD.z + 0.6f*sinf(t*2.0f*PI*1.5f) };

        Vector3 toZ = Vector3Subtract(p, camera.position);
        float distance = Vector3Length(toZ);
        if ((distance < 0.5f) || (Vector3DotProduct(toZ, forward) <= 0.0f)) continue;
        RayCollision hit = WorldRaycastVisible(world, (Ray){ camera.position, Vector3Scale(toZ, 1.0f/distance) }, distance);
        if (hit.hit && (hit.distance < distance - 0.3f)) continue;

        float alpha = fminf(t/0.15f, 1.0f)*fminf((1.0f - t)/0.35f, 1.0f);
        int size = (int)(GetScreenHeight()*(0.6f + 0.9f*t)/distance*1.4f);
        if (size < 8) continue;
        Vector2 s = GetWorldToScreen(p, camera);
        const char *z = "Z";
        int width = MeasureText(z, size);
        int x = (int)s.x - width/2, y = (int)s.y - size/2;
        int outline = (size/14 > 1)? size/14 : 1;
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++) if (dx || dy) DrawText(z, x + dx*outline, y + dy*outline, size, Fade((Color){ 20, 16, 40, 255 }, alpha));
        DrawText(z, x, y, size, Fade((Color){ 120, 165, 255, 255 }, alpha));                        // Blue shade along the bottom right
        DrawText(z, x - outline/2, y - outline/2, size, Fade((Color){ 245, 248, 255, 255 }, alpha));  // White face
    }
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

    World *world = game.inClouds? &game.cloud : &game.world;
    if (IsKeyPressed(KEY_R))
    {
        PlayerRespawn(&game.player, world);
        game.whiteout = 0.0f;
    }
    bool sprint = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    Player *player = &game.player;

    // Items: pick the shovel up in the graveyard, 1 and 2 choose what's in hand
    bool nearShovel = !game.inClouds && !game.hasShovel &&
        (Vector2Distance((Vector2){ player->position.x, player->position.z }, (Vector2){ game.shovelGround.x, game.shovelGround.z }) < SHOVEL_REACH);
    if (nearShovel && IsKeyPressed(KEY_E))
    {
        game.hasShovel = true;
        game.selected = ITEM_SHOVEL;
    }
    if (IsKeyPressed(KEY_ONE)) game.selected = ITEM_SWORD;
    if (IsKeyPressed(KEY_TWO) && game.hasShovel) game.selected = ITEM_SHOVEL;
    game.knight.halberdPutAway = (game.selected != ITEM_SWORD);
    Vector2 toStalk = { BEANSTALK_BASE.x - player->position.x, BEANSTALK_BASE.z - player->position.z };
    float stalkDistance = Vector2Length(toStalk);
    if (!game.inClouds && !player->climbing && (stalkDistance < BEANSTALK_RADIUS + BEANSTALK_GRAB) && (player->position.y < BEANSTALK_BASE.y + BEANSTALK_HEIGHT) &&
        (Vector3Length(move) > 0.1f) && ((move.x*toStalk.x + move.z*toStalk.y) > 0.5f*stalkDistance*Vector3Length(move)))
    {
        PlayerStartClimb(player, BEANSTALK_BASE, BEANSTALK_RADIUS);
    }

    if (player->climbing)
    {
        // W and S climb whichever way the camera faces; A and D go around the stalk
        float up = (IsKeyDown(KEY_W)? 1.0f : 0.0f) - (IsKeyDown(KEY_S)? 1.0f : 0.0f);
        float around = (IsKeyDown(KEY_D)? 1.0f : 0.0f) - (IsKeyDown(KEY_A)? 1.0f : 0.0f);
        if (game.whiteout > 0.0f) up = around = 0.0f;
        if (IsKeyPressed(KEY_SPACE) && (game.whiteout <= 0.0f)) PlayerJumpOffClimb(player, BEANSTALK_BASE);
        else PlayerClimb(player, &game.world, BEANSTALK_BASE, BEANSTALK_RADIUS, up, around, BEANSTALK_BASE.y + WHITEOUT_HEIGHT, dt);

        if (player->climbing && (player->position.y >= BEANSTALK_BASE.y + WHITEOUT_HEIGHT - 0.01f)) game.whiteout = fminf(game.whiteout + dt/WHITEOUT_TIME, 1.0f);

        // All white: come out on the cloud
        if ((game.whiteout >= 1.0f) && (game.cloud.model.meshCount > 0 || WorldLoad(&game.cloud, CLOUD_PATH, SKY_PATH, "")))
        {
            game.inClouds = true;
            world = &game.cloud;
            PlayerInit(player, world, CLOUD_SPAWN, CLOUD_SPAWN_YAW);
            game.orbit.yaw = CLOUD_SPAWN_YAW + PI;
        }
    }
    else
    {
        PlayerUpdate(player, world, move, sprint, IsKeyPressed(KEY_SPACE), dt);
        if (game.inClouds) game.whiteout = fmaxf(game.whiteout - dt/WHITEOUT_TIME, 0.0f);    // Fade in on the cloud
    }
    if (!game.inClouds && game.world.hasActor)
    {
        WandererUpdate(&game.hoodedFigure, &game.world, dt);
        PushApart(&game.player, KNIGHT_RADIUS, &game.hoodedFigure.body, HOODED_FIGURE_RADIUS);
    }
    if (!game.inClouds && game.hasBond) PushApart(&game.player, KNIGHT_RADIUS, &game.bondBody, BOND_RADIUS);
    UpdateOrbitCamera(&game.camera, orbit, world, game.player.position, dt);

    BeginDrawing();
        ClearBackground(WORLD_SKY_COLOR);
        BeginMode3D(game.camera);
            WorldDraw(world, game.camera);
            KnightDraw(&game.knight, &game.player);
            if ((game.selected == ITEM_SHOVEL) && !game.player.climbing) ItemsDrawShovel(&game.items, KnightHandTransform(&game.knight, SHOVEL_GRIP));
            if (!game.inClouds && !game.hasShovel)
            {
                // Stuck in the ground at a slant, blade half buried
                Matrix stuck = MatrixMultiply(MatrixRotateZ(0.25f), MatrixRotateY(0.6f));
                ItemsDrawShovel(&game.items, MatrixMultiply(stuck, MatrixTranslate(game.shovelGround.x, game.shovelGround.y - 0.12f, game.shovelGround.z)));
            }
            if (!game.inClouds)
            {
                DrawHoodedFigure(&game.hoodedFigure, &game.world);
                if (game.hasBond) DrawBond();
            }
        EndMode3D();
        if (game.inClouds) DrawSnores(world, game.camera);
        DrawHud();
        ItemsDrawBar(&game.items, game.selected, game.hasShovel);
        if (nearShovel)
        {
            const char *text = "E - pick up the shovel";
            int width = MeasureText(text, 24);
            DrawRectangle((GetScreenWidth() - width)/2 - 12, GetScreenHeight() - 90, width + 24, 40, Fade(BLACK, 0.5f));
            DrawText(text, (GetScreenWidth() - width)/2, GetScreenHeight() - 82, 24, RAYWHITE);
        }
        if (game.whiteout > 0.0f) DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(WHITE, game.whiteout));
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
        ItemsLoad(&game.items, HOTBAR_PATH, game.world.shader);
        game.shovelGround = SHOVEL_SPOT;
        WorldGroundHeight(&game.world, SHOVEL_SPOT.x, SHOVEL_SPOT.z, SHOVEL_SPOT.y + 50.0f, &game.shovelGround.y);
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
        ItemsUnload(&game.items);
        KnightUnload(&game.knight);
        if (game.cloud.model.meshCount > 0) WorldUnload(&game.cloud);
        WorldUnload(&game.world);
    }
    CloseWindow();
#endif

    return 0;
}
