#include "main.h"
#include "Third Party/stb_image.h"
#include "mob.h"
#include "Lighting/lighting.h"

static float mob_light_at(double x, double y, double z)
{
    float l = lighting_brightness_at(x, y, z);
    float p = lighting_player_brightness_at(x, y, z);
    if (p > l) l = p;
    return l > 1.0f ? 1.0f : l;
}

#define SKIN_PATH "assets/textures/mob/char.png"

typedef struct {
    float px, py, pz;
    float bx, by, bz, w, h, d;
    float u, v;
    GLuint vao, vbo;
} Part;

enum { HEAD, BODY, RARM, LARM, RLEG, LLEG, PARTS };

static Part parts[PARTS] = {
    {  0,  0, 0, -4,-8,-4, 8, 8, 8,  0,  0 },
    {  0,  0, 0, -4, 0,-2, 8,12, 4, 16, 16 },
    { -5,  2, 0, -3,-2,-2, 4,12, 4, 40, 16 },
    {  5,  2, 0, -1,-2,-2, 4,12, 4, 32, 48 },
    {-1.9f,12,0, -2, 0,-2, 4,12, 4,  0, 16 },
    { 1.9f,12,0, -2, 0,-2, 4,12, 4, 16, 48 },
};

static GLuint prog, skin;
static GLint l_mvp, l_nmat, l_tex, l_world_light;

static const char* vs =
"#version 330 core\n"
"layout(location=0) in vec3 pos;\n"
"layout(location=1) in vec2 uv;\n"
"layout(location=2) in vec3 nrm;\n"
"uniform mat4 mvp;\nuniform mat3 nmat;\n"
"out vec2 vuv;\nout vec3 vn;\n"
"void main(){vuv=uv;vn=nmat*nrm;gl_Position=mvp*vec4(pos,1.0);}\n";

static const char* fs =
"#version 330 core\n"
"in vec2 vuv;\n"
"in vec3 vn;\n"
"uniform sampler2D tex;\n"
"uniform float hurt;\n"
"uniform float world_light;\n"
"out vec4 color;\n"
"void main(){\n"
" vec4 c=texture(tex,vuv); if(c.a<0.1) discard;\n"
" vec3 n=normalize(vn);\n"
" vec3 l0=normalize(vec3(0.2,1.0,-0.7));\n"
" vec3 l1=normalize(vec3(-0.2,1.0,0.7));\n"
" float directional=min(1.0,(max(0.0,dot(l0,n))"
" +max(0.0,dot(l1,n)))*0.6+0.4);\n"
" float l=directional*clamp(world_light,0.0,1.0);\n"
" color=vec4(mix(c.rgb*l,vec3(1.0,0.0,0.0),hurt*0.3),c.a);\n"
"}\n";

static void ident(float* m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1; }
static void mul(float* o, const float* a, const float* b) {
    float r[16];
    for (int c = 0;c < 4;c++) for (int w = 0;w < 4;w++) {
        float s = 0; for (int k = 0;k < 4;k++) s += a[k * 4 + w] * b[c * 4 + k];
        r[c * 4 + w] = s;
    }
    memcpy(o, r, 64);
}
static void trans(float* m, float x, float y, float z) {
    for (int i = 0;i < 4;i++) m[12 + i] += m[i] * x + m[4 + i] * y + m[8 + i] * z;
}
static void scl(float* m, float x, float y, float z) {
    for (int i = 0;i < 4;i++) { m[i] *= x; m[4 + i] *= y; m[8 + i] *= z; }
}
static void rot(float* m, int axis, float a) {
    float c = cosf(a), s = sinf(a), r[16]; ident(r);
    if (axis == 0) { r[5] = c; r[6] = s; r[9] = -s; r[10] = c; }
    else if (axis == 1) { r[0] = c; r[2] = -s; r[8] = s; r[10] = c; }
    else { r[0] = c; r[1] = s; r[4] = -s; r[5] = c; }
    mul(m, m, r);
}

static const int FACE[6][4] = { {5,1,2,6},{0,4,7,3},{5,4,0,1},{2,3,7,6},{1,0,3,2},{4,5,6,7} };
static const float NRM[6][3] = { {1,0,0},{-1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1} };
static const int IDX[6] = { 0,1,2,0,2,3 };

static void build(const Part* p, float* o)
{
    float x0 = p->bx, x1 = x0 + p->w, y0 = p->by, y1 = y0 + p->h, z0 = p->bz, z1 = z0 + p->d;
    float c[8][3] = { {x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},
                   {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1} };
    float u0 = p->u, u1 = u0 + p->d, u2 = u1 + p->w, u3 = u2 + p->d, u4 = u3 + p->w;
    float v0 = p->v, v1 = v0 + p->d, v2 = v1 + p->h;
    float t[6][4] = { {u2,u3,v1,v2},{u0,u1,v1,v2},{u1,u2,v0,v1},
                   {u2,u3,v0,v1},{u1,u2,v1,v2},{u3,u4,v1,v2} };
    for (int f = 0;f < 6;f++) for (int k = 0;k < 6;k++) {
        int ci = IDX[k]; const float* q = c[FACE[f][ci]];
        *o++ = q[0] / 16; *o++ = q[1] / 16; *o++ = q[2] / 16;
        *o++ = ((ci == 0 || ci == 3) ? t[f][1] : t[f][0]) / 64.0f;
        *o++ = (ci < 2 ? t[f][2] : t[f][3]) / 64.0f;
        *o++ = NRM[f][0]; *o++ = NRM[f][1]; *o++ = NRM[f][2];
    }
}

