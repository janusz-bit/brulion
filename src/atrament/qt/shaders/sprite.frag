#version 440
// Coverage from an A8 atlas page (an R8 texture) × a colour, texel for pixel (no filtering:
// glyphs are rasterized at device size).

layout(location = 0) in vec2 v_pos;
layout(location = 1) flat in vec2 v_shift;
layout(location = 2) flat in vec4 v_color;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Globals {
    mat4 transform;
    float opacity;
};

layout(binding = 1) uniform sampler2D page;

void main()
{
    float c = texelFetch(page, ivec2(floor(v_pos)) - ivec2(v_shift), 0).r;
    fragColor = v_color * (c * opacity);
}
