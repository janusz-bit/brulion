#include "canvas/canvas.h"
#include "canvas/canvasfile_p.h"
#include "canvas/json_p.h"

#include "text/text.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace brulion::canvas {

using namespace detail;

namespace {

// ---- the keys the model knows -----------------------------------------------------------------

enum class FieldKind : quint8 { String, Number };

struct NodeField {
    QStringView key;
    FieldKind kind;
    QString Node::*string = nullptr;
    double Node::*number = nullptr;
};

/// In the order Obsidian writes them (a new key goes after the keys a node has, in this order).
const std::array<NodeField, 14> &nodeFields()
{
    static const std::array<NodeField, 14> fields{{
            {u"id", FieldKind::String, &Node::id, nullptr},
            {u"type", FieldKind::String, &Node::type, nullptr},
            {u"text", FieldKind::String, &Node::text, nullptr},
            {u"file", FieldKind::String, &Node::file, nullptr},
            {u"subpath", FieldKind::String, &Node::subpath, nullptr},
            {u"url", FieldKind::String, &Node::url, nullptr},
            {u"x", FieldKind::Number, nullptr, &Node::x},
            {u"y", FieldKind::Number, nullptr, &Node::y},
            {u"width", FieldKind::Number, nullptr, &Node::width},
            {u"height", FieldKind::Number, nullptr, &Node::height},
            {u"color", FieldKind::String, &Node::color, nullptr},
            {u"label", FieldKind::String, &Node::label, nullptr},
            {u"background", FieldKind::String, &Node::background, nullptr},
            {u"backgroundStyle", FieldKind::String, &Node::backgroundStyle, nullptr},
    }};
    return fields;
}

struct EdgeField {
    QStringView key;
    QString Edge::*string;
};

const std::array<EdgeField, 9> &edgeFields()
{
    static const std::array<EdgeField, 9> fields{{
            {u"id", &Edge::id},
            {u"fromNode", &Edge::fromNode},
            {u"fromSide", &Edge::fromSide},
            {u"fromEnd", &Edge::fromEnd},
            {u"toNode", &Edge::toNode},
            {u"toSide", &Edge::toSide},
            {u"toEnd", &Edge::toEnd},
            {u"color", &Edge::color},
            {u"label", &Edge::label},
    }};
    return fields;
}

/// A key the node must have even when empty (its type's own value).
bool requiredNodeKey(const Node &n, QStringView key)
{
    if (key == u"id" || key == u"type" || key == u"x" || key == u"y" || key == u"width" || key == u"height")
        return true;
    if (key == u"text")
        return n.isText();
    if (key == u"file")
        return n.isFile();
    if (key == u"url")
        return n.isLink();
    return false;
}

bool requiredEdgeKey(QStringView key)
{
    return key == u"id" || key == u"fromNode" || key == u"toNode";
}

/// A string value of the JSON (a number read as its text: ids written as numbers by hand).
QString stringOf(const JsonValue &v, QStringView text)
{
    if (v.isString())
        return v.string;
    if (v.isNumber())
        return text.sliced(v.from, v.to - v.from).toString();
    return {};
}

double numberOf(const JsonValue &v)
{
    return v.isNumber() ? v.number : 0;
}

std::shared_ptr<Origin> originOf(const JsonValue &object, QStringView text)
{
    auto o = std::make_shared<Origin>();
    o->source = text.sliced(object.from, object.to - object.from).toString();
    o->members.reserve(object.members.size());
    for (const JsonMember &m : object.members) {
        Member out;
        out.lead = text.sliced(m.leadFrom, m.keyFrom - m.leadFrom).toString();
        out.keyText = text.sliced(m.keyFrom, m.keyTo - m.keyFrom).toString();
        out.key = m.key;
        out.sep = text.sliced(m.keyTo, m.value->from - m.keyTo).toString();
        out.value = text.sliced(m.value->from, m.value->to - m.value->from).toString();
        o->members.append(std::move(out));
    }
    o->close = text.sliced(object.closeFrom, object.to - 1 - object.closeFrom).toString();
    return o;
}

Node nodeFrom(const JsonValue &object, QStringView text)
{
    Node n;
    for (const NodeField &f : nodeFields()) {
        const JsonMember *m = object.member(f.key);
        if (!m)
            continue;
        if (f.kind == FieldKind::Number)
            n.*(f.number) = numberOf(*m->value);
        else
            n.*(f.string) = stringOf(*m->value, text);
    }
    return n;
}

Edge edgeFrom(const JsonValue &object, QStringView text)
{
    Edge e;
    for (const EdgeField &f : edgeFields()) {
        if (const JsonMember *m = object.member(f.key))
            e.*(f.string) = stringOf(*m->value, text);
    }
    return e;
}

/// An id for an item without one (or with one an earlier item has): from its position and text,
/// so reading the same file twice gives the same ids.
QString madeId(QStringView kind, qsizetype index, QStringView source, const QSet<QString> &taken)
{
    for (int round = 0;; ++round) {
        const QString seed = kind + QString::number(index) + u':' + QString::number(round) + u':' + source;
        QString id = text::fnv1a64Hex(seed.toUtf8());
        if (!taken.contains(id))
            return id;
    }
}

ArraySpan spanOf(const JsonValue &array, QStringView text)
{
    ArraySpan s;
    s.present = true;
    s.from = array.from;
    s.to = array.to;
    for (qsizetype i = 0; i < array.items.size(); ++i) {
        const qsizetype lead = array.itemLeads.at(i);
        s.leads.append(text.sliced(lead, array.items.at(i)->from - lead).toString());
    }
    s.close = text.sliced(array.closeFrom, array.to - 1 - array.closeFrom).toString();
    return s;
}

// ---- writing ------------------------------------------------------------------------------------

/// How the items of an array are written: compact on one line (Obsidian) or one key per line.
struct ItemStyle {
    bool pretty = false;
    QString memberLead; ///< pretty: before each key
    QString close;      ///< pretty: before `}`
    QString sep = u":"_s;
};

/// The style of a file's items: that of its first node or edge (compact when none).
ItemStyle styleOf(const Canvas &c)
{
    const auto from = [](const Origin &o) {
        ItemStyle s;
        if (o.members.isEmpty())
            return s;
        s.sep = o.members.constFirst().sep;
        if (o.source.contains(u'\n')) {
            s.pretty = true;
            s.memberLead = o.members.constFirst().lead;
            s.close = o.close;
        } else if (!o.members.constFirst().lead.isEmpty()) {
            s.memberLead = o.members.constFirst().lead; // `{ "id": …, "x": … }`
            s.close = o.close;
        }
        return s;
    };
    for (const Node &n : c.nodes) {
        if (n.origin)
            return from(*n.origin);
    }
    for (const Edge &e : c.edges) {
        if (e.origin)
            return from(*e.origin);
    }
    return {};
}

struct Written {
    QString lead;
    QString keyText;
    QString sep;
    QString value;
};

/// An object of `members`, joined as JSON with the leads given.
QString joinObject(const QList<Written> &members, const QString &close)
{
    QString out = u"{"_s;
    for (qsizetype i = 0; i < members.size(); ++i) {
        const Written &m = members.at(i);
        if (i > 0)
            out += u',';
        out += m.lead + m.keyText + m.sep + m.value;
    }
    out += close + u'}';
    return out;
}

/// The members of a changed item: those as read with the changed values in place (removed
/// optional ones left out), the new ones after them in the fields' order. `value(key)` gives the
/// item's value of a known key (nullopt: absent), `same(key)` whether it equals what was read.
template<typename Value, typename Same, typename Keys>
QString writeItem(const Origin *origin, const ItemStyle &style, const Keys &keys, const Value &value, const Same &same)
{
    QList<Written> out;
    QSet<QString> written;
    if (origin) {
        // The last occurrence of a key is the one JSON.parse keeps: the one rewritten.
        QHash<QString, qsizetype> last;
        for (qsizetype i = 0; i < origin->members.size(); ++i)
            last.insert(origin->members.at(i).key, i);
        for (qsizetype i = 0; i < origin->members.size(); ++i) {
            const Member &m = origin->members.at(i);
            const bool known = std::ranges::any_of(keys, [&m](QStringView k) { return k == m.key; });
            if (!known || last.value(m.key) != i || same(m.key)) {
                out.append({m.lead, m.keyText, m.sep, m.value});
                if (known)
                    written.insert(m.key);
                continue;
            }
            written.insert(m.key);
            if (const std::optional<QString> v = value(m.key))
                out.append({m.lead, m.keyText, m.sep, *v});
        }
    }
    const QString lead = !out.isEmpty() ? out.constLast().lead : style.memberLead;
    const QString sep = origin && !origin->members.isEmpty() ? origin->members.constFirst().sep : style.sep;
    for (QStringView k : keys) {
        const QString key = k.toString();
        if (written.contains(key))
            continue;
        if (const std::optional<QString> v = value(key))
            out.append({lead, jsonString(key), sep, *v});
    }
    return joinObject(out, origin ? origin->close : style.close);
}

QList<QStringView> nodeKeys()
{
    QList<QStringView> keys;
    for (const NodeField &f : nodeFields())
        keys.append(f.key);
    return keys;
}

QList<QStringView> edgeKeys()
{
    QList<QStringView> keys;
    for (const EdgeField &f : edgeFields())
        keys.append(f.key);
    return keys;
}

const NodeField *nodeField(QStringView key)
{
    for (const NodeField &f : nodeFields()) {
        if (f.key == key)
            return &f;
    }
    return nullptr;
}

const EdgeField *edgeField(QStringView key)
{
    for (const EdgeField &f : edgeFields()) {
        if (f.key == key)
            return &f;
    }
    return nullptr;
}

QString nodeText(const Node &n, const ItemStyle &style)
{
    if (n.origin && !n.changed())
        return n.origin->source;
    static const QList<QStringView> keys = nodeKeys();
    const auto value = [&n](const QString &key) -> std::optional<QString> {
        const NodeField *f = nodeField(key);
        if (!f)
            return std::nullopt;
        if (f->kind == FieldKind::Number)
            return jsonNumber(n.*(f->number));
        const QString &s = n.*(f->string);
        if (s.isEmpty() && !requiredNodeKey(n, key))
            return std::nullopt;
        return jsonString(s);
    };
    const auto same = [&n](const QString &key) {
        const NodeField *f = nodeField(key);
        const Node &was = n.origin->node;
        if (f->kind == FieldKind::Number)
            return n.*(f->number) == was.*(f->number);
        return n.*(f->string) == was.*(f->string);
    };
    return writeItem(n.origin.get(), style, keys, value, same);
}

QString edgeText(const Edge &e, const ItemStyle &style)
{
    if (e.origin && !e.changed())
        return e.origin->source;
    static const QList<QStringView> keys = edgeKeys();
    const auto value = [&e](const QString &key) -> std::optional<QString> {
        const EdgeField *f = edgeField(key);
        if (!f)
            return std::nullopt;
        const QString &s = e.*(f->string);
        if (s.isEmpty() && !requiredEdgeKey(key))
            return std::nullopt;
        return jsonString(s);
    };
    const auto same = [&e](const QString &key) {
        const EdgeField *f = edgeField(key);
        return e.*(f->string) == e.origin->edge.*(f->string);
    };
    return writeItem(e.origin.get(), style, keys, value, same);
}

/// The whitespace a new array's items get, from the root's members' lead ("\n\t" → items at
/// "\n\t\t", closed at "\n\t"; a compact file: none).
std::pair<QString, QString> defaultLeads(const QString &memberLead)
{
    const qsizetype nl = memberLead.lastIndexOf(u'\n');
    if (nl < 0)
        return {QString(), QString()};
    const QString unit = memberLead.mid(nl + 1).isEmpty() ? u"\t"_s : memberLead.mid(nl + 1);
    return {memberLead + unit, memberLead};
}

/// An array of item texts with the whitespace of `span` (a new array: the defaults).
QString writeArray(const QStringList &items, const ArraySpan *span, const QString &memberLead)
{
    const auto [itemLead, itemClose] = defaultLeads(memberLead);
    QString out = u"["_s;
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (i > 0)
            out += u',';
        if (span && i < span->leads.size())
            out += span->leads.at(i);
        else if (span && !span->leads.isEmpty())
            out += span->leads.constLast();
        else
            out += itemLead;
        out += items.at(i);
    }
    if (!items.isEmpty())
        out += span && !span->leads.isEmpty() ? span->close : itemClose;
    out += u']';
    return out;
}

