#version 330 core

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in float shade;

uniform mat4 vp;

out vec2 vuv;
out float vshade;

void main()
{
    vuv = uv;
    vshade = shade;
    gl_Position = vp * vec4(pos, 1.0);
}