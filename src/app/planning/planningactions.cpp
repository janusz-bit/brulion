#include "planningactions.h"

#include "dates/format.h"
#include "dates/local.h"
#include "editorhost.h"
#include "index/vaultindex.h"
#include "markdown/edit.h"
#include "markdown/parse.h"
#include "planningutil.h"

#include <QUrl>

#include <cstdlib>

namespace brulion::planning {

using namespace Qt::StringLiterals;

PlanningActions::PlanningActions(QObject *parent) : QObject(parent) { }

PlanningActions::~PlanningActions() = default;

void PlanningActions::setHost(EditorHost *host)
{
    if (m_host == host)
        return;
    m_host = host;
    m_undoPath.clear();
    Q_EMIT hostChanged();
    Q_EMIT undoChanged();
}

VaultIndex *PlanningActions::vaultIndex() const
{
    return m_index.data();
}

void PlanningActions::setVaultIndex(VaultIndex *index)
{
    if (m_index == index)
        return;
    m_index = index;
    Q_EMIT vaultIndexChanged();
}

QObject *PlanningActions::vaultIndexObject() const
{
    return m_index.data();
}

void PlanningActions::setVaultIndexObject(QObject *index)
{
    setVaultIndex(qobject_cast<VaultIndex *>(index));
}

void PlanningActions::setToday(const QString &today)
{
    if (m_today == today)
        return;
    m_today = today;
    Q_EMIT todayChanged();
}

void PlanningActions::setNow(const QString &now)
{
    if (m_now == now)
        return;
    m_now = now;
    Q_EMIT nowChanged();
}

void PlanningActions::setWeekStart(int weekStart)
{
    if (m_weekStart == weekStart)
        return;
    m_weekStart = weekStart;
    Q_EMIT weekStartChanged();
}

void PlanningActions::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    Q_EMIT localeChanged();
}

bool PlanningActions::canUndo() const
{
    return m_host && !m_undoPath.isEmpty();
}

QDate PlanningActions::currentDay() const
{
    const QDate d = parseIsoDate(m_today);
    return d.isValid() ? d : currentTime().date();
}

QDateTime PlanningActions::currentTime() const
{
    const QDateTime t = QDateTime::fromString(m_now, u"yyyy-MM-dd HH:mm"_s);
    return t.isValid() ? t : QDateTime::currentDateTime();
}

VaultConfig PlanningActions::config() const
{
    return m_index ? m_index->config() : VaultConfig();
}

int PlanningActions::locateTask(const QString &path, const QString &content, int line, const QString &text) const
{
    const ParsedNote note = markdown::parseNote(path, content, config());
    int best = -1;
    int bestDistance = 0;
    for (const Task &t : note.tasks) {
        const bool sameText = text.isEmpty() || plainText(t.text) == text;
        if (t.line == line && sameText)
            return line;
        if (!sameText || text.isEmpty())
            continue;
        const int d = std::abs(t.line - line);
        if (best < 0 || d < bestDistance) {
            best = t.line;
            bestDistance = d;
        }
    }
    return best;
}

bool PlanningActions::writeChange(const QString &path, const QString &before, const QString &after)
{
    if (!m_host->write(path, after)) {
        Q_EMIT failed(tr("The change could not be saved."));
        return false;
    }
    m_undoPath = path;
    m_undoBefore = before;
    m_undoAfter = after;
    Q_EMIT undoChanged();
    return true;
}

bool PlanningActions::toggleTask(const QString &path, int line, const QString &text)
{
    if (!m_host)
        return false;
    bool ok = false;
    const QString content = m_host->read(path, &ok);
    if (!ok) {
        Q_EMIT failed(tr("The page could not be read."));
        return false;
    }
    const int at = locateTask(path, content, line, text);
    if (at < 0) {
        Q_EMIT failed(tr("The task was changed in the meantime."));
        return false;
    }
    const QString after = markdown::toggleTask(content, at, currentDay());
    if (after == content)
        return false;
    if (!writeChange(path, content, after))
        return false;
    // Report the new status of the toggled task. Completing a recurring task inserts its next
    // instance on `at`, the completed one moves one line down.
    const ParsedNote note = markdown::parseNote(path, after, config());
    QString status;
    for (const Task &t : note.tasks) {
        if (t.line == at)
            status = statusName(t.status);
        else if (t.line == at + 1 && isClosed(t.status) && !status.isEmpty() && !isClosed(*statusFromName(status)))
            status = statusName(t.status);
    }
    Q_EMIT taskChanged(path, at, status);
    return true;
}

bool PlanningActions::setTaskStatus(const QString &path, int line, const QString &status, const QString &text)
{
    const auto s = statusFromName(status);
    if (!m_host || !s)
        return false;
    bool ok = false;
    const QString content = m_host->read(path, &ok);
    if (!ok) {
        Q_EMIT failed(tr("The page could not be read."));
        return false;
    }
    const int at = locateTask(path, content, line, text);
    if (at < 0) {
        Q_EMIT failed(tr("The task was changed in the meantime."));
        return false;
    }
    const QString after = markdown::setTaskStatus(content, at, *s, currentDay());
    if (after == content)
        return false;
    if (!writeChange(path, content, after))
        return false;
    Q_EMIT taskChanged(path, at, status);
    return true;
}

