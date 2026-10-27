#include "appidentity.h" // Windows: the AppUserModelID of the toasts (src/platform)
#include "platform.h"
#include "ui/devtools.h"      // design system dev tools: --gallery, --screenshot, --size … (devtools.h)
#include "ui/fallbackfonts.h" // emoji and symbol fonts where the system has none
#include "ui/gestureinsets.h" // the system's gesture zones at the window's edges (GestureInsets)
#include "ui/keyboardinsets.h" // the on-screen keyboard's height (KeyboardInsets)
#include "ui/keyboardsources.h"

#include "appcontroller.h" // the shell (src/app/shell): app state, vault, navigation
#include "appsettings.h"
#include "localnetwork.h" // QML loads nothing from the network (src/app/ui)
#include "logcontroller.h" // the event log page's controller (src/app/diag)
#include "workspace.h"
#include "workspacesync.h"

#include "log/categories.h"
#include "log/logger.h"
#include "log/slowscope.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlFileSelector>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTranslator>

#include <cstdio>
#include <memory>

#ifdef BRULION_DESKTOP_INTEGRATION
// The desktop (Linux, Windows): system tray (Qt Widgets, hence QApplication) and one instance per
// session.
#include "appmessage.h"
#include "cli.h" // `brulion cli …`: the command line (src/cli)
#include "controlprotocol.h"
#include "desktop/desktoptray.h"
#include "ioselftest.h"
#include "notifyselftest.h"
#include "profile.h"
#include "remotecontrol.h" // --remote-control: AI agents drive the running app (brulion-cli app …)
#include "shelljourney.h"
#include "shellselftest.h"
#include "singleinstance.h"
#include <QApplication>
#include <QJsonObject>
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
#ifdef BRULION_DESKTOP_INTEGRATION
    // `brulion cli …` (also `Brulion.AppImage cli …`, the AppImage's one entry point): the command
    // line for scripts and AI agents (docs/CLI.md) — before anything of the app: no window, no
    // log file, no single-instance claim, no settings.
    if (argc > 1 && qstrcmp(argv[1], "cli") == 0)
        return brulion::cli::run(argc - 1, argv + 1);
#endif
    brulion::ui::DevTools::applyEarlyArguments(argc, argv); // --scale (before the app exists)
#ifdef BRULION_DESKTOP_INTEGRATION
    QApplication app(argc, argv);
#else
    QGuiApplication app(argc, argv);
#endif
#ifdef BRULION_DESKTOP_INTEGRATION
    // "Brulion", or a separate profile for BRULION_PROFILE (src/ipc/profile.h): the same names as
    // brulion-cli's.
    brulion::ipc::applyAppNames();
    // Windows: the toasts' name and icon, also for the portable ZIP (nothing elsewhere).
    brulion::platform::applyAppIdentity();
