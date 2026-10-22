// Shared helpers of the io tests: input files built in code, a tiny tar writer, lookups in
// import results and fixed options (time zone Europe/Warsaw, never the system clock).
#pragma once

#include "io/htmlrender_p.h"
#include "io/importer.h"
#include "io/zip.h"
#include "markdown/blockdocument.h"
#include "markdown/inlinerenderer.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QTextDocument>
#include <QTimeZone>

#include <algorithm>

namespace iotest {

using namespace Qt::StringLiterals;

inline brulion::io::InputFile text(const QString &path, const QString &content)
{
    return brulion::io::InputFile{path, content.toUtf8()};
}

inline brulion::io::InputFile bytes(const QString &path, const QByteArray &data)
{
    return brulion::io::InputFile{path, data};
}

/// A zip input file built from entries.
inline brulion::io::InputFile zip(const QString &path, const QList<brulion::io::FileEntry> &entries)
{
    return brulion::io::InputFile{path, brulion::io::writeZip(entries)};
}

inline QTimeZone warsaw()
{
    return QTimeZone("Europe/Warsaw");
}

inline brulion::io::ImportOptions options(const QString &targetFolder = {})
{
    brulion::io::ImportOptions o;
    o.targetFolder = targetFolder;
    o.timeZone = warsaw();
    return o;
}

/// Paths of an import result, sorted.
inline QStringList pathsOf(const brulion::io::ImportResult &r)
{
    QStringList out;
    for (const auto &f : r.files)
        out.append(f.path);
    out.sort();
    return out;
}

inline const brulion::io::ImportedFile *fileAt(const brulion::io::ImportResult &r, const QString &path)
{
    for (const auto &f : r.files) {
        if (f.path == path)
            return &f;
    }
    return nullptr;
}

/// Text of a file of an import result ("<missing>" when absent).
inline QString textAt(const brulion::io::ImportResult &r, const QString &path)
{
    const auto *f = fileAt(r, path);
    return f ? QString::fromUtf8(f->data) : u"<missing: "_s + path + u"> in "_s + pathsOf(r).join(u", "_s);
}

/// Entry of a file list (zip contents) by path.
inline QByteArray entryData(const QList<brulion::io::FileEntry> &entries, const QString &path)
{
    for (const auto &e : entries) {
        if (e.path == path)
            return e.data;
    }
    return QByteArray("<missing>");
}

/// What a note shows, block by block (frontmatter left out): each block's text rendered by the
/// editor's InlineRenderer (renderBlock(), as the block editor does) or, with `exported`, by the
/// HTML export's renderer, as plain text (table cells one by one, joined by " | "). A formula
/// shows as rich text without its dollars ("$x$" -> "x").
inline QString shownText(const QString &note, bool exported = false)
{
    const auto shown = [exported](QStringView markdown, brulion::BlockKind kind) {
        QTextDocument rich;
        rich.setHtml(exported ? brulion::io::detail::HtmlRenderer().renderInline(markdown)
                              : brulion::markdown::InlineRenderer().renderBlock(markdown, kind));
        return rich.toPlainText().replace(QChar::Nbsp, u' ');
    };
    const brulion::markdown::BlockDocument doc(note);
    QStringList out;
    for (int i = 0; i < doc.count(); ++i) {
        const brulion::markdown::BlockEntry &e = doc.at(i);
        if (e.kind == brulion::BlockKind::Frontmatter)
            continue;
        if (e.kind != brulion::BlockKind::Table) {
            out.append(shown(e.body, e.kind));
            continue;
        }
        for (const QString &row : e.body.split(u'\n')) {
            if (std::all_of(row.cbegin(), row.cend(), [](QChar c) { return QStringView(u"|-: \t").contains(c); }))
                continue; // the delimiter row
            QStringList cells;
            qsizetype from = row.startsWith(u'|') ? 1 : 0;
            for (qsizetype k = from; k <= row.size(); ++k) {
                if (k == row.size() || (row[k] == u'|' && (k == 0 || row[k - 1] != u'\\'))) {
                    if (k > from || k < row.size())
                        cells.append(shown(QStringView(row).sliced(from, k - from).trimmed(),
                                           brulion::BlockKind::Paragraph));
                    from = k + 1;
                }
            }
            out.append(cells.join(u" | "_s));
        }
    }
    return out.join(u'\n');
}

inline QStringList entryPaths(const QList<brulion::io::FileEntry> &entries)
{
    QStringList out;
    for (const auto &e : entries)
        out.append(e.path);
    out.sort();
    return out;
}

/// One member of a tar archive written by tarArchive().
struct TarMember {
    QByteArray name;
    QByteArray data;
    char type = '0';
    QByteArray prefix; ///< ustar prefix field
};

inline QByteArray tarHeader(const TarMember &m)
{
    QByteArray h(512, '\0');
    const auto put = [&](int offset, const QByteArray &value) {
        for (qsizetype i = 0; i < value.size(); ++i)
            h[offset + i] = value[i];
    };
    put(0, m.name.left(100));
    put(100, "0000644");
    put(108, "0001750");
    put(116, "0001750");
    put(124, QByteArray::number(m.data.size(), 8).rightJustified(11, '0'));
    put(136, "15000000000");
    put(148, "        ");
    h[156] = m.type;
    put(257, QByteArray("ustar\0", 6));
    put(263, "00");
    put(345, m.prefix.left(155));
    int sum = 0;
    for (const char c : std::as_const(h))
        sum += static_cast<unsigned char>(c);
    put(148, QByteArray::number(sum, 8).rightJustified(6, '0') + QByteArray("\0 ", 2));
    return h;
}

/// A ustar archive of `members` (with the two terminating zero blocks).
inline QByteArray tarArchive(const QList<TarMember> &members)
{
    QByteArray out;
    for (const TarMember &m : members) {
        out += tarHeader(m);
        out += m.data;
        if (m.data.size() % 512)
            out += QByteArray(512 - m.data.size() % 512, '\0');
    }
    out += QByteArray(1024, '\0');
    return out;
}

} // namespace iotest