QStringList nodeTexts(const Canvas &c, const ItemStyle &style)
{
    QStringList out;
    out.reserve(c.nodes.size());
    for (const Node &n : c.nodes)
        out.append(nodeText(n, style));
    return out;
}

QStringList edgeTexts(const Canvas &c, const ItemStyle &style)
{
    QStringList out;
    out.reserve(c.edges.size());
    for (const Edge &e : c.edges)
        out.append(edgeText(e, style));
    return out;
}

QString writeNew(const Canvas &c)
{
    const ItemStyle style = styleOf(c);
    const QString lead = u"\n\t"_s;
    return u"{"_s + lead + u"\"nodes\":"_s + writeArray(nodeTexts(c, style), nullptr, lead) + u',' + lead
           + u"\"edges\":"_s + writeArray(edgeTexts(c, style), nullptr, lead) + u"\n}"_s;
}

} // namespace

// ---- Node, Edge, Canvas -------------------------------------------------------------------------

bool Node::sameAs(const Node &o) const
{
    return id == o.id && type == o.type && x == o.x && y == o.y && width == o.width && height == o.height
           && color == o.color && text == o.text && file == o.file && subpath == o.subpath && url == o.url
           && label == o.label && background == o.background && backgroundStyle == o.backgroundStyle;
}

