#include "logmodel.h"

#include "log/categories.h"
#include "log/logger.h"
#include "text/text.h"

#include <QDateTime>

using namespace Qt::StringLiterals;

namespace brulion::diag {

using logging::Entry;
using logging::Level;
using logging::Logger;

LogModel::LogModel(QObject *parent) : QAbstractListModel(parent)
{
    setLogger(Logger::instance());
}

LogModel::~LogModel() = default;

void LogModel::setLogger(Logger *logger)
{
    QObject::disconnect(m_appended);
    QObject::disconnect(m_cleared);
    m_logger = logger;
    if (logger) {
        m_appended = connect(logger, &Logger::appended, this, &LogModel::onAppended);
        m_cleared = connect(logger, &Logger::cleared, this, &LogModel::onCleared);
    }
    reload();
}

LogModel::Item LogModel::makeItem(const Entry &entry, bool earlier)
{
    Item item;
    item.entry = entry;
    item.group = logging::categoryGroup(entry.category);
    item.folded = text::foldKey(QString(entry.category + u' ' + entry.message));
    item.earlier = earlier;
    return item;
}

bool LogModel::matches(const Item &item) const
{
    if (item.entry.level < m_minLevel)
        return false;
    if (!m_group.isEmpty() && item.group != m_group)
        return false;
    if (m_fromMs > 0 && item.entry.time < m_fromMs && (item.entry.lastTime <= 0 || item.entry.lastTime < m_fromMs))
        return false;
    if (m_toMs > 0 && item.entry.time > m_toMs)
        return false;
    if (!m_foldedSearch.isEmpty() && !item.folded.contains(m_foldedSearch))
        return false;
    return true;
}

void LogModel::reload()
{
    beginResetModel();
    m_all.clear();
    m_rows.clear();
    m_lastSeq = 0;
    if (m_logger) {
        if (m_includeEarlier) {
            for (const Entry &e : m_logger->earlierEntries())
                m_all.append(makeItem(e, true));
        }
        const QList<Entry> entries = m_logger->entries();
        for (const Entry &e : entries)
            m_all.append(makeItem(e, false));
        if (!entries.isEmpty())
            m_lastSeq = entries.last().seq;
    }
    for (int i = 0; i < m_all.size(); ++i) {
        if (matches(m_all.at(i)))
            m_rows.append(i);
    }
    endResetModel();
    Q_EMIT countChanged();
}

void LogModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        if (matches(m_all.at(i)))
            m_rows.append(i);
    }
    endResetModel();
    Q_EMIT countChanged();
}

void LogModel::onAppended()
{
    if (!m_logger)
        return;
    // From the newest known entry on: its repeat count may have grown.
    const QList<Entry> fresh = m_logger->entries(m_lastSeq > 0 ? m_lastSeq - 1 : 0);
    if (fresh.isEmpty())
        return;
    // The ring dropped entries we never saw (more than its capacity since the last update):
    // start over.
    if (m_lastSeq > 0 && fresh.first().seq > m_lastSeq) {
        reload();
        return;
    }
    const int oldCount = int(m_rows.size());
    for (const Entry &e : fresh) {
        if (e.seq <= m_lastSeq) {
            // The newest known entry again: update its repeat.
            for (qsizetype i = m_all.size() - 1; i >= 0; --i) {
                Item &item = m_all[i];
                if (item.earlier || item.entry.seq != e.seq)
                    continue;
                if (item.entry.repeat != e.repeat) {
                    item.entry.repeat = e.repeat;
                    item.entry.lastTime = e.lastTime;
                    const qsizetype row = m_rows.lastIndexOf(int(i));
                    if (row >= 0)
                        Q_EMIT dataChanged(index(int(row)), index(int(row)), {RepeatRole});
                }
                break;
            }
            continue;
        }
        m_all.append(makeItem(e, false));
        m_lastSeq = e.seq;
        const int i = int(m_all.size() - 1);
        if (matches(m_all.at(i))) {
            beginInsertRows({}, int(m_rows.size()), int(m_rows.size()));
            m_rows.append(i);
            endInsertRows();
        }
    }
    // Keep this session's part within the ring's size (plus the earlier entries).
    const int limit = (m_logger ? m_logger->capacity() : 5000) * 2;
    if (m_all.size() > limit) {
        reload();
        return;
    }
    if (m_rows.size() != oldCount || !fresh.isEmpty())
        Q_EMIT countChanged();
}

void LogModel::onCleared()
{
    m_includeEarlier = false;
    reload();
    Q_EMIT filterChanged();
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Item &item = m_all.at(m_rows.at(index.row()));
    const Entry &e = item.entry;
    switch (role) {
    case Qt::DisplayRole:
    case MessageRole: return e.message;
    case SeqRole: return e.seq;
    case TimeRole: return e.time;
    case TimeTextRole: return QDateTime::fromMSecsSinceEpoch(e.time).toString(u"HH:mm:ss.zzz"_s);
    case DayRole: return QDateTime::fromMSecsSinceEpoch(e.time).toString(u"yyyy-MM-dd"_s);
    case LevelRole: return logging::levelName(e.level);
    case LetterRole: return QString(logging::levelLetter(e.level));
    case GroupRole: return item.group;
    case CategoryRole: return e.category;
    case RepeatRole: return e.repeat;
    case EarlierRole: return item.earlier;
    default: return {};
    }
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {
            {SeqRole, "seq"},         {TimeRole, "time"},     {TimeTextRole, "timeText"}, {DayRole, "day"},
            {LevelRole, "level"},     {LetterRole, "letter"}, {GroupRole, "group"},       {CategoryRole, "category"},
            {MessageRole, "message"}, {RepeatRole, "repeat"}, {EarlierRole, "earlier"},
    };
}

void LogModel::setGroup(const QString &group)
{
    if (group == m_group)
        return;
    m_group = group;
    rebuild();
    Q_EMIT filterChanged();
}

QString LogModel::minLevel() const
{
    return logging::levelName(m_minLevel);
}

void LogModel::setMinLevel(const QString &level)
{
    const Level parsed = logging::levelFromName(level).value_or(Level::Debug);
    if (parsed == m_minLevel)
        return;
    m_minLevel = parsed;
    rebuild();
    Q_EMIT filterChanged();
}

void LogModel::setSearch(const QString &search)
{
    if (search == m_search)
        return;
    m_search = search;
    m_foldedSearch = text::foldKey(search.trimmed());
    rebuild();
    Q_EMIT filterChanged();
}

void LogModel::setFromMs(qint64 ms)
{
    if (ms == m_fromMs)
        return;
    m_fromMs = ms;
    rebuild();
    Q_EMIT filterChanged();
}

void LogModel::setToMs(qint64 ms)
{
    if (ms == m_toMs)
        return;
    m_toMs = ms;
    rebuild();
    Q_EMIT filterChanged();
}

void LogModel::setIncludeEarlier(bool on)
{
    if (on == m_includeEarlier)
        return;
    m_includeEarlier = on;
    reload();
    Q_EMIT filterChanged();
}

bool LogModel::filtered() const
{
    return !m_group.isEmpty() || m_minLevel > Level::Debug || !m_foldedSearch.isEmpty() || m_fromMs > 0 || m_toMs > 0;
}

QString LogModel::lineAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return logging::formatLine(m_all.at(m_rows.at(row)).entry);
}

QString LogModel::visibleText() const
{
    QStringList lines;
    lines.reserve(m_rows.size());
    for (int i : m_rows)
        lines.append(logging::formatLine(m_all.at(i).entry));
    return lines.join(u'\n');
}

} // namespace brulion::diag
