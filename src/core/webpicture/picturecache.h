// PictureCache — pictures from the internet kept on the device (webpicture.h), one file per URL
// in a folder of the app's cache: `<sha1 of the URL>.<format>`. The editor shows the cached file
// (a file: URL, the QML engine loads nothing from the network), also offline; "Pobierz do
// notatnika" copies it into the notebook. The cache is the device's, never synced; prune() keeps
// it below a size by dropping the files used longest ago — lookup() marks a file used by its
// modification time (at most once an hour; access times are often not kept, Android's noatime).
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QUrl>

namespace brulion::webpicture {

/// Default size the cache is pruned to.
inline constexpr qint64 DefaultCacheBytes = 200 * 1024 * 1024;
/// A file used again within this many seconds keeps its time (no disk write for every repaint).
inline constexpr qint64 TouchIntervalSecs = 60 * 60;

class PictureCache {
public:
    /// A cache in the folder `dir` (created on the first store()).
    explicit PictureCache(QString dir = {});

    QString dir() const { return m_dir; }
    void setDir(const QString &dir);

    /// The cached file of `url` (an absolute path), "" when there is none. Marks the file used:
    /// its modification time is set to now when it is older than TouchIntervalSecs.
    QString lookup(const QUrl &url) const;
    /// `url` has a cached file (not marked used).
    bool contains(const QUrl &url) const { return !find(url).isEmpty(); }
    /// Keep `data` (a picture in `format`, sniffFormat()) as the file of `url`; returns its path,
    /// "" when it could not be written.
    QString store(const QUrl &url, const QByteArray &data, const QString &format);
    /// The cached bytes of `url` (empty when none).
    QByteArray read(const QUrl &url) const;
    /// Forget `url` (its file is removed).
    void remove(const QUrl &url);
    /// Drop the files used longest ago until the cache holds at most `maxBytes`.
    void prune(qint64 maxBytes = DefaultCacheBytes);

    /// The file name (without folder) a URL is cached under, without the extension.
    static QString keyOf(const QUrl &url);

private:
    QString m_dir;
    /// key → file name, filled from the folder on the first lookup.
    mutable QHash<QString, QString> m_files;
    mutable bool m_scanned = false;
    void scan() const;
    QString find(const QUrl &url) const;
};

} // namespace brulion::webpicture
