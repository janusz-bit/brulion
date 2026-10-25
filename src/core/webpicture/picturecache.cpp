#include "picturecache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::webpicture {

PictureCache::PictureCache(QString dir)
    : m_dir(std::move(dir))
{
}

void PictureCache::setDir(const QString &dir)
{
    if (dir == m_dir)
        return;
    m_dir = dir;
    m_files.clear();
    m_scanned = false;
}

QString PictureCache::keyOf(const QUrl &url)
{
    return QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex());
}

void PictureCache::scan() const
{
    if (m_scanned)
        return;
    m_scanned = true;
    if (m_dir.isEmpty())
        return;
    const QStringList names = QDir(m_dir).entryList(QDir::Files);
    for (const QString &name : names) {
        const qsizetype dot = name.indexOf(u'.');
        if (dot == 40) // a SHA-1 in hex, then the format
            m_files.insert(name.left(dot), name);
    }
}

QString PictureCache::lookup(const QUrl &url) const
{
    if (m_dir.isEmpty() || !url.isValid())
        return {};
    scan();
    const QString key = keyOf(url);
    const auto it = m_files.constFind(key);
    if (it == m_files.constEnd())
        return {};
    const QString path = QDir(m_dir).filePath(*it);
    if (!QFileInfo::exists(path)) { // removed behind our back (the system cleaned the cache)
        m_files.remove(key);
        return {};
    }
    return path;
}

QString PictureCache::store(const QUrl &url, const QByteArray &data, const QString &format)
{
    if (m_dir.isEmpty() || !url.isValid() || data.isEmpty() || format.isEmpty())
        return {};
    scan();
    if (!QDir().mkpath(m_dir))
        return {};
    const QString key = keyOf(url);
    const QString name = key + u'.' + format;
    if (const auto old = m_files.constFind(key); old != m_files.constEnd() && *old != name)
        QFile::remove(QDir(m_dir).filePath(*old));
    const QString path = QDir(m_dir).filePath(name);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return {};
    m_files.insert(key, name);
    return path;
}

QByteArray PictureCache::read(const QUrl &url) const
{
    const QString path = lookup(url);
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void PictureCache::remove(const QUrl &url)
{
    const QString path = lookup(url);
    if (!path.isEmpty())
        QFile::remove(path);
    m_files.remove(keyOf(url));
}

void PictureCache::prune(qint64 maxBytes)
{
    if (m_dir.isEmpty())
        return;
    scan();
    QFileInfoList files;
    qint64 total = 0;
    for (const QString &name : std::as_const(m_files)) {
        const QFileInfo info(QDir(m_dir).filePath(name));
        if (info.exists()) {
            files.append(info);
            total += info.size();
        }
    }
    if (total <= maxBytes)
        return;
    // Longest unused first: the last read where the file system keeps it, else the last write.
    const auto used = [](const QFileInfo &f) {
        const QDateTime read = f.lastRead();
        return read.isValid() ? std::max(read, f.lastModified()) : f.lastModified();
    };
    std::sort(files.begin(), files.end(), [&](const QFileInfo &a, const QFileInfo &b) { return used(a) < used(b); });
    for (const QFileInfo &f : std::as_const(files)) {
        if (total <= maxBytes)
            break;
        if (QFile::remove(f.absoluteFilePath())) {
            total -= f.size();
            m_files.remove(f.fileName().left(40));
        }
    }
}

} // namespace brulion::webpicture
