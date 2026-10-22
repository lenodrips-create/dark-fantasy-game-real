#include "player.h"

#include "raymath.h"

#include <math.h>

#define PLAYER_RADIUS  0.2f
#define STEP_HEIGHT    0.3f     // Highest ledge the player walks up without jumping
#define WALK_SPEED     3.5f
#define SPRINT_SPEED   8.0f
#define JUMP_SPEED     5.0f
#define GRAVITY        20.0f
#define TURN_RATE      12.0f
#define FALL_LIMIT     -60.0f   // Respawn if we ever drop out of the world

// Stickman proportions (feet at 0, adds up to PLAYER_HEIGHT standing straight)
#define THIGH_LENGTH     0.34f
#define SHIN_LENGTH      0.34f
#define SPINE_LENGTH     0.43f
#define UPPER_ARM_LENGTH 0.28f
#define FOREARM_LENGTH   0.26f
#define HEAD_RADIUS      0.14f
#define LIMB_RADIUS      0.035f
#define STRIDE_RATE      4.6f   // Walk cycle radians per unit travelled

static const Color BONE_COLOR = { 228, 222, 206, 255 };

void PlayerInit(Player *player, const World *world, Vector3 spawn, float yaw)
{
    *player = (Player){ 0 };
    player->spawn = spawn;
    player->spawnYaw = yaw;
    PlayerRespawn(player, world);
}

void PlayerRespawn(Player *player, const World *world)
{
    Vector3 position = player->spawn;
    float ground;
    if (WorldGroundHeight(world, position.x, position.z, position.y + 50.0f, &ground)) position.y = ground;

    player->position = position;
    player->yaw = player->spawnYaw;
    player->velocityY = 0.0f;
    player->groundY = position.y;
    player->grounded = true;
}

// Moves horizontally by (dx, dz) unless something solid is in the way or there's no ground there
static bool TryMove(Player *player, const World *world, float dx, float dz)
{
    float distance = sqrtf(dx*dx + dz*dz);
    if (distance < 0.00001f) return false;

    Vector3 dir = { dx/distance, 0.0f, dz/distance };
    Vector3 side = { -dir.z, 0.0f, dir.x };

    // Feel ahead at knee, waist and chest height, across the width of the body
    static const float heights[] = { STEP_HEIGHT + 0.1f, 0.75f, 1.15f };
    static const float offsets[] = { -0.7f, 0.0f, 0.7f };
    for (int h = 0; h < 3; h++)
    {
        for (int o = 0; o < 3; o++)
        {
            Vector3 origin = {
                player->position.x + side.x*offsets[o]*PLAYER_RADIUS,
                player->position.y + heights[h],
                player->position.z + side.z*offsets[o]*PLAYER_RADIUS
            };
            if (WorldRaycast(world, (Ray){ origin, dir }, distance + PLAYER_RADIUS).hit) return false;
        }
    }

    // Never walk off the edge of the map
    float x = player->position.x + dx;
    float z = player->position.z + dz;
    float ground;
    if (!WorldGroundHeight(world, x, z, player->position.y + STEP_HEIGHT, &ground)) return false;

    player->position.x = x;
    player->position.z = z;
    return true;
}

