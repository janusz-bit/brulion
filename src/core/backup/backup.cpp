#include "backup/backup.h"

#include "util/paths.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryFile>

#include <algorithm>

namespace brulion::backup {

using namespace Qt::StringLiterals;

namespace {

const QString StampFormat = u"yyyy-MM-dd-HHmmss"_s;

/// The nearest existing folder at or above `path`, canonical, plus what does not exist yet.
QString canonicalAsFarAsItExists(const QString &path)
{
    QString existing = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QString rest;
    while (!QFileInfo::exists(existing)) {
        const qsizetype slash = existing.lastIndexOf(u'/');
        if (slash <= 0)
            return QDir::cleanPath(path);
        rest.prepend(existing.sliced(slash));
        existing.truncate(slash);
    }
    return QFileInfo(existing).canonicalFilePath() + rest;
}

/// More than any comment Brulion writes.
constexpr qint64 ManifestTailBytes = 4096;
constexpr qsizetype EndOfDirectorySize = 22;

} // namespace

QByteArray backupManifest(const QString &notebookId, bool complete, int files)
{
    return QJsonDocument(QJsonObject{{u"brulionBackup"_s, 1},
                                     {u"notebook"_s, notebookId},
                                     {u"complete"_s, complete},
                                     {u"files"_s, files}})
            .toJson(QJsonDocument::Compact);
}

BackupManifest readBackupManifest(const QString &path)
{
    BackupManifest out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return out;
    const qint64 start = std::max<qint64>(0, file.size() - ManifestTailBytes);
    if (!file.seek(start))
        return out;
    const QByteArray tail = file.read(ManifestTailBytes);
    // The end-of-directory record whose comment reaches exactly to the end of the file.
    static const QByteArray signature("PK\x05\x06", 4);
    for (qsizetype at = tail.lastIndexOf(signature); at >= 0; at = at > 0 ? tail.lastIndexOf(signature, at - 1) : -1) {
        if (at + EndOfDirectorySize > tail.size())
            continue;
        const auto low = static_cast<unsigned char>(tail.at(at + 20));
        const auto high = static_cast<unsigned char>(tail.at(at + 21));
        const qsizetype length = qsizetype(low) | (qsizetype(high) << 8);
        if (at + EndOfDirectorySize + length != tail.size())
            continue;
        const QJsonObject json = QJsonDocument::fromJson(tail.sliced(at + EndOfDirectorySize)).object();
        if (json.value(u"brulionBackup"_s).toInt() == 1) {
            out.valid = true;
            out.notebookId = json.value(u"notebook"_s).toString();
            out.complete = json.value(u"complete"_s).toBool();
            out.files = json.value(u"files"_s).toInt();
        }
        return out;
    }
    return out;
}

QString notebookFilePart(const QString &notebookName)
{
    QString part = paths::sanitizeFileName(notebookName);
    while (part.endsWith(u'.'))
        part.chop(1);
    return part.isEmpty() ? u"Untitled"_s : part;
}

QString backupFileName(const QString &notebookName, const QDateTime &now)
{
    return u"Brulion-"_s + notebookFilePart(notebookName) + u'-' + now.toString(StampFormat) + u".zip"_s;
}

std::optional<QDateTime> backupFileTime(const QString &fileName, const QString &notebookName)
{
    static const QRegularExpression stampShape(u"^[0-9]{4}-[0-9]{2}-[0-9]{2}-[0-9]{6}$"_s);
    const QString name = fileName.normalized(QString::NormalizationForm_C);
    const QString prefix = u"Brulion-"_s + notebookFilePart(notebookName) + u'-';
    if (!name.startsWith(prefix) || !name.endsWith(u".zip"_s))
        return std::nullopt;
    const QString stamp = name.sliced(prefix.size(), name.size() - prefix.size() - 4);
    if (!stampShape.match(stamp).hasMatch())
        return std::nullopt;
    const QDateTime time = QDateTime::fromString(stamp, StampFormat);
    if (!time.isValid())
        return std::nullopt;
    return time;
}

QString freeBackupPath(const QString &folder, const QString &notebookName, const QDateTime &now)
{
    const QDir dir(folder);
    QDateTime time = now;
    QString path = dir.filePath(backupFileName(notebookName, time));
    while (QFileInfo::exists(path)) {
        time = time.addSecs(1);
        path = dir.filePath(backupFileName(notebookName, time));
    }
    return path;
}

QList<BackupFile> listBackups(const QString &folder, const QString &notebookName)
{
    QList<BackupFile> out;
    const QDir dir(folder);
    const QFileInfoList entries =
            dir.entryInfoList({u"Brulion-*.zip"_s}, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : entries) {
        if (info.isSymLink())
            continue;
        if (const std::optional<QDateTime> time = backupFileTime(info.fileName(), notebookName)) {
            const BackupManifest manifest = readBackupManifest(info.absoluteFilePath());
            out.append(BackupFile{info.fileName(), info.absoluteFilePath(), *time, info.size(),
                                  manifest.valid ? manifest.notebookId : QString(),
                                  manifest.valid && manifest.complete});
        }
    }
    std::ranges::stable_sort(out, [](const BackupFile &a, const BackupFile &b) { return a.time > b.time; });
    return out;
}

QStringList backupsToPrune(const QList<BackupFile> &backups, int keep, const QString &notebookId,
                           const QSet<QString> &protect, bool pruneComplete)
{
    QStringList out;
    if (notebookId.isEmpty())
        return out;
    const int limit = std::max(keep, MinKeep);
    qsizetype keptComplete = 0;
    qsizetype keptIncomplete = 0;
    for (const BackupFile &file : backups) {
        if (file.notebookId == notebookId && protect.contains(file.fileName))
            ++(file.complete ? keptComplete : keptIncomplete);
    }
    for (const BackupFile &file : backups) {
        if (file.notebookId != notebookId || protect.contains(file.fileName))
            continue;
        qsizetype &kept = file.complete ? keptComplete : keptIncomplete;
        if (kept < limit)
            ++kept;
        else if (pruneComplete || !file.complete)
            out.append(file.fileName);
    }
    return out;
}

QStringList pruneBackups(const QString &folder, const QString &notebookName, int keep, const QString &notebookId,
                         const QSet<QString> &protect, bool pruneComplete, QStringList *failed)
{
    QStringList removed;
    const QDir dir(folder);
    for (const QString &name :
         backupsToPrune(listBackups(folder, notebookName), keep, notebookId, protect, pruneComplete)) {
        if (QFile::remove(dir.filePath(name)))
            removed.append(name);
        else if (failed)
            failed->append(name);
    }
    return removed;
}

int removeStalePartials(const QString &folder, const QString &notebookName, const QDateTime &now)
{
    static const QRegularExpression suffix(u"\\.zip\\.[A-Za-z0-9]{6}$"_s);
    int removed = 0;
    const QDir dir(folder);
    const QFileInfoList entries = dir.entryInfoList({u"Brulion-*.zip.*"_s}, QDir::Files | QDir::Hidden);
    for (const QFileInfo &info : entries) {
        const QRegularExpressionMatch match = suffix.match(info.fileName());
        if (!match.hasMatch() || info.isSymLink())
            continue;
        const QString backupName = info.fileName().first(match.capturedStart()) + u".zip"_s;
        if (backupFileTime(backupName, notebookName) && info.lastModified().secsTo(now) > 24 * 3600
            && QFile::remove(info.absoluteFilePath()))
            ++removed;
    }
    return removed;
}

bool isInsideFolder(const QString &path, const QString &folder)
{
    const QString inner = canonicalAsFarAsItExists(path);
    const QString outer = canonicalAsFarAsItExists(folder);
    return inner == outer || inner.startsWith(outer.endsWith(u'/') ? outer : outer + u'/');
}

bool checkFolderWritable(const QString &folder, QString *error)
{
    if (!QFileInfo(folder).isDir()) {
        if (error)
            *error = u"no such folder"_s;
        return false;
    }
    QTemporaryFile probe(QDir(folder).filePath(u".brulion-backup-probe-XXXXXX"_s));
    if (!probe.open()) {
        if (error)
            *error = probe.errorString();
        return false;
    }
    return true;
}

bool isCaseInsensitiveFolder(const QString &folder)
{
    QTemporaryFile probe(QDir(folder).filePath(u".brulion-tmp-CaseProbe-XXXXXX"_s));
    if (!probe.open())
        return false;
    const QFileInfo info(probe.fileName());
    return QFileInfo::exists(info.dir().filePath(info.fileName().toLower()))
           && QFileInfo::exists(info.dir().filePath(info.fileName().toUpper()));
}

qsizetype Sources::notebookFileCount() const
{
    return std::count_if(files.cbegin(), files.cend(), [](const SourceFile &f) { return f.path != vaultConfigPath(); });
}

Sources collectSources(const Vault &vault)
{
    Sources out;
    QList<FileStat> files = vault.files();
    if (const std::optional<FileStat> config = vault.stat(vaultConfigPath()); config && !config->isDir)
        files.append(*config);
    std::ranges::sort(files, [](const FileStat &a, const FileStat &b) { return a.path < b.path; });
    out.files.reserve(files.size());
    QSet<QString> nonEmpty;
    const auto markParents = [&nonEmpty](const QString &path) {
        for (qsizetype slash = path.lastIndexOf(u'/'); slash > 0; slash = path.lastIndexOf(u'/', slash - 1)) {
            const QString parent = path.first(slash);
            if (nonEmpty.contains(parent))
                break; // and so are the folders above it
            nonEmpty.insert(parent);
        }
    };
    for (const FileStat &f : std::as_const(files)) {
        out.files.append(SourceFile{f.path, vault.absolutePath(f.path), f.size, f.mtime});
        out.totalBytes += f.size;
        markParents(f.path);
    }
    const QList<FileStat> folders = vault.folders();
    for (const FileStat &f : folders)
        markParents(f.path);
    for (const FileStat &f : folders) {
        if (!nonEmpty.contains(f.path) && !paths::isIgnored(f.path))
            out.emptyFolders.append(f.path);
    }
    out.emptyFolders.sort();
    return out;
}

QString fingerprint(const Sources &sources)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const SourceFile &f : sources.files) {
        hash.addData(f.path.toUtf8());
        const QByteArray stat = '\0' + QByteArray::number(f.size) + '\0' + QByteArray::number(f.mtime) + '\n';
        hash.addData(stat);
    }
    for (const QString &folder : sources.emptyFolders) {
        hash.addData(folder.toUtf8());
        hash.addData(QByteArrayView("/\n"));
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool isDue(Interval interval, const QDateTime &last, const QDateTime &now)
{
    if (!last.isValid() || last > now)
        return true;
    const qint64 days = last.date().daysTo(now.date());
    return interval == Interval::Daily ? days >= 1 : days >= 7;
}

Decision decide(const Settings &settings, const ScheduleState &state, const QString &fingerprint, qsizetype fileCount,
                const QDateTime &now)
{
    if (!settings.enabled)
        return Decision::Disabled;
    if (!isDue(settings.interval, state.lastBackup, now))
        return Decision::NotDue;
    if (fileCount == 0)
        return Decision::Empty;
    if (state.lastFileExists && !state.lastFingerprint.isEmpty() && state.lastFingerprint == fingerprint)
        return Decision::Unchanged;
    return Decision::Run;
}

} // namespace brulion::backup
