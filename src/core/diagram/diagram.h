// Mermaid diagrams (docs/SYNTAX.md §11 "Mermaid diagrams"): a ```mermaid block's source drawn by Brulion's
// own parser and layout — no script engine, nothing from a note is ever executed, links and
// callbacks (`click`, `href`) are dropped. Notes come from sync and imports: every input is
// bounded (Limits below) and malformed input gives an Error with its line, never a crash.
//
// Types drawn: flowcharts (`graph`, `flowchart`), sequence diagrams (`sequenceDiagram`) and pie
// charts (`pie`). The other Mermaid types are recognised (Kind::Unsupported, their `typeName`):
// the app shows their source with a note.
//
// render() parses and lays out the source into a Scene: primitives in painting order — paths
// (move/line/cubic/close, filled and/or stroked, solid, dotted or thick; arrowheads are filled
// paths of their own) and text (lines in a box, aligned, sized, bold, with an optional background).
// Each colour is a Role (the theme's: Palette, palette.cpp) or an explicit colour from the
// diagram's `style`/`classDef` (validated "#rrggbb[aa]"). The app paints the scene with QPainter
// in its theme's colours (src/app/editor/mermaidview.h); the export writes it as SVG (svg.h).
//
// Text sizes come from the caller (Metrics::measure — QFontMetricsF in the app); approximateMetrics()
// is a deterministic estimate (no fonts) for the export and the tests.
//
// QtCore only: colours are strings, geometry QPointF/QRectF/QSizeF.
#pragma once

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringView>

#include <functional>
#include <optional>

namespace brulion::diagram {

/// Bounds on what a diagram may hold (sources come from anywhere): beyond them render() returns
/// Error::TooLarge at once.
namespace limits {
inline constexpr qsizetype MaxSourceBytes = 100 * 1024; ///< UTF-16 code units of the source
inline constexpr int MaxNodes = 1000;                   ///< flowchart nodes
inline constexpr int MaxEdges = 3000;                   ///< flowchart links (each pair of a chain)
inline constexpr int MaxClusters = 200;                 ///< flowchart subgraphs
inline constexpr int MaxNesting = 16;                   ///< subgraphs / sequence blocks inside each other
inline constexpr int MaxParticipants = 300;             ///< sequence participants
inline constexpr int MaxMessages = 500;                 ///< sequence messages and notes
inline constexpr int MaxStatements = 2000;              ///< sequence events of any kind (blocks, activations…)
inline constexpr int MaxSlices = 64;                    ///< pie slices
inline constexpr int MaxLayoutNodes = 30000;            ///< flowchart nodes plus the layout's bends
inline constexpr qreal MaxSceneExtent = 50000;          ///< the scene's width and height (px at fontSize)
inline constexpr qsizetype MaxLabelLength = 1000;       ///< characters of one label (longer: cut, "…")
inline constexpr int MaxLabelLines = 40;                ///< lines of one label after wrapping
} // namespace limits

enum class Kind {
    Flowchart,
    Sequence,
    Pie,
    Unsupported, ///< a Mermaid type Brulion does not draw yet (typeName says which)
    Unknown,     ///< no Mermaid header: empty, or not a diagram type
};

/// The diagram type of `source` from its first statement (front matter, `%%` comments and
/// `%%{init}%%` directives skipped). `typeName`: the keyword ("flowchart", "classDiagram", …).
Kind detectKind(QStringView source, QString *typeName = nullptr);

/// How text is measured. `measure` gives the size of one line of text at `pixelSize`; `fontSize`
/// is the diagram's base size (labels; edge labels and notes use it too, titles 1.25×).
struct Metrics {
    std::function<QSizeF(QStringView line, qreal pixelSize, bool bold)> measure;
    qreal fontSize = 14;
};
/// A deterministic estimate of a sans-serif font (no font files): 0.56 em a character (wide
/// characters 1 em, bold 6 % wider), lines 1.25 em. The export's SVG and the tests use it.
Metrics approximateMetrics(qreal fontSize = 14);

/// Where a colour of the scene comes from.
enum class Role : quint8 {
    None, ///< not painted (no fill / no stroke)
    Background,
    Title,
    NodeFill,
    NodeStroke,
    NodeText,
    Line,
    LineText,
    LabelBackground,
    ClusterFill,
    ClusterStroke,
    ClusterText,
    NoteFill,
    NoteStroke,
    NoteText,
    ActorFill,
    ActorStroke,
    ActorText,
    Lifeline,
    ActivationFill,
    ActivationStroke,
    FrameStroke,
    FrameText,
    FrameLabelFill,
    NumberFill,
    NumberText,
    SliceStroke,
    SliceText,
    LegendText,
    Slice0, ///< pie slices: Slice0 + (n % SliceCount)
    LastSlice = Slice0 + 11,
};
inline constexpr int RoleCount = int(Role::LastSlice) + 1;
inline constexpr int SliceCount = int(Role::LastSlice) - int(Role::Slice0) + 1;
inline Role sliceRole(int n)
{
    return Role(int(Role::Slice0) + (n % SliceCount + SliceCount) % SliceCount);
}

/// A colour: `color` ("#rrggbb" or "#rrggbbaa", validated) when set, else the palette's `role`.
struct Paint {
    Role role = Role::None;
    QString color;
    bool isNone() const { return role == Role::None && color.isEmpty(); }
    friend bool operator==(const Paint &, const Paint &) = default;
};

/// One step of a path (absolute coordinates). Cubic: `c1`, `c2` the control points, `p` the end.
struct PathOp {
    enum class Type : quint8 { Move, Line, Cubic, Close };
    Type type = Type::Move;
    QPointF p, c1, c2;
};

enum class Dash : quint8 { Solid, Dotted, Dashed };

/// A primitive of the scene, painted in list order.
struct Item {
    enum class Type : quint8 { Path, Text };
    Type type = Type::Path;

