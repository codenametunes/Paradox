#include "main.h"
#include "chunkManager.h"
#include "mesher.h"
#include "Lighting/lighting.h"
#include "Player/player.h"
#include "Sky/sky.h"

#define MESH_BUDGET            0.004  // 4ms, lower it if it stutters
#define CHUNK_LOADS_PER_FRAME  2

#define BLOCK_INDEX(x,y,z) (((y) * CHUNK_SIZE + (z)) * CHUNK_SIZE + (x))

typedef struct {
    int32_t cx, cz;
    int loaded;
    int max_y;
    uint32_t solid_count;
    uint8_t blocks[CHUNK_SIZE * CHUNK_SIZE * CHUNK_HEIGHT];  // 128KB per chunk, ouch
    ChunkMesh mesh;
    LightChunk light;
    int dirty;
} Chunk;

static Chunk world[WORLD_CHUNKS][WORLD_CHUNKS];  // huge, but it's static so no stack crash

static int slot_coord(int32_t c)
{
    int r = (int)(c % WORLD_CHUNKS);
    return r < 0 ? r + WORLD_CHUNKS : r;
}

static int32_t floor_div_chunk(int32_t c)
{
    int32_t q = c / CHUNK_SIZE;
    if (c < 0 && c % CHUNK_SIZE) --q;
    return q;
}

int world_in_bounds(int32_t x, int32_t y, int32_t z)
{
    (void)x; (void)z;
    return y >= WORLD_FLOOR && y < SKY_LIMIT;
}

static Chunk* find_chunk(int32_t cx, int32_t cz)
{
    Chunk* ch = &world[slot_coord(cx)][slot_coord(cz)];
    return ch->loaded && ch->cx == cx && ch->cz == cz ? ch : NULL;
}

int world_get_block(int32_t x, int32_t y, int32_t z)
{
    if (!world_in_bounds(x, y, z)) return BLOCK_AIR;

    int32_t cx = floor_div_chunk(x);
    int32_t cz = floor_div_chunk(z);

    Chunk* ch = find_chunk(cx, cz);
    if (!ch) return BLOCK_AIR;

    int lx = (int)(x - cx * CHUNK_SIZE);
    int lz = (int)(z - cz * CHUNK_SIZE);

    return ch->blocks[BLOCK_INDEX(lx, y, lz)];
}

int world_block(int x, int y, int z) { return world_get_block(x, y, z); }

static LightChunk* lh_chunk(int cx, int cz) { Chunk* c = find_chunk(cx, cz); return c ? &c->light : NULL; }
static LightBlocks lh_blocks(int cx, int cz) { Chunk* c = find_chunk(cx, cz); return c ? (LightBlocks)c->blocks : NULL; }
static int         lh_max_y(int cx, int cz) { Chunk* c = find_chunk(cx, cz); return c ? c->max_y : 0; }
static const LightHost light_host = { lh_chunk, lh_blocks, lh_max_y };

const LightHost* world_light_host(void) { return &light_host; }

static void mark_dirty(int32_t x, int32_t y, int32_t z)
{
    if (!world_in_bounds(x, y, z)) return;
    Chunk* ch = find_chunk(floor_div_chunk(x), floor_div_chunk(z));
    if (ch) ch->dirty = 1;
}

void world_set_block(int32_t x, int32_t y, int32_t z, int type)
{
    if (!world_in_bounds(x, y, z)) return;

    int32_t cx = floor_div_chunk(x);
    int32_t cz = floor_div_chunk(z);

    Chunk* ch = find_chunk(cx, cz);
    if (!ch) return;

    int lx = (int)(x - cx * CHUNK_SIZE);
    int lz = (int)(z - cz * CHUNK_SIZE);

    int old_type = ch->blocks[BLOCK_INDEX(lx, y, lz)];

    if (old_type == type) return;

    ch->blocks[BLOCK_INDEX(lx, y, lz)] = (unsigned char)type;

    if (old_type == BLOCK_AIR && type != BLOCK_AIR)
        ch->solid_count++;
    else if (old_type != BLOCK_AIR && type == BLOCK_AIR && ch->solid_count > 0)
        ch->solid_count--;

    if (type != BLOCK_AIR && y > ch->max_y)
        ch->max_y = y;
    else if (type == BLOCK_AIR && y == ch->max_y)
    {
        while (ch->max_y > 0)  // walk down until we find a layer with something in it
        {
            int found = 0;

            for (int zz = 0; zz < CHUNK_SIZE && !found; zz++)
            {
                for (int xx = 0; xx < CHUNK_SIZE && !found; xx++)
                {
                    if (ch->blocks[BLOCK_INDEX(xx, ch->max_y, zz)] != BLOCK_AIR)
                        found = 1;
                }
            }

            if (found) break;
            ch->max_y--;
        }
    }

    lighting_block_changed(x, y, z, old_type, type);

    mark_dirty(x, y, z);
    mark_dirty(x + 1, y, z);
    mark_dirty(x - 1, y, z);
    mark_dirty(x, y + 1, z);
    mark_dirty(x, y - 1, z);
    mark_dirty(x, y, z + 1);
    mark_dirty(x, y, z - 1);
}