bool PlanningActions::undo()
{
    if (!canUndo())
        return false;
    bool ok = false;
    const QString current = m_host->read(m_undoPath, &ok);
    const QString path = m_undoPath;
    const QString before = m_undoBefore;
    const bool unchanged = ok && current == m_undoAfter;
    m_undoPath.clear();
    m_undoBefore.clear();
    m_undoAfter.clear();
    Q_EMIT undoChanged();
    if (!unchanged) {
        Q_EMIT failed(tr("The page changed in the meantime; nothing was undone."));
        return false;
    }
    if (!m_host->write(path, before)) {
        Q_EMIT failed(tr("The change could not be saved."));
        return false;
    }
    return true;
}

bool PlanningActions::appendBlock(const QString &path, const QString &block)
{
    if (!m_host || path.isEmpty() || block.trimmed().isEmpty())
        return false;
    bool ok = false;
    const QString content = m_host->read(path, &ok);
    const QString before = ok ? content : QString();
    const QString after = planning::appendBlock(before, block);
    if (after == before)
        return false;
    return writeChange(path, before, after);
}

bool PlanningActions::openLink(const QString &url, const QString &fromPath)
{
    const qsizetype colon = url.indexOf(u':');
    if (!m_host || colon <= 0)
        return false;
    const QString scheme = url.left(colon);
    const QString rest = url.mid(colon + 1);
    if (scheme == u"ext") {
        m_host->openUrl(QUrl(rest));
        return true;
    }
    if (scheme == u"tag") {
        m_host->navigate({{u"view"_s, u"tag"_s}, {u"tag"_s, QUrl::fromPercentEncoding(rest.toUtf8())}});
        return true;
    }
    if (scheme == u"block") {
        m_host->navigate({{u"view"_s, u"page"_s}, {u"blockId"_s, rest}});
        return true;
    }
    if (scheme == u"page") {
        QString target = rest;
        QString fragment;
        const qsizetype hash = rest.indexOf(u'#');
        if (hash >= 0) {
            target = rest.left(hash);
            fragment = QUrl::fromPercentEncoding(rest.mid(hash + 1).toUtf8());
        }
        target = QUrl::fromPercentEncoding(target.toUtf8());
        QVariantMap route{{u"view"_s, u"page"_s}};
        const QString resolved = target.isEmpty() ? fromPath : m_host->resolveLink(target, fromPath);
        if (resolved.isEmpty())
            route[u"target"_s] = target; // a page that does not exist yet: the host resolves it
        else
            route[u"path"_s] = resolved;
        if (fragment.startsWith(u'^'))
            route[u"blockId"_s] = fragment.mid(1);
        else if (!fragment.isEmpty())
            route[u"heading"_s] = fragment;
        m_host->navigate(route);
        return true;
    }
    return false;
}

QString PlanningActions::taskLine(const QString &text, const QString &date) const
{
    const QDate d = parseIsoDate(date);
    return planning::taskLine(text, d.isValid() ? std::optional<DateSpec>(DateSpec{d, std::nullopt}) : std::nullopt);
}

QString PlanningActions::reminderLine(const QString &text, const QString &at, bool alarm, const QString &recurrence) const
{
    const auto spec = dates::parseDateSpec(at);
    if (!spec)
        return planning::reminderLine(text, DateSpec{});
    DateSpec s = *spec;
    if (!s.time)
        s.time = config().defaultReminderTime;
    return planning::reminderLine(text, s, alarm, recurrence);
}

QVariantMap PlanningActions::parseQuickAdd(const QString &input) const
{
    const Qt::DayOfWeek ws = m_weekStart > 0 ? dates::sanitizeWeekStart(m_weekStart) : config().weekStartsOn;
    const QDateTime now = currentTime();
    const dates::UiLocale loc = uiLocale(m_locale);
    const QuickAdd r = planning::parseQuickAdd(input, now, ws, loc);
    const bool hasTime = r.when && r.when->hasTime();
    return {
            {u"ok"_s, r.ok},
            {u"text"_s, r.text},
            {u"line"_s, r.line},
            {u"phrase"_s, r.phrase},
            {u"date"_s, r.when ? isoDate(r.when->date) : QString()},
            {u"time"_s, hasTime ? clockTime(r.when->time) : QString()},
            {u"hasTime"_s, hasTime},
            {u"kind"_s, !r.when ? u"task"_s : hasTime ? u"reminder"_s : u"due"_s},
            {u"when"_s, r.when ? humanDate(*r.when, loc, now.date()) : QString()},
    };
}

} // namespace brulion::planning
