// Android only: main() of the automation requests' process (docs/ANDROID.md "Automation";
// AutomationService.kt starts Brulion's library there with AutomationArgument). No window, no
// QML, no settings of the app: the jobs come from the Kotlin side, which binds the service for each
// request and ends the process by unbinding.
#include "automationrun.h"
#include "backgroundentry.h"

#include "android/androidbridge.h" // src/platform
#include "platform.h"

#include "log/logger.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>
#include <QtCore/private/qandroidextras_p.h> // QAndroidService

using namespace Qt::StringLiterals;

namespace brulion::background {

namespace {

Q_STATIC_LOGGING_CATEGORY(lcAutomationEntry, "brulion.automation")

/// How often the queue is looked at (a request waits at most this long once the process runs).
constexpr int kPollMs = 50;

} // namespace

int runAutomationService(int argc, char *argv[])
{
    // QAndroidService, as for the background check (backgroundentry_android.cpp).
    QAndroidService app(argc, argv);
    // The app's names: the same data folder (the automation keys) and log folder as the app.
    QCoreApplication::setApplicationName(u"Brulion"_s);
    QCoreApplication::setOrganizationName(u"Brulion"_s);
    QCoreApplication::setOrganizationDomain(u"januszbit.github.io"_s);
    QCoreApplication::setApplicationVersion(QStringLiteral(BRULION_VERSION));

    // A log of its own next to the app's: logs/automation/.
    logging::LoggerOptions logOptions;
    const QString logs = platform::logDirectory();
    logOptions.directory = logs.isEmpty() ? QString() : logs + u"/automation"_s;
    logOptions.maxFileBytes = 256 * 1024;
    logOptions.sessionInfo =
            u"Brulion %1 automation (%2)"_s.arg(QStringLiteral(BRULION_VERSION), platform::deviceDescription());
    logging::Logger *logger = logging::Logger::install(logOptions);

    AutomationRunner runner(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QTimer poll;
    poll.setInterval(kPollMs);
    QObject::connect(&poll, &QTimer::timeout, &app, [&runner, logger] {
        const QJsonObject job = platform::AndroidBridge::takeAutomationJob();
        if (job.isEmpty())
            return;
        platform::AndroidBridge::automationFinished(runner.run(job));
        logger->flush();
    });
    poll.start();
    qCInfo(lcAutomationEntry) << "Waiting for automation requests";
    const int code = QCoreApplication::exec();
    logger->endSession();
    return code;
}

} // namespace brulion::background
