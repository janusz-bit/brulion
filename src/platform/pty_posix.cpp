// platform::Pty on POSIX systems (pty.h): posix_openpt() — no libutil —, fork() and execve().
#include "pty.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSocketNotifier>
#include <QTimer>

#include <algorithm>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <vector>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

extern char **environ;

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

/// Output waiting for the program to read, at most (a paste into a program that never reads).
constexpr qsizetype MaxPendingInput = 16 * 1024 * 1024;
/// Reads of the master per notifier activation: other events get their turn under a flood.
constexpr int ReadsPerActivation = 4;
constexpr int ReadChunk = 64 * 1024;

int dupAbove2(int fd)
{
    if (fd > 2)
        return fd;
    const int moved = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
    ::close(fd);
    return moved;
}

void setCloseOnExec(int fd)
{
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0)
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

/// The path of the pty's other side ("" when unknown). ptsname_r() where there is one: ptsname()'s
/// buffer is shared by every thread.
QByteArray slavePath(int master)
{
#if defined(__linux__) || defined(__APPLE__)
    char name[256];
    if (::ptsname_r(master, name, sizeof name) != 0)
        return {};
    return QByteArray(name);
#else
    const char *name = ::ptsname(master);
    return name ? QByteArray(name) : QByteArray();
#endif
}

/// The executable `program` names: itself when it holds a '/', else the first match in `path`.
QString findProgram(const QString &program, const QString &path)
{
    if (program.contains(u'/'))
        return QFileInfo(program).isExecutable() && QFileInfo(program).isFile() ? program : QString();
    const QStringList folders =
            (path.isEmpty() ? QStringLiteral("/usr/bin:/bin") : path).split(u':', Qt::SkipEmptyParts);
    for (const QString &folder : folders) {
        const QFileInfo candidate(QDir(folder).filePath(program));
        if (candidate.isFile() && candidate.isExecutable())
            return candidate.filePath();
    }
    return {};
}

QString environmentValue(const QStringList &environment, QStringView name)
{
    for (const QString &entry : environment) {
        if (entry.size() > name.size() && entry.at(name.size()) == u'=' && QStringView(entry).left(name.size()) == name)
            return entry.mid(name.size() + 1);
    }
    return {};
}

/// The child's environment (pty.h): TERM, COLORTERM, PWD set, COLUMNS / LINES dropped, a UTF-8
/// locale when none is named.
QStringList childEnvironment(const QStringList &environment, const QString &workingDirectory)
{
    static const QStringList dropped = {u"TERM"_s, u"COLORTERM"_s, u"COLUMNS"_s, u"LINES"_s, u"TERMCAP"_s, u"PWD"_s};
    QStringList out;
    out.reserve(environment.size() + 4);
    bool hasLocale = false;
    for (const QString &entry : environment) {
        const qsizetype eq = entry.indexOf(u'=');
        if (eq <= 0)
            continue;
        const QString name = entry.left(eq);
        if (dropped.contains(name))
            continue;
        if ((name == u"LANG" || name == u"LC_ALL" || name == u"LC_CTYPE") && eq + 1 < entry.size())
            hasLocale = true;
        out.append(entry);
    }
    out.append(u"TERM=xterm-256color"_s);
    out.append(u"COLORTERM=truecolor"_s);
    out.append(u"PWD="_s + workingDirectory);
    if (!hasLocale) {
#if defined(__APPLE__)
        out.append(u"LC_CTYPE=UTF-8"_s);
#else
        out.append(u"LC_CTYPE=C.UTF-8"_s);
#endif
    }
    return out;
}

/// NULL-terminated pointers into `strings` (which must outlive them).
std::vector<char *> pointers(QList<QByteArray> &strings)
{
    std::vector<char *> out;
    out.reserve(size_t(strings.size()) + 1);
    for (QByteArray &s : strings)
        out.push_back(s.data());
    out.push_back(nullptr);
    return out;
}