static GLint l_hurt;
static int snd_hurt = -1, snd_death = -1;
extern int snd_walk[BLOCK_TYPES][BLOCK_SOUND_VARIANTS];
extern int snd_walk_n[BLOCK_TYPES];
static void poof_init(void);
static void poof_shutdown(void);
static void poof_spawn(const double* pos);
static void poof_tick(void);
static void poof_draw(double alpha, const float* vp);

static int steve_init(void)
{
    const char* src[2] = { vs,fs }; GLenum ty[2] = { GL_VERTEX_SHADER,GL_FRAGMENT_SHADER };
    prog = glCreateProgram();
    for (int i = 0;i < 2;i++) {
        GLuint s = glCreateShader(ty[i]);
        glShaderSource(s, 1, &src[i], NULL); glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(s, 512, NULL, log);
            fprintf(stderr, "mob shader: %s\n", log); return 0;
        }
        glAttachShader(prog, s); glDeleteShader(s);
    }
    glLinkProgram(prog);
    l_mvp = glGetUniformLocation(prog, "mvp");
    l_nmat = glGetUniformLocation(prog, "nmat");
    l_tex = glGetUniformLocation(prog, "tex");
    l_hurt = glGetUniformLocation(prog, "hurt");
    l_world_light = glGetUniformLocation(prog, "world_light");
    snd_hurt = audio_load("assets/sounds/steve_hurt.ogg", 1.0f);
    snd_death = audio_load("assets/sounds/steve_hurt.ogg", 1.0f);

    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* d = asset_image(SKIN_PATH, &w, &h, &ch);
    static const unsigned char fb[4] = { 198,146,107,255 };
    if (!d) { fprintf(stderr, "skin '%s': %s\n", SKIN_PATH, stbi_failure_reason()); w = h = 1; }
    glGenTextures(1, &skin);
    glBindTexture(GL_TEXTURE_2D, skin);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, d ? d : fb);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (d) stbi_image_free(d);

    for (int i = 0;i < PARTS;i++) {
        float buf[36 * 8]; build(&parts[i], buf);
        glGenVertexArrays(1, &parts[i].vao); glGenBuffers(1, &parts[i].vbo);
        glBindVertexArray(parts[i].vao);
        glBindBuffer(GL_ARRAY_BUFFER, parts[i].vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof buf, buf, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);  glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, (void*)12); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 32, (void*)20); glEnableVertexAttribArray(2);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    poof_init();
    return 1;
}

static void draw_held(const MobState* m, const float* arm_pose, const float* vp)
{
    const float D = PI / 180.0f;
    int sword = (m->held_item == ITEM_DIAMOND_SWORD);

    GLuint vao;
    int verts = 36;
    if (sword) vao = hand_sword_vao(&verts);
    else       vao = hand_cube_vao();
    if (!vao || verts <= 0) return;

    float hm[16]; memcpy(hm, arm_pose, 64);
    rot(hm, 0, -90.0f * D);
    rot(hm, 1, 180.0f * D);
    trans(hm, 1.0f / 16.0f, 2.0f / 16.0f, -10.0f / 16.0f);

    if (sword) {
        trans(hm, 0.0f, 4.0f / 16.0f, 0.5f / 16.0f);
        rot(hm, 1, -90.0f * D); rot(hm, 2, 55.0f * D);
        scl(hm, 0.85f, 0.85f, 0.85f);
    }
    else {
        trans(hm, 0.0f, 2.5f / 16.0f, 0.0f);
        rot(hm, 0, 75.0f * D); rot(hm, 1, 45.0f * D);
        scl(hm, 0.375f, 0.375f, 0.375f);
    }

    float mvp[16]; mul(mvp, vp, hm);
    float n[9] = { hm[0],hm[1],hm[2],hm[4],hm[5],hm[6],hm[8],hm[9],hm[10] };
    glUniformMatrix4fv(l_mvp, 1, GL_FALSE, mvp);
    glUniformMatrix3fv(l_nmat, 1, GL_FALSE, n);

    glBindVertexArray(vao);
    if (sword) {
        glBindTexture(GL_TEXTURE_2D, m->held_tex[0]);
        glDrawArrays(GL_TRIANGLES, 0, verts);
    }
    else {
        for (int f = 0; f < 6; f++) {
            glBindTexture(GL_TEXTURE_2D, m->held_tex[f]);
            glDrawArrays(GL_TRIANGLES, f * 6, 6);
        }
    }
    glBindTexture(GL_TEXTURE_2D, skin);
}

