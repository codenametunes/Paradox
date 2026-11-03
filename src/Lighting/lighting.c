#include "lighting.h"

#define CS       CHUNK_SIZE
#define CH       CHUNK_HEIGHT
#define PW       (CHUNK_SIZE + 2)
#define OPAQUE   15

enum { SKY = 0, BLK = 1 };

static const LightHost* host;

static unsigned char opacity_tab[256];
static unsigned char emit_tab[256];

static void init_block_tables(void)
{
    memset(opacity_tab, OPAQUE, sizeof opacity_tab);
    memset(emit_tab, 0, sizeof emit_tab);

    opacity_tab[BLOCK_AIR] = 0;
    opacity_tab[BLOCK_GLASS] = 0;
    opacity_tab[BLOCK_LEAVES] = 1;

}

static int chunk_of(int v)
{
    int q = v / CS;
    if (v % CS < 0) q--;
    return q;
}

static const int DIR[6][3] = {
    { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
    { 0, -1, 0 },
    { 0,  1, 0 },
};

static int block_at(int x, int y, int z)
{
    if (y < 0 || y >= CH) return BLOCK_AIR;
    int cx = chunk_of(x), cz = chunk_of(z);
    LightBlocks b = host->blocks(cx, cz);
    if (!b) return -1;
    return b[y][z - cz * CS][x - cx * CS];
}

static int lc_read(const LightChunk* lc, int ch, int y, int lz, int lx)
{
    if (y >= lc->h) return ch == SKY ? LIGHT_MAX : 0;
    unsigned char v = lc->data[((size_t)y * CS + lz) * CS + lx];
    return ch == SKY ? (v & 15) : (v >> 4);
}

static void lc_grow(LightChunk* lc, int y)
{
    int nh = ((y + 1) + 31) & ~31;
    if (nh > CH) nh = CH;
    if (nh <= lc->h) return;
    unsigned char* nd = (unsigned char*)realloc(lc->data, (size_t)nh * CS * CS);
    if (!nd) return;
    memset(nd + (size_t)lc->h * CS * CS, 0x0F, (size_t)(nh - lc->h) * CS * CS);
    lc->data = nd;
    lc->h = nh;
}

static void lc_write(LightChunk* lc, int ch, int y, int lz, int lx, int val)
{
    if (y >= lc->h) {
        if (val == (ch == SKY ? LIGHT_MAX : 0)) return;
        lc_grow(lc, y);
        if (y >= lc->h) return;
    }
    unsigned char* p = &lc->data[((size_t)y * CS + lz) * CS + lx];
    *p = ch == SKY ? (unsigned char)((*p & 0xF0) | val) : (unsigned char)((*p & 0x0F) | (val << 4));
}

static void mark_cell_dirty(int cx, int cz, int lx, int lz)
{
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx < 0 && lx != 0) continue;
            if (dx > 0 && lx != CS - 1) continue;
            if (dz < 0 && lz != 0) continue;
            if (dz > 0 && lz != CS - 1) continue;
            LightChunk* n = host->chunk(cx + dx, cz + dz);
            if (n) n->dirty = 1;
        }
}

static int light_get(int ch, int x, int y, int z)
{
    if (y < 0) return 0;
    if (y >= CH) return ch == SKY ? LIGHT_MAX : 0;
    int cx = chunk_of(x), cz = chunk_of(z);
    const LightChunk* lc = host->chunk(cx, cz);
    if (!lc) return -1;
    return lc_read(lc, ch, y, z - cz * CS, x - cx * CS);
}

static void light_set(int ch, int x, int y, int z, int val)
{
    if (y < 0 || y >= CH) return;
    int cx = chunk_of(x), cz = chunk_of(z);
    LightChunk* lc = host->chunk(cx, cz);
    if (!lc) return;
    int lx = x - cx * CS, lz = z - cz * CS;
    lc_write(lc, ch, y, lz, lx, val);
    mark_cell_dirty(cx, cz, lx, lz);
}

typedef struct { int x, y, z, lv; } Node;
typedef struct { Node* a; size_t n, cap; } Queue;

static Queue q_add, q_rem;

static void q_push(Queue* q, int x, int y, int z, int lv)
{
    if (q->n == q->cap) {
        size_t nc = q->cap ? q->cap * 2 : 4096;
        Node* na = (Node*)realloc(q->a, nc * sizeof(Node));
        if (!na) return;
        q->a = na;
        q->cap = nc;
    }
    q->a[q->n++] = (Node){ x, y, z, lv };
}

static int step_cost(int op) { return op > 1 ? op : 1; }

