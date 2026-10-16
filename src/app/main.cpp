#include "platform.h"
#include "ui/devtools.h" // design system dev tools: --gallery, --screenshot, --size … (devtools.h)

#include "appcontroller.h" // the shell (src/app/shell): app state, vault, navigation
#include "appsettings.h"

#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTranslator>

#include <memory>

#ifdef BRULION_DESKTOP_INTEGRATION
// Linux desktop: system tray (Qt Widgets, hence QApplication) and one instance per session.
#include "desktop/desktoptray.h"
#include "ioselftest.h"
#include "notifyselftest.h"
#include "shelljourney.h"
#include "shellselftest.h"
#include "singleinstance.h"
#include <QApplication>
#include <QThread>
#endif

#ifdef Q_OS_ANDROID
#include "android/androidbridge.h"
#endif

#ifdef Q_OS_WASM
#include "wasm/wasmselftest.h"
#include "wasm/wasmshell.h"
#include "wasm/wasmstorage.h"
#endif

using namespace Qt::StringLiterals;
using brulion::shell::AppController;

namespace {

QString argValue(const QStringList &args, const QString &option)
{
    const qsizetype i = args.indexOf(option);
    if (i >= 0 && i + 1 < args.size())
        return args.at(i + 1);
    for (const QString &arg : args) {
        if (arg.startsWith(option + u'='))
            return arg.mid(option.size() + 1);
    }
    return {};
}

} // namespace

