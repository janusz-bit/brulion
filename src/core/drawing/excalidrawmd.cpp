#include "drawing/excalidrawmd.h"

#include "drawing/lzstring.h"

#include <QHash>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

namespace {

/// Lines of the compressed block (the plugin's compress(): 256 characters, an empty line between).
constexpr qsizetype CompressedLine = 256;

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/// The length of the line break at `i` ("\r\n" 2, "\n" 1, none 0).
qsizetype eolAt(QByteArrayView b, qsizetype i)
{
    if (i < b.size() && b.at(i) == '\n')
        return 1;
    if (i + 1 < b.size() && b.at(i) == '\r' && b.at(i + 1) == '\n')
        return 2;
    return 0;
}

bool atLineStart(QByteArrayView b, qsizetype i)
{
    return i == 0 || b.at(i - 1) == '\n';
}

/// The end of the line at `i` (its line break not included; "\r\n" without the "\r").
qsizetype lineEnd(QByteArrayView b, qsizetype i)
{
    qsizetype end = b.indexOf('\n', i);
    if (end < 0)
        end = b.size();
    if (end > i && b.at(end - 1) == '\r')
        --end;
    return end;
}

/// The line at `i` is `text` and nothing more (a line break or the end after it).
bool lineIs(QByteArrayView b, qsizetype i, QByteArrayView text)
{
    return b.sliced(i).startsWith(text) && (i + text.size() == b.size() || eolAt(b, i + text.size()) > 0);
}

/// The first line from `from` on (at a line start) that is `text` itself; −1 none.
qsizetype findLine(QByteArrayView b, QByteArrayView text, qsizetype from, qsizetype to)
{
    for (qsizetype i = b.indexOf(text, from); i >= 0 && i < to; i = b.indexOf(text, i + 1)) {
        if (atLineStart(b, i) && lineIs(b, i, text))
            return i;
    }
    return -1;
}

/// A "# Drawing" or "## Drawing" line from `from` on, after a line break (as the plugin's
/// `\n##? Drawing\n`); −1 none. `next` gets the start of the line after it.
qsizetype findDrawingHeading(QByteArrayView b, qsizetype from, qsizetype *next)
{
    for (qsizetype i = b.indexOf("# Drawing", from); i >= 0; i = b.indexOf("# Drawing", i + 1)) {
        qsizetype start = i;
        if (start > 0 && b.at(start - 1) == '#')
            --start; // "## Drawing"
        if (start == 0 || b.at(start - 1) != '\n')
            continue;
        const qsizetype after = i + qsizetype(sizeof("# Drawing") - 1);
        const qsizetype eol = eolAt(b, after);
        if (eol == 0)
            continue;
        *next = after + eol;
        return start;
    }
    return -1;
}

/// The file's frontmatter (from its start, after a BOM) holds the plugin's key at the start of a
/// line; `head` may end inside the frontmatter.
bool frontmatterHasKey(QByteArrayView head)
{
    qsizetype i = head.startsWith("\xEF\xBB\xBF") ? 3 : 0;
    if (!head.sliced(i).startsWith("---"))
        return false;
    i += 3;
    while (i < head.size() && (head.at(i) == ' ' || head.at(i) == '\t'))
        ++i;
    const qsizetype first = eolAt(head, i);
    if (first == 0)
        return false;
    i += first;
    constexpr QByteArrayView key = "excalidraw-plugin";
    while (i < head.size()) {
        const qsizetype end = lineEnd(head, i);
        QByteArrayView line = head.sliced(i, end - i);
        while (!line.isEmpty() && (line.back() == ' ' || line.back() == '\t'))
            line.chop(1);
        if (line == "---" || line == "...")
            return false; // the frontmatter ended without the key
        qsizetype k = -1;
        if (line.startsWith(key))
            k = key.size();
        else if ((line.startsWith("\"") || line.startsWith("'")) && line.sliced(1).startsWith(key)
                 && line.size() > key.size() + 1 && line.at(key.size() + 1) == line.at(0))
            k = key.size() + 2;
        if (k >= 0) {
            while (k < line.size() && (line.at(k) == ' ' || line.at(k) == '\t'))
                ++k;
            if (k < line.size() && line.at(k) == ':')
                return true;
        }
        const qsizetype nl = head.indexOf('\n', end);
        if (nl < 0)
            break;
        i = nl + 1;
    }
    return false;
}

/// A UTF-8 character's length from its lead byte (a stray continuation byte: 1).
qsizetype utf8Length(uchar lead)
{
    if (lead >= 0xF0)
        return 4;
    if (lead >= 0xE0)
        return 3;
    if (lead >= 0xC0)
        return 2;
    return 1;
}

/// The entries of "## Text Elements" in [from, to): the plugin's `\s\^(.{8})[\n]+` — a space, '^',
/// eight characters of a line, line breaks (CRLF too) — each ending the raw text since the last.
QList<ExcalidrawMarkdown::TextEntry> textEntries(QByteArrayView b, qsizetype from, qsizetype to)
{
    QList<ExcalidrawMarkdown::TextEntry> out;
    qsizetype start = from;
    qsizetype i = from;
    while (i + 2 < to) {
        if (!isSpace(b.at(i)) || b.at(i + 1) != '^') {
            ++i;
            continue;
        }
        qsizetype j = i + 2;
        int chars = 0;
        while (chars < 8 && j < to && b.at(j) != '\n' && b.at(j) != '\r') {
            j += utf8Length(uchar(b.at(j)));
            ++chars;
        }
        qsizetype eol = j <= to ? eolAt(b, j) : 0;
        if (chars < 8 || j > to || eol == 0 || j + eol > to) {
            ++i;
            continue;
        }
        const QString id = QString::fromUtf8(b.sliced(i + 2, j - i - 2));
        qsizetype end = j;
        while ((eol = eolAt(b, end)) > 0 && end + eol <= to)
            end += eol;
        out.append({id, start, i, end});
        start = end;
        i = end;
    }
    return out;
}

quint32 fnv1a(QByteArrayView data, quint32 h = 2166136261u)
{
    for (const char c : data) {
        h ^= uchar(c);
        h *= 16777619u;
    }
    return h;
}

} // namespace

