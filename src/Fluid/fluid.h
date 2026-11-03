#pragma once
#ifndef FLUID_H
#define FLUID_H

#include "main.h"

void fluid_init(GLuint tex);
void fluid_shutdown(void);
void fluid_block_changed(int x, int y, int z);
void fluid_tick(void);
void fluid_draw(const float* vp, const float* cam);
int  fluid_count(void);
float fluid_height(int x, int y, int z);
int  fluid_raycast(const float* origin, const float* dir, float reach, int* hit, int* prev);
void fluid_draw_overlay(int kind, float yaw_deg, float pitch_deg, int fb_w, int fb_h);
static inline int fluid_is_fluid(int id) { return id == BLOCK_WATER || id == BLOCK_LAVA; }

#endif