/// The child after fork(): only async-signal-safe calls until execve().
[[noreturn]] void runChild(int slave, int master, int errorPipe, int maxFd, const char *path, char *const argv[],
                           char *const envp[], const char *cwd)
{
    ::setsid();
    ::ioctl(slave, TIOCSCTTY, 0);
    ::dup2(slave, 0);
    ::dup2(slave, 1);
    ::dup2(slave, 2);
    if (slave > 2)
        ::close(slave);
    if (master > 2)
        ::close(master);
    // Nothing else of the app's: every other descriptor closed (the error pipe closes on exec).
#if defined(__linux__) && defined(SYS_close_range)
    bool closed = errorPipe <= 3 || ::syscall(SYS_close_range, 3u, unsigned(errorPipe - 1), 0u) == 0;
    closed = closed && ::syscall(SYS_close_range, unsigned(errorPipe + 1), ~0u, 0u) == 0;
    if (!closed)
#endif
    {
        for (int fd = 3; fd < maxFd; ++fd) {
            if (fd != errorPipe)
                ::close(fd);
        }
    }
    struct sigaction defaults;
    std::memset(&defaults, 0, sizeof defaults);
    defaults.sa_handler = SIG_DFL;
    sigemptyset(&defaults.sa_mask);
    for (int sig = 1; sig < NSIG; ++sig) {
        if (sig != SIGKILL && sig != SIGSTOP)
            ::sigaction(sig, &defaults, nullptr);
    }
    sigset_t none;
    sigemptyset(&none);
    ::sigprocmask(SIG_SETMASK, &none, nullptr);
    if (cwd[0] != '\0' && ::chdir(cwd) != 0) {
        // Stays in the app's folder: the shell shows where it is.
    }
    ::execve(path, argv, envp);
    const int error = errno;
    ssize_t written;
    do {
        written = ::write(errorPipe, &error, sizeof error);
    } while (written < 0 && errno == EINTR);
    ::_exit(127);
}

} // namespace

struct Pty::Private {
    Pty *q = nullptr;
    pid_t pid = 0;
    int master = -1;
    int pidfd = -1;
    int columns = 80;
    int rows = 24;
    QSocketNotifier *readNotifier = nullptr;
    QSocketNotifier *writeNotifier = nullptr;
    QSocketNotifier *exitNotifier = nullptr;
    QTimer *exitPoll = nullptr;
    QTimer *killTimer = nullptr;
    QByteArray pending;
    bool eof = false;

    void onReadable();
    void flush();
    void checkExit();
    void finish(int status);
    void signalGroups(int sig);
    void cleanup();
    QByteArray drain(int maxReads);
};

