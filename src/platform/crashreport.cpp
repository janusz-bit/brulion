#include "crashreport.h"

#include <QtGlobal>

// The sanitizers bring their own handlers and reports (more precise than these): keep theirs.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define BRULION_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#define BRULION_SANITIZED 1
#endif
#endif

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && !defined(BRULION_SANITIZED) && __has_include(<execinfo.h>)
#define BRULION_CRASH_REPORT 1
#include <execinfo.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#endif

namespace brulion::platform {

#ifdef BRULION_CRASH_REPORT

namespace {

// Only async-signal-safe calls in the handler (write, prctl, syscall, sigaction, raise), apart
// from backtrace(), whose library is loaded at installation, so it allocates nothing here.

void put(const char *text)
{
    const size_t n = std::strlen(text);
    size_t done = 0;
    while (done < n) {
        const ssize_t w = ::write(STDERR_FILENO, text + done, n - done);
        if (w <= 0)
            return;
        done += size_t(w);
    }
}

void putNumber(std::uintptr_t value, unsigned base)
{
    char buffer[2 + 2 * sizeof(value) * 4 + 1];
    char *end = buffer + sizeof(buffer) - 1;
    *end = '\0';
    char *p = end;
    do {
        const unsigned digit = unsigned(value % base);
        *--p = char(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= base;
    } while (value != 0);
    if (base == 16) {
        *--p = 'x';
        *--p = '0';
    }
    put(p);
}

const char *signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGABRT: return "SIGABRT";
    default: return "signal";
    }
}

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};

void onFatalSignal(int sig, siginfo_t *info, void *)
{
    // Back to the default first: a crash inside this report ends the process as the first would.
    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    for (int s : kSignals)
        sigaction(s, &dfl, nullptr);

    char thread[17] = {};
    prctl(PR_GET_NAME, thread, 0, 0, 0);
    put("\nbrulion: fatal ");
    put(signalName(sig));
    put(" (");
    putNumber(std::uintptr_t(sig), 10);
    put(", code ");
    putNumber(std::uintptr_t(info ? info->si_code : 0), 10);
    if (info && info->si_code > 0 && sig != SIGABRT) { // a fault (a sent signal has no address)
        put(", address ");
        putNumber(reinterpret_cast<std::uintptr_t>(info->si_addr), 16);
    }
    put(") in thread '");
    put(thread);
    put("' (tid ");
    putNumber(std::uintptr_t(syscall(SYS_gettid)), 10);
    put(", pid ");
    putNumber(std::uintptr_t(getpid()), 10);
    put("); its stack:\n");
    void *frames[64];
    const int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    put("brulion: end of the crash report\n");

    // The same death as without this handler: a fault happens again when the instruction is
    // retried after the return, a sent or raised signal (abort()) is raised again here.
    if (info && info->si_code <= 0) {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, sig);
        pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
        raise(sig);
    }
}

} // namespace

void installCrashReport()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    // backtrace() loads libgcc's unwinder on its first call, which allocates: not in the handler.
    void *warm[1];
    backtrace(warm, 1);
    // Its own stack, so a stack overflow is reported too (the thread that installs it: the GUI
    // thread, where the deep recursions are).
    static char altStack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = &onFatalSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int s : kSignals)
        sigaction(s, &sa, nullptr);
}

bool crashReportAvailable()
{
    return true;
}

#else

void installCrashReport() { }

bool crashReportAvailable()
{
    return false;
}

#endif

} // namespace brulion::platform
