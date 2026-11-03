#include "main.h"
#include "mob.h"
#include "chunkGenerator.h"   // FLAT_LEVEL
#include "player.h"

int world_block(int x, int y, int z);
float fluid_height(int x, int y, int z);     // fluid.c: surface height of the fluid in a cell, -1 = none
extern int snd_walk[BLOCK_TYPES][BLOCK_SOUND_VARIANTS];
extern int snd_walk_n[BLOCK_TYPES];

Player player;
GameMode game_mode = MODE_SURVIVAL;

// water and lava are not solid: you sink into them instead of standing on them
static int is_fluid_id(int b) { return b == BLOCK_WATER || b == BLOCK_LAVA; }
static int is_solid_id(int b) { return b != BLOCK_AIR && !is_fluid_id(b); }

static int block_overlaps(const AABB* b, int x, int y, int z)
{
    return x < b->hi[0] - COLLISION_EPS && x + 1 > b->lo[0] + COLLISION_EPS &&
        y < b->hi[1] - COLLISION_EPS && y + 1 > b->lo[1] + COLLISION_EPS &&
        z < b->hi[2] - COLLISION_EPS && z + 1 > b->lo[2] + COLLISION_EPS;
}

static int box_collides(const AABB* b)
{
    for (int y = (int)floor(b->lo[1]); y <= (int)floor(b->hi[1]); y++)
        for (int z = (int)floor(b->lo[2]); z <= (int)floor(b->hi[2]); z++)
            for (int x = (int)floor(b->lo[0]); x <= (int)floor(b->hi[0]); x++)
                if (is_solid_id(world_block(x, y, z)) && block_overlaps(b, x, y, z))
                    return 1;

    return 0;
}

static int box_collides_shifted(const AABB* b, double dx, double dy, double dz)
{
    AABB s = *b;

    s.lo[0] += dx; s.hi[0] += dx;
    s.lo[1] += dy; s.hi[1] += dy;
    s.lo[2] += dz; s.hi[2] += dz;

    return box_collides(&s);
}

static AABB player_box(const Player* p)
{
    double h = PLAYER_HEIGHT;   // 1.0: same box while sneaking
    AABB b;

    b.lo[0] = p->x - PLAYER_WIDTH * 0.5;  b.hi[0] = p->x + PLAYER_WIDTH * 0.5;
    b.lo[1] = p->y;                       b.hi[1] = p->y + h;
    b.lo[2] = p->z - PLAYER_WIDTH * 0.5;  b.hi[2] = p->z + PLAYER_WIDTH * 0.5;

    return b;
}

double clip_axis(const AABB* b, int axis, double d)
{
    if (d == 0.0)
        return 0.0;

    int lo_i[3], hi_i[3];

    for (int i = 0; i < 3; i++) {
        double l = b->lo[i];
        double h = b->hi[i];

        if (i == axis) {
            if (d < 0.0) l += d;
            else         h += d;
        }

        lo_i[i] = (int)floor(l);
        hi_i[i] = (int)floor(h);
    }

    int c[3];

    for (c[0] = lo_i[0]; c[0] <= hi_i[0]; c[0]++) {
        for (c[1] = lo_i[1]; c[1] <= hi_i[1]; c[1]++) {
            for (c[2] = lo_i[2]; c[2] <= hi_i[2]; c[2]++) {

                if (!is_solid_id(world_block(c[0], c[1], c[2])))
                    continue;

                int ok = 1;

                for (int i = 0; i < 3; i++) {
                    if (i == axis)
                        continue;

                    if (!(c[i] < b->hi[i] - COLLISION_EPS && c[i] + 1 > b->lo[i] + COLLISION_EPS))
                        ok = 0;
                }

                if (!ok)
                    continue;

                if (d > 0.0 && c[axis] >= b->hi[axis] - COLLISION_EPS)
                {
                    double gap = (double)c[axis] - b->hi[axis];
                    if (gap < 0.0) gap = 0.0;
                    if (gap < d) d = gap;
                }
                else if (d < 0.0 && c[axis] + 1 <= b->lo[axis] + COLLISION_EPS)
                {
                    double gap = (double)(c[axis] + 1) - b->lo[axis];
                    if (gap > 0.0) gap = 0.0;
                    if (gap > d) d = gap;
                }
            }
        }
    }

    return d;
}

