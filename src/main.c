#include "main.h"
#include "Mobs/mob.h"
#include "Chunk Generation/chunkGenerator.h"
#include "Chunk Generation/chunkManager.h"
#include "Fog/fog.h"
#include "mesher.h"
#include "Lighting/lighting.h"
#include "Player/player.h"
#include "Player/camera.h"
#include "Sky/sky.h"
#include "UI/menus.h"
#include "Physics/physics.h"
#include "Fluid/fluid.h"

#define MAX_BLOCK_TEXTURES MESH_MAX_TEXTURES

int snd_break[BLOCK_TYPES][BLOCK_SOUND_VARIANTS];
int snd_place[BLOCK_TYPES][BLOCK_SOUND_VARIANTS];
int snd_walk[BLOCK_TYPES][BLOCK_SOUND_VARIANTS];
int snd_break_n[BLOCK_TYPES];
int snd_place_n[BLOCK_TYPES];
int snd_walk_n[BLOCK_TYPES];
#define TICK_ONLY_MOVEMENT 0
#define STB_IMAGE_IMPLEMENTATION
#include "Third Party/stb_image.h"
static int fb_w = 1280;
static int fb_h = 720;

static UiText fps_text;
static UiText item_text;

#define ITEM_NAME_TIME     2.0f
#define ITEM_NAME_FADE_OUT 0.5f
#define ITEM_NAME_FADE_IN  0.1f

#define ITEM_NAME_TIME     2.0f
#define ITEM_NAME_FADE_OUT 0.5f
#define ITEM_NAME_FADE_IN  0.1f

#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:mainCRTStartup")

static char* load_shader_source(const char* path)
{
    size_t size = 0;
    char* source = (char*)asset_read(path, &size);

    if (!source)
        fprintf(stderr, "Failed to open shader: %s\n", path);

    return source;
}

static GLuint make_program(
    const char* label,
    const char* vertex_path,
    const char* fragment_path)
{
    char* vs = load_shader_source(vertex_path);
    char* fs = load_shader_source(fragment_path);

    if (!vs || !fs) {
        free(vs);
        free(fs);
        return 0;
    }

    GLuint program = 0;
    GLuint shaders[2] = { 0, 0 };

    const char* sources[2] = { vs, fs };

    GLenum types[2] = {
        GL_VERTEX_SHADER,
        GL_FRAGMENT_SHADER
    };

    for (int i = 0; i < 2; i++) {
        shaders[i] = glCreateShader(types[i]);

        glShaderSource(shaders[i], 1, &sources[i], NULL);
        glCompileShader(shaders[i]);

        GLint ok;
        glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &ok);

        if (!ok) {
            char log[1024]; 

            glGetShaderInfoLog(
                shaders[i], sizeof log, NULL, log
            );

            fprintf(stderr,
                "Shader compilation failed (%s %s shader):\n%s\n",
                label,
                i == 0 ? "vertex" : "fragment",
                log);

            glDeleteShader(shaders[i]);
            shaders[i] = 0;
            goto cleanup;  // yes a goto, sorry
        }
    }

    program = glCreateProgram();

    glAttachShader(program, shaders[0]);
    glAttachShader(program, shaders[1]);
    glLinkProgram(program);

    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (!ok) {
        char log[1024];

        glGetProgramInfoLog(program, sizeof log, NULL, log);

        fprintf(stderr, "Shader linking failed (%s):\n%s\n",
            label, log);

        glDeleteProgram(program);
        program = 0;
    }

cleanup:
    for (int i = 0; i < 2; i++) {
        if (shaders[i])
            glDeleteShader(shaders[i]);
    }

    free(vs);
    free(fs);

    return program;
}

static GLuint load_texture(const char* path)
{
    int w, h, channels;

    stbi_set_flip_vertically_on_load(1);

    unsigned char* data = asset_image(path, &w, &h, &channels);

    static const unsigned char white[4] = { 255, 255, 255, 255 };

    if (!data) {
        fprintf(stderr, "Failed to load texture '%s': %s\n", path, stbi_failure_reason());
        w = 1;
        h = 1;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data ? data : white);
    glGenerateMipmap(GL_TEXTURE_2D);

    if (data)
        stbi_image_free(data);

    glBindTexture(GL_TEXTURE_2D, 0);

    return tex;
}

#define MAX_BLOCK_TEXTURES 64

static const char* tex_path[MAX_BLOCK_TEXTURES];
static GLuint      tex_id[MAX_BLOCK_TEXTURES];
static int         tex_count = 0;

static int block_face_tex[BLOCK_TYPES][BLOCK_FACE_COUNT];

static int texture_index(const char* path)
{
    if (!path)
        return -1;

    for (int i = 0; i < tex_count; i++)
        if (strcmp(tex_path[i], path) == 0)
            return i;

    if (tex_count >= MAX_BLOCK_TEXTURES) {
        fprintf(stderr, "Too many block textures (max %d), can't load '%s'\n",
            MAX_BLOCK_TEXTURES, path);
        return -1;
    }

    tex_path[tex_count] = path;
    tex_id[tex_count] = load_texture(path);

    return tex_count++;
}

static void load_block_textures(void)
{
    for (int type = 0; type < BLOCK_TYPES; type++)
        for (int face = 0; face < BLOCK_FACE_COUNT; face++)
            block_face_tex[type][face] = texture_index(BLOCK_DEFINITIONS[type].texture[face]);
}

static GLuint block_texture(int type, int face)
{
    if (type < 0 || type >= BLOCK_TYPES || face < 0 || face >= BLOCK_FACE_COUNT)
        return 0;

    int i = block_face_tex[type][face];

    return i < 0 ? 0 : tex_id[i];
}

static void make_block_outline(int x, int y, int z, float* out)
{
    const float e = 0.003f;
    const float x0 = (float)x - e, x1 = (float)x + 1.0f + e;
    const float y0 = (float)y - e, y1 = (float)y + 1.0f + e;
    const float z0 = (float)z - e, z1 = (float)z + 1.0f + e;
    const float corners[8][3] = {
        {x0,y0,z0}, {x1,y0,z0}, {x1,y1,z0}, {x0,y1,z0},
        {x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}
    };
    const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    int n = 0;
    for (int i = 0; i < 12; i++) {
        for (int j = 0; j < 2; j++) {
            const float* p = corners[edges[i][j]];
            out[n++] = p[0]; out[n++] = p[1]; out[n++] = p[2];
        }
    }
}

