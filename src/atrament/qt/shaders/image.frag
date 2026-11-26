#version 440
// Premultiplied RGBA scaled into a rectangle, bilinear between the texels of its source only
// (clamped to the source's edge, never a neighbour's in the atlas), cut to rounded corners, ×
// opacity, optionally grey — the CPU renderer's drawImage (raster/raster.cpp).

layout(location = 0) in vec2 v_pos;
layout(location = 1) flat in vec4 v_shape; // centre, half size
layout(location = 2) flat in vec4 v_radii;
layout(location = 3) flat in vec4 v_map; // bounds' left, top; texels per pixel
layout(location = 4) flat in vec4 v_source; // source's left, top; opacity; grey
layout(location = 5) flat in vec4 v_limits; // texels [x0, x1) × [y0, y1)

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Globals {
    mat4 transform;
    float opacity;
};

layout(binding = 1) uniform sampler2D image;

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

void main()
{
    float c = clamp(0.5 - sdf(v_pos, v_shape, v_radii), 0.0, 1.0);
    if (!(c > 0.0))
        discard;
    // Texel i covers [i, i + 1): the two around u, clamped to the source.
    vec2 uv = v_source.xy + (v_pos - v_map.xy) * v_map.zw - 0.5;
    uv = clamp(uv, v_limits.xy, v_limits.zw - 1.0);
    ivec2 i0 = ivec2(uv);
    ivec2 i1 = min(i0 + 1, ivec2(v_limits.zw) - 1);
    vec2 f = uv - vec2(i0);
    vec4 top = mix(texelFetch(image, i0, 0), texelFetch(image, ivec2(i1.x, i0.y), 0), f.x);
    vec4 bottom = mix(texelFetch(image, ivec2(i0.x, i1.y), 0), texelFetch(image, i1, 0), f.x);
    vec4 p = mix(top, bottom, f.y) * (c * v_source.z);
    if (v_source.w > 0.0)
        p.rgb = vec3(dot(p.rgb, vec3(0.2126, 0.7152, 0.0722)));
    p = clamp(p, 0.0, 1.0);
    fragColor = vec4(min(p.rgb, vec3(p.a)), p.a) * opacity;
}