bool Node::changed() const
{
    return !origin || !sameAs(origin->node);
}

bool Edge::sameAs(const Edge &o) const
{
    return id == o.id && fromNode == o.fromNode && toNode == o.toNode && fromSide == o.fromSide && toSide == o.toSide
           && fromEnd == o.fromEnd && toEnd == o.toEnd && color == o.color && label == o.label;
}

bool Edge::changed() const
{
    return !origin || !sameAs(origin->edge);
}

const Node *Canvas::node(QStringView id) const
{
    const qsizetype i = indexOfNode(id);
    return i < 0 ? nullptr : &nodes.at(i);
}

Node *Canvas::node(QStringView id)
{
    const qsizetype i = indexOfNode(id);
    return i < 0 ? nullptr : &nodes[i];
}

qsizetype Canvas::indexOfNode(QStringView id) const
{
    if (id.isEmpty())
        return -1;
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        if (nodes.at(i).id == id)
            return i;
    }
    return -1;
}

const Edge *Canvas::edge(QStringView id) const
{
    const qsizetype i = indexOfEdge(id);
    return i < 0 ? nullptr : &edges.at(i);
}

qsizetype Canvas::indexOfEdge(QStringView id) const
{
    if (id.isEmpty())
        return -1;
    for (qsizetype i = 0; i < edges.size(); ++i) {
        if (edges.at(i).id == id)
            return i;
    }
    return -1;
}

