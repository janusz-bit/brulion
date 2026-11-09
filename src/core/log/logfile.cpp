#include "log/logfile_p.h"

#include <QDir>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace brulion::logging::detail {

LogFile::LogFile(const QString &directory, qint64 maxBytes)
    : m_directory(directory), m_maxBytes(std::max<qint64>(4096, maxBytes))
{ }

LogFile::~LogFile()
{
    flush();
}

QString LogFile::path() const
{
    return m_directory + u"/brulion.log"_s;
}

QString LogFile::rotatedPath() const
{
    return m_directory + u"/brulion.log.1"_s;
}

QStringList LogFile::files() const
{
    QStringList out;
    for (const QString &name : {rotatedPath(), path()}) {
        if (QFileInfo::exists(name))
            out.append(name);
    }
    return out;
}

bool LogFile::open()
{
    if (m_directory.isEmpty()) {
        fail(u"no folder for the log"_s);
        return false;
    }
    if (!QDir().mkpath(m_directory)) {
        fail(u"cannot create %1"_s.arg(m_directory));
        return false;
    }
    // The log names notes and the server: owner-only, like the secrets folder.
    QFile::setPermissions(m_directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    if (!openFile(QIODevice::WriteOnly | QIODevice::Append))
        return false;
    if (m_file.size() >= m_maxBytes)
        rotate();
    return m_file.isOpen();
}

bool LogFile::openFile(QIODevice::OpenMode mode)
{
    m_file.setFileName(path());
    if (!m_file.open(mode)) {
        fail(m_file.errorString());
        return false;
    }
    m_file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void LogFile::append(const QString &line)
{
    if (m_failed)
        return;
    m_pending += line.toUtf8();
    m_pending += '\n';
}

bool LogFile::flush()
{
    if (m_failed)
        return false;
    if (m_pending.isEmpty() || !m_file.isOpen())
        return m_file.isOpen();
    const QByteArray data = std::exchange(m_pending, {});
    if (m_file.write(data) != data.size() || !m_file.flush()) {
        fail(m_file.errorString());
        return false;
    }
    if (m_file.size() >= m_maxBytes)
        rotate();
    return !m_failed; // reopening after the rotation can fail
}

void LogFile::rotate()
{
    m_file.close();
    QFile::remove(rotatedPath());
    QFile::rename(path(), rotatedPath());
    openFile(QIODevice::WriteOnly | QIODevice::Append);
}

void LogFile::clear()
{
    m_pending.clear();
    if (m_failed)
        return;
    m_file.close();
    QFile::remove(rotatedPath());
    openFile(QIODevice::WriteOnly | QIODevice::Truncate);
}

QByteArray LogFile::readAll()
{
    flush();
    QByteArray out;
    for (const QString &name : {rotatedPath(), path()}) {
        if (QFile f(name); f.open(QIODevice::ReadOnly))
            out += f.readAll();
    }
    return out;
}

QByteArray LogFile::tail(qint64 bytes) const
{
    for (const QString &name : {path(), rotatedPath()}) {
        QFile f(name);
        if (!f.open(QIODevice::ReadOnly) || f.size() == 0)
            continue;
        if (f.size() > bytes)
            f.seek(f.size() - bytes);
        return f.readAll();
    }
    return {};
}

void LogFile::fail(const QString &error)
{
    m_failed = true;
    m_error = error;
    m_pending.clear();
    if (m_file.isOpen())
        m_file.close();
}

} // namespace brulion::logging::detail
