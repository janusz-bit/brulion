#include "vault/vaultutil_p.h"

#include "util/paths.h"
#include "vault/filesync.h"
#include "vault/vaultconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QTimeZone>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::vaultutil {

namespace {
std::atomic<FileSync> g_fileSync{nullptr};
std::atomic<FileReplace> g_fileReplace{nullptr};
std::atomic<StampFilter> g_stampFilter{nullptr};
} // namespace

void setFileSync(FileSync sync)
{
    g_fileSync.store(sync);
}

FileSync fileSync()
{
    return g_fileSync.load();
}

void setFileReplace(FileReplace replace)
{
    g_fileReplace.store(replace);
}

FileReplace fileReplace()
{
    return g_fileReplace.load();
}

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

namespace {

std::filesystem::path fsPath(const QString &absolutePath)
{
    const QByteArray utf8 = absolutePath.toUtf8();
    return std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t *>(utf8.constData()), static_cast<size_t>(utf8.size())));
}

/// std::filesystem::rename(): the rename when the platform sets none (filesync.h).
Replaced replaceWithStd(const QString &from, const QString &to, QString *error)
{
    std::error_code code;
    std::filesystem::rename(fsPath(from), fsPath(to), code);
    if (!code)
        return Replaced::Done;
    if (error)
        *error = QString::fromStdString(code.message());
    // (Windows' refusals of a held file map to permission_denied in the C++ libraries.)
    return code == std::errc::permission_denied || code == std::errc::device_or_resource_busy ? Replaced::Busy
                                                                                              : Replaced::Failed;
}

qint64 mtimeMsOf(const QFileInfo &info)
{
    return info.fileTime(QFileDevice::FileModificationTime, QTimeZone(QTimeZone::UTC)).toMSecsSinceEpoch();
}

QString changedMeanwhile()
{
    //: The reason in "Cannot write %1: %2"
    return QCoreApplication::translate("brulion::Vault", "another program changed it meanwhile");
}

} // namespace

ReadMark markBeforeRead(const QString &absolutePath)
{
    ReadMark mark;
    const QFileInfo info(absolutePath);
    if (!info.exists() && !info.isSymLink()) {
        mark.absent = true;
    } else {
        mark.size = info.size();
        mark.mtimeMs = mtimeMsOf(info);
        mark.fineMtime = fineMtime(absolutePath);
    }
    mark.since.start();
    return mark;
}

bool stampsVouch(const ReadMark &mark, std::optional<qint64> fine, std::optional<qint64> nowStamp)
{
    // Not racy: the read came at least a tick after the file's last change. In the file system's
    // clock, now (the stamp of a file just written) minus the file's stamp exceeds two ticks plus
    // the time since the mark (an upper bound of the time from the read to now).
    const qint64 since = mark.since.isValid() ? mark.since.nsecsElapsed() : std::numeric_limits<qint64>::max() / 2;
    return fine && nowStamp && fineStampAge(*nowStamp, *fine) >= kRacyNs + since;
}

bool stillAsRead(const QString &absolutePath, const ReadMark &mark, std::optional<qint64> nowStamp)
{
    const QFileInfo info(absolutePath);
    if (mark.absent)
        return !info.exists() && !info.isSymLink();
    if (info.isSymLink() || !info.isFile() || info.size() != mark.size || mtimeMsOf(info) != mark.mtimeMs)
        return false;
    const std::optional<qint64> fine = fineMtime(absolutePath);
    if (fine != mark.fineMtime)
        return false;
    if (stampsVouch(mark, fine, nowStamp))
        return true;
    // The stamps cannot tell: the bytes decide (this one file's, read again).
    const ContentRead now = readContent(absolutePath, mark.bytes.size());
    return now.ok() && now.bytes == mark.bytes;
}

namespace {

QString ownTempName()
{
    return u".brulion-tmp-%1"_s.arg(QRandomGenerator::global()->generate64(), 16, 16, QChar(u'0'));
}

/// Removes a file of probeFolderClock() — again a moment later when another program holds it (on
/// Windows a virus scanner looking at a new file).
bool removeProbe(const QString &path)
{
    for (const int waitMs : {0, 10, 40}) {
        if (waitMs > 0)
            QThread::msleep(waitMs);
        if (QFile::remove(path))
            return true;
    }
    qCWarning(lcVault).noquote() << "Cannot remove" << path << "(removed later as a leftover)";
    return false;
}

} // namespace