static void generate_chunk(Chunk* ch)
{
    chunk_generate((unsigned char (*)[CHUNK_SIZE][CHUNK_SIZE])ch->blocks,  // ???? scary cast, only ok while the layout matches
        ch->cx, ch->cz, &ch->max_y, &ch->solid_count);
    ch->dirty = 1;
}

static void unload_chunk(Chunk* ch)
{
    if (ch->loaded) { mesh_destroy(&ch->mesh); lighting_chunk_destroy(&ch->light); }
    memset(ch, 0, sizeof(*ch));
}

static void ensure_chunk(int32_t cx, int32_t cz)
{
    Chunk* ch = &world[slot_coord(cx)][slot_coord(cz)];
    if (ch->loaded && ch->cx == cx && ch->cz == cz) return;

    unload_chunk(ch);
    ch->cx = cx; ch->cz = cz; ch->loaded = 1;

    generate_chunk(ch);
    mesh_create(&ch->mesh);
    lighting_chunk_create(&ch->light);
    lighting_chunk_generate(cx, cz);

    Chunk* n;
    n = find_chunk(cx - 1, cz); if (n) n->dirty = 1;
    n = find_chunk(cx + 1, cz); if (n) n->dirty = 1;
    n = find_chunk(cx, cz - 1); if (n) n->dirty = 1;
    n = find_chunk(cx, cz + 1); if (n) n->dirty = 1;
}

void world_init(void)
{
    memset(world, 0, sizeof world);
}

void world_shutdown(void)
{
    for (int cx = 0; cx < WORLD_CHUNKS; cx++)
        for (int cz = 0; cz < WORLD_CHUNKS; cz++)
            if (world[cx][cz].loaded) {
                mesh_destroy(&world[cx][cz].mesh);
                lighting_chunk_destroy(&world[cx][cz].light);
                world[cx][cz].loaded = 0;
            }
}

void world_load_around(double x, double z)
{
    int32_t pcx = floor_div_chunk((int32_t)floor(x));
    int32_t pcz = floor_div_chunk((int32_t)floor(z));

    for (int load = 0; load < CHUNK_LOADS_PER_FRAME; load++)
    {
        int found = 0;
        int best_dist = INT_MAX;  // start with 'infinitely far'
        int32_t best_x = 0;
        int32_t best_z = 0;

        for (int dz = -RENDER_DISTANCE; dz <= RENDER_DISTANCE; dz++)
        {
            for (int dx = -RENDER_DISTANCE; dx <= RENDER_DISTANCE; dx++)
            {
                int32_t cx = pcx + dx;
                int32_t cz = pcz + dz;

                if (find_chunk(cx, cz))
                    continue;

                int dist = dx * dx + dz * dz;

                if (!found || dist < best_dist)
                {
                    found = 1;
                    best_dist = dist;
                    best_x = cx;
                    best_z = cz;
                }
            }
        }

        if (!found)
            break;

        ensure_chunk(best_x, best_z);
    }
}

static void gather_mesh_input(const Chunk* ch, MeshInput* in)
{
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            Chunk* n = find_chunk(ch->cx + dx, ch->cz + dz);
            in->blocks[dz + 1][dx + 1] = n ? (MeshBlocks)n->blocks : NULL;
            in->max_y[dz + 1][dx + 1] = n ? n->max_y : 0;
        }
}

