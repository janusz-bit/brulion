#include "codefiledocument.h"

#include "codefilesbackground_p.h"

#include "codefiles/search.h"
#include "codefiles/viewer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::codefiles {

struct CodeFileDocument::Loaded {
    QString path;
    QString kind;
    bool truncated = false;
    qint64 size = 0;
    QString language;
    int maxColumns = 0;
    QStringList lines;
    QList<QList<CodeSpan>> spans;
    TextShape shape;
    QByteArray hash;
};

namespace {

constexpr CodeToken AllTokens[] = {CodeToken::Keyword,  CodeToken::Type,         CodeToken::Constant,
                                   CodeToken::Builtin,  CodeToken::Function,     CodeToken::Number,
                                   CodeToken::String,   CodeToken::Comment,      CodeToken::Preprocessor,
                                   CodeToken::Tag,      CodeToken::Attribute,    CodeToken::Property,
                                   CodeToken::Variable, CodeToken::Inserted,     CodeToken::Deleted};

QString kindName(FileKind kind)
{
    switch (kind) {
    case FileKind::Text: return u"text"_s;
    case FileKind::Picture: return u"picture"_s;
    case FileKind::Binary: return u"binary"_s;
    case FileKind::Missing: return u"missing"_s;
    case FileKind::Folder: return u"folder"_s;
    case FileKind::Unreadable: return u"unreadable"_s;
    }
    return {};
}

int columnsOf(QStringView line, int tabWidth)
{
    int columns = 0;
    for (const QChar c : line.first(std::min<qsizetype>(line.size(), CodeFileDocument::MaxLineLength)))
        columns = c == u'\t' ? (columns / tabWidth + 1) * tabWidth : columns + 1;
    return columns;
}

void appendEscaped(QString &out, QStringView text, int tabWidth, int *column)
{
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'\t': {
            const int to = (*column / tabWidth + 1) * tabWidth;
            for (; *column < to; ++*column)
                out += u"&nbsp;"_s;
            continue;
        }
        // (StyledText folds runs of spaces into one.)
        case u' ': out += u"&nbsp;"_s; break;
        case u'<': out += u"&lt;"_s; break;
        case u'>': out += u"&gt;"_s; break;
        case u'&': out += u"&amp;"_s; break;
        case u'"': out += u"&quot;"_s; break;
        default:
            // (Control characters show as nothing; the replacement character as itself.)
            if (c.unicode() >= 0x20 || c.unicode() == 0x1b)
                out += c;
        }
        ++*column;
    }
}

} // namespace

CodeFileDocument::CodeFileDocument(QObject *parent) : QAbstractListModel(parent) { }

int CodeFileDocument::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_lines.size());
}

QHash<int, QByteArray> CodeFileDocument::roleNames() const
{
    return {{NumberRole, "number"}, {HtmlRole, "html"}, {TextRole, "text"}};
}

QVariant CodeFileDocument::data(const QModelIndex &index, int role) const
{
    const int row = index.row();
    if (!index.isValid() || row < 0 || row >= int(m_lines.size()))
        return {};
    switch (role) {
    case NumberRole: return row + 1;
    case TextRole: return m_lines.at(row);
    case HtmlRole:
        return lineHtml(m_lines.at(row), m_spans.value(row), m_colours, m_tabWidth,
                        m_matches.isEmpty() ? QList<std::pair<int, int>>() : matchesOf(row),
                        m_palette.value(u"match"_s).toString());
    default: return {};
    }
}

namespace {

/// The colour of `spans` at `at` ("" none).
QString colourAt(const QList<CodeSpan> &spans, const QHash<int, QString> &colours, qsizetype at)
{
    for (const CodeSpan &s : spans)
        if (at >= s.from && at < s.from + s.length)
            return colours.value(int(s.kind));
    return {};
}

bool matchedAt(const QList<std::pair<int, int>> &matches, qsizetype at)
{
    return std::ranges::any_of(matches, [at](const auto &m) { return at >= m.first && at < m.first + m.second; });
}

/// Where a line of `size` characters changes colour or match: its ends and every span's and match's.
QList<qsizetype> segmentEdges(qsizetype size, const QList<CodeSpan> &spans, const QList<std::pair<int, int>> &matches)
{
    QList<qsizetype> edges{0, size};
    for (const CodeSpan &s : spans)
        edges << s.from << s.from + s.length;
    for (const auto &[from, length] : matches)
        edges << from << from + length;
    std::ranges::sort(edges);
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    edges.removeIf([size](qsizetype e) { return e < 0 || e > size; });
    return edges;
}

} // namespace