bool Canvas::sameContent(const Canvas &o) const
{
    if (nodes.size() != o.nodes.size() || edges.size() != o.edges.size())
        return false;
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        if (!nodes.at(i).sameAs(o.nodes.at(i)))
            return false;
    }
    for (qsizetype i = 0; i < edges.size(); ++i) {
        if (!edges.at(i).sameAs(o.edges.at(i)))
            return false;
    }
    return true;
}

// ---- reading ------------------------------------------------------------------------------------

ReadResult readCanvas(QStringView text)
{
    ReadResult out;
    if (text.size() > MaxCanvasChars) {
        out.error = ReadError::TooLarge;
        return out;
    }
    QStringView body = text.startsWith(QChar(0xFEFF)) ? text.sliced(1) : text;
    if (body.trimmed().isEmpty()) {
        out.canvas = Canvas();
        return out;
    }
    const JsonDocument doc = parseJson(text);
    if (!doc.root) {
        out.error = doc.error == JsonError::Syntax ? ReadError::NotJson : ReadError::TooLarge;
        return out;
    }
    const JsonValue &root = *doc.root;
    if (!root.isObject()) {
        out.error = ReadError::NotCanvas;
        return out;
    }
    const JsonMember *nodesMember = root.member(u"nodes");
    const JsonMember *edgesMember = root.member(u"edges");
    if ((nodesMember && !nodesMember->value->isArray()) || (edgesMember && !edgesMember->value->isArray())) {
        out.error = ReadError::NotCanvas;
        return out;
    }
    if ((nodesMember && nodesMember->value->items.size() > MaxNodes)
        || (edgesMember && edgesMember->value->items.size() > MaxEdges)) {
        out.error = ReadError::TooLarge;
        return out;
    }
    auto layout = std::make_shared<Layout>();
    layout->text = text.toString();
    layout->rootCloseFrom = root.closeFrom;
    layout->rootHasMembers = !root.members.isEmpty();
    if (!root.members.isEmpty()) {
        const JsonMember &lastMember = root.members.constLast();
        layout->memberLead = text.sliced(lastMember.leadFrom, lastMember.keyFrom - lastMember.leadFrom).toString();
        layout->memberSep = text.sliced(lastMember.keyTo, lastMember.value->from - lastMember.keyTo).toString();
    } else {
        layout->memberSep = u":"_s;
    }
    Canvas c;
    QSet<QString> ids;
    if (nodesMember) {
        const JsonValue &array = *nodesMember->value;
        layout->nodes = spanOf(array, text);
        c.nodes.reserve(array.items.size());
        for (qsizetype i = 0; i < array.items.size(); ++i) {
            const JsonValue &item = *array.items.at(i);
            if (!item.isObject()) {
                out.error = ReadError::NotCanvas;
                return out;
            }
            std::shared_ptr<Origin> origin = originOf(item, text);
            Node n = nodeFrom(item, text);
            origin->node = n;
            if (n.id.isEmpty() || ids.contains(n.id))
                n.id = madeId(u"node", i, origin->source, ids);
            ids.insert(n.id);
            n.origin = std::move(origin);
            c.nodes.append(std::move(n));
        }
    }
    if (edgesMember) {
        const JsonValue &array = *edgesMember->value;
        layout->edges = spanOf(array, text);
        c.edges.reserve(array.items.size());
        QSet<QString> edgeIds;
        for (qsizetype i = 0; i < array.items.size(); ++i) {
            const JsonValue &item = *array.items.at(i);
            if (!item.isObject()) {
                out.error = ReadError::NotCanvas;
                return out;
            }
            std::shared_ptr<Origin> origin = originOf(item, text);
            Edge e = edgeFrom(item, text);
            origin->edge = e;
            if (e.id.isEmpty() || edgeIds.contains(e.id))
                e.id = madeId(u"edge", i, origin->source, edgeIds);
            edgeIds.insert(e.id);
            e.origin = std::move(origin);
            c.edges.append(std::move(e));
        }
    }
    c.layout = std::move(layout);
    out.canvas = std::move(c);
    return out;
}

