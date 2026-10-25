// Central European time (Europe/Warsaw) as a test process's local time zone: the tests' dates,
// times and daylight-saving changes are Polish ones.
//
// Linux and the rest: the TZ variable, read by the C library and by Qt. Windows: the C runtime
// reads TZ only in its own "CET-1CEST" form, with the United States' daylight-saving rules, and
// Qt's local time goes through it — "Europe/Warsaw" there would mean UTC. So on Windows TZ is left
// unset and the tests run in the system's zone, which must be Central European time
// (.github/workflows/windows.yml: tzutil /s "Central European Standard Time");
// centralEuropeanTimeProblem() says when it is not.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <ctime>

namespace brulion::test {

/// Makes local time Central European: TZ=`tz` (default "Europe/Warsaw"; a POSIX rule such as
/// "CET-1CEST,M3.5.0,M10.5.0/3" needs no time zone database) — on Windows TZ unset instead.
inline void useCentralEuropeanTime(const char *tz = "Europe/Warsaw")
{
#ifdef Q_OS_WIN
    Q_UNUSED(tz);
    qunsetenv("TZ");
    _tzset();
#else
    qputenv("TZ", tz);
    tzset();
#endif
}

/// "" when local time follows Central European rules (1 h, 2 h in summer, the changes on the last
/// Sundays of March and October at 01:00 UTC); on Windows otherwise why not. Elsewhere always "":
/// TZ decides there.
inline QString centralEuropeanTimeProblem()
{
#ifdef Q_OS_WIN
    struct Probe {
        qint64 utc;
        int offset;
    };
    // 2026-01-15 12:00, 2026-07-15 12:00, around 2026-03-29 01:00 and 2026-10-25 01:00 (UTC).
    const Probe probes[] = {{1768478400, 3600}, {1784116800, 7200}, {1774745999, 3600},
                            {1774746000, 7200}, {1792889999, 7200}, {1792890000, 3600}};
    for (const Probe &p : probes) {
        if (QDateTime::fromSecsSinceEpoch(p.utc).offsetFromUtc() != p.offset)
            return QStringLiteral("The system time zone is not Central European time: the test needs it on "
                                  "Windows (tzutil /s \"Central European Standard Time\")");
    }
#endif
    return {};
}

} // namespace brulion::test
