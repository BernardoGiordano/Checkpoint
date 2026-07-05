#version 460

// Checkpoint deko3d backend: rounded-rect (SDF) fragment shader.
// Evaluates a signed-distance rounded box and turns it into ~1px
// antialiased coverage, replacing the row-of-1px-rects corner rasterisation
// the SDL backend used. thickness <= 0 fills the shape; thickness > 0 draws a
// hollow ring of that width, inset from the outer edge (inner corner radius =
// radius - thickness), matching Shapes::strokeRound.

layout (location = 0) in vec2 inLocal;
layout (location = 1) in vec4 inParams; // (halfW, halfH, radius, thickness)
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec4 outColor;

float sdRoundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

void main()
{
    vec2 halfExt    = inParams.xy;
    float radius    = inParams.z;
    float thickness = inParams.w;

    float d  = sdRoundBox(inLocal, halfExt, radius);
    float aa = max(fwidth(d), 1e-4); // ~1px edge in screen space

    float cov;
    if (thickness > 0.0) {
        float outer = 1.0 - smoothstep(-aa, aa, d);
        float inner = 1.0 - smoothstep(-aa, aa, d + thickness);
        cov         = clamp(outer - inner, 0.0, 1.0);
    }
    else {
        cov = 1.0 - smoothstep(-aa, aa, d);
    }

    outColor = vec4(inColor.rgb, inColor.a * cov);
}
