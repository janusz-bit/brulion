#include "journallistmodel.h"

#include "contentutil.h"
#include "index/vaultindex.h"
#include "workspace.h"

#include <QHash>
#include <QRegularExpression>

namespace brulion::content {

using namespace Qt::StringLiterals;

namespace {

/// Nothing written yet: only blank lines and bare list markers / empty checkboxes.
bool isBlankNote(const QString &content)
{
    static const QRegularExpression blankLine(u"^\\s*(?:[-*+](?:\\s+\\[ \\])?)?\\s*$"_s);
    for (const QStringView line : QStringView(content).split(u'\n')) {
        if (!blankLine.matchView(line).hasMatch())
            return false;
    }
    return true;
}

} // namespace

JournalListModel::JournalListModel(QObject *parent) : WorkspaceListModel(parent) { }

void JournalListModel::setToday(const QString &today)
{
    if (m_todayText == today)
        return;
    m_todayText = today;
    Q_EMIT todayChanged();
    scheduleRefresh(0);
}

void JournalListModel::setIncludeEmpty(bool on)
{
    if (m_includeEmpty == on)
        return;
    m_includeEmpty = on;
    Q_EMIT includeEmptyChanged();
    scheduleRefresh(0);
}

QDate JournalListModel::effectiveToday() const
{
    const QDate fixed = parseIsoDate(m_todayText);
    if (fixed.isValid())
        return fixed;
    return workspace() ? workspace()->now().date() : QDate::currentDate();
}

QString JournalListModel::todayPath() const
{
    if (!m_rows.isEmpty() && m_rows.first().date == effectiveToday())
        return m_rows.first().path;
    return workspace() ? workspace()->journalPath(isoDate(effectiveToday())) : QString();
}

bool JournalListModel::todayExists() const
{
    return !m_rows.isEmpty() && m_rows.first().date == effectiveToday() && m_rows.first().exists;
}

void JournalListModel::workspaceReset()
{
    m_shown.clear();
    m_lastToday = QDate();
}

void JournalListModel::refresh()
{
    QList<Day> next;
    shell::Workspace *ws = workspace();
    const VaultIndex *idx = vaultIndex();
    if (ws && idx && ws->isOpen()) {
        const QDate today = effectiveToday();
        QHash<QDate, QString> chosen;
        QList<QDate> order;
        for (const JournalRef &j : idx->journals()) { // newest first
            if (j.date > today)
                continue;
            const auto it = chosen.constFind(j.date);
            if (it == chosen.cend()) {
                chosen.insert(j.date, j.path);
                order.append(j.date);
            } else if (j.path == ws->journalPath(isoDate(j.date))) {
                chosen[j.date] = j.path; // the configured file name wins
            }
        }
        next.append(Day{ today, chosen.value(today, ws->journalPath(isoDate(today))), chosen.contains(today) });
        for (const QDate &d : std::as_const(order)) {
            if (d == today)
                continue;
            const QString path = chosen.value(d);
            if (!m_includeEmpty && !m_shown.contains(d) && isBlankNote(idx->content(path)))
                continue;
            next.append(Day{ d, path, true });
        }
        for (const Day &d : std::as_const(next))
            m_shown.insert(d.date);
    }
    applyRows(
        m_rows, next, [](const Day &d) { return d.date; },
        [](const Day &a, const Day &b) { return a.path == b.path && a.exists == b.exists; });
    // Midnight while the app runs: the kept days' isToday / daysAgo are relative to today (the
    // rows themselves stay, so a day being edited keeps its delegate).
    const QDate today = effectiveToday();
    if (m_lastToday.isValid() && today != m_lastToday && !m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), { IsTodayRole, DaysAgoRole });
    m_lastToday = today;

    const QString path = todayPath();
    const bool exists = todayExists();
    if (path != m_lastTodayPath || exists != m_lastTodayExists) {
        m_lastTodayPath = path;
        m_lastTodayExists = exists;
        Q_EMIT todayPathChanged();
    }
}

QVariant JournalListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Day &d = m_rows.at(index.row());
    const QDate today = effectiveToday();
    switch (role) {
    case DateRole: return isoDate(d.date);
    case PathRole: return d.path;
    case ExistsRole: return d.exists;
    case IsTodayRole: return d.date == today;
    case DaysAgoRole: return int(d.date.daysTo(today));
    }
    return {};
}

QHash<int, QByteArray> JournalListModel::roleNames() const
{
    return { { DateRole, "date" }, { PathRole, "path" }, { ExistsRole, "exists" },
             { IsTodayRole, "isToday" }, { DaysAgoRole, "daysAgo" } };
}

int JournalListModel::rowOfDate(const QString &date) const
{
    const QDate d = parseIsoDate(date);
    if (m_rows.isEmpty())
        return -1;
    if (!d.isValid())
        return 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).date <= d)
            return i;
    }
    return int(m_rows.size()) - 1;
}

int JournalListModel::exactRowOfDate(const QString &date) const
{
    const QDate d = parseIsoDate(date);
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).date == d)
            return i;
    }
    return -1;
}

QVariantMap JournalListModel::get(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    const Day &d = m_rows.at(row);
    return { { u"date"_s, isoDate(d.date) }, { u"path"_s, d.path }, { u"exists"_s, d.exists },
             { u"isToday"_s, d.date == effectiveToday() } };
}

} // namespace brulion::content