static void steve_draw(const MobState* m, const float* vp)
{
    if (!prog) return;

    float px[PARTS], py[PARTS], pz[PARTS], rx[PARTS] = { 0 }, ry[PARTS] = { 0 }, rz[PARTS] = { 0 };
    for (int i = 0;i < PARTS;i++) { px[i] = parts[i].px; py[i] = parts[i].py; pz[i] = parts[i].pz; }

    float head_x = -m->pitch * PI / 180.0f;
    rx[HEAD] = head_x;
    ry[HEAD] = m->head_turn * PI / 180.0f;

    float ls = m->limb_swing, la = m->limb_amount;
    rx[RARM] = cosf(ls * 0.6662f + PI) * 2.0f * la * 0.5f;
    rx[LARM] = cosf(ls * 0.6662f) * 2.0f * la * 0.5f;
    rx[RLEG] = cosf(ls * 0.6662f) * 1.4f * la;
    rx[LLEG] = cosf(ls * 0.6662f + PI) * 1.4f * la;
    ry[RLEG] = 0.005f; rz[RLEG] = 0.005f;
    ry[LLEG] = -0.005f; rz[LLEG] = -0.005f;
    pz[RLEG] = pz[LLEG] = 0.1f;

    if (m->held) rx[RARM] = rx[RARM] * 0.5f - PI / 10.0f;

    float t = m->attack;
    if (t > 0.0f) {
        float by = sinf(sqrtf(t) * PI * 2.0f) * 0.2f;
        ry[BODY] = by;
        pz[RARM] = sinf(by) * 5.0f;  px[RARM] = -cosf(by) * 5.0f;
        pz[LARM] = -sinf(by) * 5.0f;  px[LARM] = cosf(by) * 5.0f;
        ry[RARM] += by; ry[LARM] += by; rx[LARM] += by;
        float f = 1.0f - t; f *= f; f *= f; f = 1.0f - f;
        float f2 = sinf(f * PI);
        float f3 = sinf(t * PI) * -(head_x - 0.7f) * 0.75f;
        rx[RARM] -= f2 * 1.2f + f3;
        ry[RARM] += by * 2.0f;
        rz[RARM] += sinf(t * PI) * -0.4f;
    }

    if (m->crouch) {
        rx[BODY] = 0.5f; rx[RARM] += 0.4f; rx[LARM] += 0.4f;
        pz[RLEG] = pz[LLEG] = 4.0f;
        py[RLEG] = py[LLEG] = 12.2f;
        py[HEAD] = 4.2f; py[BODY] = 3.2f;
        py[RARM] = py[LARM] = 5.2f;
    }

    float z = cosf(m->age * 0.09f) * 0.05f + 0.05f, x = sinf(m->age * 0.067f) * 0.05f;
    rz[RARM] += z; rz[LARM] -= z; rx[RARM] += x; rx[LARM] -= x;

    float base[16]; ident(base);
    trans(base, (float)m->x, (float)m->y - (m->crouch ? 0.125f : 0.0f), (float)m->z);
    rot(base, 1, (270.0f - m->yaw) * PI / 180.0f);
    if (m->death > 0.0f) {
        float f = (m->death - 1.0f) / 20.0f * 1.6f; if (f < 0.0f) f = 0.0f;
        f = sqrtf(f); if (f > 1.0f) f = 1.0f;
        rot(base, 2, f * 90.0f * PI / 180.0f);
    }
    scl(base, -0.9375f, -0.9375f, 0.9375f);
    trans(base, 0.0f, -1.501f, 0.0f);

    glDisable(GL_CULL_FACE);
    glUseProgram(prog);
    glUniform1i(l_tex, 0);
    glUniform1f(l_hurt, m->hurt ? 1.0f : 0.0f);
    float world_light = mob_light_at(m->x, m->y + 1.0, m->z);
    glUniform1f(l_world_light, world_light);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, skin);

    for (int i = 0;i < PARTS;i++) {
        float mm[16]; memcpy(mm, base, 64);
        trans(mm, px[i] / 16, py[i] / 16, pz[i] / 16);
        rot(mm, 2, rz[i]); rot(mm, 1, ry[i]); rot(mm, 0, rx[i]);
        float mvp[16]; mul(mvp, vp, mm);
        float n[9] = { mm[0],mm[1],mm[2],mm[4],mm[5],mm[6],mm[8],mm[9],mm[10] };
        glUniformMatrix4fv(l_mvp, 1, GL_FALSE, mvp);
        glUniformMatrix3fv(l_nmat, 1, GL_FALSE, n);
        glBindVertexArray(parts[i].vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        if (i == RARM && m->held) draw_held(m, mm, vp);
    }
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

static void steve_shutdown(void)
{
    for (int i = 0;i < PARTS;i++) {
        glDeleteBuffers(1, &parts[i].vbo);
        glDeleteVertexArrays(1, &parts[i].vao);
    }
    glDeleteTextures(1, &skin);
    glDeleteProgram(prog);
    poof_shutdown();
    prog = 0;
}

const MobType MOB_STEVE = { "steve", steve_init, steve_draw, steve_shutdown };

#define MAX_NPCS 32

typedef struct {
    int alive;
    double p[3], po[3], v[3];
    float yaw, yaw_o, target_yaw;
    int on_ground, hcol, moving, timer;
    float health, last_dmg;
    int hurt_time, invuln, death_time;
    WalkAnim walk;
    double step_dist, next_step;
} Npc;

static Npc npcs[MAX_NPCS];

static int box_hit(const double* p)
{
    int x0 = (int)floor(p[0] - 0.3 + 1e-4), x1 = (int)floor(p[0] + 0.3 - 1e-4);
    int y0 = (int)floor(p[1] + 1e-4), y1 = (int)floor(p[1] + 1.8 - 1e-4);
    int z0 = (int)floor(p[2] - 0.3 + 1e-4), z1 = (int)floor(p[2] + 0.3 - 1e-4);
    for (int y = y0;y <= y1;y++) for (int z = z0;z <= z1;z++) for (int x = x0;x <= x1;x++)
        if (world_block(x, y, z) != BLOCK_AIR) return 1;
    return 0;
}

static void move_axis(Npc* n, int a, double d, double step)
{
    while (fabs(d) > 1e-9) {
        double s = fabs(d) < step ? d : (d > 0 ? step : -step);
        double q[3] = { n->p[0],n->p[1],n->p[2] };
        q[a] += s;
        if (box_hit(q)) {
            if (step > 0.003) { move_axis(n, a, s, 0.002); return; }
            n->v[a] = 0;
            if (a == 1 && s < 0) n->on_ground = 1;
            if (a != 1) n->hcol = 1;
            return;
        }
        n->p[a] = q[a]; d -= s;
    }
}

void npc_spawn(double x, double y, double z, float yaw)
{
    for (int i = 0;i < MAX_NPCS;i++) if (!npcs[i].alive) {
        Npc* n = &npcs[i]; memset(n, 0, sizeof * n);
        n->alive = 1; n->health = 20.0f;
        n->p[0] = n->po[0] = x; n->p[1] = n->po[1] = y; n->p[2] = n->po[2] = z;
        n->yaw = n->yaw_o = n->target_yaw = fmodf(yaw + 720.0f, 360.0f);
        n->timer = 1; n->next_step = 1.0;
        return;
    }
}

static void npc_hurt(Npc* n, float dmg, double ax, double az)
{
    if (n->health <= 0.0f) return;
    int fresh = 1;
    if (n->invuln > 10) {
        if (dmg <= n->last_dmg) return;
        n->health -= dmg - n->last_dmg; n->last_dmg = dmg; fresh = 0;
    }
    else {
        n->last_dmg = dmg; n->invuln = 20; n->health -= dmg; n->hurt_time = 10;
    }
    if (fresh) {
        double dx = ax - n->p[0], dz = az - n->p[2];
        while (dx * dx + dz * dz < 1e-4) {
            dx = (rand() / (double)RAND_MAX - rand() / (double)RAND_MAX) * 0.01;
            dz = (rand() / (double)RAND_MAX - rand() / (double)RAND_MAX) * 0.01;
        }
        double f = sqrt(dx * dx + dz * dz);
        n->v[0] /= 2; n->v[1] /= 2; n->v[2] /= 2;
        n->v[0] -= dx / f * 0.4; n->v[1] += 0.4; n->v[2] -= dz / f * 0.4;
        if (n->v[1] > 0.4) n->v[1] = 0.4;
    }
    if (n->health <= 0.0f) audio_play_3d(snd_death, n->p[0], n->p[1] + 0.9, n->p[2], 1.0f);
    else if (fresh)      audio_play_3d(snd_hurt, n->p[0], n->p[1] + 0.9, n->p[2], 1.0f);
}

static int ray_box(const float* o, const float* d, const double* lo, const double* hi, float* tout)
{
    float t0 = 0.0f, t1 = 1e9f;
    for (int i = 0;i < 3;i++) {
        if (fabsf(d[i]) < 1e-8f) { if (o[i]<lo[i] || o[i]>hi[i]) return 0; }
        else {
            float a = (float)((lo[i] - o[i]) / d[i]), b = (float)((hi[i] - o[i]) / d[i]);
            if (a > b) { float t = a; a = b; b = t; }
            if (a > t0) t0 = a;
            if (b < t1) t1 = b;
            if (t0 > t1) return 0;
        }
    }
    *tout = t0; return 1;
}

static int npc_pick(const float* o, const float* d, float reach)
{
    int best = -1; float bt = reach;
    for (int i = 0;i < MAX_NPCS;i++) {
        Npc* n = &npcs[i]; if (!n->alive || n->health <= 0.0f) continue;
        double lo[3] = { n->p[0] - 0.4,n->p[1] - 0.1,n->p[2] - 0.4 };
        double hi[3] = { n->p[0] + 0.4,n->p[1] + 1.9,n->p[2] + 0.4 };
        float t;
        if (ray_box(o, d, lo, hi, &t) && t <= bt) { bt = t; best = i; }
    }
    if (best < 0) return -1;
    for (float s = 0.0f;s < bt;s += 0.05f)
        if (world_block((int)floorf(o[0] + d[0] * s), (int)floorf(o[1] + d[1] * s), (int)floorf(o[2] + d[2] * s)) != BLOCK_AIR)
            return -1;
    return best;
}

int npc_aimed(const float* o, const float* d, float reach) { return npc_pick(o, d, reach) >= 0; }

int npc_attack(const float* o, const float* d, float reach, double ax, double az, float dmg)
{
    int best = npc_pick(o, d, reach);
    if (best < 0) return 0;
    npc_hurt(&npcs[best], dmg, ax, az);
    return 1;
}

static void push(const double* pa, double* va, const double* pb, double* vb)
{
    double d0 = pb[0] - pa[0], d1 = pb[2] - pa[2];
    double d2 = fabs(d0) > fabs(d1) ? fabs(d0) : fabs(d1);
    if (d2 < 0.01) return;
    d2 = sqrt(d2); d0 /= d2; d1 /= d2;
    double d3 = 1.0 / d2; if (d3 > 1.0) d3 = 1.0;
    d0 *= d3 * 0.05; d1 *= d3 * 0.05;
    va[0] -= d0; va[2] -= d1; vb[0] += d0; vb[2] += d1;
}

static int overlaps(const double* a, const double* b)
{
    return fabs(a[0] - b[0]) < 0.8 && fabs(a[2] - b[2]) < 0.8 && fabs(a[1] - b[1]) < 1.8;
}

void npc_push_player(double* pos, double* vel)
{
    for (int i = 0;i < MAX_NPCS;i++) {
        Npc* n = &npcs[i]; if (!n->alive || !overlaps(pos, n->p)) continue;
        push(pos, vel, n->p, n->v); push(pos, vel, n->p, n->v);
    }
}
static double lis[3];

static void npc_step_sound(const Npc* n)
{
    int t = world_block((int)floor(n->p[0]), (int)floor(n->p[1] - 0.2), (int)floor(n->p[2]));
    if (t > BLOCK_AIR && t < BLOCK_TYPES && snd_walk_n[t] > 0)
        audio_play_3d(snd_walk[t][rand() % snd_walk_n[t]], n->p[0], n->p[1], n->p[2], 1.0f);
}
static void npc_update(Npc* n)
{
    memcpy(n->po, n->p, sizeof n->p);
    n->yaw_o = n->yaw;
    if (n->hurt_time > 0) n->hurt_time--;
    if (n->invuln > 0) n->invuln--;
    int dead = n->health <= 0.0f;

    if (!dead) {
        if (--n->timer <= 0) {
            n->timer = 30 + rand() % 90;
            n->moving = rand() % 4 != 0;
            n->target_yaw = (float)(rand() % 360);
        }
        float d = fmodf(n->target_yaw - n->yaw + 540.0f, 360.0f) - 180.0f;
        n->yaw += d > 20.0f ? 20.0f : (d < -20.0f ? -20.0f : d);
        n->yaw = fmodf(n->yaw + 360.0f, 360.0f);
    }

    int ground = n->on_ground, blocked = n->hcol;
    if (!dead && ground && ((n->moving && blocked) || rand() % 60 == 0)) n->v[1] = 0.42;

    double f4 = ground ? 0.6 * 0.91 : 0.91;
    double acc = ground ? 0.1 * (0.16277136 / (f4 * f4 * f4)) : 0.02;
    if (!dead && n->moving) {
        double yr = n->yaw * PI / 180.0;
        n->v[0] += cos(yr) * acc * 0.98;
        n->v[2] += sin(yr) * acc * 0.98;
    }

    n->on_ground = 0; n->hcol = 0;
    move_axis(n, 1, n->v[1], 0.05);
    move_axis(n, 0, n->v[0], 0.05);
    move_axis(n, 2, n->v[2], 0.05);

    n->v[1] = (n->v[1] - 0.08) * 0.98;
    n->v[0] *= f4; n->v[2] *= f4;

    walk_tick(&n->walk, (float)hypot(n->p[0] - n->po[0], n->p[2] - n->po[2]));

    if (!dead && n->on_ground) {
        n->step_dist += hypot(n->p[0] - n->po[0], n->p[2] - n->po[2]) * 0.6;
        if (n->step_dist > n->next_step) {
            n->next_step = (double)((int)n->step_dist + 1);
            npc_step_sound(n);
        }
    }
    if (n->p[1] < -10.0) n->alive = 0;
    if (dead && ++n->death_time >= 20) { poof_spawn(n->p); n->alive = 0; }
}

void npc_tick(double lx, double ly, double lz)
{
    lis[0] = lx; lis[1] = ly; lis[2] = lz;
    poof_tick();
    for (int i = 0;i < MAX_NPCS;i++) if (npcs[i].alive) npc_update(&npcs[i]);

    for (int i = 0;i < MAX_NPCS;i++) for (int j = i + 1;j < MAX_NPCS;j++) {
        Npc* a = &npcs[i], * b = &npcs[j];
        if (!a->alive || !b->alive || !overlaps(a->p, b->p)) continue;
        push(a->p, a->v, b->p, b->v); push(a->p, a->v, b->p, b->v);
    }
}

void npc_draw(double alpha, const float* vp)
{
    float a = (float)alpha;
    for (int i = 0;i < MAX_NPCS;i++) {
        Npc* n = &npcs[i]; if (!n->alive) continue;
        float dy = fmodf(n->yaw - n->yaw_o + 540.0f, 360.0f) - 180.0f;
        int dead = n->health <= 0.0f;
        MobState s = {
            n->po[0] + (n->p[0] - n->po[0]) * alpha,
            n->po[1] + (n->p[1] - n->po[1]) * alpha,
            n->po[2] + (n->p[2] - n->po[2]) * alpha,
            n->yaw_o + dy * a, 0.0f, 0.0f,
            (float)(glfwGetTime() * 20.0) + i * 37.0f,
            walk_pos(&n->walk,a), walk_speed(&n->walk,a), 0,
            n->hurt_time > 0 || dead,
            dead ? n->death_time + a : 0.0f
        };
        steve_draw(&s, vp);
    }
    poof_draw(alpha, vp);
}

#define PARTICLES_PATH "assets/textures/particles.png"
#define MAX_POOF 256

typedef struct {
    double p[3], po[3], v[3];
    int alive, age, life, on_ground;
    float scale, grey;
} Poof;

static Poof poofs[MAX_POOF];
static int poof_cursor = 0;
static GLuint poof_prog, poof_vao, poof_vbo, poof_tex;
static GLint l_poof_vp, l_poof_tex;
static float poof_buf[MAX_POOF * 6 * 6];

static const char* poof_vs =
"#version 330 core\n"
"layout(location=0) in vec3 pos;\n"
"layout(location=1) in vec2 uv;\n"
"layout(location=2) in float grey;\n"
"uniform mat4 vp;\n"
"out vec2 vuv;\nout float vg;\n"
"void main(){vuv=uv;vg=grey;gl_Position=vp*vec4(pos,1.0);}\n";

static const char* poof_fs =
"#version 330 core\n"
"in vec2 vuv;\nin float vg;\n"
"uniform sampler2D tex;\n"
"out vec4 color;\n"
"void main(){\n"
" vec4 c=texture(tex,vuv); if(c.a<0.1) discard;\n"
" color=vec4(c.rgb*vg,c.a);}\n";

static void poof_init(void)
{
    const char* src[2] = { poof_vs, poof_fs };
    GLenum ty[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    poof_prog = glCreateProgram();
    for (int i = 0;i < 2;i++) {
        GLuint s = glCreateShader(ty[i]);
        glShaderSource(s, 1, &src[i], NULL); glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(s, 512, NULL, log);
            fprintf(stderr, "poof shader: %s\n", log); poof_prog = 0; return;
        }
        glAttachShader(poof_prog, s); glDeleteShader(s);
    }
    glLinkProgram(poof_prog);
    l_poof_vp = glGetUniformLocation(poof_prog, "vp");
    l_poof_tex = glGetUniformLocation(poof_prog, "tex");

    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* d = asset_image(PARTICLES_PATH, &w, &h, &ch);
    if (!d) { fprintf(stderr, "particles '%s': %s\n", PARTICLES_PATH, stbi_failure_reason()); poof_prog = 0; return; }
    glGenTextures(1, &poof_tex);
    glBindTexture(GL_TEXTURE_2D, poof_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, d);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(d);

    glGenVertexArrays(1, &poof_vao); glGenBuffers(1, &poof_vbo);
    glBindVertexArray(poof_vao);
    glBindBuffer(GL_ARRAY_BUFFER, poof_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof poof_buf, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);  glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 24, (void*)12); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 24, (void*)20); glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void poof_shutdown(void)
{
    glDeleteBuffers(1, &poof_vbo);
    glDeleteVertexArrays(1, &poof_vao);
    glDeleteTextures(1, &poof_tex);
    glDeleteProgram(poof_prog);
    poof_prog = 0;
}

