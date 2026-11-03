#include "Mesher/mesher.h"

/*
 * QUAD FORMAT (2 x uint32, read by chunk.vert)
 *   a: x [0..5]  z [6..11]  y [12..31]          chunk local position of the quad's min corner
 *   b: width-1 [0..5]  height-1 [6..15]         along the face's U / V axes
 *      ao [16..23]  (4 corners x 2 bits: (0,0) (1,0) (0,1) (1,1) in U/V)
 *      face [24..26]  order: +Z -Z -X +X +Y -Y
 *
 * Merge key = texture only. AO is not computed; the ao field is always 0xFF
 * (level 3 on all corners = unshaded), so every same-texture run merges.
 *
 * Buffer layout: sorted by bucket = texture * 6 + face, so one chunk is ONE
 * buffer and per-face-direction ranges can be skipped at draw time.
 */

#define PS   (CHUNK_SIZE + 2)
#define PS2  (PS * PS)

#if defined(_MSC_VER)
#define FORCE_INLINE static __forceinline
#else
#define FORCE_INLINE static inline __attribute__((always_inline))
#endif

typedef struct { uint32_t a, b; } Quad;

static const int AXIS[6] = { 2, 2, 0, 0, 1, 1 };   /* axis the face is perpendicular to */
static const int UAX[6] = { 0, 0, 2, 2, 0, 0 };
static const int VAX[6] = { 1, 1, 1, 1, 2, 2 };
static const int SIGN[6] = { 1, -1, -1, 1, 1, -1 };

static unsigned char opaque[BLOCK_TYPES];
static unsigned char hide_tab[BLOCK_TYPES][BLOCK_TYPES];   /* [this block][neighbour] */
static int           ftex[BLOCK_TYPES][6];

/* chunk + 1 block border on every side (so no neighbour lookups while meshing) */
static unsigned char pad[(CHUNK_HEIGHT + 2) * PS2];
static uint16_t      mask[CHUNK_SIZE * CHUNK_HEIGHT];      /* always all-zero between slices */

static Quad* tmpq;
static Quad* sortq;
static uint16_t* tmpb;
static int       bucket_count[MESH_BUCKETS + 1];

static GLuint empty_vao;
static GLint  u_vp, u_origin;

void mesher_init(GLuint program, int face_tex[][BLOCK_FACE_COUNT])
{
    for (int t = 0; t < BLOCK_TYPES; t++) {
        opaque[t] = (t != BLOCK_AIR) && !block_definition(t)->transparent;
        for (int f = 0; f < 6; f++)
            ftex[t][f] = face_tex[t][f];
    }

    /* water / lava are drawn by fluid.c (sloped surface, blending) */
    for (int f = 0; f < 6; f++)
        ftex[BLOCK_WATER][f] = ftex[BLOCK_LAVA][f] = -1;

    for (int cur = 0; cur < BLOCK_TYPES; cur++) {
        for (int nb = 0; nb < BLOCK_TYPES; nb++) {
            int h = 0;
            if (nb != BLOCK_AIR) {
                const BlockDefinition* d = block_definition(nb);
                if (!d->transparent)                         h = 1;
                else if (cur == nb && d->cull_same)          h = 1;
            }
            hide_tab[cur][nb] = (unsigned char)h;
        }
    }

    /* worst case: 3D checkerboard */
    size_t cap = (size_t)CHUNK_SIZE * CHUNK_SIZE * CHUNK_HEIGHT * 3 + CHUNK_SIZE * CHUNK_HEIGHT * 4;
    tmpq = (Quad*)malloc(cap * sizeof(Quad));
    sortq = (Quad*)malloc(cap * sizeof(Quad));
    tmpb = (uint16_t*)malloc(cap * sizeof(uint16_t));

    glGenVertexArrays(1, &empty_vao);

    glUseProgram(program);
    u_vp = glGetUniformLocation(program, "vp");
    u_origin = glGetUniformLocation(program, "chunk_origin");
    glUniform1i(glGetUniformLocation(program, "tex"), 0);
    glUniform1i(glGetUniformLocation(program, "quads"), 1);
}

void mesher_shutdown(void)
{
    free(tmpq); free(sortq); free(tmpb);
    glDeleteVertexArrays(1, &empty_vao);
}

void mesh_create(ChunkMesh* m)
{
    memset(m, 0, sizeof * m);

    glGenBuffers(1, &m->vbo);
    glGenTextures(1, &m->tbo);

    glBindBuffer(GL_TEXTURE_BUFFER, m->vbo);
    glBindTexture(GL_TEXTURE_BUFFER, m->tbo);

    glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32UI, m->vbo);

    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void mesh_destroy(ChunkMesh* m)
{
    glDeleteBuffers(1, &m->vbo);
    glDeleteTextures(1, &m->tbo);
    memset(m, 0, sizeof * m);
}

