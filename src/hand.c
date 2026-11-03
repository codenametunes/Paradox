#include "main.h"
#include "Third Party/stb_image.h"
#include "Mobs/mob.h"

#define VIEW_BOBBING 1

#define AIR_TILT 1

#define SWING_TICKS  6
#define HAND_FOV     70.0f

static const char* hand_vert =
"#version 330 core\n"
"layout (location = 0) in vec3 pos;\n"
"layout (location = 1) in vec2 uv;\n"
"layout (location = 2) in vec3 normal;\n"
"uniform mat4 mvp;\n"
"uniform mat3 nmat;\n"
"out vec2 vuv;\n"
"out vec3 vn;\n"
"void main() {\n"
"    vuv = uv;\n"
"    vn = nmat * normal;\n"
"    gl_Position = mvp * vec4(pos, 1.0);\n"
"}\n";

static const char* hand_frag =
"#version 330 core\n"
"in vec2 vuv;\n"
"in vec3 vn;\n"
"uniform sampler2D tex;\n"
"uniform vec3 l0;\n"
"uniform vec3 l1;\n"
"uniform float world_light;\n"
"out vec4 color;\n"
"void main() {\n"
"    vec4 c = texture(tex, vuv);\n"
"    if (c.a < 0.1) discard;\n"
"    vec3 n = normalize(vn);\n"
"    float diffuse = min(1.0,\n"
"        (max(0.0, dot(l0, n)) +\n"
"         max(0.0, dot(l1, n))) * 0.6 + 0.4);\n"
"    float light = diffuse * world_light;\n"
"    color = vec4(c.rgb * light, c.a);\n"
"}\n";

static GLuint prog = 0;

static GLuint skin_tex = 0;
static GLuint arm_vao = 0, arm_vbo = 0;
static GLuint cube_vao = 0, cube_vbo = 0;
static GLuint sword_vao = 0, sword_vbo = 0;

static GLint loc_mvp = -1, loc_nmat = -1;
static GLint loc_tex = -1, loc_l0 = -1, loc_l1 = -1;
static GLint loc_world_light = -1;

static int    sword_verts = 0;

static GLuint shown_tex[6] = { 0, 0, 0, 0, 0, 0 };

static void m_identity(float* m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void m_mul(float* out, const float* a, const float* b)
{
    float r[16];

    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;

            for (int k = 0; k < 4; k++)
                sum += a[k * 4 + row] * b[c * 4 + k];

            r[c * 4 + row] = sum;
        }
    }

    memcpy(out, r, sizeof r);
}

static void m_translate(float* m, float x, float y, float z)
{
    for (int i = 0; i < 4; i++)
        m[12 + i] += m[i] * x + m[4 + i] * y + m[8 + i] * z;
}

static void m_scale(float* m, float s)
{
    for (int i = 0; i < 12; i++)
        m[i] *= s;
}

static void m_rotate(float* m, int axis, float deg)
{
    float r[16];
    float a = deg * PI / 180.0f;
    float c = cosf(a);
    float s = sinf(a);

    m_identity(r);

    if (axis == 0) { r[5] = c;  r[6] = s;  r[9] = -s;  r[10] = c; }
    else if (axis == 1) { r[0] = c;  r[2] = -s; r[8] = s;   r[10] = c; }
    else { r[0] = c;  r[1] = s;  r[4] = -s;  r[5] = c; }

    m_mul(m, m, r);
}

static void m_perspective(float* m, float fovy, float aspect, float n, float f)
{
    float t = 1.0f / tanf(fovy * 0.5f);

    memset(m, 0, 16 * sizeof(float));

    m[0] = t / aspect;
    m[5] = t;
    m[10] = (f + n) / (n - f);
    m[11] = -1.0f;
    m[14] = (2.0f * f * n) / (n - f);
}

static int   want = HAND_EMPTY;
static int   shown = HAND_EMPTY;
static float height = 0.0f;
static float height_o = 0.0f;

static int   swinging = 0;
static int   swing_queued = 0;
static float swing_pos = 0.0f;

