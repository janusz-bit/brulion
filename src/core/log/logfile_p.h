// Internal: the rotating file sink of the Logger (brulion.log + brulion.log.1). Not thread-safe
// itself: the Logger calls it under its mutex.
#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace brulion::logging::detail {

class LogFile {
public:
    LogFile(const QString &directory, qint64 maxBytes);
    ~LogFile();
    LogFile(const LogFile &) = delete;
    LogFile &operator=(const LogFile &) = delete;

    /// Create the folder (owner-only) and open brulion.log for appending. False when that fails
    /// (errorString()); the sink is then disabled.
    bool open();
    QString errorString() const { return m_error; }

    QString path() const;
    QString rotatedPath() const;
    /// Existing files, oldest first.
    QStringList files() const;

    void setMaxBytes(qint64 bytes) { m_maxBytes = std::max<qint64>(4096, bytes); }
    qint64 maxBytes() const { return m_maxBytes; }

    /// Queue `line` (a newline is added).
    void append(const QString &line);
    /// Write the queued lines; rotates when the file reached maxBytes. False on a write error or
    /// when brulion.log cannot be reopened after rotating (the sink disables itself; errorString()).
    bool flush();
    /// Truncate both files.
    void clear();
    /// The files' text (rotated first), after flush().
    QByteArray readAll();
    /// The last `bytes` of brulion.log (or of brulion.log.1 when brulion.log is empty).
    QByteArray tail(qint64 bytes) const;

private:
    /// (Re)open brulion.log with `mode`, owner-only. False when that fails (the sink disables itself).
    bool openFile(QIODevice::OpenMode mode);
    void rotate();
    void fail(const QString &error);

    QString m_directory;
    qint64 m_maxBytes;
    QFile m_file;
    QByteArray m_pending;
    bool m_failed = false;
    QString m_error;
};

} // namespace brulion::logging::detail