/* copy the 3x3 chunk neighbourhood into the padded block array */
static void fill_pad(const MeshInput* in, int top)
{

    memset(pad, 0, (size_t)(top + 3) * PS2);

    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx && dz) continue;
            MeshBlocks src = in->blocks[dz + 1][dx + 1];
            if (!src) continue;

            int ymax = in->max_y[dz + 1][dx + 1];
            if (ymax > top) ymax = top;

            const int lx0 = dx < 0 ? CHUNK_SIZE - 1 : 0;
            const int nx = dx ? 1 : CHUNK_SIZE;
            const int px0 = dx < 0 ? 0 : (dx > 0 ? CHUNK_SIZE + 1 : 1);
            const int lz0 = dz < 0 ? CHUNK_SIZE - 1 : 0;
            const int nz = dz ? 1 : CHUNK_SIZE;
            const int pz0 = dz < 0 ? 0 : (dz > 0 ? CHUNK_SIZE + 1 : 1);

            for (int y = 0; y <= ymax; y++)
                for (int z = 0; z < nz; z++)
                    memcpy(&pad[(y + 1) * PS2 + (pz0 + z) * PS + px0],
                        &src[y][lz0 + z][lx0], (size_t)nx);
        }
    }
}

void mesh_build(ChunkMesh* m, const MeshInput* in)
{
    const int max_y = in->max_y[1][1];
    const int top = (max_y + 1 < CHUNK_HEIGHT) ? max_y + 1 : CHUNK_HEIGHT - 1;

    fill_pad(in, top);

    const int dim[3] = { CHUNK_SIZE, max_y + 1, CHUNK_SIZE };
    const int stride[3] = { 1, PS2, PS };
    const unsigned char* const origin = pad + PS2 + PS + 1;   /* local (0,0,0) */

    memset(bucket_count, 0, sizeof bucket_count);
    int nq = 0;

    for (int face = 0; face < 6; face++) {
        const int axis = AXIS[face], ua = UAX[face], va = VAX[face];
        const int sa = stride[axis], su = stride[ua], sv = stride[va];
        const int nd = SIGN[face] * sa;
        const int slices = dim[axis];
        const int vlen = dim[va];

        for (int s = 0; s < slices; s++) {
            int any = 0;
            const unsigned char* slice = origin + s * sa;
            /* 1. build the mask: texture only per visible face */
            for (int v = 0; v < vlen; v++) {
                const unsigned char* p = slice + v * sv;
                uint16_t* mrow = &mask[v * CHUNK_SIZE];

                for (int u = 0; u < CHUNK_SIZE; u++, p += su) {
                    const int type = *p;
                    if (!type) continue;

                    const int t = ftex[type][face];
                    if (t < 0) continue;

                    if (hide_tab[type][p[nd]]) continue;   /* neighbour hides this face */

                    mrow[u] = (uint16_t)(t + 1);           /* key = texture + 1 (0 means empty) */
                    any = 1;
                }
            }

            if (!any) continue;

            /* 2. greedy merge: grow width, then height, clear what was used */
            for (int v = 0; v < vlen; v++) {
                for (int u = 0; u < CHUNK_SIZE; u++) {
                    const uint16_t key = mask[v * CHUNK_SIZE + u];
                    if (!key) continue;

                    int w = 1;
                    while (u + w < CHUNK_SIZE && mask[v * CHUNK_SIZE + u + w] == key) w++;

                    int h = 1;
                    for (; v + h < vlen; h++) {
                        const uint16_t* r = &mask[(v + h) * CHUNK_SIZE + u];
                        int k = 0;
                        while (k < w && r[k] == key) k++;
                        if (k < w) break;
                    }

                    for (int j = 0; j < h; j++)
                        memset(&mask[(v + j) * CHUNK_SIZE + u], 0, (size_t)w * sizeof(uint16_t));

                    int loc[3];
                    loc[axis] = s;
                    loc[ua] = u;
                    loc[va] = v;

                    const int tex = (int)key - 1;        /* undo the +1 from the mask */
                    const int bucket = tex * 6 + face;

                    tmpq[nq].a = (uint32_t)loc[0] | ((uint32_t)loc[2] << 6) | ((uint32_t)loc[1] << 12);
                    tmpq[nq].b = (uint32_t)(w - 1) | ((uint32_t)(h - 1) << 6) |
                        (0xFFu << 16) | ((uint32_t)face << 24);   /* AO = 3 on all 4 corners (unshaded) */
                    tmpb[nq] = (uint16_t)bucket;
                    bucket_count[bucket]++;
                    nq++;
                }
            }
        }
    }

    /* 3. counting sort by bucket (texture, face) */
    int sum = 0;
    for (int b = 0; b < MESH_BUCKETS; b++) {
        m->first[b] = sum;
        sum += bucket_count[b];
        bucket_count[b] = m->first[b];
    }
    m->first[MESH_BUCKETS] = sum;

    for (int i = 0; i < nq; i++)
        sortq[bucket_count[tmpb[i]]++] = tmpq[i];

    m->quad_count = nq;

    if (nq)
    {
        const GLsizeiptr required = (GLsizeiptr)nq * sizeof(Quad);
        glBindBuffer(GL_TEXTURE_BUFFER, m->vbo);

        if (required > m->capacity_bytes)
        {
            GLsizeiptr capacity = m->capacity_bytes;

            if (capacity < 4096)
                capacity = 4096;

            while (capacity < required)
                capacity += capacity / 2;

            glBufferData(GL_TEXTURE_BUFFER, capacity, NULL, GL_DYNAMIC_DRAW);
            m->capacity_bytes = capacity;
        }
        glBufferSubData(GL_TEXTURE_BUFFER, 0, required, sortq);
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
    }
}

