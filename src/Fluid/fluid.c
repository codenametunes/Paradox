#include "fluid.h"
#include "Chunk Generation/chunkGenerator.h"
#include "Chunk Generation/chunkManager.h"

/* ------------------------------------------------------------ tuning */

enum { K_WATER = 0, K_LAVA = 1 };

static const int KIND_BLOCK[2] = { BLOCK_WATER, BLOCK_LAVA };
static const int KIND_DELAY[2] = { 5, 30 };     /* game ticks between updates */
static const int KIND_DECAY[2] = { 1, 2 };      /* level lost per block: water 7 blocks, lava 3 */
static const float KIND_ALPHA[2] = { 0.70f, 1.0f };

#define MAP_BITS            19
#define MAP_SIZE            (1 << MAP_BITS)
#define MAP_MASK            (MAP_SIZE - 1)
#define QUEUE_CAP           (1 << 16)
#define MAX_EVENTS_PER_TICK 4096
#define DRAW_RADIUS         96            /* blocks around the camera that get meshed */
#define MAX_VERTS           (1 << 18)     /* per fluid kind */

/* Fluids.png is 48x64 (16x16 tiles). Rects are in UV space, textures are loaded flipped.
   water tile = column 1 / row 0, lava tile = column 1 / row 2. Change here if you want other tiles. */
static const float TILE_MIN[2][2] = { { 16.0f / 48.0f, 1.0f - 16.0f / 64.0f },
                                       { 16.0f / 48.0f, 1.0f - 48.0f / 64.0f } };
static const float TILE_SIZE[2][2] = { { 16.0f / 48.0f, 16.0f / 64.0f },
                                       { 16.0f / 48.0f, 16.0f / 64.0f } };

/* ------------------------------------------------------------ cell table */

typedef struct { int x, y, z; unsigned char used, kind, meta, pending; } Cell;

static Cell* map;
static int   cell_count;
static int   dirty = 1;

static unsigned hash3(int x, int y, int z)
{
    unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)z * 83492791u;
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12;
    return h & MAP_MASK;
}

static Cell* find(int x, int y, int z)
{
    for (unsigned i = hash3(x, y, z); map[i].used; i = (i + 1) & MAP_MASK)
        if (map[i].x == x && map[i].y == y && map[i].z == z)
            return &map[i];
    return NULL;
}

static Cell* insert(int x, int y, int z, int kind, int meta)
{
    Cell* c = find(x, y, z);
    if (!c) {
        if (cell_count >= MAP_SIZE * 3 / 4) return NULL;
        unsigned i = hash3(x, y, z);
        while (map[i].used) i = (i + 1) & MAP_MASK;
        c = &map[i];
        c->x = x; c->y = y; c->z = z; c->used = 1; c->pending = 0;
        cell_count++;
    }
    c->kind = (unsigned char)kind;
    c->meta = (unsigned char)meta;
    return c;
}

/* linear probing with backward shift: pointers into the table are invalid afterwards */
static void erase(Cell* c)
{
    unsigned i = (unsigned)(c - map), j = i;
    map[i].used = 0;
    cell_count--;
    for (;;) {
        j = (j + 1) & MAP_MASK;
        if (!map[j].used) break;
        unsigned k = hash3(map[j].x, map[j].y, map[j].z);
        if (i <= j ? (i < k && k <= j) : (i < k || k <= j)) continue;
        map[i] = map[j];
        map[j].used = 0;
        i = j;
    }
}

int fluid_count(void) { return cell_count; }

/* ------------------------------------------------------- scheduled ticks */

typedef struct { int x, y, z; unsigned due; } Ev;
typedef struct { Ev* a; unsigned head, tail; } Queue;

static Queue    queues[2];      /* constant delay per kind => FIFO is already sorted by due time */
static unsigned now_tick;

static void schedule(int x, int y, int z)
{
    Cell* c = find(x, y, z);
    if (!c || c->pending) return;
    Queue* q = &queues[c->kind];
    if (q->tail - q->head >= QUEUE_CAP) return;
    c->pending = 1;
    q->a[q->tail & (QUEUE_CAP - 1)] = (Ev){ x, y, z, now_tick + (unsigned)KIND_DELAY[c->kind] };
    q->tail++;
}

