#include "world.h"

#include "raymath.h"
#include "rlgl.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GRID_CELL_SIZE 8.0f     // Collision grid cell size in world units
#define GRID_MAX_CELLS 512      // Per axis; cells get bigger on huge maps instead
#define BOX_COLLIDER_NAME "Fence"   // Meshes whose material name contains this collide as boxes
#define ACTOR_LEG_TOP  0.2f     // Fraction of the actor's height where its legs meet the robe
#define ACTOR_ARM_OUT  0.18f    // Arms (sleeves, hands, whatever they hold) reach further out than this fraction of the height
#define ACTOR_ARM_TOP  0.63f    // and sit below this fraction of the height

// Moonlight for the dark fantasy look
static const Vector3 LIGHT_DIR    = { -0.35f, -0.85f, 0.40f };  // Direction the moonlight travels
static const Vector3 LIGHT_COLOR   = { 0.62f, 0.66f, 0.82f };
static const Vector3 AMBIENT_COLOR = { 0.36f, 0.36f, 0.46f };

// The sky image covers half the horizon and is mirrored for the other half, so its edges always line up.
// Its bottom edge sits a little below the horizon, its top edge at the zenith.
#define SKY_BOTTOM -0.15f   // Elevation in radians

// Desktop runs OpenGL 3.3, the browser build WebGL 2 (OpenGL ES 3.0)
#if defined(__EMSCRIPTEN__)
    #define GLSL_HEADER "#version 300 es\nprecision highp float;\n"
#else
    #define GLSL_HEADER "#version 330\n"
#endif

static const char *LIT_VS =
    GLSL_HEADER
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matNormal;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragNormal;\n"
    "void main()\n"
    "{\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *LIT_FS =
    GLSL_HEADER
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragNormal;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 lightDir;\n"
    "uniform vec3 lightColor;\n"
    "uniform vec3 ambientColor;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec4 texel = texture(texture0, fragTexCoord)*colDiffuse*fragColor;\n"
    "    if (texel.a < 0.5) discard;\n"  // Alpha-tested leaves and tree cards
    "    vec3 n = normalize(fragNormal);\n"
    "    if (!gl_FrontFacing) n = -n;\n"  // Blender materials are double sided
    "    float diffuse = max(dot(n, -lightDir), 0.0);\n"
    "    finalColor = vec4(texel.rgb*(ambientColor + lightColor*diffuse), 1.0);\n"
    "}\n";

// Drawn on a cube around the camera: the direction to each pixel picks the spot in the sky image,
// so the sky stays fixed to the world when turning and never gets closer when walking
static const char *SKY_VS =
    GLSL_HEADER
    "in vec3 vertexPosition;\n"
    "uniform mat4 mvp;\n"
    "out vec3 fragDirection;\n"
    "void main()\n"
    "{\n"
    "    fragDirection = vertexPosition;\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *SKY_FS =
    GLSL_HEADER
    "in vec3 fragDirection;\n"
    "uniform sampler2D texture0;\n"
    "uniform float skyBottom;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    const float PI = 3.14159265;\n"
    "    vec3 d = normalize(fragDirection);\n"
    "    float u = abs(atan(d.x, d.z))/PI;\n"   // 0..1 across half the horizon, mirrored for the other half
    "    float elevation = asin(clamp(d.y, -1.0, 1.0));\n"
    "    float v = clamp(1.0 - (elevation - skyBottom)/(0.5*PI - skyBottom), 0.0, 1.0);\n"
    "    finalColor = vec4(texture(texture0, vec2(u, v)).rgb, 1.0);\n"
    "}\n";

