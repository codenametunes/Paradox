#include "sky.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stddef.h>
#include "Third Party/stb_image.h"

/*
 * See sky.h for the overview. Every formula below is a direct port of the Mojang-mapped
 * 1.20.x client code; the original method is named next to each piece.
 */

#define SKY_PI 3.14159265358979323846f

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }   /* Mth.lerp */

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

static double time_ticks = 0.0;       /* total world time (Level day time) */
static int    day_cycle = 1;
static double cloud_ticks = 0.0;      /* LevelRenderer.ticks (client ticks, runs even when time is frozen) */

static float  rain_level = 0.0f, thunder_level = 0.0f;
static int    render_dist_chunks = 12;
static int    clouds_on = 1;

/* biome colours, default = plains */
static float  biome_sky[3] = { 120.0f / 255.0f, 167.0f / 255.0f, 1.0f };            /* 0x78A7FF */
static float  biome_fog[3] = { 192.0f / 255.0f, 216.0f / 255.0f, 1.0f };            /* 0xC0D8FF */

/* derived by recompute() */
static float  tod = 0.0f;             /* getTimeOfDay: 0 = noon */
static float  sun_angle = 0.0f;       /* getSunAngle  */
static float  brightness = 1.0f;
static float  darken = 0.0f;
static float  star_bright = 0.0f;
static int    moon_phase = 0;
static float  sky_rgb[3] = { 0, 0, 0 };
static float  fog_rgb_cache[3] = { 0, 0, 0 };
static int    have_sunrise = 0;
static float  sunrise[4];             /* r g b a, see sunrise_color() */

/* ------------------------------------------------------------------ */
/* vanilla maths                                                       */
/* ------------------------------------------------------------------ */

/* DimensionSpecialEffects.OverworldEffects.getSunriseColor */
static int sunrise_color(float time_of_day, float out[4])
{
    const float g = cosf(time_of_day * (SKY_PI * 2.0f)) - 0.0f;
    if (g >= -0.4f && g <= 0.4f) {
        float i = (g - -0.0f) / 0.4f * 0.5f + 0.5f;
        float j = 1.0f - (1.0f - sinf(i * SKY_PI)) * 0.99f;
        j *= j;
        out[0] = i * 0.3f + 0.7f;
        out[1] = i * i * 0.7f + 0.2f;
        out[2] = i * i * 0.0f + 0.2f;
        out[3] = j;
        return 1;
    }
    return 0;
}

static void recompute(void)
{
    const double t = SKY_SMOOTH_TIME ? time_ticks : floor(time_ticks);

    /* DimensionType.timeOfDay */
    double d = t / (double)SKY_TICKS_PER_DAY - 0.25;
    d -= floor(d);
    const double e = 0.5 - cos(d * (double)SKY_PI) / 2.0;
    tod = (float)((d * 2.0 + e) / 3.0);
    sun_angle = tod * (SKY_PI * 2.0f);

    const float c = cosf(sun_angle);

    /* ClientLevel.getSkyColor: brightness factor of the dome */
    brightness = clampf(c * 2.0f + 0.5f, 0.0f, 1.0f);

    /* Level.updateSkyBrightness (without the (int) cast) */
    {
        const float d1 = 1.0f - rain_level * 5.0f / 16.0f;
        const float e1 = 1.0f - thunder_level * 5.0f / 16.0f;
        const float f1 = 0.5f + 2.0f * clampf(c, -0.25f, 0.25f);
        darken = (1.0f - f1 * d1 * e1) * 11.0f;
    }

    /* Level.getStarBrightness */
    {
        float g = 1.0f - (c * 2.0f + 0.25f);
        g = clampf(g, 0.0f, 1.0f);
        star_bright = g * g * 0.5f;
    }

    /* Level.getMoonPhase: (int)(time / 24000 % 8 + 8) % 8 */
    {
        long day = (long)floor(t / (double)SKY_TICKS_PER_DAY);
        moon_phase = (int)(((day % 8) + 8) % 8);
    }

    /* ClientLevel.getSkyColor (biome blend is the identity for a single biome) */
    {
        float h = biome_sky[0] * brightness;
        float i = biome_sky[1] * brightness;
        float j = biome_sky[2] * brightness;
        if (rain_level > 0.0f) {
            const float m = (h * 0.3f + i * 0.59f + j * 0.11f) * 0.6f;
            const float n = 1.0f - rain_level * 0.75f;
            h = h * n + m * (1.0f - n);
            i = i * n + m * (1.0f - n);
            j = j * n + m * (1.0f - n);
        }
        if (thunder_level > 0.0f) {
            const float n = (h * 0.3f + i * 0.59f + j * 0.11f) * 0.2f;
            const float o = 1.0f - thunder_level * 0.75f;
            h = h * o + n * (1.0f - o);
            i = i * o + n * (1.0f - o);
            j = j * o + n * (1.0f - o);
        }
        sky_rgb[0] = h; sky_rgb[1] = i; sky_rgb[2] = j;
    }

    have_sunrise = sunrise_color(tod, sunrise);
}