static int spread(int ch, int d, int from, int op)
{
    if (ch == SKY && d == 4 && from == LIGHT_MAX)
        return LIGHT_MAX - op;
    return from - step_cost(op);
}

static void propagate(int ch, Queue* q)
{
    for (size_t i = 0; i < q->n; i++) {
        const Node n = q->a[i];
        const int cur = light_get(ch, n.x, n.y, n.z);
        if (cur <= 1) continue;

        for (int d = 0; d < 6; d++) {
            const int nx = n.x + DIR[d][0], ny = n.y + DIR[d][1], nz = n.z + DIR[d][2];
            if (ny < 0 || ny >= CH) continue;

            const int b = block_at(nx, ny, nz);
            if (b < 0) continue;
            const int op = opacity_tab[b];
            if (op >= OPAQUE) continue;

            const int nl = spread(ch, d, cur, op);
            if (nl <= 0) continue;
            if (light_get(ch, nx, ny, nz) >= nl) continue;

            light_set(ch, nx, ny, nz, nl);
            q_push(q, nx, ny, nz, nl);
        }
    }
    q->n = 0;
}

static void remove_light(int ch, int x, int y, int z, int old, Queue* add)
{
    q_rem.n = 0;
    q_push(&q_rem, x, y, z, old);

    for (size_t i = 0; i < q_rem.n; i++) {
        const Node n = q_rem.a[i];

        for (int d = 0; d < 6; d++) {
            const int nx = n.x + DIR[d][0], ny = n.y + DIR[d][1], nz = n.z + DIR[d][2];
            if (ny < 0 || ny >= CH) continue;

            const int b = block_at(nx, ny, nz);
            if (b < 0) continue;

            const int nl = light_get(ch, nx, ny, nz);
            if (nl <= 0) continue;

            const int falling = (ch == SKY && d == 4 && n.lv == LIGHT_MAX);

            if (nl < n.lv || falling) {
                light_set(ch, nx, ny, nz, 0);
                q_push(&q_rem, nx, ny, nz, nl);

                if (ch == BLK && emit_tab[b]) {
                    light_set(BLK, nx, ny, nz, emit_tab[b]);
                    q_push(add, nx, ny, nz, emit_tab[b]);
                }
            }
            else {
                q_push(add, nx, ny, nz, nl);
            }
        }
    }
    q_rem.n = 0;
}

void lighting_block_changed(int x, int y, int z, int old_type, int new_type)
{
    (void)old_type;
    if (!host || y < 0 || y >= CH) return;

    const int cx = chunk_of(x), cz = chunk_of(z);
    if (!host->chunk(cx, cz)) return;

    mark_cell_dirty(cx, cz, x - cx * CS, z - cz * CS);

    const int op = opacity_tab[new_type & 255];

    for (int ch = 0; ch < 2; ch++) {
        q_add.n = 0;

        const int old = light_get(ch, x, y, z);
        if (old > 0) {
            light_set(ch, x, y, z, 0);
            remove_light(ch, x, y, z, old, &q_add);
        }

        int val = 0;
        if (ch == BLK && emit_tab[new_type & 255]) val = emit_tab[new_type & 255];

        if (op < OPAQUE) {
            for (int d = 0; d < 6; d++) {
                const int nx = x + DIR[d][0], ny = y + DIR[d][1], nz = z + DIR[d][2];
                const int nb = light_get(ch, nx, ny, nz);
                if (nb <= 0) continue;
                int cand;
                if (ch == SKY && d == 5 && nb == LIGHT_MAX) cand = LIGHT_MAX - op;
                else                                        cand = nb - step_cost(op);
                if (cand > val) val = cand;
            }
        }

        if (val > 0) {
            light_set(ch, x, y, z, val);
            q_push(&q_add, x, y, z, val);
        }

        propagate(ch, &q_add);
    }
}

static int seed_needed(const LightChunk* lc, LightBlocks B, int ch, int x, int y, int z, int lv)
{
    for (int d = 0; d < 6; d++) {
        const int nx = x + DIR[d][0], ny = y + DIR[d][1], nz = z + DIR[d][2];
        if (nx < 0 || nx >= CS || nz < 0 || nz >= CS) return 1;
        if (ny < 0 || ny >= CH) continue;
        const int op = opacity_tab[B[ny][nz][nx]];
        if (op >= OPAQUE) continue;
        const int nl = spread(ch, d, lv, op);
        if (nl > 0 && nl > lc_read(lc, ch, ny, nz, nx)) return 1;
    }
    return 0;
}