static double poof_rnd(void) { return rand() / (double)RAND_MAX; }
static double poof_gauss(void)
{
    double a = poof_rnd(); if (a < 1e-9) a = 1e-9;
    return sqrt(-2.0 * log(a)) * cos(2.0 * PI * poof_rnd());
}

static void poof_spawn(const double* pos)
{
    for (int i = 0;i < 20;i++) {
        Poof* q = &poofs[poof_cursor]; poof_cursor = (poof_cursor + 1) % MAX_POOF;
        memset(q, 0, sizeof * q);
        q->p[0] = pos[0] + poof_rnd() * 1.2 - 0.6;
        q->p[1] = pos[1] + poof_rnd() * 1.8;
        q->p[2] = pos[2] + poof_rnd() * 1.2 - 0.6;
        memcpy(q->po, q->p, sizeof q->p);
        for (int a = 0;a < 3;a++)
            q->v[a] = poof_gauss() * 0.02 + (poof_rnd() * 2.0 - 1.0) * 0.05;
        q->grey = (float)(poof_rnd() * 0.3 + 0.7);
        q->scale = (float)(poof_rnd() * poof_rnd() * 6.0 + 1.0);
        q->life = (int)(16.0 / (poof_rnd() * 0.8 + 0.2)) + 2;
        q->alive = 1;
    }
}

