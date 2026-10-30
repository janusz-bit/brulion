// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later
//
// Self-test of the Android bridge (src/platform/android) on a device or emulator, driven by
// scripts/android-emulator-test.sh — not part of the app. Writes "SELFTEST ..." lines to
// logcat: the permission state, two rounds of scheduleReminders() (a reminder in 45 s, an alarm
// in 75 s ringing 1 minute, one in the past that must be skipped), two files saved to Downloads
// (saveToDownloads: MediaStore), every activated() tap and every "Zrobione" tap (reminders 101 and
// 104 are on tasks: their notifications have the button; takeDoneRequests()). 104 comes due in
// 200 s, after the script killed the process: its tap waits in the queue for the next start. A PDF
// page through Android's PdfRenderer (pdfpages.h: a 1-page PDF written here with a red square, its
// page count, its size and two pixels).

#include "android/androidbridge.h"
#include "pdfpages.h"

#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>

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
    QObject::connect(bridge, &AndroidBridge::doneRequestsQueued, &app, [bridge] {
        for (const QJsonValue &value : bridge->takeDoneRequests()) {
            const QJsonObject done = value.toObject();
            qCWarning(lcSelfTest, "SELFTEST done key=%s local=%s path=%s line=%d day=%s",
                      qUtf8Printable(done.value(u"key"_s).toString()),
                      qUtf8Printable(done.value(u"local"_s).toString()),
                      qUtf8Printable(done.value(u"path"_s).toString()), done.value(u"line"_s).toInt(),
                      qUtf8Printable(done.value(u"day"_s).toString()));
        }
    });
    QObject::connect(bridge, &AndroidBridge::permissionsChanged, &app,
                     [bridge] { logPermissions("changed", bridge); });
    logPermissions("start", bridge);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 101 and 104 are tasks' reminders ("Zrobione" in their notifications).
    const auto task = [now](int id, const QString &title, qint64 in, const QString &path, int line) {
        QJsonObject item = reminder(id, title, now + in, false, path, line);
        item.insert(u"key"_s, u"selftest-%1"_s.arg(id));
        item.insert(u"local"_s, QDateTime::fromMSecsSinceEpoch(now + in).toString(u"yyyy-MM-ddTHH:mm"_s));
        item.insert(u"task"_s, true);
        return item;
    };
    const QJsonArray items{
            task(101, u"Przypomnienie — zażółć gęślą jaźń"_s, 45'000, u"dziennik/2026-10-14.md"_s, 3),
            reminder(102, u"Alarm — pobudka"_s, now + 75'000, true, u"strony/Łódź.md"_s, 7),
            reminder(103, u"Przeszłe"_s, now - 60'000, false, u"stare.md"_s, 1),
            task(104, u"Zadanie przy zamkniętej aplikacji"_s, 200'000, u"strony/Źródła.md"_s, 9),
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
    // A PDF page as a picture (pdfpages_android.cpp → PdfPages.kt): A4 at one pixel a point, a red
    // square 100–300 pt from the top left.
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(dir);
        const QString pdf = dir + u"/Wykład ćwiczenia.pdf"_s;
        {
            QPdfWriter writer(pdf);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QPageSize::A4));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter painter(&writer);
            painter.fillRect(QRectF(100, 100, 200, 200), Qt::red);
        }
        const brulion::platform::PdfInfo info = brulion::platform::pdfInfo(pdf);
        QString error;
        const QImage page = brulion::platform::renderPdfPage(pdf, 1, 842, &error);
        qCWarning(lcSelfTest, "SELFTEST pdf backend=%s pages=%d width=%d height=%d centre=%s corner=%s error=%s",
                  qUtf8Printable(brulion::platform::pdfBackendName()), info.pages, page.width(), page.height(),
                  qUtf8Printable(page.isNull() ? QString() : page.pixelColor(200, 200).name()),
                  qUtf8Printable(page.isNull() ? QString() : page.pixelColor(5, 5).name()),
                  qUtf8Printable(error.isEmpty() ? info.error : error));
    }
    return QGuiApplication::exec();
}