/* ClientLevel.getCloudColor (vanilla cloud base colour is white) */
static void cloud_color(float out[3])
{
    const float g = brightness;
    float h = 1.0f, i = 1.0f, j = 1.0f;
    if (rain_level > 0.0f) {
        const float l = (h * 0.3f + i * 0.59f + j * 0.11f) * 0.6f;
        const float m = 1.0f - rain_level * 0.95f;
        h = h * m + l * (1.0f - m);
        i = i * m + l * (1.0f - m);
        j = j * m + l * (1.0f - m);
    }
    h *= g * 0.9f + 0.1f;
    i *= g * 0.9f + 0.1f;
    j *= g * 0.85f + 0.15f;
    if (thunder_level > 0.0f) {
        const float m = (h * 0.3f + i * 0.59f + j * 0.11f) * 0.2f;
        const float n = 1.0f - thunder_level * 0.95f;
        h = h * n + m * (1.0f - n);
        i = i * n + m * (1.0f - n);
        j = j * n + m * (1.0f - n);
    }
    out[0] = h; out[1] = i; out[2] = j;
}

/* FogRenderer.setupColor */
void sky_get_fog_color(const float* look, float out_rgb[3])
{
    /* FogRenderer: brightness-dependent fog colour of the overworld effects */
    const float k = clampf(cosf(tod * (SKY_PI * 2.0f)) * 2.0f + 0.5f, 0.0f, 1.0f);
    float fr = biome_fog[0] * (k * 0.94f + 0.06f);
    float fg = biome_fog[1] * (k * 0.94f + 0.06f);
    float fb = biome_fog[2] * (k * 0.91f + 0.09f);

    if (render_dist_chunks >= 4 && look) {
        const float l = sinf(sun_angle) > 0.0f ? -1.0f : 1.0f;
        float m = look[0] * l;                          /* look . (l, 0, 0) */
        if (m < 0.0f) m = 0.0f;
        if (m > 0.0f && have_sunrise) {
            m *= sunrise[3];
            fr = fr * (1.0f - m) + sunrise[0] * m;
            fg = fg * (1.0f - m) + sunrise[1] * m;
            fb = fb * (1.0f - m) + sunrise[2] * m;
        }
    }

    float f = 0.25f + 0.75f * (float)render_dist_chunks / 32.0f;
    f = 1.0f - powf(f, 0.25f);
    fr += (sky_rgb[0] - fr) * f;
    fg += (sky_rgb[1] - fg) * f;
    fb += (sky_rgb[2] - fb) * f;

    if (rain_level > 0.0f) {
        const float o = 1.0f - rain_level * 0.5f;
        const float p = 1.0f - rain_level * 0.4f;
        fr *= o; fg *= o; fb *= p;
    }
    if (thunder_level > 0.0f) {
        const float p = 1.0f - thunder_level * 0.5f;
        fr *= p; fg *= p; fb *= p;
    }

    out_rgb[0] = fr; out_rgb[1] = fg; out_rgb[2] = fb;
}

void sky_get_sky_color(float out_rgb[3])
{
    out_rgb[0] = sky_rgb[0]; out_rgb[1] = sky_rgb[1]; out_rgb[2] = sky_rgb[2];
}

/* ------------------------------------------------------------------ */
/* time / environment API                                              */
/* ------------------------------------------------------------------ */

void   sky_set_time(double ticks) { time_ticks = ticks; recompute(); }
double sky_get_time(void) { return time_ticks; }
double sky_get_day_time(void)
{
    double t = fmod(time_ticks, (double)SKY_TICKS_PER_DAY);
    return t < 0.0 ? t + SKY_TICKS_PER_DAY : t;
}
long   sky_get_day(void) { return (long)floor(time_ticks / (double)SKY_TICKS_PER_DAY); }
void   sky_set_day_cycle(int on) { day_cycle = on != 0; }
int    sky_get_moon_phase(void) { return moon_phase; }

void sky_set_weather(float rain, float thunder)
{
    rain_level = clampf(rain, 0.0f, 1.0f);
    thunder_level = clampf(thunder, 0.0f, 1.0f);
    recompute();
}
void sky_set_render_distance(int chunks) { render_dist_chunks = chunks < 2 ? 2 : chunks; }
void sky_set_clouds(int on) { clouds_on = on != 0; }
void sky_set_biome_colors(const float s[3], const float f[3])
{
    memcpy(biome_sky, s, sizeof biome_sky);
    memcpy(biome_fog, f, sizeof biome_fog);
    recompute();
}

float sky_get_darken(void) { return darken; }
float sky_get_brightness(void) { return brightness; }
float sky_get_time_of_day(void) { return tod; }
float sky_get_sun_angle(void) { return sun_angle; }

/* ------------------------------------------------------------------ */
/* matrices (column-major, same rotation sense as Axis.XP/YP/ZP)       */
/* ------------------------------------------------------------------ */

