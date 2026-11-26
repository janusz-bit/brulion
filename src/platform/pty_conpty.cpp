// platform::Pty on Windows (pty.h): a pseudo console (ConPTY). CreatePseudoConsole and its two
// siblings are looked up in kernel32 at run time — Windows 10 1809 has them; on older Windows the
// app still starts and available() is false.
//
// One run:
//
//   app ──input pipe──▶ conhost (the pseudo console) ◀──console API── the program and its children
//   app ◀─output pipe── conhost: the screen rendered as UTF-8 VT
//
// * A reader thread reads the output pipe (anonymous pipes have no overlapped I/O) into a buffer the
//   GUI thread takes in slices of 256 KB (queued calls); it waits while 4 MB are not taken, so a
//   flood waits in ConPTY's pipe, not in memory. A writer thread writes the input queue: WriteFile
//   blocks while conhost is busy, never the GUI thread (and never a deadlock with a program that
//   writes while we write).
// * CreateProcessW with EXTENDED_STARTUPINFO_PRESENT and PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
//   suspended until it is in a job object (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE): every process it
//   starts is in the job too, and ends with it. STARTF_USESTDHANDLES with no handles and no handle
//   inheritance: the program's standard handles are the console's even when the app's own are
//   redirected (ctest, a service), and nothing else of the app's reaches it.
// * The end (Microsoft's documented order): the process handle signals (QWinEventNotifier) →
//   ClosePseudoConsole, on a thread of its own because it waits for conhost, while the reader keeps
//   draining the output pipe (conhost paints a last frame and can block until it is read) →
//   conhost exits, the output pipe breaks → the last output, then finished(). Programs the shell
//   left on the console get CTRL_CLOSE_EVENT; ones without a console (a GUI program started from
//   it) stay: the job no longer ends them.
// * terminate(): the console closed at once (CTRL_CLOSE_EVENT: cmd, PowerShell, node end), the job —
//   the whole tree — ended after the grace period. The destructor does the same with 200 ms.
//
// ConPTY answers the terminal's queries itself and asks few: without PSEUDOCONSOLE_INHERIT_CURSOR
// it never waits for a cursor position report; it may ask for focus events (?1004h, which the
// emulator supports) and for win32-input-mode (?9001h, which the emulator ignores: keys stay VT
// sequences, which ConPTY reads too).
#include "pty.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QWinEventNotifier>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <condition_variable>
#include <cwchar>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

/// Input waiting for the program, at most (a paste into a program that never reads).
constexpr qsizetype MaxPendingInput = 16 * 1024 * 1024;
/// Output the reader keeps before it waits for the GUI thread to take some.
constexpr qsizetype MaxBufferedOutput = 4 * 1024 * 1024;
/// Output handed to dataReceived() at once: other events get their turn under a flood.
constexpr qsizetype MaxDelivery = 256 * 1024;
constexpr DWORD ReadChunk = 64 * 1024;
constexpr DWORD PipeSize = 64 * 1024;
/// The exit code of what terminate() and the destructor end with the job: STATUS_CONTROL_C_EXIT,
/// the code a console program has after the console's close.
constexpr UINT KilledExitCode = 0xC000013A;
/// How long after the program's end its console may take to close and deliver the rest.
constexpr int SettleMs = 10000;
constexpr int MaxCommandLine = 32766;
/// ProcThreadAttributeValue(22, FALSE, TRUE, FALSE): SDKs before 10.0.17763 do not name it.
constexpr DWORD_PTR PseudoConsoleAttribute = 0x00020016;

// ---- ConPTY's functions, looked up at run time ------------------------------------------------

using PseudoConsole = void *;
using CreatePseudoConsoleFn = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, PseudoConsole *);
using ResizePseudoConsoleFn = HRESULT(WINAPI *)(PseudoConsole, COORD);
using ClosePseudoConsoleFn = void(WINAPI *)(PseudoConsole);

struct ConPtyApi {
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;
    bool ok() const { return create && resize && close; }
};

template<typename Fn>
Fn function(HMODULE module, const char *name)
{
    // (FARPROC to the function's real type: the documented way to call what GetProcAddress finds.)
    return reinterpret_cast<Fn>(reinterpret_cast<void *>(::GetProcAddress(module, name)));
}

