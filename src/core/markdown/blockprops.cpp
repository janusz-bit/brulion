// Block properties (blockprops.h): the `key:: value` lines of one block.
#include "markdown/blockprops.h"

#include "text/text.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::markdown {

namespace {

const QRegularExpression &propertyPattern()
{
    static const QRegularExpression re(uR"(^([ \t]*)([^\s:][^:]*?)::(?:[ \t](.*)|$))"_s);
    return re;
}

bool isPropertyName(const QString &key)
{
    if (key.isEmpty())
        return false;
    for (const QChar c : key)
        if (!c.isLetterOrNumber() && c != u'-' && c != u'_')
            return false;
    return true;
}

QString leadingSpace(QStringView line)
{
    qsizetype n = line.startsWith(QChar(0xFEFF)) ? 1 : 0;
    const qsizetype bom = n;
    while (n < line.size() && (line[n] == u' ' || line[n] == u'\t'))
        ++n;
    return line.mid(bom, n - bom).toString();
}

} // namespace

std::optional<QString> setBlockProperty(const QString &content, int line, int ownEndLine, const QString &key,
                                        const QString &value)
{
    if (!isPropertyName(key) || value.contains(u'\n') || value.contains(u'\r'))
        return std::nullopt;
    QList<text::Line> lines = text::splitLines(content);
    if (line < 0 || line >= lines.size())
        return std::nullopt;
    ownEndLine = std::clamp(ownEndLine, line, int(lines.size()) - 1);
    const QString wanted = key.toCaseFolded();
    const QString newValue = value.trimmed();
    QList<int> matching;
    int lastProperty = -1;
    QString propertyIndent;
    for (int i = line + 1; i <= ownEndLine; ++i) {
        const QRegularExpressionMatch m = propertyPattern().match(lines[i].text);
        if (!m.hasMatch())
            continue;
        lastProperty = i;
        if (propertyIndent.isEmpty())
            propertyIndent = m.captured(1);
        if (m.captured(2).trimmed().toCaseFolded() == wanted)
            matching << i;
    }
    if (newValue.isEmpty()) {
        for (qsizetype k = matching.size() - 1; k >= 0; --k) {
            const int i = matching[k];
            if (i == int(lines.size()) - 1 && i > 0)
                lines[i - 1].eol = lines[i].eol;
            lines.remove(i);
        }
        return text::joinLines(lines);
    }
    if (!matching.isEmpty()) {
        const int i = matching.first();
        const QRegularExpressionMatch m = propertyPattern().match(lines[i].text);
        lines[i].text = lines[i].text.left(m.capturedEnd(2)) + u":: "_s + newValue;
        for (qsizetype k = matching.size() - 1; k >= 1; --k)
            lines.remove(matching[k]); // a key given twice: one value now
        return text::joinLines(lines);
    }
    if (propertyIndent.isEmpty() && lastProperty < 0) {
        const QString first = lines[line].text;
        const qsizetype bom = first.startsWith(QChar(0xFEFF)) ? 1 : 0;
        static const QRegularExpression markerPattern(uR"(^([ \t]*)([-*+]|\d{1,9}[.)])[ \t])"_s);
        const QRegularExpressionMatch marker = markerPattern.match(first.mid(bom));
        propertyIndent = marker.hasMatch() ? marker.captured(1) + QString(marker.captured(2).size() + 1, u' ')
                                           : leadingSpace(first);
    }
    const int after = lastProperty >= 0 ? lastProperty : line;
    text::Line added;
    added.text = propertyIndent + key + u":: "_s + newValue;
    added.eol = lines[after].eol;
    if (added.eol.isEmpty()) {
        lines[after].eol = text::preferredEol(lines, after);
    }
    lines.insert(after + 1, added);
    return text::joinLines(lines);
}

std::optional<QString> blockPropertyText(QStringView content, int line, int ownEndLine, QStringView key)
{
    const QList<text::Line> lines = text::splitLines(content.toString());
    if (line < 0 || line >= lines.size())
        return std::nullopt;
    ownEndLine = std::clamp(ownEndLine, line, int(lines.size()) - 1);
    const QString wanted = key.toString().toCaseFolded();
    for (int i = line; i <= ownEndLine; ++i) {
        const QRegularExpressionMatch m = propertyPattern().match(lines[i].text);
        if (m.hasMatch() && m.captured(2).trimmed().toCaseFolded() == wanted)
            return m.captured(3).trimmed();
    }
    return std::nullopt;
}

bool isBlockPropertyKey(QStringView key)
{
    if (key.isEmpty())
        return false;
    return std::ranges::all_of(key, [](QChar c) {
        return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-'
               || c == u'_';
    });
}

} // namespace brulion::markdown