typedef struct { float m[16]; } Mat4;

static Mat4 m_ident(void) { Mat4 r; memset(&r, 0, sizeof r); r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }

static Mat4 m_mul(const Mat4* a, const Mat4* b)      /* a * b */
{
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a->m[k * 4 + rw] * b->m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}

static Mat4 m_rx(float deg) { Mat4 r = m_ident(); float a = deg * SKY_PI / 180.0f, c = cosf(a), s = sinf(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
static Mat4 m_ry(float deg) { Mat4 r = m_ident(); float a = deg * SKY_PI / 180.0f, c = cosf(a), s = sinf(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
static Mat4 m_rz(float deg) { Mat4 r = m_ident(); float a = deg * SKY_PI / 180.0f, c = cosf(a), s = sinf(a); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }
static Mat4 m_translate(float x, float y, float z) { Mat4 r = m_ident(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }

/* ------------------------------------------------------------------ */
/* GL objects                                                          */
/* ------------------------------------------------------------------ */

typedef struct { float p[3]; float uv[2]; float c[4]; } Vert;   /* 9 floats */

typedef struct { GLuint vao, vbo; int count; } Mesh;

static GLuint prog;
static GLint  u_vp, u_cam, u_model, u_fog_shape, u_far_z, u_tex_mode, u_layer,
u_modulator, u_fog_color, u_fog_start, u_fog_end, u_fog_on, u_alpha_test;

static Mesh   mesh_sky_disc, mesh_dark_disc, mesh_stars, mesh_quad_sun, mesh_quad_moon,
mesh_glow, mesh_clouds;
static GLuint tex_sun, tex_clouds, tex_moon;     /* tex_moon is a 2D array, layer = phase */
static int    cloud_cells_built = 0;
static int    prev_cx = 0x7FFFFFFF, prev_cy = 0x7FFFFFFF, prev_cz = 0x7FFFFFFF;

#define TEXUNIT_2D    6
#define TEXUNIT_ARRAY 7

static const char* VS =
"#version 330 core\n"
"layout(location=0) in vec3 a_pos;\n"
"layout(location=1) in vec2 a_uv;\n"
"layout(location=2) in vec4 a_col;\n"
"uniform mat4 vp; uniform vec3 cam; uniform mat4 model;\n"
"uniform int fog_shape; uniform int far_z;\n"
"out vec2 v_uv; out vec4 v_col; out float v_dist;\n"
"void main(){\n"
"  vec3 p = (model * vec4(a_pos, 1.0)).xyz;\n"                     /* camera-relative position */
"  v_dist = (fog_shape == 1) ? max(length(p.xz), abs(p.y)) : length(p);\n"   /* fog_distance(): cylinder / sphere */
"  v_uv = a_uv; v_col = a_col;\n"
"  vec4 clip = vp * vec4(cam + p, 1.0);\n"
"  if (far_z != 0) clip.z = clip.w * 0.99999;\n"                 /* sky never hits the far plane */
"  gl_Position = clip;\n"
"}\n";

static const char* FS =
"#version 330 core\n"
"in vec2 v_uv; in vec4 v_col; in float v_dist;\n"
"uniform int tex_mode;\n"                                         /* 0 none, 1 sampler2D, 2 sampler2DArray */
"uniform sampler2D tex0; uniform sampler2DArray tex_arr; uniform float layer;\n"
"uniform vec4 modulator; uniform vec3 fog_color; uniform float fog_start, fog_end;\n"
"uniform int fog_on; uniform int alpha_test;\n"
"out vec4 frag;\n"
"void main(){\n"
"  vec4 c = v_col * modulator;\n"
"  if (tex_mode == 1) c *= texture(tex0, v_uv);\n"
"  else if (tex_mode == 2) c *= texture(tex_arr, vec3(v_uv, layer));\n"
"  if (alpha_test != 0 && c.a < 0.1) discard;\n"                  /* cloud shader */
"  if (fog_on != 0 && v_dist > fog_start) {\n"                     /* linear_fog() */
"    float f = v_dist < fog_end ? smoothstep(fog_start, fog_end, v_dist) : 1.0;\n"
"    c.rgb = mix(c.rgb, fog_color, f);\n"
"  }\n"
"  frag = c;\n"
"}\n";

static GLuint compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "sky shader error:\n%s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static void mesh_create(Mesh* m, const Vert* v, int n, GLenum usage, int capacity)
{
    glGenVertexArrays(1, &m->vao);
    glGenBuffers(1, &m->vbo);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)capacity * sizeof(Vert), v, usage);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)offsetof(Vert, p));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)offsetof(Vert, uv));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)offsetof(Vert, c));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    m->count = n;
}

static void mesh_destroy(Mesh* m)
{
    if (m->vao) glDeleteVertexArrays(1, &m->vao);
    if (m->vbo) glDeleteBuffers(1, &m->vbo);
    memset(m, 0, sizeof * m);
}

