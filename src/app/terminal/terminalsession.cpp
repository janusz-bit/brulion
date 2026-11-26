#include "terminalsession.h"

#include "pty.h"
#include "term/keys.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::terminal {

namespace {

/// The AppImage runtime's variables about the app itself: a program started in the terminal is not
/// part of the AppImage.
bool appImageVariable(const QString &entry)
{
    static const QStringList names = {u"APPIMAGE="_s, u"APPDIR="_s, u"ARGV0="_s, u"OWD="_s};
    for (const QString &name : names) {
        if (entry.startsWith(name))
            return true;
    }
    return false;
}

constexpr int MaxTitleLength = 80;
/// Whether a job runs, looked at this long after output or input…
constexpr int BusyCheckDelay = 120;
/// … and this often while one runs (a job that prints nothing would end unseen otherwise).
constexpr int BusyPollInterval = 2000;
/// Output this soon after the start is no activity (a shell's first prompt).
constexpr int QuietStart = 500;

} // namespace

TerminalSession::TerminalSession(QObject *parent)
    : QObject(parent), m_terminal(80, 24, 5000), m_pty(new platform::Pty(this))
{
    connect(m_pty, &platform::Pty::dataReceived, this, &TerminalSession::onData);
    connect(m_pty, &platform::Pty::finished, this, &TerminalSession::onFinished);
    connect(&m_terminal, &term::Terminal::reply, this, &TerminalSession::writeBytes);
    connect(&m_terminal, &term::Terminal::bell, this, &TerminalSession::onBell);
    connect(&m_terminal, &term::Terminal::titleChanged, this, &TerminalSession::titleChanged);
    connect(&m_terminal, &term::Terminal::workingDirectoryChanged, this, &TerminalSession::workingDirectoryChanged);
    connect(&m_terminal, &term::Terminal::modesChanged, this, &TerminalSession::modesChanged);
    m_busySoon.setSingleShot(true);
    m_busySoon.setInterval(BusyCheckDelay);
    connect(&m_busySoon, &QTimer::timeout, this, &TerminalSession::checkBusy);
    m_busyPoll.setInterval(BusyPollInterval);
    m_busyPoll.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_busyPoll, &QTimer::timeout, this, &TerminalSession::checkBusy);
}

TerminalSession::~TerminalSession()
{
    // The Pty's destructor hangs up on the program and reaps it; nothing is emitted any more.
    disconnect(m_pty, nullptr, this, nullptr);
}

void TerminalSession::setFolder(const QString &folder)
{
    if (folder == m_folder)
        return;
    m_folder = folder;
    Q_EMIT folderChanged();
}

void TerminalSession::setCommand(const QString &command)
{
    if (command == m_command)
        return;
    m_command = command;
    Q_EMIT commandChanged();
    Q_EMIT titleChanged();
}

void TerminalSession::setLabel(const QString &label)
{
    if (label == m_label)
        return;
    m_label = label;
    Q_EMIT labelChanged();
    Q_EMIT titleChanged();
}

QString TerminalSession::programName() const
{
    if (!m_command.isEmpty()) {
        QString line = m_command.simplified();
        if (line.size() > MaxTitleLength)
            line = line.left(MaxTitleLength - 1) + QChar(0x2026);
        return line;
    }
    const QString shell = platform::Pty::userShell();
    return shell.isEmpty() ? tr("Terminal") : QFileInfo(shell).fileName();
}

QString TerminalSession::title() const
{
    const QString own = m_terminal.title().trimmed();
    if (!own.isEmpty())
        return own;
    if (!m_label.isEmpty())
        return m_label;
    return programName();
}

QString TerminalSession::currentDirectory() const
{
    if (const QString live = m_pty->currentDirectory(); !live.isEmpty())
        return live;
    if (const QString reported = m_terminal.workingDirectory(); !reported.isEmpty())
        return reported;
    if (!m_folder.isEmpty() && QFileInfo(m_folder).isDir())
        return m_folder;
    return QDir::homePath();
}

void TerminalSession::setShown(bool shown)
{
    m_shownBy = std::max(0, m_shownBy + (shown ? 1 : -1));
    if (m_shownBy > 0)
        setMarks(false, false);
}

