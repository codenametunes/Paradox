#pragma once
#ifndef FLUID_H
#define FLUID_H

#include "main.h"

/*
 * Water + lava, modelled on Minecraft's BlockDynamicLiquid.
 *
 * World stores only BLOCK_WATER / BLOCK_LAVA. The flow state of every fluid
 * cell lives in a side table inside fluid.c:
 *     meta 0      source
 *     meta 1..7   flowing, higher = shallower
 *     meta 8..15  falling column (full height, spreads sideways as meta 1)
 *
 * Rules:
 *  - water: tick every 5 game ticks, spreads 7 blocks;  lava: every 30, 3 blocks
 *  - falls straight down any distance, spreads sideways when it lands
 *    (prefers the direction that leads to a drop within 4 blocks)
 *  - flowing cells re-evaluate when a neighbour changes, so removing the
 *    source drains the stream and removing a block lets it flow in
 *  - 2 adjacent water sources + solid floor (or a water source below) -> new source
 *  - lava + water -> stone
 *
 * The mesher does NOT draw fluid blocks; fluid_draw() does (sloped surface,
 * water blended, lava opaque).
 */

 /* tex = the GL texture of Fluids.png (tex_id[block_face_tex[BLOCK_WATER][0]]) */
void fluid_init(GLuint tex);
void fluid_shutdown(void);

/* call after ANY world_set_block() by player/other systems (physics_block_changed does it for you) */
void fluid_block_changed(int x, int y, int z);

/* one 20 Hz game tick */
void fluid_tick(void);

/* cam = float[3] camera position, used to limit what gets built */
void fluid_draw(const float* vp, const float* cam);

int  fluid_count(void);

/* height (0..1) of the fluid surface inside this cell, -1 if the cell holds no fluid */
float fluid_height(int x, int y, int z);

/* same contract as world_raycast, but water and lava are looked through (can't be targeted),
   so you can build and mine below the surface. hit = first solid block, prev = cell before it. */
int  fluid_raycast(const float* origin, const float* dir, float reach, int* hit, int* prev);

/* full screen water / lava overlay while the camera is inside a fluid (kind 0 = water, 1 = lava).
   Call after the world + hand, before the HUD. */
void fluid_draw_overlay(int kind, float yaw_deg, float pitch_deg, int fb_w, int fb_h);

static inline int fluid_is_fluid(int id) { return id == BLOCK_WATER || id == BLOCK_LAVA; }

#endif