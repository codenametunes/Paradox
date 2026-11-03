#include "main.h"
#include "chunkGenerator.h"
#define ENABLE_SNOW   0

#define CONTINENT_SCALE   0.0035f
#define HILL_SCALE        0.012f
#define DETAIL_SCALE      0.06f
#define MOUNTAIN_SCALE    0.0055f
#define MASK_SCALE        0.003f
#define HILL_AMPLITUDE    9.0f
#define DETAIL_AMPLITUDE  1.5f
#define MOUNTAIN_HEIGHT   75.0f
#define WARP_AMOUNT       45.0f
#define PLATEAU_STEP      6.0f
#define MOUNTAIN_COVERAGE 0.40f

#define TERRAIN_BAND      28.0f
#define NOISE3_SCALE      0.028f
#define OVERHANG_PLAINS   0.18f
#define OVERHANG_MOUNTAIN 1.15f
#define OVERHANG_REACH    48.0f

#define TUNNEL_STRENGTH   2.5f
#define TUNNEL_WIDTH      0.11f
#define CAVERN_THRESHOLD  0.30f

#define ARCH_STRENGTH     3.0f
#define ARCH_WIDTH        0.16f

#define ISLAND_SCALE      0.006f
#define ISLAND_THRESHOLD  0.8f
#define ISLAND_DEPTH      40.0f
#define ISLAND_HEIGHT_ABOVE_FLAT 120

#define STONE_ALTITUDE    52
#define SNOW_ALTITUDE     72
#define OPEN_AIR_GRASS    16

#define CELL_XZ 4
#define CELL_Y  4

#define GRID_XZ (CHUNK_SIZE / CELL_XZ + 1)
#define GRID_Y  (CHUNK_HEIGHT / CELL_Y + 1)

#define ISLAND_Y_WANT (FLAT_LEVEL + ISLAND_HEIGHT_ABOVE_FLAT)
#define ISLAND_Y ((ISLAND_Y_WANT) < (CHUNK_HEIGHT - 40) ? (ISLAND_Y_WANT) : (CHUNK_HEIGHT - 40))

static float noise_offset_x;
static float noise_offset_z;

static int perm[512];
static uint32_t current_seed;
static int permutation_ready = 0;