static int poof_hit(const double* p)
{
    for (int y = (int)floor(p[1] - 0.1);y <= (int)floor(p[1] + 0.1);y++)
        for (int z = (int)floor(p[2] - 0.1);z <= (int)floor(p[2] + 0.1);z++)
            for (int x = (int)floor(p[0] - 0.1);x <= (int)floor(p[0] + 0.1);x++)
                if (world_block(x, y, z) != BLOCK_AIR) return 1;
    return 0;
}

static void poof_tick(void)
{
    for (int i = 0;i < MAX_POOF;i++) {
        Poof* q = &poofs[i]; if (!q->alive) continue;
        memcpy(q->po, q->p, sizeof q->p);
        if (q->age++ >= q->life) { q->alive = 0; continue; }

        q->v[1] += 0.004;
        q->on_ground = 0;
        static const int order[3] = { 1, 0, 2 };
        for (int k = 0;k < 3;k++) {
            int a = order[k];
            double t[3] = { q->p[0], q->p[1], q->p[2] };
            t[a] += q->v[a];
            if (poof_hit(t)) {
                if (a == 1 && q->v[1] < 0) q->on_ground = 1;
                q->v[a] = 0;
            }
            else q->p[a] = t[a];
        }
        q->v[0] *= 0.9; q->v[1] *= 0.9; q->v[2] *= 0.9;
        if (q->on_ground) { q->v[0] *= 0.7; q->v[2] *= 0.7; }
    }
}