// Scans the glTF JSON for materials whose alphaMode is BLEND or MASK (leaves, tree line cards).
// raylib keeps glTF material i at model.materials[i + 1], index 0 being its default material.
// Also finds the material called actorName and returns its raylib index in *actorMaterial, and flags materials
// whose name contains BOX_COLLIDER_NAME in box.
static void ScanMaterials(const char *json, int length, bool *alpha, bool *box, int materialCount, const char *actorName, int *actorMaterial)
{
    int actorNameLength = (int)strlen(actorName);
    bool expectName = false;
    int depth = 0;
    int materialsDepth = -1;    // Depth of the top-level "materials" array while we are inside it
    int material = -1;
    bool expectMaterials = false;
    bool expectAlphaMode = false;

    for (int i = 0; i < length; i++)
    {
        char c = json[i];

        if (c == '"')
        {
            int start = ++i;
            while ((i < length) && (json[i] != '"')) i += (json[i] == '\\')? 2 : 1;
            int len = i - start;
            const char *s = json + start;

            if (expectName)
            {
                if ((len == actorNameLength) && (memcmp(s, actorName, len) == 0)) *actorMaterial = material + 1;
                int boxNameLength = (int)strlen(BOX_COLLIDER_NAME);
                for (int k = 0; (k + boxNameLength <= len) && (material + 1 < materialCount); k++)
                {
                    if (memcmp(s + k, BOX_COLLIDER_NAME, boxNameLength) == 0) { box[material + 1] = true; break; }
                }
                expectName = false;
            }
            else if (expectAlphaMode)
            {
                bool opaque = (len == 6) && (memcmp(s, "OPAQUE", 6) == 0);
                if (!opaque && (material + 1 < materialCount)) alpha[material + 1] = true;
                expectAlphaMode = false;
            }
            else if ((depth == 1) && (len == 9) && (memcmp(s, "materials", 9) == 0)) expectMaterials = true;
            else if ((materialsDepth >= 0) && (depth == materialsDepth + 1) && (len == 9) && (memcmp(s, "alphaMode", 9) == 0)) expectAlphaMode = true;
            else if ((materialsDepth >= 0) && (depth == materialsDepth + 1) && (len == 4) && (memcmp(s, "name", 4) == 0)) expectName = true;
        }
        else if ((c == '{') || (c == '['))
        {
            depth++;
            if (expectMaterials && (c == '['))
            {
                materialsDepth = depth;
                expectMaterials = false;
            }
            else if ((materialsDepth >= 0) && (c == '{') && (depth == materialsDepth + 1)) material++;
        }
        else if ((c == '}') || (c == ']'))
        {
            if (depth == materialsDepth) materialsDepth = -1;
            depth--;
        }
    }
}

static void FindMaterials(const char *path, bool *alpha, bool *box, int materialCount, const char *actorName, int *actorMaterial)
{
    int size = 0;
    unsigned char *file = LoadFileData(path, &size);
    if (file == NULL) return;

    if ((size > 20) && (memcmp(file, "glTF", 4) == 0))
    {
        // Binary glTF: 12 byte header, then the JSON chunk (length, type, data)
        unsigned int jsonLength = file[12] | (file[13] << 8) | (file[14] << 16) | ((unsigned int)file[15] << 24);
        if (jsonLength <= (unsigned int)size - 20) ScanMaterials((const char *)file + 20, (int)jsonLength, alpha, box, materialCount, actorName, actorMaterial);
    }
    else ScanMaterials((const char *)file, size, alpha, box, materialCount, actorName, actorMaterial);

    UnloadFileData(file);
}

static int ClampInt(int value, int min, int max)
{
    return (value < min)? min : ((value > max)? max : value);
}

// Grid cells covered by an XZ rectangle, clamped to the grid
static void CellRange(const World *world, float minX, float minZ, float maxX, float maxZ, int *x0, int *z0, int *x1, int *z1)
{
    *x0 = ClampInt((int)floorf((minX - world->gridMinX)/world->cellSize), 0, world->gridWidth - 1);
    *z0 = ClampInt((int)floorf((minZ - world->gridMinZ)/world->cellSize), 0, world->gridDepth - 1);
    *x1 = ClampInt((int)floorf((maxX - world->gridMinX)/world->cellSize), 0, world->gridWidth - 1);
    *z1 = ClampInt((int)floorf((maxZ - world->gridMinZ)/world->cellSize), 0, world->gridDepth - 1);
}

