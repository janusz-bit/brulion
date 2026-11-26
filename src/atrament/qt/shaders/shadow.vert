#version 440
// Atrament's shadows (qt/gpuframe.h GpuShadowInstance).

layout(location = 0) in vec2 corner;
layout(location = 1) in vec4 rect;
layout(location = 2) in vec4 box;
layout(location = 3) in vec4 radii;
layout(location = 4) in vec4 color;
layout(location = 5) in vec4 blur;

layout(location = 0) out vec2 v_pos;
layout(location = 1) flat out vec4 v_box;
layout(location = 2) flat out vec4 v_radii;
layout(location = 3) flat out vec4 v_color;
layout(location = 4) flat out vec4 v_blur;

layout(std140, binding = 0) uniform Globals {
    mat4 transform;
    float opacity;
};

void main()
{
    v_pos = mix(rect.xy, rect.zw, corner);
    v_box = box;
    v_radii = radii;
    v_color = color;
    v_blur = blur;
    gl_Position = transform * vec4(v_pos, 0.0, 1.0);
}