static void poof_norm(float* v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static void poof_draw(double alpha, const float* vp)
{
    if (!poof_prog) return;
    float r[3] = { vp[0], vp[4], vp[8] }, u[3] = { vp[1], vp[5], vp[9] };
    poof_norm(r); poof_norm(u);

    static const int idx[6] = { 0,1,2,0,2,3 };
    static const float cu[4] = { 0,1,1,0 }, cv[4] = { 0,0,1,1 };
    static const float sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
    int n = 0;

    for (int i = 0;i < MAX_POOF;i++) {
        Poof* q = &poofs[i]; if (!q->alive) continue;
        float cx = (float)(q->po[0] + (q->p[0] - q->po[0]) * alpha);
        float cy = (float)(q->po[1] + (q->p[1] - q->po[1]) * alpha);
        float cz = (float)(q->po[2] + (q->p[2] - q->po[2]) * alpha);
        float s = 0.1f * q->scale;

        float lit = q->grey * mob_light_at(cx, cy, cz);

        int frame = 7 - q->age * 8 / q->life;
        if (frame < 0) frame = 0; else if (frame > 7) frame = 7;
        float u0 = frame / 8.0f, u1 = (frame + 1) / 8.0f;

        for (int k = 0;k < 6;k++) {
            int c = idx[k];
            float* o = &poof_buf[(n * 6 + k) * 6];
            o[0] = cx + (r[0] * sx[c] + u[0] * sy[c]) * s;
            o[1] = cy + (r[1] * sx[c] + u[1] * sy[c]) * s;
            o[2] = cz + (r[2] * sx[c] + u[2] * sy[c]) * s;
            o[3] = cu[c] ? u1 : u0;
            o[4] = cv[c];
            o[5] = lit;
        }
        n++;
    }
    if (!n) return;

    glDisable(GL_CULL_FACE);
    glUseProgram(poof_prog);
    glUniformMatrix4fv(l_poof_vp, 1, GL_FALSE, vp);
    glUniform1i(l_poof_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, poof_tex);
    glBindVertexArray(poof_vao);
    glBindBuffer(GL_ARRAY_BUFFER, poof_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * 6 * 6 * sizeof(float)), poof_buf);
    glDrawArrays(GL_TRIANGLES, 0, n * 6);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnable(GL_CULL_FACE);
}