static void notify_neighbours(int x, int y, int z)
{
    schedule(x + 1, y, z); schedule(x - 1, y, z);
    schedule(x, y, z + 1); schedule(x, y, z - 1);
    schedule(x, y + 1, z); schedule(x, y - 1, z);
}

/* ------------------------------------------------------------ world helpers */

static int blk(int x, int y, int z)
{
    return world_in_bounds(x, y, z) ? world_get_block(x, y, z) : -1;
}

static int kind_of(int id) { return id == BLOCK_WATER ? K_WATER : id == BLOCK_LAVA ? K_LAVA : -1; }

/* meta of the same-kind fluid at x,y,z or -1; unknown fluid blocks (world gen) are adopted as sources */
static int meta_at(int x, int y, int z, int kind)
{
    if (blk(x, y, z) != KIND_BLOCK[kind]) return -1;
    Cell* c = find(x, y, z);
    if (!c) c = insert(x, y, z, kind, 0);
    return c ? c->meta : 0;
}

/* same, but never touches the table (used while meshing) */
static int meta_peek(int x, int y, int z, int kind)
{
    if (blk(x, y, z) != KIND_BLOCK[kind]) return -1;
    Cell* c = find(x, y, z);
    return c ? c->meta : 0;
}

/* does block b stop this kind of fluid? (Minecraft isBlocked) */
static int blocked_for(int kind, int b)
{
    if (b == BLOCK_AIR) return 0;
    if (b < 0) return 1;
    if (b == KIND_BLOCK[kind]) return 0;
    if (b == BLOCK_WATER || b == BLOCK_LAVA) return kind == K_WATER;   /* lava may displace water, not the reverse */
    return 1;
}

static int can_flow_into(int kind, int b)
{
    return b == BLOCK_AIR || (kind == K_LAVA && b == BLOCK_WATER);
}

static void set_fluid(int x, int y, int z, int kind, int meta)
{
    world_set_block(x, y, z, KIND_BLOCK[kind]);
    if (!insert(x, y, z, kind, meta)) return;
    schedule(x, y, z);
    notify_neighbours(x, y, z);
    dirty = 1;
}

static void clear_cell(int x, int y, int z, int replacement)
{
    Cell* c = find(x, y, z);
    if (c) erase(c);
    world_set_block(x, y, z, replacement);
    notify_neighbours(x, y, z);
    dirty = 1;
}

static void flow_into(int x, int y, int z, int kind, int meta)
{
    if (!world_in_bounds(x, y, z)) return;
    int b = world_get_block(x, y, z);
    if (b == BLOCK_AIR)
        set_fluid(x, y, z, kind, meta);
    else if (kind == K_LAVA && b == BLOCK_WATER)
        clear_cell(x, y, z, BLOCK_STONE);          /* no obsidian/cobble block in the game: stone */
}

/* ------------------------------------------------------------ flow rules */

static const int DX[4] = { 1, -1, 0, 0 };
static const int DZ[4] = { 0, 0, 1, -1 };
static const int OPP[4] = { 1, 0, 3, 2 };

static int passable(int kind, int x, int y, int z)
{
    int b = blk(x, y, z);
    if (blocked_for(kind, b)) return 0;
    if (b == KIND_BLOCK[kind] && meta_at(x, y, z, kind) == 0) return 0;   /* sources are not walked through */
    return 1;
}

static int flow_cost(int kind, int x, int y, int z, int depth, int from)
{
    int best = 1000;
    for (int d = 0; d < 4; d++) {
        if (d == from) continue;
        const int nx = x + DX[d], nz = z + DZ[d];
        if (!passable(kind, nx, y, nz)) continue;
        if (!blocked_for(kind, blk(nx, y - 1, nz))) return depth;
        if (depth < 4) {
            int c = flow_cost(kind, nx, y, nz, depth + 1, OPP[d]);
            if (c < best) best = c;
        }
    }
    return best;
}

static int touches_water(int x, int y, int z)
{
    return blk(x + 1, y, z) == BLOCK_WATER || blk(x - 1, y, z) == BLOCK_WATER ||
        blk(x, y, z + 1) == BLOCK_WATER || blk(x, y, z - 1) == BLOCK_WATER ||
        blk(x, y + 1, z) == BLOCK_WATER;
}

