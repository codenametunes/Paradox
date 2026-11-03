#pragma once
#ifndef MESHER_H
#define MESHER_H

#include "main.h"
#include "Chunk Generation/chunkGenerator.h"

#define MESH_MAX_TEXTURES 64
#define MESH_BUCKETS      (MESH_MAX_TEXTURES * 6)

typedef const unsigned char (*MeshBlocks)[CHUNK_SIZE][CHUNK_SIZE];

typedef struct {
    MeshBlocks blocks[3][3];
    int        max_y[3][3];
} MeshInput;

typedef struct {
    GLuint vbo, tbo;
    int quad_count;
    GLsizeiptr capacity_bytes;
    int first[MESH_BUCKETS + 1];
} ChunkMesh;

void mesher_init(GLuint program, int face_tex[][BLOCK_FACE_COUNT]);
void mesher_shutdown(void);

void mesh_create(ChunkMesh* m);
void mesh_destroy(ChunkMesh* m);
void mesh_build(ChunkMesh* m, const MeshInput* in);

void     mesher_set_frustum(const float* vp);
int      mesher_aabb_visible(float x0, float y0, float z0, float x1, float y1, float z1);
unsigned mesher_face_mask(float camx, float camy, float camz, float chunk_x0, float chunk_z0, int max_y);
void     mesher_begin(const float* vp);
int      mesh_draw(const ChunkMesh* m, unsigned face_mask, float chunk_x0, float chunk_z0,
    const GLuint* tex_ids, int tex_count);

#endif