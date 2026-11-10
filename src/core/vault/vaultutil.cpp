#include "vault/vaultutil_p.h"

#include "util/paths.h"
#include "vault/vaultconfig.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QThread>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::vaultutil {

QString configFolder()
{
    return u".brulion"_s;
}

QStringList ancestors(const VaultPath &path)
{
    QStringList out;
    for (qsizetype i = path.indexOf(u'/'); i >= 0; i = path.indexOf(u'/', i + 1))
        out.append(path.left(i));
    return out;
}

bool isWithin(const VaultPath &path, const VaultPath &dir)
{
    return dir.isEmpty() || path == dir || isStrictlyWithin(path, dir);
}

bool isStrictlyWithin(const VaultPath &path, const VaultPath &dir)
{
    if (dir.isEmpty())
        return !path.isEmpty();
    return path.size() > dir.size() && path.at(dir.size()) == u'/' && path.startsWith(dir);
}

VaultPath rebase(const VaultPath &path, const VaultPath &from, const VaultPath &to)
{
    if (path == from)
        return to;
    const QString rest = from.isEmpty() ? path : path.mid(from.size() + 1);
    if (to.isEmpty())
        return rest;
    return to + u'/' + rest;
}

VaultPath insertBeforeExtension(const VaultPath &path, const QString &suffix, bool isDir)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    const qsizetype dot = path.lastIndexOf(u'.');
    if (isDir || dot <= slash + 1)
        return path + suffix;
    return path.left(dot) + suffix + path.mid(dot);
}

VaultPath uniqueName(const VaultPath &base, const std::function<bool(const VaultPath &)> &isTaken, bool isDir)
{
    if (!isTaken(base))
        return base;
    for (int i = 2;; ++i) {
        const VaultPath candidate = insertBeforeExtension(base, u' ' + QString::number(i), isDir);
        if (!isTaken(candidate))
            return candidate;
    }
}

QString trashStamp(const QDateTime &time)
{
    return time.toString(u"yyyyMMdd-HHmmss"_s);
}

QString stripTrashStamp(const QString &segment)
{
    static const QRegularExpression stamp(u" \\(\\d{8}-\\d{6}\\)(?: \\d+)?(?=\\.[^.]*\\z|\\z)"_s);
    return QString(segment).remove(stamp);
}

bool isTextPath(const VaultPath &path)
{
    static const QSet<QString> textExtensions = {
            u".md"_s,   u".markdown"_s, u".txt"_s, u".text"_s, u".json"_s, u".canvas"_s, u".excalidraw"_s, u".base"_s,
            u".csv"_s,  u".tsv"_s,      u".css"_s, u".html"_s, u".htm"_s,  u".xml"_s,    u".svg"_s,        u".yml"_s,
            u".yaml"_s, u".toml"_s,     u".ini"_s, u".ics"_s,  u".opml"_s, u".org"_s,    u".tex"_s,        u".rst"_s,
            u".adoc"_s, u".log"_s,      u".js"_s,  u".mjs"_s,  u".ts"_s,   u".edn"_s,
    };
    return textExtensions.contains(paths::extension(path));
}

bool isTracked(const VaultPath &path)
{
    return !path.isEmpty() && (path == vaultConfigPath() || !paths::isIgnored(path));
}

QByteArray utf8Bom()
{
    return QByteArrayLiteral("\xEF\xBB\xBF");
}

bool saveAtomically(const QString &absolutePath, QByteArrayView bytes, QString *error)
{
    constexpr int kAttempts = 4;
    for (int attempt = 1;; ++attempt) {
        QSaveFile file(absolutePath);
        const bool written = file.open(QIODevice::WriteOnly) && file.write(bytes.data(), bytes.size()) == bytes.size();
        if (written && file.commit())
            return true;
        if (!written || file.error() != QFileDevice::RenameError || attempt == kAttempts) {
            if (error)
                *error = file.errorString();
            return false;
        }
        QThread::msleep(15 * attempt); // 15 + 30 + 45 ms
    }
}

QString decodeUtf8(const QByteArray &bytes)
{
    if (bytes.startsWith(utf8Bom()))
        return QString::fromUtf8(QByteArrayView(bytes).sliced(3));
    return QString::fromUtf8(bytes);
}

ContentRead readContent(const QString &absolutePath, qint64 limit)
{
    ContentRead out;
    // QFile cannot open without following a link (no O_NOFOLLOW): the check comes first.
    if (QFileInfo(absolutePath).isSymLink()) {
        out.status = ContentRead::Status::Link;
        return out;
    }
    QFile file(absolutePath);
    const auto failed = [&] {
        out.error = file.errorString();
        return out;
    };
    if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
        return failed();
    const qint64 size = file.size();
    if (size > limit) {
        out.status = ContentRead::Status::TooBig;
        return out;
    }
    QByteArray bytes;
    if (size > 0) {
        bytes = QByteArray(size, Qt::Uninitialized);
        const qint64 got = file.read(bytes.data(), size);
        if (got < 0)
            return failed();
        bytes.truncate(got); // shortened meanwhile
    } else {
        char piece[16 * 1024];
        for (;;) {
            const qint64 got = file.read(piece, std::min<qint64>(sizeof piece, limit + 1 - bytes.size()));
            if (got < 0)
                return failed();
            if (got == 0)
                break;
            if (bytes.size() + got > limit) {
                out.status = ContentRead::Status::TooBig;
                return out;
            }
            // Grown by doubling, but never past the limit (append() would round up beyond it).
            if (bytes.capacity() - bytes.size() < got)
                bytes.reserve(std::min<qint64>(limit, std::max<qint64>(2 * bytes.capacity(), bytes.size() + got)));
            bytes.append(piece, got);
        }
    }
    out.status = ContentRead::Status::Ok;
    out.bytes = std::move(bytes);
    return out;
}

} // namespace brulion::vaultutil