bool looksLikeExcalidrawMarkdown(QByteArrayView head)
{
    return frontmatterHasKey(head);
}

bool looksLikeExcalidrawJson(QByteArrayView head)
{
    qsizetype i = head.startsWith("\xEF\xBB\xBF") ? 3 : 0;
    while (i < head.size() && isSpace(head.at(i)))
        ++i;
    return i < head.size() && head.at(i) == '{' && head.indexOf("\"excalidraw\"", i) >= 0;
}

std::optional<ExcalidrawMarkdownRead> readExcalidrawMarkdown(QByteArrayView bytes, ReadError *error)
{
    const auto fail = [error](ReadError why) -> std::optional<ExcalidrawMarkdownRead> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (error)
        *error = ReadError::None;
    if (bytes.size() > MaxSceneBytes || !frontmatterHasKey(bytes))
        return fail(ReadError::NoScene);

    ExcalidrawMarkdownRead read;
    ExcalidrawMarkdown &layout = read.layout;
    layout.bytes = bytes.toByteArray();
    const QByteArrayView b = layout.bytes;

    // The plugin: compressed when "```compressed-json" opens a block anywhere; the first
    // "## Drawing" heading whose first backtick after it opens the block of that kind.
    bool compressed = false;
    for (qsizetype i = b.indexOf("```compressed-json"); i >= 0 && !compressed;
         i = b.indexOf("```compressed-json", i + 1))
        compressed = eolAt(b, i + qsizetype(sizeof("```compressed-json") - 1)) > 0;
    const QByteArrayView fence = compressed ? QByteArrayView("```compressed-json") : QByteArrayView("```json");
    qsizetype heading = -1;
    qsizetype firstHeading = -1;
    qsizetype legacyLine = -1;
    bool found = false;
    for (qsizetype next = 0, h = findDrawingHeading(b, 0, &next); h >= 0; h = findDrawingHeading(b, next, &next)) {
        if (firstHeading < 0) {
            firstHeading = h;
            legacyLine = next;
        }
        const qsizetype tick = b.indexOf('`', next);
        if (tick < 0 || !b.sliced(tick).startsWith(fence))
            continue;
        const qsizetype eol = eolAt(b, tick + fence.size());
        if (eol == 0)
            continue;
        const qsizetype from = tick + fence.size() + eol;
        // The block ends at the first line that starts with "```".
        qsizetype close = -1;
        for (qsizetype c = b.indexOf("```", from); c >= 0; c = b.indexOf("```", c + 1)) {
            if (atLineStart(b, c)) {
                close = c;
                break;
            }
        }
        if (close < 0)
            continue;
        layout.encoding = compressed ? ExcalidrawMarkdown::Encoding::CompressedJson : ExcalidrawMarkdown::Encoding::Json;
        layout.sceneFrom = from;
        layout.sceneTo = close;
        layout.eol = eol == 2 ? QByteArray("\r\n") : QByteArray("\n");
        heading = h;
        found = true;
        break;
    }
    if (!found) {
        // The oldest form: the scene's JSON on the line after the heading.
        if (firstHeading < 0 || legacyLine >= b.size() || b.at(legacyLine) != '{')
            return fail(ReadError::Corrupt);
        layout.encoding = ExcalidrawMarkdown::Encoding::Legacy;
        layout.sceneFrom = legacyLine;
        layout.sceneTo = lineEnd(b, legacyLine);
        layout.eol = eolAt(b, layout.sceneTo) == 2 ? QByteArray("\r\n") : QByteArray("\n");
        heading = firstHeading;
    }

    // The scene.
    QByteArray json;
    const QByteArrayView content = b.sliced(layout.sceneFrom, layout.sceneTo - layout.sceneFrom);
    if (layout.encoding == ExcalidrawMarkdown::Encoding::CompressedJson) {
        for (const char c : content) {
            if (uchar(c) >= 0x80)
                return fail(ReadError::Corrupt);
        }
        const std::optional<QString> scene = lzstring::decompressFromBase64(QString::fromLatin1(content), MaxSceneBytes);
        if (!scene)
            return fail(ReadError::Corrupt);
        json = scene->toUtf8();
    } else {
        // (The plugin: up to the last '}'.)
        const qsizetype last = content.lastIndexOf('}');
        if (last < 0)
            return fail(ReadError::Corrupt);
        json = content.first(last + 1).toByteArray();
    }
    const std::optional<QJsonObject> root = sceneRoot(json);
    if (!root)
        return fail(ReadError::Corrupt);
    for (const QJsonValue &v : root->value(u"elements"_s).toArray()) {
        if (v.toObject().value(u"isDeleted"_s).toBool())
            layout.tombstones.append(v);
        else
            layout.elements.append(v);
    }
    std::optional<Drawing> drawing = sceneFromObject(*root);
    if (!drawing)
        return fail(ReadError::Corrupt);
    read.drawing = std::move(*drawing);

    // "## Text Elements" (or the older "# Text Elements") before the drawing: up to element links,
    // embedded files, the "%%" before the drawing or the drawing's heading.
    qsizetype section = findLine(b, "## Text Elements", 0, heading);
    if (section < 0)
        section = findLine(b, "# Text Elements", 0, heading);
    if (section >= 0) {
        const qsizetype titleEnd = section + (b.at(section + 1) == '#' ? 16 : 15); // the line's end
        const qsizetype from = titleEnd + eolAt(b, titleEnd);
        qsizetype to = heading;
        for (const QByteArrayView end : {QByteArrayView("## Element Links"), QByteArrayView("## Embedded Files"),
                                         QByteArrayView("# Element Links"), QByteArrayView("# Embedded Files")}) {
            const qsizetype at = findLine(b, end, from, to);
            if (at >= 0)
                to = std::min(to, at);
        }
        // "%%" right before the drawing's heading.
        for (qsizetype at = findLine(b, "%%", from, to); at >= 0; at = findLine(b, "%%", at + 1, to)) {
            qsizetype next = 0;
            const qsizetype after = at + 2 + eolAt(b, at + 2);
            if (findDrawingHeading(b, after, &next) == after) {
                to = at;
                break;
            }
        }
        if (from <= to)
            layout.textEntries = textEntries(b, from, to);
    }
    return read;
}

