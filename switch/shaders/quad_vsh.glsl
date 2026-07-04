#version 460

// Checkpoint deko3d backend: 2D UI quad vertex shader.
// Input positions are in the app's logical 1280x720 pixel space, origin at
// the top-left (matching the SDL renderer the UI was written against).

layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inUv;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec2 outUv;
layout (location = 1) out vec4 outColor;

void main()
{
    gl_Position = vec4(inPos.x * (2.0 / 1280.0) - 1.0, 1.0 - inPos.y * (2.0 / 720.0), 0.0, 1.0);
    outUv       = inUv;
    outColor    = inColor;
}
