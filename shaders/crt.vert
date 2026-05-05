#version 450

layout(location = 0) out vec2 uv;

void main()
{
    uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vec2 ndc = uv * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