void lighting_chunk_generate(int cx, int cz)
{
    LightChunk* lc = host->chunk(cx, cz);
    LightBlocks B = host->blocks(cx, cz);
    if (!lc || !B) return;

    int ymax = host->max_y(cx, cz);
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!host->chunk(cx + dx, cz + dz)) continue;
            int m = host->max_y(cx + dx, cz + dz);
            if (m > ymax) ymax = m;
        }
    int top = ymax + 1;
    if (top > CH - 1) top = CH - 1;

    int h = ((top + 3) + 31) & ~31;
    if (h > CH) h = CH;

    free(lc->data);
    lc->data = (unsigned char*)malloc((size_t)h * CS * CS);
    lc->h = h;
    if (!lc->data) { lc->h = 0; return; }

    const int wx0 = cx * CS, wz0 = cz * CS;

    q_add.n = 0;
    static Queue q_blk;
    q_blk.n = 0;

    for (int z = 0; z < CS; z++)
        for (int x = 0; x < CS; x++) {
            int s = LIGHT_MAX;
            for (int y = h - 1; y >= 0; y--) {
                const int b = B[y][z][x];
                const int op = opacity_tab[b];
                int sky;
                if (op >= OPAQUE) { s = 0; sky = 0; }
                else {
                    if (op > 0) { s -= op; if (s < 0) s = 0; }
                    sky = s;
                }
                const int em = emit_tab[b];
                lc->data[((size_t)y * CS + z) * CS + x] = (unsigned char)(sky | (em << 4));
                if (em > 1) q_push(&q_blk, wx0 + x, y, wz0 + z, em);
            }
        }

    for (int y = 0; y <= top && y < h; y++)
        for (int z = 0; z < CS; z++)
            for (int x = 0; x < CS; x++) {
                const unsigned char v = lc->data[((size_t)y * CS + z) * CS + x];
                const int sky = v & 15, blk = v >> 4;
                if (sky > 1 && seed_needed(lc, B, SKY, x, y, z, sky))
                    q_push(&q_add, wx0 + x, y, wz0 + z, sky);
                if (blk > 1 && seed_needed(lc, B, BLK, x, y, z, blk) && blk != emit_tab[B[y][z][x]])
                    q_push(&q_blk, wx0 + x, y, wz0 + z, blk);
            }

    for (int side = 0; side < 4; side++) {
        const int ox = side == 0 ? 1 : side == 1 ? -1 : 0;
        const int oz = side == 2 ? 1 : side == 3 ? -1 : 0;
        const LightChunk* n = host->chunk(cx + ox, cz + oz);
        if (!n) continue;

        const int nlx = ox > 0 ? 0 : ox < 0 ? CS - 1 : -1;
        const int nlz = oz > 0 ? 0 : oz < 0 ? CS - 1 : -1;

        for (int y = 0; y <= top; y++)
            for (int i = 0; i < CS; i++) {
                const int lx = nlx >= 0 ? nlx : i;
                const int lz = nlz >= 0 ? nlz : i;
                const int wx = (cx + ox) * CS + lx, wz = (cz + oz) * CS + lz;
                const int sky = lc_read(n, SKY, y, lz, lx);
                const int blk = lc_read(n, BLK, y, lz, lx);
                if (sky > 1) q_push(&q_add, wx, y, wz, sky);
                if (blk > 1) q_push(&q_blk, wx, y, wz, blk);
            }
    }

    propagate(SKY, &q_add);
    propagate(BLK, &q_blk);

    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            LightChunk* n = host->chunk(cx + dx, cz + dz);
            if (n) n->dirty = 1;
        }
}

static unsigned short* up_buf;
static size_t          up_cap;

