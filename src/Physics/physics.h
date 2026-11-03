#pragma once
#ifndef PHYSICS_H
#define PHYSICS_H

#include "main.h"

/*
 * Block physics (falling sand), modelled on Minecraft:
 *  - a gravity block with air/water below is converted to a falling entity
 *    2 game ticks after it was placed or the block under it was removed
 *  - entity gravity 0.04 blocks/tick^2, drag 0.98, 20 Hz like the rest of the game
 *  - on landing it turns back into a block (replacing air/water)
 *  - the block that was resting on top of a sand column falls too (chain reaction)
 */

 /* face_tex = block_face_tex from main.c (same array that goes to mesher_init) */
void physics_init(int face_tex[][BLOCK_FACE_COUNT]);
void physics_shutdown(void);

/* call after ANY world_set_block() made by the player (or other systems) */
void physics_block_changed(int x, int y, int z);

/* one 20 Hz game tick */
void physics_tick(void);

/* draw falling blocks; alpha = tick_acc / TICK_TIME, tex_ids = the block texture array */
void physics_draw(const float* vp, double alpha, const GLuint* tex_ids);

int  physics_falling_count(void);

/* ---- dropped items (survival drops) ----
 * Same rules as Minecraft's EntityItem: 0.25 box, gravity 0.04, drag 0.98, ground
 * friction 0.588, bounces x -0.5 on landing, spins and bobs, despawns after 5 minutes.
 * They can't be picked up for `pickup_delay` ticks (10 for a broken block, 40 when the
 * player drops things on death). Only block ids (< BLOCK_TYPES) can be spawned. */
void physics_spawn_item(int type, int count, double x, double y, double z,
    double vx, double vy, double vz, int pickup_delay);

/* call once per tick with the player's feet. take(type, count) returns how many of
 * them the player could take (0 = inventory full, the item stays on the ground) */
void physics_collect_items(double px, double py, double pz, int (*take)(int type, int count));

int  physics_item_count(void);

#endif