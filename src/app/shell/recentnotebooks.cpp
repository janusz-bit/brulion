#include "recentnotebooks.h"

#include "appsettings.h"
#include "dates/format.h"
#include "types.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <optional>

#if QT_CONFIG(thread) && QT_CONFIG(future) && !defined(Q_OS_WASM)
#include <QFutureWatcher>
#include <QPromise>

#include <thread>
#define BRULION_RECENT_CHECK_THREADS 1
#endif

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

const QString kChecking = u"checking"_s;
const QString kAvailable = u"available"_s;
const QString kMissing = u"missing"_s;
const QString kUnreachable = u"unreachable"_s;

QString cleaned(const QString &path)
{
    const QString trimmed = path.trimmed();
    return trimmed.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
}

/// Windows' paths compare without regard to case ("C:/Users/…" = "c:/users/…").
Qt::CaseSensitivity pathCase()
{
    return QDir::separator() == u'\\' ? Qt::CaseInsensitive : Qt::CaseSensitive;
}

} // namespace

RecentNotebooks::RecentNotebooks(AppSettings *settings, QObject *parent)
    : QAbstractListModel(parent),
      m_settings(settings),
      m_temporaryRoots(defaultTemporaryRoots())
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, &RecentNotebooks::timedOut);
    connect(m_settings, &AppSettings::recentVaultsChanged, this, &RecentNotebooks::reload);
    reload();
}

RecentNotebooks::~RecentNotebooks() = default;

// ---- Pure helpers ---------------------------------------------------------------------------

bool RecentNotebooks::isUnder(const QString &path, const QStringList &roots)
{
    const QString p = cleaned(path);
    if (p.isEmpty())
        return false;
    return std::ranges::any_of(roots, [&p](const QString &rawRoot) {
        const QString root = cleaned(rawRoot);
        if (root.isEmpty() || root == u"/")
            return false; // the whole disk is no temporary folder
        return p.compare(root, pathCase()) == 0
               || (p.size() > root.size() && p.startsWith(root, pathCase()) && p.at(root.size()) == u'/');
    });
}

QStringList RecentNotebooks::defaultTemporaryRoots()
{
    QStringList roots{cleaned(QDir::tempPath())};
    if (const QString canonical = QFileInfo(QDir::tempPath()).canonicalFilePath(); !canonical.isEmpty())
        roots.append(cleaned(canonical));
    if (QDir::separator() == u'/')
        roots.append({u"/tmp"_s, u"/var/tmp"_s});
    roots.removeDuplicates();
    return roots;
}

QString RecentNotebooks::displayPath(const QString &path, const QString &home)
{
    QString shown = cleaned(path);
    const QString h = cleaned(home);
    if (!h.isEmpty() && h != u"/") {
        if (shown.compare(h, pathCase()) == 0)
            shown = u"~"_s;
        else if (shown.startsWith(h + u'/', pathCase()))
            shown = u'~' + shown.mid(h.size());
    }
    return QDir::toNativeSeparators(shown);
}

QString RecentNotebooks::lastOpenedText(const QDateTime &when, const QDateTime &now, dates::UiLocale locale)
{
    if (!when.isValid())
        return {};
    const QDate today = now.isValid() ? now.date() : QDate();
    const QDate day = when.date();
    QString text;
    if (today.isValid() && (day == today || day == today.addDays(-1)))
        text = dates::formatHuman(DateSpec{day, QTime(when.time().hour(), when.time().minute())}, locale, today);
    else
        text = dates::formatDateShort(day, locale, today.isValid() ? std::optional<int>(today.year()) : std::nullopt);
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

QString RecentNotebooks::probe(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isDir())
        return kMissing;
    return info.isReadable() && info.isExecutable() ? kAvailable : kUnreachable;
}

// ---- The model --------------------------------------------------------------------------------

int RecentNotebooks::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant RecentNotebooks::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case PathRole: return r.path;
    case NameRole: return r.name.isEmpty() ? displayPath(r.path, m_home) : r.name;
    case DisplayPathRole: return displayPath(r.path, m_home);
    case LastOpenedRole: return r.lastOpened;
    case LastOpenedTextRole:
        return lastOpenedText(QDateTime::fromString(r.lastOpened, Qt::ISODate), now(), locale());
    case AvailabilityRole: return r.state;
    case TemporaryRole: return r.temporary;
    }
    return {};
}

