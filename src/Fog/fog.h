#ifndef FOG_H
#define FOG_H

#include <glad/gl.h>

void fog_set_render_distance(int chunks);
void fog_set_color(const float* rgb);
void fog_toggle(void);
int  fog_enabled(void);

void fog_apply(GLuint program);

#endif