void world_rebuild_meshes(double x, double z)
{
    int32_t pcx = floor_div_chunk((int32_t)floor(x));
    int32_t pcz = floor_div_chunk((int32_t)floor(z));
    double start = glfwGetTime();

    for (;;) {  // runs until the break, ???? hope it breaks
        Chunk* best = NULL;
        long long best_dist = 0;
        for (int cx = 0; cx < WORLD_CHUNKS; cx++)
            for (int cz = 0; cz < WORLD_CHUNKS; cz++) {
                Chunk* ch = &world[cx][cz];
                if (!ch->loaded || !ch->dirty) continue;

                if (ch->solid_count == 0)  // empty chunk, nothing to mesh
                {
                    ch->mesh.quad_count = 0;
                    memset(ch->mesh.first, 0, sizeof(ch->mesh.first));
                    ch->dirty = 0;
                    continue;
                }
                long long dx = (long long)ch->cx - pcx, dz = (long long)ch->cz - pcz;
                long long d = dx * dx + dz * dz;
                if (!best || d < best_dist) { best = ch; best_dist = d; }
            }
        if (!best) break;

        MeshInput in;
        gather_mesh_input(best, &in);
        mesh_build(&best->mesh, &in);
        best->dirty = 0;

        if (glfwGetTime() - start > MESH_BUDGET) break;  // out of time, the rest waits for next frame
    }
}

long world_draw(const float* vp, const float* cam_pos, GLuint chunk_prog,
    const GLuint* tex_ids, int tex_count)
{
    mesher_set_frustum(vp);
    glUseProgram(chunk_prog);
    mesher_begin(vp);
    lighting_begin();

    long tri_count = 0;

    for (int cx = 0; cx < WORLD_CHUNKS; cx++) {
        for (int cz = 0; cz < WORLD_CHUNKS; cz++) {
            Chunk* ch = &world[cx][cz];
            if (!ch->loaded || !ch->mesh.quad_count) continue;

            float x0 = (float)ch->cx * CHUNK_SIZE;
            float z0 = (float)ch->cz * CHUNK_SIZE;

            if (!mesher_aabb_visible(x0, 0.0f, z0, x0 + CHUNK_SIZE, (float)ch->max_y + 1.0f, z0 + CHUNK_SIZE))
                continue;

            unsigned fm = mesher_face_mask(cam_pos[0], cam_pos[1], cam_pos[2], x0, z0, ch->max_y);
            lighting_bind_chunk(&ch->light, ch->cx, ch->cz, ch->max_y);
            tri_count += mesh_draw(&ch->mesh, fm, x0, z0, tex_ids, tex_count);
        }
    }

    return tri_count;
}

int world_raycast(const float* o, const float* d, float max_dist, int* hit, int* prev)
{
    int ix = (int)floorf(o[0]);
    int iy = (int)floorf(o[1]);
    int iz = (int)floorf(o[2]);

    int step[3];
    float t_delta[3];
    float t_max[3];

    int cell[3] = { ix, iy, iz };

    for (int i = 0; i < 3; i++) {
        if (d[i] > 0.0f) {
            step[i] = 1;
            t_delta[i] = 1.0f / d[i];
            t_max[i] = ((float)(cell[i] + 1) - o[i]) * t_delta[i];
        }
        else if (d[i] < 0.0f) {
            step[i] = -1;
            t_delta[i] = -1.0f / d[i];
            t_max[i] = (o[i] - (float)cell[i]) * t_delta[i];
        }
        else {
            step[i] = 0;
            t_delta[i] = 1e30f;  // basically infinity
            t_max[i] = 1e30f;
        }
    }

    prev[0] = cell[0];
    prev[1] = cell[1];
    prev[2] = cell[2];

    for (;;) {  // runs until the break, ???? hope it breaks
        if (world_get_block(cell[0], cell[1], cell[2]) != BLOCK_AIR) {
            hit[0] = cell[0];
            hit[1] = cell[1];
            hit[2] = cell[2];
            return 1;
        }

        prev[0] = cell[0];
        prev[1] = cell[1];
        prev[2] = cell[2];

        int axis = 0;
        if (t_max[1] < t_max[axis]) axis = 1;
        if (t_max[2] < t_max[axis]) axis = 2;

        if (t_max[axis] > max_dist)
            return 0;

        cell[axis] += step[axis];
        t_max[axis] += t_delta[axis];
    }
}

int world_spawn_height(int bx, int bz)
{
    int y = chunk_generator_surface_height(bx, bz) + 16;

    if (y > CHUNK_HEIGHT - 3) y = CHUNK_HEIGHT - 3;  // don't spawn above the top

    for (; y > 1; y--) {  // ???? if nothing fits we end at y=1 (inside the ground probably)
        if (world_get_block(bx, y, bz) != BLOCK_AIR &&
            world_get_block(bx, y + 1, bz) == BLOCK_AIR &&
            world_get_block(bx, y + 2, bz) == BLOCK_AIR)
            break;
    }

    return y + 1;
}