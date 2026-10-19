#include "log/logger.h"

#include "log/categories.h"
#include "log/logfile_p.h"
#include "log/redact.h"

#include <QAtomicPointer>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QMutex>
#include <QTimer>

#include <atomic>
#include <cstdio>

using namespace Qt::StringLiterals;

namespace brulion::logging {

namespace {

QAtomicPointer<Logger> s_instance;
QtMessageHandler s_previous = nullptr;
bool s_forward = true;

constexpr QStringView kSessionStart = u"=== session start";
constexpr QStringView kSessionEnd = u"=== session end";
constexpr qint64 kTailBytes = 64 * 1024;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    static thread_local bool inHandler = false;
    Logger *logger = s_instance.loadAcquire();
    const QtMessageHandler previous = s_previous;
    if (inHandler || !logger) {
        // A message produced while handling one (or after uninstall): only the previous handler.
        if (previous)
            previous(type, context, message);
        return;
    }
    inHandler = true;
    const QString redacted = redact(message);
    const QString category = context.category ? QString::fromUtf8(context.category) : u"default"_s;
    logger->append(levelFromQt(type), category, redacted);
    if (type == QtFatalMsg)
        logger->flush(); // the previous handler aborts
    inHandler = false;
    if (s_forward && previous)
        previous(type, context, redacted);
}

QString utcOffsetText(qint64 now)
{
    const int seconds = QDateTime::fromMSecsSinceEpoch(now).offsetFromUtc();
    const int minutes = qAbs(seconds) / 60;
    return u"UTC%1%2:%3"_s.arg(seconds < 0 ? u'-' : u'+')
            .arg(minutes / 60, 2, 10, QChar(u'0'))
            .arg(minutes % 60, 2, 10, QChar(u'0'));
}

struct Bucket {
    double tokens = 0;
    qint64 refilled = 0;
    int dropped = 0;
};

} // namespace

QString verbosityName(Verbosity verbosity)
{
    return verbosity == Verbosity::Detailed ? u"detailed"_s : u"normal"_s;
}

std::optional<Verbosity> verbosityFromName(QStringView name)
{
    if (name == u"normal")
        return Verbosity::Normal;
    if (name == u"detailed")
        return Verbosity::Detailed;
    return std::nullopt;
}

struct Logger::Private {
    LoggerOptions options;
    mutable QMutex mutex;
    // The ring: `ring` holds up to capacity entries, `head` is the index of the oldest one.
    QList<Entry> ring;
    qsizetype head = 0;
    quint64 seq = 0;
    int problems = 0;
    std::unique_ptr<detail::LogFile> file;
    bool fileWarned = false;
    /// Repeats of the newest entry not yet written to the file.
    int unwrittenRepeats = 0;
    QHash<QString, Bucket> buckets;
    Verbosity verbosity = Verbosity::Normal;
    qint64 sessionStart = 0;
    bool sessionEnded = false;
    bool hadEarlier = false; ///< the files held entries when this session started
    std::atomic<bool> notifyPending{false};
    QElapsedTimer sinceNotify;
    bool notifyTimerArmed = false;

    qint64 now() const { return options.clock ? options.clock() : QDateTime::currentMSecsSinceEpoch(); }

    qsizetype count() const { return ring.size(); }
    const Entry &at(qsizetype i) const { return ring.at((head + i) % ring.size()); }
    Entry &newest() { return ring[(head + ring.size() - 1) % ring.size()]; }

    void push(Entry entry)
    {
        if (ring.size() < options.capacity) {
            ring.append(std::move(entry));
        } else {
            ring[head] = std::move(entry);
            head = (head + 1) % ring.size();
        }
    }

    /// Write the repeat note of the newest entry, if any repeats are unwritten.
    void writeRepeats()
    {
        if (unwrittenRepeats <= 0 || ring.isEmpty())
            return;
        Entry note = newest();
        note.repeat = unwrittenRepeats + 1;
        if (file)
            file->append(formatRepeatLine(note));
        unwrittenRepeats = 0;
    }

    /// Store a new entry (mutex held).
    void store(Level level, const QString &category, const QString &message, qint64 time)
    {
        writeRepeats();
        Entry entry;
        entry.seq = ++seq;
        entry.time = time;
        entry.level = level;
        entry.category = category;
        entry.message = message;
        if (level >= Level::Warning)
            ++problems;
        if (file)
            file->append(formatLine(entry));
        push(std::move(entry));
    }

