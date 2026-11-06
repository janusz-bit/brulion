#include "semantic/chunker.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace brulion::semantic {

namespace {

struct Part {
    QString text;
    int line = 0;
    int words = 0;
};

int countWords(QStringView s)
{
    int n = 0;
    bool in = false;
    for (QChar c : s) {
        const bool space = c.isSpace();
        if (!space && !in)
            ++n;
        in = !space;
    }
    return n;
}

/// Index of the first line after the frontmatter (0 when there is none).
qsizetype bodyStart(const QList<QStringView> &lines)
{
    if (lines.isEmpty() || lines.first().trimmed() != u"---")
        return 0;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() == u"---")
            return i + 1;
    }
    return 0;
}

void splitLong(const Part &p, int maxWords, QList<Part> *out)
{
    const QStringList words = p.text.split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < words.size(); i += maxWords)
        out->append({words.mid(i, maxWords).join(u' '), p.line, int(qMin<qsizetype>(maxWords, words.size() - i))});
}

} // namespace

QList<Chunk> chunkNote(QStringView title, QStringView content, int maxWords)
{
    maxWords = qMax(1, maxWords);
    QList<QStringView> lines;
    for (QStringView l : content.tokenize(u'\n'))
        lines.append(l.endsWith(u'\r') ? l.chopped(1) : l);

    // Parts: paragraphs, headings and top-level bullets.
    QList<Part> parts;
    Part cur;
    auto flush = [&] {
        cur.text = cur.text.trimmed();
        if (!cur.text.isEmpty()) {
            cur.words = countWords(cur.text);
            if (cur.words > maxWords)
                splitLong(cur, maxWords, &parts);
            else
                parts.append(cur);
        }
        cur = Part{};
    };
    for (qsizetype i = bodyStart(lines); i < lines.size(); ++i) {
        const QStringView l = lines.at(i);
        if (l.trimmed().isEmpty()) {
            flush();
            continue;
        }
        if (l.startsWith(u'#') || l.startsWith(u"- "))
            flush();
        if (cur.text.isEmpty())
            cur.line = int(i);
        else
            cur.text += u'\n';
        cur.text += l;
    }
    flush();

    // Chunks: consecutive parts while they fit.
    QList<Chunk> chunks;
    const QString head = title.toString() + u'\n';
    QString text;
    int words = 0, line = 0;
    for (const Part &p : std::as_const(parts)) {
        if (!text.isEmpty() && words + p.words > maxWords) {
            chunks.append({head + text, line});
            text.clear();
            words = 0;
        }
        if (text.isEmpty())
            line = p.line;
        else
            text += u' ';
        text += p.text;
        words += p.words;
    }
    if (!text.isEmpty() || chunks.isEmpty())
        chunks.append({head + text, text.isEmpty() ? 0 : line});
    return chunks;
}

bool looksEnglish(QStringView text)
{
    static const QSet<QString> pl = {u"się"_s, u"jest"_s, u"nie"_s, u"że"_s, u"już"_s, u"jak"_s,
                                     u"przez"_s, u"oraz"_s, u"dla"_s};
    static const QSet<QString> en = {u"the"_s, u"and"_s, u"is"_s, u"with"_s, u"for"_s, u"that"_s,
                                     u"this"_s, u"to"_s, u"of"_s, u"was"_s, u"have"_s};
    static const QString plLetters = u"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ"_s;
    int p = 0, e = 0;
    for (QChar c : text) {
        if (plLetters.contains(c))
            ++p;
    }
    qsizetype start = -1;
    for (qsizetype i = 0; i <= text.size(); ++i) {
        const bool letter = i < text.size() && text.at(i).isLetter();
        if (letter && start < 0)
            start = i;
        else if (!letter && start >= 0) {
            const QString w = text.mid(start, i - start).toString().toLower();
            p += pl.contains(w);
            e += en.contains(w);
            start = -1;
        }
    }
    return e > p;
}

} // namespace brulion::semantic
