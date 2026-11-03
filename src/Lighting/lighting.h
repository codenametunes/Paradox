#pragma once
#ifndef LIGHTING_H
#define LIGHTING_H

#include "main.h"
#include "Chunk Generation/chunkGenerator.h"
#include "Sky/sky.h"

#define LIGHT_MAX              15  // max light level
#define LIGHT_PLAYER_RADIUS    10

typedef const unsigned char (*LightBlocks)[CHUNK_SIZE][CHUNK_SIZE];

typedef struct LightChunk {
    unsigned char* data;  // 2 nibbles per byte, sky low / block high
    int            h;
    int            dirty;
    unsigned int   buf, tex;
    int            gpu_h;
} LightChunk;

typedef struct LightHost {
    LightChunk* (*chunk)(int cx, int cz);
    LightBlocks(*blocks)(int cx, int cz);
    int         (*max_y)(int cx, int cz);
} LightHost;

void lighting_init(GLuint program, const LightHost* host);
void lighting_shutdown(void);

void lighting_chunk_create(LightChunk* lc);
void lighting_chunk_destroy(LightChunk* lc);
void lighting_chunk_generate(int cx, int cz);
void lighting_block_changed(int x, int y, int z, int old_type, int new_type);

void lighting_update(double dt, double px, double py, double pz);
void lighting_begin(void);
void lighting_bind_chunk(LightChunk* lc, int cx, int cz, int max_y);

int  lighting_toggle_player_light(void);
void lighting_set_player_light(int on);
int  lighting_player_light(void);

void  lighting_get_light(int x, int y, int z, int* sky, int* block);
float lighting_brightness_at(double x, double y, double z);
float lighting_player_brightness_at(double x, double y, double z);

#endif