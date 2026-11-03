// Sequence diagrams (docs/SYNTAX.md §11 "Mermaid diagrams"): participants and actors (`as` aliases, named
// by their first message too), messages with every Mermaid arrow, activations (`+`/`-`,
// activate/deactivate), notes, blocks (loop, alt/else, opt, par/and, critical/option, break,
// rect), boxes, autonumber, title. Participants on top and again at the bottom, lifelines between.
#include "diagram/sequence_p.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::diagram::detail {

namespace {

using Code = Error::Code;
using Type = SeqEvent::Type;

bool startsWithWord(QStringView s, QStringView kw, Qt::CaseSensitivity cs = Qt::CaseSensitive)
{
    return s.startsWith(kw, cs) && (s.size() == kw.size() || s[kw.size()].isSpace() || s[kw.size()] == u':');
}

class Parser {
public:
    Outcome<SequenceDiagram> run(QStringView source)
    {
        const QList<Statement> st = statements(source, false, &m_d.title);
        if (st.isEmpty())
            return makeError(Code::Empty, 0);
        if (!startsWithWord(st.front().text, u"sequenceDiagram"))
            return makeError(Code::UnknownType, st.front().line);
        for (qsizetype i = 1; i < st.size(); ++i)
            if (auto e = statement(st[i]))
                return *e;
        if (!m_open.isEmpty())
            return makeError(Code::UnclosedBlock, m_open.last().second);
        return std::move(m_d);
    }

private:
    /// The participant `id` (named now when new). -1: too many.
    int participant(const QString &idIn, std::optional<Error> &error)
    {
        const QString id = idIn.trimmed();
        if (const auto it = m_ids.constFind(id); it != m_ids.cend())
            return *it;
        if (m_d.participants.size() >= limits::MaxParticipants) {
            error = makeError(Code::TooLarge, m_line, u"participants"_s);
            return -1;
        }
        SeqParticipant p;
        p.id = id;
        p.label = capLength(id);
        p.line = m_line;
        p.box = m_box;
        m_d.participants.append(p);
        m_ids.insert(id, int(m_d.participants.size() - 1));
        return int(m_d.participants.size() - 1);
    }

    std::optional<Error> event(SeqEvent e)
    {
        if (e.type == Type::Message || e.type == Type::Note) {
            if (++m_messages > limits::MaxMessages)
                return makeError(Code::TooLarge, m_line, u"messages"_s);
        }
        e.line = m_line;
        m_d.events.append(std::move(e));
        return std::nullopt;
    }

