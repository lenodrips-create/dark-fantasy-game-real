#include "knight.h"

#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Body parts the vertices are split into. Left is the knight's left (+X, it faces +Z).
enum {
    PART_TORSO,     // Also the head, shoulder pads and belt
    PART_THIGH_L,
    PART_SHIN_L,
    PART_THIGH_R,
    PART_SHIN_R,
    PART_UPPER_ARM_L,
    PART_FOREARM_L,
    PART_UPPER_ARM_R,
    PART_FOREARM_R, // Also the halberd
    PART_COUNT
};

// Joint positions on the left side in model units, measured from Armor_Halberd.obj (2.94 tall).
// The right side mirrors them in X.
static const Vector3 HIP = { 0.22f, 1.25f, 0.0f };
static const Vector3 KNEE = { 0.22f, 0.80f, 0.10f };
static const Vector3 SHOULDER = { 0.52f, 2.0f, -0.05f };
static const Vector3 ELBOW = { 0.75f, 1.78f, 0.0f };
#define THIGH_LENGTH 0.45f      // Hip to knee
#define SHIN_LENGTH  0.80f      // Knee to the ground

static Vector3 Mirror(Vector3 v, bool left)
{
    return left? v : (Vector3){ -v.x, v.y, v.z };
}

static unsigned char ClassifyVertex(Vector3 p)
{
    bool left = (p.x > 0.0f);
    float ax = fabsf(p.x);

    // The halberd runs front to back through the right hand
    if ((p.x < -0.6f) && (fabsf(p.z) > 0.55f)) return PART_FOREARM_R;

    if ((ax >= 0.45f) && (p.y >= 1.35f) && (p.y < 2.05f))
    {
        bool forearm = Vector2Distance((Vector2){ ax, p.y }, (Vector2){ 0.5f, 2.05f }) > 0.38f;
        if (left) return forearm? PART_FOREARM_L : PART_UPPER_ARM_L;
        return forearm? PART_FOREARM_R : PART_UPPER_ARM_R;
    }

    if ((p.y < 1.3f) && (ax > 0.05f))
    {
        bool shin = (p.y < KNEE.y);
        if (left) return shin? PART_SHIN_L : PART_THIGH_L;
        return shin? PART_SHIN_R : PART_THIGH_R;
    }

    return PART_TORSO;
}

bool KnightLoad(Knight *knight, const char *path, Shader shader)
{
    memset(knight, 0, sizeof(*knight));
    if (!FileExists(path)) return false;

    knight->model = LoadModel(path);    // Its .mtl brings in the texture
    if (knight->model.meshCount == 0) return false;

    for (int i = 0; i < knight->model.materialCount; i++) knight->model.materials[i].shader = shader;

    // Only the first mesh is animated; Armor_Halberd.obj is a single object
    Mesh *mesh = &knight->model.meshes[0];
    int count = mesh->vertexCount;
    knight->restPositions = malloc(count*3*sizeof(float));
    knight->restNormals = malloc(count*3*sizeof(float));
    knight->part = malloc(count);
    memcpy(knight->restPositions, mesh->vertices, count*3*sizeof(float));
    if (mesh->normals != NULL) memcpy(knight->restNormals, mesh->normals, count*3*sizeof(float));
    else memset(knight->restNormals, 0, count*3*sizeof(float));

    float minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < count; i++)
    {
        Vector3 p = { mesh->vertices[i*3], mesh->vertices[i*3 + 1], mesh->vertices[i*3 + 2] };
        knight->part[i] = ClassifyVertex(p);
        minY = fminf(minY, p.y);
        maxY = fmaxf(maxY, p.y);
    }
    knight->footY = minY;
    knight->scale = PLAYER_HEIGHT/fmaxf(maxY - minY, 0.001f);

    return true;
}

void KnightUnload(Knight *knight)
{
    for (int i = 0; i < knight->model.materialCount; i++)
    {
        Texture2D texture = knight->model.materials[i].maps[MATERIAL_MAP_ALBEDO].texture;
        if ((texture.id > 0) && (texture.id != rlGetTextureIdDefault())) UnloadTexture(texture);
    }
    UnloadModel(knight->model);     // The shader belongs to the world
    free(knight->restPositions);
    free(knight->restNormals);
    free(knight->part);
    memset(knight, 0, sizeof(*knight));
}

// Rotation around a pivot in the rest pose, on top of the parent part's transform
static Matrix Joint(Matrix parent, Vector3 pivot, Matrix rotation)
{
    Matrix local = MatrixMultiply(MatrixMultiply(MatrixTranslate(-pivot.x, -pivot.y, -pivot.z), rotation), MatrixTranslate(pivot.x, pivot.y, pivot.z));
    return MatrixMultiply(local, parent);
}

static Vector3 RotateOnly(Vector3 v, Matrix m)
{
    return (Vector3){ m.m0*v.x + m.m4*v.y + m.m8*v.z, m.m1*v.x + m.m5*v.y + m.m9*v.z, m.m2*v.x + m.m6*v.y + m.m10*v.z };
}

