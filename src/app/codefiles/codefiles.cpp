#include "codefiles.h"

#include "codefileindex.h"

#include "codefiles/viewer.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::codefiles {

CodeFiles::CodeFiles(QObject *parent) : QObject(parent) { }

QVariantMap CodeFiles::linkTarget(const QString &folder, const QString &href) const
{
    if (folder.isEmpty() || href.isEmpty())
        return {};
    QString target = href;
    // The chat's links: "page:<percent-encoded target>[#fragment]" (InlineRenderer's).
    if (target.startsWith(u"page:"_s))
        target = target.mid(5);
    else if (target.startsWith(u"ext:"_s) || target.startsWith(u"tag:"_s) || target.startsWith(u"block:"_s)
             || target.startsWith(u"task:"_s) || target.startsWith(u"part:"_s))
        return {};
    const std::optional<FileLink> link = parseFileLink(target);
    if (!link)
        return {};
    const QString relative = relativeInside(folder, link->path);
    if (relative.isEmpty())
        return {};
    const QFileInfo info(QDir(folder).absolutePath() + u'/' + relative);
    if (!info.exists())
        return {};
    return {{u"path"_s, relative},
            {u"line"_s, link->line},
            {u"column"_s, link->column},
            {u"endLine"_s, link->endLine},
            {u"isFolder"_s, info.isDir()}};
}

QString CodeFiles::linkPath(const QString &folder, const QString &href) const
{
    return linkTarget(folder, href).value(u"path"_s).toString();
}

int CodeFiles::linkLine(const QString &folder, const QString &href) const
{
    return linkTarget(folder, href).value(u"line"_s).toInt();
}

QString CodeFiles::absolutePath(const QString &folder, const QString &path) const
{
    const QString relative = relativeInside(folder, path);
    return relative.isEmpty() ? QString() : QDir::cleanPath(QDir(folder).absolutePath() + u'/' + relative);
}

QString CodeFiles::relativePath(const QString &folder, const QString &path) const
{
    return relativeInside(folder, path);
}

QStringList CodeFiles::relativePaths(const QString &folder, const QStringList &paths) const
{
    QStringList out;
    for (const QString &p : paths) {
        const QString relative = relativeInside(folder, p);
        if (!relative.isEmpty() && !out.contains(relative))
            out.append(relative);
    }
    return out;
}

int CodeFiles::mentionStart(const QString &text, int caret) const
{
    return mentionAt(text, caret).start;
}

QString CodeFiles::mentionQuery(const QString &text, int caret) const
{
    return mentionAt(text, caret).query;
}

QString CodeFiles::withMention(const QString &text, int caret, const QString &path) const
{
    return codefiles::withMention(text, caret, path);
}

int CodeFiles::mentionEnd(const QString &text, int caret, const QString &path) const
{
    return codefiles::mentionEnd(text, caret, path);
}

QString CodeFiles::reviewComment(const QString &path, int from, int to, const QString &lines,
                                 const QString &comment) const
{
    return codefiles::reviewComment(path, from, to, lines, comment);
}

QString CodeFiles::languageOf(const QString &path) const
{
    return languageForPath(path);
}

QString CodeFiles::iconOf(const QString &path, bool folder) const
{
    return fileIcon(path, folder);
}

QStringList CodeFiles::withRecent(const QStringList &recent, const QString &path, int max) const
{
    QStringList out;
    if (!path.isEmpty())
        out.append(path);
    for (const QString &p : recent) {
        if (out.size() >= max)
            break;
        if (!p.isEmpty() && !out.contains(p))
            out.append(p);
    }
    while (out.size() > max)
        out.removeLast();
    return out;
}

QString CodeFiles::recentKey(const QString &folder) const
{
    const QByteArray hash =
            QCryptographicHash::hash(QDir::cleanPath(folder).toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return u"code/recentFiles/"_s + QString::fromLatin1(hash);
}

void CodeFiles::refresh(const QString &folder) const
{
    if (!folder.isEmpty())
        FileIndexCache::instance()->refresh(folder);
}

bool CodeFiles::copyText(const QString &text) const
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    clipboard->setText(text);
    return true;
}

QString CodeFiles::sizeText(qint64 bytes) const
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

} // namespace brulion::codefiles