static Vert mk(float x, float y, float z, float u, float v, float r, float g, float b, float a)
{
    Vert o = { { x, y, z }, { u, v }, { r, g, b, a } };
    return o;
}

/* ------------------------------------------------------------------ */
/* geometry                                                            */
/* ------------------------------------------------------------------ */

/* LevelRenderer.buildSkyDisc: fan, 9 rim vertices at radius 512 */
static void build_disc(Mesh* m, float y)
{
    Vert v[10];
    const float f = (y > 0.0f ? 1.0f : -1.0f) * 512.0f;
    v[0] = mk(0.0f, y, 0.0f, 0, 0, 1, 1, 1, 1);
    int n = 1;
    for (int k = -180; k <= 180; k += 45) {
        const float a = (float)k * (SKY_PI / 180.0f);
        v[n++] = mk(f * cosf(a), y, 512.0f * sinf(a), 0, 0, 1, 1, 1, 1);
    }
    mesh_create(m, v, n, GL_STATIC_DRAW, n);
}

/* Java's java.util.Random / LegacyRandomSource, seed 10842 */
static unsigned long long rnd_seed;
static void rnd_init(long long seed) { rnd_seed = ((unsigned long long)seed ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1); }
static int  rnd_next(int bits)
{
    rnd_seed = (rnd_seed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
    return (int)(rnd_seed >> (48 - bits));
}
static float  rnd_float(void) { return (float)rnd_next(24) * 5.9604645E-8f; }
static double rnd_double(void) { return (double)(((long long)rnd_next(26) << 27) + rnd_next(27)) * 1.1102230246251565E-16; }

/* LevelRenderer.drawStars: 1500 candidates, random quads on a sphere of radius 100 */
static void build_stars(Mesh* m)
{
    Vert* v = (Vert*)malloc(1500 * 6 * sizeof(Vert));
    if (!v) return;
    int n = 0;

    rnd_init(10842LL);
    for (int i = 0; i < 1500; i++) {
        double d = (double)(rnd_float() * 2.0f - 1.0f);
        double e = (double)(rnd_float() * 2.0f - 1.0f);
        double f = (double)(rnd_float() * 2.0f - 1.0f);
        const double g = (double)(0.15f + rnd_float() * 0.1f);
        double h = d * d + e * e + f * f;
        if (h < 1.0 && h > 0.01) {
            h = 1.0 / sqrt(h);
            d *= h; e *= h; f *= h;
            const double j = d * 100.0, k = e * 100.0, l = f * 100.0;
            const double m1 = atan2(d, f);
            const double sn = sin(m1), cs = cos(m1);
            const double p = atan2(sqrt(d * d + f * f), e);
            const double q = sin(p), r = cos(p);
            const double s = rnd_double() * (double)SKY_PI * 2.0;
            const double t = sin(s), u = cos(s);

            Vert quad[4];
            for (int vi = 0; vi < 4; vi++) {
                const double x = (double)((vi & 2) - 1) * g;
                const double y = (double)(((vi + 1) & 2) - 1) * g;
                const double aa = x * u - y * t;
                const double ab = y * u + x * t;
                const double ad = aa * q + 0.0 * r;
                const double ae = 0.0 * q - aa * r;
                const double af = ae * sn - ab * cs;
                const double ah = ab * sn + ae * cs;
                quad[vi] = mk((float)(j + af), (float)(k + ad), (float)(l + ah), 0, 0, 1, 1, 1, 1);
            }
            static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
            for (int z = 0; z < 6; z++) v[n++] = quad[idx[z]];
        }
    }
    mesh_create(m, v, n, GL_STATIC_DRAW, n);
    free(v);
}

static void build_body_quad(Mesh* m, float k, float y, int moon)
{
    Vert q[4];
    if (!moon) {
        q[0] = mk(-k, y, -k, 0, 0, 1, 1, 1, 1);
        q[1] = mk(k, y, -k, 1, 0, 1, 1, 1, 1);
        q[2] = mk(k, y, k, 1, 1, 1, 1, 1, 1);
        q[3] = mk(-k, y, k, 0, 1, 1, 1, 1, 1);
    }
    else {   /* u / v already mirrored the way the vanilla atlas lookup does */
        q[0] = mk(-k, y, k, 1, 1, 1, 1, 1, 1);
        q[1] = mk(k, y, k, 0, 1, 1, 1, 1, 1);
        q[2] = mk(k, y, -k, 0, 0, 1, 1, 1, 1);
        q[3] = mk(-k, y, -k, 1, 0, 1, 1, 1, 1);
    }
    Vert v[6] = { q[0], q[1], q[2], q[0], q[2], q[3] };
    mesh_create(m, v, 6, GL_STATIC_DRAW, 6);
}

/* ---- clouds: LevelRenderer.buildClouds (FANCY) ---- */

#define CLOUD_VERTS (64 * 204)

static Vert* cw;     /* write cursor */
static void push_quad(Vert a, Vert b, Vert c, Vert d)
{
    *cw++ = a; *cw++ = b; *cw++ = c;
    *cw++ = a; *cw++ = c; *cw++ = d;
}

static void build_clouds(double x, double y, double z)
{
    const float S = 0.00390625f;                 /* 1 / 256 */
    const float EPS = 9.765625E-4f;
    const float k = (float)floor(x) * S;
    const float l = (float)floor(z) * S;
    const float y1 = (float)floor(y / 4.0) * 4.0f;

    /* vertex colour = face shade (cloud colour is applied as a uniform), alpha 0.8 */
    const float TOP = 1.0f, BOT = 0.7f, XF = 0.9f, ZF = 0.8f, A = 0.8f;

    Vert* buf = (Vert*)malloc(CLOUD_VERTS * sizeof(Vert));
    if (!buf) return;
    cw = buf;

    for (int xi = -3; xi <= 4; xi++) {
        for (int zi = -3; zi <= 4; zi++) {
            const float b = (float)(xi * 8);
            const float c = (float)(zi * 8);

            if (y1 > -5.0f) {   /* underside */
                push_quad(mk(b + 0, y1, c + 8, (b + 0) * S + k, (c + 8) * S + l, BOT, BOT, BOT, A),
                    mk(b + 8, y1, c + 8, (b + 8) * S + k, (c + 8) * S + l, BOT, BOT, BOT, A),
                    mk(b + 8, y1, c + 0, (b + 8) * S + k, (c + 0) * S + l, BOT, BOT, BOT, A),
                    mk(b + 0, y1, c + 0, (b + 0) * S + k, (c + 0) * S + l, BOT, BOT, BOT, A));
            }
            if (y1 <= 5.0f) {   /* top */
                const float yt = y1 + 4.0f - EPS;
                push_quad(mk(b + 0, yt, c + 8, (b + 0) * S + k, (c + 8) * S + l, TOP, TOP, TOP, A),
                    mk(b + 8, yt, c + 8, (b + 8) * S + k, (c + 8) * S + l, TOP, TOP, TOP, A),
                    mk(b + 8, yt, c + 0, (b + 8) * S + k, (c + 0) * S + l, TOP, TOP, TOP, A),
                    mk(b + 0, yt, c + 0, (b + 0) * S + k, (c + 0) * S + l, TOP, TOP, TOP, A));
            }
            if (xi > -1) {      /* west faces, one strip per texel column */
                for (int ab = 0; ab < 8; ab++) {
                    const float x0 = b + (float)ab + 0.0f;
                    const float u = (b + (float)ab + 0.5f) * S + k;
                    push_quad(mk(x0, y1 + 0, c + 8, u, (c + 8) * S + l, XF, XF, XF, A),
                        mk(x0, y1 + 4, c + 8, u, (c + 8) * S + l, XF, XF, XF, A),
                        mk(x0, y1 + 4, c + 0, u, (c + 0) * S + l, XF, XF, XF, A),
                        mk(x0, y1 + 0, c + 0, u, (c + 0) * S + l, XF, XF, XF, A));
                }
            }
            if (xi <= 1) {      /* east faces */
                for (int ab = 0; ab < 8; ab++) {
                    const float x0 = b + (float)ab + 1.0f - EPS;
                    const float u = (b + (float)ab + 0.5f) * S + k;
                    push_quad(mk(x0, y1 + 0, c + 8, u, (c + 8) * S + l, XF, XF, XF, A),
                        mk(x0, y1 + 4, c + 8, u, (c + 8) * S + l, XF, XF, XF, A),
                        mk(x0, y1 + 4, c + 0, u, (c + 0) * S + l, XF, XF, XF, A),
                        mk(x0, y1 + 0, c + 0, u, (c + 0) * S + l, XF, XF, XF, A));
                }
            }
            if (zi > -1) {      /* north faces */
                for (int ab = 0; ab < 8; ab++) {
                    const float z0 = c + (float)ab + 0.0f;
                    const float v = (c + (float)ab + 0.5f) * S + l;
                    push_quad(mk(b + 0, y1 + 4, z0, (b + 0) * S + k, v, ZF, ZF, ZF, A),
                        mk(b + 8, y1 + 4, z0, (b + 8) * S + k, v, ZF, ZF, ZF, A),
                        mk(b + 8, y1 + 0, z0, (b + 8) * S + k, v, ZF, ZF, ZF, A),
                        mk(b + 0, y1 + 0, z0, (b + 0) * S + k, v, ZF, ZF, ZF, A));
                }
            }
            if (zi <= 1) {      /* south faces */
                for (int ab = 0; ab < 8; ab++) {
                    const float z0 = c + (float)ab + 1.0f - EPS;
                    const float v = (c + (float)ab + 0.5f) * S + l;
                    push_quad(mk(b + 0, y1 + 4, z0, (b + 0) * S + k, v, ZF, ZF, ZF, A),
                        mk(b + 8, y1 + 4, z0, (b + 8) * S + k, v, ZF, ZF, ZF, A),
                        mk(b + 8, y1 + 0, z0, (b + 8) * S + k, v, ZF, ZF, ZF, A),
                        mk(b + 0, y1 + 0, z0, (b + 0) * S + k, v, ZF, ZF, ZF, A));
                }
            }
        }
    }

    const int n = (int)(cw - buf);
    glBindVertexArray(mesh_clouds.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh_clouds.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)n * sizeof(Vert), buf);
    mesh_clouds.count = n;
    free(buf);
}

