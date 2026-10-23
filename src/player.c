#include "player.h"

#include "raymath.h"

#include <math.h>

#define PLAYER_RADIUS  0.2f
#define STEP_HEIGHT    0.3f     // Highest ledge the player walks up without jumping
#define WALK_SPEED     5.0f
#define SPRINT_SPEED   9.0f
#define JUMP_SPEED     5.0f
#define GRAVITY        20.0f
#define TURN_RATE      12.0f
#define FALL_LIMIT     -60.0f   // Respawn if we ever drop out of the world
#define CLIMB_SPEED    3.0f     // Up and down the beanstalk
#define CLIMB_REACH    0.45f    // Gap between the pole's surface and the feet while hanging on
#define CLIMB_RATE     2.2f     // Climb cycle radians per unit climbed

#define STRIDE_RATE    1.7f     // Default walk cycle radians per unit travelled: one step covers about 1.85 units

void PlayerInit(Player *player, const World *world, Vector3 spawn, float yaw)
{
    *player = (Player){ 0 };
    player->spawn = spawn;
    player->spawnYaw = yaw;
    player->strideRate = STRIDE_RATE;
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
    player->climbing = false;
}

// Ground under the feet: the highest surface below the middle and four points around it, so plank floors
// with gaps (the dock) still hold the player up
static bool FeetGroundHeight(const World *world, float x, float z, float fromY, float *outY)
{
    static const float offsets[5][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    bool found = false;
    for (int i = 0; i < 5; i++)
    {
        float y;
        if (WorldGroundHeight(world, x + offsets[i][0]*PLAYER_RADIUS, z + offsets[i][1]*PLAYER_RADIUS, fromY, &y))
        {
            if (!found || (y > *outY)) *outY = y;
            found = true;
        }
    }
    return found;
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
    if (!FeetGroundHeight(world, x, z, player->position.y + STEP_HEIGHT, &ground)) return false;

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
    bool hasGround = FeetGroundHeight(world, player->position.x, player->position.z, player->position.y + STEP_HEIGHT, &ground);

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
    player->walkPhase = fmodf(player->walkPhase + moved*player->strideRate, 2.0f*PI);
    float targetStride = (moved > 0.0001f)? (player->sprinting? 0.8f : 0.55f) : 0.0f;
    player->stride += (targetStride - player->stride)*fminf(1.0f, 10.0f*dt);
}

// Direction from the pole's axis out to the player, on the XZ plane
static Vector2 AwayFromAxis(const Player *player, Vector3 axis)
{
    Vector2 away = { player->position.x - axis.x, player->position.z - axis.z };
    float length = Vector2Length(away);
    return (length > 0.0001f)? Vector2Scale(away, 1.0f/length) : (Vector2){ 0.0f, 1.0f };
}

void PlayerStartClimb(Player *player, Vector3 axis, float radius)
{
    Vector2 away = AwayFromAxis(player, axis);
    player->climbing = true;
    player->grounded = false;
    player->velocityY = 0.0f;
    player->stride = 0.0f;
    player->position.x = axis.x + away.x*(radius + CLIMB_REACH);
    player->position.z = axis.z + away.y*(radius + CLIMB_REACH);
    player->position.y += 0.15f;    // Off the ground, so climbing down to it lets go
    player->yaw = atan2f(-away.x, -away.y);
}

void PlayerClimb(Player *player, const World *world, Vector3 axis, float radius, float up, float around, float maxY, float dt)
{
    Vector2 away = AwayFromAxis(player, axis);
    float distance = radius + CLIMB_REACH;
    float angle = atan2f(away.y, away.x) + around*CLIMB_SPEED*0.6f*dt/distance;
    float startY = player->position.y;

    player->position.y = fminf(player->position.y + up*CLIMB_SPEED*dt, maxY);
    player->position.x = axis.x + cosf(angle)*distance;
    player->position.z = axis.z + sinf(angle)*distance;
    player->yaw = atan2f(-cosf(angle), -sinf(angle));    // Facing the pole

    float ground;
    if (WorldGroundHeight(world, player->position.x, player->position.z, player->position.y + STEP_HEIGHT, &ground))
    {
        player->groundY = ground;
        if (player->position.y <= ground)
        {
            // Back down on the ground
            player->position.y = ground;
            player->climbing = false;
            player->grounded = true;
        }
    }

    float climbed = fabsf(player->position.y - startY) + fabsf(around)*CLIMB_SPEED*0.6f*dt;
    player->climbPhase = fmodf(player->climbPhase + climbed*CLIMB_RATE, 2.0f*PI);
}

void PlayerJumpOffClimb(Player *player, Vector3 axis)
{
    Vector2 away = AwayFromAxis(player, axis);
    player->climbing = false;
    player->grounded = false;
    player->velocityY = JUMP_SPEED*0.6f;
    player->position.x += away.x*0.4f;
    player->position.z += away.y*0.4f;
    player->yaw = atan2f(away.x, away.y);
}

void PlayerDrawShadow(const Player *player, float radius)
{
    // Soft blob on the ground, shrinking as the character jumps
    float height = fmaxf(player->position.y - player->groundY, 0.0f);
    float shadowRadius = fmaxf(radius - height*0.15f, radius*0.35f);
    Vector3 shadow = { player->position.x, player->groundY + 0.03f, player->position.z };
    DrawCylinder(shadow, shadowRadius, shadowRadius, 0.01f, 20, Fade(BLACK, 0.45f));
}