void TerminalSession::setMarks(bool activity, bool bell)
{
    if (activity == m_activity && bell == m_bellRung)
        return;
    m_activity = activity;
    m_bellRung = bell;
    Q_EMIT marksChanged();
}

void TerminalSession::onBell()
{
    if (m_shownBy == 0)
        setMarks(m_activity, true);
    Q_EMIT bell();
}

void TerminalSession::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    // (Idle, it becomes busy only after input or with output: no need to look in between.)
    if (busy)
        m_busyPoll.start();
    else
        m_busyPoll.stop();
    Q_EMIT busyChanged();
}

void TerminalSession::checkBusy()
{
    // The session's own command is the program (the shell may exec it: no job of its own).
    setBusy(m_running && (!m_command.isEmpty() || m_pty->hasForegroundJob()));
}

void TerminalSession::checkBusySoon()
{
    if (m_running && !m_busySoon.isActive())
        m_busySoon.start();
}

void TerminalSession::clear()
{
    m_terminal.screen().clearKeepingCursorLine();
    Q_EMIT cleared();
    Q_EMIT contentChanged();
}

bool TerminalSession::available()
{
    return platform::Pty::available();
}

void TerminalSession::setError(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    Q_EMIT errorStringChanged();
}

bool TerminalSession::start()
{
    if (m_running)
        return true;
    if (!available()) {
        setError(tr("There is no terminal on this device."));
        return false;
    }
    if (m_exited) {
        // Again: a new terminal, as if just opened.
        m_terminal.reset();
        Q_EMIT contentChanged();
    }
    const QString shell = platform::Pty::userShell();
    QStringList arguments;
    if (!m_command.isEmpty())
        arguments = {u"-lc"_s, m_command};
    QStringList environment = platform::Pty::defaultEnvironment();
    environment.removeIf(appImageVariable);
    QString error;
    const term::Screen &screen = m_terminal.screen();
    if (!m_pty->start(shell, arguments, m_folder, environment, screen.columns(), screen.rows(), &error)) {
        setError(error);
        return false;
    }
    m_commandLine = QStringList{shell} + arguments;
    setError(QString());
    m_running = true;
    m_exited = false;
    m_exitCode = -1;
    m_startedAt.start();
    Q_EMIT runningChanged();
    Q_EMIT titleChanged();
    Q_EMIT started();
    checkBusy();
    return true;
}

void TerminalSession::stop()
{
    if (m_running)
        m_pty->terminate();
}

void TerminalSession::write(const QString &text)
{
    writeBytes(text.toUtf8());
}

void TerminalSession::paste(const QString &text)
{
    writeBytes(term::encodePaste(text, m_terminal.modes().bracketedPaste));
}

void TerminalSession::writeBytes(const QByteArray &bytes)
{
    if (m_running && !bytes.isEmpty()) {
        m_pty->write(bytes);
        checkBusySoon();
    }
}

void TerminalSession::resize(int columns, int rows)
{
    if (columns < 1 || rows < 1)
        return;
    const term::Screen &screen = m_terminal.screen();
    if (columns == screen.columns() && rows == screen.rows())
        return;
    m_terminal.resize(columns, rows);
    m_pty->resize(screen.columns(), screen.rows());
    Q_EMIT sizeChanged();
    Q_EMIT contentChanged();
}

void TerminalSession::typeWhenReady(const QString &text)
{
    m_typeWhenReady = text;
    m_typedCommand = text;
}

void TerminalSession::onData(const QByteArray &data)
{
    m_terminal.feed(data);
    if (!m_typeWhenReady.isEmpty()) {
        // (Its line editor set the terminal up before it printed the prompt: the text is its.)
        writeBytes(term::encodeText(m_typeWhenReady));
        m_typeWhenReady.clear();
    }
    if (m_shownBy == 0 && !m_activity && (!m_startedAt.isValid() || m_startedAt.elapsed() >= QuietStart))
        setMarks(true, m_bellRung);
    checkBusySoon();
    Q_EMIT contentChanged();
}

void TerminalSession::onFinished(int exitCode, bool crashed)
{
    m_running = false;
    m_exited = true;
    m_exitCode = crashed ? 128 + exitCode : exitCode;
    m_busyPoll.stop();
    m_busySoon.stop();
    setBusy(false);
    Q_EMIT runningChanged();
    Q_EMIT exited(m_exitCode);
}

} // namespace brulion::terminal
