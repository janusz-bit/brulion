// Timing helpers for log lines: SlowScope and human-readable sizes / durations (English, '.' as
// the decimal point — log lines are technical and stable).
#pragma once

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QString>

namespace brulion::logging {

/// Logs how long a scope took: info when it took at least `thresholdMs`, else debug (only
/// formatted when the category's debug output is on).
///   SlowScope slow(lcVault(), "LocalStore flush", 200);
///   …; slow.setDetail(formatBytes(bytes.size()));
///   -> "LocalStore flush took 840 ms (18.4 MB)"
class SlowScope {
public:
    SlowScope(const QLoggingCategory &category, const char *what, int thresholdMs = 100);
    ~SlowScope();
    SlowScope(const SlowScope &) = delete;
    SlowScope &operator=(const SlowScope &) = delete;

    void setDetail(const QString &detail) { m_detail = detail; }
    qint64 elapsedMs() const { return m_timer.elapsed(); }

private:
    const QLoggingCategory &m_category;
    const char *m_what;
    int m_threshold;
    QString m_detail;
    QElapsedTimer m_timer;
};

/// "312 B", "48.2 KB", "18.4 MB", "1.2 GB" (1 KB = 1024 B).
QString formatBytes(qint64 bytes);
/// "0 ms", "312 ms", "14.2 s", "3 min 05 s", "1 h 02 min".
QString formatMs(qint64 ms);

} // namespace brulion::logging