// Copies every solid triangle (foliage excluded) into a flat list and buckets it into a grid over the
// XZ plane, so a ray only tests the triangles near it no matter how big the map gets.
// The actor moves around, so it is left out too.
static void BuildCollisionGrid(World *world, const bool *alphaMaterial, const bool *boxMaterial)
{
    const Model *model = &world->model;

    int capacity = 0;
    for (int m = 0; m < model->meshCount; m++)
    {
        if (alphaMaterial[model->meshMaterial[m]] || world->isActor[m]) continue;
        capacity += boxMaterial[model->meshMaterial[m]]? 12 : model->meshes[m].triangleCount;
    }
    world->triangles = malloc((capacity > 0? capacity : 1)*3*sizeof(Vector3));

    Vector2 min = { FLT_MAX, FLT_MAX };
    Vector2 max = { -FLT_MAX, -FLT_MAX };
    int count = 0;
    for (int m = 0; m < model->meshCount; m++)
    {
        const Mesh *mesh = &model->meshes[m];
        if (alphaMaterial[model->meshMaterial[m]] || world->isActor[m] || (mesh->vertices == NULL)) continue;

        if (boxMaterial[model->meshMaterial[m]])
        {
            // Thin things like fence rails collide as their whole bounding box, so nothing slips between them
            BoundingBox b = GetMeshBoundingBox(*mesh);
            Vector3 c[8];
            for (int k = 0; k < 8; k++) c[k] = (Vector3){ (k & 1)? b.max.x : b.min.x, (k & 2)? b.max.y : b.min.y, (k & 4)? b.max.z : b.min.z };
            static const int faces[6][4] = { { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 } };
            for (int f = 0; f < 6; f++)
            {
                const int *q = faces[f];
                Vector3 quad[6] = { c[q[0]], c[q[1]], c[q[2]], c[q[0]], c[q[2]], c[q[3]] };
                for (int k = 0; k < 6; k++) world->triangles[count*3 + k] = quad[k];
                count += 2;
            }
            min = Vector2Min(min, (Vector2){ b.min.x, b.min.z });
            max = Vector2Max(max, (Vector2){ b.max.x, b.max.z });
            continue;
        }

        for (int t = 0; t < mesh->triangleCount; t++, count++)
        {
            for (int k = 0; k < 3; k++)
            {
                int v = (mesh->indices != NULL)? mesh->indices[t*3 + k] : t*3 + k;
                Vector3 p = { mesh->vertices[v*3], mesh->vertices[v*3 + 1], mesh->vertices[v*3 + 2] };
                world->triangles[count*3 + k] = p;
                min = Vector2Min(min, (Vector2){ p.x, p.z });
                max = Vector2Max(max, (Vector2){ p.x, p.z });
            }
        }
    }
    world->triangleCount = count;
    if (count == 0) return;

    world->cellSize = fmaxf(GRID_CELL_SIZE, fmaxf(max.x - min.x, max.y - min.y)/GRID_MAX_CELLS);
    world->gridMinX = min.x;
    world->gridMinZ = min.y;
    world->gridWidth = (int)((max.x - min.x)/world->cellSize) + 1;
    world->gridDepth = (int)((max.y - min.y)/world->cellSize) + 1;

    int cellCount = world->gridWidth*world->gridDepth;
    world->cellStart = calloc(cellCount + 1, sizeof(int));
    int *fill = NULL;

    // Pass 0 counts the triangles per cell, pass 1 stores them
    for (int pass = 0; pass < 2; pass++)
    {
        for (int t = 0; t < count; t++)
        {
            const Vector3 *tri = &world->triangles[t*3];
            int x0, z0, x1, z1;
            CellRange(world, fminf(tri[0].x, fminf(tri[1].x, tri[2].x)), fminf(tri[0].z, fminf(tri[1].z, tri[2].z)),
                             fmaxf(tri[0].x, fmaxf(tri[1].x, tri[2].x)), fmaxf(tri[0].z, fmaxf(tri[1].z, tri[2].z)), &x0, &z0, &x1, &z1);

            for (int z = z0; z <= z1; z++)
            {
                for (int x = x0; x <= x1; x++)
                {
                    int cell = z*world->gridWidth + x;
                    if (pass == 0) world->cellStart[cell + 1]++;
                    else world->cellItems[fill[cell]++] = t;
                }
            }
        }

        if (pass == 0)
        {
            for (int c = 0; c < cellCount; c++) world->cellStart[c + 1] += world->cellStart[c];
            world->cellItems = malloc(world->cellStart[cellCount]*sizeof(int));
            fill = malloc(cellCount*sizeof(int));
            memcpy(fill, world->cellStart, cellCount*sizeof(int));
        }
    }
    free(fill);
}

