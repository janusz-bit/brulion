// The flowchart parser: Mermaid's `graph` / `flowchart` statements (docs/SYNTAX.md §11 "Mermaid diagrams").
#include "diagram/flowchart_p.h"

#include <QRegularExpression>
#include <QStringTokenizer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::diagram::detail {

namespace {

using Code = Error::Code;

std::optional<Direction> directionOf(QStringView word)
{
    if (word == u"TB" || word == u"TD" || word == u"v")
        return Direction::TB;
    if (word == u"BT" || word == u"^")
        return Direction::BT;
    if (word == u"LR" || word == u">")
        return Direction::LR;
    if (word == u"RL" || word == u"<")
        return Direction::RL;
    return std::nullopt;
}

/// Does `s` start with keyword `kw` as a whole word?
bool startsWithWord(QStringView s, QStringView kw)
{
    return s.startsWith(kw) && (s.size() == kw.size() || s[kw.size()].isSpace());
}

void skipSpaces(QStringView s, qsizetype &i)
{
    while (i < s.size() && s[i].isSpace())
        ++i;
}

struct Opener {
    QStringView open;
    QList<std::pair<QStringView, NodeShape>> closers;
};

/// The shapes' brackets, longest first.
const QList<Opener> &openers()
{
    static const QList<Opener> list = {
            {u"(((", {{u")))", NodeShape::DoubleCircle}}},
            {u"((", {{u"))", NodeShape::Circle}}},
            {u"([", {{u"])", NodeShape::Stadium}}},
            {u"[[", {{u"]]", NodeShape::Subroutine}}},
            {u"[(", {{u")]", NodeShape::Cylinder}}},
            {u"[/", {{u"/]", NodeShape::Parallelogram}, {u"\\]", NodeShape::Trapezoid}}},
            {u"[\\", {{u"\\]", NodeShape::ParallelogramAlt}, {u"/]", NodeShape::TrapezoidAlt}}},
            {u"{{", {{u"}}", NodeShape::Hexagon}}},
            {u"[", {{u"]", NodeShape::Rect}}},
            {u"(", {{u")", NodeShape::Rounded}}},
            {u"{", {{u"}", NodeShape::Rhombus}}},
            {u">", {{u"]", NodeShape::Asymmetric}}},
    };
    return list;
}

/// The end of a shape's text: `text` where the text ends, `length` the closer's (with the spaces
/// before it after a quoted text).
struct ShapeEnd {
    qsizetype text = 0;
    qsizetype length = 0;
    NodeShape shape = NodeShape::Rect;
};

/// The end of the text from `k` of a shape opened by `o`; nullopt when it is not closed.
std::optional<ShapeEnd> shapeEnd(QStringView s, qsizetype k, const Opener &o)
{
    qsizetype q = k;
    skipSpaces(s, q);
    if (q < s.size() && s[q] == u'"') {
        // A quoted text: anything up to the next quote, then the closer.
        const qsizetype endQuote = s.indexOf(u'"', q + 1);
        if (endQuote < 0)
            return std::nullopt;
        qsizetype after = endQuote + 1;
        skipSpaces(s, after);
        for (const auto &[closer, shape] : o.closers) {
            if (s.sliced(after).startsWith(closer))
                return ShapeEnd{endQuote + 1, after - endQuote - 1 + closer.size(), shape};
        }
        return std::nullopt;
    }
    // The first closer, in one scan: a search for each closer would read on to the end for a
    // missing one, at every node of a statement.
    for (qsizetype p = k; p < s.size(); ++p) {
        for (const auto &[closer, shape] : o.closers) {
            if (s.sliced(p).startsWith(closer))
                return ShapeEnd{p, closer.size(), shape};
        }
    }
    return std::nullopt;
}

/// Mermaid's `A@{ shape: … }` names of the shapes Brulion draws.
std::optional<NodeShape> namedShape(QStringView name)
{
    static const QHash<QString, NodeShape> names = {
            {u"rect"_s, NodeShape::Rect},
            {u"rectangle"_s, NodeShape::Rect},
            {u"proc"_s, NodeShape::Rect},
            {u"process"_s, NodeShape::Rect},
            {u"rounded"_s, NodeShape::Rounded},
            {u"event"_s, NodeShape::Rounded},
            {u"stadium"_s, NodeShape::Stadium},
            {u"pill"_s, NodeShape::Stadium},
            {u"terminal"_s, NodeShape::Stadium},
            {u"subproc"_s, NodeShape::Subroutine},
            {u"subprocess"_s, NodeShape::Subroutine},
            {u"subroutine"_s, NodeShape::Subroutine},
            {u"fr-rect"_s, NodeShape::Subroutine},
            {u"cyl"_s, NodeShape::Cylinder},
            {u"cylinder"_s, NodeShape::Cylinder},
            {u"db"_s, NodeShape::Cylinder},
            {u"database"_s, NodeShape::Cylinder},
            {u"circle"_s, NodeShape::Circle},
            {u"circ"_s, NodeShape::Circle},
            {u"dbl-circ"_s, NodeShape::DoubleCircle},
            {u"double-circle"_s, NodeShape::DoubleCircle},
            {u"diam"_s, NodeShape::Rhombus},
            {u"diamond"_s, NodeShape::Rhombus},
            {u"decision"_s, NodeShape::Rhombus},
            {u"question"_s, NodeShape::Rhombus},
            {u"hex"_s, NodeShape::Hexagon},
            {u"hexagon"_s, NodeShape::Hexagon},
            {u"prepare"_s, NodeShape::Hexagon},
            {u"lean-r"_s, NodeShape::Parallelogram},
            {u"lean-right"_s, NodeShape::Parallelogram},
            {u"in-out"_s, NodeShape::Parallelogram},
            {u"lean-l"_s, NodeShape::ParallelogramAlt},
            {u"lean-left"_s, NodeShape::ParallelogramAlt},
            {u"out-in"_s, NodeShape::ParallelogramAlt},
            {u"trap-b"_s, NodeShape::Trapezoid},
            {u"trapezoid"_s, NodeShape::Trapezoid},
            {u"priority"_s, NodeShape::Trapezoid},
            {u"trap-t"_s, NodeShape::TrapezoidAlt},
            {u"inv-trapezoid"_s, NodeShape::TrapezoidAlt},
            {u"manual"_s, NodeShape::TrapezoidAlt},
            {u"odd"_s, NodeShape::Asymmetric},
    };
    const auto it = names.constFind(name.trimmed().toString().toLower());
    return it == names.cend() ? std::nullopt : std::optional(*it);
}

struct NodeRef {
    QString id;
    std::optional<NodeShape> shape;
    std::optional<QString> label;
    QStringList classes;
};

struct LinkTok {
    LinkStroke stroke = LinkStroke::Solid;
    Head start = Head::None, end = Head::None;
    int length = 1;
    QString label;
};

class Parser {
public:
    explicit Parser(QStringView source) : m_source(source) { }

