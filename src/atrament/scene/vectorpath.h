#pragma once

// Vector paths (docs/ATRAMENT.md "Vector paths and icons"): lines and curves in a path's own
// coordinates — a diagram's shapes and arrows, an icon's outlines — flattened into device pixels
// under a transform, then filled, or stroked: the outline of a line of a width with its caps, joins
// and dashes, as polygons of one winding. Both become the scene's PathPrimitive, which every
// renderer already fills (the CPU raster, the QRhi renderer's path atlas): a stroke needs no
// primitive of its own.
//
//   VectorPath p;
//   p.moveTo({0, 0}); p.cubicTo({10, 0}, {20, 10}, {20, 20});
//   window.strokePath(p, {.scale = 2, .offset = {8, 8}}, StrokeStyle{1.5f, LineCap::Round, LineJoin::Round}, ink);
//
// A stroke is the union of a rectangle per segment, a join per corner (a round disc, a miter or a
// bevel wedge on the outer side) and a cap per open end, all of one orientation: filled with the
// non-zero rule (min(|winding|, 1), base/coverage.h) the overlaps add nothing. Dashes cut the
// flattened line into pieces first (on, off… lengths along it, from `dashOffset`), each stroked
// with the caps.

#include "base/geometry.h"
#include "scene/scene.h"

#include <cstdint>
#include <span>
#include <vector>

namespace brulion::atrament {

enum class LineCap : std::uint8_t { Butt, Round, Square };
enum class LineJoin : std::uint8_t { Miter, Round, Bevel };

struct StrokeStyle {
    float width = 1;
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    float miterLimit = 4; // a miter longer than this × half the width becomes a bevel (SVG's)
    std::vector<float> dashes; // on, off, on, off… lengths along the line; empty (or all 0): solid
    float dashOffset = 0;      // how far into the pattern the line starts
};

// p → p × scale + offset: a uniform scale and a translation (what diagrams and icons need).
struct PathTransform {
    float scale = 1;
    Point offset;
    constexpr Point apply(Point p) const { return {p.x * scale + offset.x, p.y * scale + offset.y}; }
};

// A contour flattened to lines, and whether it was closed.
struct FlatContour {
    std::vector<Point> points;
    bool closed = false;
};

class VectorPath {
public:
    void moveTo(Point p);
    void lineTo(Point p);
    void quadTo(Point control, Point p);
    void cubicTo(Point control1, Point control2, Point p);
    // SVG's elliptical arc (`A`): from the current point to `p` on an ellipse of radii rx, ry
    // rotated by `rotationDegrees`, the large or the small one of the two arcs, clockwise
    // (`sweep`, y down) or not. Radii too small for the two points grow as SVG says; zero radii
    // make a line.
    void arcTo(float rx, float ry, float rotationDegrees, bool largeArc, bool sweep, Point p);
    void close();

    // Closed shapes of their own: a rectangle with rounded corners (radii clamped to half its
    // sides), an ellipse.
    void addRect(Rect r, float rx = 0, float ry = 0);
    void addEllipse(Point center, float rx, float ry);

    bool isEmpty() const { return m_ops.empty(); }
    // The last point (the start of the next segment); the origin of an empty path.
    Point currentPoint() const { return m_current; }
    // The points and control points (a curve's bounds are inside).
    Rect bounds() const;

    // The contours under `t`, curves cut into lines no farther than `tolerance` from them (in the
    // transformed units: device px).
    std::vector<FlatContour> flatten(const PathTransform &t, float tolerance = 0.2f) const;

private:
    struct Op {
        enum class Kind : std::uint8_t { Move, Line, Quad, Cubic, Close };
        Kind kind = Kind::Move;
        Point p;
        Point c1;
        Point c2;
    };
    void ensureStarted();
    std::vector<Op> m_ops; // arcs as cubics
    Point m_current;
    Point m_start;
    bool m_open = false;
};

// Fills: every contour closed into `out` (non-zero).
void fillContours(std::span<const FlatContour> contours, PathBuilder &out);
// The outline of the stroke of `contours` (lengths in the contours' units) into `out`: contours of
// one winding, filled non-zero (see above). `tolerance`: how round discs and caps are.
void strokeContours(std::span<const FlatContour> contours, const StrokeStyle &style, PathBuilder &out,
                    float tolerance = 0.2f);
// The pieces of `contours` the dash pattern keeps (open contours); solid: the contours themselves.
std::vector<FlatContour> dashContours(std::span<const FlatContour> contours, std::span<const float> dashes,
                                      float offset);

} // namespace brulion::atrament