static void cell_tick(int x, int y, int z)
{
    Cell* c = find(x, y, z);
    if (!c) return;
    c->pending = 0;

    const int kind = c->kind;

    if (blk(x, y, z) != KIND_BLOCK[kind]) {        /* replaced behind our back */
        erase(c);
        dirty = 1;
        return;
    }

    if (kind == K_LAVA && touches_water(x, y, z)) {
        clear_cell(x, y, z, BLOCK_STONE);
        return;
    }

    const int decay = KIND_DECAY[kind];
    int i = c->meta;

    if (i > 0) {
        /* work out what level this cell should have from its neighbours */
        int best = -100, sources = 0;
        for (int d = 0; d < 4; d++) {
            int m = meta_at(x + DX[d], y, z + DZ[d], kind);
            if (m < 0) continue;
            if (m == 0) sources++;
            if (m >= 8) m = 0;
            if (!(best >= 0 && m >= best)) best = m;
        }

        int j = best + decay;
        if (j >= 8 || best < 0) j = -1;

        int above = meta_at(x, y + 1, z, kind);
        if (above >= 0) j = above >= 8 ? above : above + 8;       /* stream coming from above */

        if (sources >= 2 && kind == K_WATER) {                    /* infinite water */
            int below = blk(x, y - 1, z);
            if (below > BLOCK_AIR && !fluid_is_fluid(below)) j = 0;
            else if (meta_at(x, y - 1, z, K_WATER) == 0) j = 0;
        }

        if (j != i) {
            if (j < 0) {                                          /* dried up */
                clear_cell(x, y, z, BLOCK_AIR);
                return;
            }
            c = find(x, y, z);
            if (!c) return;
            c->meta = (unsigned char)j;
            schedule(x, y, z);
            notify_neighbours(x, y, z);
            dirty = 1;
            i = j;
        }
    }

    /* spread: down first, sideways only when standing on something */
    const int below = blk(x, y - 1, z);

    if (can_flow_into(kind, below)) {
        flow_into(x, y - 1, z, kind, i >= 8 ? i : i + 8);
    }
    else if (i == 0 || blocked_for(kind, below)) {
        int k = i + decay;
        if (i >= 8) k = 1;
        if (k >= 8) return;

        int cost[4], lo = 1000;
        for (int d = 0; d < 4; d++) {
            cost[d] = 1000;
            const int nx = x + DX[d], nz = z + DZ[d];
            if (!passable(kind, nx, y, nz)) continue;
            cost[d] = !blocked_for(kind, blk(nx, y - 1, nz)) ? 0 : flow_cost(kind, nx, y, nz, 1, OPP[d]);
            if (cost[d] < lo) lo = cost[d];
        }
        for (int d = 0; d < 4; d++)
            if (cost[d] == lo)
                flow_into(x + DX[d], y, z + DZ[d], kind, k);
    }
}

void fluid_block_changed(int x, int y, int z)
{
    if (!map) return;
    int b = blk(x, y, z);
    int k = kind_of(b);
    Cell* c = find(x, y, z);

    if (k >= 0) { if (!c || c->kind != k) insert(x, y, z, k, 0); }   /* placed by hand = source */
    else if (c) erase(c);

    schedule(x, y, z);
    notify_neighbours(x, y, z);
    dirty = 1;
}

void fluid_tick(void)
{
    if (!map) return;
    now_tick++;

    for (int k = 0; k < 2; k++) {
        Queue* q = &queues[k];
        int n = 0;
        while (q->head != q->tail && n < MAX_EVENTS_PER_TICK) {
            Ev e = q->a[q->head & (QUEUE_CAP - 1)];
            if (e.due > now_tick) break;
            q->head++;
            n++;
            cell_tick(e.x, e.y, e.z);
        }
    }
}

/* ------------------------------------------------------------ meshing */

typedef struct { float* v; int n; } Buf;     /* n = vertices, 6 floats each: pos3 uv2 shade */

static Buf  bufs[2];
static float last_cam[3];

