// The outline of a freehand stroke — a port of perfect-freehand 1.2 (Steve Ruiz, MIT licence,
// https://github.com/steveruiz/perfect-freehand), with the options Excalidraw uses for its
// `freedraw` elements, so a stroke looks alike in Brulion, in the SVG it writes and in
// excalidraw.com: size = strokeWidth × 4.25, thinning 0.6, smoothing 0.5, streamline 0.5, the
// easing sin(t·π/2), pressure simulated from the speed when the device reports none.
#pragma once

#include "drawing/drawing.h"

#include <QList>
#include <QPointF>
#include <QString>

namespace brulion::drawing {

struct OutlineOptions {
    /// The stroke's base size (diameter-like; Excalidraw: strokeWidth × 4.25).
    double size = 8.5;
    double thinning = 0.6;
    double smoothing = 0.5;
    double streamline = 0.5;
    bool simulatePressure = true;
    /// The stroke is finished: its last point is taken exactly (not smoothed towards).
    bool last = true;
};

/// The options Excalidraw draws `stroke` with.
OutlineOptions outlineOptions(const Stroke &stroke, bool finished = true);

/// The outline polygon of a stroke (empty for no points); one point gives a dot. Points with
/// coordinates that are not finite are left out.
QList<QPointF> strokeOutline(const QList<StrokePoint> &points, const OutlineOptions &options);
inline QList<QPointF> strokeOutline(const Stroke &stroke)
{
    return strokeOutline(stroke.points, outlineOptions(stroke));
}

/// An SVG path of the outline as Excalidraw writes it (perfect-freehand's
/// getSvgPathFromStroke: quadratic curves through the midpoints, closed), coordinates with at
/// most two decimals, moved by (dx, dy). "" for no points.
QString outlineToSvgPath(const QList<QPointF> &outline, double dx = 0, double dy = 0);

/// A number as SVG and the scene write it: at most `decimals` decimals, no trailing zeros, no
/// exponent, "-0" as "0".
QString formatNumber(double value, int decimals = 2);

} // namespace brulion::drawing
