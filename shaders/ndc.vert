#version 450

layout(push_constant) uniform constants {
    vec2 pos;       // NDC top-left [-1, 1]
    vec2 size;      // NDC size     [0, 2]
    vec4 color;
} push;

layout(location = 0) out vec4 frag_color;

const vec2 QUAD_CORNERS[6] = vec2[](
    vec2(0.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 0.0),
    vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0)
);

void main()
{
    vec2 corner = QUAD_CORNERS[gl_VertexIndex];
    vec2 ndc = push.pos + corner * push.size;
    gl_Position = vec4(ndc, 0.0, 1.0);
    frag_color = push.color;
}
