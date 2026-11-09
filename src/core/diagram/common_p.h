// Internal helpers of the diagram module: statements of a source, labels (quotes, entities,
// <br>, wrapping), styles, paths, arrowheads, colour arithmetic.
#pragma once

#include "diagram/diagram.h"

#include <QHash>

namespace brulion::diagram::detail {

/// A statement of a source: its text (trimmed) and 1-based line.
struct Statement {
    QString text;
    int line = 0;
};

/// The statements of `source`: front matter (`---` … `---` at the top: its `title:` in `title`),
/// `%%` comments and `%%{…}%%` directives skipped; `splitSemicolons`: `a; b` are two statements
/// (outside quotes and brackets). Empty lines dropped.
QList<Statement> statements(QStringView source, bool splitSemicolons, QString *title = nullptr);

/// A label as written (`"quoted"`, `` "`markdown`" ``, `#quot;` entities, `<br>`, simple
/// formatting tags) → its text, lines separated by '\n'.
QString cleanLabel(QStringView raw);
/// Text cut to limits::MaxLabelLength characters ("…" at the end when cut).
QString capLength(const QString &text);

/// Text laid out in lines.
struct TextBlock {
    QStringList lines;
    QSizeF size;
    qreal lineHeight = 0;
    qreal pixelSize = 0;
    bool bold = false;
};
/// `text` ('\n': line breaks) wrapped at `maxWidth` (0: no wrapping), at most
/// limits::MaxLabelLines lines (the last ending in "…" when cut).
TextBlock layoutText(const Metrics &m, const QString &text, qreal pixelSize, bool bold, qreal maxWidth);
qreal lineHeightOf(const Metrics &m, qreal pixelSize);

/// A text item for `t` centred (or aligned) in `box`.
Item textItem(const TextBlock &t, const QRectF &box, Paint color, Qt::Alignment align = Qt::AlignHCenter);

/// Paths.
QList<PathOp> rectPath(const QRectF &r);
QList<PathOp> roundedRectPath(const QRectF &r, qreal radius);
QList<PathOp> ellipsePath(const QPointF &c, qreal rx, qreal ry);
QList<PathOp> polygonPath(const QList<QPointF> &points);
/// An elliptic arc from angle `a0` to `a1` (radians, y down, any span ≤ 2π) as cubics, appended
/// (no Move: the caller's current point is the arc's start).
void appendArc(QList<PathOp> &path, const QPointF &c, qreal rx, qreal ry, qreal a0, qreal a1);
Item pathItem(QList<PathOp> path, Paint fill, Paint stroke, qreal width = 1, Dash dash = Dash::Solid);

/// Line ends.
enum class Head : quint8 { None, Arrow, Open, Circle, Cross };
/// Draws head `h` at `tip` for a line arriving in direction `dir` (a unit vector).
void addHead(QList<Item> &items, Head h, const QPointF &tip, const QPointF &dir, Paint color, qreal lineWidth);
/// How far before the tip of head `h` its line stops (under the head's base).
qreal headInset(Head h, qreal lineWidth);
/// The unit vector from `a` to `b` (`fallback` when they are the same point).
QPointF unitVector(QPointF a, QPointF b, QPointF fallback = QPointF(0, 1));

/// A node's or link's style (`style`, `classDef`, `linkStyle`): colours already validated.
struct Style {
    QString fill, stroke, color;
    qreal strokeWidth = -1;
    std::optional<Dash> dash;
    bool bold = false;
    bool empty() const
    {
        return fill.isEmpty() && stroke.isEmpty() && color.isEmpty() && strokeWidth < 0 && !dash && !bold;
    }
    /// `other`'s properties over this one's.
    void merge(const Style &other);
};
/// `fill:#f9f,stroke:#333,stroke-width:4px,color:#fff,stroke-dasharray: 5 5` (commas or
/// semicolons); unknown properties and invalid values dropped.
Style parseStyle(QStringView text);

/// Colours ("#rrggbb[aa]" in, "#rrggbb" out).
struct Rgba {
    double r = 0, g = 0, b = 0, a = 1;
};
std::optional<Rgba> parseHex(QStringView hex);
QString toHex(const Rgba &c, bool withAlpha = false);
/// `a` · (1 − t) + `b` · t.
QString mix(const QString &a, const QString &b, double t);
double luminance(const QString &hex);
/// A readable text colour on `fill`: one of `light`/`dark` by contrast.
QString textOn(const QString &fill, const QString &dark, const QString &light);

/// Is `c` a character of an identifier (letters of any script, digits, `_`)?
inline bool isIdChar(QChar c)
{
    return c.isLetterOrNumber() || c == u'_' || c.category() == QChar::Mark_NonSpacing;
}

/// Error helpers.
inline Error makeError(Error::Code code, int line, QString detail = {})
{
    return Error{code, line, std::move(detail)};
}

/// A value or the Error why there is none (std::expected is C++23).
template<typename T>
class Outcome {
public:
    Outcome(T value) : m_value(std::move(value)) { }
    Outcome(Error error) : m_error(std::move(error)) { }
    explicit operator bool() const { return m_value.has_value(); }
    T &operator*() { return *m_value; }
    const T &operator*() const { return *m_value; }
    T *operator->() { return &*m_value; }
    const T *operator->() const { return &*m_value; }
    const Error &error() const { return m_error; }

private:
    std::optional<T> m_value;
    Error m_error;
};
/// A statement's text cut for an error message.
QString excerpt(QStringView text);

} // namespace brulion::diagram::detail
