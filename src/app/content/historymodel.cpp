#include "historymodel.h"

#include "contentutil.h"
#include "dates/format.h"
#include "util/paths.h"
#include "vault/history.h"
#include "vault/vault.h"
#include "workspace.h"

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

QDate HistoryModel::effectiveToday() const
{
    const QDate fixed = parseIsoDate(m_today);
    if (fixed.isValid())
        return fixed;
    return workspace() ? workspace()->now().date() : QDate::currentDate();
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
            for (const HistoryEntry &e : history->list(m_path))
                next.append(Version{ e.id, e.time, e.size, e.reason });
        }
    }
    applyRows(m_rows, next, [](const Version &v) { return v.id; }, [](const Version &a, const Version &b) { return a == b; });
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
    const dates::UiLocale locale = uiLocale(m_locale);
    const QDate today = effectiveToday();
    switch (role) {
    case IdRole: return v.id;
    case TimeRole: return local.isValid() ? local.toString(u"yyyy-MM-ddTHH:mm:ss"_s) : QString();
    case WhenRole: return local.isValid() ? humanTime(v.time.toMSecsSinceEpoch(), today, locale) : QString();
    case ClockRole: return local.isValid() ? local.toString(u"HH:mm"_s) : QString();
    case DayRole: {
        if (!local.isValid())
            return QString();
        const QDate d = local.date();
        if (d == today)
            return tr("Today");
        if (d == today.addDays(-1))
            return tr("Yesterday");
        return dates::formatDateShort(d, locale, today.year());
    }
    case SizeRole: return v.size;
    case SizeLabelRole: return humanSize(v.size, locale);
    case ReasonRole: return v.reason;
    case ReasonLabelRole: return reasonLabel(v.reason);
    case ReasonIconRole: return reasonIcon(v.reason);
    case NewestRole: return index.row() == 0;
    }
    return {};
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return { { IdRole, "id" },       { TimeRole, "time" },     { WhenRole, "when" },
             { ClockRole, "clock" }, { DayRole, "day" },       { SizeRole, "size" },
             { SizeLabelRole, "sizeLabel" }, { ReasonRole, "reason" }, { ReasonLabelRole, "reasonLabel" },
             { ReasonIconRole, "reasonIcon" }, { NewestRole, "newest" } };
}

QVariantMap HistoryModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        out.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
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
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id)
            return i;
    }
    return -1;
}

} // namespace brulion::content
