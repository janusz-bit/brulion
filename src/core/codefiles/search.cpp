#include "codefiles/search.h"

#include "codefiles/fileindex.h"
#include "text/text.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::codefiles {

namespace {

/// The text of a ripgrep "arbitrary data" object: {"text": …} or {"bytes": base64}.
QString dataText(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    if (o.contains(u"text"_s))
        return o.value(u"text"_s).toString();
    return QString::fromUtf8(QByteArray::fromBase64(o.value(u"bytes"_s).toString().toLatin1()));
}

QByteArray dataBytes(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    if (o.contains(u"text"_s))
        return o.value(u"text"_s).toString().toUtf8();
    return QByteArray::fromBase64(o.value(u"bytes"_s).toString().toLatin1());
}

QString withoutLineEnd(QString s)
{
    while (s.endsWith(u'\n') || s.endsWith(u'\r'))
        s.chop(1);
    return s;
}

} // namespace

QString patternError(const SearchOptions &options)
{
    if (options.pattern.isEmpty())
        return u"empty"_s;
    if (options.regex) {
        const QRegularExpression re(options.pattern);
        if (!re.isValid())
            return re.errorString();
    }
    return {};
}

QRegularExpression searchExpression(const SearchOptions &options)
{
    QString pattern = options.regex ? options.pattern : QRegularExpression::escape(options.pattern);
    if (options.wholeWord)
        pattern = text::wordStartAssertion() + u"(?:"_s + pattern + u')' + text::wordEndAssertion();
    QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.caseSensitive)
        flags |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression re(pattern, flags);
    if (options.pattern.isEmpty())
        return QRegularExpression(u"("_s); // (invalid: an empty pattern finds nothing)
    re.optimize();
    return re;
}

QList<std::pair<int, int>> matchRanges(QStringView line, const QRegularExpression &expression, int limit)
{
    QList<std::pair<int, int>> out;
    if (!expression.isValid())
        return out;
    for (auto it = expression.globalMatchView(line); it.hasNext() && out.size() < limit;) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedLength() > 0)
            out.append({int(m.capturedStart()), int(m.capturedEnd())});
    }
    return out;
}

QList<std::pair<int, int>> matchRanges(QStringView line, const SearchOptions &options, int limit)
{
    return matchRanges(line, searchExpression(options), limit);
}

QStringList ripgrepArguments(const SearchOptions &options)
{
    QStringList args{u"--json"_s,
                     u"--no-config"_s,
                     u"--no-messages"_s,
                     u"--hidden"_s,
                     u"--glob=!.git/"_s,
                     u"--sort=path"_s,
                     u"--max-filesize=%1"_s.arg(options.maxFileSize),
                     u"--max-count=%1"_s.arg(std::max(1, options.maxPerFile))};
    args << (options.caseSensitive ? u"--case-sensitive"_s : u"--ignore-case"_s);
    if (!options.regex)
        args << u"--fixed-strings"_s;
    if (options.wholeWord)
        args << u"--word-regexp"_s;
    // (--regexp=: a pattern starting with "-" is never an option. A path is always given: rg
    // without one reads its standard input when that is no terminal.)
    args << u"--regexp=%1"_s.arg(options.pattern) << u"--"_s << u"."_s;
    return args;
}

QStringList gitGrepArguments(const SearchOptions &options)
{
    QStringList args{u"grep"_s, u"-n"_s, u"-I"_s, u"--untracked"_s, u"--no-color"_s, u"-z"_s, u"--column"_s};
    args << (options.regex ? u"-E"_s : u"-F"_s);
    if (!options.caseSensitive)
        args << u"-i"_s;
    if (options.wholeWord)
        args << u"-w"_s;
    args << u"-e"_s << options.pattern;
    return args;
}

