#pragma once
#include <math.h>

typedef struct MobState {
    double x, y, z;
    float  yaw, pitch;
    float  attack;
    float  age;
    float  limb_swing;
    float  limb_amount;
    int    crouch;
    int    hurt;
    float  death;

    float  head_turn;
    int    held;
    int    held_item;
    unsigned int held_tex[6];
} MobState;

typedef struct BodyTurn { float body, body_o; int ready; } BodyTurn;

static inline float wrap180(float a)  // keeps the angle between -180 and 180
{
    a = fmodf(a, 360.0f);
    if (a >= 180.0f) a -= 360.0f;
    if (a < -180.0f) a += 360.0f;
    return a;
}

static inline void bodyturn_tick(BodyTurn* b, float head_yaw, double dx, double dz, int attacking)
{
    if (!b->ready) { b->body = b->body_o = head_yaw; b->ready = 1; }
    b->body_o = b->body;

    float target = b->body;
    double dist = sqrt(dx * dx + dz * dz);

    if (dist > 0.0025) {  // magic number from the game, leave it
        float move = (float)(atan2(dz, dx) * 57.29577951308232);  // rad -> deg
        float off = fabsf(wrap180(head_yaw - move));
        target = off > 95.0f ? move - 180.0f : move;  // walking backwards so face away
    }
    if (attacking) target = head_yaw;

    b->body += wrap180(target - b->body) * 0.3f;

    float d = wrap180(head_yaw - b->body);
    if (d < -75.0f) d = -75.0f;
    if (d > 75.0f) d = 75.0f;
    b->body = head_yaw - d;
    if (d * d > 2500.0f) b->body += d * 0.2f;
}

static inline void bodyturn_get(const BodyTurn* b, float head_yaw, float a, float* body, float* head_turn)
{
    float yb = b->body_o + wrap180(b->body - b->body_o) * a;
    *body = yb;
    *head_turn = wrap180(head_yaw - yb);
}

void hand_fill_held(MobState* m, const unsigned int* block_tex);
unsigned int hand_cube_vao(void);
unsigned int hand_sword_vao(int* verts);

int  npc_aimed(const float* eye, const float* dir, float reach);
int  npc_attack(const float* eye, const float* dir, float reach, double ax, double az, float dmg);
void npc_push_player(double* pos, double* vel);

typedef struct MobType {
    const char* name;
    int  (*init)(void);
    void (*draw)(const MobState* m, const float* vp);
    void (*shutdown)(void);
} MobType;

extern const MobType MOB_STEVE;

typedef struct WalkAnim { float pos, speed, speed_o; } WalkAnim;

static inline void walk_tick(WalkAnim* w, float dist)
{
    float t = dist * 4.0f; if (t > 1.0f) t = 1.0f;
    w->speed_o = w->speed;
    w->speed += (t - w->speed) * 0.4f;
    w->pos += w->speed;
}
static inline float walk_pos(const WalkAnim* w, float a) { return w->pos - w->speed * (1.0f - a); }
static inline float walk_speed(const WalkAnim* w, float a)
{
    float s = w->speed_o + (w->speed - w->speed_o) * a;
    return s > 1.0f ? 1.0f : s;
}

int world_block(int x, int y, int z);

void npc_spawn(double x, double y, double z, float yaw);
void npc_tick(double lx, double ly, double lz);
void npc_draw(double alpha, const float* vp);

void npc_debug_draw(const float* vp, int fb_w, int fb_h, float alpha);