QString CodeFileDocument::lineHtml(QStringView line, const QList<CodeSpan> &spans, const QHash<int, QString> &colours,
                                   int tabWidth, const QList<std::pair<int, int>> &matches, const QString &matchColour)
{
    tabWidth = std::max(1, tabWidth);
    const QStringView shown = line.first(std::min<qsizetype>(line.size(), MaxLineLength));
    QString out;
    out.reserve(shown.size() * 2);
    int column = 0;
    const QList<qsizetype> edges = segmentEdges(shown.size(), spans, matches);
    for (qsizetype i = 0; i + 1 < edges.size(); ++i) {
        const qsizetype from = edges.at(i);
        const bool matched = matchedAt(matches, from);
        const QString colour = matched && !matchColour.isEmpty() ? matchColour : colourAt(spans, colours, from);
        if (matched)
            out += u"<b>"_s;
        if (!colour.isEmpty())
            out += u"<font color=\""_s + colour + u"\">"_s;
        appendEscaped(out, shown.sliced(from, edges.at(i + 1) - from), tabWidth, &column);
        if (!colour.isEmpty())
            out += u"</font>"_s;
        if (matched)
            out += u"</b>"_s;
    }
    if (shown.size() < line.size())
        out += u"…"_s;
    return out;
}

void CodeFileDocument::find(const QString &pattern, bool caseSensitive, bool wholeWord, bool regex)
{
    m_findPattern = pattern;
    m_findCase = caseSensitive;
    m_findWord = wholeWord;
    m_findRegex = regex;
    refind();
}

void CodeFileDocument::refind()
{
    const QList<Match> before = m_matches;
    m_matches.clear();
    m_findError.clear();
    if (!m_findPattern.isEmpty()) {
        SearchOptions options;
        options.pattern = m_findPattern;
        options.caseSensitive = m_findCase;
        options.wholeWord = m_findWord;
        options.regex = m_findRegex;
        const QRegularExpression expression = searchExpression(options);
        if (!expression.isValid())
            m_findError = expression.errorString();
        for (int row = 0; expression.isValid() && row < m_lines.size() && m_matches.size() < MaxMatches; ++row)
            for (const auto &[from, length] : matchRanges(m_lines.at(row), expression, MaxMatches))
                m_matches.append({row, from, length});
    }
    // (The rows that had or have a match show it, or no longer.)
    QSet<int> rows;
    for (const Match &m : std::as_const(before))
        rows.insert(m.row);
    for (const Match &m : std::as_const(m_matches))
        rows.insert(m.row);
    for (const int row : std::as_const(rows))
        if (row < m_lines.size())
            Q_EMIT dataChanged(index(row), index(row), {HtmlRole});
    Q_EMIT findChanged();
}

QList<std::pair<int, int>> CodeFileDocument::matchesOf(int row) const
{
    // (In order of rows: those of `row` found by halves.)
    QList<std::pair<int, int>> out;
    auto it = std::lower_bound(m_matches.cbegin(), m_matches.cend(), row,
                               [](const Match &m, int r) { return m.row < r; });
    for (; it != m_matches.cend() && it->row == row; ++it)
        out.append({it->from, it->length});
    return out;
}

int CodeFileDocument::matchLine(int index) const
{
    return index >= 0 && index < m_matches.size() ? m_matches.at(index).row + 1 : 0;
}

int CodeFileDocument::nextMatch(int index, int delta) const
{
    const int n = int(m_matches.size());
    if (n == 0)
        return -1;
    if (index < 0)
        return delta < 0 ? n - 1 : 0;
    return ((index + (delta < 0 ? -1 : 1)) % n + n) % n;
}

void CodeFileDocument::setFolder(const QString &folder)
{
    if (folder == m_folder)
        return;
    m_folder = folder;
    Q_EMIT folderChanged();
    Q_EMIT pathChanged();
    load();
}

void CodeFileDocument::setPath(const QString &path)
{
    if (path == m_path)
        return;
    m_path = path;
    Q_EMIT pathChanged();
    load();
}

void CodeFileDocument::setPalette(const QVariantMap &palette)
{
    m_palette = palette;
    m_colours.clear();
    for (const CodeToken t : AllTokens) {
        const QVariant v = palette.value(codeTokenName(t));
        if (!v.isValid())
            continue;
        const QColor c = v.value<QColor>();
        const QColor colour = c.isValid() ? c : QColor::fromString(v.toString());
        if (colour.isValid())
            m_colours.insert(int(t), colour.name(colour.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb));
    }
    Q_EMIT paletteChanged();
    if (!m_lines.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_lines.size()) - 1), {HtmlRole});
}

void CodeFileDocument::setTabWidth(int width)
{
    width = std::clamp(width, 1, 16);
    if (width == m_tabWidth)
        return;
    m_tabWidth = width;
    Q_EMIT tabWidthChanged();
    load();
}

