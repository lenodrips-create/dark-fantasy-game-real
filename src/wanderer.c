#include "wanderer.h"

#include "raymath.h"

#define STROLL       0.35f  // Fraction of walking speed
#define ARRIVED      0.5f   // Close enough to the destination, in world units
#define GIVE_UP_TIME 1.0f   // Pick somewhere else after this long blocked (a gravestone, a tree)

static float RandomRange(float min, float max)
{
    return min + (max - min)*(float)GetRandomValue(0, 10000)/10000.0f;
}

static void PickTarget(Wanderer *wanderer)
{
    wanderer->target.x = RandomRange(wanderer->area.x, wanderer->area.x + wanderer->area.width);
    wanderer->target.y = RandomRange(wanderer->area.y, wanderer->area.y + wanderer->area.height);
    wanderer->stuck = 0.0f;
}

void WandererInit(Wanderer *wanderer, const World *world, Vector3 start, Rectangle area)
{
    *wanderer = (Wanderer){ 0 };
    wanderer->area = area;
    PlayerInit(&wanderer->body, world, start, 0.0f);
    PickTarget(wanderer);
}

void WandererUpdate(Wanderer *wanderer, const World *world, float dt)
{
    Player *body = &wanderer->body;
    Vector3 move = { 0 };

    if (wanderer->wait > 0.0f)
    {
        wanderer->wait -= dt;
        if (wanderer->wait <= 0.0f) PickTarget(wanderer);
    }
    else
    {
        Vector2 toTarget = { wanderer->target.x - body->position.x, wanderer->target.y - body->position.z };
        float distance = Vector2Length(toTarget);
        if (distance < ARRIVED) wanderer->wait = RandomRange(1.0f, 4.0f);
        else move = (Vector3){ toTarget.x/distance*STROLL, 0.0f, toTarget.y/distance*STROLL };
    }

    Vector3 before = body->position;
    PlayerUpdate(body, world, move, false, false, dt);

    if ((move.x != 0.0f) || (move.z != 0.0f))
    {
        float moved = Vector2Distance((Vector2){ before.x, before.z }, (Vector2){ body->position.x, body->position.z });
        wanderer->stuck = (moved < 0.2f*dt)? wanderer->stuck + dt : 0.0f;
        if (wanderer->stuck > GIVE_UP_TIME) PickTarget(wanderer);
    }
}
