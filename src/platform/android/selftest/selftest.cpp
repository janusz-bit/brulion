// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later
//
// Self-test of the Android bridge (src/platform/android) on a device or emulator, driven by
// scripts/android-emulator-test.sh — not part of the app. Writes "SELFTEST ..." lines to
// logcat: the permission state, two rounds of scheduleReminders() (a reminder in 45 s, an alarm
// in 75 s ringing 1 minute, one in the past that must be skipped), two files saved to Downloads
// (saveToDownloads: MediaStore) and every activated() tap.

#include "android/androidbridge.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;
using brulion::platform::AndroidBridge;

namespace {

Q_STATIC_LOGGING_CATEGORY(lcSelfTest, "brulion.selftest")

QJsonObject reminder(int id, const QString &title, qint64 at, bool alarm, const QString &path, int line)
{
    QJsonObject item{{u"id"_s, id},       {u"title"_s, title}, {u"body"_s, u"Brulion self-test"_s},
                     {u"at"_s, at},       {u"alarm"_s, alarm}, {u"path"_s, path},
                     {u"line"_s, line}};
    if (alarm)
        item.insert(u"ringMinutes"_s, 1);
    return item;
}

void logPermissions(const char *when, const AndroidBridge *bridge)
{
    qCWarning(lcSelfTest, "SELFTEST permissions(%s) allFiles=%d notifications=%d exact=%d fullScreen=%d",
              when, bridge->hasAllFilesAccess(), bridge->notificationPermission(),
              bridge->canScheduleExact(), bridge->canUseFullScreenIntent());
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    AndroidBridge *bridge = AndroidBridge::instance();

    QObject::connect(bridge, &AndroidBridge::activated, &app, [](const QString &path, int line) {
        qCWarning(lcSelfTest, "SELFTEST activated path=%s line=%d", qUtf8Printable(path), line);
    });
    QObject::connect(bridge, &AndroidBridge::permissionsChanged, &app,
                     [bridge] { logPermissions("changed", bridge); });
    logPermissions("start", bridge);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QJsonArray items{
        reminder(101, u"Przypomnienie — zażółć gęślą jaźń"_s, now + 45'000, false,
                 u"dziennik/2026-10-14.md"_s, 3),
        reminder(102, u"Alarm — pobudka"_s, now + 75'000, true, u"strony/Łódź.md"_s, 7),
        reminder(103, u"Przeszłe"_s, now - 60'000, false, u"stare.md"_s, 1),
    };
    // Twice: replacing the set with the same items must be idempotent.
    for (int round = 1; round <= 2; ++round) {
        const auto result = bridge->scheduleReminders(items);
        qCWarning(lcSelfTest, "SELFTEST schedule round=%d ok=%d scheduled=%d exact=%d error=%s", round,
                  result.ok, result.scheduled, result.exact, qUtf8Printable(result.error));
    }
    // "Save to Downloads" of the export, without any storage permission (MediaStore, Android
    // 10+): twice, the second one gets another name from the system.
    for (int round = 1; round <= 2; ++round) {
        const auto saved = bridge->saveToDownloads(u"Brulion — zażółć.txt"_s, u"text/plain"_s,
                                                   "Zażółć gęślą jaźń\n"_ba.repeated(1000));
        qCWarning(lcSelfTest, "SELFTEST download round=%d ok=%d unsupported=%d name=%s error=%s", round, saved.ok,
                  saved.unsupported, qUtf8Printable(saved.name), qUtf8Printable(saved.error));
    }
    return QGuiApplication::exec();
}