std::optional<SearchMatch> parseRipgrepLine(QByteArrayView line)
{
    if (line.isEmpty() || !line.contains("\"match\""))
        return std::nullopt;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toByteArray(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    const QJsonObject o = doc.object();
    if (o.value(u"type"_s).toString() != u"match")
        return std::nullopt;
    const QJsonObject data = o.value(u"data"_s).toObject();
    SearchMatch m;
    m.path = cleanRelativePath(dataText(data.value(u"path"_s)));
    m.line = data.value(u"line_number"_s).toInt();
    if (m.path.isEmpty() || m.line <= 0)
        return std::nullopt;
    const QByteArray bytes = dataBytes(data.value(u"lines"_s));
    m.text = withoutLineEnd(QString::fromUtf8(bytes));
    // Byte offsets of the UTF-8 line → UTF-16 offsets of its text.
    const auto utf16 = [&bytes](qint64 offset) {
        return int(QString::fromUtf8(bytes.first(std::clamp<qint64>(offset, 0, bytes.size()))).size());
    };
    for (const QJsonValue &v : data.value(u"submatches"_s).toArray()) {
        const QJsonObject s = v.toObject();
        const int from = std::min(utf16(s.value(u"start"_s).toInteger()), int(m.text.size()));
        const int to = std::min(utf16(s.value(u"end"_s).toInteger()), int(m.text.size()));
        if (to > from)
            m.ranges.append({from, to});
    }
    return clipped(std::move(m));
}

QList<SearchMatch> parseGitGrep(QByteArrayView output, const SearchOptions &options, int limit)
{
    QList<SearchMatch> out;
    const QRegularExpression expression = searchExpression(options);
    qsizetype at = 0;
    const qsizetype n = output.size();
    while (at < n && out.size() < limit) {
        const qsizetype pathEnd = output.indexOf('\0', at);
        if (pathEnd < 0)
            break;
        const qsizetype lineEnd = output.indexOf('\0', pathEnd + 1);
        if (lineEnd < 0)
            break;
        // (--column: the first match's column before the text; older output without it also reads.)
        qsizetype textStart = lineEnd + 1;
        const qsizetype columnEnd = output.indexOf('\0', textStart);
        qsizetype recordEnd = output.indexOf('\n', textStart);
        if (recordEnd < 0)
            recordEnd = n;
        if (columnEnd >= 0 && columnEnd < recordEnd)
            textStart = columnEnd + 1;
        SearchMatch m;
        m.path = cleanRelativePath(QString::fromUtf8(output.sliced(at, pathEnd - at)));
        bool ok = false;
        m.line = QByteArrayView(output.sliced(pathEnd + 1, lineEnd - pathEnd - 1)).toInt(&ok);
        m.text = withoutLineEnd(QString::fromUtf8(output.sliced(textStart, recordEnd - textStart)));
        at = recordEnd + 1;
        if (!ok || m.line <= 0 || m.path.isEmpty())
            continue;
        m.ranges = matchRanges(m.text, expression);
        out.append(clipped(std::move(m)));
    }
    return out;
}

QList<SearchMatch> searchText(const QString &path, QStringView text, const QRegularExpression &expression, int limit)
{
    QList<SearchMatch> out;
    if (!expression.isValid() || limit <= 0)
        return out;
    int line = 1;
    qsizetype start = 0;
    const qsizetype n = text.size();
    while (start <= n && out.size() < limit) {
        qsizetype end = start;
        while (end < n && text.at(end) != u'\n' && text.at(end) != u'\r')
            ++end;
        const QStringView l = text.sliced(start, end - start);
        if (!l.isEmpty()) {
            const QList<std::pair<int, int>> ranges = matchRanges(l, expression);
            if (!ranges.isEmpty())
                out.append(clipped({path, line, l.toString(), ranges}));
        }
        if (end >= n)
            break;
        start = end + ((text.at(end) == u'\r' && end + 1 < n && text.at(end + 1) == u'\n') ? 2 : 1);
        ++line;
    }
    return out;
}

SearchMatch clipped(SearchMatch match)
{
    if (match.text.size() <= MaxLineLength)
        return match;
    const int first = match.ranges.isEmpty() ? 0 : match.ranges.first().first;
    // A quarter of the room before the first match, the rest after it.
    int from = std::max(0, first - MaxLineLength / 4);
    int to = std::min(int(match.text.size()), from + MaxLineLength);
    from = std::max(0, to - MaxLineLength);
    // (Never between the two halves of a surrogate pair.)
    if (from > 0 && match.text.at(from).isLowSurrogate())
        ++from;
    if (to < match.text.size() && to > 0 && match.text.at(to - 1).isHighSurrogate())
        --to;
    const QString prefix = from > 0 ? u"…"_s : QString();
    const QString suffix = to < match.text.size() ? u"…"_s : QString();
    QList<std::pair<int, int>> ranges;
    for (const auto &[a, b] : std::as_const(match.ranges)) {
        const int ca = std::max(a, from);
        const int cb = std::min(b, to);
        if (cb > ca)
            ranges.append({ca - from + int(prefix.size()), cb - from + int(prefix.size())});
    }
    match.text = prefix + match.text.mid(from, to - from) + suffix;
    match.ranges = ranges;
    return match;
}

} // namespace brulion::codefiles