// Does the box touch fluid `id`? Like Minecraft the box is shrunk (0.4 off the bottom, a hair on the
// sides) and a cell only counts up to its surface height, so wading in a shallow puddle doesn't count.
static int box_in_fluid(const AABB* b, int id)
{
    const double lo_y = b->lo[1] + 0.4, hi_y = b->hi[1];

    for (int y = (int)floor(lo_y); y <= (int)floor(hi_y - 0.001); y++)
        for (int z = (int)floor(b->lo[2] + 0.001); z <= (int)floor(b->hi[2] - 0.001); z++)
            for (int x = (int)floor(b->lo[0] + 0.001); x <= (int)floor(b->hi[0] - 0.001); x++) {
                if (world_block(x, y, z) != id) continue;
                float h = fluid_height(x, y, z);
                if (h >= 0.0f && lo_y < y + h && hi_y > y)
                    return 1;
            }

    return 0;
}

static void player_move(Player* p, double dx, double dy, double dz)
{
    AABB b = player_box(p);


    if (p->sneaking && p->on_ground) {
        const double step = 0.05;

        while (dx != 0.0 && !box_collides_shifted(&b, dx, -1.0, 0.0))
        {
            if (dx < step && dx >= -step) dx = 0.0;
            else if (dx > 0.0)            dx -= step;
            else                          dx += step;
        }

        while (dz != 0.0 && !box_collides_shifted(&b, 0.0, -1.0, dz))
        {
            if (dz < step && dz >= -step) dz = 0.0;
            else if (dz > 0.0)            dz -= step;
            else                          dz += step;
        }

        while (dx != 0.0 && dz != 0.0 && !box_collides_shifted(&b, dx, -1.0, dz))
        {
            if (dx < step && dx >= -step) dx = 0.0;
            else if (dx > 0.0)            dx -= step;
            else                          dx += step;

            if (dz < step && dz >= -step) dz = 0.0;
            else if (dz > 0.0)            dz -= step;
            else                          dz += step;
        }
    }

    double odx = dx;
    double ody = dy;
    double odz = dz;

    dy = clip_axis(&b, 1, dy);
    b.lo[1] += dy; b.hi[1] += dy;

    if (fabs(dx) >= fabs(dz)) {
        dx = clip_axis(&b, 0, dx);
        b.lo[0] += dx; b.hi[0] += dx;

        dz = clip_axis(&b, 2, dz);
    }
    else {
        dz = clip_axis(&b, 2, dz);
        b.lo[2] += dz; b.hi[2] += dz;

        dx = clip_axis(&b, 0, dx);
    }

    p->x += dx;
    p->y += dy;
    p->z += dz;

    p->on_ground = (ody != dy) && ody < 0.0;
    p->collided_h = (odx != dx) || (odz != dz);

    if (odx != dx) p->vx = 0.0;
    if (ody != dy) p->vy = 0.0;
    if (odz != dz) p->vz = 0.0;
}

void player_reset(Player* p)
{
    memset(p, 0, sizeof * p);

    p->x = 32.5;
    p->y = (double)FLAT_LEVEL;
    p->z = 42.5;

    p->px = p->x;
    p->py = p->y;
    p->pz = p->z;

    p->y_size = 0.0;
    p->eye_h = EYE_HEIGHT;
    p->eye_prev = EYE_HEIGHT;

    p->fov_mod = 1.0;
    p->fov_prev = 1.0;

    p->health = 20.0f;
    p->food = 20;
    p->saturation = 5.0f;

    p->air = PLAYER_MAX_AIR;
}

// SURVIVAL

#define GAME_DIFFICULTY 2    // 2 = Normal: starvation stops at half a heart

void player_add_exhaustion(float amount)
{
    player.exhaustion += amount;
}

void player_hurt(Player* p, float amount)
{
    if (game_mode != MODE_SURVIVAL || p->dead || amount <= 0.0f)
        return;

    if (p->hurt_resist > 10) {            // maxHurtResistantTime / 2
        if (amount <= p->last_damage)
            return;

        float orig = amount;
        amount -= p->last_damage;
        p->last_damage = orig;
    }
    else {
        p->last_damage = amount;
        p->hurt_resist = 20;
    }

    p->health -= amount;

    if (p->health <= 0.0f) {
        p->health = 0.0f;
        p->dead = 1;
    }
}