#else
    QGuiApplication::setApplicationName(QStringLiteral("Brulion"));
    QGuiApplication::setOrganizationName(QStringLiteral("Brulion"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("januszbit.github.io"));
#endif
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
    std::unique_ptr<brulion::ipc::SingleInstance> singleInstance;
    QString singleInstanceProblem; // why this run has none (written to the event log below)
    if (mainUi && !devTools.screenshotMode() && !args.contains(u"--no-single-instance"_s)) {
        singleInstance = std::make_unique<brulion::ipc::SingleInstance>(brulion::ipc::SingleInstance::keyFor(
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));
        // --hidden (Windows' start at sign-in, the running one already there): no window.
        const QByteArray message = args.contains(u"--capture"_s) ? "capture"
                                   : args.contains(u"--today"_s)  ? "today"
                                   : args.contains(u"--hidden"_s) ? "hidden"
                                                                  : "activate";
        if (singleInstance->sendToPrimary(message)) {
            // The running app keeps its own setting: a second launch cannot switch it on.
            if (args.contains(u"--remote-control"_s))
                std::fprintf(stderr, "Brulion is already running: --remote-control applies to a new run only. Turn "
                                     "remote control on in its settings (Settings → Application → For developers) or "
                                     "quit it first.\n");
            return 0;
        }
        if (singleInstance->listen()) {
            QObject::connect(singleInstance.get(), &brulion::ipc::SingleInstance::refused, [](qint64 uid) {
                qCWarning(brulion::logging::lcApp).noquote()
                        << u"Single instance: refused a connection of another user (uid %1)"_s.arg(uid);
            });
        } else if (!singleInstance->errorString().isEmpty()) {
            // Nowhere safe for it (no private runtime folder …): run without, never in the shared
            // temp folder where another user could block the start or read what is sent.
            singleInstanceProblem = singleInstance->errorString();
            singleInstance.reset();
        } else {
            // Another launch started at the same moment and is the primary: hand it the message
            // once it listens. Should it not answer, run anyway rather than not at all.
            for (int attempt = 0; attempt < 20; ++attempt) {
                QThread::msleep(100);
                if (singleInstance->sendToPrimary(message, 200))
                    return 0;
            }
            singleInstanceProblem = u"another Brulion holds %1.lock but does not answer"_s.arg(singleInstance->key());
            singleInstance.reset();
        }
    }
#endif

    // The event log ("Rejestr zdarzeń", src/core/log) — after the single-instance check, so a second
    // launch never writes into the running one's log: every Qt message from here on, redacted,
    // in memory and in brulion.log (desktop: ~/.local/state/Brulion/Brulion/logs; Android: the
    // app's files dir; browser: memory only). stderr / logcat keep their output.
    using brulion::logging::Logger;
    brulion::logging::LoggerOptions logOptions;
    // The developer tools (gallery, editor playground) log to memory only.
    logOptions.directory = mainUi ? brulion::platform::logDirectory() : QString();
    // --now "2026-10-14 10:00" (screenshots): the log's clock starts there too.
    if (const QDateTime fixedStart = QDateTime::fromString(argValue(args, u"--now"_s), u"yyyy-MM-dd HH:mm"_s);
        fixedStart.isValid()) {
        auto since = std::make_shared<QElapsedTimer>();
        since->start();
        logOptions.clock = [base = fixedStart.toMSecsSinceEpoch(), since] { return base + since->elapsed(); };
    }
    logOptions.sessionInfo = u"Brulion %1 (%2, Qt %3)"_s.arg(
            QStringLiteral(BRULION_VERSION), brulion::platform::deviceDescription(), QString::fromLatin1(qVersion()));
    Logger *logger = Logger::install(logOptions);
    logger->setVerbosity(brulion::logging::Verbosity::Normal);
#ifdef BRULION_DESKTOP_INTEGRATION
    if (!singleInstanceProblem.isEmpty())
        qCWarning(brulion::logging::lcApp).noquote()
                << u"No single instance: %1. A second launch starts another Brulion, and brulion-cli app open, "
                   u"today and capture do not reach this one."_s.arg(singleInstanceProblem);
#endif
    QObject::connect(&app, &QCoreApplication::aboutToQuit, logger, &Logger::endSession);
    // Going to the background: write the log now (Android may stop the process there without
    // aboutToQuit) and note how long the app was away when it comes back.
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, logger,
                     [logger, away = QElapsedTimer()](Qt::ApplicationState state) mutable {
                         const bool background = state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden;
                         if (background && !away.isValid()) {
                             away.start();
                             qCInfo(brulion::logging::lcApp) << "App in the background";
                             logger->flush();
                         } else if (state == Qt::ApplicationActive && away.isValid()) {
                             qCInfo(brulion::logging::lcApp).noquote()
                                     << u"App active again after %1"_s.arg(brulion::logging::formatMs(away.elapsed()));
                             away.invalidate();
                         }
                     });

    // Polish is the default UI language (English when the system or --locale en asks for it,
    // or the language chosen in the settings). English loads brulion_en.qm too: it holds the
    // plural forms of the English source texts.
    QTranslator translator;
    if (translator.load(QStringLiteral(":/i18n/brulion_") + devTools.language() + QStringLiteral(".qm")))
        QCoreApplication::installTranslator(&translator);
    devTools.setTranslator(&translator);

    // Emoji and symbols where the system has no font for them (ui/fallbackfonts.h), before any
    // text is laid out: Qt for WebAssembly only has DejaVu Sans and DejaVu Sans Mono (every ⏰ 🔔
    // 📅 was an empty box), some Linux systems have no emoji font, Android always has one.
#ifndef Q_OS_ANDROID
#ifdef Q_OS_WASM
    const auto fallbackPolicy = brulion::ui::FallbackFontPolicy::Always;
#else
    const auto fallbackPolicy = brulion::ui::FallbackFontPolicy::WhenMissing;
#endif
    const QList<brulion::ui::FallbackFontReport> fallbackFonts =
            brulion::ui::installFallbackFonts(fallbackPolicy, QGuiApplication::font());
    for (const brulion::ui::FallbackFontReport &font : fallbackFonts) {
        if (!font.missingAfter.isEmpty())
            qCWarning(brulion::logging::lcApp).noquote() << u"Fallback font %1 %2: %3 still drawn as boxes"_s.arg(
                    font.family, font.loaded ? u"installed"_s : u"not available"_s, font.missingAfter);
        else if (font.installed && !font.missingBefore.isEmpty())
            qCInfo(brulion::logging::lcApp).noquote()
                    << u"Fallback font %1 installed: the system fonts drew %2 as boxes"_s.arg(font.family,
                                                                                              font.missingBefore);
        else
            qCDebug(brulion::logging::lcApp).noquote() << u"Fallback font %1 %2"_s.arg(
                    font.family, font.installed ? u"installed"_s
                                 : font.loaded  ? u"available to the system's fallbacks"_s
                                                : u"not needed"_s);
    }
