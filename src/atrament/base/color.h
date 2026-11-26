#pragma once

// Colours of Atrament: sRGB components in [0, 1], straight (not premultiplied) alpha — what a
// theme writes down. Renderers blend premultiplied colours in sRGB space, as browsers and Qt do,
// so a translucent layer looks the same as it does in today's QML.

#include <cstdint>

namespace brulion::atrament {

struct Color {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 0;

    constexpr bool isTransparent() const { return a <= 0; }
    constexpr bool isOpaque() const { return a >= 1; }
    constexpr Color withAlpha(float alpha) const { return { r, g, b, alpha }; }
    constexpr Color fadedBy(float opacity) const { return { r, g, b, a * opacity }; }
    constexpr bool operator==(const Color &) const = default;

    // Linear interpolation of straight components (t in [0, 1]).
    static constexpr Color mix(Color x, Color y, float t)
    {
        return { x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t };
    }
    // Hue in degrees, saturation and lightness in [0, 1].
    static Color hsla(float hue, float saturation, float lightness, float alpha = 1);
    // OKLCH (lightness [0, 1], chroma ~[0, 0.4], hue in degrees) mapped into sRGB, clipped.
    static Color oklch(float lightness, float chroma, float hue, float alpha = 1);
};

constexpr Color rgb(std::uint32_t hex)
{
    return { float((hex >> 16) & 0xff) / 255.0f, float((hex >> 8) & 0xff) / 255.0f, float(hex & 0xff) / 255.0f, 1 };
}
constexpr Color rgba(std::uint32_t hex)
{
    return { float((hex >> 24) & 0xff) / 255.0f, float((hex >> 16) & 0xff) / 255.0f, float((hex >> 8) & 0xff) / 255.0f,
        float(hex & 0xff) / 255.0f };
}
constexpr Color transparent() { return { 0, 0, 0, 0 }; }
constexpr Color black() { return { 0, 0, 0, 1 }; }
constexpr Color white() { return { 1, 1, 1, 1 }; }

// A premultiplied 8-bit pixel, the renderers' storage order R, G, B, A in memory (as
// QImage::Format_RGBA8888_Premultiplied).
struct Pixel {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
    constexpr bool operator==(const Pixel &) const = default;
};

// Premultiplied components in [0, 1].
struct PremulColor {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 0;

    static constexpr PremulColor from(Color c) { return { c.r * c.a, c.g * c.a, c.b * c.a, c.a }; }
    constexpr PremulColor operator*(float s) const { return { r * s, g * s, b * s, a * s }; }
    constexpr PremulColor operator+(PremulColor o) const { return { r + o.r, g + o.g, b + o.b, a + o.a }; }
};

} // namespace brulion::atrament