    /// Token bucket of debug / info lines (mutex held). True: the line may be stored.
    bool admit(Level level, const QString &category, qint64 time)
    {
        if (level >= Level::Warning || options.perSecond <= 0)
            return true;
        const QString key = category + u'\x1f' + levelLetter(level);
        auto it = buckets.find(key);
        if (it == buckets.end())
            it = buckets.insert(key, Bucket{double(options.burst), time, 0});
        Bucket &b = it.value();
        b.tokens = qMin<double>(options.burst, b.tokens + double(time - b.refilled) * options.perSecond / 1000.0);
        b.refilled = time;
        if (b.tokens < 1.0) {
            ++b.dropped;
            return false;
        }
        b.tokens -= 1.0;
        if (b.dropped > 0) {
            const int dropped = std::exchange(b.dropped, 0);
            store(Level::Info, category, u"(dropped %1 lines of %2: too many at once)"_s.arg(dropped).arg(category),
                  time);
        }
        return true;
    }

    /// Notes for buckets that dropped lines and have refilled since (mutex held).
    void noteDrops(qint64 time)
    {
        for (auto it = buckets.begin(); it != buckets.end(); ++it) {
            Bucket &b = it.value();
            if (b.dropped == 0)
                continue;
            const double tokens = b.tokens + double(time - b.refilled) * options.perSecond / 1000.0;
            if (tokens < 1.0)
                continue;
            const QString category = it.key().section(u'\x1f', 0, 0);
            store(Level::Info, category,
                  u"(dropped %1 lines of %2: too many at once)"_s.arg(std::exchange(b.dropped, 0)).arg(category), time);
        }
    }

    bool flushFile()
    {
        if (!file)
            return false;
        const bool ok = file->flush();
        if (!ok && !fileWarned) {
            fileWarned = true;
            // Never through the handler (recursion): straight to the previous one.
            if (s_previous) {
                const QByteArray text =
                        u"Brulion: the event log file cannot be written: %1"_s.arg(file->errorString()).toUtf8();
                s_previous(QtWarningMsg, QMessageLogContext(), QString::fromUtf8(text));
            }
        }
        return ok;
    }
};

Logger *Logger::instance()
{
    return s_instance.loadAcquire();
}

Logger *Logger::install(const LoggerOptions &options)
{
    if (Logger *existing = instance())
        return existing;
    auto *logger = new Logger(options);
    s_instance.storeRelease(logger);
    if (options.installHandler) {
        s_forward = options.forwardToPrevious;
        s_previous = qInstallMessageHandler(messageHandler);
    }
    return logger;
}

void Logger::uninstall()
{
    Logger *logger = s_instance.fetchAndStoreAcquire(nullptr);
    if (!logger)
        return;
    if (logger->d->options.installHandler) {
        qInstallMessageHandler(s_previous);
        s_previous = nullptr;
    }
    delete logger;
}

Logger::Logger(const LoggerOptions &options) : d(std::make_unique<Private>())
{
    d->options = options;
    d->options.capacity = qMax(16, options.capacity);
    d->ring.reserve(qMin(d->options.capacity, 1024));
    d->sessionStart = d->now();
    d->sinceNotify.start();
    QString previousSession;
    if (!options.directory.isEmpty()) {
        d->file = std::make_unique<detail::LogFile>(options.directory, options.maxFileBytes);
        // Did the previous session end properly? (Android kills apps without aboutToQuit.)
        const QList<Entry> tail = parseLines(QString::fromUtf8(d->file->tail(kTailBytes)));
        d->hadEarlier = !tail.isEmpty();
        qsizetype start = -1;
        for (qsizetype i = tail.size() - 1; i >= 0; --i) {
            if (tail.at(i).message.startsWith(kSessionStart)) {
                start = i;
                break;
            }
        }
        if (start >= 0) {
            bool ended = false;
            for (qsizetype i = start + 1; i < tail.size() && !ended; ++i)
                ended = tail.at(i).message.startsWith(kSessionEnd);
            if (!ended) {
                previousSession =
                        u"Previous session (started %1) ended without closing (stopped by the system or "
                        u"crashed); its last entry: %2"_s.arg(
                                formatTime(tail.at(start).time),
                                formatTime(tail.last().lastTime > 0 ? tail.last().lastTime : tail.last().time));
            }
        }
        if (!d->file->open()) {
            const QString text = u"Brulion: the event log file cannot be opened: %1"_s.arg(d->file->errorString());
            std::fprintf(stderr, "%s\n", text.toLocal8Bit().constData());
            d->file.reset();
        }
    }
    const QString info = options.sessionInfo.isEmpty() ? QCoreApplication::applicationName() : options.sessionInfo;
    append(Level::Info, u"brulion.app",
           u"=== session start: %1, pid %2, %3 ==="_s.arg(info)
                   .arg(QCoreApplication::applicationPid())
                   .arg(utcOffsetText(d->sessionStart)));
    if (!previousSession.isEmpty())
        append(Level::Info, u"brulion.app", previousSession);
    flush();
}