/* ------------------------------------------------------------------ */
/* textures                                                            */
/* ------------------------------------------------------------------ */

/* from your asset system (main.h): decodes an embedded asset to RGBA8 */
extern unsigned char* asset_image(const char* path, int* w, int* h, int* channels);

static unsigned char* load_png(const char* dir, const char* name, int* w, int* h)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    int comp = 0;
    stbi_set_flip_vertically_on_load(0);          /* uv (0,0) = top-left, like Minecraft */
    unsigned char* px = asset_image(path, w, h, &comp);
    stbi_set_flip_vertically_on_load(1);          /* restore what load_texture() expects */
    if (!px) fprintf(stderr, "sky: cannot load %s\n", path);
    return px;
}

static GLuint load_tex2d(const char* dir, const char* name, GLint wrap)
{
    int w, h;
    unsigned char* px = load_png(dir, name, &w, &h);
    if (!px) return 0;

    GLuint t;
    glGenTextures(1, &t);
    glActiveTexture(GL_TEXTURE0 + TEXUNIT_2D);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   /* vanilla: blur = false */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glActiveTexture(GL_TEXTURE0);
    stbi_image_free(px);
    return t;
}

static GLuint load_moon_array(const char* dir)
{
    /* order = vanilla moon_phases.png cell order (row 0 then row 1) = getMoonPhase() */
    static const char* names[8] = {
        "full_moon.png", "waning_gibbous.png", "third_quarter.png", "waning_crescent.png",
        "new_moon.png", "waxing_crescent.png", "first_quarter.png", "waxing_gibbous.png"
    };
    unsigned char* px[8] = { 0 };
    int w = 0, h = 0, ok = 1;

    for (int i = 0; i < 8; i++) {
        int pw, ph;
        px[i] = load_png(dir, names[i], &pw, &ph);
        if (!px[i]) { ok = 0; continue; }
        if (i == 0 || w == 0) { w = pw; h = ph; }
        else if (pw != w || ph != h) { fprintf(stderr, "sky: %s size mismatch\n", names[i]); ok = 0; }
    }

    GLuint t = 0;
    if (ok) {
        glGenTextures(1, &t);
        glActiveTexture(GL_TEXTURE0 + TEXUNIT_ARRAY);
        glBindTexture(GL_TEXTURE_2D_ARRAY, t);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, w, h, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        for (int i = 0; i < 8; i++)
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[i]);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glActiveTexture(GL_TEXTURE0);
    }
    for (int i = 0; i < 8; i++) if (px[i]) stbi_image_free(px[i]);
    return t;
}

