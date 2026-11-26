#version 440
// Atrament's quads (qt/gpuframe.h GpuQuadInstance): a unit square per instance, stretched over the
// pixels it draws; the fragment shader gets the device-pixel position and the instance's shape.

layout(location = 0) in vec2 corner;
layout(location = 1) in vec4 rect;
layout(location = 2) in vec4 outer;
layout(location = 3) in vec4 outerRadii;
layout(location = 4) in vec4 inner;
layout(location = 5) in vec4 innerRadii;
layout(location = 6) in vec4 fill;
layout(location = 7) in vec4 fill2;
layout(location = 8) in vec4 border;
layout(location = 9) in vec4 gradient;

layout(location = 0) out vec2 v_pos;
layout(location = 1) flat out vec4 v_outer;
layout(location = 2) flat out vec4 v_outerRadii;
layout(location = 3) flat out vec4 v_inner;
layout(location = 4) flat out vec4 v_innerRadii;
layout(location = 5) flat out vec4 v_fill;
layout(location = 6) flat out vec4 v_fill2;
layout(location = 7) flat out vec4 v_border;
layout(location = 8) flat out vec4 v_gradient;

layout(std140, binding = 0) uniform Globals {
    mat4 transform; // device pixels of the target → clip space
    float opacity;
};

void main()
{
    v_pos = mix(rect.xy, rect.zw, corner);
    v_outer = outer;
    v_outerRadii = outerRadii;
    v_inner = inner;
    v_innerRadii = innerRadii;
    v_fill = fill;
    v_fill2 = fill2;
    v_border = border;
    v_gradient = gradient;
    gl_Position = transform * vec4(v_pos, 0.0, 1.0);
}
