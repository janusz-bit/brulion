#include "historymodel.h"

#include "contentutil.h"
#include "dates/format.h"
#include "util/paths.h"
#include "vault/history.h"
#include "vault/vault.h"
#include "workspace.h"

#include <algorithm>
#include <functional>

namespace brulion::content {

using namespace Qt::StringLiterals;

HistoryModel::HistoryModel(QObject *parent) : WorkspaceListModel(parent) { }

void HistoryModel::setPath(const QString &path)
{
    const QString p = paths::normalize(path);
    if (m_path == p)
        return;
    m_path = p;
    Q_EMIT pathChanged();
    scheduleRefresh(0);
}

void HistoryModel::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    Q_EMIT localeChanged();
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1));
}

void HistoryModel::setToday(const QString &today)
{
    if (m_today == today)
        return;
    m_today = today;
    Q_EMIT todayChanged();
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1));
}

QString HistoryModel::reasonLabel(const QString &reason)
{
    if (reason == u"user")
        return tr("Replaced by your edit");
    if (reason == u"external")
        return tr("Replaced by another program");
    if (reason == u"sync")
        return tr("Replaced by sync");
    if (reason == u"import")
        return tr("Replaced by an import");
    if (reason == u"restore")
        return tr("Replaced by a restored version");
    if (reason == u"cli")
        return tr("Replaced from the command line (agent)"); // brulion-cli, e.g. an AI agent
    if (reason == u"replace")
        return tr("Replaced by Find and replace in the notebook");
    return tr("Replaced by Brulion");
}

QString HistoryModel::reasonIcon(const QString &reason)
{
    if (reason == u"user")
        return u"pencil"_s;
    if (reason == u"external")
        return u"file-text"_s;
    if (reason == u"sync")
        return u"refresh-cw"_s;
    if (reason == u"import")
        return u"import"_s;
    if (reason == u"restore")
        return u"history"_s;
    if (reason == u"cli")
        return u"terminal"_s;
    if (reason == u"replace")
        return u"search"_s;
    return u"settings"_s;
}

void HistoryModel::refresh()
{
    QList<Version> next;
    QString current;
    bool exists = false;
    shell::Workspace *ws = workspace();
    History *history = ws && ws->vault() ? ws->vault()->history() : nullptr;
    if (ws && ws->isOpen() && !m_path.isEmpty()) {
        exists = ws->exists(m_path);
        current = exists ? ws->readFile(m_path) : QString();
        if (history) {
            const QList<HistoryEntry> entries = history->list(m_path);
            next.reserve(entries.size());
            for (const HistoryEntry &e : entries)
                next.append(Version{ e.id, e.time, e.size, e.reason });
        }
    }
    applyRows(m_rows, next, [](const Version &v) { return v.id; }, std::equal_to<>{});
    // "newest" moves with new versions.
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(std::min<int>(1, int(m_rows.size()) - 1)), { NewestRole });
    if (current != m_current || exists != m_exists) {
        m_current = current;
        m_exists = exists;
        Q_EMIT currentChanged();
    }
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Version &v = m_rows.at(index.row());
    const QDateTime local = v.time.toLocalTime();
    switch (role) {
    case IdRole: return v.id;
    case TimeRole: return local.isValid() ? local.toString(u"yyyy-MM-ddTHH:mm:ss"_s) : QString();
    case WhenRole:
        if (!local.isValid())
            return QString();
        return humanTime(v.time.toMSecsSinceEpoch(), effectiveToday(m_today, workspace()), uiLocale(m_locale));
    case ClockRole: return local.isValid() ? local.toString(u"HH:mm"_s) : QString();
    case DayRole: {
        if (!local.isValid())
            return QString();
        const QDate d = local.date();
        const QDate today = effectiveToday(m_today, workspace());
        if (d == today)
            return tr("Today");
        if (d == today.addDays(-1))
            return tr("Yesterday");
        return dates::formatDateShort(d, uiLocale(m_locale), today.year());
    }
    case SizeRole: return v.size;
    case SizeLabelRole: return humanSize(v.size, uiLocale(m_locale));
    case ReasonRole: return v.reason;
    case ReasonLabelRole: return reasonLabel(v.reason);
    case ReasonIconRole: return reasonIcon(v.reason);
    case NewestRole: return index.row() == 0;
    }
    return {};
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    static const QHash<int, QByteArray> names{{IdRole, "id"},
                                              {TimeRole, "time"},
                                              {WhenRole, "when"},
                                              {ClockRole, "clock"},
                                              {DayRole, "day"},
                                              {SizeRole, "size"},
                                              {SizeLabelRole, "sizeLabel"},
                                              {ReasonRole, "reason"},
                                              {ReasonLabelRole, "reasonLabel"},
                                              {ReasonIconRole, "reasonIcon"},
                                              {NewestRole, "newest"}};
    return names;
}

QVariantMap HistoryModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (const auto &[role, name] : names.asKeyValueRange())
        out.insert(QString::fromLatin1(name), data(index(row), role));
    return out;
}

QString HistoryModel::textAt(int row) const
{
    if (row < 0 || row >= m_rows.size() || !workspace())
        return {};
    return workspace()->historyText(m_rows.at(row).id);
}

bool HistoryModel::restore(int row)
{
    if (row < 0 || row >= m_rows.size() || !workspace())
        return false;
    const bool ok = workspace()->restoreVersion(m_path, m_rows.at(row).id);
    refreshNow();
    return ok;
}

int HistoryModel::rowOf(const QString &id) const
{
    return rowIndex(m_rows, std::ranges::find(m_rows, id, &Version::id));
}

} // namespace brulion::content
