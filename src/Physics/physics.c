#include "physics.h"
#include "fluid.h"
#include "Chunk Generation/chunkGenerator.h"
#include "Chunk Generation/chunkManager.h"

#define FALL_DELAY_TICKS   2
#define GRAVITY            0.04  // same as minecraft lawl
#define DRAG               0.98
#define MAX_FALLING        512
#define MAX_SCHEDULED      8192
#define MAX_LIFE_TICKS     600

static int is_gravity_block(int t) { return t == BLOCK_SAND; }

static int is_replaceable(int t) { return t == BLOCK_AIR || t == BLOCK_WATER || t == BLOCK_LAVA; }

static int can_fall(int x, int y, int z)
{
    if (!world_in_bounds(x, y - 1, z)) return 0;
    return is_replaceable(world_get_block(x, y - 1, z));
}

typedef struct {
    int    alive;
    int    type;
    double x, y, z;
    double prev_y;
    double vy;
    int    age;
} Falling;

static Falling falling[MAX_FALLING];
static int     falling_n;

typedef struct { int x, y, z, delay; } Sched;

static Sched sched[MAX_SCHEDULED];
static int   sched_n;
static Sched due[MAX_SCHEDULED];

static void schedule(int x, int y, int z, int delay)
{
    for (int i = 0; i < sched_n; i++)
        if (sched[i].x == x && sched[i].y == y && sched[i].z == z)
            return;
    if (sched_n >= MAX_SCHEDULED) return;  // ???? silently drops the tick if the list is full
    sched[sched_n++] = (Sched){ x, y, z, delay };
}

void physics_block_changed(int x, int y, int z)
{
    fluid_block_changed(x, y, z);

    if (world_in_bounds(x, y, z) && is_gravity_block(world_get_block(x, y, z)))
        schedule(x, y, z, FALL_DELAY_TICKS);
    if (world_in_bounds(x, y + 1, z) && is_gravity_block(world_get_block(x, y + 1, z)))
        schedule(x, y + 1, z, FALL_DELAY_TICKS);
}

static void spawn_falling(int x, int y, int z, int type)
{
    for (int i = 0; i < MAX_FALLING; i++) {
        if (falling[i].alive) continue;
        falling[i] = (Falling){ 1, type, x + 0.5, (double)y, z + 0.5, (double)y, 0.0, 0 };
        falling_n++;

        world_set_block(x, y, z, BLOCK_AIR);
        physics_block_changed(x, y, z);
        return;
    }
}

static void run_scheduled(int x, int y, int z)
{
    if (!world_in_bounds(x, y, z)) return;
    int t = world_get_block(x, y, z);
    if (!is_gravity_block(t)) return;
    if (can_fall(x, y, z))
        spawn_falling(x, y, z, t);
}

static void land(Falling* f, int cy)
{
    const int cx = (int)floor(f->x);
    const int cz = (int)floor(f->z);

    f->alive = 0;
    falling_n--;

    if (!world_in_bounds(cx, cy, cz) || !is_replaceable(world_get_block(cx, cy, cz)))
        return;

    world_set_block(cx, cy, cz, f->type);
    audio_play_3d(snd_pick(snd_place[f->type], snd_place_n[f->type]),
        cx + 0.5, cy + 0.5, cz + 0.5, 0.6f);

    physics_block_changed(cx, cy, cz);
}

static void tick_falling(Falling* f)
{
    f->prev_y = f->y;
    f->age++;

    f->vy -= GRAVITY;
    const double ny = f->y + f->vy;

    const int cx = (int)floor(f->x);
    const int cz = (int)floor(f->z);

    const int from = (int)floor(f->y) - 1;
    const int to = (int)floor(ny);

    for (int c = from; c >= to; c--) {  // check every cell we pass through this tick
        int solid;
        if (c < 0 || !world_in_bounds(cx, c, cz))
            solid = (c < 0);
        else
            solid = !is_replaceable(world_get_block(cx, c, cz));

        if (solid) {
            f->y = c + 1;
            land(f, c + 1);
            return;
        }
    }

    f->y = ny;
    f->vy *= DRAG;

    if (f->y < -64.0 || f->age > MAX_LIFE_TICKS) {  // fell out of the world or lived too long, kill it
        f->alive = 0;
        falling_n--;
    }
}

#define MAX_ITEMS        512
#define ITEM_SIZE        0.25
#define ITEM_LIFE_TICKS  6000

typedef struct {
    int    alive, type, count, age, delay;
    double x, y, z;
    double px, py, pz;
    double vx, vy, vz;
    int    on_ground;
    float  phase;
} Item;

static Item items[MAX_ITEMS];
static int  item_n;