/* uvtype: 0 = u:x v:z (top/bottom)   1 = u:x v:y (+-Z faces)   2 = u:z v:y (+-X faces) */
static void emit(Buf* b, const float P[4][3], float shade, int uvtype)
{
    static const int I[6] = { 0, 1, 2, 0, 2, 3 };
    if (b->n + 6 > MAX_VERTS) return;
    for (int k = 0; k < 6; k++) {
        const float* p = P[I[k]];
        float* o = b->v + (size_t)b->n * 6;
        o[0] = p[0]; o[1] = p[1]; o[2] = p[2];
        o[3] = uvtype == 2 ? p[2] : p[0];
        o[4] = uvtype == 0 ? p[2] : p[1];
        o[5] = shade;
        b->n++;
    }
}

/* Minecraft style: the surface height at a block corner is the weighted average of the 4 cells touching it,
   so neighbouring cells always agree and the surface has no gaps. */
static float corner_height(int x, int y, int z, int dx, int dz, int kind)
{
    float sum = 0.0f;
    int   w = 0;
    for (int ox = dx - 1; ox <= dx; ox++) {
        for (int oz = dz - 1; oz <= dz; oz++) {
            const int cx = x + ox, cz = z + oz;
            if (meta_peek(cx, y + 1, cz, kind) >= 0) return 1.0f;
            int m = meta_peek(cx, y, cz, kind);
            if (m >= 0) {
                const float empty = ((m >= 8 ? 0 : m) + 1) / 9.0f;
                if (m == 0 || m >= 8) { sum += empty * 10.0f; w += 10; }   /* sources / streams weigh 11x */
                sum += empty;
                w++;
            }
            else if (!blocked_for(kind, blk(cx, y, cz))) {
                sum += 1.0f;           /* open air counts as completely empty: pulls the edge down */
                w++;
            }
        }
    }
    return w ? 1.0f - sum / (float)w : 0.0f;
}

static int side_visible(int kind, int x, int y, int z)
{
    int b = blk(x, y, z);
    if (b < 0) return 0;
    if (b == BLOCK_AIR) return 1;
    if (b == KIND_BLOCK[kind]) return 0;
    return block_definition(b)->transparent;
}

static void build_cell(const Cell* c)
{
    const int kind = c->kind, x = c->x, y = c->y, z = c->z;
    Buf* b = &bufs[kind];
    const int lava = kind == K_LAVA;

    float H[2][2];
    for (int dx = 0; dx < 2; dx++)
        for (int dz = 0; dz < 2; dz++)
            H[dx][dz] = corner_height(x, y, z, dx, dz, kind);

    const float fx = (float)x, fy = (float)y, fz = (float)z;

    if (meta_peek(x, y + 1, z, kind) < 0) {
        const float P[4][3] = { {fx, fy + H[0][0], fz}, {fx + 1, fy + H[1][0], fz},
                                {fx + 1, fy + H[1][1], fz + 1}, {fx, fy + H[0][1], fz + 1} };
        emit(b, P, 1.0f, 0);
    }

    if (y > 0 && side_visible(kind, x, y - 1, z) && meta_peek(x, y - 1, z, kind) < 0) {
        const float P[4][3] = { {fx, fy, fz}, {fx + 1, fy, fz}, {fx + 1, fy, fz + 1}, {fx, fy, fz + 1} };
        emit(b, P, lava ? 1.0f : 0.75f, 0);
    }

    if (side_visible(kind, x, y, z + 1)) {
        const float P[4][3] = { {fx, fy, fz + 1}, {fx + 1, fy, fz + 1},
                                {fx + 1, fy + H[1][1], fz + 1}, {fx, fy + H[0][1], fz + 1} };
        emit(b, P, lava ? 1.0f : 0.95f, 1);
    }
    if (side_visible(kind, x, y, z - 1)) {
        const float P[4][3] = { {fx + 1, fy, fz}, {fx, fy, fz},
                                {fx, fy + H[0][0], fz}, {fx + 1, fy + H[1][0], fz} };
        emit(b, P, lava ? 1.0f : 0.95f, 1);
    }
    if (side_visible(kind, x - 1, y, z)) {
        const float P[4][3] = { {fx, fy, fz}, {fx, fy, fz + 1},
                                {fx, fy + H[0][1], fz + 1}, {fx, fy + H[0][0], fz} };
        emit(b, P, lava ? 1.0f : 0.88f, 2);
    }
    if (side_visible(kind, x + 1, y, z)) {
        const float P[4][3] = { {fx + 1, fy, fz + 1}, {fx + 1, fy, fz},
                                {fx + 1, fy + H[1][0], fz}, {fx + 1, fy + H[1][1], fz + 1} };
        emit(b, P, lava ? 1.0f : 0.88f, 2);
    }
}