// FoodStats.onUpdate + hurt resistance countdown (start of the tick)
static void survival_food_tick(Player* p)
{
    if (p->hurt_resist > 0)
        p->hurt_resist--;

    if (game_mode != MODE_SURVIVAL || p->dead)
        return;

    if (p->exhaustion > 4.0f) {
        p->exhaustion -= 4.0f;

        if (p->saturation > 0.0f) {
            p->saturation -= 1.0f;
            if (p->saturation < 0.0f) p->saturation = 0.0f;
        }
        else if (p->food > 0) {
            p->food--;
        }
    }

    if (p->food >= 18 && p->health > 0.0f && p->health < 20.0f) {
        if (++p->food_timer >= 80) {
            p->health += 1.0f;
            if (p->health > 20.0f) p->health = 20.0f;
            p->food_timer = 0;
        }
    }
    else if (p->food <= 0) {
        if (++p->food_timer >= 80) {
            if (p->health > 10.0f || (p->health > 1.0f && GAME_DIFFICULTY >= 2))
                player_hurt(p, 1.0f);
            p->food_timer = 0;
        }
    }
    else {
        p->food_timer = 0;
    }
}

// Entity.updateFallState + sprint exhaustion (after the move)
static void survival_move_tick(Player* p)
{
    double dy = p->y - p->py;

    if (game_mode != MODE_SURVIVAL || p->dead || p->flying) {
        p->fall_distance = 0.0f;
        return;
    }

    if (p->on_ground) {
        if (p->fall_distance > 0.0f) {
            int dmg = (int)ceilf(p->fall_distance - 3.0f);
            if (dmg > 0)
                player_hurt(p, (float)dmg);
        }
        p->fall_distance = 0.0f;
    }
    else if (dy < 0.0) {
        p->fall_distance -= (float)dy;
    }

    if (p->sprinting && p->on_ground) {
        double moved = hypot(p->x - p->px, p->z - p->pz);
        int cm = (int)floor(moved * 100.0 + 0.5);
        if (cm > 0)
            p->exhaustion += 0.099999994f * (float)cm * 0.01f;
    }
}

// one 20 Hz game tick