static double walk_dist = 0.0, walk_dist_o = 0.0;
static float  bob = 0.0f, bob_o = 0.0f;
static float  tilt = 0.0f, tilt_o = 0.0f;

static float x_bob = 0.0f, x_bob_o = 0.0f;
static float y_bob = 0.0f, y_bob_o = 0.0f;
static int   bob_ready = 0;

void hand_select(int item) { want = item; }
int  hand_item(void) { return want; }

void hand_swing(void)
{
    if (!swinging) {
        swinging = 1;
        swing_queued = 0;
        swing_pos = 0.0f;
    }
    else if (swing_pos >= SWING_TICKS / 2) {
        swing_queued = 1;
    }
}

void hand_frame(double dt)
{
    if (!swinging)
        return;

    float speed = swing_queued ? 3.0f : 1.0f;

    swing_pos += (float)dt * 20.0f * speed;

    if (swing_pos >= SWING_TICKS) {
        swing_pos -= SWING_TICKS;

        if (swing_queued) {
            swing_queued = 0;
            swing_pos /= speed;
        }
        else {
            swinging = 0;
            swing_pos = 0.0f;
        }
    }
}

void hand_use(void)
{
    hand_swing();
    height = 0.0f;
}

void hand_reject(void)
{
    height = 0.0f;
}

void hand_tick(double moved_x, double moved_z, double vel_x, double vel_y, double vel_z,
    int on_ground, float yaw_deg, float pitch_deg)
{
    height_o = height;

    if (shown != want && height_o <= 0.0f)
        shown = want;

    float target = (shown == want) ? 1.0f : 0.0f;
    float step = target - height;

    if (step > 0.4f)  step = 0.4f;
    if (step < -0.4f) step = -0.4f;

    height += step;

    walk_dist_o = walk_dist;
    walk_dist += sqrt(moved_x * moved_x + moved_z * moved_z) * 0.6;

    bob_o = bob;

    float bob_target = 0.0f;

    if (on_ground) {
        bob_target = (float)sqrt(vel_x * vel_x + vel_z * vel_z);

        if (bob_target > 0.1f)
            bob_target = 0.1f;
    }

    bob += (bob_target - bob) * 0.4f;

    tilt_o = tilt;

    float tilt_target = 0.0f;

    if (!on_ground)
        tilt_target = (float)atan(-vel_y * 0.2) * 15.0f;

    tilt += (tilt_target - tilt) * 0.8f;

    if (!bob_ready) {
        x_bob = -pitch_deg;
        y_bob = yaw_deg;
        bob_ready = 1;
    }

    x_bob_o = x_bob;
    y_bob_o = y_bob;

    x_bob += (-pitch_deg - x_bob) * 0.5f;
    y_bob += (yaw_deg - y_bob) * 0.5f;
}

void hand_bob_matrix(double alpha, float* out)
{
    m_identity(out);

#if VIEW_BOBBING
    float a = (float)alpha;
    float f = (float)(walk_dist - walk_dist_o);
    float f1 = -((float)walk_dist + f * a);
    float f2 = bob_o + (bob - bob_o) * a;

    float s = sinf(f1 * PI);
    float c = cosf(f1 * PI);

    m_translate(out, s * f2 * 0.5f, -fabsf(c * f2), 0.0f);
    m_rotate(out, 2, s * f2 * 3.0f);
    m_rotate(out, 0, fabsf(cosf(f1 * PI - 0.2f) * f2) * 5.0f);
#endif

#if AIR_TILT
    m_rotate(out, 0, tilt_o + (tilt - tilt_o) * (float)alpha);
#endif
}

static void emit(float** o, float x, float y, float z, float u, float v, const float* n)
{
    float* p = *o;

    p[0] = x; p[1] = y; p[2] = z;
    p[3] = u; p[4] = v;
    p[5] = n[0]; p[6] = n[1]; p[7] = n[2];

    *o += 8;
}

static const int tri_idx[6] = { 0, 1, 2, 0, 2, 3 };

static const int cube_corners[8][3] = {
    {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}, {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}
};

