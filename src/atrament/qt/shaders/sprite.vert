#version 440
// Atrament's monochrome sprites — glyphs and paths (qt/gpuframe.h GpuSpriteInstance).

layout(location = 0) in vec2 corner;
layout(location = 1) in vec4 rect;
layout(location = 2) in vec4 shift;
layout(location = 3) in vec4 color;

layout(location = 0) out vec2 v_pos;
layout(location = 1) flat out vec2 v_shift;
layout(location = 2) flat out vec4 v_color;

layout(std140, binding = 0) uniform Globals {
    mat4 transform;
    float opacity;
};

void main()
{
    v_pos = mix(rect.xy, rect.zw, corner);
    v_shift = shift.xy;
    v_color = color;
    gl_Position = transform * vec4(v_pos, 0.0, 1.0);
}
