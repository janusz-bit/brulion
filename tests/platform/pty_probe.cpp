// pty_probe — a small console program for tst_pty's Windows cases (ConPTY): what a program on the
// pseudo console sees and does, without cmd.exe's or PowerShell's localized output. Console I/O
// through the wide-character API (ReadConsoleW / WriteConsoleW), so Polish letters survive any
// code page.
//
//   pty_probe print <text>…        the texts, a line each
//   pty_probe exit <n>             exits with n
//   pty_probe args <a>…            each argument as [a] on a line of its own (the command line's quoting)
//   pty_probe size                 "size <rows> <columns>", then again after a line of input
//   pty_probe echo                 "got: <line>" for every line typed (echoed) until "quit"
//   pty_probe count                no echo; the UTF-16 units of the lines typed until "end": "count <n>"
//   pty_probe cwd                  "cwd <the current folder>"
//   pty_probe env <name>…          "<name>=<value>" or "<name> unset"
//   pty_probe handle <hex> <name>  whether the handle value is a file whose path holds <name>
//   pty_probe sleep <ms>           sleeps (INFINITE without a number)
//   pty_probe ignore-close         "ready", then sleeps; a console close (CTRL_CLOSE_EVENT) is held up
//   pty_probe spawn console|detached [exit]  starts `pty_probe sleep` (on this console, or with
//                                  none), "child <pid>", then sleeps (or exits)
//   pty_probe crash                ends with STATUS_ACCESS_VIOLATION
//   pty_probe flood <lines>        that many lines "y", then "flood done"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace {

HANDLE out()
{
    return ::GetStdHandle(STD_OUTPUT_HANDLE);
}

void write(const std::wstring &text)
{
    DWORD written = 0;
    if (!::WriteConsoleW(out(), text.data(), DWORD(text.size()), &written, nullptr)) {
        // Not a console (a test run by hand with redirected output): UTF-8.
        const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
        std::string bytes(size_t(size > 0 ? size : 0), '\0');
        if (size > 0)
            ::WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), bytes.data(), size, nullptr, nullptr);
        ::WriteFile(out(), bytes.data(), DWORD(bytes.size()), &written, nullptr);
    }
}

void line(const std::wstring &text)
{
    write(text + L"\r\n");
}

/// A line of console input without its line break; false at the end of the input.
bool readLine(std::wstring &text)
{
    text.clear();
    const HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    wchar_t buffer[512];
    for (;;) {
        DWORD n = 0;
        if (!::ReadConsoleW(in, buffer, DWORD(std::size(buffer)), &n, nullptr) || n == 0)
            return !text.empty();
        text.append(buffer, n);
        if (!text.empty() && text.back() == L'\n') {
            while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r'))
                text.pop_back();
            return true;
        }
    }
}

std::wstring size()
{
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!::GetConsoleScreenBufferInfo(out(), &info))
        return L"size unknown";
    return L"size " + std::to_wstring(info.srWindow.Bottom - info.srWindow.Top + 1) + L' '
            + std::to_wstring(info.srWindow.Right - info.srWindow.Left + 1);
}

BOOL WINAPI holdClose(DWORD event)
{
    if (event == CTRL_CLOSE_EVENT) {
        ::Sleep(INFINITE); // the system (or the test's job) ends the process
    }
    return TRUE;
}

