#ifndef FOG_H
#define FOG_H

#include <glad/gl.h>

// Linear distance fog. The shaders do the blending; this module only owns the
// range, the colour and the on/off switch and uploads them as uniforms:
//     uniform float fogStart;   uniform float fogEnd;   uniform vec3 fogColor;
// A program that doesn't declare them is simply left alone (location -1).

void fog_set_render_distance(int chunks);   // fog ends one chunk inside the loaded area
void fog_set_color(const float* rgb);       // use the sky's fog colour so the blend matches the clear colour
void fog_toggle(void);
int  fog_enabled(void);

// binds `program` and uploads the fog uniforms
void fog_apply(GLuint program);

#endif