static int cell_solid(int x, int y, int z)
{
    if (y < 0) return 1;
    if (!world_in_bounds(x, y, z)) return 0;
    int b = world_get_block(x, y, z);
    return b != BLOCK_AIR && !fluid_is_fluid(b);
}

static int box_hits(double x, double y, double z)
{
    const double h = ITEM_SIZE * 0.5;
    const int x0 = (int)floor(x - h), x1 = (int)floor(x + h);
    const int y0 = (int)floor(y), y1 = (int)floor(y + ITEM_SIZE);
    const int z0 = (int)floor(z - h), z1 = (int)floor(z + h);
    for (int yy = y0; yy <= y1; yy++)
        for (int zz = z0; zz <= z1; zz++)
            for (int xx = x0; xx <= x1; xx++)
                if (cell_solid(xx, yy, zz)) return 1;
    return 0;
}

void physics_spawn_item(int type, int count, double x, double y, double z,
    double vx, double vy, double vz, int pickup_delay)
{
    if (type <= BLOCK_AIR || type >= BLOCK_TYPES || count <= 0) return;
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (items[i].alive) continue;
        items[i] = (Item){ 1, type, count, 0, pickup_delay, x, y, z, x, y, z, vx, vy, vz, 0,
                           (float)(rand() % 628) / 100.0f };  // random 0..6.28 so items don't spin in sync
        item_n++;
        return;
    }
}

static void tick_item(Item* it)
{
    it->px = it->x; it->py = it->y; it->pz = it->z;
    it->age++;
    if (it->delay > 0) it->delay--;
    if (it->age > ITEM_LIFE_TICKS || it->y < -64.0) { it->alive = 0; item_n--; return; }

    it->vy -= GRAVITY;

    double nx = it->x + it->vx;
    if (box_hits(nx, it->y, it->z)) it->vx = 0.0; else it->x = nx;

    double nz = it->z + it->vz;
    if (box_hits(it->x, it->y, nz)) it->vz = 0.0; else it->z = nz;

    double ny = it->y + it->vy;
    it->on_ground = 0;
    if (box_hits(it->x, ny, it->z)) {
        if (it->vy < 0.0) {
            it->y = floor(ny) + 1.0;
            it->on_ground = 1;
        }
        it->vy = 0.0;
    }
    else {
        it->y = ny;
    }

    double fr = 0.98;
    if (it->on_ground) fr = 0.6 * 0.98;
    it->vx *= fr;
    it->vz *= fr;
    it->vy *= 0.98;
    if (it->on_ground) it->vy *= -0.5;
}

static void tick_items(void)
{
    for (int i = 0; i < MAX_ITEMS; i++)
        if (items[i].alive) tick_item(&items[i]);
}

void physics_collect_items(double px, double py, double pz, int (*take)(int type, int count))
{
    if (!take) return;
    for (int i = 0; i < MAX_ITEMS; i++) {
        Item* it = &items[i];
        if (!it->alive || it->delay > 0) continue;
        if (fabs(it->x - px) > 1.0 || fabs(it->z - pz) > 1.0) continue;
        if (it->y < py - 0.5 || it->y > py + 1.9) continue;

        int got = take(it->type, it->count);
        if (got <= 0) continue;
        if (got >= it->count) { it->alive = 0; item_n--; }
        else it->count -= got;
    }
}

int physics_item_count(void) { return item_n; }

void physics_tick(void)
{
    for (int i = 0; i < MAX_FALLING; i++)
        if (falling[i].alive)
            tick_falling(&falling[i]);

    int nd = 0, keep = 0;
    for (int i = 0; i < sched_n; i++) {
        if (--sched[i].delay <= 0) due[nd++] = sched[i];
        else                       sched[keep++] = sched[i];
    }
    sched_n = keep;

    for (int i = 0; i < nd; i++)
        run_scheduled(due[i].x, due[i].y, due[i].z);

    tick_items();
}

int physics_falling_count(void) { return falling_n; }

static GLuint prog, vao, vbo;
static GLint  u_vp, u_pos, u_scale, u_yaw;
static int    ftex[BLOCK_TYPES][BLOCK_FACE_COUNT];

static const char* VS =
"#version 330 core\n"
"layout(location=0) in vec3 p;\n"
"layout(location=1) in vec2 uv;\n"
"layout(location=2) in float shade;\n"
"uniform mat4 vp;\n"
"uniform vec3 pos;\n"
"uniform float scale;\n"
"uniform float yaw;\n"
"out vec2 v_uv; out float v_shade;\n"
"void main(){ v_uv = uv; v_shade = shade; vec3 q = p * scale; float c = cos(yaw), s = sin(yaw); q = vec3(c*q.x + s*q.z, q.y, -s*q.x + c*q.z); gl_Position = vp * vec4(q + pos, 1.0); }\n";

