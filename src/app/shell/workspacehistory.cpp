// Workspace: version history and the trash (see workspace.h).
#include "workspace.h"

#include "dates/dates.h"
#include "notebook/cleanup.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QLocale>
#include <QMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

QString isoOf(const QDateTime &time)
{
    return time.isValid() ? time.toLocalTime().toString(u"yyyy-MM-ddTHH:mm:ss"_s) : QString();
}

QString humanOf(const QDateTime &time, const QString &language, QDate today)
{
    if (!time.isValid())
        return {};
    const QDateTime local = time.toLocalTime();
    return dates::formatHuman(DateSpec{ local.date(), QTime(local.time().hour(), local.time().minute()) },
                              language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish, today);
}

/// `write()` with no throttle of the history (if any) for this one write: the version being
/// replaced is always recorded.
template<typename Write>
bool unthrottled(History *history, const Write &write)
{
    if (!history)
        return write();
    const HistoryPolicy policy = history->policy();
    HistoryPolicy none = policy;
    none.throttleSeconds = 0;
    history->setPolicy(none);
    const bool written = write();
    history->setPolicy(policy);
    return written;
}

} // namespace

// ---- History ------------------------------------------------------------------------------------

bool writeTextKeepingHistory(Vault &vault, const QString &path, const QString &content, ChangeSource source)
{
    return unthrottled(vault.history(), [&] { return vault.writeText(path, content, source); });
}

bool writeBytesKeepingHistory(Vault &vault, const QString &path, const QByteArray &bytes, ChangeSource source,
                              QString *error)
{
    History *h = vault.history();
    bool recorded = true;
    const bool written = unthrottled(h, [&] {
        // The vault snapshots the content it holds in memory, never a file beyond its cache (a
        // drawing with pasted photos): the version replaced is recorded from the disk here. (The
        // vault's own snapshot of the same bytes is then no new entry.)
        if (h && vault.exists(path)) {
            bool read = false;
            const QByteArray current = vault.readBinary(path, &read);
            recorded = read && (current == bytes || h->snapshotBytes(path, current, changeSourceName(source)));
            if (!recorded) {
                if (error)
                    *error = read ? h->errorString() : vault.errorString();
                return false;
            }
        }
        return vault.writeBinary(path, bytes, source);
    });
    if (!written && recorded && error)
        *error = vault.errorString();
    return written;
}

QVariantList Workspace::history(const QString &path) const
{
    QVariantList out;
    History *h = m_vault ? m_vault->history() : nullptr;
    if (!h)
        return out;
    const QDate today = now().date();
    const QList<HistoryEntry> entries = h->list(paths::normalize(path));
    for (const HistoryEntry &e : entries) {
        out.append(QVariantMap{ { u"id"_s, e.id },
                                { u"time"_s, isoOf(e.time) },
                                { u"when"_s, humanOf(e.time, m_language, today) },
                                { u"size"_s, e.size },
                                { u"reason"_s, e.reason } });
    }
    return out;
}

QString Workspace::historyText(const QString &id) const
{
    History *h = m_vault ? m_vault->history() : nullptr;
    return h ? h->read(id) : QString();
}

bool Workspace::restoreVersion(const QString &rawPath, const QString &id)
{
    History *h = m_vault ? m_vault->history() : nullptr;
    const QString path = paths::normalize(rawPath);
    if (!h || path.isEmpty())
        return false;
    bool ok = false;
    const QString text = h->read(id, &ok);
    if (!ok)
        return false;
    const bool written = writeTextKeepingHistory(*m_vault, path, text);
    if (!written)
        Q_EMIT errorOccurred(tr("Could not restore the version: %1").arg(m_vault->errorString()));
    return written;
}

QStringList Workspace::historyPaths() const
{
    History *h = m_vault ? m_vault->history() : nullptr;
    return h ? h->paths() : QStringList();
}

// ---- Trash --------------------------------------------------------------------------------------