const ConPtyApi &conPty()
{
    static const ConPtyApi api = [] {
        ConPtyApi a;
        if (const HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll")) {
            a.create = function<CreatePseudoConsoleFn>(kernel, "CreatePseudoConsole");
            a.resize = function<ResizePseudoConsoleFn>(kernel, "ResizePseudoConsole");
            a.close = function<ClosePseudoConsoleFn>(kernel, "ClosePseudoConsole");
        }
        return a;
    }();
    return api;
}

// ---- Small helpers -----------------------------------------------------------------------------

/// Windows' text for an error code (FormatMessageW, in the system's language), without the line
/// break it ends with.
QString systemMessage(DWORD code)
{
    wchar_t *buffer = nullptr;
    const DWORD size = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                                | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text = size && buffer ? QString::fromWCharArray(buffer, int(size)).trimmed() : QString();
    if (buffer)
        ::LocalFree(buffer);
    if (text.isEmpty())
        text = u"0x"_s + QString::number(quint32(code), 16).toUpper();
    return text;
}

void closeHandle(HANDLE &handle)
{
    if (handle && handle != INVALID_HANDLE_VALUE)
        ::CloseHandle(handle);
    handle = nullptr;
}

/// Waits for a thread at most `ms`, then lets it go (closing the handle does not stop it): true
/// when it ended.
bool joinThread(HANDLE &thread, DWORD ms)
{
    if (!thread)
        return true;
    const bool ended = ::WaitForSingleObject(thread, ms) == WAIT_OBJECT_0;
    closeHandle(thread);
    return ended;
}

QString environmentName(const QString &entry)
{
    // ("=C:=C:\\x": cmd.exe's current folder of a drive — a name that starts with '='.)
    const qsizetype eq = entry.indexOf(u'=', 1);
    return eq < 0 ? QString() : entry.left(eq);
}

QString environmentValue(const QStringList &environment, const QString &name)
{
    for (auto it = environment.crbegin(); it != environment.crend(); ++it) {
        if (environmentName(*it).compare(name, Qt::CaseInsensitive) == 0)
            return it->mid(name.size() + 1);
    }
    return {};
}

/// The child's environment (pty.h): one entry a name (the last one; names in any case), the
/// terminal's own variables replaced, sorted as CreateProcess wants it (by name, ignoring case).
QStringList childEnvironment(const QStringList &environment)
{
    static const QStringList dropped = {u"TERM"_s,    u"COLORTERM"_s,  u"COLUMNS"_s,      u"LINES"_s,
                                        u"TERMCAP"_s, u"WT_SESSION"_s, u"WT_PROFILE_ID"_s};
    QStringList out;
    QStringList names;
    for (const QString &entry : environment) {
        const QString name = environmentName(entry);
        if (name.isEmpty())
            continue;
        bool drop = false;
        for (const QString &d : dropped)
            drop = drop || name.compare(d, Qt::CaseInsensitive) == 0;
        if (drop)
            continue;
        const QString key = name.toUpper();
        const qsizetype at = names.indexOf(key);
        if (at >= 0) {
            out[at] = entry;
            continue;
        }
        names.append(key);
        out.append(entry);
    }
    out.append(u"COLORTERM=truecolor"_s);
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        return environmentName(a).compare(environmentName(b), Qt::CaseInsensitive) < 0;
    });
    return out;
}

/// The block CreateProcessW takes with CREATE_UNICODE_ENVIRONMENT: NAME=value\0 … \0.
std::vector<wchar_t> environmentBlock(const QStringList &environment)
{
    std::vector<wchar_t> block;
    for (const QString &entry : environment) {
        const std::wstring w = entry.toStdWString();
        block.insert(block.end(), w.begin(), w.end());
        block.push_back(L'\0');
    }
    if (block.empty())
        block.push_back(L'\0');
    block.push_back(L'\0');
    return block;
}

bool isBatchFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix();
    return suffix.compare(u"bat"_s, Qt::CaseInsensitive) == 0 || suffix.compare(u"cmd"_s, Qt::CaseInsensitive) == 0;
}

/// The file `base` names: itself when it has an extension, else with .exe or .com.
QString existingProgram(const QString &base)
{
    const QFileInfo info(base);
    if (!info.suffix().isEmpty())
        return info.isFile() ? info.absoluteFilePath() : QString();
    for (const QString &suffix : {u".exe"_s, u".com"_s}) {
        const QFileInfo candidate(base + suffix);
        if (candidate.isFile())
            return candidate.absoluteFilePath();
    }
    return {};
}