/* ------------------------------------------------------------ rendering */

static GLuint prog, vao, vbo, fluid_tex;
static GLint  u_vp, u_rmin, u_rsize, u_alpha, u_time, u_scroll, u_dual;

static const char* VS =
"#version 330 core\n"
"layout(location=0) in vec3 p;\n"
"layout(location=1) in vec2 uv;\n"
"layout(location=2) in float shade;\n"
"uniform mat4 vp;\n"
"out vec2 v_uv; out float v_shade;\n"
"void main(){ v_uv = uv; v_shade = shade; gl_Position = vp * vec4(p, 1.0); }\n";

/* uv is in world units: fract() tiles one 16x16 tile of Fluids.png over every block face */
static const char* FS =
"#version 330 core\n"
"in vec2 v_uv; in float v_shade;\n"
"uniform sampler2D tex;\n"
"uniform vec2 rmin; uniform vec2 rsize;\n"
"uniform float alpha;\n"
"uniform float time;\n"
"uniform vec2 scroll;\n"
"uniform float dual;\n"
"out vec4 color;\n"
"vec4 tile(vec2 uv){ return textureLod(tex, rmin + fract(uv) * rsize, 0.0); }\n"
"void main(){\n"
"  vec2 uv = v_uv - scroll * time;\n"
"  vec4 c = tile(uv);\n"
"  if (dual > 0.5) c = 0.5 * (c + tile(v_uv * 1.0 + vec2(scroll.y, -scroll.x) * 0.7 * time + vec2(0.37, 0.21)));\n"
"  color = vec4(c.rgb * v_shade, alpha);\n"
"}\n";

static GLuint compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "fluid shader: %s\n", log);
    }
    return s;
}

static GLuint ov_prog, ov_vao, ov_vbo;
static GLint  ov_rmin, ov_rsize, ov_color;
static void overlay_init(void);

void fluid_init(GLuint tex)
{
    fluid_tex = tex;
    map = (Cell*)calloc(MAP_SIZE, sizeof(Cell));
    cell_count = 0;
    now_tick = 0;
    for (int k = 0; k < 2; k++) {
        queues[k].a = (Ev*)malloc(QUEUE_CAP * sizeof(Ev));
        queues[k].head = queues[k].tail = 0;
        bufs[k].v = (float*)malloc((size_t)MAX_VERTS * 6 * sizeof(float));
        bufs[k].n = 0;
    }

    GLuint vs = compile(GL_VERTEX_SHADER, VS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS);
    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_vp = glGetUniformLocation(prog, "vp");
    u_rmin = glGetUniformLocation(prog, "rmin");
    u_rsize = glGetUniformLocation(prog, "rsize");
    u_alpha = glGetUniformLocation(prog, "alpha");
    u_time = glGetUniformLocation(prog, "time");
    u_scroll = glGetUniformLocation(prog, "scroll");
    u_dual = glGetUniformLocation(prog, "dual");

    GLint prev;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);
    glUseProgram((GLuint)prev);

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    overlay_init();
}

void fluid_shutdown(void)
{
    free(map); map = NULL;
    for (int k = 0; k < 2; k++) { free(queues[k].a); queues[k].a = NULL; free(bufs[k].v); bufs[k].v = NULL; }
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(prog);
    glDeleteBuffers(1, &ov_vbo);
    glDeleteVertexArrays(1, &ov_vao);
    glDeleteProgram(ov_prog);
}

