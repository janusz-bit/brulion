#include "term/links.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::term {

namespace {

/// What a URL cannot hold: it ends there.
bool endsUrl(QChar c)
{
    return c.isSpace() || c.unicode() < 0x20 || c == u'"' || c == u'\'' || c == u'`' || c == u'<' || c == u'>'
           || c.unicode() == 0x7f;
}

/// What paths are made of (letters of any script, digits, and a few marks): anything else ends one.
bool isPathChar(QChar c)
{
    return c.isLetterOrNumber() || c.isMark() || c == u'_' || c == u'-' || c == u'.' || c == u'/' || c == u'~'
           || c == u'+' || c == u'@';
}

bool startsWithScheme(QStringView text, qsizetype at, qsizetype *schemeLength)
{
    static const QStringView schemes[] = {u"https://", u"http://", u"file://"};
    for (QStringView scheme : schemes) {
        if (text.mid(at).startsWith(scheme, Qt::CaseInsensitive)) {
            *schemeLength = scheme.size();
            return true;
        }
    }
    return false;
}

/// The end of the URL starting at `start`: up to what ends it, then without trailing punctuation
/// and closing brackets it does not open.
qsizetype urlEnd(QStringView text, qsizetype start)
{
    qsizetype end = start;
    while (end < text.size() && !endsUrl(text.at(end)))
        ++end;
    for (;;) {
        if (end <= start)
            break;
        const QChar last = text.at(end - 1);
        if (QStringView(u".,;:!?").contains(last)) {
            --end;
            continue;
        }
        const QChar open = last == u')' ? u'(' : last == u']' ? u'[' : last == u'}' ? u'{' : QChar();
        if (!open.isNull()) {
            const QStringView url = text.mid(start, end - start);
            if (url.count(open) < url.count(last)) {
                --end;
                continue;
            }
        }
        break;
    }
    return end;
}

/// Digits at `at`: their value (capped), and where they end; -1 when there are none.
int readNumber(QStringView text, qsizetype at, qsizetype *end)
{
    qsizetype i = at;
    qint64 value = 0;
    while (i < text.size() && text.at(i).isDigit() && text.at(i).unicode() < 0x80) {
        value = std::min<qint64>(value * 10 + (text.at(i).unicode() - u'0'), 1000000000);
        ++i;
    }
    if (i == at)
        return -1;
    *end = i;
    return int(value);
}

/// A word that may be a path: one with a '/' (not only slashes and dots, no "//" of a URL), or a
/// file name with an extension of letters and digits that is not a number ("1.5", "3.14.2").
bool looksLikePath(QStringView word)
{
    if (word.isEmpty())
        return false;
    bool letter = false;
    for (QChar c : word) {
        if (c.isLetter()) {
            letter = true;
            break;
        }
    }
    if (word.contains(u'/'))
        return word.contains(u"//") ? false : std::ranges::any_of(word, [](QChar c) { return c != u'/' && c != u'.'; });
    if (!letter)
        return false;
    const qsizetype dot = word.lastIndexOf(u'.');
    if (dot <= 0 || dot == word.size() - 1)
        return false;
    const QStringView extension = word.mid(dot + 1);
    return extension.size() <= 12
           && std::ranges::all_of(extension, [](QChar c) { return c.isLetterOrNumber() && c.unicode() < 0x80; });
}

} // namespace