    std::optional<Error> statement(const Statement &st)
    {
        m_line = st.line;
        const QStringView s = st.text;
        std::optional<Error> error;
        // participant A / actor A as Alice / create participant A
        QStringView decl = s;
        if (startsWithWord(decl, u"create"))
            decl = decl.sliced(6).trimmed();
        const bool isActor = startsWithWord(decl, u"actor");
        if (isActor || startsWithWord(decl, u"participant")) {
            QStringView rest = decl.sliced(isActor ? 5 : 11).trimmed();
            // Mermaid 11's `participant A@{ "type": "database" }`: the metadata is not drawn.
            if (const qsizetype at = rest.indexOf(u"@{"); at >= 0)
                rest = rest.first(at).trimmed();
            static const QRegularExpression as(u"^(.+?)\\s+as\\s+(.+)$"_s);
            QString id = rest.toString(), label;
            if (const auto m = as.matchView(rest); m.hasMatch()) {
                id = m.captured(1);
                label = cleanLabel(m.capturedView(2));
            }
            if (id.isEmpty())
                return makeError(Code::Syntax, m_line, excerpt(s));
            const int p = participant(id, error);
            if (p < 0)
                return error;
            if (!label.isEmpty())
                m_d.participants[p].label = label;
            m_d.participants[p].actor = isActor;
            return std::nullopt;
        }
        if (startsWithWord(s, u"destroy") || startsWithWord(s, u"links") || startsWithWord(s, u"link")
            || startsWithWord(s, u"properties") || startsWithWord(s, u"details") || startsWithWord(s, u"accDescr")
            || startsWithWord(s, u"click"))
            return std::nullopt;
        if (startsWithWord(s, u"accTitle")) {
            if (m_d.title.isEmpty())
                m_d.title = cleanLabel(s.sliced(s.indexOf(u':') + 1));
            return std::nullopt;
        }
        if (startsWithWord(s, u"title")) {
            QStringView t = s.sliced(5).trimmed();
            if (t.startsWith(u':'))
                t = t.sliced(1);
            m_d.title = cleanLabel(t);
            return std::nullopt;
        }
        if (startsWithWord(s, u"autonumber")) {
            const QList<QStringView> args = s.sliced(10).trimmed().split(u' ', Qt::SkipEmptyParts);
            if (!args.isEmpty() && args.front() == u"off") {
                m_number = 0;
                return std::nullopt;
            }
            m_number = std::clamp(args.value(0).toInt(), 1, 1000000);
            if (args.isEmpty() || args.front().toInt() <= 0)
                m_number = 1;
            m_step = args.size() > 1 ? std::clamp(args[1].toInt(), 1, 1000) : 1;
            return std::nullopt;
        }
        if (startsWithWord(s, u"activate") || startsWithWord(s, u"deactivate")) {
            const bool on = s.startsWith(u"activate");
            const int p = participant(s.sliced(on ? 8 : 10).toString(), error);
            if (p < 0)
                return error;
            SeqEvent e;
            e.type = on ? Type::Activate : Type::Deactivate;
            e.from = p;
            return event(e);
        }
        if (startsWithWord(s, u"note", Qt::CaseInsensitive))
            return note(s.sliced(4).trimmed());
        if (startsWithWord(s, u"box")) {
            if (m_box >= 0 || !m_open.isEmpty())
                return makeError(Code::Syntax, m_line, excerpt(s));
            QStringView rest = s.sliced(3).trimmed();
            SeqBox b;
            b.line = m_line;
            // An optional colour first: a name, #hex, rgb(…) / rgba(…).
            qsizetype end = rest.indexOf(u' ');
            if (rest.startsWith(u"rgb"))
                end = rest.indexOf(u')') + 1;
            const QStringView first = end > 0 ? rest.first(end) : rest;
            if (const QString c = parseCssColor(first); !c.isEmpty()) {
                b.color = c;
                rest = end > 0 ? rest.sliced(end).trimmed() : QStringView();
            }
            b.title = cleanLabel(rest);
            m_d.boxes.append(b);
            m_box = int(m_d.boxes.size() - 1);
            m_open.append({u"box"_s, m_line});
            return std::nullopt;
        }
        static const QStringList starts = {u"loop"_s,     u"alt"_s,   u"opt"_s, u"par"_s,
                                           u"critical"_s, u"break"_s, u"rect"_s};
        static const QStringList sections = {u"else"_s, u"and"_s, u"option"_s};
        for (const QString &kw : starts) {
            if (!startsWithWord(s, kw))
                continue;
            if (m_open.size() >= limits::MaxNesting)
                return makeError(Code::TooLarge, m_line, u"nesting"_s);
            SeqEvent e;
            e.type = Type::BlockStart;
            e.keyword = kw;
            const QStringView rest = s.sliced(kw.size()).trimmed();
            if (kw == u"rect")
                e.color = parseCssColor(rest);
            else
                e.text = cleanLabel(rest);
            m_open.append({kw, m_line});
            return event(e);
        }
        for (const QString &kw : sections) {
            if (!startsWithWord(s, kw))
                continue;
            if (m_open.isEmpty() || m_open.last().first == u"box")
                return makeError(Code::Syntax, m_line, excerpt(s));
            SeqEvent e;
            e.type = Type::BlockSection;
            e.keyword = kw;
            e.text = cleanLabel(s.sliced(kw.size()));
            return event(e);
        }
        if (s == u"end") {
            if (m_open.isEmpty())
                return makeError(Code::UnexpectedEnd, m_line);
            const QString kw = m_open.takeLast().first;
            if (kw == u"box") {
                m_box = -1;
                return std::nullopt;
            }
            SeqEvent e;
            e.type = Type::BlockEnd;
            return event(e);
        }
        return message(s);
    }