// Walk cycle modeled on Banjo's in Banjo-Kazooie: big quick strides with a high knee lift and a kick back,
// leaning into the run, bent arms pumping against the legs, a bounce on every step and a side-to-side roll.
// Swinging forward is a negative rotation around X because the model faces +Z.
static void PoseParts(const Player *player, Matrix *parts)
{
    float swing = player->stride;   // 0 standing still, 0.55 walking, 0.8 sprinting
    float phase = player->walkPhase;
    float breathe = sinf((float)GetTime()*2.0f)*0.012f*(1.0f - fminf(swing*2.0f, 1.0f));

    float thigh[2], knee[2];
    float reach = 0.0f;
    for (int i = 0; i < 2; i++)
    {
        float p = phase + i*PI;
        thigh[i] = sinf(p)*swing*1.5f;
        knee[i] = swing*(0.3f + 1.6f*fmaxf(0.0f, cosf(p)));
        if (!player->grounded)
        {
            thigh[i] = (i == 0)? 0.6f : -0.25f;
            knee[i] = (i == 0)? 0.9f : 0.4f;
        }

        // The hips sink to whichever foot reaches lowest, which makes the bounce
        reach = fmaxf(reach, THIGH_LENGTH*cosf(thigh[i]) + SHIN_LENGTH*cosf(thigh[i] - knee[i]));
    }
    float bounce = reach - (THIGH_LENGTH + SHIN_LENGTH) + breathe;

    float lean = swing*0.45f;
    // The upper body swings with every step: shoulders turn against the hips and rock side to side
    float roll = sinf(phase)*swing*0.25f;
    float twist = sinf(phase)*swing*0.55f;

    Matrix pelvis = MatrixTranslate(0.0f, bounce, 0.0f);
    Matrix torsoRotation = MatrixMultiply(MatrixMultiply(MatrixRotateY(twist), MatrixRotateZ(roll)), MatrixRotateX(lean));
    parts[PART_TORSO] = Joint(pelvis, (Vector3){ 0.0f, HIP.y, 0.0f }, torsoRotation);

    for (int i = 0; i < 2; i++)
    {
        bool left = (i == 0);
        int thighPart = left? PART_THIGH_L : PART_THIGH_R;
        int shinPart = left? PART_SHIN_L : PART_SHIN_R;
        parts[thighPart] = Joint(pelvis, Mirror(HIP, left), MatrixRotateX(-thigh[i]));
        parts[shinPart] = Joint(parts[thighPart], Mirror(KNEE, left), MatrixRotateX(knee[i]));

        // Arms pump against the leg on the same side. The right hand holds the halberd,
        // so that arm moves much less to keep the weapon roughly level.
        float armSwing = left? -thigh[i]*0.9f : -thigh[i]*0.35f;
        float elbowBend = left? 0.3f + swing*1.2f : 0.1f + swing*0.2f;
        if (!player->grounded)
        {
            armSwing = left? 0.6f : 0.2f;
            elbowBend = left? 1.0f : 0.2f;
        }

        int upperPart = left? PART_UPPER_ARM_L : PART_UPPER_ARM_R;
        int forearmPart = left? PART_FOREARM_L : PART_FOREARM_R;
        parts[upperPart] = Joint(parts[PART_TORSO], Mirror(SHOULDER, left), MatrixRotateX(-armSwing));
        parts[forearmPart] = Joint(parts[upperPart], Mirror(ELBOW, left), MatrixRotateX(-elbowBend));
    }
}

void KnightDraw(Knight *knight, const Player *player)
{
    Matrix parts[PART_COUNT];
    PoseParts(player, parts);

    Mesh *mesh = &knight->model.meshes[0];
    for (int i = 0; i < mesh->vertexCount; i++)
    {
        Matrix m = parts[knight->part[i]];
        Vector3 p = Vector3Transform((Vector3){ knight->restPositions[i*3], knight->restPositions[i*3 + 1], knight->restPositions[i*3 + 2] }, m);
        Vector3 n = RotateOnly((Vector3){ knight->restNormals[i*3], knight->restNormals[i*3 + 1], knight->restNormals[i*3 + 2] }, m);
        mesh->vertices[i*3] = p.x;
        mesh->vertices[i*3 + 1] = p.y;
        mesh->vertices[i*3 + 2] = p.z;
        if (mesh->normals != NULL)
        {
            mesh->normals[i*3] = n.x;
            mesh->normals[i*3 + 1] = n.y;
            mesh->normals[i*3 + 2] = n.z;
        }
    }
    UpdateMeshBuffer(*mesh, 0, mesh->vertices, mesh->vertexCount*3*sizeof(float), 0);
    if (mesh->normals != NULL) UpdateMeshBuffer(*mesh, 2, mesh->normals, mesh->vertexCount*3*sizeof(float), 0);

    float s = knight->scale;
    Matrix transform = MatrixMultiply(MatrixTranslate(0.0f, -knight->footY, 0.0f), MatrixScale(s, s, s));
    transform = MatrixMultiply(transform, MatrixRotateY(player->yaw));
    transform = MatrixMultiply(transform, MatrixTranslate(player->position.x, player->position.y, player->position.z));

    // Parts of the armor and the halberd blade are single sided
    rlDisableBackfaceCulling();
    DrawMesh(*mesh, knight->model.materials[knight->model.meshMaterial[0]], transform);
    rlEnableBackfaceCulling();

    PlayerDrawShadow(player, 0.4f);
}