static void rebuild(const float* cam)
{
    bufs[0].n = bufs[1].n = 0;

    for (int i = 0; i < MAP_SIZE; i++) {
        const Cell* c = &map[i];
        if (!c->used) continue;
        if (fabsf((float)c->x - cam[0]) > DRAW_RADIUS || fabsf((float)c->z - cam[2]) > DRAW_RADIUS) continue;
        if (blk(c->x, c->y, c->z) != KIND_BLOCK[c->kind]) continue;
        build_cell(c);
    }

    const size_t lava_bytes = (size_t)bufs[K_LAVA].n * 6 * sizeof(float);
    const size_t water_bytes = (size_t)bufs[K_WATER].n * 6 * sizeof(float);

    /* buffer layout: lava (opaque) first, then water (blended) */
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(lava_bytes + water_bytes), NULL, GL_DYNAMIC_DRAW);
    if (lava_bytes)  glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)lava_bytes, bufs[K_LAVA].v);
    if (water_bytes) glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)lava_bytes, (GLsizeiptr)water_bytes, bufs[K_WATER].v);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    last_cam[0] = cam[0]; last_cam[1] = cam[1]; last_cam[2] = cam[2];
    dirty = 0;
}

void fluid_draw(const float* vp, const float* cam)
{
    if (!map) return;

    if (dirty || fabsf(cam[0] - last_cam[0]) > 16.0f || fabsf(cam[2] - last_cam[2]) > 16.0f)
        rebuild(cam);

    const int n_lava = bufs[K_LAVA].n, n_water = bufs[K_WATER].n;
    if (!n_lava && !n_water) return;

    GLint prev_prog;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    const GLboolean blend = glIsEnabled(GL_BLEND);

    glDisable(GL_CULL_FACE);
    glUseProgram(prog);
    glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fluid_tex);
    glBindVertexArray(vao);
    glUniform1f(u_time, (float)glfwGetTime());

    if (n_lava) {
        glDisable(GL_BLEND);
        glUniform2fv(u_rmin, 1, TILE_MIN[K_LAVA]);
        glUniform2fv(u_rsize, 1, TILE_SIZE[K_LAVA]);
        glUniform1f(u_alpha, KIND_ALPHA[K_LAVA]);
        glUniform2f(u_scroll, 0.0f, 0.02f);     /* lava creeps slowly */
        glUniform1f(u_dual, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, n_lava);
    }
    if (n_water) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUniform2fv(u_rmin, 1, TILE_MIN[K_WATER]);
        glUniform2fv(u_rsize, 1, TILE_SIZE[K_WATER]);
        glUniform1f(u_alpha, KIND_ALPHA[K_WATER]);
        glUniform2f(u_scroll, 0.04f, 0.06f);    /* two layers drifting past each other */
        glUniform1f(u_dual, 1.0f);
        glDrawArrays(GL_TRIANGLES, n_lava, n_water);
        glDepthMask(GL_TRUE);
    }

    glBindVertexArray(0);
    if (cull) glEnable(GL_CULL_FACE);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glUseProgram((GLuint)prev_prog);
}

/* ------------------------------------------------------------ queries */

float fluid_height(int x, int y, int z)
{
    int k = kind_of(blk(x, y, z));
    if (k < 0) return -1.0f;
    if (meta_peek(x, y + 1, z, k) >= 0) return 1.0f;       /* fluid on top: full cell */
    int m = meta_peek(x, y, z, k);
    return 1.0f - ((m >= 8 ? 0 : m) + 1) / 9.0f;
}

static int ray_solid(int x, int y, int z)
{
    int b = blk(x, y, z);
    return b > BLOCK_AIR && !fluid_is_fluid(b);
}