/// The executable `program` names: a path (with a separator or a drive), else the first match in
/// the folders of `path` — never the current folder, which CreateProcess's own search would try.
QString findProgram(const QString &program, const QString &path)
{
    if (program.isEmpty())
        return {};
    if (program.contains(u'/') || program.contains(u'\\') || program.contains(u':'))
        return existingProgram(program);
    for (QString folder : path.split(u';', Qt::SkipEmptyParts)) {
        folder = folder.trimmed();
        if (folder.size() >= 2 && folder.startsWith(u'"') && folder.endsWith(u'"'))
            folder = folder.mid(1, folder.size() - 2);
        if (folder.isEmpty() || !QDir::isAbsolutePath(folder))
            continue;
        const QString found = existingProgram(QDir(folder).filePath(program));
        if (!found.isEmpty())
            return found;
    }
    return {};
}

/// `argument` as CommandLineToArgvW and the C runtime read it back: quoted when it holds white
/// space or a quote (or is empty); backslashes doubled before a quote — escaped as \" — and before
/// the closing one.
QString quoteArgument(const QString &argument)
{
    const bool plain = !argument.isEmpty()
            && std::none_of(argument.begin(), argument.end(), [](QChar c) {
                   return c == u' ' || c == u'\t' || c == u'\n' || c == u'\v' || c == u'"';
               });
    if (plain)
        return argument;
    QString out(1, u'"');
    qsizetype backslashes = 0;
    for (const QChar c : argument) {
        if (c == u'\\') {
            ++backslashes;
            continue;
        }
        if (c == u'"')
            out += QString(backslashes * 2 + 1, u'\\') + u'"';
        else
            out += QString(backslashes, u'\\') + c;
        backslashes = 0;
    }
    out += QString(backslashes * 2, u'\\') + u'"';
    return out;
}

// ---- What the threads share with the GUI thread ------------------------------------------------

/// One run's pipes and buffers. The reader and writer threads hold it too, so a thread that ends
/// late never touches freed memory; the last owner closes the pipes.
struct Channel {
    std::mutex mutex;
    /// The reader waits here while the buffer is full; the writer while there is nothing to write.
    std::condition_variable outputTaken;
    std::condition_variable inputQueued;
    QByteArray output;
    bool outputEnded = false;
    bool deliveryPosted = false;
    /// The console is closing: the reader reads on without waiting for room (conhost may block
    /// until its last frame is read).
    bool draining = false;
    QByteArray input;
    bool stopWriting = false;
    bool inputBroken = false;
    /// Where the reader's queued calls go; null once nothing may be posted any more.
    QObject *receiver = nullptr;
    std::function<void()> deliver;
    HANDLE outputRead = nullptr;
    HANDLE inputWrite = nullptr;

    ~Channel()
    {
        closeHandle(outputRead);
        closeHandle(inputWrite);
    }
    /// A queued call to the GUI thread unless one is pending (the mutex held).
    void post()
    {
        if (deliveryPosted || !receiver || !deliver)
            return;
        deliveryPosted = true;
        QMetaObject::invokeMethod(receiver, deliver, Qt::QueuedConnection);
    }
};

DWORD WINAPI readerMain(void *argument)
{
    const std::shared_ptr<Channel> c = *static_cast<std::shared_ptr<Channel> *>(argument);
    delete static_cast<std::shared_ptr<Channel> *>(argument);
    std::vector<char> buffer(ReadChunk);
    for (;;) {
        {
            std::unique_lock lock(c->mutex);
            c->outputTaken.wait(lock, [&c] { return c->draining || c->output.size() < MaxBufferedOutput; });
        }
        DWORD n = 0;
        const BOOL ok = ::ReadFile(c->outputRead, buffer.data(), ReadChunk, &n, nullptr);
        const std::lock_guard lock(c->mutex);
        if (!ok || n == 0) {
            // ERROR_BROKEN_PIPE: conhost is gone; ERROR_OPERATION_ABORTED: CancelSynchronousIo().
            c->outputEnded = true;
            c->post();
            return 0;
        }
        c->output.append(buffer.data(), qsizetype(n));
        c->post();
    }
}

