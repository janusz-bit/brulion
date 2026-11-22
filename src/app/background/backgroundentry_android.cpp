#include "backgroundentry.h"

#include "backgroundcheck.h"

#include "android/androidbridge.h" // src/platform
#include "appsettings.h"           // the stored Nextcloud app password (src/app/shell)
#include "devicefolder.h"           // a folder of the device the notebook syncs with (src/platform)
#include "platform.h"

#include "log/logger.h"
#include "sync/http.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>
#include <QtCore/private/qandroidextras_p.h> // QAndroidService

using namespace Qt::StringLiterals;

namespace brulion::background {

namespace {

Q_STATIC_LOGGING_CATEGORY(lcBackgroundEntry, "brulion.background")

/// The whole check, sync included, ends within this (the job gets about ten minutes from Android;
/// the Kotlin side gives up a little later).
constexpr int kCheckTimeoutMs = 4 * 60 * 1000;

} // namespace

int runBackgroundCheck(int argc, char *argv[])
{
    // QAndroidService, not a plain QCoreApplication: Qt's QtServiceBase keeps Android's main
    // thread waiting until it exists (no onBind() before), so with another application object the
    // service would never bind. Its binder is unused — BackgroundCheckService.kt answers onBind().
    QAndroidService app(argc, argv);
    // The app's names: the same data folder (secrets) and the same log folder as the app.
    QCoreApplication::setApplicationName(u"Brulion"_s);
    QCoreApplication::setOrganizationName(u"Brulion"_s);
    QCoreApplication::setOrganizationDomain(u"januszbit.github.io"_s);
    QCoreApplication::setApplicationVersion(QStringLiteral(BRULION_VERSION));

    // A log of its own next to the app's (two processes never write one file): logs/background/.
    logging::LoggerOptions logOptions;
    const QString logs = platform::logDirectory();
    logOptions.directory = logs.isEmpty() ? QString() : logs + u"/background"_s;
    logOptions.maxFileBytes = 256 * 1024;
    logOptions.sessionInfo =
            u"Brulion %1 background check (%2)"_s.arg(QStringLiteral(BRULION_VERSION), platform::deviceDescription());
    logging::Logger *logger = logging::Logger::install(logOptions);

    const std::optional<Target> target = Target::fromJson(platform::AndroidBridge::backgroundCheckTarget());
    if (!target || !target->isValid()) {
        qCInfo(lcBackgroundEntry) << "Nothing to check (no notebook, or the check is off)";
        platform::AndroidBridge::backgroundCheckFinished(
                QJsonObject{{u"ok"_s, false}, {u"error"_s, u"invalid"_s}, {u"sync"_s, u"off"_s}});
        logger->endSession();
        return 0;
    }

    sync::NetworkHttpTransport transport;
    const QString password = target->nextcloud
                                     ? shell::AppSettings::storedNextcloudPassword(
                                               QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                     : QString();
    Check check(*target, password, &transport);
    // A notebook linked to a folder of the device (a Syncthing folder picked in the system's
    // dialog): reached with the app's grant, this process being the app's too.
    check.setFolderOpener([](const QString &address) { return platform::openDeviceFolder(address); });
    // CalDAV accounts' passwords: next to the app password (WorkspaceCalendars' files).
    const QString secrets = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + u"/secrets"_s;
    check.setCalendarPasswords([secrets](const QString &account) {
        QString safe;
        for (const QChar c : account) {
            if (c.isLetterOrNumber() || c == u'-')
                safe += c;
        }
        QFile f(secrets + u"/caldav-"_s + safe);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
    });
    QObject::connect(&check, &Check::finished, &app, [&app](const QJsonObject &result) {
        platform::AndroidBridge::backgroundCheckFinished(result);
        app.quit();
    });
    QTimer::singleShot(kCheckTimeoutMs, &app, [&app, &check] {
        if (check.isFinished())
            return;
        qCWarning(lcBackgroundEntry) << "The check took too long: given up";
        platform::AndroidBridge::backgroundCheckFinished(QJsonObject{{u"ok"_s, false}, {u"error"_s, u"timeout"_s}});
        app.quit();
    });
    check.start();
    const int code = QCoreApplication::exec();
    logger->endSession();
    return code;
}

} // namespace brulion::background