/* RENDERING */

static float planes[6][4];

void mesher_set_frustum(const float* vp)
{
    for (int k = 0; k < 3; k++) {
        for (int c = 0; c < 4; c++) {
            float r3 = vp[c * 4 + 3];
            float rk = vp[c * 4 + k];
            planes[k * 2][c] = r3 + rk;
            planes[k * 2 + 1][c] = r3 - rk;
        }
    }
}

int mesher_aabb_visible(float x0, float y0, float z0, float x1, float y1, float z1)
{
    for (int i = 0; i < 6; i++) {
        const float* p = planes[i];
        float px = p[0] > 0.0f ? x1 : x0;
        float py = p[1] > 0.0f ? y1 : y0;
        float pz = p[2] > 0.0f ? z1 : z0;
        if (p[0] * px + p[1] * py + p[2] * pz + p[3] < 0.0f)
            return 0;
    }
    return 1;
}

/* bit per face (+Z -Z -X +X +Y -Y): can any face of that direction in this chunk face the camera? */
unsigned mesher_face_mask(float cx, float cy, float cz, float x0, float z0, int max_y)
{
    unsigned m = 0;
    if (cz > z0)                          m |= 1u << 0;
    if (cz < z0 + CHUNK_SIZE)             m |= 1u << 1;
    if (cx < x0 + CHUNK_SIZE)             m |= 1u << 2;
    if (cx > x0)                          m |= 1u << 3;
    if (cy > 1.0f)                        m |= 1u << 4;
    if (cy < (float)max_y + 1.0f)         m |= 1u << 5;
    return m;
}

static GLuint last_tex;

void mesher_begin(const float* vp)
{
    glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp);
    glBindVertexArray(empty_vao);
    glActiveTexture(GL_TEXTURE0);
    last_tex = 0;
}

int mesh_draw(const ChunkMesh* m, unsigned face_mask, float x0, float z0,
    const GLuint* tex_ids, int tex_count)
{
    int tris = 0;

    glUniform3f(u_origin, x0, 0.0f, z0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, m->tbo);
    glActiveTexture(GL_TEXTURE0);

    for (int t = 0; t < tex_count; t++) {
        const int* f = &m->first[t * 6];
        if (f[6] == f[0]) continue;

        GLint   firsts[6];
        GLsizei counts[6];
        int n = 0;

        for (int face = 0; face < 6; face++) {
            if (!((face_mask >> face) & 1u)) continue;
            int a = f[face], b = f[face + 1];
            if (a == b) continue;

            if (n && firsts[n - 1] + counts[n - 1] == a * 6)
                counts[n - 1] += (b - a) * 6;          /* touching range: merge */
            else {
                firsts[n] = a * 6;
                counts[n] = (b - a) * 6;
                n++;
            }
        }
        if (!n) continue;

        if (tex_ids[t] != last_tex) {
            glBindTexture(GL_TEXTURE_2D, tex_ids[t]);
            last_tex = tex_ids[t];
        }

        if (n == 1) {
            glDrawArrays(GL_TRIANGLES, firsts[0], counts[0]);
            tris += counts[0] / 3;
        }
        else {
            glMultiDrawArrays(GL_TRIANGLES, firsts, counts, n);
            for (int i = 0; i < n; i++) tris += counts[i] / 3;
        }
    }

    return tris;
}