#include "terminalsession.h"

#include "pty.h"
#include "term/keys.h"

#include <QFileInfo>

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

} // namespace

TerminalSession::TerminalSession(QObject *parent)
    : QObject(parent), m_terminal(80, 24, 5000), m_pty(new platform::Pty(this))
{
    connect(m_pty, &platform::Pty::dataReceived, this, &TerminalSession::onData);
    connect(m_pty, &platform::Pty::finished, this, &TerminalSession::onFinished);
    connect(&m_terminal, &term::Terminal::reply, this, &TerminalSession::writeBytes);
    connect(&m_terminal, &term::Terminal::bell, this, &TerminalSession::bell);
    connect(&m_terminal, &term::Terminal::titleChanged, this, &TerminalSession::titleChanged);
    connect(&m_terminal, &term::Terminal::workingDirectoryChanged, this, &TerminalSession::workingDirectoryChanged);
    connect(&m_terminal, &term::Terminal::modesChanged, this, &TerminalSession::modesChanged);
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
    Q_EMIT runningChanged();
    Q_EMIT titleChanged();
    Q_EMIT started();
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
    if (m_running && !bytes.isEmpty())
        m_pty->write(bytes);
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

void TerminalSession::onData(const QByteArray &data)
{
    m_terminal.feed(data);
    Q_EMIT contentChanged();
}

void TerminalSession::onFinished(int exitCode, bool crashed)
{
    m_running = false;
    m_exited = true;
    m_exitCode = crashed ? 128 + exitCode : exitCode;
    Q_EMIT runningChanged();
    Q_EMIT exited(m_exitCode);
}

} // namespace brulion::terminal