QHash<int, QByteArray> RecentNotebooks::roleNames() const
{
    static const QHash<int, QByteArray> names{{PathRole, "path"},
                                              {NameRole, "name"},
                                              {DisplayPathRole, "displayPath"},
                                              {LastOpenedRole, "lastOpened"},
                                              {LastOpenedTextRole, "lastOpenedText"},
                                              {AvailabilityRole, "availability"},
                                              {TemporaryRole, "temporary"}};
    return names;
}

int RecentNotebooks::unavailableCount() const
{
    return int(std::ranges::count_if(m_rows,
                                     [](const Row &r) { return r.state == kMissing || r.state == kUnreachable; }));
}

bool RecentNotebooks::checking() const
{
    return std::ranges::any_of(m_rows, [](const Row &r) { return r.state == kChecking || r.state.isEmpty(); });
}

void RecentNotebooks::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {LastOpenedTextRole});
}

void RecentNotebooks::setLanguage(const QString &language)
{
    const QString next = language == u"pl" ? u"pl"_s : u"en"_s;
    if (next == m_language)
        return;
    m_language = next;
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {LastOpenedTextRole});
}

void RecentNotebooks::setHome(const QString &home)
{
    if (home == m_home)
        return;
    m_home = home;
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {NameRole, DisplayPathRole});
}

void RecentNotebooks::setAssumeAvailable(std::function<bool(const QString &)> assume)
{
    m_assumeAvailable = std::move(assume);
}

void RecentNotebooks::setProbe(std::function<QString(const QString &)> probe)
{
    m_probe = std::move(probe);
}

void RecentNotebooks::setTimeout(int milliseconds)
{
    m_timeoutMs = std::max(1, milliseconds);
}

void RecentNotebooks::setTemporaryRoots(const QStringList &roots)
{
    m_temporaryRoots = roots;
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        const bool temporary = isTemporary(m_rows.at(i).path);
        if (temporary != m_rows.at(i).temporary) {
            m_rows[i].temporary = temporary;
            Q_EMIT dataChanged(index(int(i)), index(int(i)), {TemporaryRole});
        }
    }
}

bool RecentNotebooks::isTemporary(const QString &path) const
{
    return isUnder(path, m_temporaryRoots);
}

QDateTime RecentNotebooks::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

dates::UiLocale RecentNotebooks::locale() const
{
    return m_language == u"pl" ? dates::UiLocale::Polish : dates::UiLocale::English;
}

int RecentNotebooks::indexOf(const QString &path) const
{
    const QString p = cleaned(path);
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).path == p)
            return int(i);
    }
    return -1;
}

QString RecentNotebooks::availability(const QString &path) const
{
    const int row = indexOf(path);
    return row < 0 ? QString() : m_rows.at(row).state;
}

void RecentNotebooks::reload()
{
    QList<Row> next;
    const QVariantList stored = m_settings->recentVaults();
    next.reserve(stored.size());
    QStringList unchecked;
    for (const QVariant &v : stored) {
        const QVariantMap m = v.toMap();
        Row r;
        r.path = cleaned(m.value(u"path"_s).toString());
        if (r.path.isEmpty())
            continue;
        r.name = m.value(u"name"_s).toString();
        if (r.name.isEmpty())
            r.name = QFileInfo(r.path).fileName();
        r.lastOpened = m.value(u"lastOpened"_s).toString();
        r.temporary = isTemporary(r.path);
        const int known = indexOf(r.path);
        if (known >= 0) {
            r.state = m_rows.at(known).state;
        } else {
            r.state = m_checksStarted ? kChecking : QString();
            unchecked.append(r.path);
        }
        next.append(r);
    }

    const auto pathsOf = [](const QList<Row> &rows) {
        QStringList out;
        out.reserve(rows.size());
        for (const Row &r : rows)
            out.append(r.path);
        return out;
    };
    const QStringList before = pathsOf(m_rows);
    const QStringList after = pathsOf(next);
    // `after` is `before` with some rows taken out, in the same order: remove just those (a list
    // keeps its current row and the keyboard focus when one entry goes).
    const auto onlyRemoved = [&before, &after] {
        if (after.size() >= before.size())
            return false;
        qsizetype j = 0;
        for (const QString &p : before) {
            if (j < after.size() && after.at(j) == p)
                ++j;
        }
        return j == after.size();
    };
    const int oldCount = int(m_rows.size());
    if (before == after) {
        m_rows = next;
        if (!m_rows.isEmpty())
            Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1));
    } else if (onlyRemoved()) {
        for (qsizetype i = before.size() - 1; i >= 0; --i) {
            if (after.contains(before.at(i)))
                continue;
            beginRemoveRows({}, int(i), int(i));
            m_rows.removeAt(i);
            endRemoveRows();
        }
        m_rows = next; // (the same rows, their names and times as stored now)
        if (!m_rows.isEmpty())
            Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1));
    } else {
        beginResetModel();
        m_rows = next;
        endResetModel();
    }
    if (oldCount != m_rows.size())
        Q_EMIT countChanged();
    Q_EMIT statesChanged();
    if (m_checksStarted && !unchecked.isEmpty())
        check(unchecked);
}