// Finds the actor's feet and splits off its limbs. Legs: everything below ACTOR_LEG_TOP of its height, left or
// right of the middle; the middle line itself (the front tip of the hooded figure's robe) stays put.
// Arms: everything further out to the side than ACTOR_ARM_OUT, between the legs and ACTOR_ARM_TOP.
static void SetupActorLimbs(World *world)
{
    const Mesh *mesh = &world->model.meshes[world->actorMesh];
    int count = mesh->vertexCount;
    world->actorRestPositions = malloc(count*3*sizeof(float));
    world->actorRestNormals = malloc(count*3*sizeof(float));
    world->actorLimb = calloc(count, sizeof(signed char));
    memcpy(world->actorRestPositions, mesh->vertices, count*3*sizeof(float));
    if (mesh->normals != NULL) memcpy(world->actorRestNormals, mesh->normals, count*3*sizeof(float));
    else memset(world->actorRestNormals, 0, count*3*sizeof(float));

    BoundingBox box = GetMeshBoundingBox(*mesh);
    float height = box.max.y - box.min.y;

    // It stands between its feet: the middle of everything near the ground
    Vector3 feet = { 0 };
    int feetCount = 0;
    for (int i = 0; i < count; i++)
    {
        Vector3 p = { mesh->vertices[i*3], mesh->vertices[i*3 + 1], mesh->vertices[i*3 + 2] };
        if (p.y < box.min.y + height*0.05f) { feet = Vector3Add(feet, p); feetCount++; }
    }
    if (feetCount == 0) feet = (Vector3){ (box.min.x + box.max.x)*0.5f, 0.0f, (box.min.z + box.max.z)*0.5f };
    else feet = Vector3Scale(feet, 1.0f/feetCount);
    world->actorOrigin = (Vector3){ feet.x, box.min.y, feet.z };

    // Joints: hips at the top middle of each leg, shoulders at the top of the part of each arm nearest the body
    float legTop = box.min.y + height*ACTOR_LEG_TOP;
    float armTop = box.min.y + height*ACTOR_ARM_TOP;
    Vector3 jointSum[ACTOR_LIMBS] = { 0 };
    int jointCount[ACTOR_LIMBS] = { 0 };
    float shoulderY[2] = { legTop, legTop };
    for (int i = 0; i < count; i++)
    {
        Vector3 p = { mesh->vertices[i*3], mesh->vertices[i*3 + 1], mesh->vertices[i*3 + 2] };
        float side = p.x - feet.x;
        int limb = -1;
        if ((p.y < legTop) && (fabsf(side) >= 0.05f)) limb = (side > 0.0f)? ACTOR_LEG_LEFT : ACTOR_LEG_RIGHT;
        else if ((p.y < armTop) && (fabsf(side) > height*ACTOR_ARM_OUT)) limb = (side > 0.0f)? ACTOR_ARM_LEFT : ACTOR_ARM_RIGHT;
        if (limb < 0) continue;
        world->actorLimb[i] = (signed char)(limb + 1);

        bool nearJoint = (limb <= ACTOR_LEG_RIGHT)? (p.y > legTop - height*0.08f) : (fabsf(side) < height*(ACTOR_ARM_OUT + 0.06f));
        if (nearJoint)
        {
            jointSum[limb] = Vector3Add(jointSum[limb], p);
            jointCount[limb]++;
            if (limb >= ACTOR_ARM_LEFT) shoulderY[limb - ACTOR_ARM_LEFT] = fmaxf(shoulderY[limb - ACTOR_ARM_LEFT], p.y);
        }
    }
    for (int limb = 0; limb < ACTOR_LIMBS; limb++)
    {
        Vector3 joint = (jointCount[limb] > 0)? Vector3Scale(jointSum[limb], 1.0f/jointCount[limb]) : feet;
        joint.y = (limb <= ACTOR_LEG_RIGHT)? legTop : shoulderY[limb - ACTOR_ARM_LEFT];
        world->actorJoint[limb] = joint;
    }
}