DWORD WINAPI writerMain(void *argument)
{
    const std::shared_ptr<Channel> c = *static_cast<std::shared_ptr<Channel> *>(argument);
    delete static_cast<std::shared_ptr<Channel> *>(argument);
    for (;;) {
        QByteArray chunk;
        {
            std::unique_lock lock(c->mutex);
            c->inputQueued.wait(lock, [&c] { return c->stopWriting || !c->input.isEmpty(); });
            if (c->stopWriting)
                return 0;
            chunk.swap(c->input);
        }
        qsizetype done = 0;
        while (done < chunk.size()) {
            DWORD written = 0;
            const DWORD size = DWORD(std::min<qsizetype>(chunk.size() - done, ReadChunk));
            if (!::WriteFile(c->inputWrite, chunk.constData() + done, size, &written, nullptr) || written == 0) {
                const std::lock_guard lock(c->mutex);
                c->inputBroken = true;
                c->input.clear();
                return 0;
            }
            done += qsizetype(written);
        }
    }
}

DWORD WINAPI closerMain(void *console)
{
    conPty().close(static_cast<PseudoConsole>(console));
    return 0;
}

HANDLE startThread(LPTHREAD_START_ROUTINE main, void *argument)
{
    return ::CreateThread(nullptr, 0, main, argument, 0, nullptr);
}

} // namespace

// ---- The Pty -----------------------------------------------------------------------------------

struct Pty::Private {
    Pty *q = nullptr;
    int columns = 80;
    int rows = 24;
    PseudoConsole console = nullptr;
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    DWORD pid = 0;
    HANDLE reader = nullptr;
    HANDLE writer = nullptr;
    std::shared_ptr<Channel> channel;
    QWinEventNotifier *exitNotifier = nullptr;
    QTimer *killTimer = nullptr;
    QTimer *settleTimer = nullptr;
    bool processEnded = false;
    bool outputEnded = false;
    /// terminate() or the destructor asked for the end: the job ends what is left.
    bool terminating = false;
    DWORD exitCode = 0;

    void deliver(const std::shared_ptr<Channel> &from);
    void onProcessEnded();
    void maybeFinish();
    void finish();
    void closeConsole();
    void endTree();
    void stopThreads(DWORD readerMs);
    void cleanup();
};

void Pty::Private::closeConsole()
{
    if (!console)
        return;
    if (channel) {
        const std::lock_guard lock(channel->mutex);
        channel->draining = true;
        channel->outputTaken.notify_all();
    }
    // ClosePseudoConsole waits for conhost (which waits for its last frame to be read and, on older
    // Windows, for its programs to go): never on the GUI thread. The thread uses nothing of ours,
    // so it is let go — it ends with conhost.
    PseudoConsole closing = std::exchange(console, nullptr);
    if (HANDLE closer = startThread(closerMain, closing))
        ::CloseHandle(closer);
    else
        conPty().close(closing); // (no thread to be had: the reader drains, so it returns)
}

void Pty::Private::endTree()
{
    if (!process)
        return;
    if (!job || !::TerminateJobObject(job, KilledExitCode))
        ::TerminateProcess(process, KilledExitCode);
}

void Pty::Private::deliver(const std::shared_ptr<Channel> &from)
{
    if (from != channel)
        return; // (a call queued by an earlier run's reader)
    QByteArray data;
    bool ended = false;
    {
        const std::lock_guard lock(from->mutex);
        if (from->output.size() > MaxDelivery) {
            data = from->output.left(MaxDelivery);
            from->output.remove(0, MaxDelivery);
            from->deliveryPosted = false;
            from->post(); // the rest after other events
        } else {
            data.swap(from->output);
            from->deliveryPosted = false;
            ended = from->outputEnded;
        }
        from->outputTaken.notify_all();
    }
    QPointer<Pty> guard(q);
    if (!data.isEmpty())
        Q_EMIT q->dataReceived(data);
    if (!guard || from != channel)
        return;
    if (ended) {
        outputEnded = true;
        maybeFinish();
    }
}

void Pty::Private::onProcessEnded()
{
    if (!process || processEnded)
        return;
    processEnded = true;
    if (exitNotifier)
        exitNotifier->setEnabled(false);
    DWORD code = 0;
    exitCode = ::GetExitCodeProcess(process, &code) ? code : DWORD(-1);
    // Programs it left on the console get CTRL_CLOSE_EVENT; conhost goes once they have, and its
    // output pipe breaks after the last frame.
    closeConsole();
    if (terminating)
        endTree();
    settleTimer->start(SettleMs);
    maybeFinish();
}

