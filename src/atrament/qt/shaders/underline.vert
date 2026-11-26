#version 440
// Atrament's wavy underlines (qt/gpuframe.h GpuUnderlineInstance).

layout(location = 0) in vec2 corner;
layout(location = 1) in vec4 rect;
layout(location = 2) in vec4 wave;
layout(location = 3) in vec4 wave2;
layout(location = 4) in vec4 color;

layout(location = 0) out vec2 v_pos;
layout(location = 1) flat out vec4 v_wave;
layout(location = 2) flat out vec4 v_wave2;
layout(location = 3) flat out vec4 v_color;

layout(std140, binding = 0) uniform Globals {
    mat4 transform;
    float opacity;
};

void main()
{
    v_pos = mix(rect.xy, rect.zw, corner);
    v_wave = wave;
    v_wave2 = wave2;
    v_color = color;
    gl_Position = transform * vec4(v_pos, 0.0, 1.0);
}
