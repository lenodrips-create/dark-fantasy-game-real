#include "world.h"

#include "raymath.h"
#include "rlgl.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GRID_CELL_SIZE 8.0f     // Collision grid cell size in world units
#define GRID_MAX_CELLS 512      // Per axis; cells get bigger on huge maps instead

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
static void ScanAlphaMaterials(const char *json, int length, bool *alpha, int materialCount)
{
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

            if (expectAlphaMode)
            {
                bool opaque = (len == 6) && (memcmp(s, "OPAQUE", 6) == 0);
                if (!opaque && (material + 1 < materialCount)) alpha[material + 1] = true;
                expectAlphaMode = false;
            }
            else if ((depth == 1) && (len == 9) && (memcmp(s, "materials", 9) == 0)) expectMaterials = true;
            else if ((materialsDepth >= 0) && (depth == materialsDepth + 1) && (len == 9) && (memcmp(s, "alphaMode", 9) == 0)) expectAlphaMode = true;
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

static void FindAlphaMaterials(const char *path, bool *alpha, int materialCount)
{
    int size = 0;
    unsigned char *file = LoadFileData(path, &size);
    if (file == NULL) return;

    if ((size > 20) && (memcmp(file, "glTF", 4) == 0))
    {
        // Binary glTF: 12 byte header, then the JSON chunk (length, type, data)
        unsigned int jsonLength = file[12] | (file[13] << 8) | (file[14] << 16) | ((unsigned int)file[15] << 24);
        if (jsonLength <= (unsigned int)size - 20) ScanAlphaMaterials((const char *)file + 20, (int)jsonLength, alpha, materialCount);
    }
    else ScanAlphaMaterials((const char *)file, size, alpha, materialCount);

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
// XZ plane, so a ray only tests the triangles near it no matter how big the map gets
static void BuildCollisionGrid(World *world, const bool *alphaMaterial)
{
    const Model *model = &world->model;

    int capacity = 0;
    for (int m = 0; m < model->meshCount; m++)
    {
        if (!alphaMaterial[model->meshMaterial[m]]) capacity += model->meshes[m].triangleCount;
    }
    world->triangles = malloc((capacity > 0? capacity : 1)*3*sizeof(Vector3));

    Vector2 min = { FLT_MAX, FLT_MAX };
    Vector2 max = { -FLT_MAX, -FLT_MAX };
    int count = 0;
    for (int m = 0; m < model->meshCount; m++)
    {
        const Mesh *mesh = &model->meshes[m];
        if (alphaMaterial[model->meshMaterial[m]] || (mesh->vertices == NULL)) continue;

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

bool WorldLoad(World *world, const char *path, const char *skyPath)
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
    FindAlphaMaterials(path, alphaMaterial, model->materialCount);

    BuildCollisionGrid(world, alphaMaterial);
    free(alphaMaterial);

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
    DrawModel(world->model, (Vector3){ 0 }, 1.0f, WHITE);
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