void Pty::Private::maybeFinish()
{
    if (processEnded && outputEnded)
        finish();
}

void Pty::Private::stopThreads(DWORD readerMs)
{
    if (channel) {
        const std::lock_guard lock(channel->mutex);
        channel->receiver = nullptr;
        channel->stopWriting = true;
        channel->draining = true;
        channel->inputQueued.notify_all();
        channel->outputTaken.notify_all();
    }
    if (writer) {
        ::CancelSynchronousIo(writer); // (a WriteFile conhost does not take)
        joinThread(writer, 1000);
    }
    if (reader && ::WaitForSingleObject(reader, readerMs) != WAIT_OBJECT_0) {
        // No end of the output: the read cancelled and our end of the pipe closed, so conhost's
        // writes fail instead of waiting for a reader (and ClosePseudoConsole can return).
        ::CancelSynchronousIo(reader);
        joinThread(reader, 1000);
        if (channel) {
            const std::lock_guard lock(channel->mutex);
            closeHandle(channel->outputRead);
        }
    }
    closeHandle(reader);
}

void Pty::Private::cleanup()
{
    if (exitNotifier) {
        // Deleted later: cleanup() may run inside its own activated() signal.
        exitNotifier->setEnabled(false);
        exitNotifier->deleteLater();
        exitNotifier = nullptr;
    }
    if (killTimer)
        killTimer->stop();
    if (settleTimer)
        settleTimer->stop();
    closeConsole();
    stopThreads(0);
    channel.reset();
    if (job && !terminating) {
        // The program ended by itself: what it started without a console (an editor, a browser)
        // stays, as a program started from a terminal on Linux does.
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof info);
    }
    closeHandle(job);
    closeHandle(process);
    pid = 0;
    processEnded = false;
    outputEnded = false;
    terminating = false;
}

void Pty::Private::finish()
{
    QByteArray rest;
    if (channel) {
        const std::lock_guard lock(channel->mutex);
        rest.swap(channel->output);
    }
    QPointer<Pty> guard(q);
    if (!rest.isEmpty())
        Q_EMIT q->dataReceived(rest);
    if (!guard)
        return;
    const DWORD code = exitCode;
    cleanup();
    // (Like QProcess: an NTSTATUS — 0xC0000005, 0xC000013A … — is a crash.)
    Q_EMIT q->finished(int(code), int(code) < 0);
}

Pty::Pty(QObject *parent) : QObject(parent), d(std::make_unique<Private>())
{
    d->q = this;
    d->killTimer = new QTimer(this);
    d->killTimer->setSingleShot(true);
    connect(d->killTimer, &QTimer::timeout, this, [this] { d->endTree(); });
    d->settleTimer = new QTimer(this);
    d->settleTimer->setSingleShot(true);
    connect(d->settleTimer, &QTimer::timeout, this, [this] {
        // The console did not close in time (a program that ignores its close): the rest of the
        // output is given up.
        if (d->processEnded)
            d->finish();
    });
}

Pty::~Pty()
{
    if (d->process) {
        if (d->exitNotifier)
            d->exitNotifier->setEnabled(false);
        d->terminating = true;
        d->closeConsole();
        if (::WaitForSingleObject(d->process, 200) != WAIT_OBJECT_0)
            d->endTree();
        ::WaitForSingleObject(d->process, 2000);
    }
    d->cleanup();
}

bool Pty::available()
{
    return conPty().ok();
}

QStringList Pty::defaultEnvironment()
{
    QStringList out;
    if (wchar_t *strings = ::GetEnvironmentStringsW()) {
        for (const wchar_t *entry = strings; *entry; entry += wcslen(entry) + 1)
            out.append(QString::fromWCharArray(entry));
        ::FreeEnvironmentStringsW(strings);
    }
    return out;
}