void lighting_chunk_create(LightChunk* lc)
{
    memset(lc, 0, sizeof * lc);
    glGenBuffers(1, &lc->buf);
    glGenTextures(1, &lc->tex);
    glBindBuffer(GL_TEXTURE_BUFFER, lc->buf);
    glBindTexture(GL_TEXTURE_BUFFER, lc->tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R16UI, lc->buf);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void lighting_chunk_destroy(LightChunk* lc)
{
    free(lc->data);
    glDeleteBuffers(1, &lc->buf);
    glDeleteTextures(1, &lc->tex);
    memset(lc, 0, sizeof * lc);
}

static void upload_chunk(LightChunk* lc, int cx, int cz, int max_y)
{
    int H = max_y + 3;
    if (H > CH) H = CH;
    if (H < 1) H = 1;

    const size_t count = (size_t)PW * PW * H;
    if (count > up_cap) {
        unsigned short* nb = (unsigned short*)realloc(up_buf, count * sizeof(unsigned short));
        if (!nb) return;
        up_buf = nb;
        up_cap = count;
    }

    const LightChunk* L[3][3];
    LightBlocks       Bk[3][3];
    for (int dz = 0; dz < 3; dz++)
        for (int dx = 0; dx < 3; dx++) {
            L[dz][dx] = host->chunk(cx + dx - 1, cz + dz - 1);
            Bk[dz][dx] = host->blocks(cx + dx - 1, cz + dz - 1);
        }

    for (int y = 0; y < H; y++) {
        for (int pz = 0; pz < PW; pz++) {
            const int lz = pz - 1;
            const int rz = lz < 0 ? 0 : (lz >= CS ? 2 : 1);
            const int mz = lz < 0 ? CS - 1 : (lz >= CS ? 0 : lz);

            unsigned short* row = &up_buf[((size_t)y * PW + pz) * PW];

            for (int px = 0; px < PW; px++) {
                const int lx = px - 1;
                const int rx = lx < 0 ? 0 : (lx >= CS ? 2 : 1);
                const int mx = lx < 0 ? CS - 1 : (lx >= CS ? 0 : lx);

                const LightChunk* c = L[rz][rx];
                LightBlocks b = Bk[rz][rx];

                if (!c || !b) {
                    row[px] = LIGHT_MAX;
                    continue;
                }

                unsigned v = (unsigned)lc_read(c, SKY, y, mz, mx)
                    | ((unsigned)lc_read(c, BLK, y, mz, mx) << 4);
                if (opacity_tab[b[y][mz][mx]] >= OPAQUE) v |= 1u << 8;
                row[px] = (unsigned short)v;
            }
        }
    }

    glBindBuffer(GL_TEXTURE_BUFFER, lc->buf);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(count * sizeof(unsigned short)), up_buf, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);

    lc->gpu_h = H;
    lc->dirty = 0;
}

#define MIN_LIGHT 0.045f

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }

#define DYN 16

static int           player_on;
static GLuint        dyn_tex;
static unsigned char dyn_data[DYN * DYN * DYN];
static unsigned char dyn_last[DYN * DYN * DYN];
static int           dyn_org[3];
static int           dyn_org_last[3] = { 0x7fffffff, 0, 0 };

int  lighting_toggle_player_light(void) { player_on = !player_on; return player_on; }
void lighting_set_player_light(int on) { player_on = on != 0; }
int  lighting_player_light(void) { return player_on; }

static void dyn_rebuild(double px, double py, double pz)
{
    static signed char dist[DYN * DYN * DYN];
    static unsigned short queue[DYN * DYN * DYN];

    const int R = LIGHT_PLAYER_RADIUS;
    const int cx = (int)floor(px), cy = (int)floor(py + 1.0), cz = (int)floor(pz);
    const int mid = DYN / 2;

    dyn_org[0] = cx - mid; dyn_org[1] = cy - mid; dyn_org[2] = cz - mid;

    memset(dyn_data, 0, sizeof dyn_data);
    memset(dist, -1, sizeof dist);

    const int b0 = block_at(cx, cy, cz);
    if (b0 < 0 || opacity_tab[b0] >= OPAQUE) return;

#define DIDX(x, y, z) (((z) * DYN + (y)) * DYN + (x))

    size_t head = 0, tail = 0;
    queue[tail++] = (unsigned short)DIDX(mid, mid, mid);
    dist[DIDX(mid, mid, mid)] = 0;

    while (head < tail) {
        const int idx = queue[head++];
        const int x = idx % DYN, y = (idx / DYN) % DYN, z = idx / (DYN * DYN);
        const int d = dist[idx];

        dyn_data[idx] = (unsigned char)(255.0f * (1.0f - (float)d / (float)(R + 1)) + 0.5f);

        if (d >= R) continue;

        for (int k = 0; k < 6; k++) {
            const int nx = x + DIR[k][0], ny = y + DIR[k][1], nz = z + DIR[k][2];
            if (nx < 0 || nx >= DYN || ny < 0 || ny >= DYN || nz < 0 || nz >= DYN) continue;
            const int ni = DIDX(nx, ny, nz);
            if (dist[ni] >= 0) continue;

            const int b = block_at(dyn_org[0] + nx, dyn_org[1] + ny, dyn_org[2] + nz);
            if (b < 0 || opacity_tab[b] >= OPAQUE) continue;

            dist[ni] = (signed char)(d + 1);
            queue[tail++] = (unsigned short)ni;
        }
    }
#undef DIDX
}

