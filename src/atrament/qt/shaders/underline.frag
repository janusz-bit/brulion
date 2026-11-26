#version 440
// A sine around the bounds' middle line whose stroke just fills their height, the distance to it
// |Δy| / √(1 + slope²) anti-aliased over a pixel, ends cut at the bounds' x span — the CPU
// renderer's wavy underline (raster/raster.cpp), the wave evaluated at the column's centre.

layout(location = 0) in vec2 v_pos;
layout(location = 1) flat in vec4 v_wave; // left, right, middle, amplitude
layout(location = 2) flat in vec4 v_wave2; // ω, half stroke + ½
layout(location = 3) flat in vec4 v_color;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Globals {
    mat4 transform;
    float opacity;
};

void main()
{
    float x = floor(v_pos.x);
    float omega = v_wave2.x;
    float amplitude = v_wave.w;
    float phase = omega * (x + 0.5 - v_wave.x);
    float slope = amplitude * omega * cos(phase);
    float centre = v_wave.z + amplitude * sin(phase);
    float scale = 1.0 / sqrt(1.0 + slope * slope);
    float horizontal = clamp(min(x + 1.0, v_wave.y) - max(x, v_wave.x), 0.0, 1.0);
    float alpha = clamp(v_wave2.y - abs(v_pos.y - centre) * scale, 0.0, 1.0) * horizontal;
    fragColor = v_color * (alpha * opacity);
}