#define MAX_PARTICLES          2048
#define PARTICLE_HALF_BOX      0.1
#define PARTICLE_GRAVITY       0.04
#define PARTICLE_DRAG          0.98
#define PARTICLE_GROUND_DRAG   0.7
#define PARTICLE_TINT          0.6f

typedef struct {
    double x, y, z;
    double px, py, pz;
    double vx, vy, vz;
    int    age, life;
    int    on_ground;
    int    alive;
    float  scale;
    float  u0, v0, u1, v1;
    GLuint texture;
} BlockParticle;

static BlockParticle particles[MAX_PARTICLES];
static int particle_cursor = 0;

static GLuint particle_vao = 0;
static GLuint particle_vbo = 0;

static float  particle_vertices[MAX_PARTICLES * 6 * 6];
static GLuint particle_tex[MAX_PARTICLES];

static float random_float(float min, float max)
{
    return min + (max - min) *
        ((float)rand() / (float)RAND_MAX);
}

static float entity_light_at(double x, double y, double z)
{
    float l = lighting_brightness_at(x, y, z);
    float p = lighting_player_brightness_at(x, y, z);
    if (p > l) l = p;
    return l > 1.0f ? 1.0f : l;
}

static void spawn_block_particles(int bx, int by, int bz, int type)
{
    if (type <= BLOCK_AIR || type >= BLOCK_TYPES)
        return;

    GLuint tex = block_texture(type, BLOCK_FACE_FRONT);
    if (tex == 0)
        return;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                BlockParticle* p = &particles[particle_cursor];
                particle_cursor = (particle_cursor + 1) % MAX_PARTICLES;

                memset(p, 0, sizeof * p);

                double ox = (i + 0.5) / 4.0;
                double oy = (j + 0.5) / 4.0;
                double oz = (k + 0.5) / 4.0;

                p->x = bx + ox;
                p->y = by + oy;
                p->z = bz + oz;

                p->px = p->x;
                p->py = p->y;
                p->pz = p->z;

                double mx = (ox - 0.5) + random_float(-1.0f, 1.0f) * 0.4;
                double my = (oy - 0.5) + random_float(-1.0f, 1.0f) * 0.4;
                double mz = (oz - 0.5) + random_float(-1.0f, 1.0f) * 0.4;

                double speed = (random_float(0.0f, 1.0f) +
                    random_float(0.0f, 1.0f) + 1.0) * 0.15;

                double len = sqrt(mx * mx + my * my + mz * mz);
                if (len < 1.0e-6)
                    len = 1.0e-6;

                p->vx = mx / len * speed * 0.4;
                p->vy = my / len * speed * 0.4 + 0.1;
                p->vz = mz / len * speed * 0.4;

                p->age = 0;
                p->life = (int)(4.0f / (random_float(0.0f, 1.0f) * 0.9f + 0.1f));
                p->scale = random_float(0.0f, 1.0f) * 0.5f + 0.5f;

                p->u0 = random_float(0.0f, 3.0f) / 4.0f;
                p->v0 = random_float(0.0f, 3.0f) / 4.0f;
                p->u1 = p->u0 + 0.25f;
                p->v1 = p->v0 + 0.25f;

                p->texture = tex;
                p->alive = 1;
            }
        }
    }
}

static void particle_move(BlockParticle* p, double dx, double dy, double dz)
{
    AABB b;

    b.lo[0] = p->x - PARTICLE_HALF_BOX;  b.hi[0] = p->x + PARTICLE_HALF_BOX;
    b.lo[1] = p->y - PARTICLE_HALF_BOX;  b.hi[1] = p->y + PARTICLE_HALF_BOX;
    b.lo[2] = p->z - PARTICLE_HALF_BOX;  b.hi[2] = p->z + PARTICLE_HALF_BOX;

    double odx = dx, ody = dy, odz = dz;

    dy = clip_axis(&b, 1, dy);
    b.lo[1] += dy; b.hi[1] += dy;

    dx = clip_axis(&b, 0, dx);
    b.lo[0] += dx; b.hi[0] += dx;

    dz = clip_axis(&b, 2, dz);

    p->x += dx;
    p->y += dy;
    p->z += dz;

    p->on_ground = (ody != dy) && ody < 0.0;

    if (odx != dx) p->vx = 0.0;
    if (ody != dy) p->vy = 0.0;
    if (odz != dz) p->vz = 0.0;
}

static void tick_block_particles(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        BlockParticle* p = &particles[i];

        if (!p->alive)
            continue;

        p->px = p->x;
        p->py = p->y;
        p->pz = p->z;

        if (p->age++ >= p->life || p->y < -20.0) {
            p->alive = 0;
            continue;
        }

        p->vy -= PARTICLE_GRAVITY;

        particle_move(p, p->vx, p->vy, p->vz);

        p->vx *= PARTICLE_DRAG;
        p->vy *= PARTICLE_DRAG;
        p->vz *= PARTICLE_DRAG;

        if (p->on_ground) {
            p->vx *= PARTICLE_GROUND_DRAG;
            p->vz *= PARTICLE_GROUND_DRAG;
        }
    }
}