static const char* FS =
"#version 330 core\n"
"in vec2 v_uv; in float v_shade;\n"
"uniform sampler2D tex;\n"
"out vec4 color;\n"
"void main(){ vec4 c = texture(tex, v_uv); if (c.a < 0.1) discard; color = vec4(c.rgb * v_shade, c.a); }\n";

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
        fprintf(stderr, "physics shader: %s\n", log);  // ???? keeps going even if the shader failed to compile
    }
    return s;
}

void physics_init(int face_tex[][BLOCK_FACE_COUNT])
{
    memset(falling, 0, sizeof falling);
    falling_n = 0;
    sched_n = 0;
    memset(items, 0, sizeof items);
    item_n = 0;

    for (int t = 0; t < BLOCK_TYPES; t++)
        for (int f = 0; f < BLOCK_FACE_COUNT; f++)
            ftex[t][f] = face_tex[t][f];

    GLuint vs = compile(GL_VERTEX_SHADER, VS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS);
    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_vp = glGetUniformLocation(prog, "vp");
    u_pos = glGetUniformLocation(prog, "pos");
    u_scale = glGetUniformLocation(prog, "scale");
    u_yaw = glGetUniformLocation(prog, "yaw");

    GLint prev;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);
    glUseProgram((GLuint)prev);

    static const float corner[6][4][3] = {
        { {0,0,1},{1,0,1},{1,1,1},{0,1,1} },
        { {1,0,0},{0,0,0},{0,1,0},{1,1,0} },
        { {0,0,0},{0,0,1},{0,1,1},{0,1,0} },
        { {1,0,1},{1,0,0},{1,1,0},{1,1,1} },
        { {0,1,1},{1,1,1},{1,1,0},{0,1,0} },
        { {0,0,0},{1,0,0},{1,0,1},{0,0,1} },
    };
    static const float shade[6] = { 0.8f, 0.8f, 0.6f, 0.6f, 1.0f, 0.5f };
    static const int   idx[6] = { 0, 1, 2, 0, 2, 3 };

    float data[6 * 6 * 6];  // 6 faces * 6 verts * 6 floats
    int n = 0;
    for (int f = 0; f < 6; f++) {
        for (int k = 0; k < 6; k++) {
            const int c = idx[k];
            data[n++] = corner[f][c][0] - 0.5f;
            data[n++] = corner[f][c][1];
            data[n++] = corner[f][c][2] - 0.5f;
            const float cx = corner[f][c][0], cy = corner[f][c][1], cz = corner[f][c][2];
            data[n++] = (f >= 2 && f <= 3) ? cz : cx;
            data[n++] = (f >= 4) ? cz : cy;
            data[n++] = shade[f];
        }
    }

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof data, data, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void physics_shutdown(void)
{
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(prog);
}

void physics_draw(const float* vp, double alpha, const GLuint* tex_ids)
{
    if (falling_n <= 0 && item_n <= 0) return;

    GLint prev_prog;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    glUseProgram(prog);
    glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);

    for (int i = 0; i < MAX_FALLING; i++) {
        const Falling* f = &falling[i];
        if (!f->alive) continue;

        const double y = f->prev_y + (f->y - f->prev_y) * alpha;
        glUniform3f(u_pos, (float)f->x, (float)y, (float)f->z);
        glUniform1f(u_scale, 1.0f);
        glUniform1f(u_yaw, 0.0f);

        for (int face = 0; face < 6; face++) {
            const int t = ftex[f->type][face];
            if (t < 0) continue;
            glBindTexture(GL_TEXTURE_2D, tex_ids[t]);
            glDrawArrays(GL_TRIANGLES, face * 6, 6);
        }
    }

    for (int i = 0; i < MAX_ITEMS; i++) {
        const Item* it = &items[i];
        if (!it->alive) continue;

        const double x = it->px + (it->x - it->px) * alpha;
        const double yy = it->py + (it->y - it->py) * alpha;
        const double z = it->pz + (it->z - it->pz) * alpha;
        const float  t = (float)(it->age + alpha);
        const float  bob = sinf(t / 10.0f + it->phase) * 0.1f + 0.1f;

        glUniform3f(u_pos, (float)x, (float)yy + bob, (float)z);
        glUniform1f(u_scale, 0.25f);
        glUniform1f(u_yaw, t / 20.0f + it->phase);

        for (int face = 0; face < 6; face++) {
            const int tx = ftex[it->type][face];
            if (tx < 0) continue;
            glBindTexture(GL_TEXTURE_2D, tex_ids[tx]);
            glDrawArrays(GL_TRIANGLES, face * 6, 6);
        }
    }

    glBindVertexArray(0);
    if (cull) glEnable(GL_CULL_FACE);
    glUseProgram((GLuint)prev_prog);
}