QString CodeFileDocument::absolutePath() const
{
    if (m_folder.isEmpty() || m_path.isEmpty())
        return {};
    const QString relative = relativeInside(m_folder, m_path);
    return relative.isEmpty() ? QString() : QDir::cleanPath(QDir(m_folder).absolutePath() + u'/' + relative);
}

QUrl CodeFileDocument::fileUrl() const
{
    const QString abs = absolutePath();
    return abs.isEmpty() ? QUrl() : QUrl::fromLocalFile(abs);
}

void CodeFileDocument::reload()
{
    load();
}

void CodeFileDocument::load()
{
    const int generation = ++m_generation;
    if (m_folder.isEmpty() || m_path.isEmpty()) {
        apply({});
        return;
    }
    const QString abs = absolutePath();
    if (abs.isEmpty()) {
        Loaded refused;
        refused.kind = u"outside"_s;
        apply(refused);
        return;
    }
    // (The same file read again keeps its lines shown until the new ones are there.)
    if (m_kind != u"loading" && abs != m_loadedPath) {
        m_kind = u"loading"_s;
        Q_EMIT loaded();
    }
    const int tabWidth = m_tabWidth;
    detail::onPool<Loaded>(
            this,
            [abs, tabWidth] {
                Loaded l;
                l.path = abs;
                const ViewerFile file = readForViewer(abs);
                l.kind = kindName(file.kind);
                l.truncated = file.truncated;
                l.size = file.size;
                l.shape = file.shape;
                l.hash = file.hash;
                if (file.kind != FileKind::Text)
                    return l;
                l.language = languageForPath(abs);
                QStringView text = file.text;
                // (A last line break ends the last line: no empty line after it.)
                if (text.endsWith(u'\n'))
                    text.chop(text.endsWith(u"\r\n") ? 2 : 1);
                else if (text.endsWith(u'\r'))
                    text.chop(1);
                CodeState state;
                qsizetype start = 0;
                const qsizetype n = text.size();
                while (start <= n) {
                    qsizetype end = start;
                    while (end < n && text.at(end) != u'\n' && text.at(end) != u'\r')
                        ++end;
                    const QStringView line = text.sliced(start, end - start);
                    l.lines.append(line.toString());
                    l.spans.append(lexCodeLine(l.language, line, &state));
                    l.maxColumns = std::max(l.maxColumns, columnsOf(line, tabWidth));
                    if (end >= n)
                        break;
                    start = end + ((text.at(end) == u'\r' && end + 1 < n && text.at(end + 1) == u'\n') ? 2 : 1);
                }
                return l;
            },
            [this, generation](Loaded l) {
                if (generation == m_generation)
                    apply(std::move(l));
            });
}

void CodeFileDocument::apply(Loaded l)
{
    beginResetModel();
    m_loadedPath = l.path;
    m_kind = l.kind;
    m_truncated = l.truncated;
    m_size = l.size;
    m_language = l.language;
    m_maxColumns = l.maxColumns;
    m_lines = std::move(l.lines);
    m_spans = std::move(l.spans);
    m_shape = l.shape;
    m_hash = l.hash;
    m_matches.clear();
    endResetModel();
    Q_EMIT loaded();
    if (!m_findPattern.isEmpty())
        refind();
}

bool CodeFileDocument::editable() const
{
    return m_kind == u"text" && !m_truncated && !m_shape.lossy && !m_hash.isEmpty();
}

QString CodeFileDocument::editText() const
{
    return editable() ? m_lines.join(u'\n') + (m_shape.finalNewline ? u"\n"_s : QString()) : QString();
}

QString CodeFileDocument::save(const QString &text)
{
    if (!editable())
        return tr("This file cannot be edited here.");
    const QString abs = absolutePath();
    QFile current(abs);
    if (!current.open(QIODevice::ReadOnly)
        || QCryptographicHash::hash(current.readAll(), QCryptographicHash::Sha1) != m_hash)
        return tr("The file changed on disk since it was opened: reload it, then edit it again.");
    current.close();
    QSaveFile out(abs);
    const QByteArray bytes = encodeEdited(text, m_shape);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        return tr("The file could not be saved: %1").arg(out.errorString());
    m_hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
    load();
    return {};
}

QString CodeFileDocument::linesText(int from, int to) const
{
    if (m_lines.isEmpty())
        return {};
    if (to < from)
        std::swap(from, to);
    from = std::clamp(from, 1, int(m_lines.size()));
    to = std::clamp(to, 1, int(m_lines.size()));
    return m_lines.mid(from - 1, to - from + 1).join(u'\n');
}

QString CodeFileDocument::lineText(int line) const
{
    return line >= 1 && line <= int(m_lines.size()) ? m_lines.at(line - 1) : QString();
}

} // namespace brulion::codefiles