int main(int argc, char *argv[])
{
    brulion::ui::DevTools::applyEarlyArguments(argc, argv); // --scale (before the app exists)
#ifdef BRULION_DESKTOP_INTEGRATION
    QApplication app(argc, argv);
#else
    QGuiApplication app(argc, argv);
#endif
    QGuiApplication::setApplicationName(QStringLiteral("Brulion"));
    QGuiApplication::setOrganizationName(QStringLiteral("Brulion"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("januszbit.github.io"));
    QGuiApplication::setApplicationVersion(QStringLiteral(BRULION_VERSION));
#ifdef BRULION_DESKTOP_INTEGRATION
    // Wayland app id / desktop entry: io.github.januszbit.brulion.desktop (packaging/).
    QGuiApplication::setDesktopFileName(u"io.github.januszbit.brulion"_s);
#endif
#ifdef Q_OS_ANDROID
    // Right after the application object: the bridge picks up the notification tap that
    // cold-started the app and delivers it once the UI is connected (docs/ANDROID.md).
    auto *androidBridge = brulion::platform::AndroidBridge::instance();
#endif

    const QStringList args = QCoreApplication::arguments();
    // Dev/test options from the command line (component gallery, screenshots, forced theme
    // and language): src/app/ui/devtools.h. Without options it changes nothing.
    brulion::ui::DevTools devTools(args);
    // The real app (Main.qml), not a dev tool window (gallery, editor playground).
    const bool mainUi = devTools.rootComponent() == u"Main"_s;

#ifdef BRULION_DESKTOP_INTEGRATION
    // A second launch brings the running Brulion to the front and ends.
    std::unique_ptr<brulion::shell::SingleInstance> singleInstance;
    if (mainUi && !devTools.screenshotMode() && !args.contains(u"--no-single-instance"_s)) {
        singleInstance = std::make_unique<brulion::shell::SingleInstance>(
            brulion::shell::SingleInstance::keyFor(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));
        const QByteArray message = args.contains(u"--capture"_s) ? "capture"
                                   : args.contains(u"--today"_s)  ? "today"
                                                                  : "activate";
        if (singleInstance->sendToPrimary(message))
            return 0;
        if (!singleInstance->listen()) {
            // Another launch started at the same moment and is the primary: hand it the message
            // once it listens. Should it not answer, run anyway rather than not at all.
            for (int attempt = 0; attempt < 20; ++attempt) {
                QThread::msleep(100);
                if (singleInstance->sendToPrimary(message, 200))
                    return 0;
            }
            singleInstance.reset();
        }
    }
#endif

    // Polish is the default UI language (English when the system or --locale en asks for it,
    // or the language chosen in the settings). English loads brulion_en.qm too: it holds the
    // plural forms of the English source texts.
    QTranslator translator;
    if (translator.load(QStringLiteral(":/i18n/brulion_") + devTools.language() + QStringLiteral(".qm")))
        QCoreApplication::installTranslator(&translator);
    devTools.setTranslator(&translator);

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Declared before the engine: QML singletons point into it, so it must outlive the engine.
    std::unique_ptr<AppController> controller;
#ifdef BRULION_DESKTOP_INTEGRATION
    std::unique_ptr<brulion::shell::DesktopTray> tray;
#endif
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    devTools.attach(&engine); // --size, --screenshot

    // The shell's controller: settings, the vault, navigation (src/app/shell/appcontroller.h).
    const auto createController = [&](const QString &settingsFile) {
        AppController::Options options;
        options.settingsFile = settingsFile;
        options.arguments = args;
        controller = std::make_unique<AppController>(options);
        // --now "2026-10-14 10:00": the same fixed clock as DatesHelper (reproducible screenshots).
        const QDateTime fixedNow = QDateTime::fromString(argValue(args, u"--now"_s), u"yyyy-MM-dd HH:mm"_s);
        if (fixedNow.isValid())
            controller->setClock([fixedNow] { return fixedNow; });
        // The saved UI language, unless --locale overrides it for this run.
        if (argValue(args, u"--locale"_s).isEmpty())
            devTools.setLanguage(controller->settings()->language());
        QObject::connect(controller.get(), &AppController::languageChanged, &devTools,
                         &brulion::ui::DevTools::setLanguage);
        QObject::connect(controller.get(), &AppController::quitRequested, &app, &QCoreApplication::quit,
                         Qt::QueuedConnection);
#ifdef BRULION_DESKTOP_INTEGRATION
        if (mainUi && !devTools.screenshotMode()) {
            tray = std::make_unique<brulion::shell::DesktopTray>(controller.get());
            controller->setTrayAvailable(tray->isAvailable());
            QObject::connect(tray.get(), &brulion::shell::DesktopTray::availableChanged, controller.get(),
                             &AppController::setTrayAvailable);
        }
        if (singleInstance) {
            QObject::connect(singleInstance.get(), &brulion::shell::SingleInstance::messageReceived,
                             controller.get(), [ctl = controller.get()](const QByteArray &message) {
                                 ctl->showWindow();
                                 // Both wait for the notebook when it is still opening.
                                 if (message == "capture")
                                     ctl->quickCapture();
                                 else if (message == "today")
                                     ctl->openToday();
                             });
        }
#endif
#ifdef Q_OS_ANDROID
        QObject::connect(androidBridge, &brulion::platform::AndroidBridge::activated, controller.get(),
                         &AppController::openNotification);
#endif
    };
    const auto startUi = [&] {
        engine.loadFromModule("Brulion", devTools.rootComponent()); // "Main", "Gallery", …
        if (mainUi)
            controller->start(); // open the last vault or show onboarding
#ifdef BRULION_DESKTOP_INTEGRATION
        // Key routing and focus of the real shell, headless (tests/app/shell: tst_shell_qml).
        if (mainUi && args.contains(u"--selftest-shell"_s))
            brulion::shell::startShellSelfTest(&engine, controller.get());
        // The ways to the import screen and an import (tests/app/io: tst_appio_import_qml).
        if (mainUi && args.contains(u"--selftest-import"_s))
            brulion::ioviews::startImportSelfTest(&engine, controller.get());
        // Settings → Reminders: the sounds of reminders and alarms (tests/app/notify).
        if (mainUi && args.contains(u"--selftest-sounds"_s))
            brulion::notify::startSoundsSelfTest(&engine, controller.get());
        // A first-time user's walk through the app with a screenshot per step (shelljourney.h).
        if (const QString journey = argValue(args, u"--journey"_s); mainUi && !journey.isEmpty())
            brulion::shell::startShellJourney(&engine, controller.get(), journey);
#endif
    };

#ifdef Q_OS_WASM
    // Browser: the file system lives in memory. Load the vault and app data from IndexedDB
    // first; changes are written back automatically (docs/WEB.md). Settings live in the app
    // data folder there (QSettings' browser backend is asynchronous).
    namespace wasm = brulion::platform;
    wasm::WasmStorage storage;
    QObject::connect(&storage, &wasm::WasmStorage::error, &app, [&controller](const QString &message) {
        if (controller)
            controller->toast(QCoreApplication::translate("main", "Saving to the browser's storage failed: %1").arg(message),
                              u"error"_s);
    });
    QObject::connect(
        &storage, &wasm::WasmStorage::ready, &engine,
        [&storage, &controller, &createController, &startUi] {
            const QStringList args = QCoreApplication::arguments();
            if (args.contains(QStringLiteral("--selftest-storage")))
                wasm::runStorageSelfTest(&storage);
            if (args.contains(QStringLiteral("--selftest-notify")))
                wasm::runNotifierSelfTest();
            wasm::setShellStage(wasm::ShellStage::StartingUi);
            createController(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + u"/settings.ini"_s);
            startUi();
            wasm::hideShellLoadingScreen();
            // Private mode or blocked site data: nothing reaches IndexedDB. Say so before the
            // user writes anything — the notes would be gone with the tab.
            if (!storage.isBackedByIndexedDb())
                controller->toast(QCoreApplication::translate(
                                      "main", "This browser does not let Brulion store data, so notes last only until "
                                              "this tab is closed. Allow site data (or leave private mode), or export "
                                              "your notes before closing."),
                                  u"error"_s);
        },
        Qt::SingleShotConnection);
    wasm::setShellStage(wasm::ShellStage::LoadingNotes);
    storage.start();
#else
    createController(QString());
    startUi();
#endif
    const int code = QGuiApplication::exec();
    if (controller)
        controller->settings()->sync();
    return code;
}
