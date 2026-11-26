#pragma once

// An anti-aliased outline rasterizer (signed-area accumulation, after font-rs): glyph outlines
// and filled paths become 8-bit coverage. Lines add the area they cover to a buffer of floats,
// one row of `width + 2` cells per pixel row; a prefix sum along each row gives the coverage.
// The non-zero winding rule, approximated as min(|winding area|, 1) — exact for outlines whose
// contours do not overlap with opposite directions, which holds for fonts and Atrament's paths.
//
// Coordinates are in pixels of the target, y down; anything outside [0, width] × [0, height]
// is clipped (left of 0 it still counts for the pixels to its right).

#include "base/geometry.h"

#include <cstdint>
#include <vector>

namespace brulion::atrament {

class CoverageRasterizer {
public:
    void reset(int width, int height);
    int width() const { return m_width; }
    int height() const { return m_height; }

    void moveTo(Point p);
    void lineTo(Point p);
    void quadTo(Point control, Point p);
    void cubicTo(Point control1, Point control2, Point p);
    // Closes the current contour with a line back to its start (moveTo and the end close too).
    void close();

    // Coverage 0–255 into `out` (`stride` bytes per row, at least width).
    void accumulate(std::uint8_t *out, int stride) const;
    // The same, as floats in [0, 1] (width × height, row by row).
    void accumulate(std::vector<float> &out) const;

    // Curve flattening tolerance in pixels (default 0.05: a 1 % smaller area of round shapes at 0.2).
    void setTolerance(float tolerance) { m_tolerance = tolerance; }

private:
    void addLine(Point p0, Point p1);

    int m_width = 0;
    int m_height = 0;
    int m_stride = 0;
    std::vector<float> m_cells;
    Point m_start;
    Point m_current;
    bool m_open = false;
    float m_tolerance = 0.05f;
};

} // namespace brulion::atrament