static const int cube_faces[6][4] = {
    {4,5,6,7}, {1,0,3,2}, {0,4,7,3}, {5,1,2,6}, {3,7,6,2}, {0,1,5,4}
};

static const float cube_normal[6][3] = {
    {0,0,1}, {0,0,-1}, {-1,0,0}, {1,0,0}, {0,1,0}, {0,-1,0}
};

static const float cube_uv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };

static void build_cube(float* out)
{
    float* o = out;

    for (int face = 0; face < 6; face++) {
        for (int t = 0; t < 6; t++) {
            int ci = tri_idx[t];
            const int* c = cube_corners[cube_faces[face][ci]];

            emit(&o, c[0] - 0.5f, c[1] - 0.5f, c[2] - 0.5f,
                cube_uv[ci][0], cube_uv[ci][1], cube_normal[face]);
        }
    }
}

static const int arm_face[6][4] = {
    {5,1,2,6},
    {0,4,7,3},
    {5,4,0,1},
    {2,3,7,6},
    {1,0,3,2},
    {4,5,6,7}
};

static const float arm_uv[6][4][2] = {
    {{52,20}, {48,20}, {48,32}, {52,32}},
    {{44,20}, {40,20}, {40,32}, {44,32}},
    {{48,16}, {44,16}, {44,20}, {48,20}},
    {{52,16}, {48,16}, {48,20}, {52,20}},
    {{48,20}, {44,20}, {44,32}, {48,32}},
    {{56,20}, {52,20}, {52,32}, {56,32}}
};

static const float arm_normal[6][3] = {
    {1,0,0}, {-1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}
};

static void build_arm(float* out)
{
    const float x0 = -3, x1 = 1, y0 = -2, y1 = 10, z0 = -2, z1 = 2;

    const float corner[8][3] = {
        {x0,y0,z0}, {x1,y0,z0}, {x1,y1,z0}, {x0,y1,z0},
        {x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}
    };

    float* o = out;

    for (int face = 0; face < 6; face++) {
        for (int t = 0; t < 6; t++) {
            int ci = tri_idx[t];
            const float* c = corner[arm_face[face][ci]];

            emit(&o, c[0] / 16.0f, c[1] / 16.0f, c[2] / 16.0f,
                arm_uv[face][ci][0] / 64.0f, arm_uv[face][ci][1] / 64.0f,
                arm_normal[face]);
        }
    }
}

