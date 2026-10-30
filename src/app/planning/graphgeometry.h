// Pure geometry of the graph's scene-graph renderer (GraphScene) and the label placement shared
// with the QPainter path (GraphItem::render): no scene graph here, so all of it is unit-tested.
//
// * Meshes are triangle lists of coloured vertices (QSGGeometry::ColoredPoint2D, premultiplied
//   colours, QSGVertexColorMaterial) with 16-bit indices — they work on every RHI backend (GLES2
//   included) — split into chunks of at most 65 535 vertices (MeshChunks).
// * Antialiasing without shaders: every shape gets a fringe of about one device pixel whose outer
//   vertices are transparent (`fringe` in the shape's units: 1 / (device pixel ratio · zoom) for
//   world-space shapes). Shapes thinner than the fringe fade instead of shrinking (their coverage
//   stays right).
// * Non-finite coordinates (which should never happen) add nothing: no NaN vertex reaches the GPU.
#pragma once

#include <QColor>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QtGlobal>

#include <span>
#include <vector>

namespace brulion::planning::geometry {

/// Layout of QSGGeometry::ColoredPoint2D (premultiplied colour).
struct Vertex {
    float x, y;
    quint8 r, g, b, a;
};
static_assert(sizeof(Vertex) == 12);

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<quint16> indices;
};

/// Meshes of at most MaxVertices vertices each, in drawing order; clear() keeps the allocations
/// (a moving graph rebuilds its meshes on every frame).
class MeshChunks {
public:
    static constexpr int MaxVertices = 65535;

    /// The chunk to append `vertices` more vertices to (a new one when the current would overflow).
    Mesh &room(int vertices);
    std::span<const Mesh> chunks() const { return {m_chunks.data(), m_used}; }
    int vertexCount() const;
    int indexCount() const;
    bool isEmpty() const { return m_used == 0; }
    void clear();

private:
    std::vector<Mesh> m_chunks;
    size_t m_used = 0;
};

QRgb premultiplied(const QColor &c, double alphaFactor = 1.0);
Vertex vertex(QPointF p, QRgb premultipliedColour);
/// Segments of a disc of `screenRadius` px: clamp(ceil(2π·r / 3 px), 8, 64).
int discSegments(double screenRadius);
/// Filled disc with a fringe (centre, an inner ring and a transparent outer ring:
/// 1 + 2·segments vertices).
void appendDisc(MeshChunks &out, QPointF centre, double radius, QRgb colour, double fringe, int segments);
/// A circle line of `width` centred on `radius`, with a fringe on both sides.
void appendRing(MeshChunks &out, QPointF centre, double radius, double width, QRgb colour, double fringe, int segments);
/// A line segment as a quad with fringe quads along both long sides (no caps; 8 vertices, 6
/// triangles); zero length adds nothing.
void appendSegment(MeshChunks &out, QPointF a, QPointF b, double width, QRgb colour, double fringe);
/// A thin line as one quad of width + fringe whose colour carries the coverage (× width / (width +
/// fringe)): 4 vertices and 2 triangles instead of 8 and 6 — for the thousands of edges of a graph
/// (a CPU rasteriser such as llvmpipe, or a weak phone GPU, draws far fewer triangles); softer
/// sides, the same ink.
void appendLine(MeshChunks &out, QPointF a, QPointF b, double width, QRgb colour, double fringe);
void appendRoundedRect(MeshChunks &out, const QRectF &rect, double radius, QRgb colour, double fringe);

/// A node that wants a label (screen coordinates), in priority order.
struct LabelCandidate {
    int node = -1;
    QPointF centre;
    double radius = 0;   ///< the disc's radius on screen
    QSizeF text;         ///< the label text's size
    bool always = false; ///< the highlighted node: placed below it even over other labels
};

/// GraphItem's label rule: a box of text + (6, 2) px below the node, else above, right, left —
/// the first that overlaps no label placed before (on a 64-px grid); `always` takes the first.
/// At most `maxLabels`. One rect per candidate, in candidate order (a null rect: not placed).
QList<QRectF> placeLabels(const std::vector<LabelCandidate> &ordered, int maxLabels);

} // namespace brulion::planning::geometry