bool WorldLoad(World *world, const char *path, const char *skyPath, const char *actorMaterialName)
{
    memset(world, 0, sizeof(*world));

    world->model = LoadModel(path);
    if (world->model.meshCount == 0) return false;

    world->shader = LoadShaderFromMemory(LIT_VS, LIT_FS);
    Vector3 lightDir = Vector3Normalize(LIGHT_DIR);
    SetShaderValue(world->shader, GetShaderLocation(world->shader, "lightDir"), &lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(world->shader, GetShaderLocation(world->shader, "lightColor"), &LIGHT_COLOR, SHADER_UNIFORM_VEC3);
    SetShaderValue(world->shader, GetShaderLocation(world->shader, "ambientColor"), &AMBIENT_COLOR, SHADER_UNIFORM_VEC3);

    // Sky (optional: without the image the clear color shows instead)
    world->skyTexture = LoadTexture(skyPath);
    if (world->skyTexture.id > 0)
    {
        SetTextureFilter(world->skyTexture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(world->skyTexture, TEXTURE_WRAP_CLAMP);
        world->skyMesh = GenMeshCube(10.0f, 10.0f, 10.0f);
        world->skyMaterial = LoadMaterialDefault();
        world->skyMaterial.shader = LoadShaderFromMemory(SKY_VS, SKY_FS);
        world->skyMaterial.maps[MATERIAL_MAP_ALBEDO].texture = world->skyTexture;
        float skyBottom = SKY_BOTTOM;
        SetShaderValue(world->skyMaterial.shader, GetShaderLocation(world->skyMaterial.shader, "skyBottom"), &skyBottom, SHADER_UNIFORM_FLOAT);
    }

    Model *model = &world->model;
    for (int i = 0; i < model->materialCount; i++)
    {
        model->materials[i].shader = world->shader;

        // Mipmaps keep distant grass and cliffs from shimmering
        Texture2D *albedo = &model->materials[i].maps[MATERIAL_MAP_ALBEDO].texture;
        if ((albedo->id > 0) && (albedo->id != rlGetTextureIdDefault()))
        {
            GenTextureMipmaps(albedo);
            SetTextureFilter(*albedo, TEXTURE_FILTER_TRILINEAR);
            SetTextureFilter(*albedo, TEXTURE_FILTER_ANISOTROPIC_8X);
        }
    }

    bool *alphaMaterial = calloc(model->materialCount, sizeof(bool));
    bool *boxMaterial = calloc(model->materialCount, sizeof(bool));
    int actorMaterial = -1;
    FindMaterials(path, alphaMaterial, boxMaterial, model->materialCount, actorMaterialName, &actorMaterial);

    // Lift the actor out of the level (the first mesh using its material)
    world->isActor = calloc(model->meshCount, sizeof(bool));
    world->actorMesh = -1;
    for (int m = 0; m < model->meshCount; m++)
    {
        if ((model->meshMaterial[m] == actorMaterial) && (model->meshes[m].vertices != NULL))
        {
            world->actorMesh = m;
            world->isActor[m] = true;
            world->hasActor = true;
            break;
        }
    }
    if (world->hasActor) SetupActorLimbs(world);

    BuildCollisionGrid(world, alphaMaterial, boxMaterial);
    free(alphaMaterial);
    free(boxMaterial);

    return true;
}

void WorldUnload(World *world)
{
    Model *model = &world->model;
    for (int i = 0; i < model->materialCount; i++)
    {
        for (int m = 0; m <= MATERIAL_MAP_BRDF; m++)
        {
            Texture2D texture = model->materials[i].maps[m].texture;
            if ((texture.id > 0) && (texture.id != rlGetTextureIdDefault())) UnloadTexture(texture);
        }
    }
    UnloadModel(world->model);
    UnloadShader(world->shader);
    if (world->skyTexture.id > 0)
    {
        UnloadMesh(world->skyMesh);
        UnloadMaterial(world->skyMaterial);     // Also unloads the sky shader and texture
    }
    free(world->isActor);
    free(world->actorRestPositions);
    free(world->actorRestNormals);
    free(world->actorLimb);
    free(world->triangles);
    free(world->cellStart);
    free(world->cellItems);
    memset(world, 0, sizeof(*world));
}

void WorldDraw(const World *world, Camera3D camera)
{
    if (world->skyTexture.id > 0)
    {
        // Follows the camera's position but not its rotation, and writes no depth so everything draws over it
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        DrawMesh(world->skyMesh, world->skyMaterial, MatrixTranslate(camera.position.x, camera.position.y, camera.position.z));
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
    }

    // Leaves and tree cards are single quads, so draw both sides of everything
    rlDisableBackfaceCulling();
    const Model *model = &world->model;
    for (int m = 0; m < model->meshCount; m++)
    {
        if (!world->isActor[m]) DrawMesh(model->meshes[m], model->materials[model->meshMaterial[m]], model->transform);
    }
    rlEnableBackfaceCulling();
}

void WorldDrawActor(const World *world, Vector3 position, float yaw, float lean, float roll, const float limbSwing[ACTOR_LIMBS])
{
    if (!world->hasActor) return;

    // Swing each limb around its joint; it faces +Z, so forward is a negative turn around X
    Mesh *mesh = &world->model.meshes[world->actorMesh];
    Matrix limbs[ACTOR_LIMBS];
    for (int limb = 0; limb < ACTOR_LIMBS; limb++)
    {
        Vector3 j = world->actorJoint[limb];
        limbs[limb] = MatrixMultiply(MatrixMultiply(MatrixTranslate(-j.x, -j.y, -j.z), MatrixRotateX(-limbSwing[limb])), MatrixTranslate(j.x, j.y, j.z));
    }
    for (int i = 0; i < mesh->vertexCount; i++)
    {
        int limb = world->actorLimb[i] - 1;
        if (limb < 0) continue;
        Vector3 p = Vector3Transform((Vector3){ world->actorRestPositions[i*3], world->actorRestPositions[i*3 + 1], world->actorRestPositions[i*3 + 2] }, limbs[limb]);
        mesh->vertices[i*3] = p.x;
        mesh->vertices[i*3 + 1] = p.y;
        mesh->vertices[i*3 + 2] = p.z;
        if (mesh->normals != NULL)
        {
            Matrix r = limbs[limb];
            Vector3 n = { world->actorRestNormals[i*3], world->actorRestNormals[i*3 + 1], world->actorRestNormals[i*3 + 2] };
            mesh->normals[i*3] = r.m0*n.x + r.m4*n.y + r.m8*n.z;
            mesh->normals[i*3 + 1] = r.m1*n.x + r.m5*n.y + r.m9*n.z;
            mesh->normals[i*3 + 2] = r.m2*n.x + r.m6*n.y + r.m10*n.z;
        }
    }
    UpdateMeshBuffer(*mesh, 0, mesh->vertices, mesh->vertexCount*3*sizeof(float), 0);
    if (mesh->normals != NULL) UpdateMeshBuffer(*mesh, 2, mesh->normals, mesh->vertexCount*3*sizeof(float), 0);

    Vector3 o = world->actorOrigin;
    Matrix transform = MatrixMultiply(MatrixTranslate(-o.x, -o.y, -o.z), MatrixMultiply(MatrixRotateX(lean), MatrixRotateZ(roll)));
    transform = MatrixMultiply(transform, MatrixRotateY(yaw));
    transform = MatrixMultiply(transform, MatrixTranslate(position.x, position.y, position.z));

    rlDisableBackfaceCulling();
    const Model *model = &world->model;
    for (int m = 0; m < model->meshCount; m++)
    {
        if (world->isActor[m]) DrawMesh(model->meshes[m], model->materials[model->meshMaterial[m]], transform);
    }
    rlEnableBackfaceCulling();
}

RayCollision WorldRaycast(const World *world, Ray ray, float maxDistance)
{
    RayCollision closest = { 0 };
    closest.distance = maxDistance;
    if (world->triangleCount == 0) return closest;

    // Only the cells under the ray's footprint; a triangle spanning several cells may get tested twice, which is harmless
    Vector3 end = Vector3Add(ray.position, Vector3Scale(ray.direction, maxDistance));
    int x0, z0, x1, z1;
    CellRange(world, fminf(ray.position.x, end.x), fminf(ray.position.z, end.z), fmaxf(ray.position.x, end.x), fmaxf(ray.position.z, end.z), &x0, &z0, &x1, &z1);

    for (int z = z0; z <= z1; z++)
    {
        for (int x = x0; x <= x1; x++)
        {
            int cell = z*world->gridWidth + x;
            for (int i = world->cellStart[cell]; i < world->cellStart[cell + 1]; i++)
            {
                const Vector3 *tri = &world->triangles[world->cellItems[i]*3];
                RayCollision hit = GetRayCollisionTriangle(ray, tri[0], tri[1], tri[2]);
                if (hit.hit && (hit.distance < closest.distance)) closest = hit;
            }
        }
    }

    return closest;
}

bool WorldGroundHeight(const World *world, float x, float z, float fromY, float *outY)
{
    Ray ray = { { x, fromY, z }, { 0.0f, -1.0f, 0.0f } };
    RayCollision hit = WorldRaycast(world, ray, 1000.0f);
    if (!hit.hit) return false;

    *outY = hit.point.y;
    return true;
}