std::wstring self()
{
    std::vector<wchar_t> path(32768);
    const DWORD n = ::GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    return std::wstring(path.data(), n);
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2)
        return 2;
    const std::wstring command = argv[1];
    if (command == L"print") {
        for (int i = 2; i < argc; ++i)
            line(argv[i]);
        return 0;
    }
    if (command == L"exit")
        return argc > 2 ? int(std::wcstol(argv[2], nullptr, 10)) : 0;
    if (command == L"args") {
        for (int i = 2; i < argc; ++i)
            line(L"[" + std::wstring(argv[i]) + L"]");
        line(L"args done");
        return 0;
    }
    if (command == L"size") {
        line(size());
        std::wstring text;
        readLine(text);
        line(size());
        return 0;
    }
    if (command == L"echo") {
        line(L"ready");
        std::wstring text;
        while (readLine(text) && text != L"quit")
            line(L"got: " + text);
        return 0;
    }
    if (command == L"count") {
        const HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        ::GetConsoleMode(in, &mode);
        ::SetConsoleMode(in, (mode | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT) & ~DWORD(ENABLE_ECHO_INPUT));
        line(L"ready");
        unsigned long long total = 0;
        std::wstring text;
        while (readLine(text) && text != L"end")
            total += text.size();
        line(L"count " + std::to_wstring(total));
        return 0;
    }
    if (command == L"cwd") {
        std::vector<wchar_t> folder(32768);
        const DWORD n = ::GetCurrentDirectoryW(DWORD(folder.size()), folder.data());
        line(L"cwd " + std::wstring(folder.data(), n));
        return 0;
    }
    if (command == L"env") {
        for (int i = 2; i < argc; ++i) {
            std::vector<wchar_t> value(32768);
            const DWORD n = ::GetEnvironmentVariableW(argv[i], value.data(), DWORD(value.size()));
            if (n == 0 && ::GetLastError() == ERROR_ENVVAR_NOT_FOUND)
                line(std::wstring(argv[i]) + L" unset");
            else
                line(std::wstring(argv[i]) + L'=' + std::wstring(value.data(), n));
        }
        return 0;
    }
    if (command == L"handle" && argc > 3) {
        const auto value = static_cast<ULONG_PTR>(std::wcstoull(argv[2], nullptr, 16));
        const auto handle = reinterpret_cast<HANDLE>(value);
        std::vector<wchar_t> path(32768);
        DWORD flags = 0;
        const bool open = ::GetHandleInformation(handle, &flags) != 0;
        const DWORD n = open ? ::GetFinalPathNameByHandleW(handle, path.data(), DWORD(path.size()), 0) : 0;
        const std::wstring name(path.data(), n < path.size() ? n : 0);
        line(name.find(argv[3]) != std::wstring::npos ? L"handle inherited" : L"handle not inherited");
        return 0;
    }
    if (command == L"sleep") {
        ::Sleep(argc > 2 ? DWORD(std::wcstoul(argv[2], nullptr, 10)) : INFINITE);
        return 0;
    }
    if (command == L"ignore-close") {
        ::SetConsoleCtrlHandler(holdClose, TRUE);
        line(L"ready");
        ::Sleep(INFINITE);
        return 0;
    }
    if (command == L"spawn" && argc > 2) {
        const bool detached = std::wstring(argv[2]) == L"detached";
        std::wstring commandLine = L"\"" + self() + L"\" sleep";
        STARTUPINFOW startup{};
        startup.cb = sizeof startup;
        PROCESS_INFORMATION info{};
        if (!::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, detached ? DETACHED_PROCESS : 0,
                              nullptr, nullptr, &startup, &info)) {
            line(L"spawn failed " + std::to_wstring(::GetLastError()));
            return 3;
        }
        ::CloseHandle(info.hThread);
        ::CloseHandle(info.hProcess);
        line(L"child " + std::to_wstring(info.dwProcessId));
        if (argc > 3 && std::wstring(argv[3]) == L"exit")
            return 0;
        ::Sleep(INFINITE);
        return 0;
    }
    if (command == L"crash") {
        ::TerminateProcess(::GetCurrentProcess(), 0xC0000005);
        return 1;
    }
    if (command == L"flood" && argc > 2) {
        const unsigned long lines = std::wcstoul(argv[2], nullptr, 10);
        std::wstring chunk;
        for (unsigned long i = 0; i < lines; ++i) {
            chunk += L"y\r\n";
            if (chunk.size() > 30000) {
                write(chunk);
                chunk.clear();
            }
        }
        write(chunk);
        line(L"flood done");
        return 0;
    }
    return 2;
}
