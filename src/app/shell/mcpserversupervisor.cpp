#include "mcpserversupervisor.h"

#include "log/log.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::shell {

using logging::lcNet;

namespace {

/// A run this long counts as one that worked: the next end starts the delays from the first.
constexpr qint64 kStableSeconds = 60;
constexpr int kFirstDelayMs = 1000;
constexpr int kMaxDelayMs = 60000;
/// How long it may take to end when asked before it is killed.
constexpr int kStopMs = 3000;

} // namespace

McpServerSupervisor::McpServerSupervisor(QObject *parent) : QObject(parent), m_program(defaultProgram())
{
    m_restart.setSingleShot(true);
    connect(&m_restart, &QTimer::timeout, this, [this] {
        if (wanted())
            start();
    });
}

McpServerSupervisor::~McpServerSupervisor()
{
    stop();
}

bool McpServerSupervisor::available()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM) || !QT_CONFIG(process)
    return false;
#else
    return true;
#endif
}

QStringList McpServerSupervisor::defaultProgram()
{
    if (!available())
        return {};
    const QDir dir(QCoreApplication::applicationDirPath());
    for (const QString &name : {u"brulion-cli"_s, u"brulion-cli.exe"_s}) {
        const QFileInfo cli(dir.filePath(name));
        if (cli.isFile() && cli.isExecutable())
            return {QDir::cleanPath(cli.absoluteFilePath())};
    }
    // The app's binary runs the command line itself (`brulion cli …`: an AppImage, a build).
    return {QCoreApplication::applicationFilePath(), u"cli"_s};
}

void McpServerSupervisor::setProgram(const QStringList &program)
{
    if (program == m_program)
        return;
    m_program = program;
    stop();
    apply();
}

QStringList McpServerSupervisor::arguments(const Config &config)
{
    // (--compact: an error at its start is one JSON line too, as the line it prints when it listens.)
    QStringList args{u"--compact"_s, u"--vault"_s, config.vault, u"mcp"_s, u"serve"_s,
                     u"--port"_s,    QString::number(config.port)};
    if (!config.passwordFile.isEmpty())
        args << u"--password-file"_s << config.passwordFile;
    if (const QString url = config.publicUrl.trimmed(); !url.isEmpty())
        args << u"--public-url"_s << url;
    if (config.readOnly)
        args << u"--read-only"_s;
    return args;
}

bool McpServerSupervisor::wanted() const
{
    return m_config.enabled && !m_config.vault.isEmpty() && !m_program.isEmpty();
}

void McpServerSupervisor::setConfig(const Config &config)
{
    if (config == m_config)
        return;
    m_config = config;
    m_failures = 0;
    stop();
    apply();
}

void McpServerSupervisor::restart()
{
    m_failures = 0;
    stop();
    apply();
}

void McpServerSupervisor::apply()
{
    m_restart.stop();
    if (!m_config.enabled || m_program.isEmpty()) {
        setState(u"off"_s);
        return;
    }
    if (m_config.vault.isEmpty()) {
        setState(u"failed"_s, tr("Open a notebook first."));
        return;
    }
    if (m_config.passwordFile.isEmpty() || !QFileInfo::exists(m_config.passwordFile)) {
        setState(u"failed"_s, tr("Set a password first."));
        return;
    }
    start();
}

void McpServerSupervisor::start()
{
    stop();
    m_out.clear();
    m_err.clear();
    m_sawFirstLine = false;
    m_refused = false;
    m_lastWords.clear();
    m_endpoint.clear();
    auto process = std::make_unique<QProcess>();
    QProcess *p = process.get();
    connect(p, &QProcess::readyReadStandardOutput, this, &McpServerSupervisor::readOutput);
    connect(p, &QProcess::readyReadStandardError, this, &McpServerSupervisor::readErrors);
    connect(p, &QProcess::finished, this, &McpServerSupervisor::ended);
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_process.get() != p)
            return;
        qCWarning(lcNet, "mcp serve: %s cannot be started: %s", qUtf8Printable(m_program.value(0)),
                  qUtf8Printable(p->errorString()));
        m_process.release()->deleteLater();
        setState(u"failed"_s, tr("%1 cannot be started: %2").arg(QFileInfo(m_program.value(0)).fileName(), p->errorString()));
    });
    // (It speaks English in its log, whatever the app's language; its sign-in pages follow the
    // browser's.)
    const QStringList args = m_program.mid(1) + arguments(m_config);
    m_process = std::move(process);
    setState(u"starting"_s);
    qCInfo(lcNet, "mcp serve: starting for %s on port %d", qUtf8Printable(m_config.vault), m_config.port);
    m_process->start(m_program.value(0), args);
}