static void make_mesh(GLuint* vao, GLuint* vbo, const float* data)
{
    glGenVertexArrays(1, vao);
    glGenBuffers(1, vbo);

    glBindVertexArray(*vao);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, 36 * 8 * sizeof(float), data, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static GLuint make_program(const char* vs, const char* fs)
{
    const char* src[2] = { vs, fs };
    GLenum type[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    GLuint sh[2];

    for (int i = 0; i < 2; i++) {
        sh[i] = glCreateShader(type[i]);
        glShaderSource(sh[i], 1, &src[i], NULL);
        glCompileShader(sh[i]);

        GLint ok;
        glGetShaderiv(sh[i], GL_COMPILE_STATUS, &ok);

        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(sh[i], sizeof log, NULL, log);
            fprintf(stderr, "Hand shader compilation failed:\n%s\n", log);
            return 0;
        }
    }

    GLuint p = glCreateProgram();

    glAttachShader(p, sh[0]);
    glAttachShader(p, sh[1]);
    glLinkProgram(p);

    glDeleteShader(sh[0]);
    glDeleteShader(sh[1]);

    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);

    if (!ok) {
        char log[1024];  // hope the log fits
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "Hand shader linking failed:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }

    return p;
}

static void emit_quad(float** o, const float p[4][3], const float uv[4][2], const float* n)
{
    for (int t = 0; t < 6; t++) {
        int i = tri_idx[t];
        emit(o, p[i][0], p[i][1], p[i][2], uv[i][0], uv[i][1], n);
    }
}

int hand_load_sword(const char* path)
{
    int w, h, channels;

    stbi_set_flip_vertically_on_load(0);

    unsigned char* data = asset_image(path, &w, &h, &channels);

    if (!data) {
        fprintf(stderr, "Failed to load item '%s': %s\n", path, stbi_failure_reason());
        return 0;
    }

#define OPAQUE(px, py) ((px) >= 0 && (px) < w && (py) >= 0 && (py) < h && data[((py) * w + (px)) * 4 + 3] >= 26)

    size_t max_quads = 2 + (size_t)w * h * 4;
    float* mesh = (float*)malloc(max_quads * 6 * 8 * sizeof(float));

    if (!mesh) {
        stbi_image_free(data);
        return 0;
    }

    float* o = mesh;
    const float T = 1.0f / 32.0f;

    {
        const float p[4][3] = { {-0.5f,-0.5f, T}, {0.5f,-0.5f, T}, {0.5f,0.5f, T}, {-0.5f,0.5f, T} };
        const float uv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
        const float n[3] = { 0, 0, 1 };
        emit_quad(&o, p, uv, n);

        const float q[4][3] = { {0.5f,-0.5f,-T}, {-0.5f,-0.5f,-T}, {-0.5f,0.5f,-T}, {0.5f,0.5f,-T} };
        const float quv[4][2] = { {1,0}, {0,0}, {0,1}, {1,1} };
        const float bn[3] = { 0, 0, -1 };
        emit_quad(&o, q, quv, bn);
    }

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            if (!OPAQUE(px, py))
                continue;

            float x0 = (float)px / w - 0.5f, x1 = (float)(px + 1) / w - 0.5f;
            float y1 = 0.5f - (float)py / h, y0 = 0.5f - (float)(py + 1) / h;

            float u = ((float)px + 0.5f) / w;
            float v = 1.0f - ((float)py + 0.5f) / h;
            const float uv[4][2] = { {u,v}, {u,v}, {u,v}, {u,v} };

            if (!OPAQUE(px - 1, py)) {
                const float p[4][3] = { {x0,y0,-T}, {x0,y0,T}, {x0,y1,T}, {x0,y1,-T} };
                const float n[3] = { -1, 0, 0 };
                emit_quad(&o, p, uv, n);
            }
            if (!OPAQUE(px + 1, py)) {
                const float p[4][3] = { {x1,y0,T}, {x1,y0,-T}, {x1,y1,-T}, {x1,y1,T} };
                const float n[3] = { 1, 0, 0 };
                emit_quad(&o, p, uv, n);
            }
            if (!OPAQUE(px, py - 1)) {
                const float p[4][3] = { {x0,y1,T}, {x1,y1,T}, {x1,y1,-T}, {x0,y1,-T} };
                const float n[3] = { 0, 1, 0 };
                emit_quad(&o, p, uv, n);
            }
            if (!OPAQUE(px, py + 1)) {
                const float p[4][3] = { {x0,y0,-T}, {x1,y0,-T}, {x1,y0,T}, {x0,y0,T} };
                const float n[3] = { 0, -1, 0 };
                emit_quad(&o, p, uv, n);
            }
        }
    }

#undef OPAQUE

    sword_verts = (int)((o - mesh) / 8);

    glGenVertexArrays(1, &sword_vao);
    glGenBuffers(1, &sword_vbo);

    glBindVertexArray(sword_vao);
    glBindBuffer(GL_ARRAY_BUFFER, sword_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sword_verts * 8 * sizeof(float), mesh, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    free(mesh);
    stbi_image_free(data);

    return 1;
}
static float hand_world_light = 1.0f;