static GLuint dbg_prog = 0, dbg_vao = 0, dbg_vbo = 0;
static GLint  l_dbg_vp = -1;
static float  dbg_buf[4096 * 6];
static int    dbg_n = 0;
static UiText dbg_label[MAX_NPCS];

static const char* dbg_vs =
"#version 330 core\n"
"layout(location=0) in vec3 pos;\n"
"layout(location=1) in vec3 col;\n"
"uniform mat4 vp;\n"
"out vec3 vc;\n"
"void main(){vc=col;gl_Position=vp*vec4(pos,1.0);}\n";

static const char* dbg_fs =
"#version 330 core\n"
"in vec3 vc;\nout vec4 color;\n"
"void main(){color=vec4(vc,1.0);}\n";

static void dbg_init(void)
{
    const char* src[2] = { dbg_vs, dbg_fs };
    GLenum ty[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    GLuint p = glCreateProgram();
    for (int i = 0;i < 2;i++) {
        GLuint s = glCreateShader(ty[i]);
        glShaderSource(s, 1, &src[i], NULL); glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(s, 512, NULL, log);
            fprintf(stderr, "debug shader: %s\n", log);
            glDeleteProgram(p);
            return;
        }
        glAttachShader(p, s); glDeleteShader(s);
    }
    glLinkProgram(p);
    dbg_prog = p;
    l_dbg_vp = glGetUniformLocation(dbg_prog, "vp");

    glGenVertexArrays(1, &dbg_vao); glGenBuffers(1, &dbg_vbo);
    glBindVertexArray(dbg_vao);
    glBindBuffer(GL_ARRAY_BUFFER, dbg_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof dbg_buf, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);  glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12); glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void dbg_line(double x0, double y0, double z0, double x1, double y1, double z1,
    float r, float g, float b)
{
    if (dbg_n + 2 > 4096) return;
    float* o = &dbg_buf[dbg_n * 6];
    o[0] = (float)x0; o[1] = (float)y0; o[2] = (float)z0; o[3] = r; o[4] = g; o[5] = b;
    o[6] = (float)x1; o[7] = (float)y1; o[8] = (float)z1; o[9] = r; o[10] = g; o[11] = b;
    dbg_n += 2;
}