QList<TextLink> findLinks(QStringView text)
{
    QList<TextLink> out;
    qsizetype i = 0;
    while (i < text.size()) {
        const QChar c = text.at(i);
        const bool wordStart = i == 0 || !text.at(i - 1).isLetterOrNumber();
        qsizetype schemeLength = 0;
        if (wordStart && (c == u'h' || c == u'H' || c == u'f' || c == u'F') && startsWithScheme(text, i, &schemeLength)) {
            const qsizetype end = urlEnd(text, i);
            if (end > i + schemeLength) {
                TextLink link;
                link.kind = TextLink::Url;
                link.start = i;
                link.length = end - i;
                link.target = text.mid(i, end - i).toString();
                out.append(link);
                i = end;
                continue;
            }
        }
        if (!isPathChar(c)) {
            ++i;
            continue;
        }
        // A word of path characters; trailing dots (the end of a sentence) are not the path's.
        qsizetype end = i;
        while (end < text.size() && isPathChar(text.at(end)))
            ++end;
        qsizetype wordEnd = end;
        while (wordEnd > i && text.at(wordEnd - 1) == u'.' && text.mid(i, wordEnd - i) != u".."
               && text.mid(i, wordEnd - i) != u".")
            --wordEnd;
        const QStringView word = text.mid(i, wordEnd - i);
        // (A '~' only as the home folder's.)
        const bool tildeOk = !word.contains(u'~') || (word.startsWith(u"~/") && word.count(u'~') == 1);
        if (tildeOk && looksLikePath(word) && word != u"~/") {
            TextLink link;
            link.kind = TextLink::Path;
            link.start = i;
            link.target = word.toString();
            qsizetype after = wordEnd;
            qsizetype numberEnd = 0;
            if (after + 1 < text.size() && text.at(after) == u':') {
                // path:line, path:line:column
                const int line = readNumber(text, after + 1, &numberEnd);
                if (line > 0) {
                    link.line = line;
                    after = numberEnd;
                    if (after + 1 < text.size() && text.at(after) == u':') {
                        const int column = readNumber(text, after + 1, &numberEnd);
                        if (column > 0) {
                            link.column = column;
                            after = numberEnd;
                        }
                    }
                }
            } else if (after + 1 < text.size() && text.at(after) == u'(') {
                // path(line), path(line,column), path(line, column) — MSVC, TypeScript
                const int line = readNumber(text, after + 1, &numberEnd);
                if (line > 0) {
                    qsizetype at = numberEnd;
                    int column = 0;
                    if (at < text.size() && text.at(at) == u',') {
                        ++at;
                        while (at < text.size() && text.at(at) == u' ')
                            ++at;
                        column = readNumber(text, at, &numberEnd);
                        at = column > 0 ? numberEnd : -1;
                    }
                    if (at > 0 && at < text.size() && text.at(at) == u')') {
                        link.line = line;
                        link.column = std::max(0, column);
                        after = at + 1;
                    }
                }
            } else if (text.mid(after).startsWith(u"\", line ")) {
                // Python's tracebacks: File "x.py", line 12
                const int line = readNumber(text, after + 8, &numberEnd);
                if (line > 0) {
                    link.line = line;
                    after = numberEnd;
                }
            }
            link.length = after - i;
            out.append(link);
            i = after;
            continue;
        }
        i = end;
    }
    return out;
}

qsizetype linkAt(const QList<TextLink> &links, qsizetype index)
{
    for (qsizetype i = 0; i < links.size(); ++i) {
        const TextLink &link = links.at(i);
        if (index >= link.start && index < link.start + link.length)
            return i;
    }
    return -1;
}

QString resolvePath(const QString &path, const QString &workingDirectory, const QString &home)
{
    if (path.isEmpty() || path.contains(u"://"))
        return {};
    auto existing = [](const QString &candidate) -> QString {
        const QFileInfo info(candidate);
        return info.isFile() ? QDir::cleanPath(info.absoluteFilePath()) : QString();
    };
    if (path.startsWith(u"~/"))
        return home.isEmpty() ? QString() : existing(home + path.mid(1));
    if (path.startsWith(u'/'))
        return existing(path);
    if (workingDirectory.isEmpty() || !QDir::isAbsolutePath(workingDirectory))
        return {};
    const QString found = existing(workingDirectory + u'/' + path);
    if (!found.isEmpty())
        return found;
    // git's diff names: a/src/x.h, b/src/x.h.
    if (path.size() > 2 && (path.startsWith(u"a/") || path.startsWith(u"b/")))
        return existing(workingDirectory + u'/' + path.mid(2));
    return {};
}

} // namespace brulion::term