void player_tick(Player* p, GLFWwindow* win, float yaw_deg)
{
    p->px = p->x;
    p->py = p->y;
    p->pz = p->z;
    p->eye_prev = p->eye_h;
    p->fov_prev = p->fov_mod;

    int key_w = glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS;
    int key_s = glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS;
    int key_a = glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS;
    int key_d = glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS;
    int key_jump = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
    int key_sneak = glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

    if (p->dead)   // no control while dead
        key_w = key_s = key_a = key_d = key_jump = key_sneak = 0;

    double yaw_r = (double)yaw_deg * 3.14159265358979323846 / 180.0;

    double fx = cos(yaw_r);   // forward
    double fz = sin(yaw_r);
    double rx = -fz;          // right
    double rz = fx;

    // double tap space toggles flying, touching the ground stops it

    if (key_jump && !p->jump_prev) {
        if (p->fly_timer == 0) {
            p->fly_timer = 7;
        }
        else {
            if (game_mode == MODE_CREATIVE)     // survival can't fly
                p->flying = !p->flying;
            p->fly_timer = 0;
        }
    }

    p->jump_prev = key_jump;

    if (game_mode != MODE_CREATIVE)
        p->flying = 0;

    if (p->flying && p->on_ground)
        p->flying = 0;

    // sneaking (Release 1.0): just the shift key, also while flying (there it
    // sinks you too). The hitbox does not change, so there is no ceiling check.
    // The camera dips through ySize: EntityPlayerSP sets it to 0.2 while
    // sneaking, Entity.moveEntity then multiplies it by 0.4 every tick, so it
    // settles at 0.08 while crouched and eases back out when you let go.

    p->sneaking = key_sneak;

    if (p->sneaking && p->y_size < 0.2)
        p->y_size = 0.2;

    p->y_size *= 0.4;

    // timers

    if (p->sprint_timer > 0) p->sprint_timer--;
    if (p->jump_ticks > 0)   p->jump_ticks--;
    if (p->fly_timer > 0)    p->fly_timer--;

    survival_food_tick(p);

    // fluids (Release 1.0): in water / lava gravity, drag and jumping change and landing in them is safe
    {
        AABB fb = player_box(p);
        p->in_water = !p->flying && box_in_fluid(&fb, BLOCK_WATER);
        p->in_lava = !p->flying && !p->in_water && box_in_fluid(&fb, BLOCK_LAVA);

        if (p->in_water || p->in_lava) {
            p->fall_distance = 0.0f;
            p->sprinting = 0;
        }
    }
    const int in_liquid = p->in_water || p->in_lava;

    // input (sneaking slows it down to 30%)

    double in_fwd = (double)(key_w - key_s);
    double in_side = (double)(key_d - key_a);

    if (p->sneaking) {
        in_fwd *= 0.3;
        in_side *= 0.3;
    }

    // sprinting (Release 1.0): double tap W on the ground. There is no sprint
    // key (the Ctrl binding only arrived in 1.7.2).

    int fwd_ok = in_fwd >= 0.8;
    int food_ok = p->food > 6 || game_mode == MODE_CREATIVE;   // no sprinting when hungry

    if (p->on_ground && !p->fwd_prev && fwd_ok && !p->sprinting && food_ok) {
        if (p->sprint_timer == 0) {
            p->sprint_timer = 7;
        }
        else {
            p->sprinting = 1;
            p->sprint_timer = 0;
        }
    }

    if (p->sprinting && (!fwd_ok || p->collided_h || !food_ok))
        p->sprinting = 0;

    p->fwd_prev = fwd_ok;

    // flying: space goes up, shift goes down (fly speed 0.05 * 3)

    if (p->flying) {
        if (key_sneak) p->vy -= 0.05 * 3.0;
        if (key_jump)  p->vy += 0.05 * 3.0;
    }

    // tiny velocity cutoff (0.005 up to 1.8, 0.003 from 1.9 on)

    if (fabs(p->vx) < 0.005) p->vx = 0.0;
    if (fabs(p->vy) < 0.005) p->vy = 0.0;
    if (fabs(p->vz) < 0.005) p->vz = 0.0;

    // jumping

    if (key_jump) {
        if (in_liquid) {
            p->vy += 0.04;          // holding space pushes you up through the fluid
        }
        else if (p->on_ground && p->jump_ticks == 0) {
            p->vy = 0.42;

            if (p->sprinting) {
                p->vx += fx * 0.2;
                p->vz += fz * 0.2;
            }

            p->jump_ticks = 10;

            if (game_mode == MODE_SURVIVAL)
                player_add_exhaustion(p->sprinting ? 0.8f : 0.2f);
        }
    }
    else {
        p->jump_ticks = 0;
    }

    // movement input is scaled by 0.98 every tick

    in_fwd *= 0.98;
    in_side *= 0.98;

    double vy_before = p->vy;   // flying keeps this, see below

    // friction of the block below (every block here is 0.6)

    const double slipperiness = 0.6;

    double f4 = p->on_ground ? slipperiness * 0.91 : 0.91;

    double move_speed = p->sprinting ? 0.1 * 1.3 : 0.1;

    double accel;

    if (in_liquid)
        accel = 0.02;
    else if (p->on_ground)
        accel = move_speed * (0.16277136 / (f4 * f4 * f4));
    else if (p->flying)
        accel = 0.05;   // sprinting only speeds up flying from 1.8 (14w34d) on
    else
        accel = p->sprinting ? 0.026 : 0.02;

    // accelerate (diagonal input is normalized so it isn't faster)

    double mag = in_side * in_side + in_fwd * in_fwd;

    if (mag >= 1.0e-4) {
        mag = sqrt(mag);

        if (mag < 1.0)
            mag = 1.0;

        mag = accel / mag;

        in_side *= mag;
        in_fwd *= mag;

        p->vx += in_fwd * fx + in_side * rx;
        p->vz += in_fwd * fz + in_side * rz;
    }

    // move, then gravity and friction

    const double y_before = p->y;

    player_move(p, p->vx, p->vy, p->vz);

    survival_move_tick(p);

    if (in_liquid) {
        // water drags everything by 0.8, lava by 0.5, with a little sinking on top
        const double drag = p->in_water ? 0.8 : 0.5;
        p->vx *= drag;
        p->vy *= drag;
        p->vz *= drag;
        p->vy -= 0.02;

        // against a wall with free air just above the ledge: hop out
        if (p->collided_h) {
            AABB t = player_box(p);
            const double oy = p->vy + 0.6 - (p->y - y_before);
            t.lo[0] += p->vx; t.hi[0] += p->vx;
            t.lo[1] += oy;    t.hi[1] += oy;
            t.lo[2] += p->vz; t.hi[2] += p->vz;

            if (!box_collides(&t) && !box_in_fluid(&t, BLOCK_WATER) && !box_in_fluid(&t, BLOCK_LAVA))
                p->vy = 0.3;
        }
    }
    else {
        p->vy -= 0.08;
        p->vy *= 0.98;
        p->vx *= f4;
        p->vz *= f4;
    }

    // flying has no gravity, vertical speed just fades out by 0.6 per tick

    if (p->flying)
        p->vy = vy_before * 0.6;

    // field of view: 70 * (speed / walk speed + 1) / 2, bigger while flying,
    // eased towards the target by half every tick

    double fov_target = p->flying ? 1.1 : 1.0;

    fov_target *= ((p->sprinting ? 0.13 : 0.1) / 0.1 + 1.0) / 2.0;

    p->fov_mod += (fov_target - p->fov_mod) * 0.5;

    if (p->fov_mod > 1.5) p->fov_mod = 1.5;
    if (p->fov_mod < 0.1) p->fov_mod = 0.1;

    // camera height: standing eye height minus the ySize dip (interpolated
    // between ticks through eye_prev / eye_h, like the entity position)

    p->eye_h = EYE_HEIGHT - p->y_size;

    // eyes inside a fluid: drowning (300 ticks of air, then 2 damage per second) and lava burns
    {
        const double ey = p->y + p->eye_h - 0.11111111;
        const int ex = (int)floor(p->x), ez = (int)floor(p->z), eyi = (int)floor(ey);
        const int b = world_block(ex, eyi, ez);
        const float h = is_fluid_id(b) ? fluid_height(ex, eyi, ez) : -1.0f;
        const int under = h >= 0.0f && ey < eyi + h;

        p->head_water = under && b == BLOCK_WATER;
        p->head_lava = under && b == BLOCK_LAVA;

        if (game_mode == MODE_SURVIVAL && !p->dead) {
            if (p->head_water) {
                p->air--;
                if (p->air == -20) {
                    p->air = 0;
                    player_hurt(p, 2.0f);
                }
            }
            else {
                p->air = PLAYER_MAX_AIR;
            }

            if (p->in_lava)
                player_hurt(p, 4.0f);
        }
        else {
            p->air = PLAYER_MAX_AIR;
        }
    }

    if (p->y < 1.0) { p->y = 1.0; if (p->vy < 0.0) p->vy = 0.0; }
    if (p->y > SKY_LIMIT - 2.0) { p->y = SKY_LIMIT - 2.0; if (p->vy > 0.0) p->vy = 0.0; }
}