FolderClock probeFolderClock(const QString &absoluteDir, std::optional<qint64> stamp)
{
    // Four changes c1..c4, each right after a look at the stamp. A clock that stamps with its tick
    // gives every change within a tick the tick's stamp. If c1..c4 lie within a span shorter than
    // any tick, at most one tick boundary falls between c1 and c4, so of the three pairs of
    // consecutive changes at least two lie within one tick: the look after the later change of such
    // a pair shows the stamp the earlier one left — whatever another program changed there meanwhile
    // (stamped from the same ticks). A slower attempt proves nothing when every change moved the
    // stamp: made again.
    constexpr int kAttempts = 4;
    constexpr int kChanges = 4;
    const QDir dir(absoluteDir);
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        std::optional<qint64> last = attempt == 0 && stamp ? stamp : fineMtime(absoluteDir);
        if (!last)
            return FolderClock::Unknown;
        QElapsedTimer span;
        span.start();
        QString probe; // the file made by the last change, until removed
        for (int change = 0; change < kChanges; ++change) {
            bool changed = false;
            if (change % 2 == 0) {
                probe = dir.filePath(ownTempName());
                QFile file(probe);
                changed = file.open(QIODevice::WriteOnly | QIODevice::NewOnly); // (closed at once)
                if (!changed)
                    probe.clear();
            } else {
                changed = removeProbe(probe);
                probe.clear();
            }
            const std::optional<qint64> now = changed ? fineMtime(absoluteDir) : std::nullopt;
            if (!now || *now == *last) {
                if (!probe.isEmpty())
                    removeProbe(probe);
                return now ? FolderClock::Coarse : FolderClock::Unknown;
            }
            last = now;
        }
        if (span.nsecsElapsed() < kClockProbeSpanNs)
            return FolderClock::Fine;
    }
    return FolderClock::Unknown;
}

namespace {

/// The rest of saveAtomicallyStamped() and commitStagedStamped() once the temporary file `temp`
/// is there (open for writing; `fill` writes its content): sync, the caller's check, the rename.
/// The temporary file is gone afterwards unless it was renamed.
StampedSave replaceWithTemp(StampedSave out, QFile &temp, const QFileInfo &target, const QString &dir,
                            const FileSync sync, const std::function<bool(QFile &)> &fill,
                            const std::function<void()> &beforeRename, const ReadMark *expect)
{
    const QString absolutePath = target.absoluteFilePath();
    const auto failed = [&](const QString &why) {
        out.error = why;
        temp.close();
        temp.remove();
        out.before = out.afterTemp = out.beforeRename = out.fileMtime = std::nullopt;
        return out;
    };
    if (target.exists())
        temp.setPermissions(target.permissions()); // (QSaveFile keeps them too)
    if (!fill(temp) || !temp.flush())
        return failed(temp.errorString());
    QString syncError;
    if (sync && !sync(temp, &syncError))
        return failed(syncError);
    out.fileMtime = sync ? fineMtime(temp.fileName()) : std::nullopt;
    out.fileSize = temp.size();
    temp.close();
    if (beforeRename)
        beforeRename();
    // A rename refused for a moment (Windows: a virus scanner, an indexer or a sync client holding
    // one of the files) is tried again for 0.3 s (kReplaceWaitsMs); the stamp before it is
    // taken again each time (what changed while waiting is not ours), and so is the caller's check.
    const FileReplace replace = fileReplace() ? fileReplace() : &replaceWithStd;
    Replaced result = Replaced::Failed;
    QString replaceError;
    for (std::size_t attempt = 0;; ++attempt) {
        if (expect && !stillAsRead(absolutePath, *expect, out.fileMtime)) {
            out.changed = true;
            return failed(changedMeanwhile());
        }
        if (sync)
            out.beforeRename = fineMtime(dir);
        replaceError.clear();
        result = replace(temp.fileName(), absolutePath, &replaceError);
        if (result != Replaced::Busy || attempt == std::size(kReplaceWaitsMs))
            break;
        QThread::msleep(kReplaceWaitsMs[attempt]);
    }
    if (result != Replaced::Done)
        return failed(replaceError);
    if (sync) {
        out.after = fineMtime(dir);
        out.fineStamps = out.fileMtime.has_value();
    } else {
        out.fineStamps = fineMtime(absolutePath).has_value();
    }
    out.ok = true;
    return out;
}

} // namespace