    std::optional<Error> note(QStringView rest)
    {
        static const QRegularExpression re(u"^(left of|right of|over)\\s+([^:]+?)\\s*:(.*)$"_s,
                                           QRegularExpression::CaseInsensitiveOption);
        const auto m = re.matchView(rest);
        if (!m.hasMatch())
            return makeError(Code::Syntax, m_line, excerpt(rest));
        std::optional<Error> error;
        SeqEvent e;
        e.type = Type::Note;
        const QString place = m.captured(1).toLower();
        e.place = place == u"left of"    ? SeqEvent::NotePlace::LeftOf
                  : place == u"right of" ? SeqEvent::NotePlace::RightOf
                                         : SeqEvent::NotePlace::Over;
        const QList<QStringView> who = m.capturedView(2).split(u',');
        if (who.size() > 2 || (who.size() == 2 && e.place != SeqEvent::NotePlace::Over))
            return makeError(Code::Syntax, m_line, excerpt(rest));
        e.from = participant(who.front().toString(), error);
        if (e.from < 0)
            return error;
        e.to = who.size() == 2 ? participant(who.back().toString(), error) : e.from;
        if (e.to < 0)
            return error;
        e.text = cleanLabel(m.capturedView(3));
        return event(e);
    }

    std::optional<Error> message(QStringView s)
    {
        static const QRegularExpression re(
                u"^([^\\-<>:,;+][^<>:,;]*?)\\s*(<<-->>|<<->>|-->>|->>|--x|-x|--\\)|-\\)|-->|->)\\s*([+-]?)\\s*"
                u"([^\\-<>:,;+][^<>:,;]*?)\\s*(?::(.*))?$"_s);
        const auto m = re.matchView(s);
        if (!m.hasMatch())
            return makeError(Code::Syntax, m_line, excerpt(s));
        std::optional<Error> error;
        SeqEvent e;
        e.type = Type::Message;
        e.from = participant(m.captured(1), error);
        if (e.from < 0)
            return error;
        e.to = participant(m.captured(4), error);
        if (e.to < 0)
            return error;
        const QString arrow = m.captured(2);
        e.dotted = arrow.contains(u"--");
        if (arrow.startsWith(u"<<")) {
            e.startHead = Head::Arrow;
            e.endHead = Head::Arrow;
        } else if (arrow.endsWith(u">>")) {
            e.endHead = Head::Arrow;
        } else if (arrow.endsWith(u'x')) {
            e.endHead = Head::Cross;
        } else if (arrow.endsWith(u')')) {
            e.endHead = Head::Open;
        }
        e.activateTarget = m.captured(3) == u"+";
        e.deactivateSource = m.captured(3) == u"-";
        e.text = cleanLabel(m.capturedView(5));
        if (m_number > 0) {
            e.number = m_number;
            m_number = std::min(m_number + m_step, 1000000000);
        }
        return event(e);
    }

    SequenceDiagram m_d;
    QHash<QString, int> m_ids;
    QList<std::pair<QString, int>> m_open; ///< blocks and the box being read: keyword, line
    int m_box = -1;
    int m_line = 0;
    int m_messages = 0;
    int m_number = 0;
    int m_step = 1;
};

void translate(QList<Item> &items, const QPointF &d)
{
    for (Item &it : items) {
        it.box.translate(d);
        for (PathOp &op : it.path) {
            op.p += d;
            op.c1 += d;
            op.c2 += d;
        }
    }
}

} // namespace

Outcome<SequenceDiagram> parseSequence(QStringView source)
{
    return Parser().run(source);
}

