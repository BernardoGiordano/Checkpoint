#version 460

// Checkpoint deko3d backend: 2D UI quad fragment shader.
// Samples the bound texture and modulates by the vertex color; solid rects
// go through the same path by sampling a 1x1 white texture.

layout (location = 0) in vec2 inUv;
layout (location = 1) in vec4 inColor;

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D tex;

void main()
{
    outColor = inColor * texture(tex, inUv);
}