StampedSave saveAtomicallyStamped(const QString &absolutePath, QByteArrayView bytes,
                                  const std::function<void()> &beforeRename, const ReadMark *expect, bool probeClock)
{
    StampedSave out;
    const FileSync sync = fileSync();
    if (!sync) {
        // No sync of its own (filesync.h): QSaveFile's, without stamps. What the caller read is
        // checked before the commit (which syncs, then renames).
        if (beforeRename)
            beforeRename();
        if (expect && !stillAsRead(absolutePath, *expect, std::nullopt)) {
            out.changed = true;
            out.error = changedMeanwhile();
            return out;
        }
        out.ok = saveAtomically(absolutePath, bytes, &out.error);
        out.fineStamps = out.ok && fineMtime(absolutePath).has_value();
        return out;
    }
    const QFileInfo target(absolutePath);
    // A file made read-only is not written over (as QSaveFile refuses it): the rename would.
    if (target.exists() && !target.isWritable()) {
        out.error = QCoreApplication::translate("QSaveFile", "Existing file %1 is not writable").arg(absolutePath);
        return out;
    }
    const QString dir = target.absolutePath();
    out.before = fineMtime(dir);
    if (probeClock)
        out.folderClock = out.before ? probeFolderClock(dir, out.before) : FolderClock::Unknown;
    QFile temp;
    if (!createOwnTemp(dir, temp, &out.error))
        return out;
    out.afterTemp = fineMtime(dir);
    return replaceWithTemp(
            out, temp, target, dir, sync,
            [bytes](QFile &file) { return file.write(bytes.data(), bytes.size()) == bytes.size(); }, beforeRename,
            expect);
}

bool createOwnTemp(const QString &absoluteDir, QFile &file, QString *error)
{
    for (int attempt = 0;; ++attempt) {
        file.setFileName(QDir(absoluteDir).filePath(ownTempName())); // ("E:/" at a drive's root: no "E://")
        if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return true;
        if (attempt >= 8 || !file.exists()) {
            if (error)
                *error = file.errorString();
            return false;
        }
    }
}

StampedSave commitStagedStamped(const QString &absolutePath, const QString &stagedFile,
                                const std::function<void()> &beforeRename, const ReadMark *expect, bool probeClock)
{
    StampedSave out;
    const FileSync sync = fileSync();
    const QFileInfo target(absolutePath);
    QFile staged(stagedFile);
    const QString dir = target.absolutePath();
    if (target.exists() && !target.isWritable()) {
        out.error = QCoreApplication::translate("QSaveFile", "Existing file %1 is not writable").arg(absolutePath);
        staged.remove();
        return out;
    }
    if (sync) {
        out.before = fineMtime(dir);
        if (probeClock)
            out.folderClock = out.before ? probeFolderClock(dir, out.before) : FolderClock::Unknown;
        out.afterTemp = out.before; // (the staged file was there before the save began)
    }
    // Opened for writing (never truncated): syncing needs a writable handle on some systems.
    if (!staged.open(QIODevice::ReadWrite)) {
        out.error = staged.errorString();
        staged.remove();
        return out;
    }
    return replaceWithTemp(out, staged, target, dir, sync, [](QFile &) { return true; }, beforeRename, expect);
}

bool isLeftoverTempName(QStringView name)
{
    static const QRegularExpression own(
            u"^\\.brulion-tmp-(?:[0-9a-f]{16}|[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"
            u"|CaseProbe-[A-Za-z0-9]{6})\\z"_s);
    return name.startsWith(u".brulion-tmp-"_s) && own.matchView(name).hasMatch();
}

bool removeLeftoverTemp(const QString &absolutePath, qint64 nowMs)
{
    // A fresh look (lstat): no link, no folder, our own name only.
    const QFileInfo info(absolutePath);
    if (info.isSymLink() || !info.isFile() || !isLeftoverTempName(info.fileName()))
        return false;
    qint64 newest = std::numeric_limits<qint64>::min();
    for (const QFileDevice::FileTime which :
         {QFileDevice::FileModificationTime, QFileDevice::FileMetadataChangeTime, QFileDevice::FileBirthTime}) {
        const QDateTime time = info.fileTime(which, QTimeZone(QTimeZone::UTC));
        if (time.isValid())
            newest = std::max(newest, time.toMSecsSinceEpoch());
    }
    // (No time at all, or one from the future — another device's clock, a clock set back: kept.)
    if (newest == std::numeric_limits<qint64>::min() || nowMs - newest < kLeftoverAgeMs)
        return false;
    // remove() unlinks the entry: had another program put a link there since the look above, the
    // link would go, never what it points to.
    return QFile::remove(absolutePath);
}

std::optional<qint64> fineMtime(const QString &absolutePath)
{
    // QFileInfo has milliseconds only: std::filesystem reads the nanoseconds.
    const std::filesystem::path path = fsPath(absolutePath);
    std::error_code error;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, error);
    const std::optional<qint64> stamp = error ? std::nullopt : fineStampOf(time.time_since_epoch());
    if (const StampFilter filter = g_stampFilter.load(std::memory_order_relaxed))
        return filter(absolutePath, stamp);
    return stamp;
}

void setStampFilter(StampFilter filter)
{
    g_stampFilter.store(filter);
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