    Outcome<Flowchart> run()
    {
        const QList<Statement> st = statements(m_source, true, &m_chart.title);
        if (st.isEmpty())
            return makeError(Code::Empty, 0);
        // The header: `graph TD`, `flowchart LR`, maybe more on the same line.
        {
            const Statement &h = st.front();
            const QStringView s = h.text;
            const auto wordEnd = [&](qsizetype k) {
                while (k < s.size() && !s[k].isSpace())
                    ++k;
                return k;
            };
            qsizetype i = wordEnd(0);
            const QStringView word = s.first(i);
            if (word != u"graph" && word != u"flowchart" && word != u"flowchart-elk")
                return makeError(Code::UnknownType, h.line);
            skipSpaces(s, i);
            const qsizetype j = wordEnd(i);
            if (const auto d = directionOf(s.sliced(i, j - i))) {
                m_chart.direction = *d;
                i = j;
                skipSpaces(s, i);
            }
            if (i < s.size())
                if (auto e = statement({s.sliced(i).toString(), h.line}))
                    return *e;
        }
        for (qsizetype k = 1; k < st.size(); ++k) {
            if (auto e = statement(st[k]))
                return *e;
        }
        if (!m_stack.isEmpty())
            return makeError(Code::UnclosedBlock, m_chart.clusters.at(m_stack.constLast()).line);
        if (m_inAccDescr)
            return makeError(Code::UnclosedBlock, m_accDescrLine);
        finish();
        return std::move(m_chart);
    }

private:
    std::optional<Error> statement(const Statement &st)
    {
        const QStringView s = st.text;
        m_line = st.line;
        if (m_inAccDescr) {
            if (s.contains(u'}'))
                m_inAccDescr = false;
            return std::nullopt;
        }
        if (s == u"end")
            return endCluster();
        if (startsWithWord(s, u"subgraph"))
            return beginCluster(s.sliced(8).trimmed());
        if (startsWithWord(s, u"direction")) {
            const auto d = directionOf(s.sliced(9).trimmed());
            if (!d)
                return makeError(Code::Syntax, m_line, excerpt(s));
            if (m_stack.isEmpty())
                m_chart.direction = *d;
            else
                m_chart.clusters[m_stack.constLast()].direction = d;
            return std::nullopt;
        }
        if (startsWithWord(s, u"classDef"))
            return classDef(s.sliced(8).trimmed());
        if (startsWithWord(s, u"class"))
            return classAssign(s.sliced(5).trimmed());
        if (startsWithWord(s, u"style")) {
            const QStringView rest = s.sliced(5).trimmed();
            const qsizetype sp = rest.indexOf(u' ');
            if (sp < 0)
                return makeError(Code::Syntax, m_line, excerpt(s));
            m_styles.append({rest.first(sp).toString(), parseStyle(rest.sliced(sp + 1))});
            return std::nullopt;
        }
        if (startsWithWord(s, u"linkStyle"))
            return linkStyle(s.sliced(9).trimmed());
        // Interactions are never run: links, callbacks and tooltips are left out entirely.
        if (startsWithWord(s, u"click") || startsWithWord(s, u"href") || startsWithWord(s, u"callback"))
            return std::nullopt;
        if (s.startsWith(u"accTitle") && s.sliced(8).trimmed().startsWith(u':')) {
            if (m_chart.title.isEmpty())
                m_chart.title = cleanLabel(s.sliced(s.indexOf(u':') + 1));
            return std::nullopt;
        }
        if (s.startsWith(u"accDescr")) {
            const QStringView rest = s.sliced(8).trimmed();
            if (rest.startsWith(u'{') && !rest.contains(u'}')) {
                m_inAccDescr = true;
                m_accDescrLine = m_line;
            }
            if (rest.startsWith(u'{') || rest.startsWith(u':'))
                return std::nullopt;
        }
        if (startsWithWord(s, u"title")) {
            if (m_chart.title.isEmpty())
                m_chart.title = cleanLabel(s.sliced(5));
            return std::nullopt;
        }
        return chain(s);
    }

