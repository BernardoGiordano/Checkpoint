#version 460

// Checkpoint deko3d backend: rounded-rect (SDF) vertex shader.
// Same logical 1280x720 -> NDC mapping as quad_vsh; carries the fragment's
// local offset from the rect centre plus the rect's half-extents / corner
// radius / stroke thickness so the fragment shader can evaluate the SDF.

layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inLocal;
layout (location = 2) in vec4 inParams; // (halfW, halfH, radius, thickness)
layout (location = 3) in vec4 inColor;

layout (location = 0) out vec2 outLocal;
layout (location = 1) out vec4 outParams;
layout (location = 2) out vec4 outColor;

void main()
{
    gl_Position = vec4(inPos.x * (2.0 / 1280.0) - 1.0, 1.0 - inPos.y * (2.0 / 720.0), 0.0, 1.0);
    outLocal    = inLocal;
    outParams   = inParams;
    outColor    = inColor;
}