void RecentNotebooks::refresh()
{
    m_checksStarted = true;
    QStringList paths;
    paths.reserve(m_rows.size());
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        paths.append(m_rows.at(i).path);
        if (m_rows.at(i).state.isEmpty()) {
            m_rows[i].state = kChecking;
            Q_EMIT dataChanged(index(int(i)), index(int(i)), {AvailabilityRole});
        }
    }
    Q_EMIT statesChanged();
    check(paths);
}

void RecentNotebooks::check(const QStringList &paths)
{
    bool started = false;
    for (const QString &path : paths) {
        if (m_assumeAvailable && m_assumeAvailable(path)) {
            answer(path, 0, kAvailable);
            continue;
        }
#ifdef BRULION_RECENT_CHECK_THREADS
        // One check per folder at a time: a folder that never answers does not pile up threads
        // (its running check answers for the later ones).
        if (m_running.contains(path))
            continue;
        const quint64 generation = ++m_nextGeneration;
        m_running.insert(path, generation);
        // A thread of its own, never joined: a check stuck in the file system (a network share
        // that hangs) neither blocks a pool the app waits for at exit nor the exit itself. The
        // watcher hands the answer to this thread, and lets go of it when the model goes first.
        QPromise<QString> promise;
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, path, generation] {
            const QFuture<QString> future = watcher->future();
            answer(path, generation, future.resultCount() > 0 ? future.result() : kUnreachable);
            watcher->deleteLater();
        });
        watcher->setFuture(promise.future());
        std::thread([promise = std::move(promise), path, custom = m_probe]() mutable {
            promise.start();
            promise.addResult(custom ? custom(path) : probe(path));
            promise.finish();
        }).detach();
        started = true;
#else
        // The browser: the notebook lives in IndexedDB, nothing to check.
        answer(path, 0, kAvailable);
#endif
    }
    if (started)
        m_timeout->start(m_timeoutMs);
}

void RecentNotebooks::answer(const QString &path, quint64 generation, const QString &state)
{
    if (generation != 0 && m_running.value(path) != generation)
        return; // an older check's, or one an answer given at once replaced
    m_running.remove(path);
    if (m_running.isEmpty())
        m_timeout->stop();
    setState(path, state);
}

void RecentNotebooks::setState(const QString &path, const QString &state)
{
    const int row = indexOf(path);
    if (row < 0)
        return;
    if (state == kMissing && m_rows.at(row).temporary) {
        // A temporary folder that is gone never comes back.
        m_settings->forgetRecentVault(m_rows.at(row).path);
        return;
    }
    if (m_rows.at(row).state == state)
        return;
    m_rows[row].state = state;
    Q_EMIT dataChanged(index(row), index(row), {AvailabilityRole});
    Q_EMIT statesChanged();
}

void RecentNotebooks::timedOut()
{
    // Still no answer: a network share or a drive that hangs.
    const QStringList waiting = m_running.keys();
    for (const QString &path : waiting)
        setState(path, kUnreachable);
}

void RecentNotebooks::remove(const QString &path)
{
    m_settings->forgetRecentVault(path);
}

void RecentNotebooks::markMissing(const QString &path)
{
    setState(cleaned(path), kMissing);
}

int RecentNotebooks::dropVanishedTemporary()
{
    QStringList gone;
    for (const Row &r : std::as_const(m_rows)) {
        if (r.temporary && !QFileInfo(r.path).isDir())
            gone.append(r.path);
    }
    for (const QString &path : std::as_const(gone))
        m_settings->forgetRecentVault(path);
    return int(gone.size());
}

} // namespace brulion::shell
