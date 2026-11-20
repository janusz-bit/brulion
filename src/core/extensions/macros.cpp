#include "extensions/macros.h"

#include "text/text.h"
#include "util/paths.h"

#include <QDateTime>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

namespace {

constexpr int kMaxDepth = 8;
constexpr int kMaxTimeouts = 2;
constexpr qsizetype kMaxVariableText = 20000;

/// A number as b.formatNumber writes it by default: at most two decimals, no trailing zeros.
QString numberText(double value, bool comma)
{
    if (!std::isfinite(value))
        return u"—"_s;
    QString s = QString::number(value, 'f', 2);
    while (s.contains(u'.') && (s.endsWith(u'0') || s.endsWith(u'.')))
        s.chop(1);
    if (s == u"-0")
        s = u"0"_s;
    return comma ? s.replace(u'.', u',') : s;
}

/// One line of text: line breaks and tabs are blanks, other control characters go.
QString oneLine(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == u'\n' || c == u'\r' || c == u'\t')
            out.append(u' ');
        else if (c.category() != QChar::Other_Control)
            out.append(c);
    }
    return out;
}

bool isNumber(const QVariant &v)
{
    switch (v.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float: return true;
    default: return false;
    }
}

QString scalarText(const QVariant &v, bool comma)
{
    if (v.metaType().id() == QMetaType::Bool)
        return v.toBool() ? u"true"_s : u"false"_s;
    if (v.metaType().id() == QMetaType::Double || v.metaType().id() == QMetaType::Float)
        return numberText(v.toDouble(), comma);
    return v.toString();
}

QString toneOf(const QVariantMap &node)
{
    if (node.value(u"muted"_s).toBool())
        return u"muted"_s;
    const QString tone = node.value(u"tone"_s).toString();
    return tone == u"success" || tone == u"danger" || tone == u"accent" ? tone : QString();
}

struct Builder {
    bool comma = false;
    markdown::MacroView view;
    int characters = 0;
    bool full = false; // the bounds are reached: what follows is left out
    QString error;

    void add(markdown::MacroPiece piece)
    {
        if (full)
            return;
        piece.text = oneLine(piece.text);
        if (view.pieces.size() >= MaxMacroPieces || characters + piece.text.size() > MaxMacroCharacters) {
            // The last piece that fits says that something follows: "…".
            const int room = MaxMacroCharacters - characters;
            const markdown::MacroPiece more{markdown::MacroPiece::Kind::Text, u"…"_s, {}, {}, false};
            if (view.pieces.size() >= MaxMacroPieces) {
                view.pieces.last() = more;
            } else if (room > 1) {
                piece.text = piece.text.left(room - 1) + u'…';
                view.pieces.append(piece);
            } else {
                view.pieces.append(more);
            }
            full = true;
            return;
        }
        characters += int(piece.text.size());
        view.pieces.append(std::move(piece));
    }

    bool walk(const QVariant &value, int depth)
    {
        if (!value.isValid() || value.isNull())
            return true;
        if (depth > kMaxDepth) {
            error = u"the value is nested too deeply"_s;
            return false;
        }
        if (value.metaType().id() == QMetaType::QString || isNumber(value)
            || value.metaType().id() == QMetaType::Bool) {
            add({markdown::MacroPiece::Kind::Text, scalarText(value, comma), {}, {}, false});
            return true;
        }
        if (value.metaType().id() == QMetaType::QVariantList) {
            for (const QVariant &item : value.toList()) {
                if (!walk(item, depth + 1))
                    return false;
            }
            return true;
        }
        if (value.metaType().id() == QMetaType::QVariantMap) {
            const QVariantMap node = value.toMap();
            const QString type = node.value(u"type"_s).toString();
            if (type == u"text") {
                add({markdown::MacroPiece::Kind::Text,
                     scalarText(node.value(u"text"_s), comma),
                     {},
                     toneOf(node),
                     false});
                return true;
            }
            if (type == u"heading") {
                add({markdown::MacroPiece::Kind::Text, scalarText(node.value(u"text"_s), comma), {}, {}, true});
                return true;
            }
            if (type == u"link") {
                const QString page = oneLine(node.value(u"page"_s).toString()).trimmed();
                const QString label = node.value(u"label"_s).toString();
                if (page.isEmpty()) {
                    add({markdown::MacroPiece::Kind::Text, label, {}, {}, false});
                    return true;
                }
                add({markdown::MacroPiece::Kind::Link, label.isEmpty() ? page : label, page, {}, false});
                return true;
            }
            if (type == u"row" || type == u"column" || type == u"card") {
                for (const QVariant &item : node.value(u"children"_s).toList()) {
                    if (!walk(item, depth + 1))
                        return false;
                }
                return true;
            }
            error = type.isEmpty() ? u"a table that is no b.ui node"_s : u"the node b.ui.%1"_s.arg(type.left(40));
            return false;
        }
        error = u"a value of type %1"_s.arg(QString::fromLatin1(value.metaType().name()));
        return false;
    }
};

} // namespace