static void init_block_particles(void)
{
    glGenVertexArrays(1, &particle_vao);
    glGenBuffers(1, &particle_vbo);

    glBindVertexArray(particle_vao);
    glBindBuffer(GL_ARRAY_BUFFER, particle_vbo);

    glBufferData(
        GL_ARRAY_BUFFER,
        MAX_PARTICLES * 6 * 6 * sizeof(float),
        NULL,
        GL_DYNAMIC_DRAW
    );

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void draw_block_particles(
    const float* vp,
    const float* dir,
    GLuint program,
    GLint vp_location,
    double alpha)
{
    float right[3] = { -dir[2], 0.0f, dir[0] };

    float len = sqrtf(right[0] * right[0] + right[2] * right[2]);

    if (len < 1.0e-6f)
        return;

    right[0] /= len;
    right[2] /= len;

    float up[3] = {
        -dir[1] * right[2],
        -dir[2] * right[0] + dir[0] * right[2],
        dir[1] * right[0]
    };

    const int indices[6] = { 0, 1, 2, 0, 2, 3 };

    int count = 0;

    for (int i = 0; i < MAX_PARTICLES; i++) {
        BlockParticle* p = &particles[i];

        if (!p->alive)
            continue;

        float cx = (float)(p->px + (p->x - p->px) * alpha);
        float cy = (float)(p->py + (p->y - p->py) * alpha);
        float cz = (float)(p->pz + (p->z - p->pz) * alpha);

        float s = 0.1f * p->scale;
        float shade = PARTICLE_TINT * entity_light_at(cx, cy, cz);

        float rx = right[0] * s, ry = right[1] * s, rz = right[2] * s;
        float ux = up[0] * s, uy = up[1] * s, uz = up[2] * s;

        float corners[4][3] = {
            { cx - rx - ux, cy - ry - uy, cz - rz - uz },
            { cx + rx - ux, cy + ry - uy, cz + rz - uz },
            { cx + rx + ux, cy + ry + uy, cz + rz + uz },
            { cx - rx + ux, cy - ry + uy, cz - rz + uz }
        };

        float uv[4][2] = {
            { p->u0, p->v0 },
            { p->u1, p->v0 },
            { p->u1, p->v1 },
            { p->u0, p->v1 }
        };

        for (int j = 0; j < 6; j++) {
            int c = indices[j];
            int n = (count * 6 + j) * 6;

            particle_vertices[n + 0] = corners[c][0];
            particle_vertices[n + 1] = corners[c][1];
            particle_vertices[n + 2] = corners[c][2];
            particle_vertices[n + 3] = uv[c][0];
            particle_vertices[n + 4] = uv[c][1];
            particle_vertices[n + 5] = shade;
        }

        particle_tex[count++] = p->texture;
    }

    if (count == 0)
        return;

    glUseProgram(program);
    glUniformMatrix4fv(vp_location, 1, GL_FALSE, vp);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(particle_vao);
    glBindBuffer(GL_ARRAY_BUFFER, particle_vbo);

    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        (GLsizeiptr)(count * 6 * 6 * sizeof(float)),
        particle_vertices
    );

    int start = 0;

    for (int j = 1; j <= count; j++) {
        if (j == count || particle_tex[j] != particle_tex[start]) {
            glBindTexture(GL_TEXTURE_2D, particle_tex[start]);
            glDrawArrays(GL_TRIANGLES, start * 6, (j - start) * 6);
            start = j;
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

#define STEP_GAIN   0.2f

static int file_exists(const char* path)
{
    return audio_exists(path);
}

static int load_variants(int* out, const char* const* paths,
    const char* const* fallback, float gain)
{
    int n = 0;

    for (int v = 0; v < BLOCK_SOUND_VARIANTS; v++) {
        if (paths[v] && file_exists(paths[v])) {
            int id = audio_load(paths[v], gain);
            if (id >= 0) out[n++] = id;
        }
    }

    if (n == 0 && fallback) {
        for (int v = 0; v < BLOCK_SOUND_VARIANTS; v++) {
            if (fallback[v] && file_exists(fallback[v])) {
                int id = audio_load(fallback[v], gain);
                if (id >= 0) out[n++] = id;
            }
        }
    }

    return n;
}

static void load_block_sounds(void)
{
    const BlockDefinition* stone = &BLOCK_DEFINITIONS[BLOCK_STONE];

    for (int type = 1; type < BLOCK_TYPES; type++) {
        const BlockDefinition* d = &BLOCK_DEFINITIONS[type];

        snd_break_n[type] = load_variants(
            snd_break[type], d->break_sounds, stone->break_sounds, 1.0f);

        snd_place_n[type] = load_variants(
            snd_place[type], d->place_sounds, stone->place_sounds, 1.0f);

        snd_walk_n[type] = load_variants(
            snd_walk[type], d->walk_sounds, stone->walk_sounds, STEP_GAIN);

        printf(
            "Block %d (%s): walk=%d break=%d place=%d\n",
            type,
            d->display_name,
            snd_walk_n[type],
            snd_break_n[type],
            snd_place_n[type]
        );
    }
}

#define REACH 5.0f

#define BREAK_REPEAT_TICKS 5
#define PLACE_REPEAT_TICKS 4

#define VSYNC 0

static int break_requested = 0;
static int place_requested = 0;

static int wireframe_mode = 0;
static int entity_debug = 0;

static int break_cooldown = 0;
static int place_cooldown = 0;

#define HOTBAR_SLOTS UI_HOTBAR_SLOTS

#define SHOW_ITEM_NAME 0

static int hotbar_blocks[HOTBAR_SLOTS] = {
    BLOCK_STONE, BLOCK_GRASS, BLOCK_SAND, BLOCK_DIRT, BLOCK_PLANKS,
    BLOCK_LOG, BLOCK_LEAVES, BLOCK_GLASS, ITEM_DIAMOND_SWORD
};

static GLuint sword_tex = 0;

static GLuint item_sprite(int id)
{
    return id == ITEM_DIAMOND_SWORD ? sword_tex : 0;
}

static void menu_item_info(int id, UiItem* out)
{
    out->top = block_texture(id, BLOCK_FACE_TOP);
    out->side = block_texture(id, BLOCK_FACE_RIGHT);
    out->flat = item_sprite(id);
}

static int hotbar_sel = 0;

static int held_block(void)
{
    return hotbar_blocks[hotbar_sel];
}

static void select_slot(int slot)
{
    if (slot < 0 || slot >= HOTBAR_SLOTS)
        return;

    hotbar_sel = slot;
    hand_select(held_block());
}

#define STACK_MAX 64

static int surv_id[HOTBAR_SLOTS];
static int surv_cnt[HOTBAR_SLOTS];
static int creative_hotbar[HOTBAR_SLOTS];
static int mode_inited = 0;

static int   mine_active = 0;
static int   mine_pos[3];
static int   mine_type = 0;
static int   mine_held = -1;
static float mine_progress = 0.0f;
static float mine_sound_ticks = 0.0f;

static void sync_survival_hotbar(void)
{
    for (int i = 0; i < HOTBAR_SLOTS; i++)
        hotbar_blocks[i] = surv_cnt[i] > 0 ? surv_id[i] : BLOCK_AIR;
}

static int survival_take(int id, int count)
{
    if (id <= BLOCK_AIR || count <= 0)
        return 0;

    int left = count;

    for (int i = 0; i < HOTBAR_SLOTS && left > 0; i++) {
        if (surv_cnt[i] > 0 && surv_id[i] == id && surv_cnt[i] < STACK_MAX) {
            int n = STACK_MAX - surv_cnt[i];
            if (n > left) n = left;
            surv_cnt[i] += n;
            left -= n;
        }
    }

    for (int i = 0; i < HOTBAR_SLOTS && left > 0; i++) {
        if (surv_cnt[i] <= 0) {
            int n = left < STACK_MAX ? left : STACK_MAX;
            surv_id[i] = id;
            surv_cnt[i] = n;
            left -= n;
        }
    }

    if (left != count) {
        sync_survival_hotbar();
        select_slot(hotbar_sel);
    }

    return count - left;
}

static void survival_consume_held(void)
{
    if (surv_cnt[hotbar_sel] > 0 && --surv_cnt[hotbar_sel] <= 0) {
        surv_cnt[hotbar_sel] = 0;
        surv_id[hotbar_sel] = BLOCK_AIR;
    }

    sync_survival_hotbar();
    select_slot(hotbar_sel);
}

static void survival_drop_all(void)
{
    for (int i = 0; i < HOTBAR_SLOTS; i++) {
        if (surv_cnt[i] > 0)
            physics_spawn_item(surv_id[i], surv_cnt[i], player.x, player.y + 1.3, player.z,
                random_float(-1.0f, 1.0f) * 0.3, random_float(0.0f, 1.0f) * 0.2 + 0.1,
                random_float(-1.0f, 1.0f) * 0.3, 40);

        surv_cnt[i] = 0;
        surv_id[i] = BLOCK_AIR;
    }

    sync_survival_hotbar();
    select_slot(hotbar_sel);
}

static float cur_reach(void)
{
    return game_mode == MODE_CREATIVE ? 5.0f : 4.0f;
}

static float block_hardness(int id)
{
    switch (id) {
    case BLOCK_STONE:   return 1.5f;
    case BLOCK_GRASS:   return 0.6f;
    case BLOCK_DIRT:    return 0.5f;
    case BLOCK_SAND:    return 0.5f;
    case BLOCK_PLANKS:  return 2.0f;
    case BLOCK_LOG:     return 2.0f;
    case BLOCK_LEAVES:  return 0.2f;
    case BLOCK_GLASS:   return 0.3f;
    case BLOCK_BRICK:   return 2.0f;
    case BLOCK_SNOW:    return 0.2f;
    default:            return -1.0f;
    }
}

static int block_needs_tool(int id)
{
    return id == BLOCK_STONE || id == BLOCK_BRICK || id == BLOCK_SNOW;
}

static int block_drop(int id)
{
    switch (id) {
    case BLOCK_GRASS:  return BLOCK_DIRT;
    case BLOCK_GLASS:  return BLOCK_AIR;
    case BLOCK_LEAVES: return BLOCK_AIR;
    default:           return id;
    }
}

static void set_game_mode(GameMode m)
{
    if (mode_inited && m == game_mode)
        return;

    if (!mode_inited)
        memcpy(creative_hotbar, hotbar_blocks, sizeof creative_hotbar);
    else if (game_mode == MODE_CREATIVE)
        memcpy(creative_hotbar, hotbar_blocks, sizeof creative_hotbar);

    mode_inited = 1;
    game_mode = m;

    mine_active = 0;
    mine_progress = 0.0f;
    player.flying = 0;
    player.fall_distance = 0.0f;

    if (m == MODE_CREATIVE) {
        memcpy(hotbar_blocks, creative_hotbar, sizeof creative_hotbar);
        player.health = 20.0f;
        player.food = 20;
        player.saturation = 5.0f;
        player.exhaustion = 0.0f;
    }
    else {
        sync_survival_hotbar();
    }

    ui_set_survival_hud(m == MODE_SURVIVAL);
    menus_set_inventory_enabled(m == MODE_CREATIVE);
    select_slot(hotbar_sel);
}

static GLuint crack_tex = 0, crack_prog = 0, crack_vao = 0, crack_vbo = 0;
static GLint  crack_vp_loc = -1;

static GLuint compile_inline(const char* vs_src, const char* fs_src)
{
    const char* src[2] = { vs_src, fs_src };
    GLenum type[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    GLuint sh[2];

    for (int i = 0; i < 2; i++) {
        sh[i] = glCreateShader(type[i]);
        glShaderSource(sh[i], 1, &src[i], NULL);
        glCompileShader(sh[i]);

        GLint ok = 0;
        glGetShaderiv(sh[i], GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(sh[i], sizeof log, NULL, log);
            fprintf(stderr, "crack shader: %s\n", log);
        }
    }

    GLuint p = glCreateProgram();
    glAttachShader(p, sh[0]);
    glAttachShader(p, sh[1]);
    glLinkProgram(p);
    glDeleteShader(sh[0]);
    glDeleteShader(sh[1]);
    return p;
}

static void crack_init(void)
{
    static const char* vs =
        "#version 330 core\n"
        "layout(location=0) in vec3 p;\n"
        "layout(location=1) in vec2 uv;\n"
        "uniform mat4 vp;\n"
        "out vec2 v_uv;\n"
        "void main(){ v_uv = uv; gl_Position = vp * vec4(p, 1.0); }\n";
    static const char* fs =
        "#version 330 core\n"
        "in vec2 v_uv;\n"
        "uniform sampler2D tex;\n"
        "out vec4 color;\n"
        "void main(){ vec4 c = texture(tex, v_uv); if (c.a < 0.01) discard; color = vec4(c.rgb, 1.0); }\n";

    crack_prog = compile_inline(vs, fs);
    crack_vp_loc = glGetUniformLocation(crack_prog, "vp");

    crack_tex = load_texture("assets/textures/Breaking.png");
    glBindTexture(GL_TEXTURE_2D, crack_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenVertexArrays(1, &crack_vao);
    glGenBuffers(1, &crack_vbo);
    glBindVertexArray(crack_vao);
    glBindBuffer(GL_ARRAY_BUFFER, crack_vbo);
    glBufferData(GL_ARRAY_BUFFER, 36 * 5 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void crack_shutdown(void)
{
    if (crack_tex)  glDeleteTextures(1, &crack_tex);
    if (crack_vbo)  glDeleteBuffers(1, &crack_vbo);
    if (crack_vao)  glDeleteVertexArrays(1, &crack_vao);
    if (crack_prog) glDeleteProgram(crack_prog);
    crack_tex = crack_vbo = crack_vao = crack_prog = 0;
}

static void crack_draw(const float* vp, const int* pos, int stage)
{
    if (!crack_prog || !crack_tex)
        return;

    if (stage < 0) stage = 0;
    if (stage > 9) stage = 9;

    const float e = 0.003f;
    const float x0 = pos[0] - e, x1 = pos[0] + 1.0f + e;
    const float y0 = pos[1] - e, y1 = pos[1] + 1.0f + e;
    const float z0 = pos[2] - e, z1 = pos[2] + 1.0f + e;

    const float c[6][4][3] = {
        { {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1} },
        { {x1,y0,z0},{x0,y0,z0},{x0,y1,z0},{x1,y1,z0} },
        { {x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0} },
        { {x1,y0,z1},{x1,y0,z0},{x1,y1,z0},{x1,y1,z1} },
        { {x0,y1,z1},{x1,y1,z1},{x1,y1,z0},{x0,y1,z0} },
        { {x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1} },
    };

    const float u0 = stage / 10.0f, u1 = (stage + 1) / 10.0f;
    const float uv[4][2] = { {u0, 0.0f}, {u1, 0.0f}, {u1, 1.0f}, {u0, 1.0f} };
    const int idx[6] = { 0, 1, 2, 0, 2, 3 };

    float data[36 * 5];
    int n = 0;

    for (int f = 0; f < 6; f++) {
        for (int k = 0; k < 6; k++) {
            const int v = idx[k];
            data[n++] = c[f][v][0];
            data[n++] = c[f][v][1];
            data[n++] = c[f][v][2];
            data[n++] = uv[v][0];
            data[n++] = uv[v][1];
        }
    }

    glUseProgram(crack_prog);
    glUniformMatrix4fv(crack_vp_loc, 1, GL_FALSE, vp);
    glUniform1i(glGetUniformLocation(crack_prog, "tex"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, crack_tex);
    glBindVertexArray(crack_vao);
    glBindBuffer(GL_ARRAY_BUFFER, crack_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof data, data);

    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void on_resize(GLFWwindow* window, int width, int height) {
    (void)window;

    fb_w = width;
    fb_h = height;

    glViewport(0, 0, width, height);
    camera_set_viewport(width, height);
    ui_resize(width, height);
    text_resize(width, height);
    menus_resize(width, height);
}

static void on_click(GLFWwindow* window, int button, int action, int mods)
{
    (void)window;
    (void)mods;

    menus_mouse(button, action);

    if (menus_open())
        return;

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
        break_requested = 1;

    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
        place_requested = 1;
}

static void on_scroll(GLFWwindow* window, double xoffset, double yoffset)
{
    (void)window;
    (void)xoffset;

    if (menus_open()) {
        menus_scroll(yoffset);
        return;
    }

    if (yoffset > 0.0)
        select_slot((hotbar_sel + HOTBAR_SLOTS - 1) % HOTBAR_SLOTS);
    else if (yoffset < 0.0)
        select_slot((hotbar_sel + 1) % HOTBAR_SLOTS);
}

static void on_mouse(GLFWwindow* window, double x, double y)
{
    static float held_yaw = 0.0f, held_pitch = 0.0f;
    static int   was_open = 0;

    int open = menus_open();

    if (open && !was_open) {
        held_yaw = yaw;
        held_pitch = pitch;
    }
    was_open = open;

    camera_on_mouse(window, x, y);

    if (open) {
        yaw = held_yaw;
        pitch = held_pitch;
    }
}

static void on_key(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;

    menus_key(key, action);

    int in_menu = menus_open();

    if (!in_menu && action == GLFW_PRESS && key >= GLFW_KEY_1 && key < GLFW_KEY_1 + HOTBAR_SLOTS)
        select_slot(key - GLFW_KEY_1);

    if (!in_menu && key == GLFW_KEY_F4 && action == GLFW_PRESS)
        wireframe_mode = !wireframe_mode;

    if (!in_menu && key == GLFW_KEY_F5 && action == GLFW_PRESS)
        view_mode = (view_mode + 1) % 3;

    if (!in_menu && key == GLFW_KEY_F6 && action == GLFW_PRESS)
        entity_debug = !entity_debug;

    if (!in_menu && key == GLFW_KEY_F7 && action == GLFW_PRESS)
        fog_toggle();

    if (!in_menu && key == GLFW_KEY_F10 && action == GLFW_PRESS) {
        set_game_mode(game_mode == MODE_SURVIVAL ? MODE_CREATIVE : MODE_SURVIVAL);
        glfwSetWindowTitle(window, game_mode == MODE_CREATIVE ? "Paradox - Creative Mode" : "Paradox - Survival Mode");
    }

    if (!in_menu && key == GLFW_KEY_L && action == GLFW_PRESS)
        lighting_toggle_player_light();

    if (!in_menu && key == GLFW_KEY_G && action == GLFW_PRESS) {
        float r = yaw * PI / 180.0f;
        npc_spawn(player.x + cosf(r) * 3.0f, player.y, player.z + sinf(r) * 3.0f, yaw + 180.0f);
    }

    if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
        static int x = 100;
        static int y = 100;
        static int width = 1280;
        static int height = 720;

        if (glfwGetWindowMonitor(window))
            glfwSetWindowMonitor(window, NULL, x, y, width, height, 0);

        else {
            glfwGetWindowPos(window, &x, &y);
            glfwGetWindowSize(window, &width, &height);

            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode =
                glfwGetVideoMode(monitor);

            if (monitor && mode)
                glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }

        glfwSwapInterval(VSYNC);
    }
}

static void APIENTRY gl_dbg(GLenum s, GLenum t, GLuint id, GLenum sev, GLsizei l, const GLchar* m, const void* u)
{
    fprintf(stderr, "GL: %s\n", m);
    if (t == GL_DEBUG_TYPE_ERROR) __debugbreak();
}
static BodyTurn player_body;

int main(void)
{
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(fb_w, fb_h, "Paradox", NULL, NULL);

    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGL(glfwGetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    glfwSwapInterval(VSYNC);

    glfwSetFramebufferSizeCallback(window, on_resize);
    glfwSetKeyCallback(window, on_key);
    glfwSetCursorPosCallback(window, on_mouse);
    glfwSetMouseButtonCallback(window, on_click);
    glfwSetScrollCallback(window, on_scroll);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    camera_set_viewport(fb_w, fb_h);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glClearColor(0.47f, 0.65f, 1.0f, 1.0f);

    GLuint cube_prog = make_program("cube", "assets/shaders/cube.vert", "assets/shaders/cube.frag");
    GLuint chunk_prog = make_program("chunk", "assets/shaders/chunk.vert", "assets/shaders/chunk.frag");
    if (!cube_prog) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    GLint vp_loc = glGetUniformLocation(cube_prog, "vp");
    GLint tex_loc = glGetUniformLocation(cube_prog, "tex");

    GLuint outline_prog = make_program("outline", "assets/shaders/outline.vert", "assets/shaders/outline.frag");

    GLuint outline_vao = 0, outline_vbo = 0;
    if (!outline_prog) {
        glDeleteProgram(cube_prog);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    srand((unsigned int)time(NULL));
    init_block_particles();

    glGenVertexArrays(1, &outline_vao);
    glGenBuffers(1, &outline_vbo);
    glBindVertexArray(outline_vao);
    glBindBuffer(GL_ARRAY_BUFFER, outline_vbo);
    glBufferData(GL_ARRAY_BUFFER, 24 * 3 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    GLint outline_vp_loc = glGetUniformLocation(outline_prog, "vp");

    if (!ui_init("assets/textures/Reticle.png"))
        fprintf(stderr, "UI disabled (shader failed)\n");
    ui_resize(fb_w, fb_h);

    if (!text_init("assets/fonts/Font1.ttf", 16))
        MessageBoxA(NULL, "text_init failed (FreeType or font)", "Paradox", MB_OK);
    text_resize(fb_w, fb_h);
    text_set(&fps_text, "FPS");

    if (!menus_init(window, "assets/textures/ui/allitems.png"))
        fprintf(stderr, "Menus: overlay disabled (shader failed)\n");
    menus_set_hotbar(hotbar_blocks, HOTBAR_SLOTS, menu_item_info);

    hand_init("assets/textures/Steve.png");

    if (!blocks_validate())
        fprintf(stderr, "blocks.h has a bad entry, see above\n");

    load_block_textures();
    mesher_init(chunk_prog, block_face_tex);
    physics_init(block_face_tex);
    fluid_init(block_texture(BLOCK_WATER, BLOCK_FACE_TOP));
    lighting_init(chunk_prog, world_light_host());
    if (!sky_init("assets/textures/sky"))
        MessageBoxA(NULL, "sky_init failed (shader)", "Sky", MB_OK);
    sky_set_render_distance(RENDER_DISTANCE);
    fog_set_render_distance(RENDER_DISTANCE);
    sword_tex = load_texture("assets/textures/diamond_sword.png");
    hand_load_sword("assets/textures/diamond_sword.png");
    crack_init();

    select_slot(hotbar_sel);

    audio_init();
    audio_set_master(1.0f);
    load_block_sounds();

    MOB_STEVE.init();

    chunk_generator_set_seed((uint32_t)time(NULL));
    world_init();
    player_reset(&player);
    world_load_around(player.x, player.z);
    player.y = player.py = (double)world_spawn_height((int)floor(player.x), (int)floor(player.z));
    player.vy = 0.0;

    set_game_mode(MODE_SURVIVAL);
    glfwSetWindowTitle(window, "Paradox - Survival Mode");

    double last_time = glfwGetTime();
    double fps_timer = last_time;
    double tick_acc = 0.0;

    int frame_count = 0;
    int   last_sel = hotbar_sel;
    float item_timer = 0.0f;
    int fps_shown = 0;
    int death_handled = 0;

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();

        float dt = (float)(now - last_time);
        last_time = now;

        frame_count++;

        if (now - fps_timer >= 1) {
            fps_shown = frame_count;
            frame_count = 0;
            fps_timer = now;
        }

        if (dt > 0.1f)
            dt = 0.1f;

        int   paused = menus_paused();
        float gdt = paused ? 0.0f : dt;

        if (!paused)
            tick_acc += dt;

        int ticks = 0;

        while (tick_acc >= TICK_TIME && ticks < 10) {
            player_tick(&player, window, yaw);
            npc_push_player(&player.x, &player.vx);
            hand_tick(player.x - player.px, player.z - player.pz,
                player.vx, player.vy, player.vz, player.on_ground, yaw, pitch);
            bodyturn_tick(&player_body, yaw, player.x - player.px, player.z - player.pz,
                hand_attack_time() > 0.0f);
            audio_set_listener(player.x, player.y + 1.62, player.z, yaw);
            tick_footsteps();
            tick_block_particles();
            physics_tick();
            fluid_tick();

            if (game_mode == MODE_SURVIVAL && !player.dead)
                physics_collect_items(player.x, player.y, player.z, survival_take);

            player_walk_tick();
            npc_tick(player.x, player.y, player.z);

            if (break_cooldown > 0) break_cooldown--;
            if (place_cooldown > 0) place_cooldown--;

            tick_acc -= TICK_TIME;
            ticks++;
        }

        if (tick_acc >= TICK_TIME)
            tick_acc = 0.0;

        if (player.dead && !death_handled) {
            death_handled = 1;
            mine_active = 0;
            mine_progress = 0.0f;
            survival_drop_all();
            menus_show_death();
        }

        if (menus_take_respawn()) {
            player_reset(&player);
            world_load_around(player.x, player.z);
            player.y = player.py = (double)world_spawn_height((int)floor(player.x), (int)floor(player.z));
            player.vy = 0.0;
            death_handled = 0;
        }

        double alpha = tick_acc / TICK_TIME;

        player_eye_pos(&player, alpha, cam_pos);
        sky_update(gdt);
        lighting_update(gdt, player.x, player.y, player.z);

        camera_set_fov_modifier(player_fov_modifier(&player, alpha));

        float dir[3];
        camera_direction(dir);

        float view[16];
        float vp[16];

        float bobm[16];
        float bobbed[16];

        float cpos[3];
        float vdir[3];
        camera_resolve_view(cam_pos, dir, cpos, vdir);

        mat_look(view, cpos, vdir);
        hand_bob_matrix(alpha, bobm);
        if (view_mode) { memset(bobm, 0, sizeof bobm); bobm[0] = bobm[5] = bobm[10] = bobm[15] = 1.0f; }
        mat_mul(bobbed, bobm, view);
        mat_mul(vp, proj, bobbed);

        int left_held = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        int right_held = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        if (menus_open()) {
            left_held = right_held = 0;
            break_requested = place_requested = 0;
        }

        if (!left_held)  break_cooldown = 0;
        if (!right_held) place_cooldown = 0;

        int do_break = break_requested || (left_held && break_cooldown == 0);
        int do_place = place_requested || (right_held && place_cooldown == 0);

        if (break_requested && npc_attack(cam_pos, dir, 6.0f, player.x, player.z, 1.0f)) {
            do_break = 0;
            break_cooldown = BREAK_REPEAT_TICKS;
        }
        if (do_break && npc_aimed(cam_pos, dir, 6.0f)) do_break = 0;
        if (break_requested) hand_swing();

        break_requested = 0;
        place_requested = 0;

        if (game_mode == MODE_SURVIVAL && !player.dead) {
            int hit[3];
            int prev[3];
            int mining = 0;

            if (left_held && !npc_aimed(cam_pos, dir, 6.0f) &&
                fluid_raycast(cam_pos, dir, cur_reach(), hit, prev)) {

                int t = world_get_block(hit[0], hit[1], hit[2]);
                float hard = block_hardness(t);

                if (block_definition(t)->breakable && hard >= 0.0f) {
                    mining = 1;

                    if (!mine_active || t != mine_type || mine_held != held_block() ||
                        hit[0] != mine_pos[0] || hit[1] != mine_pos[1] || hit[2] != mine_pos[2]) {
                        mine_active = 1;
                        mine_type = t;
                        mine_held = held_block();
                        mine_pos[0] = hit[0]; mine_pos[1] = hit[1]; mine_pos[2] = hit[2];
                        mine_progress = 0.0f;
                        mine_sound_ticks = 0.0f;
                    }

                    if (break_cooldown == 0) {
                        float ticks = gdt * 20.0f;

                        float str = (mine_held == ITEM_DIAMOND_SWORD) ? 1.5f : 1.0f;
                        if (!player.on_ground) str /= 5.0f;

                        float rate = hard <= 0.0f ? 1.0f
                            : str / hard / (block_needs_tool(t) ? 100.0f : 30.0f);

                        mine_progress += rate * ticks;

                        mine_sound_ticks -= ticks;
                        if (mine_sound_ticks <= 0.0f) {
                            audio_play_3d(snd_pick(snd_walk[t], snd_walk_n[t]),
                                hit[0] + 0.5, hit[1] + 0.5, hit[2] + 0.5, 0.25f);
                            mine_sound_ticks = 4.0f;
                        }

                        if (mine_progress >= 1.0f) {
                            world_set_block(hit[0], hit[1], hit[2], BLOCK_AIR);
                            physics_block_changed(hit[0], hit[1], hit[2]);
                            spawn_block_particles(hit[0], hit[1], hit[2], t);
                            audio_play_3d(snd_pick(snd_break[t], snd_break_n[t]),
                                hit[0] + 0.5, hit[1] + 0.5, hit[2] + 0.5, 1.0f);

                            int drop = block_drop(t);
                            if (drop != BLOCK_AIR)
                                physics_spawn_item(drop, 1,
                                    hit[0] + random_float(0.0f, 1.0f) * 0.7 + 0.15,
                                    hit[1] + random_float(0.0f, 1.0f) * 0.7 + 0.15,
                                    hit[2] + random_float(0.0f, 1.0f) * 0.7 + 0.15,
                                    random_float(-1.0f, 1.0f) * 0.1, 0.2,
                                    random_float(-1.0f, 1.0f) * 0.1, 10);

                            player_add_exhaustion(0.025f);
                            break_cooldown = BREAK_REPEAT_TICKS;
                            player_quiet_steps(4);

                            mine_active = 0;
                            mine_progress = 0.0f;
                            mining = 0;
                        }
                    }
                }
            }

            if (!mining) {
                mine_active = 0;
                mine_progress = 0.0f;
            }
        }

        if (game_mode == MODE_CREATIVE && do_break) {
            int hit[3];
            int prev[3];

            if (fluid_raycast(cam_pos, dir, cur_reach(), hit, prev)) {
                int hit_type = world_get_block(hit[0], hit[1], hit[2]);

                if (block_definition(hit_type)->breakable) {
                    world_set_block(hit[0], hit[1], hit[2], BLOCK_AIR);
                    physics_block_changed(hit[0], hit[1], hit[2]);
                    spawn_block_particles(hit[0], hit[1], hit[2], hit_type);
                    audio_play_3d(snd_pick(snd_break[hit_type], snd_break_n[hit_type]),
                        hit[0] + 0.5, hit[1] + 0.5, hit[2] + 0.5, 1.0f);

                    break_cooldown = BREAK_REPEAT_TICKS;
                    player_quiet_steps(4);
                }
            }
        }

        int held = held_block();

        if (do_place && held != BLOCK_AIR && held < BLOCK_TYPES) {
            int hit[3];
            int prev[3];
            int placed = 0;

            if (fluid_raycast(cam_pos, dir, cur_reach(), hit, prev) &&
                !(prev[0] == hit[0] && prev[1] == hit[1] && prev[2] == hit[2]) &&
                world_in_bounds(prev[0], prev[1], prev[2]) &&
                !block_hits_player(prev[0], prev[1], prev[2])) {

                world_set_block(prev[0], prev[1], prev[2], held);
                physics_block_changed(prev[0], prev[1], prev[2]);
                audio_play_3d(snd_pick(snd_place[held], snd_place_n[held]),
                    prev[0] + 0.5, prev[1] + 0.5, prev[2] + 0.5, 1.0f);
                hand_use();

                place_cooldown = PLACE_REPEAT_TICKS;
                player_quiet_steps(4);
                placed = 1;

                if (game_mode == MODE_SURVIVAL)
                    survival_consume_held();
            }

            if (!placed) {
                hand_reject();
                place_cooldown = PLACE_REPEAT_TICKS;
            }
        }

        world_load_around(player.x, player.z);
        world_rebuild_meshes(player.x, player.z);

        int target_hit[3], target_prev[3];
        int has_target = fluid_raycast(cam_pos, dir, cur_reach(), target_hit, target_prev);

        if (left_held && has_target) hand_swing();

        {
            float fog_rgb[3];
            sky_get_fog_color(vdir, fog_rgb);
            fog_set_color(fog_rgb);
            glClearColor(fog_rgb[0], fog_rgb[1], fog_rgb[2], 1.0f);
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glPolygonMode(GL_FRONT_AND_BACK, wireframe_mode ? GL_LINE : GL_FILL);

        sky_draw(vp, cpos, vdir);

        fog_apply(chunk_prog);
        long tri_count = world_draw(vp, cpos, chunk_prog, tex_id, tex_count);
        physics_draw(vp, alpha, tex_id);
        fluid_draw(vp, cpos);
        fog_apply(cube_prog);
        glUniformMatrix4fv(vp_loc, 1, GL_FALSE, vp);
        glUniform1i(tex_loc, 0);
        draw_block_particles(vp, dir, cube_prog, vp_loc, alpha);

        sky_draw_clouds(vp, cpos);
        glUseProgram(cube_prog);

        if (has_target) {
            float outline_vertices[24 * 3];
            make_block_outline(target_hit[0], target_hit[1], target_hit[2], outline_vertices);

            glUseProgram(outline_prog);
            glUniformMatrix4fv(outline_vp_loc, 1, GL_FALSE, vp);
            glBindVertexArray(outline_vao);
            glBindBuffer(GL_ARRAY_BUFFER, outline_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof outline_vertices, outline_vertices);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glLineWidth(2.0f);
            glDrawArrays(GL_LINES, 0, 24);
            glDisable(GL_BLEND);
            glBindVertexArray(0);
            glUseProgram(cube_prog);
        }

        if (game_mode == MODE_SURVIVAL && mine_active && mine_progress > 0.0f) {
            crack_draw(vp, mine_pos, (int)(mine_progress * 10.0f));
            glUseProgram(cube_prog);
        }

        hand_frame(gdt);

        GLuint held_tex[BLOCK_FACE_COUNT];

        for (int face = 0; face < BLOCK_FACE_COUNT; face++)
            held_tex[face] = block_texture(held, face);

        if (item_sprite(held))
            held_tex[0] = item_sprite(held);

        npc_draw(alpha, vp);

        if (entity_debug)
            npc_debug_draw(vp, fb_w, fb_h, (float)alpha);

        if (view_mode) {
            MobState ms = {
                player.px + (player.x - player.px) * alpha,
                player.py + (player.y - player.py) * alpha,
                player.pz + (player.z - player.pz) * alpha,
                yaw, pitch, hand_attack_time(), (float)(glfwGetTime() * 20.0),
                player_walk_pos((float)alpha), player_walk_speed((float)alpha),
                player.sneaking
            };
            bodyturn_get(&player_body, yaw, (float)alpha, &ms.yaw, &ms.head_turn);
            hand_fill_held(&ms, held_tex);
            MOB_STEVE.draw(&ms, vp);
        }
        else {
            double hx = player.px;
            double hy = player.py + 1.62;
            double hz = player.pz;

            float world_light = lighting_brightness_at(hx, hy, hz);
            float self_light = lighting_player_brightness_at(hx, hy, hz);

            if (self_light > world_light)
                world_light = self_light;

            hand_set_world_light(world_light);

            hand_draw(alpha, yaw, pitch, view, fb_w, fb_h, held_tex);
        }

        if (menus_hotbar_dirty())
            select_slot(hotbar_sel);

        if (view_mode == 0) {
            if (player.head_water)     fluid_draw_overlay(0, yaw, pitch, fb_w, fb_h);
            else if (player.head_lava) fluid_draw_overlay(1, yaw, pitch, fb_w, fb_h);
        }

        UiItem hotbar_items[HOTBAR_SLOTS];

        for (int i = 0; i < HOTBAR_SLOTS; i++) {
            hotbar_items[i].top = block_texture(hotbar_blocks[i], BLOCK_FACE_TOP);
            hotbar_items[i].side = block_texture(hotbar_blocks[i], BLOCK_FACE_RIGHT);
            hotbar_items[i].flat = item_sprite(hotbar_blocks[i]);
        }

        if (game_mode == MODE_SURVIVAL) {
            int bubbles = (int)ceilf((float)(player.air - 2) * 10.0f / (float)PLAYER_MAX_AIR);
            if (bubbles < 0) bubbles = 0;
            if (bubbles > 10) bubbles = 10;
            ui_set_stats((int)ceilf(player.health), player.food, 0.0f, 0, bubbles);
            ui_set_hotbar_counts(surv_cnt);
        }
        else {
            ui_set_hotbar_counts(NULL);
        }

        ui_draw(hotbar_items, HOTBAR_SLOTS, hotbar_sel);

        char hud[64];
        snprintf(hud, sizeof hud, "%d FPS  %ld tris", fps_shown, tri_count);
        text_set(&fps_text, hud);

        float ui_s = (float)fb_h / 480.0f;
        float left = -((float)fb_w - 640.0f * ui_s) * 0.5f / ui_s;
        text_draw(&fps_text, left + 4.0f, 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1);
        if (hotbar_sel != last_sel) {
            last_sel = hotbar_sel;

            int id = held_block();
            const char* name = "";

            if (id == ITEM_DIAMOND_SWORD) name = "Diamond Sword";
            else if (id != BLOCK_AIR)     name = block_definition(id)->display_name;

            text_set(&item_text, name);
            item_timer = SHOW_ITEM_NAME ? ITEM_NAME_TIME : 0.0f;
        }

        if (item_timer > 0.0f) {
            item_timer -= gdt;

            float a = item_timer / ITEM_NAME_FADE_OUT;
            if (ITEM_NAME_FADE_IN > 0.0f) {
                float fi = (ITEM_NAME_TIME - item_timer) / ITEM_NAME_FADE_IN;
                if (fi < a) a = fi;
            }
            if (a > 1.0f) a = 1.0f;
            if (a < 0.0f) a = 0.0f;

            float x = 320.0f - text_width(&item_text) * 0.5f;
            float y = ui_item_name_y();
            text_draw(&item_text, x, y, 1.0f, 1.0f, 1.0f, a, 1);
        }

        menus_draw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    world_shutdown();
    lighting_shutdown();
    sky_shutdown();
    mesher_shutdown();
    physics_shutdown();
    fluid_shutdown();

    audio_shutdown();
    menus_shutdown();
    text_free(&fps_text);
    text_free(&item_text);
    text_shutdown();
    ui_shutdown();
    if (sword_tex) glDeleteTextures(1, &sword_tex);
    crack_shutdown();
    hand_shutdown();

    for (int t = 0; t < tex_count; t++)
        glDeleteTextures(1, &tex_id[t]);

    glDeleteBuffers(1, &outline_vbo);
    glDeleteVertexArrays(1, &outline_vao);
    glDeleteProgram(outline_prog);
    MOB_STEVE.shutdown();
    glDeleteProgram(cube_prog);
    glDeleteBuffers(1, &particle_vbo);
    glDeleteVertexArrays(1, &particle_vao);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}