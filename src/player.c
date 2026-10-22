#include "player.h"

#include "raymath.h"

#include <math.h>

#define PLAYER_RADIUS  0.2f
#define STEP_HEIGHT    0.3f     // Highest ledge the player walks up without jumping
#define WALK_SPEED     4.5f
#define SPRINT_SPEED   9.0f
#define JUMP_SPEED     5.0f
#define GRAVITY        20.0f
#define TURN_RATE      12.0f
#define FALL_LIMIT     -60.0f   // Respawn if we ever drop out of the world

#define STRIDE_RATE    3.1f     // Walk cycle radians per unit travelled: one step covers about a unit

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

void PlayerDrawShadow(const Player *player, float radius)
{
    // Soft blob on the ground, shrinking as the character jumps
    float height = fmaxf(player->position.y - player->groundY, 0.0f);
    float shadowRadius = fmaxf(radius - height*0.15f, radius*0.35f);
    Vector3 shadow = { player->position.x, player->groundY + 0.03f, player->position.z };
    DrawCylinder(shadow, shadowRadius, shadowRadius, 0.01f, 20, Fade(BLACK, 0.45f));
}