QByteArray Pty::Private::drain(int maxReads)
{
    QByteArray out;
    if (master < 0 || eof)
        return out;
    char buffer[ReadChunk];
    for (int i = 0; i < maxReads; ++i) {
        const ssize_t n = ::read(master, buffer, sizeof buffer);
        if (n > 0) {
            out.append(buffer, qsizetype(n));
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        // 0 or EIO: no process holds the pty's other side any more.
        eof = true;
        if (readNotifier)
            readNotifier->setEnabled(false);
        break;
    }
    return out;
}

void Pty::Private::onReadable()
{
    const QByteArray data = drain(ReadsPerActivation);
    QPointer<Pty> guard(q);
    if (!data.isEmpty())
        Q_EMIT q->dataReceived(data);
    if (!guard)
        return;
    if (eof) {
        // The child is most likely gone: look now, and soon again if it is still being reaped.
        checkExit();
        if (pid > 0 && exitPoll)
            exitPoll->start(20);
    }
}

void Pty::Private::flush()
{
    while (!pending.isEmpty() && master >= 0) {
        const ssize_t n = ::write(master, pending.constData(), size_t(pending.size()));
        if (n > 0) {
            pending.remove(0, qsizetype(n));
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (writeNotifier)
                writeNotifier->setEnabled(true);
            return;
        }
        pending.clear();
        break;
    }
    if (writeNotifier)
        writeNotifier->setEnabled(false);
}

void Pty::Private::checkExit()
{
    if (pid <= 0)
        return;
    int status = 0;
    pid_t r;
    do {
        r = ::waitpid(pid, &status, WNOHANG);
    } while (r < 0 && errno == EINTR);
    if (r == pid)
        finish(status);
    else if (r < 0 && errno == ECHILD)
        finish(-1); // reaped by someone else: the status is lost
}

void Pty::Private::finish(int status)
{
    // What the program wrote before it ended comes first.
    const QByteArray rest = drain(64);
    QPointer<Pty> guard(q);
    if (!rest.isEmpty())
        Q_EMIT q->dataReceived(rest);
    if (!guard)
        return;
    pid = 0;
    cleanup();
    int exitCode = -1;
    bool crashed = true;
    if (status >= 0 && WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
        crashed = false;
    } else if (status >= 0 && WIFSIGNALED(status)) {
        exitCode = WTERMSIG(status);
    }
    Q_EMIT q->finished(exitCode, crashed);
}

void Pty::Private::signalGroups(int sig)
{
    if (pid <= 0)
        return;
    if (master >= 0) {
        const pid_t foreground = ::tcgetpgrp(master);
        if (foreground > 0 && foreground != pid)
            ::kill(-foreground, sig);
    }
    if (::kill(-pid, sig) != 0)
        ::kill(pid, sig);
}

void Pty::Private::cleanup()
{
    // Deleted later: cleanup() may run inside one of their own activated() signals.
    for (QSocketNotifier **notifier : {&readNotifier, &writeNotifier, &exitNotifier}) {
        if (*notifier) {
            (*notifier)->setEnabled(false);
            (*notifier)->deleteLater();
            *notifier = nullptr;
        }
    }
    if (exitPoll)
        exitPoll->stop();
    if (killTimer)
        killTimer->stop();
    if (pidfd >= 0) {
        ::close(pidfd);
        pidfd = -1;
    }
    if (master >= 0) {
        ::close(master);
        master = -1;
    }
    pending.clear();
    eof = false;
}

Pty::Pty(QObject *parent) : QObject(parent), d(std::make_unique<Private>())
{
    d->q = this;
}

Pty::~Pty()
{
    if (d->pid > 0) {
        // Hang up: the shell and its jobs get SIGHUP; what ignores it gets SIGKILL a moment later.
        d->signalGroups(SIGHUP);
        d->signalGroups(SIGCONT);
        pid_t foreground = d->master >= 0 ? ::tcgetpgrp(d->master) : -1;
        const pid_t pid = d->pid;
        d->pid = 0;
        d->cleanup();
        int status = 0;
        bool reaped = false;
        for (int i = 0; i < 20 && !reaped; ++i) {
            const pid_t r = ::waitpid(pid, &status, WNOHANG);
            reaped = r == pid || (r < 0 && errno == ECHILD);
            if (!reaped)
                ::usleep(10000);
        }
        if (!reaped) {
            if (foreground > 0 && foreground != pid)
                ::kill(-foreground, SIGKILL);
            ::kill(-pid, SIGKILL);
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
        }
    }
    d->cleanup();
}

bool Pty::available()
{
    return true;
}

QStringList Pty::defaultEnvironment()
{
    QStringList out;
    for (char **entry = environ; entry && *entry; ++entry)
        out.append(QString::fromLocal8Bit(*entry));
    return out;
}

QString Pty::userShell()
{
    auto usable = [](const QString &path) {
        const QFileInfo info(path);
        return info.isAbsolute() && info.isFile() && info.isExecutable();
    };
    const QString shell = qEnvironmentVariable("SHELL");
    if (usable(shell))
        return shell;
    // The account's login shell when $SHELL is missing (a desktop launcher may not set it).
    std::vector<char> buffer(16384);
    struct passwd entry;
    struct passwd *found = nullptr;
    if (::getpwuid_r(::getuid(), &entry, buffer.data(), buffer.size(), &found) == 0 && found && found->pw_shell) {
        const QString login = QFile::decodeName(found->pw_shell);
        if (usable(login))
            return login;
    }
    return u"/bin/sh"_s;
}

bool Pty::start(const QString &program, const QStringList &arguments, const QString &workingDirectory,
                const QStringList &environment, int columns, int rows, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (d->pid > 0)
        return fail(tr("A program is running already."));

    d->columns = std::max(1, columns);
    d->rows = std::max(1, rows);
    QString cwd = workingDirectory;
    if (cwd.isEmpty() || !QFileInfo(cwd).isDir())
        cwd = QDir::homePath();
    cwd = QDir(cwd).absolutePath();
    const QStringList env = childEnvironment(environment, cwd);
    const QString path = findProgram(program, environmentValue(env, u"PATH"));
    if (path.isEmpty())
        return fail(tr("Program not found: %1").arg(program));

    // Everything the child needs, made before fork(): it may not allocate afterwards.
    QList<QByteArray> argStrings;
    argStrings.append(QFile::encodeName(program));
    for (const QString &argument : arguments)
        argStrings.append(argument.toLocal8Bit());
    QList<QByteArray> envStrings;
    for (const QString &entry : env)
        envStrings.append(entry.toLocal8Bit());
    std::vector<char *> argv = pointers(argStrings);
    std::vector<char *> envp = pointers(envStrings);
    const QByteArray pathBytes = QFile::encodeName(path);
    const QByteArray cwdBytes = QFile::encodeName(cwd);
    struct rlimit limit;
    int maxFd = 1024;
    if (::getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY)
        maxFd = int(std::min<rlim_t>(limit.rlim_cur, 65536));
    else
        maxFd = 65536;

    // The pty.
    int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0)
        return fail(tr("Cannot open a pseudo-terminal: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
    master = dupAbove2(master);
    setCloseOnExec(master);
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        const int e = errno;
        ::close(master);
        return fail(tr("Cannot open a pseudo-terminal: %1").arg(QString::fromLocal8Bit(std::strerror(e))));
    }
    const QByteArray slaveName = slavePath(master);
    if (slaveName.isEmpty()) {
        ::close(master);
        return fail(tr("Cannot open a pseudo-terminal."));
    }
    int slave = ::open(slaveName.constData(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave < 0) {
        const int e = errno;
        ::close(master);
        return fail(tr("Cannot open a pseudo-terminal: %1").arg(QString::fromLocal8Bit(std::strerror(e))));
    }
    slave = dupAbove2(slave);
    struct winsize size;
    std::memset(&size, 0, sizeof size);
    size.ws_col = static_cast<unsigned short>(d->columns);
    size.ws_row = static_cast<unsigned short>(d->rows);
    ::ioctl(slave, TIOCSWINSZ, &size);
    struct termios modes;
    if (::tcgetattr(slave, &modes) == 0) {
#ifdef IUTF8
        modes.c_iflag |= IUTF8; // the line editor's erase takes whole UTF-8 characters
#endif
        modes.c_cc[VERASE] = 0x7f; // Backspace sends DEL
        ::tcsetattr(slave, TCSANOW, &modes);
    }

    // The exec's failure comes back through a pipe that closes on a successful exec.
    int pipeFds[2];
    if (::pipe(pipeFds) != 0) {
        const int e = errno;
        ::close(master);
        ::close(slave);
        return fail(QString::fromLocal8Bit(std::strerror(e)));
    }
    const int errorRead = dupAbove2(pipeFds[0]);
    const int errorWrite = dupAbove2(pipeFds[1]);
    setCloseOnExec(errorRead);
    setCloseOnExec(errorWrite);

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int e = errno;
        ::close(master);
        ::close(slave);
        ::close(errorRead);
        ::close(errorWrite);
        return fail(tr("Cannot start %1: %2").arg(program, QString::fromLocal8Bit(std::strerror(e))));
    }
    if (pid == 0)
        runChild(slave, master, errorWrite, maxFd, pathBytes.constData(), argv.data(), envp.data(),
                 cwdBytes.constData());

    ::close(slave);
    ::close(errorWrite);
    int execError = 0;
    ssize_t n;
    do {
        n = ::read(errorRead, &execError, sizeof execError);
    } while (n < 0 && errno == EINTR);
    ::close(errorRead);
    if (n == ssize_t(sizeof execError)) {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
        ::close(master);
        return fail(tr("Cannot start %1: %2").arg(program, QString::fromLocal8Bit(std::strerror(execError))));
    }

    const int flags = ::fcntl(master, F_GETFL);
    ::fcntl(master, F_SETFL, flags | O_NONBLOCK);
    d->pid = pid;
    d->master = master;
    d->eof = false;
    d->pending.clear();
    d->readNotifier = new QSocketNotifier(master, QSocketNotifier::Read, this);
    connect(d->readNotifier, &QSocketNotifier::activated, this, [this] { d->onReadable(); });
    d->writeNotifier = new QSocketNotifier(master, QSocketNotifier::Write, this);
    d->writeNotifier->setEnabled(false);
    connect(d->writeNotifier, &QSocketNotifier::activated, this, [this] { d->flush(); });
    if (!d->exitPoll) {
        d->exitPoll = new QTimer(this);
        connect(d->exitPoll, &QTimer::timeout, this, [this] { d->checkExit(); });
    }
#if defined(__linux__) && defined(SYS_pidfd_open)
    // Linux 5.3+: the child's exit as a readable descriptor (pidfd_open sets close-on-exec).
    d->pidfd = int(::syscall(SYS_pidfd_open, pid, 0));
    if (d->pidfd >= 0) {
        d->exitNotifier = new QSocketNotifier(d->pidfd, QSocketNotifier::Read, this);
        connect(d->exitNotifier, &QSocketNotifier::activated, this, [this] { d->checkExit(); });
    }
#endif
    if (d->pidfd < 0)
        d->exitPoll->start(250);
    return true;
}

bool Pty::isRunning() const
{
    return d->pid > 0;
}

qint64 Pty::processId() const
{
    return d->pid;
}

bool Pty::write(const QByteArray &data)
{
    if (d->pid <= 0 || d->master < 0)
        return false;
    if (data.isEmpty())
        return true;
    if (d->pending.size() + data.size() > MaxPendingInput)
        return false;
    d->pending.append(data);
    d->flush();
    return true;
}

bool Pty::resize(int columns, int rows)
{
    d->columns = std::max(1, columns);
    d->rows = std::max(1, rows);
    if (d->master < 0)
        return false;
    struct winsize size;
    std::memset(&size, 0, sizeof size);
    size.ws_col = static_cast<unsigned short>(d->columns);
    size.ws_row = static_cast<unsigned short>(d->rows);
    return ::ioctl(d->master, TIOCSWINSZ, &size) == 0;
}

void Pty::terminate(int graceMs)
{
    if (d->pid <= 0)
        return;
    d->signalGroups(SIGHUP);
    d->signalGroups(SIGCONT);
    if (!d->killTimer) {
        d->killTimer = new QTimer(this);
        d->killTimer->setSingleShot(true);
        connect(d->killTimer, &QTimer::timeout, this, [this] { d->signalGroups(SIGKILL); });
    }
    d->killTimer->start(std::max(0, graceMs));
}

} // namespace brulion::platform