    std::optional<Error> beginCluster(QStringView rest)
    {
        if (m_stack.size() >= limits::MaxNesting)
            return makeError(Code::TooLarge, m_line, u"nesting"_s);
        if (m_chart.clusters.size() >= limits::MaxClusters)
            return makeError(Code::TooLarge, m_line, u"subgraphs"_s);
        FlowCluster c;
        c.line = m_line;
        c.parent = m_stack.isEmpty() ? -1 : m_stack.constLast();
        static const QRegularExpression idTitle(u"^([^\\s\\[\\]\"]+)\\s*\\[(.*)\\]$"_s);
        if (rest.isEmpty()) {
            c.id = u"subGraph"_s + QString::number(m_chart.clusters.size());
        } else if (const auto m = idTitle.matchView(rest); m.hasMatch()) {
            c.id = m.captured(1);
            c.title = cleanLabel(m.capturedView(2));
        } else if (rest.front() == u'"') {
            if (rest.size() < 2 || rest.back() != u'"')
                return makeError(Code::UnterminatedText, m_line);
            c.id = u"subGraph"_s + QString::number(m_chart.clusters.size());
            c.title = cleanLabel(rest);
        } else {
            c.id = rest.toString();
            c.title = cleanLabel(rest);
        }
        m_chart.clusters.append(std::move(c));
        m_stack.append(int(m_chart.clusters.size() - 1));
        return std::nullopt;
    }

    std::optional<Error> endCluster()
    {
        if (m_stack.isEmpty())
            return makeError(Code::UnexpectedEnd, m_line);
        m_stack.removeLast();
        return std::nullopt;
    }

