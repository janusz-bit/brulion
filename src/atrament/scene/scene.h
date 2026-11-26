#pragma once

// The scene (docs/ATRAMENT.md D5): what a frame draws, as a flat list of primitives in painter's
// order, in device pixels. Built by a Window's paint phase, drawn by any renderer (raster/ for the
// CPU, gpu/ later). Primitives carry their own content mask (the clip rectangle in effect when
// they were painted), so a renderer never keeps a clip stack.
//
// Kinds, after GPUI's: quads (rounded rectangles with borders and a fill), shadows (blurred
// rounded rectangles), monochrome sprites (glyphs: atlas coverage × a colour), colour sprites
// (colour glyphs, icons: atlas RGBA), pictures (an RGBA image scaled into a rectangle), paths
// (filled polygons) and underlines (straight or wavy).

#include "base/color.h"
#include "base/geometry.h"
#include "scene/atlas.h"

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace brulion::atrament {

// The visible part; primitives are cut to it.
struct ContentMask {
    Rect bounds;
    bool operator==(const ContentMask &) const = default;
};

struct Background {
    enum class Kind : std::uint8_t { Solid, LinearGradient };
    Kind kind = Kind::Solid;
    Color color; // solid; the gradient's start
    Color color2; // the gradient's end
    // Direction of a gradient in degrees, CSS's convention: 0 = to the top, 90 = to the right.
    float angle = 180;

    static Background solid(Color c) { return { Kind::Solid, c, c, 180 }; }
    static Background linear(float angleDegrees, Color from, Color to) { return { Kind::LinearGradient, from, to, angleDegrees }; }
    bool isTransparent() const { return color.isTransparent() && (kind == Kind::Solid || color2.isTransparent()); }
};

struct Quad {
    Rect bounds;
    ContentMask mask;
    Background background;
    Color borderColor;
    Edges borderWidths;
    Corners cornerRadii;
};

// A blurred rounded rectangle — box-shadow's shape after offset and spread are applied to
// `bounds`. It spreads `blurRadius` (CSS's blur, σ = blur / 2) beyond them.
struct Shadow {
    Rect bounds;
    ContentMask mask;
    Corners cornerRadii;
    Color color;
    float blurRadius = 0;
};

// Coverage from an Alpha atlas tile, tinted. `bounds` is in whole device pixels, the tile's size.
struct MonochromeSprite {
    Rect bounds;
    ContentMask mask;
    Color color;
    AtlasTile tile;
};

// Premultiplied colour from a Color atlas tile, scaled into `bounds`.
struct ColorSprite {
    Rect bounds;
    ContentMask mask;
    AtlasTile tile;
    float opacity = 1;
    bool grayscale = false;
};

// A decoded picture, premultiplied RGBA8, shared by every frame that shows it.
struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<Pixel> pixels; // width × height
    // Unique for the process: a GPU renderer keys its texture cache with it.
    std::uint64_t id = 0;

    static std::shared_ptr<ImageData> make(int width, int height, std::vector<Pixel> pixels);
};

struct Picture {
    Rect bounds;
    ContentMask mask;
    Corners cornerRadii;
    std::shared_ptr<const ImageData> image;
    // The part of the image to show, in its pixels (empty: all of it).
    Rect source;
    float opacity = 1;
    bool grayscale = false;
};

// A filled polygon (non-zero), possibly of several contours. Points in device pixels.
struct PathPrimitive {
    ContentMask mask;
    Color color;
    Rect bounds; // of all points
    std::vector<Point> points;
    std::vector<std::uint32_t> contourEnds; // index one past each contour's last point
};

struct Underline {
    Rect bounds; // its extent: x span, y from the top of the line's ink to the bottom of the wave
    ContentMask mask;
    Color color;
    float thickness = 1;
    bool wavy = false;
};

using Primitive = std::variant<Quad, Shadow, MonochromeSprite, ColorSprite, Picture, PathPrimitive, Underline>;

class Scene {
public:
    void clear();

    void insert(Primitive p);
    const std::vector<Primitive> &primitives() const { return m_primitives; }
    std::size_t size() const { return m_primitives.size(); }
    bool isEmpty() const { return m_primitives.empty(); }

    // The union of every primitive's visible bounds.
    Rect bounds() const;

private:
    std::vector<Primitive> m_primitives;
};

// Builds a PathPrimitive: moveTo/lineTo/curves in device pixels, flattened to lines.
class PathBuilder {
public:
    void moveTo(Point p);
    void lineTo(Point p);
    void quadTo(Point control, Point p);
    void cubicTo(Point control1, Point control2, Point p);
    void close();
    // An arc of the circle around `center`, from angle a0 to a1 (radians, clockwise in y-down).
    void arcTo(Point center, float radius, float a0, float a1);

    PathPrimitive build(Color color, ContentMask mask) &&;

private:
    std::vector<Point> m_points;
    std::vector<std::uint32_t> m_ends;
    Point m_current;
    bool m_open = false;
};

// The bounds a primitive can touch, before its mask.
Rect primitiveBounds(const Primitive &p);

} // namespace brulion::atrament