QByteArray writeExcalidrawMarkdown(const ExcalidrawMarkdown &layout, const Drawing &drawing, qint64 now)
{
    Q_ASSERT(layout.encoding != ExcalidrawMarkdown::Encoding::Legacy);
    QJsonObject scene = sceneObject(drawing);
    QJsonArray elements = scene.value(u"elements"_s).toArray();

    QHash<QString, QJsonObject> read;
    read.reserve(layout.elements.size());
    for (const QJsonValue &v : layout.elements) {
        const QJsonObject e = v.toObject();
        const QString id = e.value(u"id"_s).toString();
        if (!id.isEmpty())
            read.insert(id, e);
    }
    // Text elements new or changed here: their text is their raw text too (the plugin reads the
    // section's, then rawText); the entries of the changed ones get it.
    QSet<QString> present;
    present.reserve(elements.size());
    QHash<QString, QString> newText;
    for (qsizetype i = 0; i < elements.size(); ++i) {
        QJsonObject e = elements.at(i).toObject();
        const QString id = e.value(u"id"_s).toString();
        present.insert(id);
        if (e.value(u"type"_s).toString() != u"text")
            continue;
        const auto it = read.constFind(id);
        if (it != read.constEnd() && it->value(u"version"_s).toInteger(1) == e.value(u"version"_s).toInteger(1))
            continue;
        const QJsonValue original = e.value(u"originalText"_s);
        const QString raw = original.isString() ? original.toString() : e.value(u"text"_s).toString();
        e.insert(u"rawText"_s, raw);
        elements.replace(i, e);
        if (it != read.constEnd())
            newText.insert(id, raw);
    }
    // Tombstones: the file's (unless the element is back), then the elements read and gone.
    QSet<QString> buried;
    for (const QJsonValue &v : layout.tombstones) {
        const QString id = v.toObject().value(u"id"_s).toString();
        if (!id.isEmpty() && present.contains(id))
            continue;
        elements.append(v);
        buried.insert(id);
    }
    for (const QJsonValue &v : layout.elements) {
        QJsonObject e = v.toObject();
        const QString id = e.value(u"id"_s).toString();
        if (id.isEmpty() || present.contains(id) || buried.contains(id))
            continue;
        buried.insert(id);
        e.insert(u"isDeleted"_s, true);
        e.insert(u"version"_s, e.value(u"version"_s).toInteger(1) + 1);
        const QByteArray seed = id.toUtf8() + '\n' + QByteArray::number(now);
        e.insert(u"versionNonce"_s, qint64(fnv1a(seed) & 0x7fffffff));
        e.insert(u"updated"_s, now);
        elements.append(e);
    }
    scene.insert(u"elements"_s, elements);

    // The block's content.
    const QByteArray json = stringifyJson(scene, "\t");
    QByteArray block;
    if (layout.encoding == ExcalidrawMarkdown::Encoding::Json) {
        // (JSON's strings hold no line breaks: every one is the layout's.)
        block = layout.eol == "\n" ? json : QByteArray(json).replace("\n", layout.eol);
        block += layout.eol;
    } else {
        const QString b64 = lzstring::compressToBase64(QString::fromUtf8(json));
        block.reserve(b64.size() + b64.size() / CompressedLine * 2 * layout.eol.size() + 4);
        for (qsizetype i = 0; i < b64.size(); i += CompressedLine) {
            if (i > 0)
                block += layout.eol + layout.eol;
            block += QStringView(b64).sliced(i, std::min(CompressedLine, b64.size() - i)).toLatin1();
        }
        block += layout.eol;
    }

    struct Edit {
        qsizetype from = 0;
        qsizetype to = 0;
        QByteArray with;
    };
    QList<Edit> edits;
    for (const ExcalidrawMarkdown::TextEntry &entry : layout.textEntries) {
        if (const auto it = newText.constFind(entry.id); it != newText.constEnd()) {
            // (A text's lines in the file's line breaks.)
            QByteArray raw = it->toUtf8();
            if (layout.eol != "\n")
                raw.replace("\r\n", "\n").replace("\n", layout.eol);
            edits.append({entry.from, entry.to, raw});
        } else if (read.contains(entry.id) && !present.contains(entry.id)) {
            edits.append({entry.from, entry.end, {}});
        }
    }
    edits.append({layout.sceneFrom, layout.sceneTo, block});
    std::stable_sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.from < b.from; });
    QByteArray out;
    out.reserve(layout.bytes.size() - (layout.sceneTo - layout.sceneFrom) + block.size() + 64);
    qsizetype at = 0;
    for (const Edit &edit : std::as_const(edits)) {
        if (edit.from < at || edit.to < edit.from || edit.to > layout.bytes.size())
            continue; // (overlapping entries: never for a layout read from a file)
        out += QByteArrayView(layout.bytes).sliced(at, edit.from - at);
        out += edit.with;
        at = edit.to;
    }
    out += QByteArrayView(layout.bytes).sliced(at);
    return out;
}

