#ifndef PLAYER_H
#define PLAYER_H

struct GLFWwindow;

#define TICK_TIME            (1.0 / 20.0)  // 20 ticks a second

#define PLAYER_WIDTH         0.6
#define PLAYER_HEIGHT        1.8
#define PLAYER_HEIGHT_SNEAK  PLAYER_HEIGHT  // sneaking doesn't shrink you here (that's 1.9)
#define EYE_HEIGHT           1.62
#define EYE_HEIGHT_SNEAK     (EYE_HEIGHT - 0.08)

#define COLLISION_EPS        1.0e-7

#define PLAYER_MAX_AIR       300

#ifndef SKY_LIMIT
#define SKY_LIMIT            512
#endif

typedef struct {
    double lo[3];
    double hi[3];
} AABB;

typedef struct {
    double x, y, z;
    double px, py, pz;
    double vx, vy, vz;

    int on_ground;
    int collided_h;
    int sprinting;
    int sneaking;

    int jump_ticks;
    int sprint_timer;
    int fwd_prev;

    int flying;
    int fly_timer;
    int jump_prev;

    double fov_mod;
    double fov_prev;

    double y_size;
    double eye_h;
    double eye_prev;

    float health;
    int   food;
    float saturation;
    float exhaustion;
    int   food_timer;
    float fall_distance;
    int   hurt_resist;
    float last_damage;
    int   dead;

    int   in_water;
    int   in_lava;
    int   head_water;
    int   head_lava;
    int   air;
} Player;

extern Player player;

void   player_reset(Player* p);
void   player_tick(Player* p, struct GLFWwindow* win, float yaw_deg);
void   player_eye_pos(const Player* p, double alpha, float* out);
double player_fov_modifier(const Player* p, double alpha);
int    block_hits_player(int bx, int by, int bz);

void   player_hurt(Player* p, float amount);
void   player_add_exhaustion(float amount);

double clip_axis(const AABB* b, int axis, double d);

void tick_footsteps(void);
void player_quiet_steps(int ticks);

void  player_walk_tick(void);
float player_walk_pos(float alpha);
float player_walk_speed(float alpha);

#endif