void hand_set_world_light(float brightness)
{
    hand_world_light = brightness;

    if (hand_world_light < 0.0f)
        hand_world_light = 0.0f;
    if (hand_world_light > 1.0f)
        hand_world_light = 1.0f;
}
int hand_init(const char* skin_path)
{
    prog = make_program(hand_vert, hand_frag);

    if (!prog)
        return 0;

    loc_mvp = glGetUniformLocation(prog, "mvp");
    loc_nmat = glGetUniformLocation(prog, "nmat");
    loc_tex = glGetUniformLocation(prog, "tex");
    loc_l0 = glGetUniformLocation(prog, "l0");
    loc_l1 = glGetUniformLocation(prog, "l1");
    loc_world_light = glGetUniformLocation(prog, "world_light");
    int w, h, channels;

    stbi_set_flip_vertically_on_load(0);

    unsigned char* data = asset_image(skin_path, &w, &h, &channels);

    static const unsigned char fallback[4] = { 198, 146, 107, 255 };

    if (!data) {
        fprintf(stderr, "Failed to load skin '%s': %s\n", skin_path, stbi_failure_reason());
        w = 1;
        h = 1;
    }

    glGenTextures(1, &skin_tex);
    glBindTexture(GL_TEXTURE_2D, skin_tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data ? data : fallback);

    glBindTexture(GL_TEXTURE_2D, 0);

    if (data)
        stbi_image_free(data);

    float mesh[36 * 8];

    build_arm(mesh);
    make_mesh(&arm_vao, &arm_vbo, mesh);

    build_cube(mesh);
    make_mesh(&cube_vao, &cube_vbo, mesh);

    return 1;
}

void hand_shutdown(void)
{
    if (skin_tex) glDeleteTextures(1, &skin_tex);
    if (arm_vbo)  glDeleteBuffers(1, &arm_vbo);
    if (arm_vao)  glDeleteVertexArrays(1, &arm_vao);
    if (cube_vbo) glDeleteBuffers(1, &cube_vbo);
    if (cube_vao) glDeleteVertexArrays(1, &cube_vao);
    if (sword_vbo) glDeleteBuffers(1, &sword_vbo);
    if (sword_vao) glDeleteVertexArrays(1, &sword_vao);
    if (prog)     glDeleteProgram(prog);

    skin_tex = arm_vbo = arm_vao = cube_vbo = cube_vao = prog = 0;
    sword_vbo = sword_vao = 0;
    sword_verts = 0;
}

static void rotate_dir(const float* m, const float* d, float* out)
{
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float x = d[0] / len, y = d[1] / len, z = d[2] / len;

    out[0] = m[0] * x + m[4] * y + m[8] * z;
    out[1] = m[1] * x + m[5] * y + m[9] * z;
    out[2] = m[2] * x + m[6] * y + m[10] * z;
}

