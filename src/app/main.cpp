#include "platform.h"
#include "ui/devtools.h" // design system dev tools: --gallery, --screenshot, --size … (devtools.h)

#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTranslator>

#ifdef Q_OS_WASM
#include "wasm/wasmselftest.h"
#include "wasm/wasmshell.h"
#include "wasm/wasmstorage.h"
#endif

int main(int argc, char *argv[])
{
    brulion::ui::DevTools::applyEarlyArguments(argc, argv); // --scale (before the app exists)
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Brulion"));
    QGuiApplication::setOrganizationName(QStringLiteral("Brulion"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("januszbit.github.io"));
    QGuiApplication::setApplicationVersion(QStringLiteral(BRULION_VERSION));

    // Dev/test options from the command line (component gallery, screenshots, forced theme
    // and language): src/app/ui/devtools.h. Without options it changes nothing.
    brulion::ui::DevTools devTools(QCoreApplication::arguments());

    // Polish is the default UI language (English when the system or --locale en asks for it).
    // English loads brulion_en.qm too: it holds the plural forms of the English source texts
    // ("%n unchanged line(s)" -> "1 unchanged line" / "3 unchanged lines").
    QTranslator translator;
    if (translator.load(QStringLiteral(":/i18n/brulion_") + devTools.language() + QStringLiteral(".qm")))
        QCoreApplication::installTranslator(&translator);
    devTools.setTranslator(&translator);

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    devTools.attach(&engine); // --size, --screenshot
#ifdef Q_OS_WASM
    // Browser: the file system lives in memory. Load the vault and app data from IndexedDB
    // first; changes are written back automatically (docs/WEB.md).
    namespace wasm = brulion::platform;
    wasm::WasmStorage storage;
    QObject::connect(
        &storage, &wasm::WasmStorage::ready, &engine,
        [&storage, &engine, &devTools] {
            const QStringList args = QCoreApplication::arguments();
            if (args.contains(QStringLiteral("--selftest-storage")))
                wasm::runStorageSelfTest(&storage);
            if (args.contains(QStringLiteral("--selftest-notify")))
                wasm::runNotifierSelfTest();
            wasm::setShellStage(wasm::ShellStage::StartingUi);
            engine.loadFromModule("Brulion", devTools.rootComponent()); // "Main" or "Gallery"
            wasm::hideShellLoadingScreen();
        },
        Qt::SingleShotConnection);
    wasm::setShellStage(wasm::ShellStage::LoadingNotes);
    storage.start();
#else
    engine.loadFromModule("Brulion", devTools.rootComponent()); // "Main" or "Gallery"
#endif
    return QGuiApplication::exec();
}