/* ------------------------------------------------------------------ */
/* init / shutdown / update                                            */
/* ------------------------------------------------------------------ */

int sky_init(const char* dir)
{
    GLuint vs = compile(GL_VERTEX_SHADER, VS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }

    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(prog); prog = 0; return 0; }

#define U(n) glGetUniformLocation(prog, n)
    u_vp = U("vp"); u_cam = U("cam"); u_model = U("model");
    u_fog_shape = U("fog_shape"); u_far_z = U("far_z"); u_tex_mode = U("tex_mode");
    u_layer = U("layer"); u_modulator = U("modulator"); u_fog_color = U("fog_color");
    u_fog_start = U("fog_start"); u_fog_end = U("fog_end"); u_fog_on = U("fog_on");
    u_alpha_test = U("alpha_test");

    GLint prev_prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glUseProgram(prog);
    glUniform1i(U("tex0"), TEXUNIT_2D);
    glUniform1i(U("tex_arr"), TEXUNIT_ARRAY);
    glUseProgram((GLuint)prev_prog);
#undef U

    tex_sun = load_tex2d(dir, "sun.png", GL_CLAMP_TO_EDGE);
    tex_clouds = load_tex2d(dir, "clouds.png", GL_REPEAT);
    tex_moon = load_moon_array(dir);

    build_disc(&mesh_sky_disc, 16.0f);
    build_disc(&mesh_dark_disc, -16.0f);
    build_stars(&mesh_stars);
    build_body_quad(&mesh_quad_sun, 30.0f, 100.0f, 0);      /* sun: 60x60 at distance 100 */
    build_body_quad(&mesh_quad_moon, 20.0f, -100.0f, 1);    /* moon: 40x40 */
    mesh_create(&mesh_glow, NULL, 0, GL_DYNAMIC_DRAW, 18);
    mesh_create(&mesh_clouds, NULL, 0, GL_DYNAMIC_DRAW, CLOUD_VERTS);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    cloud_cells_built = 0;
    prev_cx = prev_cy = prev_cz = 0x7FFFFFFF;
    recompute();
    return 1;
}

