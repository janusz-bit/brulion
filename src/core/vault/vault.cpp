// Vault: construction, cached state, reading, writing and the vault config.
// See vaultmove.cpp (rename, trash) and vaultscan.cpp (listing, diffing, watching).
#include "vault/vault.h"
#include "vault/vault_p.h"

#include "text/text.h"
#include "util/paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTimeZone>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion {
namespace {

QString cleanAbsolute(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString withTrailingSlash(const QString &path)
{
    if (path.endsWith(u'/'))
        return path;
    return path + u'/';
}

QDateTime systemNow()
{
    return QDateTime::currentDateTime();
}

/// Device and mount root of the file system holding `path` ("" when unknown).
QString systemStorageIdentity(const QString &path)
{
    const QStorageInfo info(path);
    if (!info.isValid())
        return {};
    return QString::fromUtf8(info.device()) + u'|' + info.rootPath();
}

/// The on-disk spelling of the segments of `rel` after its first `depth` ones, with the leading
/// "/" ("A'/b'/c'.md", 1 -> "/b'/c'.md"); empty when `rel` has no more segments. Spellings of
/// one path always have the same number of segments, whatever their Unicode normalisation.
QString diskSuffix(const QString &rel, qsizetype depth)
{
    qsizetype pos = -1;
    for (qsizetype i = 0; i < depth; ++i) {
        pos = rel.indexOf(u'/', pos + 1);
        if (pos < 0)
            return {};
    }
    return rel.sliced(pos);
}

/// Text files up to this size are re-read before every overwrite, to catch changes that left
/// size and modification time alone (coarse timestamps: FAT, network mounts).
constexpr qint64 kVerifyBytes = 1024 * 1024;

/// .brulion/config.json larger than this is not read (a settings file is a few hundred bytes).
constexpr qint64 kMaxConfigBytes = 1024 * 1024;

/// `path` equals `dir` or lies inside it (absolute file system paths).
bool fsWithin(const QString &path, const QString &dir)
{
    if (dir.endsWith(u'/'))
        return path.startsWith(dir);
    return path == dir || (path.size() > dir.size() && path.at(dir.size()) == u'/' && path.startsWith(dir));
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Free helpers
// ---------------------------------------------------------------------------------------------

QString changeSourceName(ChangeSource source)
{
    switch (source) {
    case ChangeSource::User: return u"user"_s;
    case ChangeSource::External: return u"external"_s;
    case ChangeSource::Sync: return u"sync"_s;
    case ChangeSource::Import: return u"import"_s;
    case ChangeSource::System: return u"system"_s;
    }
    return u"user"_s;
}

QString vaultTrashFolder()
{
    return u".trash"_s;
}

FileStat statOf(const QFileInfo &info, const VaultPath &path)
{
    FileStat st;
    st.path = path;
    st.isDir = info.isDir();
    st.size = st.isDir ? 0 : info.size();
    st.mtime = info.fileTime(QFileDevice::FileModificationTime, QTimeZone(QTimeZone::UTC)).toMSecsSinceEpoch();
    return st;
}

bool sameStat(const FileStat &a, const FileStat &b)
{
    return a.isDir == b.isDir && (a.isDir || (a.size == b.size && a.mtime == b.mtime));
}

// ---------------------------------------------------------------------------------------------
// ContentCache
// ---------------------------------------------------------------------------------------------

std::optional<QByteArray> ContentCache::get(const VaultPath &path) const
{
    const auto it = m_items.constFind(path);
    if (it == m_items.cend())
        return std::nullopt;
    return it->bytes;
}

void ContentCache::put(const VaultPath &path, const QByteArray &bytes)
{
    remove(path);
    if (bytes.size() > kMaxItemBytes)
        return; // huge files are not worth the memory
    m_items.insert(path, Item{bytes, ++m_tick});
    m_bytes += bytes.size();
    evict();
}

std::optional<QByteArray> ContentCache::take(const VaultPath &path)
{
    const auto it = m_items.find(path);
    if (it == m_items.end())
        return std::nullopt;
    QByteArray bytes = it->bytes;
    m_bytes -= bytes.size();
    m_items.erase(it);
    return bytes;
}

void ContentCache::remove(const VaultPath &path)
{
    take(path);
}

void ContentCache::clear()
{
    m_items.clear();
    m_bytes = 0;
}

void ContentCache::evict()
{
    if (m_bytes <= m_limit)
        return;
    QList<std::pair<quint64, VaultPath>> order;
    order.reserve(m_items.size());
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it)
        order.append({it->tick, it.key()});
    std::sort(order.begin(), order.end());
    for (const auto &entry : std::as_const(order)) {
        if (m_bytes <= m_limit / 4 * 3)
            break;
        remove(entry.second);
    }
}

// ---------------------------------------------------------------------------------------------
// VaultPrivate: errors and paths
// ---------------------------------------------------------------------------------------------

VaultPrivate::VaultPrivate(Vault *owner, const QString &rootPath, const QString &deviceStatePath)
    : q(owner)
    , root(cleanAbsolute(rootPath))
    , rootPrefix(withTrailingSlash(root))
    , statePath(cleanAbsolute(deviceStatePath))
    , clock(systemNow)
    , storageProbe(systemStorageIdentity)
{
    if (!statePath.isEmpty()) {
        history = std::make_unique<History>(statePath + u"/history"_s);
        history->setClock(clock);
    }
    store = new LocalStore(statePath.isEmpty() ? QString() : statePath + u"/store.json"_s, owner);
    batchTimer.setSingleShot(true);
    batchTimer.setInterval(50);
    QObject::connect(&batchTimer, &QTimer::timeout, owner, [this] { processPendingEvents(); });
    QObject::connect(&rescanTimer, &QTimer::timeout, owner, [this] { q->rescan(); });
}

VaultPrivate::~VaultPrivate() = default;

bool VaultPrivate::fail(const QString &message)
{
    error = message;
    return false;
}

bool VaultPrivate::requireOpen()
{
    return isOpen || fail(Vault::tr("The vault is not open"));
}

QString VaultPrivate::diskRel(const VaultPath &path) const
{
    if (diskNames.isEmpty() || path.isEmpty())
        return path;
    // The nearest entry at or above `path` gives the on-disk spelling of that prefix.
    for (VaultPath cur = path;;) {
        const auto it = diskNames.constFind(cur);
        if (it != diskNames.cend()) {
            if (cur.size() == path.size())
                return *it;
            return *it + QStringView(path).sliced(cur.size());
        }
        const qsizetype slash = cur.lastIndexOf(u'/');
        if (slash < 0)
            return path;
        cur.truncate(slash);
    }
}

QString VaultPrivate::absPath(const VaultPath &path) const
{
    return absOfDiskRel(diskRel(path));
}

QString VaultPrivate::absOfDiskRel(const QString &rel) const
{
    if (rel.isEmpty())
        return root;
    return rootPrefix + rel;
}

std::optional<VaultPath> VaultPrivate::vaultPathOfAbs(const QString &absolute) const
{
    if (absolute == root)
        return VaultPath();
    if (!absolute.startsWith(rootPrefix))
        return std::nullopt;
    return paths::normalize(absolute.sliced(rootPrefix.size()));
}

void VaultPrivate::rememberDiskName(const VaultPath &path, const QString &rel)
{
    if (path.isEmpty())
        return;
    diskNames.remove(path);
    if (diskRel(path) != rel)
        diskNames.insert(path, rel);
}

void VaultPrivate::forgetDiskNames(const VaultPath &path)
{
    for (auto it = diskNames.begin(); it != diskNames.end();) {
        if (vaultutil::isWithin(it.key(), path))
            it = diskNames.erase(it);
        else
            ++it;
    }
}

void VaultPrivate::rebaseDiskNames(const VaultPath &from, const VaultPath &to)
{
    if (diskNames.isEmpty())
        return;
    // New vault path -> on-disk spelling of the part below `from` (which moves unchanged).
    // Cut by segments, not by the length of `from`'s spelling: entries below may have been
    // recorded under another spelling of `from`.
    const qsizetype depth = from.count(u'/') + 1;
    QList<std::pair<VaultPath, QString>> moved;
    for (auto it = diskNames.begin(); it != diskNames.end();) {
        if (!vaultutil::isWithin(it.key(), from)) {
            ++it;
            continue;
        }
        // `from` itself gets the new (normalised) name given to the rename.
        if (it.key() != from) {
            if (const QString suffix = diskSuffix(it.value(), depth); !suffix.isEmpty())
                moved.append({vaultutil::rebase(it.key(), from, to), suffix});
        }
        it = diskNames.erase(it);
    }
    std::sort(moved.begin(), moved.end()); // parents before children
    for (const auto &entry : std::as_const(moved))
        rememberDiskName(entry.first, diskRel(to) + entry.second);
}

void VaultPrivate::respell(const VaultPath &path, const QString &rel)
{
    // Entries below keep their own spelling, now below the new spelling of `path`.
    const qsizetype depth = path.count(u'/') + 1;
    QList<std::pair<VaultPath, QString>> below;
    for (auto it = diskNames.begin(); it != diskNames.end();) {
        if (!vaultutil::isStrictlyWithin(it.key(), path)) {
            ++it;
            continue;
        }
        if (const QString suffix = diskSuffix(it.value(), depth); !suffix.isEmpty())
            below.append({it.key(), suffix});
        it = diskNames.erase(it);
    }
    rememberDiskName(path, rel);
    std::sort(below.begin(), below.end()); // parents before children
    for (const auto &entry : std::as_const(below))
        rememberDiskName(entry.first, rel + entry.second);
}

void VaultPrivate::adoptSpellings(const DiskListing &listing)
{
    // Another program may re-spell a known entry (NFC <-> NFD) without changing its vault path.
    QList<DiskEntry> respelled;
    for (const DiskEntry &entry : listing.dirs) {
        if (dirs.contains(entry.stat.path) && diskRel(entry.stat.path) != entry.diskRel)
            respelled.append(entry);
    }
    std::sort(respelled.begin(), respelled.end(),
              [](const DiskEntry &a, const DiskEntry &b) { return a.stat.path < b.stat.path; });
    for (const DiskEntry &entry : std::as_const(respelled)) {
        if (diskRel(entry.stat.path) != entry.diskRel) // a parent's respelling may have fixed it
            respell(entry.stat.path, entry.diskRel);
    }
    for (const DiskEntry &entry : listing.files) {
        if (files.contains(entry.stat.path) && diskRel(entry.stat.path) != entry.diskRel)
            rememberDiskName(entry.stat.path, entry.diskRel);
    }
}

std::optional<FileStat> VaultPrivate::diskStat(const VaultPath &path) const
{
    const QFileInfo info(absPath(path));
    if (!info.exists() || (!info.isDir() && !info.isFile()))
        return std::nullopt;
    if (info.isDir() && info.isSymLink() && !path.isEmpty() && !followsLink(info, diskRel(paths::dirname(path))))
        return std::nullopt;
    return statOf(info, path);
}

void VaultPrivate::resolveSpelling(const VaultPath &path)
{
    if (path.isEmpty() || existsOnDisk(path))
        return;
    // Not there under the spelling the vault knows: another program may have re-spelled a known
    // entry (NFC <-> NFD) since the last listing, and entries the vault does not track (the
    // trash) may have been written in another normalisation by other devices. Paths that are
    // simply new are left alone (no listing).
    VaultPath cur;
    const QStringList segments = path.split(u'/');
    for (const QString &segment : segments) {
        const VaultPath parent = cur;
        cur = parent.isEmpty() ? segment : parent + u'/' + segment;
        if (existsOnDisk(cur))
            continue;
        if (vaultutil::isTracked(cur) && !files.contains(cur) && !dirs.contains(cur))
            return;
        QString found;
        QDirIterator it(absPath(parent), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        while (it.hasNext() && found.isEmpty()) {
            const QString name = it.nextFileInfo().fileName();
            if (name.normalized(QString::NormalizationForm_C) == segment)
                found = name;
        }
        if (found.isEmpty())
            return;
        const QString parentRel = diskRel(parent);
        respell(cur, parentRel.isEmpty() ? found : parentRel + u'/' + found);
    }
}

bool VaultPrivate::isDirOnDisk(const VaultPath &path) const
{
    const QFileInfo info(absPath(path));
    return info.isDir() && (path.isEmpty() || !info.isSymLink() || followsLink(info, diskRel(paths::dirname(path))));
}

bool VaultPrivate::followsLink(const QFileInfo &link, const QString &parentRel) const
{
    const QString target = link.canonicalFilePath();
    // Links into the vault would show files twice under different paths: not followed.
    if (target.isEmpty() || canonicalRoot.isEmpty() || fsWithin(target, canonicalRoot))
        return false;
    // A target containing the folder that holds the link (or any folder above it) is a loop.
    for (QString rel = parentRel;;) {
        const QString folder = QFileInfo(absOfDiskRel(rel)).canonicalFilePath();
        if (!folder.isEmpty() && fsWithin(folder, target))
            return false;
        if (rel.isEmpty())
            return true;
        rel.truncate(qMax<qsizetype>(rel.lastIndexOf(u'/'), 0));
    }
}

bool VaultPrivate::existsOnDisk(const VaultPath &path) const
{
    const QFileInfo info(absPath(path));
    return info.exists() || info.isSymLink();
}

QDateTime VaultPrivate::now() const
{
    return clock();
}

// ---------------------------------------------------------------------------------------------
// VaultPrivate: known entries
// ---------------------------------------------------------------------------------------------

void VaultPrivate::addLower(const VaultPath &path)
{
    ++lowerCounts[path.toLower()];
}

void VaultPrivate::removeLower(const VaultPath &path)
{
    const auto it = lowerCounts.find(path.toLower());
    if (it != lowerCounts.end() && --*it <= 0)
        lowerCounts.erase(it);
}

bool VaultPrivate::isTakenCaseInsensitive(const VaultPath &path) const
{
    return lowerCounts.contains(path.toLower());
}

void VaultPrivate::trackFile(const FileStat &stat)
{
    const VaultPath &p = stat.path;
    if (dirs.contains(p))
        untrackDir(p);
    if (!files.contains(p))
        addLower(p);
    files.insert(p, stat);
    sortedFilesValid = false;
    trackAncestors(p);
}

void VaultPrivate::insertDir(const FileStat &stat)
{
    const VaultPath &p = stat.path;
    if (files.contains(p))
        untrackFile(p);
    if (!dirs.contains(p))
        addLower(p);
    FileStat st = stat;
    st.isDir = true;
    st.size = 0;
    dirs.insert(p, st);
}

void VaultPrivate::trackDir(const FileStat &stat)
{
    trackAncestors(stat.path);
    insertDir(stat);
}

void VaultPrivate::trackAncestors(const VaultPath &path)
{
    const QStringList chain = vaultutil::ancestors(path);
    for (const VaultPath &dir : chain) {
        if (dirs.contains(dir) || !vaultutil::isTracked(dir))
            continue;
        const std::optional<FileStat> st = diskStat(dir);
        insertDir(st && st->isDir ? *st : FileStat{dir, true, 0, 0});
    }
}

bool VaultPrivate::untrackFile(const VaultPath &path)
{
    if (!files.remove(path))
        return false;
    removeLower(path);
    hashes.remove(path);
    contents.remove(path);
    bomPaths.remove(path);
    staleContent.remove(path);
    diskNames.remove(path);
    sortedFilesValid = false;
    return true;
}

void VaultPrivate::untrackDir(const VaultPath &path)
{
    if (!dirs.remove(path))
        return;
    removeLower(path);
    diskNames.remove(path);
}

std::optional<FileStat> VaultPrivate::knownStat(const VaultPath &path) const
{
    if (const auto it = files.constFind(path); it != files.cend())
        return *it;
    if (const auto it = dirs.constFind(path); it != dirs.cend())
        return *it;
    return std::nullopt;
}

std::optional<VaultPath> VaultPrivate::storedCaseVariant(const VaultPath &path, const FileStat &disk) const
{
    if (disk.isDir || !isTakenCaseInsensitive(path))
        return std::nullopt;
    const QString lower = path.toLower();
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (it.key() == path || it.key().toLower() != lower)
            continue;
        const std::optional<FileStat> st = diskStat(it.key());
        if (st && sameStat(*st, disk))
            return it.key();
    }
    return std::nullopt;
}

QList<VaultChange> VaultPrivate::forgetTree(const VaultPath &path, ChangeSource source)
{
    QList<VaultPath> goneFiles;
    QList<VaultPath> goneDirs;
    if (files.contains(path))
        goneFiles.append(path);
    // Untracked folders (".brulion") may hold tracked files too.
    if (dirs.contains(path) || !vaultutil::isTracked(path)) {
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            if (vaultutil::isStrictlyWithin(it.key(), path))
                goneFiles.append(it.key());
        }
        for (auto it = dirs.cbegin(); it != dirs.cend(); ++it) {
            if (vaultutil::isWithin(it.key(), path))
                goneDirs.append(it.key());
        }
    }
    std::sort(goneFiles.begin(), goneFiles.end());
    QList<VaultChange> out;
    for (const VaultPath &file : std::as_const(goneFiles)) {
        unwatch(file);
        untrackFile(file);
        out.append(VaultChange{ChangeType::Deleted, file, {}, source});
    }
    for (const VaultPath &dir : std::as_const(goneDirs)) {
        unwatch(dir);
        untrackDir(dir);
    }
    forgetDiskNames(path);
    syncConfigFolderWatch();
    return out;
}

QList<VaultChange> VaultPrivate::takeInKindChange(const VaultPath &path, bool isDirNow)
{
    // The cache may disagree with the disk about what `path` is (a folder replaced by a file
    // behind our back): take that in first, so an operation on it reports and forgets exactly
    // what is there instead of leaving the old entries behind.
    const std::optional<FileStat> known = knownStat(path);
    if (!known || known->isDir == isDirNow)
        return {};
    return reconcile(ScanScope{{path}, {}, {}}, ChangeSource::External);
}

void VaultPrivate::reset()
{
    files.clear();
    dirs.clear();
    lowerCounts.clear();
    diskNames.clear();
    hashes.clear();
    contents.clear();
    bomPaths.clear();
    staleContent.clear();
    sortedFiles.clear();
    sortedFilesValid = false;
    pendingDirs.clear();
    pendingFiles.clear();
    batchTimer.stop();
}

// ---------------------------------------------------------------------------------------------
// VaultPrivate: content
// ---------------------------------------------------------------------------------------------

bool VaultPrivate::isTextual(const VaultPath &path) const
{
    // Text by extension, or read/written as text through readText()/writeText() before.
    return vaultutil::isTextPath(path) || contents.contains(path);
}

bool VaultPrivate::comparesContent(const FileStat &stat) const
{
    // Bigger files (a CSV export, a log) are compared by size and modification time like binary
    // files: scans never read them, so memory stays bounded whatever lands in the vault.
    return stat.size <= ContentCache::kMaxItemBytes && isTextual(stat.path);
}

void VaultPrivate::rememberContent(const VaultPath &path, const QByteArray &bytes, bool asText)
{
    hashes.insert(path, text::fnv1a64Hex(bytes));
    if (bytes.startsWith(vaultutil::utf8Bom()))
        bomPaths.insert(path);
    else
        bomPaths.remove(path);
    if (asText || isTextual(path))
        contents.put(path, bytes);
}

QByteArray VaultPrivate::readDisk(const VaultPath &path, bool *ok) const
{
    QFile file(absPath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        *ok = false;
        return {};
    }
    const QByteArray bytes = file.readAll();
    *ok = file.error() == QFileDevice::NoError;
    return bytes;
}

QByteArray VaultPrivate::readFile(const VaultPath &path, bool asText, bool *ok)
{
    *ok = false;
    if (!requireOpen())
        return {};
    if (path.isEmpty()) {
        fail(Vault::tr("Cannot read: empty path"));
        return {};
    }
    const bool tracked = vaultutil::isTracked(path);
    resolveSpelling(path);
    // Stat before reading: if the file changes in between, the content read is newer than the
    // stat, which only causes one harmless check later.
    const std::optional<FileStat> before = tracked ? diskStat(path) : std::nullopt;
    QFile file(absPath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        fail(Vault::tr("Cannot read %1: %2").arg(path, file.errorString()));
        if (files.contains(path))
            scheduleCheck(path); // gone behind our back
        return {};
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        fail(Vault::tr("Cannot read %1: %2").arg(path, file.errorString()));
        return {};
    }
    *ok = true;
    if (tracked) {
        const auto known = files.constFind(path);
        if (known == files.cend() || !before || !sameStat(*before, *known)) {
            scheduleCheck(path); // changed or created behind our back: report it soon
        } else if (const auto hash = hashes.constFind(path);
                   hash != hashes.cend() && *hash != text::fnv1a64Hex(bytes)) {
            // Other content behind an unchanged stat (coarse timestamps: FAT, network mounts):
            // keep the known hash and content so the check reports it and History keeps the old.
            staleContent.insert(path);
            scheduleCheck(path);
        } else {
            rememberContent(path, bytes, asText);
        }
    }
    return bytes;
}

void VaultPrivate::snapshotCached(const VaultPath &path, ChangeSource reason)
{
    if (!history)
        return;
    const std::optional<QByteArray> bytes = contents.get(path);
    if (!bytes)
        return;
    // The bytes as they were: a BOM, CRLF or a legacy encoding stays recoverable.
    if (!history->snapshotBytes(path, *bytes, changeSourceName(reason)))
        qCWarning(lcVault).noquote() << "History snapshot failed:" << history->errorString();
}

bool VaultPrivate::usesBom(const VaultPath &path) const
{
    // Known content (writeFile refreshes it first) answers; otherwise look at the file itself
    // (unseen, untracked or too big to read).
    if (hashes.contains(path) && !staleContent.contains(path))
        return bomPaths.contains(path);
    QFile file(absPath(path));
    return file.open(QIODevice::ReadOnly) && file.read(3) == vaultutil::utf8Bom();
}

// ---------------------------------------------------------------------------------------------
// VaultPrivate: writing
// ---------------------------------------------------------------------------------------------

bool VaultPrivate::ensureDirs(const VaultPath &dir, QList<VaultChange> &changes)
{
    if (dir.isEmpty() || (dirs.contains(dir) && isDirOnDisk(dir)))
        return true;
    VaultPath cur;
    const QStringList segments = dir.split(u'/');
    for (const QString &segment : segments) {
        if (cur.isEmpty())
            cur = segment;
        else
            cur = cur + u'/' + segment;
        if (isDirOnDisk(cur)) {
            // A folder created behind our back: take in what it holds first.
            if (vaultutil::isTracked(cur) && !dirs.contains(cur))
                changes += reconcile(ScanScope{{cur}, {}, {}}, ChangeSource::External);
            continue;
        }
        const QFileInfo info(absPath(cur));
        if (info.exists() || info.isSymLink())
            return fail(Vault::tr("Cannot create the folder %1: a file or link with that name exists").arg(cur));
        if (!QDir().mkdir(info.absoluteFilePath()))
            return fail(Vault::tr("Cannot create the folder %1").arg(cur));
        if (vaultutil::isTracked(cur)) {
            if (const std::optional<FileStat> st = diskStat(cur))
                trackDir(*st);
            watchDir(cur);
        } else if (cur == vaultutil::configFolder()) {
            watchDir(cur);
        }
    }
    return true;
}

void VaultPrivate::refreshKnownContent(const VaultPath &path, bool asText, qint64 newSize, QList<VaultChange> &changes)
{
    const qint64 size = files.value(path).size;
    const bool textual = asText || isTextual(path);
    // Text is read when History needs its content or when it is small enough to verify that
    // nothing changed behind an unchanged stat; binary content only when its hash is unknown
    // and the size alone cannot tell. Nothing above the cache limit is ever read here.
    const bool needed = textual ? (!hashes.contains(path) || !contents.contains(path) || size <= kVerifyBytes)
                                : (!hashes.contains(path) && size == newSize);
    if (!needed || size > ContentCache::kMaxItemBytes)
        return;
    bool ok = false;
    const QByteArray current = readDisk(path, &ok);
    if (!ok)
        return;
    const auto hash = hashes.constFind(path);
    if (hash != hashes.cend() && *hash != text::fnv1a64Hex(current)) {
        // Changed behind our back without a visible stat change (coarse timestamps).
        staleContent.insert(path);
        changes += reconcile(ScanScope{{}, {}, {path}}, ChangeSource::External);
    } else {
        rememberContent(path, current, textual);
    }
}

bool VaultPrivate::writeFile(const VaultPath &path, const QByteArray &bytes, ChangeSource source, bool asText,
                             QList<VaultChange> &changes)
{
    if (path.isEmpty())
        return fail(Vault::tr("Cannot write: empty path"));
    resolveSpelling(path);
    std::optional<FileStat> disk = diskStat(path);
    if (dirs.contains(path) || (disk && disk->isDir))
        return fail(Vault::tr("Cannot write %1: it is a folder").arg(path));
    // QSaveFile writes through symlinks: one that leads nowhere would create its target,
    // wherever it points (outside the vault). Such links are not listed either.
    if (!disk && QFileInfo(absPath(path)).isSymLink())
        return fail(Vault::tr("Cannot write %1: it is a link that does not lead to a file").arg(path));
    const bool tracked = vaultutil::isTracked(path);
    if (tracked) {
        // Case-insensitive storage (Android shared storage, Windows, macOS): `path` reaches a
        // known file spelled differently; update that one instead of tracking a duplicate.
        if (!files.contains(path) && disk) {
            if (const std::optional<VaultPath> variant = storedCaseVariant(path, *disk))
                return writeFile(*variant, bytes, source, asText, changes);
        }
        const auto known = files.constFind(path);
        const bool changedBehind = known == files.cend() ? disk.has_value() : (!disk || !sameStat(*disk, *known));
        if (changedBehind) {
            // Take in what other programs did first: reported as External and kept in History
            // instead of being overwritten without a trace.
            changes += reconcile(ScanScope{{}, {}, {path}}, ChangeSource::External);
        }
        if (files.contains(path))
            refreshKnownContent(path, asText, bytes.size(), changes);
    }
    // Text written to a file that has a byte order mark keeps it (editors such as Notepad add one).
    QByteArray data = bytes;
    if (asText && !data.startsWith(vaultutil::utf8Bom()) && usesBom(path))
        data.prepend(vaultutil::utf8Bom());
    if (tracked && files.contains(path)) {
        if (hashes.value(path) == text::fnv1a64Hex(data))
            return true; // unchanged: no write, no change
        snapshotCached(path, source);
    }
    const bool existed = tracked ? files.contains(path) : disk.has_value();
    if (!ensureDirs(paths::dirname(path), changes))
        return false;
    QSaveFile file(absPath(path));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return fail(Vault::tr("Cannot write %1: %2").arg(path, file.errorString()));
    if (!tracked)
        return true;
    const std::optional<FileStat> written = diskStat(path);
    if (!written)
        return fail(Vault::tr("%1 disappeared right after it was written").arg(path));
    trackFile(*written);
    // Known hash = what we wrote: the watcher's echo of this write is recognised as such.
    rememberContent(path, data, asText);
    watchFile(path);
    changes.append(VaultChange{existed ? ChangeType::Modified : ChangeType::Created, path, {}, source});
    return true;
}

bool VaultPrivate::write(const VaultPath &path, const QByteArray &bytes, ChangeSource source, bool asText)
{
    if (!requireOpen() || !requireStorage())
        return false;
    QList<VaultChange> changes;
    const bool ok = writeFile(path, bytes, source, asText, changes);
    finish(changes);
    return ok;
}

VaultPath VaultPrivate::create(const VaultPath &path, const QByteArray &bytes, ChangeSource source, bool asText)
{
    if (!requireOpen() || !requireStorage())
        return {};
    const VaultPath base = paths::normalize(path);
    if (base.isEmpty()) {
        fail(Vault::tr("Cannot create a file with an empty name"));
        return {};
    }
    const VaultPath target = vaultutil::uniqueName(base, [this](const VaultPath &candidate) {
        return isTakenCaseInsensitive(candidate) || existsOnDisk(candidate);
    });
    return write(target, bytes, source, asText) ? target : VaultPath();
}

// ---------------------------------------------------------------------------------------------
// VaultPrivate: config and change delivery
// ---------------------------------------------------------------------------------------------

bool VaultPrivate::loadConfig()
{
    ParsedVaultConfig parsed;
    const QString path = vaultConfigPath();
    const auto known = files.constFind(path);
    if (known != files.cend() && known->size > kMaxConfigBytes) {
        parsed.warnings.append(u"%1 is too big (%2 bytes); using the defaults"_s.arg(path).arg(known->size));
    } else if (known != files.cend()) {
        bool ok = false;
        const QByteArray bytes = readDisk(path, &ok);
        if (ok)
            parsed = parseVaultConfig(bytes);
        else
            parsed.warnings.append(u"Cannot read %1"_s.arg(path));
    }
    for (const QString &warning : std::as_const(parsed.warnings))
        qCWarning(lcVault).noquote() << warning;
    const bool differs = !sameVaultConfig(config, parsed.config);
    config = parsed.config;
    rawConfig = parsed.raw;
    configWarnings = parsed.warnings;
    return differs;
}

void VaultPrivate::finish(const QList<VaultChange> &changes)
{
    if (changes.isEmpty())
        return;
    const QString configPath = vaultConfigPath();
    const bool configTouched = std::any_of(changes.cbegin(), changes.cend(), [&](const VaultChange &c) {
        return c.path == configPath || c.oldPath == configPath;
    });
    const bool configDiffers = configTouched && loadConfig();
    Q_EMIT q->changed(changes);
    if (configDiffers)
        Q_EMIT q->configChanged();
}

// ---------------------------------------------------------------------------------------------
// Vault
// ---------------------------------------------------------------------------------------------

Vault::Vault(const QString &rootPath, const QString &deviceStatePath, QObject *parent)
    : QObject(parent), d(std::make_unique<VaultPrivate>(this, rootPath, deviceStatePath))
{
    qRegisterMetaType<brulion::VaultChange>();
    qRegisterMetaType<QList<brulion::VaultChange>>();
    if (!d->root.isEmpty())
        d->id = idForPath(d->root);
}

Vault::~Vault()
{
    d->stopWatching();
    d->store->flush();
}

QString Vault::idForPath(const QString &rootPath)
{
    // Canonical path of the nearest existing folder plus the missing rest: a vault folder that
    // open() has yet to create gets the id it will have afterwards, even below a symlinked
    // parent (/sdcard -> /storage/emulated/0) — the device state folder is named by it.
    const QString clean = QDir::cleanPath(QFileInfo(rootPath).absoluteFilePath());
    QString existing = clean;
    QStringList missing;
    QString canonical = QFileInfo(existing).canonicalFilePath();
    while (canonical.isEmpty()) {
        const QFileInfo info(existing);
        const QString parent = info.path();
        if (parent == existing || info.fileName().isEmpty())
            break;
        missing.prepend(info.fileName());
        existing = parent;
        canonical = QFileInfo(existing).canonicalFilePath();
    }
    if (canonical.isEmpty())
        return text::fnv1a64Hex(clean.toUtf8());
    for (const QString &segment : std::as_const(missing)) {
        if (!canonical.endsWith(u'/'))
            canonical += u'/';
        canonical += segment;
    }
    return text::fnv1a64Hex(canonical.toUtf8());
}

bool Vault::open(QString *error)
{
    const auto failed = [&](const QString &message) {
        d->fail(message);
        if (error)
            *error = message;
        return false;
    };
    if (d->root.isEmpty())
        return failed(tr("No vault folder given"));
    const QFileInfo info(d->root);
    if (info.exists() && !info.isDir())
        return failed(tr("%1 is not a folder").arg(d->root));
    if (!info.exists() && !QDir().mkpath(d->root))
        return failed(tr("Cannot create the folder %1").arg(d->root));
    const bool watch = d->watchRequested; // also when requested before the first open()
    d->stopWatching();
    d->reset();
    d->canonicalRoot = QFileInfo(d->root).canonicalFilePath();
    d->storageId = d->probeStorage();
    DiskListing listing;
    listing.trees.insert(QString());
    d->listChildren(QString(), QString(), true, listing);
    d->adoptListing(listing);
    d->id = idForPath(d->root);
    d->isOpen = true;
    d->error.clear();
    d->noteListing(listing, true);
    const bool configDiffers = d->loadConfig();
    if (watch)
        d->startWatching();
    if (configDiffers)
        Q_EMIT configChanged();
    return true;
}

bool Vault::isOpen() const
{
    return d->isOpen;
}

QString Vault::id() const
{
    return d->id;
}

QString Vault::rootPath() const
{
    return d->root;
}

QString Vault::deviceStatePath() const
{
    return d->statePath;
}

QString Vault::absolutePath(const VaultPath &path) const
{
    return d->absPath(paths::normalize(path));
}

QString Vault::errorString() const
{
    return d->error;
}

const VaultConfig &Vault::config() const
{
    return d->config;
}

QStringList Vault::configWarnings() const
{
    return d->configWarnings;
}

bool Vault::saveConfig(const VaultConfig &config)
{
    return d->write(vaultConfigPath(), serializeVaultConfig(config, d->rawConfig), ChangeSource::User, true);
}

QList<FileStat> Vault::files() const
{
    if (!d->sortedFilesValid) {
        d->sortedFiles.clear();
        for (const FileStat &st : std::as_const(d->files)) {
            if (!paths::isIgnored(st.path))
                d->sortedFiles.append(st);
        }
        std::sort(d->sortedFiles.begin(), d->sortedFiles.end(), byPath);
        d->sortedFilesValid = true;
    }
    return d->sortedFiles;
}

QList<FileStat> Vault::markdownFiles() const
{
    QList<FileStat> out = files();
    out.removeIf([](const FileStat &st) { return !paths::isMarkdown(st.path); });
    return out;
}

QList<FileStat> Vault::folders() const
{
    QList<FileStat> out = d->dirs.values();
    std::sort(out.begin(), out.end(), byPath);
    return out;
}

bool Vault::exists(const VaultPath &path) const
{
    const VaultPath p = paths::normalize(path);
    return !p.isEmpty() && (d->files.contains(p) || d->dirs.contains(p));
}

std::optional<FileStat> Vault::stat(const VaultPath &path) const
{
    return d->knownStat(paths::normalize(path));
}

QString Vault::readText(const VaultPath &path, bool *ok)
{
    bool good = false;
    const QByteArray bytes = d->readFile(paths::normalize(path), true, &good);
    if (ok)
        *ok = good;
    return good ? vaultutil::decodeUtf8(bytes) : QString();
}

QByteArray Vault::readBinary(const VaultPath &path, bool *ok)
{
    bool good = false;
    QByteArray bytes = d->readFile(paths::normalize(path), false, &good);
    if (ok)
        *ok = good;
    return bytes;
}

bool Vault::writeText(const VaultPath &path, const QString &content, ChangeSource source)
{
    return d->write(paths::normalize(path), content.toUtf8(), source, true);
}

bool Vault::writeBinary(const VaultPath &path, const QByteArray &data, ChangeSource source)
{
    return d->write(paths::normalize(path), data, source, false);
}

VaultPath Vault::create(const VaultPath &path, const QString &content, ChangeSource source)
{
    return d->create(path, content.toUtf8(), source, true);
}

VaultPath Vault::createBinary(const VaultPath &path, const QByteArray &data, ChangeSource source)
{
    return d->create(path, data, source, false);
}

bool Vault::createFolder(const VaultPath &path)
{
    if (!d->requireOpen() || !d->requireStorage())
        return false;
    const VaultPath p = paths::normalize(path);
    if (p.isEmpty())
        return d->fail(tr("Cannot create a folder with an empty name"));
    if (d->files.contains(p))
        return d->fail(tr("Cannot create the folder %1: a file with that name exists").arg(p));
    QList<VaultChange> changes;
    const bool ok = d->ensureDirs(p, changes);
    d->finish(changes);
    return ok;
}

History *Vault::history() const
{
    return d->history.get();
}

LocalStore *Vault::store() const
{
    return d->store;
}

void Vault::setClock(std::function<QDateTime()> clock)
{
    d->clock = clock ? std::move(clock) : std::function<QDateTime()>(systemNow);
    if (d->history)
        d->history->setClock(d->clock);
}

} // namespace brulion
