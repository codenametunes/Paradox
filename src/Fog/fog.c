#include "fog.h"
#include "Chunk Generation/chunkGenerator.h"   // CHUNK_SIZE

#define FOG_START_FRACTION 0.6f   // fog begins at this fraction of the end distance

static float fog_start = 0.0f;
static float fog_end = 0.0f;
static float fog_rgb[3] = { 0.47f, 0.65f, 1.0f };
static int   fog_on = 1;

void fog_set_render_distance(int chunks)
{
    // the loaded area is a square: along an axis it reaches `chunks` chunks, and
    // the outermost one pops in while streaming, so end the fog a chunk earlier
    int safe = chunks > 1 ? chunks - 1 : 1;
    fog_end = (float)(safe * CHUNK_SIZE);
    fog_start = fog_end * FOG_START_FRACTION;
}

void fog_set_color(const float* rgb)
{
    fog_rgb[0] = rgb[0];
    fog_rgb[1] = rgb[1];
    fog_rgb[2] = rgb[2];
}

void fog_toggle(void) { fog_on = !fog_on; }
int  fog_enabled(void) { return fog_on; }

void fog_apply(GLuint program)
{
    glUseProgram(program);

    GLint s = glGetUniformLocation(program, "fogStart");
    GLint e = glGetUniformLocation(program, "fogEnd");
    GLint c = glGetUniformLocation(program, "fogColor");

    // fogEnd <= fogStart makes the shader skip fog entirely
    if (s >= 0) glUniform1f(s, fog_on ? fog_start : 0.0f);
    if (e >= 0) glUniform1f(e, fog_on ? fog_end : 0.0f);
    if (c >= 0) glUniform3f(c, fog_rgb[0], fog_rgb[1], fog_rgb[2]);
}