QVariantList Workspace::trashItems() const
{
    if (!m_vault)
        return {};
    struct Item {
        QString trashPath;
        int files = 0;
        qint64 size = 0;
        bool folder = false;
    };
    // A deleted folder is one item: the outermost trashed entry the vault recorded (its
    // "trash/<path>" metadata in the LocalStore) above each file.
    const QString trash = vaultTrashFolder();
    QMap<QString, Item> items;
    const QList<FileStat> trashed = m_vault->trash();
    for (const FileStat &f : trashed) {
        QString item = f.path;
        const QStringList segments = f.path.split(u'/');
        QString prefix = trash;
        for (qsizetype i = 1; i + 1 < segments.size(); ++i) {
            prefix += u'/' + segments.at(i);
            if (m_vault->store()->contains(u"trash/"_s + prefix)) {
                item = prefix;
                break;
            }
        }
        Item &entry = items[item];
        entry.trashPath = item;
        entry.files += 1;
        entry.size += f.size;
        entry.folder = item != f.path;
    }
    QList<QVariantMap> list;
    const QDate today = now().date();
    for (const Item &item : std::as_const(items)) {
        const TrashInfo info = m_vault->trashInfo(item.trashPath);
        const QString &original = info.original;
        list.append(QVariantMap{ { u"trashPath"_s, item.trashPath },
                                 { u"original"_s, original },
                                 { u"title"_s, item.folder ? paths::basename(original) : paths::pageTitle(original) },
                                 { u"folder"_s, item.folder },
                                 { u"files"_s, item.files },
                                 { u"size"_s, item.size },
                                 { u"deletedAt"_s, isoOf(info.deletedAt) },
                                 { u"when"_s, humanOf(info.deletedAt, m_language, today) } });
    }
    std::ranges::stable_sort(list, [](const QVariantMap &a, const QVariantMap &b) {
        const QString ta = a.value(u"deletedAt"_s).toString();
        const QString tb = b.value(u"deletedAt"_s).toString();
        if (ta != tb)
            return ta > tb; // newest first; unknown ("") last
        return a.value(u"trashPath"_s).toString() < b.value(u"trashPath"_s).toString();
    });
    m_trashCount = int(list.size());
    return QVariantList(list.cbegin(), list.cend());
}

int Workspace::trashCount() const
{
    if (!m_vault)
        return 0;
    if (m_trashCount < 0)
        trashItems(); // sets m_trashCount
    return m_trashCount;
}

bool Workspace::deleteForever(const QString &trashPath)
{
    const QString path = paths::normalize(trashPath);
    if (!m_vault || !path.startsWith(vaultTrashFolder() + u'/'))
        return false;
    const bool ok = m_vault->removePermanently(path, ChangeSource::User);
    if (!ok)
        Q_EMIT errorOccurred(tr("Could not delete %1: %2").arg(paths::basename(path), m_vault->errorString()));
    m_trashCount = -1;
    Q_EMIT trashChanged();
    return ok;
}

bool Workspace::emptyTrash()
{
    if (!m_vault)
        return false;
    const bool ok = m_vault->emptyTrash();
    if (!ok)
        Q_EMIT errorOccurred(tr("Could not empty the trash: %1").arg(m_vault->errorString()));
    m_trashCount = -1;
    Q_EMIT trashChanged();
    return ok;
}

// ---- Cleaning up --------------------------------------------------------------------------------

QVariantList Workspace::unreferencedFiles() const
{
    QVariantList list;
    if (!ready() || !m_index)
        return list;
    const QLocale locale(m_language == u"en" ? QLocale::English : QLocale::Polish);
    const QDate today = now().date();
    const QList<notebook::UnreferencedFile> files = notebook::unreferencedFiles(*m_index, m_vault->config());
    for (const notebook::UnreferencedFile &file : files) {
        if (isFavorite(file.path))
            continue;
        const std::optional<FileStat> stat = m_vault->stat(file.path);
        const qint64 size = stat ? stat->size : 0;
        const QDateTime modified = stat && stat->mtime > 0 ? QDateTime::fromMSecsSinceEpoch(stat->mtime) : QDateTime();
        list.append(QVariantMap{ { u"path"_s, file.path },
                                 { u"title"_s, paths::pageTitle(file.path) },
                                 { u"folder"_s, folderOf(file.path) },
                                 { u"attachment"_s, file.attachment },
                                 { u"picture"_s, paths::isPicturePath(file.path) },
                                 { u"pdf"_s, paths::isPdfPath(file.path) },
                                 { u"size"_s, size },
                                 { u"sizeText"_s, locale.formattedDataSize(size, 1, QLocale::DataSizeTraditionalFormat) },
                                 { u"modified"_s, humanOf(modified, m_language, today) },
                                 { u"openTasks"_s, file.openTasks },
                                 { u"reminders"_s, file.reminders } });
    }
    return list;
}

QStringList Workspace::deletePages(const QStringList &paths)
{
    QStringList trashed;
    for (const QString &path : paths) {
        const QString trashPath = deletePage(path);
        if (!trashPath.isEmpty())
            trashed.append(trashPath);
    }
    return trashed;
}

int Workspace::restoreAll(const QStringList &trashPaths)
{
    int restored = 0;
    for (const QString &trashPath : trashPaths) {
        if (!restore(trashPath).isEmpty())
            ++restored;
    }
    return restored;
}

} // namespace brulion::shell
