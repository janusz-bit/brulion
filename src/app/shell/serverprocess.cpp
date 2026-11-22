#include "serverprocess.h"

#include "log/categories.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

#if QT_CONFIG(process)
#include <QProcess>
#endif

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// The most of its output kept before a line ends (a server that writes nonsense).
constexpr qsizetype kMaxLineBytes = 64 * 1024;

/// A log line without the time the server puts in front ("2026-11-22 10:30:00 paired …").
QString withoutTime(const QString &line)
{
    static const QRegularExpression time(u"^\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d "_s);
    QString out = line;
    out.remove(time);
    return out;
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace

ServerProcess::ServerProcess(const QString &name, QObject *parent) : QObject(parent), m_name(name)
{
    m_retry = new QTimer(this);
    m_retry->setSingleShot(true);
    connect(m_retry, &QTimer::timeout, this, &ServerProcess::launch);
}

ServerProcess::~ServerProcess()
{
    m_pendingStart = false;
    halt(true);
#if QT_CONFIG(process)
    // (One still stopping from before: waited for too — the app quits.)
    if (QProcess *p = m_stopping.data(); p && p->state() != QProcess::NotRunning) {
        p->disconnect(this);
        if (!p->waitForFinished(m_stopMs)) {
            p->kill();
            p->waitForFinished(1000);
        }
    }
#endif
}

void ServerProcess::setProgram(const QString &program, const QStringList &arguments)
{
    m_program = program;
    m_arguments = arguments;
}

qint64 ServerProcess::retryIn() const
{
    return m_state == State::Waiting && m_retry->isActive() ? m_retry->remainingTime() : 0;
}

qint64 ServerProcess::processId() const
{
#if QT_CONFIG(process)
    return m_process ? m_process->processId() : 0;
#else
    return 0;
#endif
}

void ServerProcess::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged();
}

void ServerProcess::start()
{
    halt(false);
    m_failures = 0;
    m_error.clear();
    m_errorCode.clear();
#if QT_CONFIG(process)
    // The one stopping still holds its port: started once it has ended.
    if (m_stopping && m_stopping->state() != QProcess::NotRunning) {
        m_pendingStart = true;
        setState(State::Starting);
        return;
    }
#endif
    launch();
}

void ServerProcess::stop()
{
    const bool was = m_state != State::Stopped;
    m_pendingStart = false;
    halt(false);
    m_failures = 0;
    m_error.clear();
    m_errorCode.clear();
    if (was)
        qCInfo(logging::lcApp, "%s: stopped", qUtf8Printable(m_name));
    setState(State::Stopped);
}

void ServerProcess::halt(bool wait)
{
    m_retry->stop();
#if QT_CONFIG(process)
    QProcess *p = std::exchange(m_process, nullptr);
    m_announcement = {};
    m_announced = false;
    if (!p)
        return;
    p->disconnect(this);
    if (p->state() == QProcess::NotRunning) {
        p->deleteLater();
        return;
    }
    // Its standard input closed: a server started with --stop-on-eof ends by itself (its agents
    // too); terminated after a while, killed after another.
    p->closeWriteChannel();
    if (wait) {
        if (!p->waitForFinished(m_stopMs)) {
            p->terminate();
            if (!p->waitForFinished(m_stopMs)) {
                p->kill();
                p->waitForFinished(1000);
            }
        }
        delete p;
        return;
    }
    p->setParent(nullptr);
    connect(p, &QProcess::finished, p, &QObject::deleteLater);
    m_stopping = p;
    connect(p, &QProcess::finished, this, [this] {
        if (std::exchange(m_pendingStart, false))
            launch();
    });
    const QPointer<QProcess> guard(p);
    const int ms = m_stopMs;
    QTimer::singleShot(ms, p, [guard, ms] {
        if (!guard || guard->state() == QProcess::NotRunning)
            return;
        guard->terminate();
        QTimer::singleShot(ms, guard, [guard] {
            if (guard && guard->state() != QProcess::NotRunning)
                guard->kill();
        });
    });
#else
    Q_UNUSED(wait)
#endif
}

