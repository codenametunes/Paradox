#ifndef PLAYER_H
#define PLAYER_H

// Player state, Minecraft-style movement / collision, footsteps.
// Physics run at a fixed 20 ticks per second.

struct GLFWwindow;

#define TICK_TIME            (1.0 / 20.0)

#define PLAYER_WIDTH         0.6
#define PLAYER_HEIGHT        1.8
// Release 1.0: sneaking does NOT shrink the hitbox (that came with 1.9), it only
// sinks the camera a little (Entity.ySize, see player_tick)
#define PLAYER_HEIGHT_SNEAK  PLAYER_HEIGHT
#define EYE_HEIGHT           1.62
#define EYE_HEIGHT_SNEAK     (EYE_HEIGHT - 0.08)   // steady state of the ySize dip

#define COLLISION_EPS        1.0e-7

#define PLAYER_MAX_AIR       300     // 15 seconds under water before you start drowning

#ifndef SKY_LIMIT
#define SKY_LIMIT            512     // hard ceiling of the world
#endif

typedef struct {
    double lo[3];
    double hi[3];
} AABB;

typedef struct {
    double x, y, z;          // feet position (center of the hitbox bottom)
    double px, py, pz;       // position one tick ago, for interpolation
    double vx, vy, vz;       // velocity in blocks per tick

    int on_ground;
    int collided_h;          // hit a wall during the last move
    int sprinting;
    int sneaking;

    int jump_ticks;          // delay before the next jump
    int sprint_timer;        // double tap W window
    int fwd_prev;            // was forward held last tick

    int flying;              // creative flight
    int fly_timer;           // double tap space window
    int jump_prev;           // was space held last tick

    double fov_mod;          // field of view multiplier (sprinting / flying)
    double fov_prev;

    double y_size;           // camera dip (Entity.ySize): set to 0.2 while sneaking, x0.4 per tick
    double eye_h;            // eye height = EYE_HEIGHT - y_size
    double eye_prev;

    // survival (Release 1.0 FoodStats / EntityLiving)
    float health;            // 0..20, 2 = one heart
    int   food;              // 0..20, 2 = one drumstick
    float saturation;
    float exhaustion;
    int   food_timer;        // 80 ticks per regen / starve step
    float fall_distance;
    int   hurt_resist;       // hurtResistantTime, 20 after a hit
    float last_damage;
    int   dead;

    // fluids (Release 1.0)
    int   in_water;          // body touches water: slow, floaty movement, hold space to rise
    int   in_lava;
    int   head_water;        // eyes under water: overlay + drowning
    int   head_lava;
    int   air;               // breath in ticks, PLAYER_MAX_AIR when dry
} Player;

extern Player player;        // the one local player

// --- physics ---
void   player_reset(Player* p);
void   player_tick(Player* p, struct GLFWwindow* win, float yaw_deg);   // one 20 Hz tick
void   player_eye_pos(const Player* p, double alpha, float* out);
double player_fov_modifier(const Player* p, double alpha);
int    block_hits_player(int bx, int by, int bz);   // would a block here overlap the player?

// --- survival ---
// damage in health points (2 = one heart). Ignored in creative / while dead.
// Uses the vanilla 20 tick invulnerability: a second hit inside it only counts
// for the part that is bigger than the last one.
void   player_hurt(Player* p, float amount);
void   player_add_exhaustion(float amount);

// swept box against the world: how far can `b` move along `axis` (0=x,1=y,2=z)
// by `d` before touching a solid block (also used by the particles)
double clip_axis(const AABB* b, int axis, double d);

// --- footsteps ---
void tick_footsteps(void);               // call once per tick after player_tick
void player_quiet_steps(int ticks);      // mute footsteps for a few ticks (after break/place)

// --- walk animation (third person model) ---
void  player_walk_tick(void);            // call once per tick after player_tick
float player_walk_pos(float alpha);
float player_walk_speed(float alpha);

#endif