QByteArray stringifyJson(const QJsonValue &value, QByteArrayView indent)
{
    QByteArray compact;
    if (value.isObject()) {
        compact = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    } else if (value.isArray()) {
        compact = QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    } else {
        // A scalar: as an array's only item.
        compact = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
        return compact.mid(1, compact.size() - 2);
    }
    QByteArray out;
    out.reserve(compact.size() * 2);
    int depth = 0;
    const auto newLine = [&] {
        out += '\n';
        for (int i = 0; i < depth; ++i)
            out += indent;
    };
    const qsizetype n = compact.size();
    for (qsizetype i = 0; i < n; ++i) {
        const char c = compact.at(i);
        switch (c) {
        case '"': {
            // A string, its escapes as they are.
            const qsizetype start = i;
            for (++i; i < n && compact.at(i) != '"'; ++i) {
                if (compact.at(i) == '\\')
                    ++i;
            }
            out += QByteArrayView(compact).sliced(start, std::min(i, n - 1) - start + 1);
            break;
        }
        case '{':
        case '[': {
            const char close = c == '{' ? '}' : ']';
            out += c;
            if (i + 1 < n && compact.at(i + 1) == close) {
                out += close;
                ++i;
                break;
            }
            ++depth;
            newLine();
            break;
        }
        case '}':
        case ']':
            --depth;
            newLine();
            out += c;
            break;
        case ',':
            out += ',';
            newLine();
            break;
        case ':': out += ": "; break;
        default: out += c;
        }
    }
    return out;
}

} // namespace brulion::drawing
