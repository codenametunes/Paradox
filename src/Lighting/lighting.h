#pragma once
#ifndef LIGHTING_H
#define LIGHTING_H

#include "main.h"
#include "Chunk Generation/chunkGenerator.h"
#include "Sky/sky.h"      /* time of day, sun, moon, stars, clouds */


#define LIGHT_MAX              15
#define LIGHT_PLAYER_RADIUS    10       /* blocks */

typedef const unsigned char (*LightBlocks)[CHUNK_SIZE][CHUNK_SIZE];   /* blocks[y][z][x] */

/* One of these lives inside every Chunk. Zero it (memset) before lighting_chunk_create. */
typedef struct LightChunk {
    unsigned char* data;     /* [h][CHUNK_SIZE][CHUNK_SIZE]; low nibble sky, high nibble block   */
    int            h;        /* layers allocated. Above that: sky 15, block 0 (open sky)         */
    int            dirty;    /* GPU copy out of date                                              */
    unsigned int   buf, tex; /* texture buffer (GL_R16UI) read by chunk.frag                      */
    int            gpu_h;    /* layers in the GPU copy                                            */
} LightChunk;

/* How the lighting code reaches your world. All return NULL / 0 for chunks that are not loaded. */
typedef struct LightHost {
    LightChunk* (*chunk)(int cx, int cz);
    LightBlocks(*blocks)(int cx, int cz);
    int         (*max_y)(int cx, int cz);
} LightHost;

/* ---- setup ---- */
/* program = chunk.vert + chunk.frag. Call after mesher_init. The host must outlive the module. */
void lighting_init(GLuint program, const LightHost* host);
void lighting_shutdown(void);

/* ---- per chunk ---- */
void lighting_chunk_create(LightChunk* lc);      /* after the chunk is allocated, before generate */
void lighting_chunk_destroy(LightChunk* lc);
/* call once the chunk's blocks exist and the chunk is findable through the host */
void lighting_chunk_generate(int cx, int cz);
/* call AFTER the block array / max_y were updated */
void lighting_block_changed(int x, int y, int z, int old_type, int new_type);

/* ---- per frame ---- */
/* px,py,pz = player feet (used for the L light). dt is unused now: sky_update(dt) owns time. */
void lighting_update(double dt, double px, double py, double pz);
/* with the chunk program active, before drawing chunks */
void lighting_begin(void);
/* before mesh_draw() of that chunk (uploads its light if it changed) */
void lighting_bind_chunk(LightChunk* lc, int cx, int cz, int max_y);

/* ---- player light (L) ---- */
int  lighting_toggle_player_light(void);         /* returns the new state */
void lighting_set_player_light(int on);
int  lighting_player_light(void);

/* ---- queries (entities, particles, hand ...) ---- */
void  lighting_get_light(int x, int y, int z, int* sky, int* block);
float lighting_brightness_at(double x, double y, double z);   /* 0..1, same curve as the shader */
float lighting_player_brightness_at(double x, double y, double z);

#endif