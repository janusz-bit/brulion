#include "codefiledocument.h"

#include "codefilesbackground_p.h"

#include "codefiles/viewer.h"

#include <QDir>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::codefiles {

struct CodeFileDocument::Loaded {
    QString kind;
    bool truncated = false;
    qint64 size = 0;
    QString language;
    int maxColumns = 0;
    QStringList lines;
    QList<QList<CodeSpan>> spans;
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
    case HtmlRole: return lineHtml(m_lines.at(row), m_spans.value(row), m_colours, m_tabWidth);
    default: return {};
    }
}

QString CodeFileDocument::lineHtml(QStringView line, const QList<CodeSpan> &spans, const QHash<int, QString> &colours,
                                   int tabWidth)
{
    tabWidth = std::max(1, tabWidth);
    const QStringView shown = line.first(std::min<qsizetype>(line.size(), MaxLineLength));
    QString out;
    out.reserve(shown.size() * 2);
    int column = 0;
    qsizetype at = 0;
    for (const CodeSpan &s : spans) {
        if (s.from >= shown.size())
            break;
        if (s.from > at)
            appendEscaped(out, shown.sliced(at, s.from - at), tabWidth, &column);
        const qsizetype end = std::min<qsizetype>(s.from + s.length, shown.size());
        const QString colour = colours.value(int(s.kind));
        if (!colour.isEmpty())
            out += u"<font color=\""_s + colour + u"\">"_s;
        appendEscaped(out, shown.sliced(s.from, end - s.from), tabWidth, &column);
        if (!colour.isEmpty())
            out += u"</font>"_s;
        at = end;
    }
    if (at < shown.size())
        appendEscaped(out, shown.sliced(at), tabWidth, &column);
    if (shown.size() < line.size())
        out += u"…"_s;
    return out;
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
    if (m_kind != u"loading") {
        m_kind = u"loading"_s;
        Q_EMIT loaded();
    }
    const int tabWidth = m_tabWidth;
    detail::onPool<Loaded>(
            this,
            [abs, tabWidth] {
                Loaded l;
                const ViewerFile file = readForViewer(abs);
                l.kind = kindName(file.kind);
                l.truncated = file.truncated;
                l.size = file.size;
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
    m_kind = l.kind;
    m_truncated = l.truncated;
    m_size = l.size;
    m_language = l.language;
    m_maxColumns = l.maxColumns;
    m_lines = std::move(l.lines);
    m_spans = std::move(l.spans);
    endResetModel();
    Q_EMIT loaded();
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