// ---- writing ------------------------------------------------------------------------------------

QString writeCanvas(const Canvas &c)
{
    if (!c.layout)
        return writeNew(c);
    const Layout &l = *c.layout;
    const ItemStyle style = styleOf(c);
    struct Piece {
        qsizetype from;
        qsizetype to;
        QString text;
    };
    QList<Piece> pieces;
    QString inserted; // members added before the root's `}`
    const auto place = [&](const ArraySpan &span, const QStringList &items, QStringView key) {
        if (span.present) {
            pieces.append({span.from, span.to, writeArray(items, &span, l.memberLead)});
        } else if (!items.isEmpty()) {
            const bool comma = l.rootHasMembers || !inserted.isEmpty();
            const QString lead = l.rootHasMembers ? l.memberLead : u"\n\t"_s;
            inserted += (comma ? u","_s : QString()) + lead + jsonString(key) + l.memberSep
                        + writeArray(items, nullptr, lead);
        }
    };
    place(l.nodes, nodeTexts(c, style), u"nodes");
    place(l.edges, edgeTexts(c, style), u"edges");
    std::ranges::sort(pieces, {}, &Piece::from);
    QString out;
    out.reserve(l.text.size() + 64);
    qsizetype at = 0;
    for (const Piece &p : std::as_const(pieces)) {
        out += QStringView(l.text).sliced(at, p.from - at);
        out += p.text;
        at = p.to;
    }
    if (!inserted.isEmpty()) {
        out += QStringView(l.text).sliced(at, l.rootCloseFrom - at);
        out += inserted;
        // (A root that had no members gets its closing line from the inserted lead.)
        if (!l.rootHasMembers)
            out += u'\n';
        at = l.rootCloseFrom;
    }
    out += QStringView(l.text).sliced(at);
    return out;
}

