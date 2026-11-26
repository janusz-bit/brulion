#include "base/color.h"

#include <algorithm>
#include <cmath>

namespace brulion::atrament {

namespace {

float hueToRgb(float p, float q, float t)
{
    if (t < 0)
        t += 1;
    if (t > 1)
        t -= 1;
    if (t < 1.0f / 6)
        return p + (q - p) * 6 * t;
    if (t < 1.0f / 2)
        return q;
    if (t < 2.0f / 3)
        return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

float linearToSrgb(float c)
{
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f;
}

} // namespace

Color Color::hsla(float hue, float saturation, float lightness, float alpha)
{
    const float h = std::fmod(std::fmod(hue, 360.0f) + 360.0f, 360.0f) / 360.0f;
    const float s = std::clamp(saturation, 0.0f, 1.0f);
    const float l = std::clamp(lightness, 0.0f, 1.0f);
    if (s == 0)
        return { l, l, l, alpha };
    const float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
    const float p = 2 * l - q;
    return { hueToRgb(p, q, h + 1.0f / 3), hueToRgb(p, q, h), hueToRgb(p, q, h - 1.0f / 3), alpha };
}

Color Color::oklch(float lightness, float chroma, float hue, float alpha)
{
    // Björn Ottosson's OKLab → linear sRGB.
    const float hr = hue * float(M_PI) / 180.0f;
    const float L = lightness;
    const float a = chroma * std::cos(hr);
    const float b = chroma * std::sin(hr);
    const float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
    const float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
    const float s_ = L - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;
    const float r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    const float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    const float bl = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
    return { linearToSrgb(r), linearToSrgb(g), linearToSrgb(bl), alpha };
}

} // namespace brulion::atrament