    std::optional<Error> classDef(QStringView rest)
    {
        const qsizetype sp = rest.indexOf(u' ');
        if (sp < 0)
            return makeError(Code::Syntax, m_line, excerpt(rest));
        const Style style = parseStyle(rest.sliced(sp + 1));
        for (const QStringView name : qTokenize(rest.first(sp), u','))
            m_classDefs[name.trimmed().toString()].merge(style);
        return std::nullopt;
    }

    std::optional<Error> classAssign(QStringView rest)
    {
        const qsizetype sp = rest.lastIndexOf(u' ');
        if (sp < 0)
            return makeError(Code::Syntax, m_line, excerpt(rest));
        const QString cls = rest.sliced(sp + 1).trimmed().toString();
        for (const QStringView id : qTokenize(rest.first(sp), u','))
            m_classAssigns.append({id.trimmed().toString(), cls});
        return std::nullopt;
    }

    std::optional<Error> linkStyle(QStringView rest)
    {
        const qsizetype sp = rest.indexOf(u' ');
        if (sp < 0)
            return makeError(Code::Syntax, m_line, excerpt(rest));
        QString css = rest.sliced(sp + 1).trimmed().toString();
        static const QRegularExpression interpolate(u"^interpolate\\s+\\S+\\s*"_s);
        css.remove(interpolate);
        const Style style = parseStyle(css);
        const QStringView which = rest.first(sp);
        if (which == u"default") {
            m_defaultLinkStyle.merge(style);
            return std::nullopt;
        }
        for (const QStringView n : qTokenize(which, u',')) {
            bool ok = false;
            const int index = n.trimmed().toInt(&ok);
            if (!ok || index < 0)
                return makeError(Code::Syntax, m_line, excerpt(rest));
            m_linkStyles.append({index, style});
        }
        return std::nullopt;
    }

    /// A node: its id, then optionally `@{…}`, a shape with its text, `:::class`.
    std::optional<NodeRef> nodeRef(QStringView s, qsizetype &i, std::optional<Error> &error)
    {
        skipSpaces(s, i);
        const qsizetype start = i;
        // Letters, digits and `_`; `-` and `.` between them.
        const auto idChar = [&](qsizetype k) {
            return isIdChar(s[k])
                   || ((s[k] == u'-' || s[k] == u'.') && k > start && k + 1 < s.size() && isIdChar(s[k + 1]));
        };
        while (i < s.size() && idChar(i))
            ++i;
        if (i == start)
            return std::nullopt;
        NodeRef ref;
        ref.id = s.sliced(start, i - start).toString();
        // Mermaid 11's `A@{ shape: diam, label: "…" }`.
        if (s.sliced(i).startsWith(u"@{")) {
            const qsizetype close = s.indexOf(u'}', i);
            if (close < 0) {
                error = makeError(Code::UnterminatedText, m_line);
                return std::nullopt;
            }
            // (`\b`: tried where a word starts, not again from each of its letters.)
            static const QRegularExpression prop(u"\\b(\\w+)\\s*:\\s*(\"(?:[^\"\\\\]|\\\\.)*\"|[^,]*)"_s);
            for (auto it = prop.globalMatchView(s.sliced(i + 2, close - i - 2)); it.hasNext();) {
                const auto m = it.next();
                const QStringView key = m.capturedView(1);
                if (key == u"shape") {
                    if (const auto shape = namedShape(m.capturedView(2)))
                        ref.shape = shape;
                } else if (key == u"label") {
                    ref.label = cleanLabel(m.capturedView(2));
                }
            }
            i = close + 1;
        }
        // Spaces between the id and a bracket are allowed (not before `>`, a link's end).
        qsizetype j = i;
        skipSpaces(s, j);
        const QStringView at = s.sliced(j);
        const auto o = std::ranges::find_if(
                openers(), [&](const Opener &op) { return at.startsWith(op.open) && (j == i || op.open != u">"); });
        if (o != openers().cend()) {
            const qsizetype k = j + o->open.size();
            const auto end = shapeEnd(s, k, *o);
            if (!end) {
                error = makeError(Code::UnterminatedText, m_line);
                return std::nullopt;
            }
            ref.shape = end->shape;
            ref.label = cleanLabel(s.sliced(k, end->text - k));
            i = end->text + end->length;
        }
        while (s.sliced(i).startsWith(u":::")) {
            i += 3;
            const qsizetype c0 = i;
            while (i < s.size() && (isIdChar(s[i]) || s[i] == u'-'))
                ++i;
            if (i > c0)
                ref.classes.append(s.sliced(c0, i - c0).toString());
        }
        return ref;
    }

