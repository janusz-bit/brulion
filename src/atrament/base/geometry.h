#pragma once

// Geometry of Atrament (docs/ATRAMENT.md): points, sizes, rectangles, edges and corners in
// floating-point pixels. Elements work in logical pixels; the scene holds device pixels (logical
// × the window's scale factor). Plain aggregates, constexpr where it costs nothing.

#include <algorithm>
#include <cmath>

namespace brulion::atrament {

struct Point {
    float x = 0;
    float y = 0;

    constexpr Point operator+(Point o) const { return { x + o.x, y + o.y }; }
    constexpr Point operator-(Point o) const { return { x - o.x, y - o.y }; }
    constexpr Point operator*(float s) const { return { x * s, y * s }; }
    constexpr Point operator/(float s) const { return { x / s, y / s }; }
    constexpr Point operator-() const { return { -x, -y }; }
    constexpr Point &operator+=(Point o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }
    constexpr Point &operator-=(Point o)
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }
    constexpr bool operator==(const Point &) const = default;
};

struct Size {
    float width = 0;
    float height = 0;

    constexpr Size operator*(float s) const { return { width * s, height * s }; }
    constexpr Size operator/(float s) const { return { width / s, height / s }; }
    constexpr Size operator+(Size o) const { return { width + o.width, height + o.height }; }
    constexpr Size operator-(Size o) const { return { width - o.width, height - o.height }; }
    constexpr bool isEmpty() const { return !(width > 0) || !(height > 0); }
    constexpr bool operator==(const Size &) const = default;
};

struct Edges {
    float top = 0;
    float right = 0;
    float bottom = 0;
    float left = 0;

    static constexpr Edges all(float v) { return { v, v, v, v }; }
    static constexpr Edges symmetric(float vertical, float horizontal)
    {
        return { vertical, horizontal, vertical, horizontal };
    }
    constexpr float horizontal() const { return left + right; }
    constexpr float vertical() const { return top + bottom; }
    constexpr Edges operator*(float s) const { return { top * s, right * s, bottom * s, left * s }; }
    constexpr bool isZero() const { return top == 0 && right == 0 && bottom == 0 && left == 0; }
    constexpr bool operator==(const Edges &) const = default;
};

struct Corners {
    float topLeft = 0;
    float topRight = 0;
    float bottomRight = 0;
    float bottomLeft = 0;

    static constexpr Corners all(float v) { return { v, v, v, v }; }
    constexpr Corners operator*(float s) const
    {
        return { topLeft * s, topRight * s, bottomRight * s, bottomLeft * s };
    }
    constexpr bool isZero() const { return topLeft == 0 && topRight == 0 && bottomRight == 0 && bottomLeft == 0; }
    constexpr float max() const { return std::max(std::max(topLeft, topRight), std::max(bottomRight, bottomLeft)); }
    // Radii no larger than half the shorter side (as CSS scales overlapping radii down).
    Corners clampedTo(Size size) const
    {
        const float limit = std::max(0.0f, std::min(size.width, size.height) / 2);
        return { std::clamp(topLeft, 0.0f, limit), std::clamp(topRight, 0.0f, limit),
            std::clamp(bottomRight, 0.0f, limit), std::clamp(bottomLeft, 0.0f, limit) };
    }
    constexpr bool operator==(const Corners &) const = default;
};

struct Rect {
    Point origin;
    Size size;

    static constexpr Rect fromLTRB(float l, float t, float r, float b) { return { { l, t }, { r - l, b - t } }; }
    static constexpr Rect fromXYWH(float x, float y, float w, float h) { return { { x, y }, { w, h } }; }

    constexpr float left() const { return origin.x; }
    constexpr float top() const { return origin.y; }
    constexpr float right() const { return origin.x + size.width; }
    constexpr float bottom() const { return origin.y + size.height; }
    constexpr float width() const { return size.width; }
    constexpr float height() const { return size.height; }
    constexpr Point center() const { return { origin.x + size.width / 2, origin.y + size.height / 2 }; }
    constexpr Point topRight() const { return { right(), top() }; }
    constexpr Point bottomLeft() const { return { left(), bottom() }; }
    constexpr Point bottomRight() const { return { right(), bottom() }; }
    constexpr bool isEmpty() const { return size.isEmpty(); }

    // Half-open: the right and bottom edges are outside.
    constexpr bool contains(Point p) const
    {
        return p.x >= left() && p.x < right() && p.y >= top() && p.y < bottom();
    }
    constexpr bool intersects(const Rect &o) const
    {
        return left() < o.right() && o.left() < right() && top() < o.bottom() && o.top() < bottom();
    }
    // The common part; an empty rectangle (size 0) at the nearest corner when they do not meet.
    constexpr Rect intersected(const Rect &o) const
    {
        const float l = std::max(left(), o.left());
        const float t = std::max(top(), o.top());
        const float r = std::max(l, std::min(right(), o.right()));
        const float b = std::max(t, std::min(bottom(), o.bottom()));
        return fromLTRB(l, t, r, b);
    }
    constexpr Rect united(const Rect &o) const
    {
        if (isEmpty())
            return o;
        if (o.isEmpty())
            return *this;
        return fromLTRB(std::min(left(), o.left()), std::min(top(), o.top()), std::max(right(), o.right()),
                std::max(bottom(), o.bottom()));
    }
    constexpr Rect translated(Point d) const { return { origin + d, size }; }
    constexpr Rect inset(const Edges &e) const
    {
        return { { origin.x + e.left, origin.y + e.top },
            { std::max(0.0f, size.width - e.horizontal()), std::max(0.0f, size.height - e.vertical()) } };
    }
    constexpr Rect outset(const Edges &e) const
    {
        return { { origin.x - e.left, origin.y - e.top }, { size.width + e.horizontal(), size.height + e.vertical() } };
    }
    constexpr Rect dilated(float d) const { return outset(Edges::all(d)); }
    constexpr Rect scaled(float s) const { return { origin * s, size * s }; }
    // The smallest rectangle of whole pixels that covers this one.
    Rect roundedOut() const
    {
        return fromLTRB(std::floor(left()), std::floor(top()), std::ceil(right()), std::ceil(bottom()));
    }
    // Edges moved to the nearest whole pixel (sharp borders at any scale).
    Rect snapped() const
    {
        return fromLTRB(std::round(left()), std::round(top()), std::round(right()), std::round(bottom()));
    }
    constexpr bool operator==(const Rect &) const = default;
};

// A range of UTF-16 code units, [start, end).
struct Range {
    int start = 0;
    int end = 0;

    constexpr int length() const { return end - start; }
    constexpr bool isEmpty() const { return end <= start; }
    constexpr bool contains(int i) const { return i >= start && i < end; }
    constexpr bool operator==(const Range &) const = default;
};

} // namespace brulion::atrament