void ServerProcess::launch()
{
#if QT_CONFIG(process)
    if (m_program.isEmpty()) {
        m_error = tr("No brulion-cli was found to run it");
        setState(State::Failed);
        return;
    }
    m_out.clear();
    m_early.clear();
    m_err.clear();
    m_lastErrorLine.clear();
    m_announced = false;
    m_announcement = {};
    auto *p = new QProcess(this);
    m_process = p;
    if (m_ownEnvironment)
        p->setProcessEnvironment(m_environment);
    p->setProgram(m_program);
    p->setArguments(m_arguments);
    connect(p, &QProcess::readyReadStandardOutput, this, &ServerProcess::readOutput);
    connect(p, &QProcess::readyReadStandardError, this, &ServerProcess::readErrors);
    connect(p, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        ended(code, status == QProcess::CrashExit);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || p != m_process)
            return;
        m_error = tr("%1 did not start: %2").arg(m_program, p->errorString());
        qCWarning(logging::lcApp, "%s: %s", qUtf8Printable(m_name), qUtf8Printable(m_error));
        m_process = nullptr;
        p->deleteLater();
        setState(State::Failed);
    });
    m_startedAt = nowMs();
    qCInfo(logging::lcApp, "%s: starting %s %s", qUtf8Printable(m_name), qUtf8Printable(m_program),
           qUtf8Printable(m_arguments.join(u' ')));
    setState(State::Starting);
    p->start(QIODevice::ReadWrite);
#else
    m_error = tr("Programs cannot run here");
    setState(State::Failed);
#endif
}

void ServerProcess::readOutput()
{
#if QT_CONFIG(process)
    if (!m_process)
        return;
    m_out += m_process->readAllStandardOutput();
    while (true) {
        const qsizetype nl = m_out.indexOf('\n');
        if (nl < 0) {
            if (m_out.size() > kMaxLineBytes)
                m_out.clear();
            return;
        }
        const QByteArray line = m_out.left(nl).trimmed();
        if (!m_announced && m_early.size() < kMaxLineBytes)
            m_early += m_out.left(nl + 1);
        m_out.remove(0, nl + 1);
        if (m_announced || line.isEmpty())
            continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.value(u"ok"_s).toBool()) {
            m_announced = true;
            m_announcement = o.value(u"result"_s).toObject();
            m_error.clear();
            m_errorCode.clear();
            qCInfo(logging::lcApp, "%s: serving %s", qUtf8Printable(m_name),
                   qUtf8Printable(m_announcement.value(u"url"_s).toString()));
            setState(State::Running);
            Q_EMIT announced(m_announcement);
        } else if (o.contains(u"error"_s)) {
            const QJsonObject e = o.value(u"error"_s).toObject();
            m_error = e.value(u"message"_s).toString();
            m_errorCode = e.value(u"code"_s).toString();
        }
    }
#endif
}

void ServerProcess::readErrors()
{
#if QT_CONFIG(process)
    if (!m_process)
        return;
    m_err += m_process->readAllStandardError();
    while (true) {
        const qsizetype nl = m_err.indexOf('\n');
        if (nl < 0) {
            if (m_err.size() > kMaxLineBytes)
                m_err.clear();
            return;
        }
        const QString line = withoutTime(QString::fromUtf8(m_err.left(nl)).trimmed());
        m_err.remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        m_lastErrorLine = line.left(300);
        qCInfo(logging::lcApp, "%s: %s", qUtf8Printable(m_name), qUtf8Printable(line));
        Q_EMIT logLine(line);
    }
#endif
}

void ServerProcess::ended(int exitCode, bool crashed)
{
#if QT_CONFIG(process)
    if (!m_process)
        return;
    readOutput();
    readErrors();
    if (m_error.isEmpty() && !m_announced) {
        // (A refusal as indented JSON over several lines: read whole.)
        const QJsonObject e = QJsonDocument::fromJson(m_early + m_out).object().value(u"error"_s).toObject();
        m_error = e.value(u"message"_s).toString();
        m_errorCode = e.value(u"code"_s).toString();
    }
    QProcess *p = std::exchange(m_process, nullptr);
    p->deleteLater();
    m_announcement = {};
    const bool ranLong = m_announced && nowMs() - m_startedAt >= m_stableMs;
    m_announced = false;
    if (m_error.isEmpty())
        m_error = !m_lastErrorLine.isEmpty() ? m_lastErrorLine
                  : crashed                  ? tr("it crashed")
                                             : tr("it ended (exit code %1)").arg(exitCode);
    // A usage error or a refusal: started the same way, it would end the same way.
    if (!crashed && (exitCode == 2 || exitCode == 7)) {
        qCWarning(logging::lcApp, "%s: refused to start: %s", qUtf8Printable(m_name), qUtf8Printable(m_error));
        setState(State::Failed);
        return;
    }
    m_failures = ranLong ? 1 : m_failures + 1;
    const int delay = m_delays.isEmpty() ? 5000 : m_delays.at(std::min<qsizetype>(m_failures - 1, m_delays.size() - 1));
    qCWarning(logging::lcApp, "%s: ended (%s), starting again in %d s", qUtf8Printable(m_name),
              qUtf8Printable(m_error), delay / 1000);
    m_retry->start(delay);
    setState(State::Waiting);
#else
    Q_UNUSED(exitCode)
    Q_UNUSED(crashed)
#endif
}

} // namespace brulion::shell