static uint32_t random_next(uint32_t* state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void chunk_generator_set_seed(uint32_t seed)
{
    if (permutation_ready && current_seed == seed)
        return;

    current_seed = seed;

    uint32_t state = seed ^ 0x9E3779B9u;
    if (state == 0)
        state = 0x6D2B79F5u;

    for (int i = 0; i < 256; i++)
        perm[i] = i;

    for (int i = 255; i > 0; i--) {
        int j = (int)(random_next(&state) % (uint32_t)(i + 1));
        int t = perm[i];
        perm[i] = perm[j];
        perm[j] = t;
    }

    for (int i = 0; i < 256; i++)
        perm[i + 256] = perm[i];

    uint32_t offset_state = seed ^ 0xA511E9B3u;
    noise_offset_x = (float)(random_next(&offset_state) % 256u);
    noise_offset_z = (float)(random_next(&offset_state) % 256u);

    permutation_ready = 1;
}

static float fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float smoothstep(float e0, float e1, float x)
{
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float grad2(int hash, float x, float y)
{
    static const float gx[8] = { 1, -1,  0,  0,  0.70710678f, -0.70710678f,  0.70710678f, -0.70710678f };
    static const float gy[8] = { 0,  0,  1, -1,  0.70710678f,  0.70710678f, -0.70710678f, -0.70710678f };
    int h = hash & 7;
    return gx[h] * x + gy[h] * y;
}

static float perlin(float x, float y)
{
    float fx = floorf(x);
    float fy = floorf(y);

    int X = (int)fx & 255;
    int Y = (int)fy & 255;

    x -= fx;
    y -= fy;

    float u = fade(x);
    float v = fade(y);

    int A = perm[X] + Y;
    int B = perm[X + 1] + Y;

    float n = lerp(
        lerp(grad2(perm[A], x, y),
            grad2(perm[B], x - 1.0f, y), u),
        lerp(grad2(perm[A + 1], x, y - 1.0f),
            grad2(perm[B + 1], x - 1.0f, y - 1.0f), u),
        v);

    return n * 1.41421356f;
}

static float fbm(float x, float y, int octaves)
{
    float total = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;

    for (int i = 0; i < octaves; i++) {
        total += perlin(x * freq + i * 31.7f, y * freq - i * 17.3f) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return total / norm;
}

static float ridged(float x, float y, int octaves)
{
    float total = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    float weight = 1.0f;

    for (int i = 0; i < octaves; i++) {
        float n = 1.0f - fabsf(perlin(x * freq + i * 23.1f, y * freq + i * 11.9f));
        n *= n;
        n *= weight;
        weight = clampf(n * 2.0f, 0.0f, 1.0f);

        total += n * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return total / norm;
}

static float grad3(int h, float x, float y, float z)
{
    h &= 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : ((h == 12 || h == 14) ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

static float perlin3(float x, float y, float z)
{
    float fx = floorf(x), fy = floorf(y), fz = floorf(z);

    int X = (int)fx & 255;
    int Y = (int)fy & 255;
    int Z = (int)fz & 255;

    x -= fx;
    y -= fy;
    z -= fz;

    float u = fade(x), v = fade(y), w = fade(z);

    int A = perm[X] + Y, AA = perm[A] + Z, AB = perm[A + 1] + Z;
    int B = perm[X + 1] + Y, BA = perm[B] + Z, BB = perm[B + 1] + Z;

    return lerp(
        lerp(lerp(grad3(perm[AA], x, y, z),
            grad3(perm[BA], x - 1.0f, y, z), u),
            lerp(grad3(perm[AB], x, y - 1.0f, z),
                grad3(perm[BB], x - 1.0f, y - 1.0f, z), u), v),
        lerp(lerp(grad3(perm[AA + 1], x, y, z - 1.0f),
            grad3(perm[BA + 1], x - 1.0f, y, z - 1.0f), u),
            lerp(grad3(perm[AB + 1], x, y - 1.0f, z - 1.0f),
                grad3(perm[BB + 1], x - 1.0f, y - 1.0f, z - 1.0f), u), v),
        w);
}

static float fbm3(float x, float y, float z, int octaves)
{
    float total = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;

    for (int i = 0; i < octaves; i++) {
        total += perlin3(x * freq + i * 31.7f,
            y * freq + i * 17.3f,
            z * freq - i * 23.9f) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return clampf(total / norm * 1.4f, -1.0f, 1.0f);
}

typedef struct {
    float h;
    float mask;
    float island;
} Column;

static float base_curve(float c)
{
    static const float pts[][2] = {
        { -1.0f, -22.0f },
        { -0.35f, -9.0f },
        { -0.05f, -2.0f },
        {  0.10f,  2.0f },
        {  0.40f,  7.0f },
        {  1.00f, 14.0f },
    };
    const int n = (int)(sizeof(pts) / sizeof(pts[0]));

    if (c <= pts[0][0])     return pts[0][1];
    if (c >= pts[n - 1][0]) return pts[n - 1][1];

    for (int i = 1; i < n; i++) {
        if (c <= pts[i][0]) {
            float t = (c - pts[i - 1][0]) / (pts[i][0] - pts[i - 1][0]);
            return lerp(pts[i - 1][1], pts[i][1], t);
        }
    }
    return 0.0f;
}

static Column terrain_column(int wx, int wz)
{
    float px = (float)wx + noise_offset_x;
    float pz = (float)wz + noise_offset_z;

    Column col;

    float continent = clampf(fbm(px * CONTINENT_SCALE, pz * CONTINENT_SCALE, 4) * 1.7f,
        -1.0f, 1.0f);
    float h = base_curve(continent);

    float erosion = fbm(px * MASK_SCALE + 500.0f, pz * MASK_SCALE - 300.0f, 3) * 0.5f + 0.5f;
    float hill_strength = lerp(1.0f, 0.25f, smoothstep(0.45f, 0.75f, erosion));
    h += fbm(px * HILL_SCALE, pz * HILL_SCALE, 4) * HILL_AMPLITUDE * hill_strength;

    float inland = smoothstep(-0.10f, 0.45f, continent);
    float region = fbm(px * MASK_SCALE - 800.0f, pz * MASK_SCALE + 200.0f, 3) * 0.5f + 0.5f;
    float mask = inland * smoothstep(MOUNTAIN_COVERAGE, MOUNTAIN_COVERAGE + 0.2f, region);

    if (mask > 0.0f) {
        float wx2 = px + fbm(px * 0.008f + 90.0f, pz * 0.008f, 2) * WARP_AMOUNT;
        float wz2 = pz + fbm(px * 0.008f - 40.0f, pz * 0.008f + 70.0f, 2) * WARP_AMOUNT;

        float r = ridged(wx2 * MOUNTAIN_SCALE, wz2 * MOUNTAIN_SCALE, 6);
        float dome = fbm(px * MOUNTAIN_SCALE * 0.5f, pz * MOUNTAIN_SCALE * 0.5f, 2) * 0.5f + 0.5f;
        float m = r * (0.55f + 0.45f * dome);

        float mesa_zone = smoothstep(0.55f, 0.75f,
            fbm(px * 0.004f + 1200.0f, pz * 0.004f - 900.0f, 2) * 0.5f + 0.5f);
        if (mesa_zone > 0.0f) {
            float raw = m * MOUNTAIN_HEIGHT;
            float stepped = floorf(raw / PLATEAU_STEP) * PLATEAU_STEP;
            float frac = (raw - stepped) / PLATEAU_STEP;
            stepped += smoothstep(0.75f, 1.0f, frac) * PLATEAU_STEP;
            m = lerp(raw, stepped, mesa_zone * 0.85f) / MOUNTAIN_HEIGHT;
        }

        h += m * MOUNTAIN_HEIGHT * mask;
    }

    h += fbm(px * DETAIL_SCALE, pz * DETAIL_SCALE, 2) * DETAIL_AMPLITUDE * (1.0f - mask * 0.7f);

    float blob = fbm(px * ISLAND_SCALE + 2500.0f, pz * ISLAND_SCALE - 1700.0f, 3) * 0.5f + 0.5f;

    col.h = (float)FLAT_LEVEL + h;
    col.mask = mask;
    col.island = smoothstep(ISLAND_THRESHOLD, ISLAND_THRESHOLD + 0.15f, blob);
    return col;
}

int chunk_generator_surface_height(int wx, int wz)
{
    if (!permutation_ready)
        chunk_generator_set_seed(1337u);
    return (int)floorf(terrain_column(wx, wz).h);
}

static float sample_density(int wx, int y, int wz, const Column* col)
{
    if (y < 3)
        return 1.0f;

    float px = (float)wx + noise_offset_x;
    float pz = (float)wz + noise_offset_z;
    float py = (float)y;

    float mask = col->mask;
    float rel = col->h - py;
    float d = -1.0f;

    if (rel > -OVERHANG_REACH) {
        float amp = lerp(OVERHANG_PLAINS, OVERHANG_MOUNTAIN, mask);
        float n = fbm3(px * NOISE3_SCALE, py * NOISE3_SCALE * 0.8f, pz * NOISE3_SCALE, 3);

        d = clampf(rel / TERRAIN_BAND, -1.5f, 1.5f) + n * amp;

        float cf = smoothstep(4.0f, 10.0f, py) * smoothstep(-3.0f, 10.0f, rel);
        if (cf > 0.0f) {
            float a = perlin3(px * 0.020f + 100.0f, py * 0.030f, pz * 0.020f);
            float b = perlin3(px * 0.020f - 200.0f, py * 0.030f + 50.0f, pz * 0.020f + 300.0f);
            float t = fmaxf(fabsf(a), fabsf(b));
            float carve = (1.0f - smoothstep(0.04f, TUNNEL_WIDTH, t)) * TUNNEL_STRENGTH;

            float c = fbm3(px * 0.011f + 500.0f, py * 0.018f, pz * 0.011f - 500.0f, 2);
            carve += smoothstep(CAVERN_THRESHOLD, CAVERN_THRESHOLD + 0.12f, c) * TUNNEL_STRENGTH;

            d -= carve * cf;
        }

        if (mask > 0.15f) {
            float alt = smoothstep(10.0f, 35.0f, py - (float)FLAT_LEVEL);
            if (alt > 0.0f) {
                float a = perlin3(px * 0.012f + 900.0f, py * 0.016f, pz * 0.012f);
                float b = perlin3(px * 0.012f - 700.0f, py * 0.016f + 80.0f, pz * 0.012f + 400.0f);
                float t = fmaxf(fabsf(a), fabsf(b));
                d -= (1.0f - smoothstep(0.05f, ARCH_WIDTH, t)) * ARCH_STRENGTH * mask * alt;
            }
        }
    }

    float s = col->island;
    if (s > 0.0f &&
        py > (float)ISLAND_Y - ISLAND_DEPTH - 14.0f &&
        py < (float)ISLAND_Y + 20.0f) {

        float top = (float)ISLAND_Y + 5.0f * s;
        float bot = (float)ISLAND_Y - s * ISLAND_DEPTH;
        float n = fbm3(px * 0.05f, py * 0.05f, pz * 0.05f, 3);

        float id = fminf(top - py, py - bot) / 9.0f + n * 0.55f * s - 0.12f * (1.0f - s);
        if (id > d)
            d = id;
    }

    return d;
}

static int air_sides(unsigned char (*b)[CHUNK_SIZE][CHUNK_SIZE], int x, int y, int z)
{
    int n = 0;
    if (x > 0 && b[y][z][x - 1] == BLOCK_AIR) n++;
    if (x < CHUNK_SIZE - 1 && b[y][z][x + 1] == BLOCK_AIR) n++;
    if (z > 0 && b[y][z - 1][x] == BLOCK_AIR) n++;
    if (z < CHUNK_SIZE - 1 && b[y][z + 1][x] == BLOCK_AIR) n++;
    return n;
}

void chunk_generate(unsigned char (*blocks)[CHUNK_SIZE][CHUNK_SIZE],
    int32_t cx, int32_t cz, int* out_max_y, uint32_t* out_solid)
{
    if (!permutation_ready)
        chunk_generator_set_seed(1337u);

    memset(blocks, BLOCK_AIR,
        (size_t)CHUNK_HEIGHT * CHUNK_SIZE * CHUNK_SIZE);

    const int wx0 = cx * CHUNK_SIZE;
    const int wz0 = cz * CHUNK_SIZE;

    Column cols[GRID_XZ][GRID_XZ];
    float  grid[GRID_Y][GRID_XZ][GRID_XZ];

    for (int gz = 0; gz < GRID_XZ; gz++)
        for (int gx = 0; gx < GRID_XZ; gx++)
            cols[gz][gx] = terrain_column(wx0 + gx * CELL_XZ, wz0 + gz * CELL_XZ);

    for (int gy = 0; gy < GRID_Y; gy++)
        for (int gz = 0; gz < GRID_XZ; gz++)
            for (int gx = 0; gx < GRID_XZ; gx++)
                grid[gy][gz][gx] = sample_density(wx0 + gx * CELL_XZ, gy * CELL_Y,
                    wz0 + gz * CELL_XZ, &cols[gz][gx]);

    int max_y = 0;
    uint32_t solid = 0;

    for (int cy = 0; cy < CHUNK_HEIGHT / CELL_Y; cy++) {
        for (int cz2 = 0; cz2 < CHUNK_SIZE / CELL_XZ; cz2++) {
            for (int cx2 = 0; cx2 < CHUNK_SIZE / CELL_XZ; cx2++) {

                float d000 = grid[cy][cz2][cx2], d100 = grid[cy][cz2][cx2 + 1];
                float d001 = grid[cy][cz2 + 1][cx2], d101 = grid[cy][cz2 + 1][cx2 + 1];
                float d010 = grid[cy + 1][cz2][cx2], d110 = grid[cy + 1][cz2][cx2 + 1];
                float d011 = grid[cy + 1][cz2 + 1][cx2], d111 = grid[cy + 1][cz2 + 1][cx2 + 1];

                float lo = fminf(fminf(fminf(d000, d100), fminf(d001, d101)),
                    fminf(fminf(d010, d110), fminf(d011, d111)));
                float hi = fmaxf(fmaxf(fmaxf(d000, d100), fmaxf(d001, d101)),
                    fmaxf(fmaxf(d010, d110), fmaxf(d011, d111)));

                if (hi <= 0.0f)
                    continue;

                int all_solid = lo > 0.0f;

                for (int ly = 0; ly < CELL_Y; ly++) {
                    float ty = (float)ly / (float)CELL_Y;
                    int y = cy * CELL_Y + ly;

                    for (int lz = 0; lz < CELL_XZ; lz++) {
                        float tz = (float)lz / (float)CELL_XZ;
                        int z = cz2 * CELL_XZ + lz;

                        for (int lx = 0; lx < CELL_XZ; lx++) {
                            int x = cx2 * CELL_XZ + lx;
                            int is_solid = all_solid;

                            if (!is_solid) {
                                float tx = (float)lx / (float)CELL_XZ;
                                float d =
                                    lerp(lerp(lerp(d000, d100, tx), lerp(d001, d101, tx), tz),
                                        lerp(lerp(d010, d110, tx), lerp(d011, d111, tx), tz), ty);
                                is_solid = d > 0.0f;
                            }

                            if (is_solid) {
                                blocks[y][z][x] = BLOCK_STONE;
                                solid++;
                                if (y > max_y)
                                    max_y = y;
                            }
                        }
                    }
                }
            }
        }
    }

    for (int z = 0; z < CHUNK_SIZE; z++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {

            int wx = wx0 + x;
            int wz = wz0 + z;

            uint32_t h = (uint32_t)wx * 73856093u ^ (uint32_t)wz * 19349663u ^ current_seed;
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            int dirt_depth = DIRT_LAYERS - 1 + (int)(h % 3u);
            if (dirt_depth < 1)
                dirt_depth = 1;

            int sky = 1;
            int air_run = 0;
            int dirt_left = 0;

            for (int y = CHUNK_HEIGHT - 1; y >= 1; y--) {

                if (blocks[y][z][x] == BLOCK_AIR) {
                    air_run++;
                    dirt_left = 0;
                    continue;
                }

                if (air_run > 0 || y == CHUNK_HEIGHT - 1) {
                    int open = sky || air_run >= OPEN_AIR_GRASS;
                    int steep = (y > 1) && air_sides(blocks, x, y - 1, z) > 0;
                    int high = y > FLAT_LEVEL + STONE_ALTITUDE;

                    if (open && !steep && !high) {
                        blocks[y][z][x] = BLOCK_GRASS;
                        dirt_left = dirt_depth;
                    }
                    else {
                        dirt_left = 0;
#if ENABLE_SNOW
                        if (open && !steep && y > FLAT_LEVEL + SNOW_ALTITUDE)
                            blocks[y][z][x] = BLOCK_SNOW;
#endif
                    }
                    sky = 0;
                    air_run = 0;
                }
                else if (dirt_left > 0) {
                    dirt_left--;
                    if (air_sides(blocks, x, y, z) == 0)
                        blocks[y][z][x] = BLOCK_DIRT;
                }
            }

            blocks[0][z][x] = BLOCK_BEDROCK;
        }
    }

    *out_max_y = max_y;
    *out_solid = solid;
}