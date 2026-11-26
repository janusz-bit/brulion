#version 440
// Evan Wallace's blurred rounded rectangle, as GPUI's fs_shadow and the CPU renderer
// (raster/raster.cpp): erf along x, the blur along y integrated with four samples over the part of
// [−3σ, 3σ] where the box is, each sample's row narrower where it crosses a corner.

layout(location = 0) in vec2 v_pos;
layout(location = 1) flat in vec4 v_box; // centre, half size
layout(location = 2) flat in vec4 v_radii;
layout(location = 3) flat in vec4 v_color;
layout(location = 4) flat in vec4 v_blur; // σ, √½ / σ, 1 / (√(2π) σ), 3σ

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Globals {
    mat4 transform;
    float opacity;
};

// Abramowitz & Stegun 7.1.27.
float erfApprox(float v)
{
    float a = abs(v);
    float r1 = 1.0 + (0.278393 + (0.230389 + (0.000972 + 0.078108 * a) * a) * a) * a;
    float r2 = r1 * r1;
    return sign(v) * (1.0 - 1.0 / (r2 * r2));
}

void main()
{
    float px = v_pos.x - v_box.x;
    float py = v_pos.y - v_box.y;
    float hw = v_box.z;
    float hh = v_box.w;
    float sigma = v_blur.x;
    float k = v_blur.y;
    float reach = v_blur.w;
    float start = clamp(-reach, py - hh, py + hh);
    float end = clamp(reach, py - hh, py + hh);
    float step = (end - start) / 4.0;
    if (!(step > 0.0))
        discard;
    bool left = px < 0.0;
    float corner = py < 0.0 ? (left ? v_radii.x : v_radii.y) : (left ? v_radii.w : v_radii.z);
    float straightWeight = 0.0;
    float curved = 0.0;
    for (int i = 0; i < 4; ++i) {
        float yi = start + step * (float(i) + 0.5);
        float u = yi / sigma;
        float weight = v_blur.z * exp(-0.5 * u * u) * step;
        float delta = min(hh - corner - abs(py - yi), 0.0);
        if (delta < 0.0) {
            float width = hw - corner + sqrt(max(0.0, corner * corner - delta * delta));
            curved += weight * 0.5 * (erfApprox((px + width) * k) - erfApprox((px - width) * k));
        } else {
            straightWeight += weight;
        }
    }
    float alpha = straightWeight * (0.5 * (erfApprox((px + hw) * k) - erfApprox((px - hw) * k))) + curved;
    vec4 c = v_color * clamp(alpha, 0.0, 1.0);
    fragColor = vec4(min(c.rgb, vec3(c.a)), c.a) * opacity;
}