    /// The text of `-- text -->`: up to the closing part of the link (`closing`'s match).
    std::optional<LinkTok> textLink(QStringView s, qsizetype &j, const QRegularExpression &closing, LinkTok link,
                                    QChar body)
    {
        const auto m = closing.matchView(s, j);
        if (!m.hasMatch() || m.capturedStart() == j)
            return std::nullopt;
        link.label = cleanLabel(s.sliced(j, m.capturedStart() - j));
        const QStringView c = m.capturedView();
        if (c.endsWith(u'>'))
            link.end = Head::Arrow;
        else if (c.endsWith(u'o'))
            link.end = Head::Circle;
        else if (c.endsWith(u'x'))
            link.end = Head::Cross;
        const qsizetype bodies = c.count(body);
        if (link.stroke == LinkStroke::Dotted)
            link.length = int(std::max<qsizetype>(1, bodies));
        else
            link.length = int(std::max<qsizetype>(1, link.end == Head::None ? bodies - 2 : bodies - 1));
        j = m.capturedEnd();
        return link;
    }

    /// A link at `i` (spaces before it skipped): `-->`, `---`, `-.->`, `==>`, `~~~`, `<-->`,
    /// `o--o`, `x--x`, longer forms, `-- text -->`, `-->|text|`.
    std::optional<LinkTok> link(QStringView s, qsizetype &i, std::optional<Error> &error)
    {
        qsizetype j = i;
        skipSpaces(s, j);
        LinkTok t;
        if (j < s.size() && s[j] == u'<') {
            t.start = Head::Arrow;
            ++j;
        } else if (j + 1 < s.size() && (s[j] == u'o' || s[j] == u'x') && (s[j + 1] == u'-' || s[j + 1] == u'=')) {
            t.start = s[j] == u'o' ? Head::Circle : Head::Cross;
            ++j;
        }
        if (j >= s.size())
            return std::nullopt;
        const auto endHead = [&](qsizetype &k) {
            if (k < s.size() && s[k] == u'>') {
                ++k;
                return Head::Arrow;
            }
            if (k < s.size() && (s[k] == u'o' || s[k] == u'x') && (k + 1 == s.size() || !isIdChar(s[k + 1]))) {
                const Head h = s[k] == u'o' ? Head::Circle : Head::Cross;
                ++k;
                return h;
            }
            return Head::None;
        };
        const auto run = [&](qsizetype k, QChar c) {
            qsizetype n = 0;
            while (k + n < s.size() && s[k + n] == c)
                ++n;
            return n;
        };
        const QChar c = s[j];
        if (c == u'~') {
            const qsizetype n = run(j, u'~');
            if (n < 3)
                return std::nullopt;
            t.stroke = LinkStroke::Invisible;
            t.length = int(n - 2);
            j += n;
        } else if (c == u'-' && j + 1 < s.size() && s[j + 1] == u'.') {
            t.stroke = LinkStroke::Dotted;
            const qsizetype dots = run(j + 1, u'.');
            j += 1 + dots;
            if (j < s.size() && s[j] == u'-') {
                ++j;
                t.end = endHead(j);
                t.length = int(dots);
            } else {
                // (`(?<!\.)`: tried where a run of dots starts, not again from each of its dots.)
                static const QRegularExpression closing(u"(?<!\\.)\\.+-(?:>|[ox](?![\\p{L}\\p{N}_])|)"_s);
                const auto r = textLink(s, j, closing, t, u'.');
                if (!r)
                    return std::nullopt;
                t = *r;
            }
        } else if (c == u'-' || c == u'=') {
            const qsizetype n = run(j, c);
            if (n < 2)
                return std::nullopt;
            t.stroke = c == u'=' ? LinkStroke::Thick : LinkStroke::Solid;
            j += n;
            if (const Head h = endHead(j); h != Head::None) {
                t.end = h;
                t.length = int(n - 1);
            } else if (n >= 3) {
                t.length = int(n - 2);
            } else {
                static const QRegularExpression solid(u"-{2,}>|-{2,}[ox](?![\\p{L}\\p{N}_])|-{3,}"_s);
                static const QRegularExpression thick(u"={2,}>|={2,}[ox](?![\\p{L}\\p{N}_])|={3,}"_s);
                const auto r = textLink(s, j, c == u'=' ? thick : solid, t, c);
                if (!r) {
                    error = makeError(Code::Syntax, m_line, excerpt(s.sliced(i).trimmed()));
                    return std::nullopt;
                }
                t = *r;
            }
        } else {
            return std::nullopt;
        }
        // `-->|text|`
        qsizetype k = j;
        skipSpaces(s, k);
        if (k < s.size() && s[k] == u'|') {
            // A quoted label may hold `|`.
            qsizetype q = k + 1;
            skipSpaces(s, q);
            const qsizetype quote = (q < s.size() && s[q] == u'"') ? s.indexOf(u'"', q + 1) : -1;
            const qsizetype close = s.indexOf(u'|', quote < 0 ? k + 1 : quote + 1);
            if (close < 0) {
                error = makeError(Code::UnterminatedText, m_line);
                return std::nullopt;
            }
            t.label = cleanLabel(s.sliced(k + 1, close - k - 1));
            j = close + 1;
        }
        i = j;
        return t;
    }