// camera position between the last two ticks (alpha 0..1)

void player_eye_pos(const Player* p, double alpha, float* out)
{
    out[0] = (float)(p->px + (p->x - p->px) * alpha);
    out[1] = (float)(p->py + (p->y - p->py) * alpha +
        p->eye_prev + (p->eye_h - p->eye_prev) * alpha);
    out[2] = (float)(p->pz + (p->z - p->pz) * alpha);
}

double player_fov_modifier(const Player* p, double alpha)
{
    return p->fov_prev + (p->fov_mod - p->fov_prev) * alpha;
}

// blocks can't be placed inside the player's hitbox

int block_hits_player(int bx, int by, int bz)
{
    AABB pb = player_box(&player);

    return block_overlaps(&pb, bx, by, bz);
}

// FOOTSTEPS

static double step_distance = 0.0;
static double next_step = 1.0;
static int    sfx_quiet = 0;      // ticks to hold back footsteps after a break / place

void player_quiet_steps(int ticks) { sfx_quiet = ticks; }

static void play_step_sound(void)
{
    int type = world_block((int)floor(player.x),
        (int)floor(player.y - 0.2),
        (int)floor(player.z));

    if (type > BLOCK_AIR && type < BLOCK_TYPES)
        audio_play(snd_pick(snd_walk[type], snd_walk_n[type]));
}

void tick_footsteps(void)
{
    static int was_on_ground = 1;

    if (sfx_quiet > 0)
        sfx_quiet--;

    int landed = player.on_ground && !was_on_ground;
    was_on_ground = player.on_ground;

    if (player.flying || player.sneaking) {
        step_distance = next_step - 1.0;
        return;
    }

    double dx = player.x - player.px;
    double dy = player.y - player.py;
    double dz = player.z - player.pz;

    if (player.on_ground)
        step_distance += sqrt(dx * dx + dy * dy + dz * dz) * 0.6;

    if (landed) {
        if (sfx_quiet == 0)
            play_step_sound();

        next_step = (double)((int)step_distance + 1);
        return;
    }

    if (!player.on_ground)
        return;

    if (step_distance > next_step) {
        next_step = (double)((int)step_distance + 1);

        if (sfx_quiet == 0)
            play_step_sound();
    }
}

static WalkAnim player_walk;

void player_walk_tick(void) {
    walk_tick(&player_walk, (float)hypot(player.x - player.px, player.z - player.pz));
}

float player_walk_pos(float alpha) { return walk_pos(&player_walk, alpha); }
float player_walk_speed(float alpha) { return walk_speed(&player_walk, alpha); }