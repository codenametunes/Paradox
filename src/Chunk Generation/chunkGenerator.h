#ifndef CHUNK_GENERATOR_H
#define CHUNK_GENERATOR_H

#include <stdint.h>

// Chunk dimensions and terrain parameters. These live here so that
// main.c (storage / meshing) and chunkGenerator.c (terrain) agree on them.
#define CHUNK_SIZE       16
#define CHUNK_HEIGHT     512
#define FLAT_LEVEL       96   /* base height; lowest terrain ~ y=65, above the sky's y=63 horizon */
#define DIRT_LAYERS      3

void chunk_generator_set_seed(uint32_t seed);

// Fills a chunk's block array with terrain (currently superflat).
//
//   blocks      the chunk's [y][z][x] block array (CHUNK_HEIGHT x CHUNK_SIZE x CHUNK_SIZE)
//   cx, cz      chunk coordinates, unused by superflat but needed for noise / seeds later
//   out_max_y   receives the highest non-air block
//   out_solid   receives the number of non-air blocks
void chunk_generate(unsigned char (*blocks)[CHUNK_SIZE][CHUNK_SIZE],
    int32_t cx, int32_t cz,
    int* out_max_y, uint32_t* out_solid);

// 2D surface height of a world column, used to find a spawn point
int chunk_generator_surface_height(int wx, int wz);

#endif