Logger::~Logger()
{
    endSession();
}

void Logger::setVerbosity(Verbosity verbosity)
{
    const bool changed = d->verbosity != verbosity;
    d->verbosity = verbosity;
    QStringList rules;
    const QString value = verbosity == Verbosity::Detailed ? u"true"_s : u"false"_s;
    for (const QString &category : verbosityCategories())
        rules.append(category + u".debug="_s + value);
    QLoggingCategory::setFilterRules(rules.join(u'\n'));
    if (changed) {
        qCInfo(lcApp).noquote() << u"Log verbosity: %1"_s.arg(verbosityName(verbosity));
        Q_EMIT verbosityChanged();
    }
}

Verbosity Logger::verbosity() const
{
    return d->verbosity;
}

void Logger::setMaxFileBytes(qint64 bytes)
{
    QMutexLocker locker(&d->mutex);
    d->options.maxFileBytes = bytes;
    if (d->file)
        d->file->setMaxBytes(bytes);
}

qint64 Logger::maxFileBytes() const
{
    QMutexLocker locker(&d->mutex);
    return d->file ? d->file->maxBytes() : d->options.maxFileBytes;
}

void Logger::append(Level level, QStringView category, QStringView message)
{
    const QString text = truncateMessage(redact(message));
    const QString cat = category.isEmpty() ? u"default"_s : category.toString();
    bool urgent = level >= Level::Warning;
    {
        QMutexLocker locker(&d->mutex);
        const qint64 time = d->now();
        if (!d->ring.isEmpty()) {
            Entry &last = d->newest();
            const qint64 lastTime = last.lastTime > 0 ? last.lastTime : last.time;
            if (last.level == level && last.category == cat && last.message == text
                && time - lastTime <= d->options.repeatWindowMs) {
                ++last.repeat;
                last.lastTime = time;
                ++d->unwrittenRepeats;
                urgent = false; // already written once
                locker.unlock();
                scheduleNotify();
                return;
            }
        }
        if (!d->admit(level, cat, time))
            return;
        d->store(level, cat, text, time);
        if (urgent)
            d->flushFile();
    }
    scheduleNotify();
}

void Logger::scheduleNotify()
{
    if (d->notifyPending.exchange(true))
        return;
    QMetaObject::invokeMethod(this, [this] { deliver(); }, Qt::QueuedConnection);
}

void Logger::deliver()
{
    const qint64 wait = d->options.notifyIntervalMs - d->sinceNotify.elapsed();
    if (wait > 0) {
        if (!d->notifyTimerArmed) {
            d->notifyTimerArmed = true;
            QTimer::singleShot(int(wait), this, [this] {
                d->notifyTimerArmed = false;
                deliver();
            });
        }
        return;
    }
    d->notifyPending.store(false);
    d->sinceNotify.restart();
    quint64 last = 0;
    {
        QMutexLocker locker(&d->mutex);
        d->noteDrops(d->now());
        if (d->unwrittenRepeats > 0 && !d->ring.isEmpty()
            && d->now() - d->newest().lastTime > d->options.repeatWindowMs)
            d->writeRepeats();
        d->flushFile();
        last = d->seq;
    }
    Q_EMIT appended(last);
}

