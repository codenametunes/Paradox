#ifndef CHUNK_MANAGER_H
#define CHUNK_MANAGER_H

#include <stdint.h>
#include <glad/gl.h>
#include "chunkGenerator.h"
#include "Lighting/lighting.h"

#define WORLD_FLOOR      0
#define RENDER_DISTANCE  16
#define WORLD_CHUNKS     ((RENDER_DISTANCE * 2) + 1)

void world_init(void);
void world_shutdown(void);

void world_load_around(double x, double z);
void world_rebuild_meshes(double x, double z);

long world_draw(const float* vp, const float* cam_pos, GLuint chunk_prog,
    const GLuint* tex_ids, int tex_count);

int  world_in_bounds(int32_t x, int32_t y, int32_t z);
int  world_get_block(int32_t x, int32_t y, int32_t z);
int  world_block(int x, int y, int z);
void world_set_block(int32_t x, int32_t y, int32_t z, int type);

int  world_raycast(const float* origin, const float* dir, float max_dist, int* hit, int* prev);

int  world_spawn_height(int bx, int bz);

const LightHost* world_light_host(void);

#endif