/* voxel walk (Amanatides & Woo) that ignores air and fluids */
int fluid_raycast(const float* o, const float* d, float reach, int* hit, int* prev)
{
    int cell[3] = { (int)floorf(o[0]), (int)floorf(o[1]), (int)floorf(o[2]) };
    int last[3] = { cell[0], cell[1], cell[2] };
    int step[3];
    double tmax[3], tdelta[3];

    for (int i = 0; i < 3; i++) {
        if (d[i] > 0.0f) { step[i] = 1;  tdelta[i] = 1.0 / d[i];  tmax[i] = (cell[i] + 1 - o[i]) * tdelta[i]; }
        else if (d[i] < 0.0f) { step[i] = -1; tdelta[i] = -1.0 / d[i]; tmax[i] = (o[i] - cell[i]) * tdelta[i]; }
        else { step[i] = 0;  tdelta[i] = 1e30;         tmax[i] = 1e30; }
    }

    for (;;) {
        if (ray_solid(cell[0], cell[1], cell[2])) {
            hit[0] = cell[0]; hit[1] = cell[1]; hit[2] = cell[2];
            prev[0] = last[0]; prev[1] = last[1]; prev[2] = last[2];
            return 1;
        }

        int a = (tmax[0] < tmax[1]) ? (tmax[0] < tmax[2] ? 0 : 2) : (tmax[1] < tmax[2] ? 1 : 2);
        if (tmax[a] > reach) return 0;

        last[0] = cell[0]; last[1] = cell[1]; last[2] = cell[2];
        cell[a] += step[a];
        tmax[a] += tdelta[a];
    }
}

/* ------------------------------------------------------------ screen overlay */


static const char* OV_VS =
"#version 330 core\n"
"layout(location=0) in vec2 p;\n"
"layout(location=1) in vec2 uv;\n"
"out vec2 v_uv;\n"
"void main(){ v_uv = uv; gl_Position = vec4(p, 0.0, 1.0); }\n";

static const char* OV_FS =
"#version 330 core\n"
"in vec2 v_uv;\n"
"uniform sampler2D tex;\n"
"uniform vec2 rmin; uniform vec2 rsize;\n"
"uniform vec4 color;\n"
"out vec4 out_color;\n"
"void main(){ vec4 c = textureLod(tex, rmin + fract(v_uv) * rsize, 0.0); out_color = vec4(c.rgb * color.rgb, color.a); }\n";

static void overlay_init(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, OV_VS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, OV_FS);
    ov_prog = glCreateProgram();
    glAttachShader(ov_prog, vs);
    glAttachShader(ov_prog, fs);
    glLinkProgram(ov_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    ov_rmin = glGetUniformLocation(ov_prog, "rmin");
    ov_rsize = glGetUniformLocation(ov_prog, "rsize");
    ov_color = glGetUniformLocation(ov_prog, "color");

    GLint prev;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev);
    glUseProgram(ov_prog);
    glUniform1i(glGetUniformLocation(ov_prog, "tex"), 0);
    glUseProgram((GLuint)prev);

    glGenVertexArrays(1, &ov_vao);
    glGenBuffers(1, &ov_vbo);
    glBindVertexArray(ov_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ov_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void fluid_draw_overlay(int kind, float yaw_deg, float pitch_deg, int fb_w, int fb_h)
{
    if (!map || !ov_prog || fb_h <= 0) return;

    /* the texture is tiled 4x over the screen height and slides with the head, like Minecraft's overlay */
    const float aspect = (float)fb_w / (float)fb_h;
    const float tiles = 2.0f;                      /* ndc -1..1 spans 2 * tiles = 4 tiles */
    const float ox = yaw_deg / 64.0f, oy = -pitch_deg / 64.0f;
    const float x0 = -aspect * tiles + ox, x1 = aspect * tiles + ox;
    const float y0 = -tiles + oy, y1 = tiles + oy;

    const float v[6 * 4] = {
        -1, -1, x0, y0,   1, -1, x1, y0,   1, 1, x1, y1,
        -1, -1, x0, y0,   1,  1, x1, y1,  -1, 1, x0, y1,
    };

    GLint prev_prog;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    const GLboolean blend = glIsEnabled(GL_BLEND);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(ov_prog);
    glUniform2fv(ov_rmin, 1, TILE_MIN[kind]);
    glUniform2fv(ov_rsize, 1, TILE_SIZE[kind]);
    if (kind == K_LAVA) glUniform4f(ov_color, 1.0f, 0.85f, 0.85f, 0.85f);
    else                glUniform4f(ov_color, 0.65f, 0.78f, 1.0f, 0.55f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fluid_tex);
    glBindVertexArray(ov_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ov_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof v, v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (!blend) glDisable(GL_BLEND);
    glUseProgram((GLuint)prev_prog);
}