float lighting_player_brightness_at(double x, double y, double z)
{
    if (!player_on)
        return 0.0f;

    int ix = (int)floor(x) - dyn_org[0];
    int iy = (int)floor(y) - dyn_org[1];
    int iz = (int)floor(z) - dyn_org[2];

    if (ix < 0 || ix >= DYN ||
        iy < 0 || iy >= DYN ||
        iz < 0 || iz >= DYN)
        return 0.0f;

    size_t i = ((size_t)iz * DYN + iy) * DYN + ix;
    return dyn_data[i] / 255.0f;
}

static GLint u_light_h, u_darken, u_tint, u_minl, u_dyn_org, u_dyn_on;

void lighting_init(GLuint program, const LightHost* h)
{
    host = h;
    init_block_tables();

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "light"), 2);
    glUniform1i(glGetUniformLocation(program, "dyn"), 3);
    u_light_h = glGetUniformLocation(program, "light_h");
    u_darken = glGetUniformLocation(program, "sky_darken");
    u_tint = glGetUniformLocation(program, "sky_tint");
    u_minl = glGetUniformLocation(program, "min_light");
    u_dyn_org = glGetUniformLocation(program, "dyn_origin");
    u_dyn_on = glGetUniformLocation(program, "dyn_on");

    glGenTextures(1, &dyn_tex);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, dyn_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // 1 byte rows aren't 4 aligned, text comes out skewed without this
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, DYN, DYN, DYN, 0, GL_RED, GL_UNSIGNED_BYTE, dyn_data);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);

}

void lighting_shutdown(void)
{
    free(q_add.a); free(q_rem.a); free(up_buf);
    memset(&q_add, 0, sizeof q_add);
    memset(&q_rem, 0, sizeof q_rem);
    up_buf = NULL; up_cap = 0;

    if (dyn_tex) glDeleteTextures(1, &dyn_tex);
    dyn_tex = 0;
}

void lighting_update(double dt, double px, double py, double pz)
{
    (void)dt;

    if (player_on && host) {
        dyn_rebuild(px, py, pz);
        if (memcmp(dyn_data, dyn_last, sizeof dyn_data) != 0 ||
            memcmp(dyn_org, dyn_org_last, sizeof dyn_org) != 0) {
            memcpy(dyn_last, dyn_data, sizeof dyn_data);
            memcpy(dyn_org_last, dyn_org, sizeof dyn_org);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_3D, dyn_tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // 1 byte rows aren't 4 aligned, text comes out skewed without this
            glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, DYN, DYN, DYN, GL_RED, GL_UNSIGNED_BYTE, dyn_data);
            glActiveTexture(GL_TEXTURE0);
        }
    }
}

void lighting_begin(void)
{
    const float day = sky_get_brightness();
    glUniform1f(u_darken, sky_get_darken());
    glUniform3f(u_tint, mixf(0.55f, 1.0f, day), mixf(0.62f, 1.0f, day), 1.0f);
    glUniform1f(u_minl, MIN_LIGHT);
    glUniform3i(u_dyn_org, dyn_org[0], dyn_org[1], dyn_org[2]);
    glUniform1i(u_dyn_on, player_on);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, dyn_tex);
    glActiveTexture(GL_TEXTURE0);
}

void lighting_bind_chunk(LightChunk* lc, int cx, int cz, int max_y)
{
    if (lc->dirty || lc->gpu_h == 0)
        upload_chunk(lc, cx, cz, max_y);

    glUniform1i(u_light_h, lc->gpu_h);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, lc->tex);
    glActiveTexture(GL_TEXTURE0);
}

void lighting_get_light(int x, int y, int z, int* sky, int* block)
{
    int s = host ? light_get(SKY, x, y, z) : LIGHT_MAX;
    int b = host ? light_get(BLK, x, y, z) : 0;
    if (sky)   *sky = s < 0 ? LIGHT_MAX : s;
    if (block) *block = b < 0 ? 0 : b;
}

static float curve(float l)
{
    float v = clampf(l, 0.0f, 15.0f) / 15.0f;
    return v / (4.0f - 3.0f * v);
}

float lighting_brightness_at(double x, double y, double z)
{
    int s, b;
    lighting_get_light((int)floor(x), (int)floor(y), (int)floor(z), &s, &b);
    float sk = curve((float)s - sky_get_darken());
    float bk = curve((float)b);
    float v = sk > bk ? sk : bk;
    return MIN_LIGHT + (1.0f - MIN_LIGHT) * v;
}