    std::optional<QList<int>> group(QStringView s, qsizetype &i, std::optional<Error> &error)
    {
        QList<int> out;
        while (const auto ref = nodeRef(s, i, error)) {
            const int n = touch(*ref, error);
            if (n < 0)
                return std::nullopt;
            out.append(n);
            qsizetype k = i;
            skipSpaces(s, k);
            if (k >= s.size() || s[k] != u'&')
                return out;
            i = k + 1;
        }
        if (error || out.isEmpty())
            return std::nullopt;
        return out;
    }

    std::optional<Error> chain(QStringView s)
    {
        std::optional<Error> error;
        qsizetype i = 0;
        auto prev = group(s, i, error);
        if (!prev)
            return error ? error : makeError(Code::Syntax, m_line, excerpt(s));
        while (true) {
            skipSpaces(s, i);
            if (i >= s.size())
                return std::nullopt;
            const auto l = link(s, i, error);
            if (!l)
                return error ? error : makeError(Code::Syntax, m_line, excerpt(s.sliced(i)));
            auto next = group(s, i, error);
            if (!next)
                return error ? error : makeError(Code::Syntax, m_line, excerpt(s));
            for (const int a : std::as_const(*prev)) {
                for (const int b : std::as_const(*next)) {
                    if (m_chart.edges.size() >= limits::MaxEdges)
                        return makeError(Code::TooLarge, m_line, u"links"_s);
                    m_chart.edges.append({.from = {a, false},
                                          .to = {b, false},
                                          .label = l->label,
                                          .stroke = l->stroke,
                                          .startHead = l->start,
                                          .endHead = l->end,
                                          .length = std::clamp(l->length, 1, 10),
                                          .style = {},
                                          .line = m_line});
                }
            }
            prev = std::move(next);
        }
    }

    /// The node of `ref` (made when new; its shape, label and classes set), in the subgraph being
    /// read when it is in none yet or in one around it. -1 when there are too many nodes.
    int touch(const NodeRef &ref, std::optional<Error> &error)
    {
        int n = m_ids.value(ref.id, -1);
        if (n < 0) {
            if (m_chart.nodes.size() >= limits::MaxNodes) {
                error = makeError(Code::TooLarge, m_line, u"nodes"_s);
                return -1;
            }
            n = int(m_chart.nodes.size());
            m_chart.nodes.append(
                    {.id = ref.id, .label = capLength(ref.id), .classes = {}, .style = {}, .line = m_line});
            m_ids.insert(ref.id, n);
        }
        FlowNode &node = m_chart.nodes[n];
        if (ref.shape)
            node.shape = *ref.shape;
        if (ref.label)
            node.label = *ref.label;
        node.defined = node.defined || ref.shape || ref.label;
        node.classes.append(ref.classes);
        if (!m_stack.isEmpty()) {
            const int c = m_stack.constLast();
            // Moved into a deeper subgraph of the one it is in (or into one when it is in none).
            const auto encloses = [&](int outer, int inner) {
                for (int k = inner; k >= 0; k = m_chart.clusters.at(k).parent)
                    if (k == outer)
                        return true;
                return false;
            };
            if (node.cluster < 0 || (node.cluster != c && encloses(node.cluster, c)))
                node.cluster = c;
        }
        return n;
    }

