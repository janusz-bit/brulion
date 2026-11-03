// Flowcharts (`graph` / `flowchart`): the model, the parser, the layout and the scene. Internal,
// but the tests read the model and the layout directly (tests/core/diagram).
#pragma once

#include "diagram/common_p.h"
#include "diagram/diagram.h"

namespace brulion::diagram::detail {

enum class Direction : quint8 { TB, BT, LR, RL };

enum class NodeShape : quint8 {
    Rect,             ///< A[text]
    Rounded,          ///< A(text)
    Stadium,          ///< A([text])
    Subroutine,       ///< A[[text]]
    Cylinder,         ///< A[(text)]
    Circle,           ///< A((text))
    DoubleCircle,     ///< A(((text)))
    Asymmetric,       ///< A>text]
    Rhombus,          ///< A{text}
    Hexagon,          ///< A{{text}}
    Parallelogram,    ///< A[/text/]
    ParallelogramAlt, ///< A[\text\]
    Trapezoid,        ///< A[/text\]
    TrapezoidAlt,     ///< A[\text/]
};

struct FlowNode {
    QString id;
    QString label; ///< '\n' between lines
    NodeShape shape = NodeShape::Rect;
    bool defined = false; ///< a shape or label was given (not just named in a link)
    QStringList classes;
    Style style; ///< the final style (classDef default, classes, `style`)
    int cluster = -1;
    int line = 0;
};

enum class LinkStroke : quint8 { Solid, Dotted, Thick, Invisible };

/// An end of a link: a node or a subgraph (`A --> sub1`).
struct FlowEnd {
    int index = -1;
    bool cluster = false;
    friend bool operator==(const FlowEnd &, const FlowEnd &) = default;
};

struct FlowEdge {
    FlowEnd from, to;
    QString label;
    LinkStroke stroke = LinkStroke::Solid;
    Head startHead = Head::None;
    Head endHead = Head::Arrow;
    int length = 1; ///< `-->` 1, `--->` 2 …: the ranks the link spans at least
    Style style;
    int line = 0;
};

struct FlowCluster {
    QString id;
    QString title;
    int parent = -1;
    std::optional<Direction> direction;
    QStringList classes;
    Style style;
    int line = 0;
};

struct Flowchart {
    Direction direction = Direction::TB;
    QList<FlowNode> nodes;
    QList<FlowEdge> edges;
    QList<FlowCluster> clusters;
    QString title;
};

Outcome<Flowchart> parseFlowchart(QStringView source);

/// Where everything of a flowchart is drawn (scene coordinates).
struct FlowLayout {
    QSizeF size;
    QList<QRectF> nodes; ///< per node: its shape's bounds
    QList<TextBlock> nodeText;
    QList<QRectF> clusters; ///< per cluster: its frame
    QList<TextBlock> clusterText;
    struct Edge {
        bool drawn = false;         ///< false: invisible or impossible (a subgraph to its own member)
        QList<PathOp> path;         ///< the curve (Move, Cubic…)
        QPointF start, end;         ///< the ends on the shapes' outlines (the heads' tips)
        QPointF lineStart, lineEnd; ///< where the curve itself starts and ends (under the heads)
        QPointF startDir, endDir;   ///< the curve's direction arriving at `start` / `end` (unit)
        qreal width = 1.5;          ///< the line's width
        QRectF label;               ///< the label's box (empty: no label)
        TextBlock labelText;
    };
    QList<Edge> edges;
};

Outcome<FlowLayout> layoutFlowchart(const Flowchart &chart, const Metrics &metrics);
Scene drawFlowchart(const Flowchart &chart, const FlowLayout &layout, const Metrics &metrics);

/// The size of a node of `shape` around text of `text` (padding included).
QSizeF shapeSize(NodeShape shape, const QSizeF &text, qreal fontSize);
/// The path drawing `shape` in `r` (outline; `decorations`: the inner lines of a subroutine, the
/// top of a cylinder, the inner circle of a double circle).
QList<PathOp> shapePath(NodeShape shape, const QRectF &r, QList<PathOp> *decorations = nullptr);
/// The outline of `shape` in `r` as a polygon (curves flattened) — where links end.
QList<QPointF> shapeOutline(NodeShape shape, const QRectF &r);
/// The point where the line from `inside` towards `outside` leaves `outline` (`inside` if none).
QPointF outlineExit(const QList<QPointF> &outline, const QPointF &inside, const QPointF &outside);

} // namespace brulion::diagram::detail
