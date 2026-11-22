#include "semantic/notes.h"

#include "markdown/encrypted.h"
#include "semantic/vectorindex.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace brulion::semantic {

QList<Chunk> noteChunks(QStringView title, QStringView content)
{
    QList<Chunk> chunks = markdown::mayContainEncrypted(content)
                                  ? chunkNote(title, markdown::maskEncryptedFragments(content))
                                  : chunkNote(title, content);
    if (chunks.size() > MaxChunksPerNote)
        chunks.resize(MaxChunksPerNote);
    return chunks;
}

QString passageExcerpt(QStringView content, int line, qsizetype chars)
{
    static const QRegularExpression marker(QStringLiteral("^(?:#{1,6}\\s+|>\\s*|[-*+]\\s+(?:\\[.\\]\\s+)?|\\d+[.)]\\s+)"));
    const QString masked = markdown::mayContainEncrypted(content) ? markdown::maskEncryptedFragments(content)
                                                                  : content.toString();
    QString out;
    int at = 0;
    for (QStringView l : QStringView(masked).tokenize(u'\n')) {
        if (at++ < line)
            continue;
        QString t = l.trimmed().toString();
        if (t.isEmpty() || t.startsWith(u"```") || t.startsWith(u"~~~"))
            continue;
        // (Repeated: a quoted list item, a task in a quote.)
        for (int i = 0; i < 3; ++i) {
            const QRegularExpressionMatch m = marker.match(t);
            if (!m.hasMatch() || m.capturedLength() == 0)
                break;
            t = t.mid(m.capturedLength()).trimmed();
        }
        if (t.isEmpty())
            continue;
        if (!out.isEmpty())
            out += u' ';
        out += t;
        if (out.size() >= chars)
            break;
    }
    return out;
}

quint64 noteKey(QStringView title, QStringView content)
{
    quint64 h = 14695981039346656037ULL;
    const auto add = [&h](QStringView text) {
        for (const QChar c : text) {
            const char16_t u = c.unicode();
            h ^= quint64(u & 0xff);
            h *= 1099511628211ULL;
            h ^= quint64(u >> 8);
            h *= 1099511628211ULL;
        }
    };
    add(title);
    // (A separator no title has — a file name: "ab" + "c" is not "a" + "bc".)
    add(u"\n");
    add(content);
    return h == 0 ? 1 : h;
}

IndexDiff diffIndex(const QList<std::pair<QString, quint64>> &notes, const VectorIndex &index)
{
    IndexDiff diff;
    QSet<QString> present;
    present.reserve(notes.size());
    for (const auto &[id, key] : notes) {
        present.insert(id);
        if (index.key(id) != key || !index.contains(id))
            diff.embed.append(id);
    }
    for (const QString &id : index.ids()) {
        if (!present.contains(id))
            diff.remove.append(id);
    }
    std::ranges::sort(diff.remove);
    return diff;
}

} // namespace brulion::semantic
