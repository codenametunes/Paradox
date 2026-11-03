#pragma once
#include <math.h>

typedef struct MobState {
    double x, y, z;
    float  yaw, pitch;       // degrees, up = positive
    float  attack;           // swing progress 0..1
    float  age;              // ticks
    float  limb_swing;       // walk animation position
    float  limb_amount;      // walk animation speed 0..1
    int    crouch;
    int    hurt;     // red tint
    float  death;    // deathTime + partial, 0 = alive

    // --- appended at the END so existing positional initializers keep working (all zero = old look)
    float  head_turn;        // degrees, head yaw RELATIVE to the body (yaw). 0 = head follows body
    int    held;             // 1 = something is in the right hand
    int    held_item;        // item id (what hand_item() returns), only read when held = 1
    unsigned int held_tex[6];// textures of the held item: blocks use all 6 faces, the sword uses [0]
} MobState;

// ---- head / body turning (LivingEntity.tickHeadTurn) ----
// The head follows the camera instantly. The body only follows once the head is more than
// 75 degrees away from it, or when you walk (then it turns towards the walking direction).
// yaw here uses the same convention as MobState.yaw / Npc.yaw: forward = (cos yaw, sin yaw)
typedef struct BodyTurn { float body, body_o; int ready; } BodyTurn;

static inline float wrap180(float a)
{
    a = fmodf(a, 360.0f);
    if (a >= 180.0f) a -= 360.0f;
    if (a < -180.0f) a += 360.0f;
    return a;
}

// 20 Hz. head_yaw = camera yaw, dx/dz = horizontal distance moved this tick,
// attacking = 1 while the swing animation plays (the body snaps to the head then, like the game)
static inline void bodyturn_tick(BodyTurn* b, float head_yaw, double dx, double dz, int attacking)
{
    if (!b->ready) { b->body = b->body_o = head_yaw; b->ready = 1; }
    b->body_o = b->body;

    float target = b->body;
    double dist = sqrt(dx * dx + dz * dz);

    if (dist > 0.0025) {
        float move = (float)(atan2(dz, dx) * 57.29577951308232);
        float off = fabsf(wrap180(head_yaw - move));
        target = off > 95.0f ? move - 180.0f : move;      // walking backwards: face away
    }
    if (attacking) target = head_yaw;

    b->body += wrap180(target - b->body) * 0.3f;

    float d = wrap180(head_yaw - b->body);
    if (d < -75.0f) d = -75.0f;
    if (d > 75.0f) d = 75.0f;
    b->body = head_yaw - d;
    if (d * d > 2500.0f) b->body += d * 0.2f;
}

// per frame: fills MobState.yaw (body) and MobState.head_turn. head_yaw = current camera yaw
static inline void bodyturn_get(const BodyTurn* b, float head_yaw, float a, float* body, float* head_turn)
{
    float yb = b->body_o + wrap180(b->body - b->body_o) * a;
    *body = yb;
    *head_turn = wrap180(head_yaw - yb);
}

// hand.c: fills held / held_item / held_tex of a MobState from what the first person hand shows
// (so the third person model holds the same item, and swaps it at the same moment).
// block_tex = the same 6 textures you pass to hand_draw
void hand_fill_held(MobState* m, const unsigned int* block_tex);
// hand.c: the shared item meshes (unit cube centered on the origin / flat sword slab)
unsigned int hand_cube_vao(void);
unsigned int hand_sword_vao(int* verts);

int  npc_aimed(const float* eye, const float* dir, float reach);   // 1 = a mob is under the crosshair
int  npc_attack(const float* eye, const float* dir, float reach, double ax, double az, float dmg);
void npc_push_player(double* pos, double* vel);   // pass &player.x, &player.vx

typedef struct MobType {
    const char* name;
    int  (*init)(void);
    void (*draw)(const MobState* m, const float* vp);
    void (*shutdown)(void);
} MobType;

extern const MobType MOB_STEVE;

// WalkAnimationState: tick at 20 Hz with the horizontal distance moved
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

// implemented in main.c
int world_block(int x, int y, int z);

// NPC Steves (mob_steve.c)
void npc_spawn(double x, double y, double z, float yaw);
void npc_tick(double lx, double ly, double lz);   // 20 Hz
void npc_draw(double alpha, const float* vp);

void npc_debug_draw(const float* vp, int fb_w, int fb_h, float alpha);