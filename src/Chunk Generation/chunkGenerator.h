#ifndef CHUNK_GENERATOR_H
#define CHUNK_GENERATOR_H

#include <stdint.h>

#define CHUNK_SIZE       16
#define CHUNK_HEIGHT     512  // tall, 512
#define FLAT_LEVEL       96  // everything is flat for now
#define DIRT_LAYERS      3

void chunk_generator_set_seed(uint32_t seed);

void chunk_generate(unsigned char (*blocks)[CHUNK_SIZE][CHUNK_SIZE],
    int32_t cx, int32_t cz,
    int* out_max_y, uint32_t* out_solid);

int chunk_generator_surface_height(int wx, int wz);

#endif