markdown::MacroView macroViewFrom(const QVariant &value, bool comma, QString *error)
{
    Builder b;
    b.comma = comma;
    if (!b.walk(value, 0)) {
        markdown::MacroView failed;
        failed.state = markdown::MacroView::State::Error;
        failed.message = u"a macro returns a text, a number, b.ui.text, b.ui.link or a list of them, not "_s + b.error;
        if (error)
            *error = failed.message;
        return failed;
    }
    return b.view;
}

std::optional<QString> variableTextFrom(const QVariant &value, bool comma)
{
    if (!value.isValid() || value.isNull())
        return QString();
    if (value.metaType().id() == QMetaType::QString || isNumber(value) || value.metaType().id() == QMetaType::Bool)
        return scalarText(value, comma).left(kMaxVariableText);
    return std::nullopt;
}

// ---- MacroCache ----------------------------------------------------------------------------------

QString MacroCache::keyOf(const MacroRequest &request)
{
    return request.extension + u'\x1f' + request.name + u'\x1f' + request.arguments + u'\x1f' + request.path;
}

std::optional<markdown::MacroView> MacroCache::view(const QString &key) const
{
    const auto it = m_entries.constFind(key);
    return it == m_entries.constEnd() ? std::nullopt : it->view;
}

bool MacroCache::needsRun(const QString &key) const
{
    const auto it = m_entries.constFind(key);
    return it == m_entries.constEnd() || ((it->stale || !it->view) && it->timeouts < kMaxTimeouts);
}

bool MacroCache::isStale(const QString &key) const
{
    const auto it = m_entries.constFind(key);
    return it == m_entries.constEnd() || it->stale || !it->view;
}

std::optional<MacroRequest> MacroCache::request(const QString &key) const
{
    const auto it = m_entries.constFind(key);
    return it == m_entries.constEnd() ? std::nullopt : std::optional<MacroRequest>(it->request);
}

bool MacroCache::ask(const QString &key, const MacroRequest &request, qint64 nowMs)
{
    const auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        it->askedMs = nowMs;
        return true;
    }
    if (!request.path.isEmpty() && keysOf(request.path).size() >= MaxMacrosPerPage)
        return false;
    Entry e;
    e.request = request;
    e.askedMs = nowMs;
    m_entries.insert(key, e);
    return true;
}

bool MacroCache::store(const QString &key, const markdown::MacroView &view, const Reads &reads, bool failed,
                       qint64 nowMs)
{
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return false; // forgotten meanwhile (its extension went)
    const bool changed = it->view != view;
    it->view = view;
    it->reads = reads;
    it->stale = false;
    if (!failed)
        it->timeouts = 0;
    if (it->askedMs == 0)
        it->askedMs = nowMs;
    return changed;
}

int MacroCache::timeouts(const QString &key) const
{
    return m_entries.value(key).timeouts;
}

void MacroCache::countTimeout(const QString &key)
{
    if (const auto it = m_entries.find(key); it != m_entries.end())
        ++it->timeouts;
}

QStringList MacroCache::notesChanged(const QStringList &paths)
{
    QStringList out;
    if (paths.isEmpty())
        return out;
    QSet<QString> names;
    for (const QString &path : paths)
        names.insert(text::foldKey(paths::pageTitle(path)).simplified());
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->stale || !it->view)
            continue;
        bool hit = it->reads.everything;
        for (qsizetype i = 0; !hit && i < paths.size(); ++i)
            hit = it->reads.paths.contains(paths[i]);
        if (!hit && !it->reads.names.isEmpty())
            hit = it->reads.names.intersects(names);
        if (hit) {
            it->stale = true;
            out << it.key();
        }
    }
    return out;
}

QStringList MacroCache::extensionChanged(const QString &extension)
{
    QStringList out;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->request.extension != extension)
            continue;
        it->stale = true;
        it->timeouts = 0;
        out << it.key();
    }
    return out;
}

QStringList MacroCache::markAllStale()
{
    QStringList out;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        it->stale = true;
        it->timeouts = 0;
        out << it.key();
    }
    return out;
}

QStringList MacroCache::pending(qint64 sinceMs) const
{
    QList<std::pair<qint64, QString>> found;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if ((it->stale || !it->view) && it->timeouts < kMaxTimeouts && it->askedMs >= sinceMs)
            found.append({it->askedMs, it.key()});
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    QStringList out;
    out.reserve(found.size());
    for (const auto &f : std::as_const(found))
        out << f.second;
    return out;
}

QStringList MacroCache::keysOf(const QString &path) const
{
    QStringList out;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->request.path == path)
            out << it.key();
    }
    return out;
}

void MacroCache::prune(qint64 beforeMs, int maxEntries)
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->askedMs < beforeMs)
            it = m_entries.erase(it);
        else
            ++it;
    }
    if (m_entries.size() <= maxEntries)
        return;
    QList<std::pair<qint64, QString>> byAge;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        byAge.append({it->askedMs, it.key()});
    std::sort(byAge.begin(), byAge.end());
    for (qsizetype i = 0; i < byAge.size() - maxEntries; ++i)
        m_entries.remove(byAge[i].second);
}

} // namespace brulion::extensions