void sky_shutdown(void)
{
    mesh_destroy(&mesh_sky_disc);
    mesh_destroy(&mesh_dark_disc);
    mesh_destroy(&mesh_stars);
    mesh_destroy(&mesh_quad_sun);
    mesh_destroy(&mesh_quad_moon);
    mesh_destroy(&mesh_glow);
    mesh_destroy(&mesh_clouds);
    if (tex_sun) glDeleteTextures(1, &tex_sun);
    if (tex_clouds) glDeleteTextures(1, &tex_clouds);
    if (tex_moon) glDeleteTextures(1, &tex_moon);
    tex_sun = tex_clouds = tex_moon = 0;
    if (prog) glDeleteProgram(prog);
    prog = 0;
}

void sky_update(double dt)
{
    const double ticks = dt * SKY_TICKS_PER_SECOND;
    if (day_cycle) time_ticks += ticks;
    cloud_ticks += ticks;
    recompute();
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

static void set_model(const Mat4* m) { glUniformMatrix4fv(u_model, 1, GL_FALSE, m->m); }

static void set_fog(int on, int cylinder, float start, float end)
{
    glUniform1i(u_fog_on, on);
    glUniform1i(u_fog_shape, cylinder);
    glUniform1f(u_fog_start, start);
    glUniform1f(u_fog_end, end);
}

static void draw_mesh(const Mesh* m, GLenum mode)
{
    glBindVertexArray(m->vao);
    glDrawArrays(mode, 0, m->count);
}

void sky_draw(const float* vp, const float* cam, const float* look)
{
    if (!prog) return;

    sky_get_fog_color(look, fog_rgb_cache);
    const float rd = (float)render_dist_chunks * 16.0f;

    GLint prev_prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    const GLboolean was_depth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean was_cull = glIsEnabled(GL_CULL_FACE);
    const GLboolean was_blend = glIsEnabled(GL_BLEND);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glUseProgram(prog);
    glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp);
    glUniform3f(u_cam, cam[0], cam[1], cam[2]);
    glUniform3f(u_fog_color, fog_rgb_cache[0], fog_rgb_cache[1], fog_rgb_cache[2]);
    glUniform1i(u_far_z, 1);
    glUniform1i(u_alpha_test, 0);
    glUniform1i(u_tex_mode, 0);

    const Mat4 I = m_ident();
    set_model(&I);

    /* 1. sky dome: flat disc 16 blocks up, sky colour, SKY fog (sphere, 0 .. render distance) */
    glUniform4f(u_modulator, sky_rgb[0], sky_rgb[1], sky_rgb[2], 1.0f);
    set_fog(1, 0, 0.0f, rd);
    draw_mesh(&mesh_sky_disc, GL_TRIANGLE_FAN);

    /* 2. sunrise / sunset glow */
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
    if (have_sunrise) {
        Vert fan[18];
        const float a = sunrise[3];
        fan[0] = mk(0.0f, 100.0f, 0.0f, 0, 0, sunrise[0], sunrise[1], sunrise[2], a);
        for (int n = 0; n <= 16; n++) {
            const float o = (float)n * (SKY_PI * 2.0f) / 16.0f;
            const float p = sinf(o), q = cosf(o);
            fan[n + 1] = mk(p * 120.0f, q * 120.0f, -q * 40.0f * a, 0, 0,
                sunrise[0], sunrise[1], sunrise[2], 0.0f);
        }
        glBindVertexArray(mesh_glow.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh_glow.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof fan, fan);
        mesh_glow.count = 18;

        const float i = sinf(sun_angle) < 0.0f ? 180.0f : 0.0f;
        Mat4 t1 = m_rx(90.0f), t2 = m_rz(i), t3 = m_rz(90.0f);
        Mat4 t = m_mul(&t1, &t2);
        t = m_mul(&t, &t3);

        set_model(&t);
        glUniform4f(u_modulator, 1, 1, 1, 1);
        set_fog(0, 0, 0.0f, 0.0f);
        draw_mesh(&mesh_glow, GL_TRIANGLE_FAN);
    }

    /* 3. sun, moon, stars: additive, rotated with the time of day */
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
    {
        Mat4 ry = m_ry(-90.0f), rx = m_rx(tod * 360.0f);
        Mat4 sky = m_mul(&ry, &rx);
        const float i = 1.0f - rain_level;

        set_model(&sky);
        set_fog(0, 0, 0.0f, 0.0f);

        if (tex_sun) {
            glActiveTexture(GL_TEXTURE0 + TEXUNIT_2D);
            glBindTexture(GL_TEXTURE_2D, tex_sun);
            glActiveTexture(GL_TEXTURE0);
            glUniform1i(u_tex_mode, 1);
            glUniform4f(u_modulator, 1, 1, 1, i);
            draw_mesh(&mesh_quad_sun, GL_TRIANGLES);
        }
        if (tex_moon) {
            glActiveTexture(GL_TEXTURE0 + TEXUNIT_ARRAY);
            glBindTexture(GL_TEXTURE_2D_ARRAY, tex_moon);
            glActiveTexture(GL_TEXTURE0);
            glUniform1i(u_tex_mode, 2);
            glUniform1f(u_layer, (float)moon_phase);
            glUniform4f(u_modulator, 1, 1, 1, i);
            draw_mesh(&mesh_quad_moon, GL_TRIANGLES);
        }

        glUniform1i(u_tex_mode, 0);
        const float u = star_bright * i;
        if (u > 0.0f) {
            glUniform4f(u_modulator, u, u, u, u);
            draw_mesh(&mesh_stars, GL_TRIANGLES);
        }
    }

    /* 4. void disc under the horizon: black, SKY fog, 4 blocks below the eye */
    glDisable(GL_BLEND);
    if ((double)cam[1] - (double)SKY_HORIZON_HEIGHT < 0.0) {
        Mat4 t = m_translate(0.0f, 12.0f, 0.0f);
        set_model(&t);
        glUniform4f(u_modulator, 0, 0, 0, 1);
        set_fog(1, 0, 0.0f, rd);
        draw_mesh(&mesh_dark_disc, GL_TRIANGLE_FAN);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram((GLuint)prev_prog);

    if (was_depth) glEnable(GL_DEPTH_TEST);
    if (was_cull)  glEnable(GL_CULL_FACE);
    if (was_blend) glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* LevelRenderer.renderClouds (FANCY): two passes, depth only then colour, so inner walls vanish */
void sky_draw_clouds(const float* vp, const float* cam)
{
    if (!prog || !clouds_on || !tex_clouds) return;

    const double e = cloud_ticks * 0.03;
    double i = ((double)cam[0] + e) / 12.0;
    const double j = (double)(SKY_CLOUD_HEIGHT - cam[1] + 0.33f);
    double k = (double)cam[2] / 12.0 + (double)0.33f;
    i -= floor(i / 2048.0) * 2048.0;
    k -= floor(k / 2048.0) * 2048.0;

    const float l = (float)(i - floor(i));
    const float m = (float)(j / 4.0 - floor(j / 4.0)) * 4.0f;
    const float n = (float)(k - floor(k));

    const int cx = (int)floor(i), cy = (int)floor(j / 4.0), cz = (int)floor(k);

    GLint prev_prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    GLint prev_func = GL_LESS;
    GLboolean prev_mask = GL_TRUE;
    glGetIntegerv(GL_DEPTH_FUNC, &prev_func);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_mask);
    const GLboolean was_depth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean was_cull = glIsEnabled(GL_CULL_FACE);
    const GLboolean was_blend = glIsEnabled(GL_BLEND);

    if (!cloud_cells_built || cx != prev_cx || cy != prev_cy || cz != prev_cz) {
        prev_cx = cx; prev_cy = cy; prev_cz = cz;
        build_clouds(i, j, k);
        cloud_cells_built = 1;
    }

    float cc[3];
    cloud_color(cc);

    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);

    glUseProgram(prog);
    glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp);
    glUniform3f(u_cam, cam[0], cam[1], cam[2]);
    glUniform3f(u_fog_color, fog_rgb_cache[0], fog_rgb_cache[1], fog_rgb_cache[2]);
    glUniform1i(u_far_z, 0);
    glUniform1i(u_alpha_test, 1);
    glUniform1i(u_tex_mode, 1);
    glUniform4f(u_modulator, cc[0], cc[1], cc[2], 1.0f);

    /* terrain fog (cylinder): FogRenderer.setupFog(FOG_TERRAIN, max(renderDistance, 32)) */
    {
        float fe = (float)render_dist_chunks * 16.0f;
        if (fe < 32.0f) fe = 32.0f;
        set_fog(1, 1, fe - clampf(fe / 10.0f, 4.0f, 64.0f), fe);
    }

    glActiveTexture(GL_TEXTURE0 + TEXUNIT_2D);
    glBindTexture(GL_TEXTURE_2D, tex_clouds);
    glActiveTexture(GL_TEXTURE0);

    /* poseStack.scale(12, 1, 12); poseStack.translate(-l, m, -n) */
    Mat4 model = m_ident();
    model.m[0] = 12.0f; model.m[10] = 12.0f;
    model.m[12] = -l * 12.0f; model.m[13] = m; model.m[14] = -n * 12.0f;
    set_model(&model);

    glBindVertexArray(mesh_clouds.vao);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDrawArrays(GL_TRIANGLES, 0, mesh_clouds.count);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDrawArrays(GL_TRIANGLES, 0, mesh_clouds.count);

    glBindVertexArray(0);
    glUseProgram((GLuint)prev_prog);

    glDepthFunc((GLenum)prev_func);
    glDepthMask(prev_mask);
    if (!was_depth) glDisable(GL_DEPTH_TEST);
    if (was_cull)   glEnable(GL_CULL_FACE);
    if (!was_blend) glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}