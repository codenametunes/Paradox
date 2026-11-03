#version 330 core

in vec2 vuv;
in float vshade;

uniform sampler2D tex;
uniform float fogStart;
uniform float fogEnd;
uniform vec3  fogColor;

out vec4 color;

void main()
{
    vec4 t = texture(tex, vuv);

    if (t.a < 0.5)
        discard;

    color = vec4(t.rgb * vshade, 1.0);

    if (fogEnd > fogStart) {
        float fogDist   = 1.0 / gl_FragCoord.w;   // view-space depth
        float fogAmount = clamp((fogEnd - fogDist) / (fogEnd - fogStart), 0.0, 1.0);
        color.rgb = mix(fogColor, color.rgb, fogAmount);
    }
}
