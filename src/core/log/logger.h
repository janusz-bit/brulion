// Logger — Brulion's event log (docs/ARCHITECTURE.md "Logging"). Captures every Qt message
// (qCDebug… of brulion.*, Qt's own qt.*, QML warnings, console.*) through a chained message
// handler: redacts it, collapses repeats, rate-limits bursts of debug / info lines, keeps the
// newest `capacity` entries in memory, appends them to a rotating file and hands the redacted
// text on to the previous handler (stderr / logcat; not rate-limited). Thread-safe: messages
// come from worker threads too; the Logger object itself lives on the thread that installed it
// (the GUI thread). Idle cost ~0: no timers; one queued call per burst of messages.
//
//   auto *logger = Logger::install({.directory = dir, .sessionInfo = "Brulion 0.2.1 (linux)"});
//   connect(app, &QCoreApplication::aboutToQuit, logger, &Logger::endSession);
//   logger->setVerbosity(Verbosity::Detailed);   // brulion.* debug lines too
//   logger->exportText({"Brulion — event log", …});
#pragma once

#include "log/entry.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace brulion::logging {

enum class Verbosity { Normal, Detailed };
QString verbosityName(Verbosity verbosity); ///< "normal" | "detailed"
std::optional<Verbosity> verbosityFromName(QStringView name);

struct LoggerOptions {
    int capacity = 5000;               ///< entries kept in memory
    QString directory;                 ///< file sink folder; "" = memory only (browser, tests)
    qint64 maxFileBytes = 1024 * 1024; ///< brulion.log rotates to brulion.log.1 above this
    bool installHandler = true;        ///< false: tests feed append() directly
    bool forwardToPrevious = true;     ///< keep stderr / logcat output (redacted text)
    int repeatWindowMs = 30'000;       ///< identical consecutive messages within it collapse
    int burst = 200;                   ///< per category + level, debug / info only:
    int perSecond = 20;                ///<   token bucket; dropped lines are counted and noted
    int notifyIntervalMs = 250;        ///< appended() at most this often
    std::function<qint64()> clock;     ///< ms since epoch; default: the system clock (tests)
    QString sessionInfo;               ///< "Brulion 0.2.1 (android 15, arm64-v8a, Qt 6.11.2)"
};

class Logger : public QObject {
    Q_OBJECT
public:
    /// The log file names in the directory.
    static QString fileName() { return QStringLiteral("brulion.log"); }
    static QString rotatedFileName() { return QStringLiteral("brulion.log.1"); }

    /// The installed logger, nullptr before install().
    static Logger *instance();
    /// Create the logger; with installHandler, install the message handler (chained). Call
    /// once, right after the QCoreApplication exists (a second call returns the first logger).
    /// Opens the file, writes a "session start" line and, when the previous session in the file
    /// has no "session end", an info line saying when it stopped (killed by the system or
    /// crashed).
    static Logger *install(const LoggerOptions &options);
    /// Restore the previous handler and delete the logger (tests).
    static void uninstall();

    ~Logger() override;
    Q_DISABLE_COPY_MOVE(Logger)

    /// Normal: QLoggingCategory::setFilterRules("brulion.sync.debug=false\n…") for the brulion
    /// categories (verbosityCategories()); Detailed: "=true". QT_LOGGING_RULES / QT_LOGGING_CONF
    /// still take precedence (Qt's rule order).
    void setVerbosity(Verbosity verbosity);
    Verbosity verbosity() const;
    void setMaxFileBytes(qint64 bytes);
    qint64 maxFileBytes() const;

    /// Record one message: redact, truncate, collapse, rate-limit, ring, file. Thread-safe.
    void append(Level level, QStringView category, QStringView message);
    /// Overload for QString expressions (string builders).
    void append(Level level, QStringView category, const QString &message)
    {
        append(level, category, QStringView(message));
    }

    /// Entries with seq > afterSeq, oldest first (a copy). The newest entry may have changed
    /// since (repeat): read from lastSeq() - 1 to see it again.
    QList<Entry> entries(quint64 afterSeq = 0) const;
    quint64 lastSeq() const;
    int capacity() const;
    /// Warnings, errors and fatals recorded since start or clear().
    int problemCount() const;
    /// When this session started (ms since epoch).
    qint64 sessionStart() const;
    /// The files held entries of earlier sessions when this one started (cheap; until clear()).
    bool hasEarlierEntries() const;
    /// Entries of earlier sessions from the files (oldest first, at most the newest `max`; only
    /// the newest 2 MB of the files are read).
    QList<Entry> earlierEntries(int max = 5000) const;

    QString directory() const;
    /// Existing log files, oldest first.
    QStringList files() const;
    /// Write pending lines now. Also done at once for warnings and worse, by the coalesced queued
    /// call otherwise, and by endSession(). False when the file cannot be written.
    bool flush();
    /// Empty the ring and the files; keeps one line "Log cleared".
    void clear();
    /// The log as text for sharing: `header` lines, a "----" line, then the files (or the ring
    /// when there is no file sink), redacted once more; at most `maxBytes` (UTF-8), the newest
    /// kept, cut at a line.
    QString exportText(const QStringList &header, qint64 maxBytes = 4 * 1024 * 1024) const;
    /// "session end" line + flush (QCoreApplication::aboutToQuit). Idempotent.
    void endSession();

Q_SIGNALS:
    /// New entries were recorded (coalesced: at most every notifyIntervalMs, on the logger's
    /// thread).
    void appended(quint64 lastSeq);
    void cleared();
    void verbosityChanged();

private:
    explicit Logger(const LoggerOptions &options);
    void scheduleNotify();
    void deliver();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace brulion::logging
