#pragma once
#ifndef PHYSICS_H
#define PHYSICS_H

#include "main.h"

void physics_init(int face_tex[][BLOCK_FACE_COUNT]);
void physics_shutdown(void);

void physics_block_changed(int x, int y, int z);

void physics_tick(void);

void physics_draw(const float* vp, double alpha, const GLuint* tex_ids);

int  physics_falling_count(void);

void physics_spawn_item(int type, int count, double x, double y, double z,
    double vx, double vy, double vz, int pickup_delay);

void physics_collect_items(double px, double py, double pz, int (*take)(int type, int count));

int  physics_item_count(void);

#endif