void hand_draw(double alpha, float yaw_deg, float pitch_deg, const float* view,
    int fb_w, int fb_h, const GLuint* block_tex)
{

    if (!prog || fb_w <= 0 || fb_h <= 0)
        return;

    float a = (float)alpha;

    if (shown == want)
        memcpy(shown_tex, block_tex, sizeof shown_tex);

    block_tex = shown_tex;

    float proj[16];
    m_perspective(proj, HAND_FOV * PI / 180.0f, (float)fb_w / (float)fb_h, 0.05f, 128.0f);

    float bobm[16];
    hand_bob_matrix(alpha, bobm);

    float pose[16];
    memcpy(pose, bobm, sizeof pose);

    m_rotate(pose, 0, (-pitch_deg - (x_bob_o + (x_bob - x_bob_o) * a)) * 0.1f);
    m_rotate(pose, 1, (yaw_deg - (y_bob_o + (y_bob - y_bob_o) * a)) * 0.1f);

    float equip = 1.0f - (height_o + (height - height_o) * a);

    float sw = swinging ? swing_pos / (float)SWING_TICKS : 0.0f;

    float sq = sqrtf(sw);

    if (shown == HAND_EMPTY) {
        float f2 = -0.3f * sinf(sq * PI);
        float f3 = 0.4f * sinf(sq * PI * 2.0f);
        float f4 = -0.4f * sinf(sw * PI);

        m_translate(pose, f2 + 0.64000005f, f3 - 0.6f + equip * -0.6f, f4 - 0.71999997f);
        m_rotate(pose, 1, 45.0f);

        float f5 = sinf(sw * sw * PI);
        float f6 = sinf(sq * PI);

        m_rotate(pose, 1, f6 * 70.0f);
        m_rotate(pose, 2, f5 * -20.0f);

        m_translate(pose, -1.0f, 3.6f, 3.5f);
        m_rotate(pose, 2, 120.0f);
        m_rotate(pose, 0, 200.0f);
        m_rotate(pose, 1, -135.0f);
        m_translate(pose, 5.6f, 0.0f, 0.0f);

        m_translate(pose, -5.0f / 16.0f, 2.0f / 16.0f, 0.0f);
        m_rotate(pose, 2, 0.1f * 180.0f / PI);
    }
    else {
        m_translate(pose, -0.4f * sinf(sq * PI), 0.2f * sinf(sq * PI * 2.0f), 0.2f * sinf(sw * PI));

        m_translate(pose, 0.56f, -0.52f + equip * -0.6f, -0.72f);

        float f = sinf(sw * sw * PI);
        float f1 = sinf(sq * PI);

        m_rotate(pose, 1, 45.0f + f * -20.0f);
        m_rotate(pose, 2, f1 * -20.0f);
        m_rotate(pose, 0, f1 * -80.0f);
        m_rotate(pose, 1, -45.0f);

        if (shown == ITEM_DIAMOND_SWORD) {
            m_translate(pose, 1.13f / 16.0f, 3.2f / 16.0f, 1.13f / 16.0f);
            m_rotate(pose, 1, -90.0f);
            m_rotate(pose, 2, 25.0f);
            m_scale(pose, 0.68f);
        }
        else {
            m_rotate(pose, 1, 45.0f);
            m_scale(pose, 0.4f);
        }
    }

    float mvp[16];
    m_mul(mvp, proj, pose);

    float nmat[9] = {
        pose[0], pose[1], pose[2],
        pose[4], pose[5], pose[6],
        pose[8], pose[9], pose[10]
    };

    float world_view[16];
    m_mul(world_view, bobm, view);

    static const float light0[3] = { 0.2f, 1.0f, -0.7f };
    static const float light1[3] = { -0.2f, 1.0f, 0.7f };

    float l0[3], l1[3];
    rotate_dir(world_view, light0, l0);
    rotate_dir(world_view, light1, l1);

    glClear(GL_DEPTH_BUFFER_BIT);

    GLboolean cull_was_on = glIsEnabled(GL_CULL_FACE);
    GLint cull_mode, front_face;
    glGetIntegerv(GL_CULL_FACE_MODE, &cull_mode);
    glGetIntegerv(GL_FRONT_FACE, &front_face);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glUseProgram(prog);
    glUniform1f(loc_world_light, hand_world_light);
    glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp);
    glUniformMatrix3fv(loc_nmat, 1, GL_FALSE, nmat);
    glUniform3fv(loc_l0, 1, l0);
    glUniform3fv(loc_l1, 1, l1);
    glUniform1i(loc_tex, 0);
    glActiveTexture(GL_TEXTURE0);

    if (shown == HAND_EMPTY) {
        glBindTexture(GL_TEXTURE_2D, skin_tex);
        glBindVertexArray(arm_vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    else if (shown == ITEM_DIAMOND_SWORD) {
        if (sword_vao && sword_verts > 0) {
            glBindTexture(GL_TEXTURE_2D, block_tex[0]);
            glBindVertexArray(sword_vao);
            glDrawArrays(GL_TRIANGLES, 0, sword_verts);
        }
    }
    else {
        glBindVertexArray(cube_vao);

        for (int face = 0; face < 6; face++) {
            glBindTexture(GL_TEXTURE_2D, block_tex[face]);
            glDrawArrays(GL_TRIANGLES, face * 6, 6);
        }
    }

    glBindVertexArray(0);

    glCullFace((GLenum)cull_mode);
    glFrontFace((GLenum)front_face);

    if (cull_was_on)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
}
float hand_attack_time(void)
{
    return swinging ? swing_pos / (float)SWING_TICKS : 0.0f;
}

unsigned int hand_cube_vao(void) { return cube_vao; }

unsigned int hand_sword_vao(int* verts)
{
    if (verts) *verts = sword_verts;
    return sword_vao;
}

void hand_fill_held(MobState* m, const unsigned int* block_tex)
{
    m->held = (want != HAND_EMPTY);
    m->held_item = want;
    memcpy(m->held_tex, block_tex ? block_tex : shown_tex, sizeof shown_tex);
}