void McpServerSupervisor::stop()
{
    if (!m_process)
        return;
    std::unique_ptr<QProcess> process = std::move(m_process);
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
#ifdef Q_OS_WIN
        process->kill(); // (a console program ignores WM_CLOSE)
#else
        process->terminate();
        if (!process->waitForFinished(kStopMs))
            process->kill();
#endif
        process->waitForFinished(kStopMs);
        qCInfo(lcNet, "mcp serve: stopped");
    }
    m_endpoint.clear();
}

void McpServerSupervisor::readOutput()
{
    if (!m_process)
        return;
    m_out += m_process->readAllStandardOutput();
    if (m_sawFirstLine) {
        m_out.clear(); // (nothing more is said there)
        return;
    }
    const qsizetype end = m_out.indexOf('\n');
    if (end < 0) {
        if (m_out.size() > 1024 * 1024)
            m_out.clear();
        return;
    }
    m_sawFirstLine = true;
    // One JSON line when it listens ({ok, result: {endpoint, …}}), else the envelope of its error.
    const QJsonObject first = QJsonDocument::fromJson(m_out.left(end)).object();
    m_out.clear();
    if (first.value(u"ok"_s).toBool()) {
        m_endpoint = first.value(u"result"_s).toObject().value(u"endpoint"_s).toString();
        m_runningSince = QDateTime::currentDateTimeUtc();
        qCInfo(lcNet, "mcp serve: running at %s", qUtf8Printable(m_endpoint));
        setState(u"running"_s);
        return;
    }
    m_refused = true;
    const QString why = first.value(u"error"_s).toObject().value(u"message"_s).toString();
    m_error = why.isEmpty() ? tr("The server did not start.") : why;
    qCWarning(lcNet, "mcp serve: refused to start: %s", qUtf8Printable(m_error));
}

void McpServerSupervisor::readErrors()
{
    if (!m_process)
        return;
    m_err += m_process->readAllStandardError();
    qsizetype from = 0;
    for (qsizetype end = m_err.indexOf('\n'); end >= 0; end = m_err.indexOf('\n', from)) {
        const QString line = QString::fromUtf8(m_err.mid(from, end - from)).trimmed();
        from = end + 1;
        if (!line.isEmpty())
            addLog(line);
    }
    m_err.remove(0, from);
    if (m_err.size() > 64 * 1024)
        m_err.clear();
}

void McpServerSupervisor::addLog(const QString &line)
{
    // (The server never writes a token, a password or a query string: docs/CLI.md.)
    qCInfo(lcNet, "mcp serve: %s", qUtf8Printable(line));
    m_lastWords = line;
    m_log.append(line);
    while (m_log.size() > MaxLogLines)
        m_log.removeFirst();
    Q_EMIT logChanged();
}

void McpServerSupervisor::ended(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process)
        return;
    readErrors();
    readOutput();
    const bool ran = m_state == u"running";
    const bool stable = ran && m_runningSince.secsTo(QDateTime::currentDateTimeUtc()) >= kStableSeconds;
    m_process.release()->deleteLater();
    m_endpoint.clear();
    if (m_refused) {
        // Its settings or its notebook: the same start would fail again.
        setState(u"failed"_s, m_error);
        return;
    }
    const QString why = status == QProcess::CrashExit
                                ? tr("The server stopped unexpectedly.")
                                : (m_lastWords.isEmpty() ? tr("The server stopped (exit code %1).").arg(exitCode)
                                                         : m_lastWords);
    qCWarning(lcNet, "mcp serve: ended (%s, exit code %d)", status == QProcess::CrashExit ? "crashed" : "exited",
              exitCode);
    if (!ran && m_failures >= 5) {
        // It never got as far as listening, five times: it is not going to.
        setState(u"failed"_s, why);
        return;
    }
    if (stable)
        m_failures = 0;
    const int delay = m_testDelayMs > 0 ? m_testDelayMs : restartDelayMs(m_failures);
    ++m_failures;
    m_restart.start(delay);
    setState(u"waiting"_s, why);
}

int McpServerSupervisor::restartDelayMs(int failures)
{
    qint64 delay = kFirstDelayMs;
    for (int i = 0; i < failures && delay < kMaxDelayMs; ++i)
        delay *= 2;
    return int(std::min<qint64>(delay, kMaxDelayMs));
}

void McpServerSupervisor::setState(const QString &state, const QString &error)
{
    if (state == m_state && error == m_error)
        return;
    m_state = state;
    m_error = error;
    Q_EMIT changed();
}

bool McpServerSupervisor::reachable() const
{
    return m_endpoint.startsWith(u"https://"_s);
}

qint64 McpServerSupervisor::processId() const
{
    return m_process && m_process->state() == QProcess::Running ? m_process->processId() : 0;
}

bool McpServerSupervisor::copy(const QString &text) const
{
    QClipboard *clipboard = qobject_cast<QGuiApplication *>(QCoreApplication::instance()) ? QGuiApplication::clipboard()
                                                                                         : nullptr;
    if (!clipboard || text.isEmpty())
        return false;
    clipboard->setText(text);
    return true;
}

} // namespace brulion::shell
