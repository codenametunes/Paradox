#version 330 core

// Vertex pulling: no vertex attributes. Each quad is 8 bytes in a texture
// buffer, 6 vertices are generated from it with gl_VertexID.

uniform mat4  vp;
uniform vec3  chunk_origin;
uniform usamplerBuffer quads;

out vec2      v_uv;
flat out vec4 v_ao;      // brightness at the quad's (0,0) (1,0) (0,1) (1,1) block corners
out vec3      v_pos;     // chunk local position (lighting looks up the cell in front of the face)
flat out int  v_face;

const int TRI[6] = int[6](0, 1, 2, 0, 2, 3);

// corner (u,v) of each face: 6 faces * 4 corners, matches the CCW winding of the old mesher
const int CU[24] = int[24](0,1,1,0,  1,0,0,1,  0,1,1,0,  1,0,0,1,  0,0,1,1,  0,1,1,0);
const int CV[24] = int[24](0,0,1,1,  0,0,1,1,  0,0,1,1,  0,0,1,1,  0,1,1,0,  0,0,1,1);

const float SHADE[6] = float[6](0.8, 0.8, 0.6, 0.6, 1.0, 0.5);
const float AO[4]    = float[4](0.55, 0.70, 0.85, 1.00);

void main()
{
    int quad   = gl_VertexID / 6;
    int corner = TRI[gl_VertexID - quad * 6];

    uvec2 q = texelFetch(quads, quad).rg;

    float x = float(q.x & 63u);
    float z = float((q.x >> 6) & 63u);
    float y = float(q.x >> 12);

    float w = float(q.y & 63u) + 1.0;
    float h = float((q.y >> 6) & 1023u) + 1.0;
    uint  a = (q.y >> 16) & 255u;
    int face = int(q.y >> 24);

    int k = face * 4 + corner;
    float u = float(CU[k]) * w;
    float v = float(CV[k]) * h;

    vec3 p;
    if (face < 2)       p = vec3(x + u, y + v, z + (face == 0 ? 1.0 : 0.0));
    else if (face < 4)  p = vec3(x + (face == 3 ? 1.0 : 0.0), y + v, z + u);
    else                p = vec3(x + u, y + (face == 4 ? 1.0 : 0.0), z + v);

    v_pos  = p;
    v_face = face;
    v_uv = vec2(u, v);
    v_ao = vec4(AO[a & 3u], AO[(a >> 2) & 3u], AO[(a >> 4) & 3u], AO[(a >> 6) & 3u]) * SHADE[face];

    gl_Position = vp * vec4(p + chunk_origin, 1.0);
}