QString Pty::userShell()
{
    const QString shell = qEnvironmentVariable("SHELL");
    if (!shell.isEmpty()) {
        const QFileInfo info(shell);
        // (Git Bash's own SHELL is "/usr/bin/bash": not a path of Windows.)
        if (info.isAbsolute() && shell.size() > 2 && shell.at(1) == u':' && info.isFile()
            && info.suffix().compare(u"exe"_s, Qt::CaseInsensitive) == 0)
            return QDir::toNativeSeparators(info.absoluteFilePath());
    }
    const QString path = qEnvironmentVariable("PATH");
    const QString pwsh = findProgram(u"pwsh.exe"_s, path);
    if (!pwsh.isEmpty())
        return QDir::toNativeSeparators(pwsh);
    QString root = qEnvironmentVariable("SystemRoot");
    if (root.isEmpty())
        root = u"C:\\Windows"_s;
    const QString windowsPowerShell = QDir(root).filePath(u"System32/WindowsPowerShell/v1.0/powershell.exe"_s);
    if (QFileInfo(windowsPowerShell).isFile())
        return QDir::toNativeSeparators(windowsPowerShell);
    const QString onPath = findProgram(u"powershell.exe"_s, path);
    if (!onPath.isEmpty())
        return QDir::toNativeSeparators(onPath);
    const QString comspec = qEnvironmentVariable("ComSpec");
    if (!comspec.isEmpty() && QFileInfo(comspec).isFile())
        return QDir::toNativeSeparators(comspec);
    return QDir::toNativeSeparators(QDir(root).filePath(u"System32/cmd.exe"_s));
}

bool Pty::start(const QString &program, const QStringList &arguments, const QString &workingDirectory,
                const QStringList &environment, int columns, int rows, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (d->process)
        return fail(tr("A program is running already."));
    const ConPtyApi &api = conPty();
    if (!api.ok())
        return fail(tr("There is no terminal on this device: it needs Windows 10 version 1809 or later."));

    d->columns = std::clamp(columns, 1, 32767);
    d->rows = std::clamp(rows, 1, 32767);
    QString cwd = workingDirectory;
    if (cwd.isEmpty() || !QFileInfo(cwd).isDir())
        cwd = QDir::homePath();
    cwd = QDir::toNativeSeparators(QDir(cwd).absolutePath());
    const QStringList env = childEnvironment(environment);
    const QString path = findProgram(program, environmentValue(env, u"PATH"_s));
    if (path.isEmpty())
        return fail(tr("Program not found: %1").arg(program));
    if (isBatchFile(path))
        return fail(tr("Cannot start %1: a batch file runs through cmd.exe /c").arg(program));
    const QString nativePath = QDir::toNativeSeparators(path);
    QString commandLine = quoteArgument(nativePath);
    for (const QString &argument : arguments)
        commandLine += u' ' + quoteArgument(argument);
    if (!m_nativeArguments.isEmpty())
        commandLine += u' ' + m_nativeArguments;
    if (commandLine.size() > MaxCommandLine)
        return fail(tr("Cannot start %1: the command line is too long").arg(program));

    // The pipes: ConPTY reads the input one and writes the output one (it duplicates its ends,
    // ours of its side are closed once it has them).
    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!::CreatePipe(&inputRead, &inputWrite, nullptr, PipeSize)) {
        const DWORD e = ::GetLastError();
        return fail(tr("Cannot open a pseudo-terminal: %1").arg(systemMessage(e)));
    }
    if (!::CreatePipe(&outputRead, &outputWrite, nullptr, PipeSize)) {
        const DWORD e = ::GetLastError();
        closeHandle(inputRead);
        closeHandle(inputWrite);
        return fail(tr("Cannot open a pseudo-terminal: %1").arg(systemMessage(e)));
    }
    PseudoConsole console = nullptr;
    const HRESULT created =
            api.create(COORD{SHORT(d->columns), SHORT(d->rows)}, inputRead, outputWrite, 0, &console);
    closeHandle(inputRead);
    closeHandle(outputWrite);
    if (FAILED(created)) {
        closeHandle(inputWrite);
        closeHandle(outputRead);
        return fail(tr("Cannot open a pseudo-terminal: %1").arg(systemMessage(DWORD(created))));
    }
    auto channel = std::make_shared<Channel>();
    channel->outputRead = outputRead;
    channel->inputWrite = inputWrite;

    // The process, on the pseudo console, suspended until it is in its job.
    SIZE_T attributesSize = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributesSize);
    std::vector<char> attributesBuffer(attributesSize);
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributesBuffer.data());
    if (!::InitializeProcThreadAttributeList(attributes, 1, 0, &attributesSize)
        || !::UpdateProcThreadAttribute(attributes, 0, PseudoConsoleAttribute, console, sizeof console, nullptr,
                                        nullptr)) {
        const DWORD e = ::GetLastError();
        channel.reset(); // (the pipes broken first: ClosePseudoConsole then never waits for a reader)
        api.close(console);
        return fail(tr("Cannot open a pseudo-terminal: %1").arg(systemMessage(e)));
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.lpAttributeList = attributes;
    // The console's first title: the program's name ("pwsh"), not its path — as Windows Terminal
    // starts one with its profile's name.
    std::wstring title = QFileInfo(path).completeBaseName().toStdWString();
    startup.StartupInfo.lpTitle = title.data();
    std::vector<wchar_t> block = environmentBlock(env);
    std::wstring line = commandLine.toStdWString();
    line.push_back(L'\0');
    const std::wstring application = nativePath.toStdWString();
    const std::wstring folder = cwd.toStdWString();
    PROCESS_INFORMATION info{};
    const BOOL started = ::CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, FALSE,
                                          EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                                          block.data(), folder.c_str(), &startup.StartupInfo, &info);
    const DWORD startError = started ? 0 : ::GetLastError();
    ::DeleteProcThreadAttributeList(attributes);
    if (!started) {
        channel.reset(); // (the pipes broken first: ClosePseudoConsole then never waits for a reader)
        api.close(console);
        return fail(tr("Cannot start %1: %2").arg(program, systemMessage(startError)));
    }

    // The job: every process the program starts is in it (unless it asks to break away).
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        if (!::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)
            || !::AssignProcessToJobObject(job, info.hProcess))
            closeHandle(job); // (terminate() ends the process alone then)
    }
    ::ResumeThread(info.hThread);
    ::CloseHandle(info.hThread);

    d->console = console;
    d->process = info.hProcess;
    d->pid = info.dwProcessId;
    d->job = job;
    d->channel = channel;
    d->processEnded = false;
    d->outputEnded = false;
    d->terminating = false;
    d->exitCode = 0;
    channel->receiver = this;
    {
        const std::weak_ptr<Channel> weak = channel;
        Private *p = d.get();
        channel->deliver = [p, weak] {
            if (const std::shared_ptr<Channel> c = weak.lock())
                p->deliver(c);
        };
    }
    d->reader = startThread(readerMain, new std::shared_ptr<Channel>(channel));
    d->writer = startThread(writerMain, new std::shared_ptr<Channel>(channel));
    d->exitNotifier = new QWinEventNotifier(info.hProcess, this);
    connect(d->exitNotifier, &QWinEventNotifier::activated, this, [this] { d->onProcessEnded(); });
    if (!d->reader || !d->writer) {
        // (No threads to be had: nothing could read or write; ended at once.)
        d->terminating = true;
        d->endTree();
    }
    return true;
}