#endif

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Declared before the engine: QML singletons point into them, so they must outlive the engine.
    brulion::diag::LogController logs;
    // How much of the window the on-screen keyboard covers (QML singleton KeyboardInsets), fed by
    // the platform (keyboardsources.h); --keyboard <px> simulates one.
    brulion::ui::KeyboardInsets keyboardInsets;
    if (devTools.keyboard() >= 0)
        keyboardInsets.setSimulated(devTools.keyboard());
    // The system's gesture zones at the window's edges (QML singleton GestureInsets: Android's
    // gesture navigation), fed by the platform (keyboardsources.h).
    brulion::ui::GestureInsets gestureInsets;
    std::unique_ptr<AppController> controller;
#ifdef BRULION_DESKTOP_INTEGRATION
    std::unique_ptr<brulion::shell::DesktopTray> tray;
#endif
#if QT_CONFIG(qml_network)
    // Pictures come from the vault and icons from the resources: the engine refuses remote URLs,
    // so no rich text (a note's rendered Markdown) can make it fetch one (localnetwork.h).
    brulion::ui::LocalOnlyNetworkFactory networkFactory;
#endif
    QQmlApplicationEngine engine;
#if QT_CONFIG(qml_network)
    engine.setNetworkAccessManagerFactory(&networkFactory);
#endif
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    devTools.attach(&engine); // --size, --screenshot
    brulion::ui::connectKeyboardSources(&keyboardInsets, &engine);
    brulion::ui::connectGestureSources(&gestureInsets, &engine);
#ifdef BRULION_DESKTOP_INTEGRATION
    // After the engine: it drives the engine's window and goes before it.
    std::unique_ptr<brulion::shell::RemoteControl> remoteControl;
#endif

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
        // The detailed event log (Settings → Application, the event log page) is a device setting.
        brulion::shell::AppSettings *settings = controller->settings();
        logs.setDetailed(settings->value(u"diagnostics/logVerbosity"_s).toString() == u"detailed");
        logs.setPersistVerbosity(
                [settings](const QString &verbosity) { settings->setValue(u"diagnostics/logVerbosity"_s, verbosity); });
        logs.setRecentSyncs([ctl = controller.get()] { return ctl->workspace()->sync()->historyLines(5); });
        if (fixedNow.isValid())
            logs.setClock([fixedNow] { return fixedNow; });
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
            // A second launch or brulion-cli (`app open …`): src/ipc/appmessage.h.
            QObject::connect(singleInstance.get(), &brulion::ipc::SingleInstance::messageReceived, controller.get(),
                             [ctl = controller.get()](const QByteArray &line) {
                                 const auto message = brulion::ipc::parseMessage(line);
                                 if (!message) {
                                     qCWarning(brulion::logging::lcApp) << "Ignoring a malformed instance message";
                                     return;
                                 }
                                 ctl->handleInstanceMessage(message->verb, message->payload);
                             });
        }
#endif
#ifdef Q_OS_ANDROID
        QObject::connect(androidBridge, &brulion::platform::AndroidBridge::activated, controller.get(),
                         &AppController::openNotification);
#endif
    };
    const auto startUi = [&] {
        // QQmlApplicationEngine resolves every URL (QML types, images, icons) through a
        // QQmlFileSelector, which looks up +<selector>/ variants in the resources: about a third
        // of the QML type loading before the first window on a phone-class CPU. Brulion has no
        // file selector directories (src/app/CMakeLists.txt checks): remove it. get() initialises
        // the engine as its first load would, at the same moment (locale, Qt translations); it is
        // deprecated, but the only way to the selector the engine creates in its first load.
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        delete QQmlFileSelector::get(&engine);
        QT_WARNING_POP
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
        // AI agents and scripts drive the running app over a socket of its own (remotecontrol.h):
        // with --remote-control, or while the device setting is on (Settings → Application). It
        // listens only then; it exists for the settings page also in --screenshot runs.
        if (mainUi) {
            brulion::shell::RemoteControl::Options remote;
            remote.key = brulion::ipc::controlKeyFor(brulion::ipc::SingleInstance::keyFor(
                    QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));
            remote.forced = args.contains(u"--remote-control"_s);
            remote.language = [&devTools] { return devTools.language(); };
            remoteControl = std::make_unique<brulion::shell::RemoteControl>(&engine, controller.get(), remote);
        }
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
    // ?selftest=glyphs (scripts/wasm-smoke.mjs --glyphs): did the fallback fonts load and draw
    // Brulion's emoji and symbols?
    if (args.contains(u"--selftest-glyphs"_s))
        wasm::publishSelfTestResult("brulionGlyphSelfTest", brulion::ui::fallbackFontsJson(fallbackFonts));
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