static void dbg_box(const double* lo, const double* hi, float r, float g, float b)
{
    static const int e[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
    };
    double c[8][3] = {
        {lo[0],lo[1],lo[2]}, {hi[0],lo[1],lo[2]}, {hi[0],hi[1],lo[2]}, {lo[0],hi[1],lo[2]},
        {lo[0],lo[1],hi[2]}, {hi[0],lo[1],hi[2]}, {hi[0],hi[1],hi[2]}, {lo[0],hi[1],hi[2]}
    };
    for (int i = 0;i < 12;i++)
        dbg_line(c[e[i][0]][0], c[e[i][0]][1], c[e[i][0]][2],
            c[e[i][1]][0], c[e[i][1]][1], c[e[i][1]][2], r, g, b);
}

void npc_debug_draw(const float* vp, int fb_w, int fb_h, float alpha)
{
    if (!dbg_prog) dbg_init();
    if (!dbg_prog) return;

    dbg_n = 0;

    double pos[MAX_NPCS][3];

    for (int i = 0;i < MAX_NPCS;i++) {
        Npc* n = &npcs[i]; if (!n->alive) continue;
        for (int a = 0;a < 3;a++) pos[i][a] = n->po[a] + (n->p[a] - n->po[a]) * alpha;
        double x = pos[i][0], y = pos[i][1], z = pos[i][2];

        double lo[3] = { x - 0.3, y, z - 0.3 }, hi[3] = { x + 0.3, y + 1.8, z + 0.3 };
        dbg_box(lo, hi, 1.0f, 1.0f, 1.0f);

        double hlo[3] = { x - 0.4, y - 0.1, z - 0.4 }, hhi[3] = { x + 0.4, y + 1.9, z + 0.4 };
        dbg_box(hlo, hhi, 1.0f, 1.0f, 0.0f);

        double elo[3] = { x - 0.3, y + 1.62, z - 0.3 }, ehi[3] = { x + 0.3, y + 1.62, z + 0.3 };
        dbg_box(elo, ehi, 1.0f, 0.0f, 0.0f);

        double yr = n->yaw * PI / 180.0;
        dbg_line(x, y + 1.62, z, x + cos(yr) * 2.0, y + 1.62, z + sin(yr) * 2.0, 0.2f, 0.5f, 1.0f);
        dbg_line(x, y + 0.9, z, x + n->v[0] * 10.0, y + 0.9 + n->v[1] * 10.0, z + n->v[2] * 10.0,
            0.0f, 1.0f, 0.0f);
    }

    if (dbg_n) {
        glUseProgram(dbg_prog);
        glUniformMatrix4fv(l_dbg_vp, 1, GL_FALSE, vp);
        glBindVertexArray(dbg_vao);
        glBindBuffer(GL_ARRAY_BUFFER, dbg_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(dbg_n * 6 * sizeof(float)), dbg_buf);
        glDrawArrays(GL_LINES, 0, dbg_n);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    float scale = (float)fb_h / 480.0f;
    float origin_x = ((float)fb_w - 64 * scale) * 0.5f;

    for (int i = 0;i < MAX_NPCS;i++) {
        Npc* n = &npcs[i]; if (!n->alive) continue;
        double x = pos[i][0], y = pos[i][1], z = pos[i][2];

        double dx = x - lis[0], dy = y - lis[1], dz = z - lis[2];
        double dist = sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > 40.0) continue;

        float px = (float)x, py = (float)y + 2.3f, pz = (float)z;
        float cx = vp[0] * px + vp[4] * py + vp[8] * pz + vp[12];
        float cy = vp[1] * px + vp[5] * py + vp[9] * pz + vp[13];
        float cw = vp[3] * px + vp[7] * py + vp[11] * pz + vp[15];
        if (cw <= 0.1f) continue;

        float sx = ((cx / cw) * 0.5f + 0.5f) * (float)fb_w;
        float sy = (1.0f - ((cy / cw) * 0.5f + 0.5f)) * (float)fb_h;

        char s[320];
        snprintf(s, sizeof s,
            "Steve %d%s\n"
            "%.2f %.2f %.2f\n"
            "%.1f / 20   dmg %.1f\n",
            i, n->health <= 0.0f ? "  DEAD" : "",
            n->p[0], n->p[1], n->p[2],
            n->v[0], n->v[1], n->v[2],
            n->health, n->last_dmg,
            n->hurt_time, n->invuln, n->death_time,
            n->on_ground, n->hcol, n->moving,
            n->yaw, dist);
        text_set(&dbg_label[i], s);

        float a = 1.0f - (float)(dist / 40.0);
        if (a < 0.35f) a = 0.35f;

        text_draw(&dbg_label[i],
            (sx - origin_x) / scale - text_width(&dbg_label[i]) * 0.5f,
            sy / scale - text_height(&dbg_label[i]),
            1.0f, 1.0f, 0.7f, a, 1);
    }
}