    /// Links to subgraphs, classes and styles.
    void finish()
    {
        QHash<QString, int> clusterIds;
        for (qsizetype c = 0; c < m_chart.clusters.size(); ++c)
            clusterIds.insert(m_chart.clusters.at(c).id, int(c));
        // A node named like a subgraph and never given a shape or label is that subgraph.
        QList<int> remap(m_chart.nodes.size(), -1);
        QList<int> asCluster(m_chart.nodes.size(), -1);
        QList<FlowNode> kept;
        for (qsizetype n = 0; n < m_chart.nodes.size(); ++n) {
            const FlowNode &node = m_chart.nodes.at(n);
            const int c = clusterIds.value(node.id, -1);
            if (c >= 0 && !node.defined) {
                asCluster[n] = c;
                m_chart.clusters[c].classes.append(node.classes);
                continue;
            }
            remap[n] = int(kept.size());
            kept.append(node);
        }
        if (kept.size() != m_chart.nodes.size()) {
            const auto fix = [&](FlowEnd &e) {
                if (asCluster[e.index] >= 0)
                    e = {asCluster[e.index], true};
                else
                    e.index = remap[e.index];
            };
            for (FlowEdge &e : m_chart.edges) {
                fix(e.from);
                fix(e.to);
            }
            m_chart.nodes = std::move(kept);
            m_ids.clear();
            for (qsizetype n = 0; n < m_chart.nodes.size(); ++n)
                m_ids.insert(m_chart.nodes.at(n).id, int(n));
        }
        for (const auto &[id, cls] : std::as_const(m_classAssigns)) {
            if (const int n = m_ids.value(id, -1); n >= 0)
                m_chart.nodes[n].classes.append(cls);
            else if (const int c = clusterIds.value(id, -1); c >= 0)
                m_chart.clusters[c].classes.append(cls);
        }
        const auto classStyle = [&](Style s, const QStringList &classes) {
            for (const QString &cls : classes)
                s.merge(m_classDefs.value(cls));
            return s;
        };
        const Style defaults = m_classDefs.value(u"default"_s);
        for (FlowNode &node : m_chart.nodes)
            node.style = classStyle(defaults, node.classes);
        for (FlowCluster &c : m_chart.clusters)
            c.style = classStyle({}, c.classes);
        for (const auto &[id, style] : std::as_const(m_styles)) {
            if (const int n = m_ids.value(id, -1); n >= 0)
                m_chart.nodes[n].style.merge(style);
            else if (const int c = clusterIds.value(id, -1); c >= 0)
                m_chart.clusters[c].style.merge(style);
        }
        for (FlowEdge &e : m_chart.edges)
            e.style = m_defaultLinkStyle;
        for (const auto &[index, style] : std::as_const(m_linkStyles))
            if (index < m_chart.edges.size())
                m_chart.edges[index].style.merge(style);
    }

    QStringView m_source;
    Flowchart m_chart;
    QHash<QString, int> m_ids;
    QList<int> m_stack;
    QHash<QString, Style> m_classDefs;
    QList<std::pair<QString, QString>> m_classAssigns;
    QList<std::pair<QString, Style>> m_styles;
    QList<std::pair<int, Style>> m_linkStyles;
    Style m_defaultLinkStyle;
    bool m_inAccDescr = false;
    int m_accDescrLine = 0;
    int m_line = 0;
};

} // namespace

Outcome<Flowchart> parseFlowchart(QStringView source)
{
    return Parser(source).run();
}

} // namespace brulion::diagram::detail
