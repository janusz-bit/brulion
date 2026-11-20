// crashreport — what a crash of the app prints before the process dies (Linux with glibc: the
// desktop, the AppImage, the Nix package, the CI's test runs; nothing elsewhere and in sanitizer
// builds, whose own reports are better). On SIGSEGV, SIGBUS, SIGFPE, SIGILL or SIGABRT it writes
// to stderr the signal, the faulting address, the crashing thread's name and id, and that
// thread's stack (glibc's backtrace: exported symbols by name, the rest as file(+offset), which
// `addr2line -f -C -e <file> <offset>` resolves against the same build with its symbols — the Nix
// package and the AppImage ship without symbol tables, so against that build not stripped:
// `dontStrip = true`, `cmake --install` without `--strip`; stripping leaves the addresses), then
// dies of the same signal as before — the exit status and the core dump stay those of the crash.
#pragma once

namespace brulion::platform {

/// Installs the handlers (once; later calls do nothing). Call at the start of main().
void installCrashReport();

/// Whether this build has the report (Linux with glibc, no sanitizer).
bool crashReportAvailable();

} // namespace brulion::platform