QString emptyCanvasText()
{
    return writeCanvas(Canvas());
}

QString fragmentText(const Canvas &canvas, const QSet<QString> &nodeIds, const QSet<QString> &edgeIds)
{
    Canvas out;
    for (const Node &n : canvas.nodes) {
        if (nodeIds.contains(n.id))
            out.nodes.append(n);
    }
    for (const Edge &e : canvas.edges) {
        if (edgeIds.contains(e.id) || (nodeIds.contains(e.fromNode) && nodeIds.contains(e.toNode)))
            out.edges.append(e);
    }
    // Compact, without the file's layout (the items as read stay as they were written).
    return writeCanvas(out);
}

std::optional<Canvas> parseFragment(QStringView text)
{
    const QStringView t = text.trimmed();
    if (!t.startsWith(u'{') || !t.endsWith(u'}') || t.size() > MaxCanvasChars)
        return std::nullopt;
    ReadResult r = readCanvas(t);
    if (!r.canvas || r.canvas->nodes.isEmpty())
        return std::nullopt;
    // Only a text that names nodes as a canvas does (a JSON object with `nodes`).
    if (!t.contains(u"\"nodes\""))
        return std::nullopt;
    return std::move(r.canvas);
}

bool isCanvasPath(QStringView path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    const QStringView name = slash < 0 ? path : path.sliced(slash + 1);
    return name.size() > 7 && name.endsWith(u".canvas", Qt::CaseInsensitive);
}

QString canvasTitle(QStringView path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    QStringView name = slash < 0 ? path : path.sliced(slash + 1);
    if (name.size() > 7 && name.endsWith(u".canvas", Qt::CaseInsensitive))
        name.chop(7);
    return name.toString();
}

NodeStyle nodeStyle(const Node &node)
{
    NodeStyle out;
    if (!node.origin)
        return out;
    const auto member = std::ranges::find(node.origin->members, u"styleAttributes"_s, &detail::Member::key);
    if (member == node.origin->members.end())
        return out;
    const QJsonObject style = QJsonDocument::fromJson(member->value.toUtf8()).object();
    const auto known = [&style](const char16_t *key, std::initializer_list<QStringView> values) {
        const QString v = style.value(QStringView(key)).toString();
        return std::ranges::find(values, QStringView(v)) != values.end() ? v : QString();
    };
    out.shape = known(u"shape", {u"pill", u"circle", u"diamond", u"parallelogram", u"predefined-process", u"document",
                                 u"database"});
    out.border = known(u"border", {u"dashed", u"dotted", u"invisible"});
    out.textAlign = known(u"textAlign", {u"center", u"right"});
    return out;
}

} // namespace brulion::canvas
