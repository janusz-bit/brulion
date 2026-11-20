#include "canvas/indextext.h"

#include "markdown/parse.h"

#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::canvas {

namespace {

/// A fence line's character and length ("```", "~~~~"), or {0, 0}.
std::pair<QChar, int> fenceOf(QStringView line)
{
    const QStringView t = line.trimmed();
    if (t.size() < 3 || (t[0] != u'`' && t[0] != u'~'))
        return {QChar(), 0};
    int n = 0;
    while (n < t.size() && t[n] == t[0])
        ++n;
    return n >= 3 ? std::pair{t[0], n} : std::pair{QChar(), 0};
}

/// A text node's Markdown as one piece of the index text: LF, what is left open closed, no blank
/// lines at its ends.
QString sanitized(const QString &text)
{
    QString t = text;
    t.replace(u"\r\n"_s, u"\n"_s);
    t.replace(u'\r', u'\n');
    // Blank lines at the ends go (a card's padding is no paragraph).
    while (t.startsWith(u'\n'))
        t.remove(0, 1);
    while (t.endsWith(u'\n') || t.endsWith(u' ') || t.endsWith(u'\t'))
        t.chop(1);
    if (t.isEmpty())
        return t;
    QChar fenceChar;
    int fenceLength = 0;
    bool math = false;
    const QStringList lines = t.split(u'\n');
    for (const QString &line : lines) {
        const auto [c, n] = fenceOf(line);
        if (fenceLength == 0 && n > 0) {
            fenceChar = c;
            fenceLength = n;
            continue;
        }
        if (fenceLength > 0) {
            if (c == fenceChar && n >= fenceLength && line.trimmed().size() == n)
                fenceLength = 0;
            continue;
        }
        if (line.trimmed() == u"$$")
            math = !math;
    }
    if (fenceLength > 0)
        t += u'\n' + QString(fenceLength, fenceChar);
    else if (math)
        t += u"\n$$"_s;
    const qsizetype open = t.lastIndexOf(u"<!--"_s);
    if (open >= 0 && t.indexOf(u"-->"_s, open + 4) < 0)
        t += u" -->"_s;
    return t;
}

bool wikiSafe(QStringView s)
{
    for (const QChar c : s) {
        if (c == u'[' || c == u']' || c == u'|' || c == u'#' || c == u'^' || c == u'\n' || c == u'\r')
            return false;
    }
    return true;
}

QString urlText(const QString &url)
{
    const QString u = url.trimmed();
    if (u.isEmpty())
        return {};
    const QUrl parsed(u, QUrl::StrictMode);
    // An address with a scheme as an autolink (a link of the canvas); else its text.
    if (parsed.isValid() && !parsed.scheme().isEmpty() && !u.contains(u' ') && !u.contains(u'>'))
        return u'<' + u + u'>';
    return u;
}

} // namespace

QString fileLink(const QString &path, const QString &subpath)
{
    QString sub = subpath.trimmed();
    if (!sub.isEmpty() && !sub.startsWith(u'#'))
        sub.prepend(u'#');
    const QStringView fragment = sub.size() > 1 ? QStringView(sub).sliced(1) : QStringView();
    // (A fragment may hold '#' — a heading path — and '^' for a block: allowed after the first '#'.)
    const bool fragmentSafe = !fragment.contains(u'[') && !fragment.contains(u']') && !fragment.contains(u'|')
                              && !fragment.contains(u'\n');
    if (wikiSafe(path) && fragmentSafe)
        return u"[["_s + path + (fragment.isEmpty() ? QString() : u'#' + fragment.toString()) + u"]]"_s;
    const qsizetype slash = path.lastIndexOf(u'/');
    QString name = slash < 0 ? path : path.mid(slash + 1);
    name.replace(u'[', u'(').replace(u']', u')');
    const QByteArray encoded = QUrl::toPercentEncoding(path, "/");
    return u'[' + name + u"]("_s + QString::fromLatin1(encoded) + u')';
}

IndexText indexText(const Canvas &c)
{
    IndexText out;
    int line = 0;
    const auto add = [&](const QString &piece, const QString &nodeId, const QString &edgeId) {
        if (piece.trimmed().isEmpty())
            return;
        if (!out.text.isEmpty()) {
            out.text += u"\n\n"_s;
            line += 2;
        } else if (piece.startsWith(u"---"_s)) {
            out.text += u'\n'; // never frontmatter
            line += 1;
        }
        const int first = line;
        out.text += piece;
        line += int(piece.count(u'\n'));
        out.segments.append({first, line, nodeId, edgeId});
    };
    for (const Node &n : c.nodes) {
        if (n.isFile()) {
            if (!n.file.trimmed().isEmpty())
                add(fileLink(n.file.trimmed(), n.subpath), n.id, {});
        } else if (n.isLink()) {
            add(urlText(n.url), n.id, {});
        } else if (n.isGroup()) {
            QString label = n.label.simplified();
            add(label, n.id, {});
        } else {
            add(sanitized(n.text), n.id, {}); // text nodes, and the text of a type Brulion does not know
        }
    }
    for (const Edge &e : c.edges)
        add(e.label.simplified(), {}, e.id);
    return out;
}

IndexText indexText(QStringView fileText)
{
    const ReadResult r = readCanvas(fileText);
    return r.canvas ? indexText(*r.canvas) : IndexText();
}

ParsedNote indexNote(const VaultPath &path, const QString &indexText, const VaultConfig &config)
{
    ParsedNote note = markdown::parseNote(path, indexText, config);
    note.title = canvasTitle(path);
    note.frontmatter.clear();
    note.properties.clear();
    note.aliases.clear();
    note.tasks.clear();
    note.reminders.clear();
    note.blockIds.clear();
    note.journalDate.reset();
    for (Block &b : note.blocks) {
        b.task.reset();
        b.reminders.clear();
        b.id.clear();
        b.properties.clear();
    }
    return note;
}

QString nodeAtLine(const IndexText &text, int line, QString *edgeId)
{
    for (const IndexSegment &s : text.segments) {
        if (line >= s.firstLine && line <= s.lastLine) {
            if (edgeId)
                *edgeId = s.edgeId;
            return s.nodeId;
        }
    }
    if (edgeId)
        edgeId->clear();
    return {};
}

std::optional<QString> rewriteCanvas(const QString &fileText, const std::function<QString(const QString &)> &text,
                                     const std::function<QString(const QString &)> &path, int *paths)
{
    ReadResult r = readCanvas(fileText);
    if (!r.canvas)
        return std::nullopt;
    Canvas &c = *r.canvas;
    bool changed = false;
    int pathCount = 0;
    for (Node &n : c.nodes) {
        if (n.isText() && text) {
            if (QString t = text(n.text); t != n.text) {
                n.text = std::move(t);
                changed = true;
            }
        }
        if (n.isFile() && path && !n.file.isEmpty()) {
            if (QString p = path(n.file); !p.isEmpty() && p != n.file) {
                n.file = std::move(p);
                changed = true;
                ++pathCount;
            }
        }
        if (n.isGroup() && path && !n.background.isEmpty()) {
            if (QString p = path(n.background); !p.isEmpty() && p != n.background) {
                n.background = std::move(p);
                changed = true;
                ++pathCount;
            }
        }
    }
    if (paths)
        *paths = pathCount;
    return changed ? writeCanvas(c) : fileText;
}

} // namespace brulion::canvas
