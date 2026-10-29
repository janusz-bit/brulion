// Workspace: version history and the trash (see workspace.h).
#include "workspace.h"

#include "dates/dates.h"
#include "util/paths.h"
#include "vault/vault.h"

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

} // namespace

// ---- History ------------------------------------------------------------------------------------

bool writeTextKeepingHistory(Vault &vault, const QString &path, const QString &content, ChangeSource source)
{
    History *h = vault.history();
    if (!h)
        return vault.writeText(path, content, source);
    // No throttle for this one write: the text being replaced is always recorded.
    const HistoryPolicy policy = h->policy();
    HistoryPolicy unthrottled = policy;
    unthrottled.throttleSeconds = 0;
    h->setPolicy(unthrottled);
    const bool written = vault.writeText(path, content, source);
    h->setPolicy(policy);
    return written;
}

bool writeBytesKeepingHistory(Vault &vault, const QString &path, const QByteArray &bytes, ChangeSource source)
{
    History *h = vault.history();
    if (!h)
        return vault.writeBinary(path, bytes, source);
    const HistoryPolicy policy = h->policy();
    HistoryPolicy unthrottled = policy;
    unthrottled.throttleSeconds = 0;
    h->setPolicy(unthrottled);
    const bool written = vault.writeBinary(path, bytes, source);
    h->setPolicy(policy);
    return written;
}

QVariantList Workspace::history(const QString &path) const
{
    QVariantList out;
    History *h = m_vault ? m_vault->history() : nullptr;
    if (!h)
        return out;
    const QDate today = now().date();
    for (const HistoryEntry &e : h->list(paths::normalize(path))) {
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
    QVariantList out;
    if (!m_vault)
        return out;
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
    for (const FileStat &f : m_vault->trash()) {
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
        const QString original = info.original;
        list.append(QVariantMap{ { u"trashPath"_s, item.trashPath },
                                 { u"original"_s, original },
                                 { u"title"_s, item.folder ? paths::basename(original) : paths::pageTitle(original) },
                                 { u"folder"_s, item.folder },
                                 { u"files"_s, item.files },
                                 { u"size"_s, item.size },
                                 { u"deletedAt"_s, isoOf(info.deletedAt) },
                                 { u"when"_s, humanOf(info.deletedAt, m_language, today) } });
    }
    std::stable_sort(list.begin(), list.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const QString ta = a.value(u"deletedAt"_s).toString();
        const QString tb = b.value(u"deletedAt"_s).toString();
        if (ta != tb)
            return ta > tb; // newest first; unknown ("") last
        return a.value(u"trashPath"_s).toString() < b.value(u"trashPath"_s).toString();
    });
    m_trashCount = int(list.size());
    for (const QVariantMap &m : std::as_const(list))
        out.append(m);
    return out;
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

} // namespace brulion::shell