Scene drawSequence(const SequenceDiagram &d, const Metrics &m)
{
    Scene scene;
    scene.title = d.title;
    const qreal fs = m.fontSize;
    const int P = int(d.participants.size());
    const qreal textMax = fs * 16;

    // Participants: their boxes and the room the messages and notes between them need.
    QList<TextBlock> names;
    QList<qreal> boxW;
    qreal boxH = 0;
    for (const SeqParticipant &p : d.participants) {
        names.append(layoutText(m, p.label, fs, false, fs * 11));
        boxW.append(std::max(names.last().size.width() + 2 * fs, fs * 6.5));
        boxH = std::max(boxH, names.last().size.height() + fs * 1.3);
    }
    const qreal figureH = fs * 2.6; // an actor's stick figure above its name
    bool anyActor = false;
    for (const SeqParticipant &p : d.participants)
        anyActor = anyActor || p.actor;
    const qreal headH = anyActor ? std::max(boxH, figureH + boxH - fs * 0.6) : boxH;
    QList<qreal> gap(std::max(0, P - 1));
    for (int i = 0; i + 1 < P; ++i)
        gap[i] = (boxW[i] + boxW[i + 1]) / 2 + fs * 2;
    struct Need {
        int a, b;
        qreal width;
    };
    QList<Need> needs;
    QList<TextBlock> texts(d.events.size());
    for (qsizetype k = 0; k < d.events.size(); ++k) {
        const SeqEvent &e = d.events[k];
        if (e.type == Type::Message) {
            texts[k] = layoutText(m, e.text, fs * 0.95, false, textMax);
            const qreal w = texts[k].size.width() + (e.number > 0 ? fs * 1.6 : 0);
            if (e.from == e.to)
                needs.append({e.from, e.from + 1, w + fs * 3});
            else
                needs.append({std::min(e.from, e.to), std::max(e.from, e.to), w + fs * 2});
        } else if (e.type == Type::Note) {
            texts[k] = layoutText(m, e.text, fs * 0.92, false, textMax);
            const qreal w = texts[k].size.width() + fs * 1.4;
            if (e.place == SeqEvent::NotePlace::RightOf)
                needs.append({e.from, e.from + 1, w + boxW.value(e.from + 1) / 2 + fs});
            else if (e.place == SeqEvent::NotePlace::LeftOf)
                needs.append({e.from - 1, e.from, w + boxW.value(e.from - 1) / 2 + fs});
            else if (e.from == e.to) {
                needs.append({e.from - 1, e.from, w / 2 + boxW.value(e.from - 1) / 2 + fs * 0.5});
                needs.append({e.from, e.from + 1, w / 2 + boxW.value(e.from + 1) / 2 + fs * 0.5});
            } else {
                needs.append({std::min(e.from, e.to), std::max(e.from, e.to), w - fs * 3});
            }
        } else if (e.type == Type::BlockStart || e.type == Type::BlockSection) {
            texts[k] = layoutText(m, e.text.isEmpty() ? QString() : u'[' + e.text + u']', fs * 0.9, false, textMax);
        }
    }
    std::stable_sort(needs.begin(), needs.end(), [](const Need &x, const Need &y) { return x.b - x.a < y.b - y.a; });
    for (const Need &n : std::as_const(needs)) {
        if (n.a < 0 || n.b >= P || n.a >= n.b)
            continue;
        qreal have = 0;
        for (int i = n.a; i < n.b; ++i)
            have += gap[i];
        if (have < n.width)
            for (int i = n.a; i < n.b; ++i)
                gap[i] += (n.width - have) / (n.b - n.a);
    }
    QList<qreal> cx(P, 0);
    for (int i = 0; i < P; ++i)
        cx[i] = i == 0 ? boxW[0] / 2 : cx[i - 1] + gap[i - 1];

    // Top: the title, room for box titles, the participants.
    QList<Item> back, middle, front;
    qreal y = 0;
    TextBlock title;
    if (!d.title.isEmpty()) {
        title = layoutText(m, d.title, fs * 1.15, true, fs * 40);
        y += title.size.height() + 10;
    }
    const qreal titleY = 0;
    QList<TextBlock> boxTitles;
    qreal boxTitleH = 0;
    for (const SeqBox &b : d.boxes) {
        boxTitles.append(layoutText(m, b.title, fs * 0.95, true, fs * 20));
        if (!b.title.isEmpty())
            boxTitleH = std::max(boxTitleH, boxTitles.last().size.height() + 6);
    }
    const qreal boxTop = y;
    if (!d.boxes.isEmpty())
        y += 8 + boxTitleH;
    const qreal actorsTop = y;
    y += headH + fs;

    // The events, top to bottom.
    struct Open {
        int start; ///< the event
        qreal top;
        int minP, maxP;
        QList<std::pair<qreal, int>> sections; ///< y, event
    };
    QList<Open> open;
    QList<QList<qreal>> active(P); // activation starts
    const auto touch = [&](int p) {
        for (Open &o : open) {
            o.minP = std::min(o.minP, p);
            o.maxP = std::max(o.maxP, p);
        }
    };
    const qreal actW = 10;
    const auto activationEdge = [&](int p, bool rightSide) {
        const qsizetype n = active[p].size();
        if (n == 0)
            return cx[p];
        const qreal center = cx[p] + (n - 1) * actW / 2;
        return rightSide ? center + actW / 2 : center - actW / 2;
    };
    const auto closeActivation = [&](int p, qreal at) {
        if (active[p].isEmpty())
            return;
        const qreal top = active[p].takeLast();
        const qreal center = cx[p] + active[p].size() * actW / 2;
        middle.append(pathItem(rectPath(QRectF(center - actW / 2, top, actW, std::max<qreal>(at - top, 8))),
                               Paint{Role::ActivationFill, {}}, Paint{Role::ActivationStroke, {}}, 1));
    };
    QList<Item> rectFills; // `rect` blocks, under everything
    for (qsizetype k = 0; k < d.events.size(); ++k) {
        const SeqEvent &e = d.events[k];
        const TextBlock &t = texts[k];
        switch (e.type) {
        case Type::Message: {
            touch(e.from);
            touch(e.to);
            const Paint color{Role::Line, {}};
            const Dash dash = e.dotted ? Dash::Dashed : Dash::Solid;
            constexpr qreal width = 1.4;
            if (e.from == e.to) {
                const qreal x0 = activationEdge(e.from, true);
                const qreal textX = x0 + (e.number > 0 ? fs * 1.6 : 6);
                if (!e.text.isEmpty())
                    front.append(textItem(t, QRectF(textX, y, t.size.width(), t.size.height()),
                                          Paint{Role::LineText, {}}, Qt::AlignLeft));
                y += t.size.height() + 4;
                const qreal loopW = fs * 2.4, loopH = fs * 1.6;
                const QPointF tip(x0, y + loopH);
                QList<PathOp> p;
                p.append({PathOp::Type::Move, QPointF(x0, y), {}, {}});
                p.append({PathOp::Type::Line, QPointF(x0 + loopW, y), {}, {}});
                p.append({PathOp::Type::Line, QPointF(x0 + loopW, y + loopH), {}, {}});
                p.append({PathOp::Type::Line, tip + QPointF(headInset(e.endHead, width), 0), {}, {}});
                middle.append(pathItem(p, Paint{}, color, width, dash));
                addHead(middle, e.endHead, tip, QPointF(-1, 0), color, width);
                if (e.number > 0) {
                    const TextBlock n = layoutText(m, QString::number(e.number), fs * 0.72, true, 0);
                    const qreal r = std::max(n.size.width() / 2 + 3, fs * 0.62);
                    middle.append(pathItem(ellipsePath(QPointF(x0, y), r, r), Paint{Role::NumberFill, {}}, Paint{}, 0));
                    middle.append(textItem(n, QRectF(x0 - r, y - n.size.height() / 2, 2 * r, n.size.height()),
                                           Paint{Role::NumberText, {}}));
                }
                y += loopH + fs * 0.9;
            } else {
                const bool right = e.to > e.from;
                const qreal x0 = activationEdge(e.from, right);
                if (e.activateTarget)
                    active[e.to].append(y + t.size.height() + 4);
                const qreal x1 = activationEdge(e.to, !right);
                const qreal mid = (x0 + x1) / 2;
                if (!e.text.isEmpty())
                    front.append(textItem(t, QRectF(mid - t.size.width() / 2, y, t.size.width(), t.size.height()),
                                          Paint{Role::LineText, {}}));
                y += t.size.height() + 4;
                const qreal dir = right ? 1 : -1;
                const QPointF a(x0, y), b(x1, y);
                const QPointF la = a + QPointF(dir * headInset(e.startHead, width), 0);
                const QPointF lb = b - QPointF(dir * headInset(e.endHead, width), 0);
                middle.append(pathItem({{PathOp::Type::Move, la, {}, {}}, {PathOp::Type::Line, lb, {}, {}}}, Paint{},
                                       color, width, dash));
                addHead(middle, e.endHead, b, QPointF(dir, 0), color, width);
                addHead(middle, e.startHead, a, QPointF(-dir, 0), color, width);
                if (e.number > 0) {
                    const TextBlock n = layoutText(m, QString::number(e.number), fs * 0.72, true, 0);
                    const qreal r = std::max(n.size.width() / 2 + 3, fs * 0.62);
                    middle.append(pathItem(ellipsePath(a, r, r), Paint{Role::NumberFill, {}}, Paint{}, 0));
                    middle.append(textItem(n, QRectF(a.x() - r, a.y() - n.size.height() / 2, 2 * r, n.size.height()),
                                           Paint{Role::NumberText, {}}));
                }
                y += fs * 1.1;
            }
            if (e.deactivateSource)
                closeActivation(e.from, y - fs * 1.1);
            break;
        }
        case Type::Note: {
            touch(e.from);
            touch(e.to);
            const qreal w = t.size.width() + fs * 1.4, h = t.size.height() + fs * 0.9;
            qreal left = 0, nw = w;
            switch (e.place) {
            case SeqEvent::NotePlace::RightOf: left = activationEdge(e.from, true) + fs * 0.6; break;
            case SeqEvent::NotePlace::LeftOf: left = activationEdge(e.from, false) - fs * 0.6 - w; break;
            case SeqEvent::NotePlace::Over: {
                const qreal a = std::min(cx[e.from], cx[e.to]) - (e.from == e.to ? 0 : fs * 1.5);
                const qreal b = std::max(cx[e.from], cx[e.to]) + (e.from == e.to ? 0 : fs * 1.5);
                nw = std::max(w, b - a);
                left = (a + b) / 2 - nw / 2;
                break;
            }
            }
            const QRectF r(left, y + 4, nw, h);
            front.append(pathItem(rectPath(r), Paint{Role::NoteFill, {}}, Paint{Role::NoteStroke, {}}, 1));
            front.append(textItem(t, r, Paint{Role::NoteText, {}}));
            y += h + fs * 1.0;
            break;
        }
        case Type::Activate: active[e.from].append(y - fs * 0.4); break;
        case Type::Deactivate: closeActivation(e.from, y - fs * 0.4); break;
        case Type::BlockStart:
            open.append({int(k), y, std::numeric_limits<int>::max(), -1, {}});
            y += std::max(t.size.height(), fs * 1.3) + fs * 0.9;
            break;
        case Type::BlockSection:
            if (!open.isEmpty()) {
                open.last().sections.append({y, int(k)});
                y += std::max(t.size.height(), fs) + fs * 0.6;
            }
            break;
        case Type::BlockEnd: {
            if (open.isEmpty())
                break;
            const Open o = open.takeLast();
            const SeqEvent &start = d.events[o.start];
            const int depth = int(open.size());
            const int a = o.maxP < 0 ? 0 : o.minP, b = o.maxP < 0 ? P - 1 : o.maxP;
            const qreal left = (P > 0 ? cx[std::max(0, a)] - boxW[std::max(0, a)] / 2 : 0) + depth * 5 - fs * 0.4;
            const qreal right =
                    (P > 0 ? cx[std::min(P - 1, b)] + boxW[std::min(P - 1, b)] / 2 : 0) - depth * 5 + fs * 0.4;
            const QRectF r(left, o.top, std::max(right - left, fs * 6), y - o.top + 2);
            if (start.keyword == u"rect") {
                rectFills.append(pathItem(rectPath(r),
                                          Paint{start.color.isEmpty() ? Role::FrameLabelFill : Role::None, start.color},
                                          Paint{}, 0));
                y += fs * 0.6;
                break;
            }
            front.append(pathItem(rectPath(r), Paint{}, Paint{Role::FrameStroke, {}}, 1));
            // The keyword in a tab at the top left, the condition next to it.
            const TextBlock kw = layoutText(m, start.keyword, fs * 0.85, true, 0);
            const qreal tabW = kw.size.width() + fs * 1.2, tabH = kw.size.height() + 4;
            const QPointF tl = r.topLeft();
            front.append(pathItem(polygonPath({tl, tl + QPointF(tabW, 0), tl + QPointF(tabW, tabH - 5),
                                               tl + QPointF(tabW - 6, tabH), tl + QPointF(0, tabH)}),
                                  Paint{Role::FrameLabelFill, {}}, Paint{Role::FrameStroke, {}}, 1));
            front.append(
                    textItem(kw, QRectF(tl.x(), tl.y() + 2, tabW - 2, kw.size.height()), Paint{Role::FrameText, {}}));
            const TextBlock &cond = texts[o.start];
            if (!start.text.isEmpty())
                front.append(textItem(
                        cond, QRectF(tl.x() + tabW + fs * 0.5, tl.y() + 2, cond.size.width(), cond.size.height()),
                        Paint{Role::FrameText, {}}, Qt::AlignLeft));
            for (const auto &[sy, ev] : o.sections) {
                front.append(pathItem({{PathOp::Type::Move, QPointF(r.left(), sy), {}, {}},
                                       {PathOp::Type::Line, QPointF(r.right(), sy), {}, {}}},
                                      Paint{}, Paint{Role::FrameStroke, {}}, 1, Dash::Dashed));
                const TextBlock &st = texts[ev];
                if (!d.events[ev].text.isEmpty())
                    front.append(textItem(st, QRectF(r.left(), sy + 3, r.width(), st.size.height()),
                                          Paint{Role::FrameText, {}}));
            }
            y += fs * 0.9;
            break;
        }
        }
    }
    y += fs * 0.4;
    for (int p = 0; p < P; ++p)
        while (!active[p].isEmpty())
            closeActivation(p, y);
    const qreal bottomTop = y + fs * 0.4;
    const qreal bottom = bottomTop + headH;

    // Participants (top and bottom) and their lifelines.
    const auto drawParticipant = [&](int i, qreal top, bool atTop) {
        const SeqParticipant &p = d.participants[i];
        const TextBlock &n = names[i];
        if (!p.actor) {
            const QRectF r(cx[i] - boxW[i] / 2, top + headH - boxH, boxW[i], boxH);
            front.append(
                    pathItem(roundedRectPath(r, 3), Paint{Role::ActorFill, {}}, Paint{Role::ActorStroke, {}}, 1.2));
            front.append(textItem(n, r, Paint{Role::ActorText, {}}));
            return;
        }
        // A stick figure, its name under it (at the top) or over it (at the bottom).
        const qreal nameH = n.size.height() + 4;
        const qreal figTop = atTop ? top + headH - nameH - figureH : top + nameH;
        const qreal hr = figureH * 0.16;
        const QPointF headC(cx[i], figTop + hr);
        const qreal neck = figTop + 2 * hr, hip = figTop + figureH * 0.68, foot = figTop + figureH;
        QList<PathOp> body;
        body.append(ellipsePath(headC, hr, hr));
        const auto line = [&](QPointF a, QPointF b) {
            body.append({PathOp::Type::Move, a, {}, {}});
            body.append({PathOp::Type::Line, b, {}, {}});
        };
        line(QPointF(cx[i], neck), QPointF(cx[i], hip));
        line(QPointF(cx[i] - figureH * 0.3, neck + figureH * 0.16),
             QPointF(cx[i] + figureH * 0.3, neck + figureH * 0.16));
        line(QPointF(cx[i], hip), QPointF(cx[i] - figureH * 0.24, foot));
        line(QPointF(cx[i], hip), QPointF(cx[i] + figureH * 0.24, foot));
        front.append(pathItem(body, Paint{Role::ActorFill, {}}, Paint{Role::ActorStroke, {}}, 1.4));
        const qreal ny = atTop ? top + headH - nameH + 2 : top;
        front.append(
                textItem(n, QRectF(cx[i] - boxW[i] / 2, ny, boxW[i], n.size.height()), Paint{Role::ActorText, {}}));
    };
    for (int i = 0; i < P; ++i) {
        back.append(pathItem({{PathOp::Type::Move, QPointF(cx[i], actorsTop + headH), {}, {}},
                              {PathOp::Type::Line, QPointF(cx[i], bottomTop), {}, {}}},
                             Paint{}, Paint{Role::Lifeline, {}}, 1, Dash::Dashed));
        drawParticipant(i, actorsTop, true);
        drawParticipant(i, bottomTop, false);
    }
    // Boxes around their participants, under everything.
    QList<Item> boxes;
    for (qsizetype b = 0; b < d.boxes.size(); ++b) {
        qreal left = std::numeric_limits<qreal>::max(), right = std::numeric_limits<qreal>::lowest();
        for (int i = 0; i < P; ++i) {
            if (d.participants[i].box != b)
                continue;
            left = std::min(left, cx[i] - boxW[i] / 2 - fs * 0.6);
            right = std::max(right, cx[i] + boxW[i] / 2 + fs * 0.6);
        }
        if (left > right)
            continue;
        const QRectF r(left, boxTop, right - left, bottom + 8 - boxTop);
        const Paint fill =
                d.boxes[b].color.isEmpty() ? Paint{Role::ClusterFill, {}} : Paint{Role::None, d.boxes[b].color};
        boxes.append(pathItem(rectPath(r), fill, Paint{Role::ClusterStroke, {}}, 1));
        if (!d.boxes[b].title.isEmpty())
            boxes.append(textItem(boxTitles[b], QRectF(r.left(), boxTop + 4, r.width(), boxTitles[b].size.height()),
                                  d.boxes[b].color.isEmpty()
                                          ? Paint{Role::ClusterText, {}}
                                          : Paint{Role::None, textOn(d.boxes[b].color, u"#1f2430"_s, u"#f3f1ec"_s)}));
    }

    QList<Item> &items = scene.items;
    items = boxes + rectFills + back + middle + front;
    // Everything inside the scene, a margin around it.
    qreal minX = std::numeric_limits<qreal>::max(), maxX = std::numeric_limits<qreal>::lowest();
    qreal maxY = bottom + (d.boxes.isEmpty() ? 0 : 8);
    for (const Item &it : std::as_const(items)) {
        if (it.type == Item::Type::Text) {
            minX = std::min(minX, it.box.left());
            maxX = std::max(maxX, it.box.right());
            maxY = std::max(maxY, it.box.bottom());
        }
        for (const PathOp &op : it.path) {
            if (op.type == PathOp::Type::Close)
                continue;
            minX = std::min(minX, op.p.x());
            maxX = std::max(maxX, op.p.x());
            maxY = std::max(maxY, op.p.y());
        }
    }
    if (minX > maxX)
        minX = maxX = 0;
    constexpr qreal margin = 8;
    qreal width = maxX - minX + 2 * margin;
    if (!d.title.isEmpty())
        width = std::max(width, title.size.width() + 2 * margin);
    translate(items, QPointF(margin - minX + (width - 2 * margin - (maxX - minX)) / 2, margin));
    if (!d.title.isEmpty())
        items.prepend(textItem(title, QRectF(0, margin + titleY, width, title.size.height()), Paint{Role::Title, {}}));
    scene.size = QSizeF(width, maxY + 2 * margin);
    return scene;
}

} // namespace brulion::diagram::detail
