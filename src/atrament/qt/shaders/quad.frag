#version 440
// A rounded rectangle with per-side borders and a solid or linear-gradient fill: the CPU
// renderer's formula (raster/raster.cpp drawQuad) at the pixel's centre.

layout(location = 0) in vec2 v_pos;
layout(location = 1) flat in vec4 v_outer;
layout(location = 2) flat in vec4 v_outerRadii;
layout(location = 3) flat in vec4 v_inner;
layout(location = 4) flat in vec4 v_innerRadii;
layout(location = 5) flat in vec4 v_fill;
layout(location = 6) flat in vec4 v_fill2;
layout(location = 7) flat in vec4 v_border;
layout(location = 8) flat in vec4 v_gradient;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Globals {
    mat4 transform;
    float opacity;
};

// GPUI's quad_sdf: the corner's circle where both distances point outside it, the straight edges
// elsewhere. box = centre, half size; radii = top-left, top-right, bottom-right, bottom-left.
float sdf(vec2 p, vec4 box, vec4 radii)
{
    float dx = abs(p.x - box.x) - box.z;
    float dy = abs(p.y - box.y) - box.w;
    bool left = p.x < box.x;
    float r = p.y < box.y ? (left ? radii.x : radii.y) : (left ? radii.w : radii.z);
    float vx = dx + r;
    float vy = dy + r;
    if (r > 0.0 && vx > 0.0 && vy > 0.0)
        return sqrt(vx * vx + vy * vy) - r;
    return max(dx, dy);
}

// Anti-aliasing over one pixel.
float coverage(float d)
{
    return clamp(0.5 - d, 0.0, 1.0);
}

void main()
{
    float co = coverage(sdf(v_pos, v_outer, v_outerRadii));
    // Covered by the inside of the border (all of it without a border).
    float ci = co;
    if (v_border.a > 0.0)
        ci = (v_inner.z > 0.0 && v_inner.w > 0.0) ? min(co, coverage(sdf(v_pos, v_inner, v_innerRadii))) : 0.0;
    vec4 fill = v_fill;
    if (v_gradient.w > 0.0)
        fill = mix(v_fill, v_fill2, clamp(v_gradient.x * v_pos.x + (v_gradient.y * v_pos.y + v_gradient.z), 0.0, 1.0));
    // The fill under the whole quad, the border over it in the band between the edges, mixed by
    // area in premultiplied space.
    float wb = co - ci;
    float wf = ci + (1.0 - v_border.a) * wb;
    vec4 c = clamp(fill * wf + v_border * wb, 0.0, 1.0);
    fragColor = vec4(min(c.rgb, vec3(c.a)), c.a) * opacity;
}