void PlayerUpdate(Player *player, const World *world, Vector3 move, bool sprint, bool jump, float dt)
{
    Vector3 start = player->position;
    float amount = fminf(Vector3Length(move), 1.0f);
    player->sprinting = sprint && (amount > 0.1f);

    if (amount > 0.01f)
    {
        Vector3 dir = Vector3Scale(move, 1.0f/Vector3Length(move));
        float distance = (player->sprinting? SPRINT_SPEED : WALK_SPEED)*amount*dt;
        float dx = dir.x*distance;
        float dz = dir.z*distance;

        if (!TryMove(player, world, dx, dz))
        {
            // Slide along whatever blocked us
            TryMove(player, world, dx, 0.0f);
            TryMove(player, world, 0.0f, dz);
        }

        // Turn smoothly toward the direction of travel
        float turn = Wrap(atan2f(dir.x, dir.z) - player->yaw, -PI, PI);
        player->yaw = Wrap(player->yaw + turn*fminf(1.0f, TURN_RATE*dt), -PI, PI);
    }

    float ground = 0.0f;
    bool hasGround = WorldGroundHeight(world, player->position.x, player->position.z, player->position.y + STEP_HEIGHT, &ground);

    if (player->grounded && jump)
    {
        player->velocityY = JUMP_SPEED;
        player->grounded = false;
    }

    if (player->grounded)
    {
        // Stay glued to the ground walking down slopes, fall off anything taller than a step
        if (hasGround && (player->position.y - ground <= STEP_HEIGHT)) player->position.y = ground;
        else player->grounded = false;
    }

    if (!player->grounded)
    {
        player->velocityY -= GRAVITY*dt;
        player->position.y += player->velocityY*dt;
        if (hasGround && (player->position.y <= ground))
        {
            player->position.y = ground;
            player->velocityY = 0.0f;
            player->grounded = true;
        }
    }

    if (hasGround) player->groundY = ground;
    if (player->position.y < FALL_LIMIT) PlayerRespawn(player, world);

    // Walk cycle follows the distance actually covered, so legs don't run in place against walls
    float moved = Vector2Length((Vector2){ player->position.x - start.x, player->position.z - start.z });
    player->walkPhase = fmodf(player->walkPhase + moved*STRIDE_RATE, 2.0f*PI);
    float targetStride = (moved > 0.0001f)? (player->sprinting? 0.8f : 0.55f) : 0.0f;
    player->stride += (targetStride - player->stride)*fminf(1.0f, 10.0f*dt);
}

Stickman StickmanLoad(Shader shader)
{
    Stickman stickman = { 0 };
    stickman.limb = GenMeshCylinder(1.0f, 1.0f, 10);   // Unit cylinder from y = 0 to y = 1
    stickman.joint = GenMeshSphere(1.0f, 12, 16);
    stickman.material = LoadMaterialDefault();
    stickman.material.shader = shader;
    stickman.material.maps[MATERIAL_MAP_ALBEDO].color = BONE_COLOR;
    return stickman;
}

void StickmanUnload(Stickman *stickman)
{
    UnloadMesh(stickman->limb);
    UnloadMesh(stickman->joint);
    MemFree(stickman->material.maps);   // The shader belongs to the world
    *stickman = (Stickman){ 0 };
}

// Local stickman space: x = right, y = up, z = forward
static Vector3 ToWorld(const Player *player, Vector3 local)
{
    float s = sinf(player->yaw);
    float c = cosf(player->yaw);
    return (Vector3){
        player->position.x - local.x*c + local.z*s,
        player->position.y + local.y,
        player->position.z + local.x*s + local.z*c
    };
}

// Direction swung forward by angle from hanging straight down, splayed sideways
static Vector3 LimbDir(float angle, float splay)
{
    return Vector3Normalize((Vector3){ splay, -cosf(angle), sinf(angle) });
}

static void DrawJoint(const Stickman *stickman, Vector3 position, float radius)
{
    Matrix transform = MatrixMultiply(MatrixScale(radius, radius, radius), MatrixTranslate(position.x, position.y, position.z));
    DrawMesh(stickman->joint, stickman->material, transform);
}

static void DrawBone(const Stickman *stickman, Vector3 a, Vector3 b, float radius)
{
    // Always point the cylinder upward so the rotation never has to flip 180 degrees
    if (b.y < a.y) { Vector3 t = a; a = b; b = t; }

    Vector3 delta = Vector3Subtract(b, a);
    float length = Vector3Length(delta);
    if (length < 0.0001f) return;

    Quaternion rotation = QuaternionFromVector3ToVector3((Vector3){ 0.0f, 1.0f, 0.0f }, Vector3Scale(delta, 1.0f/length));
    Matrix transform = MatrixMultiply(MatrixMultiply(MatrixScale(radius, length, radius), QuaternionToMatrix(rotation)), MatrixTranslate(a.x, a.y, a.z));
    DrawMesh(stickman->limb, stickman->material, transform);
    DrawJoint(stickman, a, radius);
    DrawJoint(stickman, b, radius);
}