QList<Entry> Logger::entries(quint64 afterSeq) const
{
    QMutexLocker locker(&d->mutex);
    QList<Entry> out;
    const qsizetype n = d->count();
    // Entries are in seq order: skip the old ones from the end backwards.
    qsizetype first = n;
    while (first > 0 && d->at(first - 1).seq > afterSeq)
        --first;
    out.reserve(n - first);
    for (qsizetype i = first; i < n; ++i)
        out.append(d->at(i));
    return out;
}

quint64 Logger::lastSeq() const
{
    QMutexLocker locker(&d->mutex);
    return d->seq;
}

int Logger::capacity() const
{
    return d->options.capacity;
}

int Logger::problemCount() const
{
    QMutexLocker locker(&d->mutex);
    return d->problems;
}

qint64 Logger::sessionStart() const
{
    return d->sessionStart;
}

bool Logger::hasEarlierEntries() const
{
    QMutexLocker locker(&d->mutex);
    return d->hadEarlier;
}

QList<Entry> Logger::earlierEntries(int max) const
{
    QByteArray bytes;
    {
        QMutexLocker locker(&d->mutex);
        if (!d->file)
            return {};
        bytes = d->file->readAll();
    }
    // Bounded work on the GUI thread: only the newest part of the files (a whole line onwards).
    constexpr qsizetype kMaxEarlierBytes = 2 * 1024 * 1024;
    if (bytes.size() > kMaxEarlierBytes) {
        bytes = bytes.right(kMaxEarlierBytes);
        const qsizetype nl = bytes.indexOf('\n');
        bytes = nl >= 0 ? bytes.mid(nl + 1) : QByteArray();
    }
    QList<Entry> all = parseLines(QString::fromUtf8(bytes));
    QList<Entry> out;
    for (const Entry &e : std::as_const(all)) {
        if (e.time < d->sessionStart)
            out.append(e);
    }
    if (out.size() > max)
        out = out.mid(out.size() - max);
    return out;
}

QString Logger::directory() const
{
    return d->file ? d->options.directory : QString();
}

QStringList Logger::files() const
{
    QMutexLocker locker(&d->mutex);
    return d->file ? d->file->files() : QStringList();
}

bool Logger::flush()
{
    QMutexLocker locker(&d->mutex);
    return d->flushFile();
}

void Logger::clear()
{
    {
        QMutexLocker locker(&d->mutex);
        d->ring.clear();
        d->head = 0;
        d->problems = 0;
        d->unwrittenRepeats = 0;
        d->buckets.clear();
        d->hadEarlier = false;
        if (d->file)
            d->file->clear();
    }
    Q_EMIT cleared();
    append(Level::Info, u"brulion.app", u"Log cleared");
}

QString Logger::exportText(const QStringList &header, qint64 maxBytes) const
{
    QByteArray body;
    {
        QMutexLocker locker(&d->mutex);
        if (d->unwrittenRepeats > 0)
            d->writeRepeats();
        if (d->file) {
            body = d->file->readAll();
        } else {
            for (qsizetype i = 0; i < d->count(); ++i) {
                body += formatLine(d->at(i)).toUtf8();
                body += '\n';
            }
        }
    }
    const QByteArray head = (header.join(u'\n') + u"\n----\n"_s).toUtf8();
    const qint64 room = qMax<qint64>(0, maxBytes - head.size());
    bool shortened = false;
    if (body.size() > room) {
        shortened = true;
        qsizetype cut = body.size() - room;
        // Start at a line that begins an entry (not a continuation line).
        while (cut < body.size()) {
            const qsizetype nl = body.indexOf('\n', cut);
            if (nl < 0) {
                cut = body.size();
                break;
            }
            cut = nl + 1;
            if (cut < body.size() && body.at(cut) != '\t')
                break;
        }
        body = body.mid(cut);
    }
    QString text = QString::fromUtf8(head);
    if (shortened)
        text += u"(older entries left out)\n"_s;
    text += redact(QString::fromUtf8(body));
    return text;
}

void Logger::endSession()
{
    {
        QMutexLocker locker(&d->mutex);
        if (d->sessionEnded)
            return;
        d->sessionEnded = true;
    }
    append(Level::Info, u"brulion.app", u"=== session end ===");
    QMutexLocker locker(&d->mutex);
    d->writeRepeats();
    d->flushFile();
}

} // namespace brulion::logging
