#version 330 core

uniform sampler2D      tex;
uniform usamplerBuffer light;        // unit 2: this chunk's light + 1 block border, 18 x light_h x 18
uniform sampler3D      dyn;          // unit 3: the "L" light volume around the player

uniform vec3  chunk_origin;
uniform int   light_h;               // layers in `light`; above that it is open sky
uniform float sky_darken;            // 0 (noon) .. 11 (midnight), vanilla style
uniform vec3  sky_tint;              // white by day, bluish moonlight at night
uniform float min_light;
uniform ivec3 dyn_origin;
uniform int   dyn_on;
uniform float fogStart;
uniform float fogEnd;
uniform vec3  fogColor;

in vec2      v_uv;
in vec3      v_pos;
flat in int  v_face;
flat in vec4 v_ao;

out vec4 frag;

const int PW = 18;                   // CHUNK_SIZE + 2

// face order: +Z -Z -X +X +Y -Y, U / V as in chunk.vert
const ivec3 NA[6] = ivec3[6](ivec3(0,0,1), ivec3(0,0,-1), ivec3(-1,0,0), ivec3(1,0,0), ivec3(0,1,0), ivec3(0,-1,0));
const ivec3 UA[6] = ivec3[6](ivec3(1,0,0), ivec3(1,0,0),  ivec3(0,0,1),  ivec3(0,0,1), ivec3(1,0,0), ivec3(1,0,0));
const ivec3 VA[6] = ivec3[6](ivec3(0,1,0), ivec3(0,1,0),  ivec3(0,1,0),  ivec3(0,1,0), ivec3(0,0,1), ivec3(0,0,1));

// x = sky, y = block, z = opaque
vec3 fetch_cell(ivec3 c)
{
    if (c.y >= light_h) return vec3(15.0, 0.0, 0.0);
    if (c.y < 0)        return vec3(0.0, 0.0, 1.0);

    uint  v   = texelFetch(light, (c.y * PW + (c.z + 1)) * PW + (c.x + 1)).r;
    float sky = float(v & 15u);
    float blk = float((v >> 4) & 15u);
    float op  = float((v >> 8) & 1u);

    if (dyn_on != 0) {
        ivec3 d = ivec3(chunk_origin) + c - dyn_origin;
        if (all(greaterThanEqual(d, ivec3(0))) && all(lessThan(d, ivec3(16))))
            blk = max(blk, texelFetch(dyn, d, 0).r * 15.0);
    }
    return vec3(sky, blk, op);
}

// vanilla smooth lighting for one corner: average of the cell in front of the face,
// the two side cells and the diagonal one. Solid cells don't darken the result.
vec2 corner(vec3 a, vec3 s1, vec3 s2, vec3 c)
{
    vec2 r1 = s1.z > 0.5 ? a.xy : s1.xy;
    vec2 r2 = s2.z > 0.5 ? a.xy : s2.xy;
    vec2 rc = (c.z > 0.5 || (s1.z > 0.5 && s2.z > 0.5)) ? a.xy : c.xy;
    return (a.xy + r1 + r2 + rc) * 0.25;
}

float curve(float l)                 // vanilla brightness curve
{
    float v = clamp(l, 0.0, 15.0) / 15.0;
    return v / (4.0 - 3.0 * v);
}

vec3 lightmap(vec2 L)
{
    float sky = curve(max(L.x - sky_darken, 0.0));
    float blk = curve(L.y);

    vec3 skyc = sky * sky_tint;
    vec3 blkc = vec3(blk, blk * ((blk * 0.6 + 0.4) * 0.6 + 0.4), blk * (blk * blk * 0.6 + 0.4));   // warm torch colour

    return mix(vec3(min_light), vec3(1.0), clamp(skyc + blkc, 0.0, 1.0));
}

void main()
{
    // AO is per block: interpolate it inside each 1x1 cell of the merged quad
    vec2  f = fract(v_uv);
    float s = mix(mix(v_ao.x, v_ao.y, f.x), mix(v_ao.z, v_ao.w, f.x), f.y);

    vec4 c = texture(tex, v_uv);
    if (c.a < 0.1) discard;

    // the air cell in front of this face, and where we are inside it
    ivec3 N = NA[v_face], U = UA[v_face], V = VA[v_face];
    ivec3 cell = ivec3(floor(v_pos + 0.5 * vec3(N)));
    vec3  rel  = v_pos - vec3(cell);
    vec2  t    = clamp(vec2(dot(rel, vec3(U)), dot(rel, vec3(V))), 0.0, 1.0);

    vec3 g[9];
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++)
            g[(j + 1) * 3 + (i + 1)] = fetch_cell(cell + i * U + j * V);

    vec3 a = g[4];
    vec2 c00 = corner(a, g[3], g[1], g[0]);
    vec2 c10 = corner(a, g[5], g[1], g[2]);
    vec2 c01 = corner(a, g[3], g[7], g[6]);
    vec2 c11 = corner(a, g[5], g[7], g[8]);
    vec2 L = mix(mix(c00, c10, t.x), mix(c01, c11, t.x), t.y);

    frag = vec4(c.rgb * s * lightmap(L), c.a);

    if (fogEnd > fogStart) {
        float fogDist   = 1.0 / gl_FragCoord.w;   // view-space depth
        float fogAmount = clamp((fogEnd - fogDist) / (fogEnd - fogStart), 0.0, 1.0);
        frag.rgb = mix(fogColor, frag.rgb, fogAmount);
    }
}