void PlayerDraw(const Player *player, const Stickman *stickman)
{
    float swing = player->stride;
    float idle = sinf((float)GetTime()*1.8f)*0.05f*(1.0f - fminf(swing*2.0f, 1.0f));

    // Legs: left and right half a cycle apart, knee bends while the leg swings forward
    Vector3 thigh[2], shin[2];
    float hipHeight = 0.0f;
    for (int i = 0; i < 2; i++)
    {
        float side = (i == 0)? 1.0f : -1.0f;
        float phase = player->walkPhase + i*PI;
        float thighAngle = sinf(phase)*swing;
        float kneeBend = swing*1.3f*fmaxf(0.0f, cosf(phase));
        if (!player->grounded) { thighAngle = 0.35f*side; kneeBend = 0.6f; }

        thigh[i] = Vector3Scale(LimbDir(thighAngle, side*0.12f), THIGH_LENGTH);
        shin[i] = Vector3Scale(LimbDir(thighAngle - kneeBend, side*0.08f), SHIN_LENGTH);

        // The hip rides on whichever leg reaches lowest, which gives the walk its bob
        hipHeight = fmaxf(hipHeight, -(thigh[i].y + shin[i].y));
    }

    Vector3 hip = { 0.0f, hipHeight, 0.0f };
    float lean = swing*0.3f;
    Vector3 spine = { 0.0f, cosf(lean), sinf(lean) };
    Vector3 neck = Vector3Add(hip, Vector3Scale(spine, SPINE_LENGTH));
    Vector3 shoulder = Vector3Add(hip, Vector3Scale(spine, SPINE_LENGTH - 0.05f));
    Vector3 head = Vector3Add(neck, Vector3Scale(spine, HEAD_RADIUS + 0.015f));

    Vector3 hipW = ToWorld(player, hip);
    DrawBone(stickman, hipW, ToWorld(player, neck), LIMB_RADIUS*1.2f);
    DrawJoint(stickman, ToWorld(player, head), HEAD_RADIUS);

    for (int i = 0; i < 2; i++)
    {
        float side = (i == 0)? 1.0f : -1.0f;

        Vector3 knee = Vector3Add(hip, thigh[i]);
        Vector3 foot = Vector3Add(knee, shin[i]);
        DrawBone(stickman, hipW, ToWorld(player, knee), LIMB_RADIUS);
        DrawBone(stickman, ToWorld(player, knee), ToWorld(player, foot), LIMB_RADIUS);

        // Arms swing against the leg on the same side
        float armAngle = -sinf(player->walkPhase + i*PI)*swing*0.9f + idle*side;
        float forearmAngle = armAngle + 0.25f + swing*0.6f;
        if (!player->grounded) { armAngle = 0.4f; forearmAngle = 1.2f; }

        Vector3 elbow = Vector3Add(shoulder, Vector3Scale(LimbDir(armAngle, side*0.35f), UPPER_ARM_LENGTH));
        Vector3 hand = Vector3Add(elbow, Vector3Scale(LimbDir(forearmAngle, side*0.2f), FOREARM_LENGTH));
        DrawBone(stickman, ToWorld(player, shoulder), ToWorld(player, elbow), LIMB_RADIUS);
        DrawBone(stickman, ToWorld(player, elbow), ToWorld(player, hand), LIMB_RADIUS);
    }

    // Soft blob shadow on the ground, shrinking as we jump
    float height = fmaxf(player->position.y - player->groundY, 0.0f);
    float shadowRadius = fmaxf(0.28f - height*0.15f, 0.1f);
    Vector3 shadow = { player->position.x, player->groundY + 0.03f, player->position.z };
    DrawCylinder(shadow, shadowRadius, shadowRadius, 0.01f, 20, Fade(BLACK, 0.45f));
}