    // Path
    QList<PathOp> path;
    Paint fill;   ///< None: not filled
    Paint stroke; ///< None: not stroked
    qreal strokeWidth = 1;
    Dash dash = Dash::Solid;

    // Text: `lines` stacked in `box` (vertically centred, each `lineHeight` high), aligned in it.
    QRectF box;
    QStringList lines;
    Qt::Alignment align = Qt::AlignHCenter;
    qreal fontSize = 14;
    qreal lineHeight = 17.5;
    bool bold = false;
    Paint color;      ///< the text's colour
    Paint background; ///< painted as `box` behind the text (None: nothing)
};

/// A laid-out diagram: its items within (0, 0) – `size`.
struct Scene {
    QSizeF size;
    QList<Item> items;
    QString title; ///< the diagram's title (front matter, `title`), for the SVG's <title>
};

/// The colours of the roles ("#rrggbb" or "#rrggbbaa").
struct Palette {
    QString colors[RoleCount];
    const QString &operator[](Role r) const { return colors[int(r)]; }
    QString &operator[](Role r) { return colors[int(r)]; }
};

/// The few theme colours a palette is derived from ("#rgb"/"#rrggbb"; malformed: the defaults).
/// Obsidian-like: nodes tinted with the accent, lines in the muted text colour.
struct ThemeColors {
    QString background = QStringLiteral("#fbf7ee"); ///< what the diagram is drawn on
    QString text = QStringLiteral("#1f2430");
    QString mutedText = QStringLiteral("#535a6a");
    QString border = QStringLiteral("#ddd6c6");
    QString accent = QStringLiteral("#2c6aa3");
    bool dark = false;
};
Palette themedPalette(const ThemeColors &theme);
/// Brulion's light and dark look (the HTML export's colours).
Palette lightPalette();
Palette darkPalette();
/// The resolved colour of `paint` ("" for None).
QString colorOf(const Paint &paint, const Palette &palette);

/// Why a source could not be drawn. The app translates the code (src/app/editor/mermaidview.cpp).
struct Error {
    enum class Code {
        Empty,            ///< no diagram text
        UnknownType,      ///< the first statement names no diagram type
        Unsupported,      ///< a Mermaid type Brulion does not draw (Result::typeName)
        Syntax,           ///< a statement Brulion cannot read (`detail`: its text, cut)
        UnterminatedText, ///< a quote or bracket not closed on its line
        UnexpectedEnd,    ///< `end` without a subgraph or block
        UnclosedBlock,    ///< a subgraph or block without its `end` (line: where it starts)
        InvalidValue,     ///< pie: a value that is no number
        NoData,           ///< pie: no slice with a value above zero
        TooLarge,         ///< beyond limits (`detail`: what)
    };
    Code code = Code::Syntax;
    int line = 0; ///< 1-based line of the source (0: the whole diagram)
    QString detail;
};

struct Result {
    Kind kind = Kind::Unknown;
    QString typeName;
    Scene scene;
    std::optional<Error> error; ///< set: nothing to draw (the scene is empty)
    bool ok() const { return !error.has_value(); }
};

/// Parses and lays out `source` (the block's text between its fences). Metrics without `measure`
/// use approximateMetrics().
Result render(QStringView source, const Metrics &metrics = approximateMetrics());

/// A colour from a diagram's style (`fill:#f9f`, `stroke:red`, `color:rgb(1,2,3)`): "#rgb",
/// "#rgba", "#rrggbb", "#rrggbbaa", rgb()/rgba() and the CSS colour names → "#rrggbb[aa]"
/// (lower case); "" for anything else (`transparent`/`none`: "#00000000").
QString parseCssColor(QStringView text);

} // namespace brulion::diagram