bool Pty::isRunning() const
{
    return d->process != nullptr;
}

qint64 Pty::processId() const
{
    return d->process ? qint64(d->pid) : 0;
}

bool Pty::write(const QByteArray &data)
{
    if (!d->process || !d->channel || d->processEnded)
        return false;
    if (data.isEmpty())
        return true;
    const std::lock_guard lock(d->channel->mutex);
    if (d->channel->stopWriting)
        return false;
    if (d->channel->inputBroken)
        return true; // (dropped, as the POSIX side drops what a closed pty does not take)
    if (d->channel->input.size() + data.size() > MaxPendingInput)
        return false;
    d->channel->input.append(data);
    d->channel->inputQueued.notify_one();
    return true;
}

bool Pty::resize(int columns, int rows)
{
    d->columns = std::clamp(columns, 1, 32767);
    d->rows = std::clamp(rows, 1, 32767);
    if (!d->console)
        return false;
    return SUCCEEDED(conPty().resize(d->console, COORD{SHORT(d->columns), SHORT(d->rows)}));
}

void Pty::terminate(int graceMs)
{
    if (!d->process)
        return;
    d->terminating = true;
    if (d->processEnded) {
        d->endTree(); // (what the program left: finished() is on its way)
        return;
    }
    d->closeConsole();
    d->killTimer->start(std::max(0, graceMs));
}

} // namespace brulion::platform
