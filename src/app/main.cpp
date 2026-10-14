#include "platform.h"

#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Brulion"));
    QGuiApplication::setOrganizationName(QStringLiteral("Brulion"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("januszbit.github.io"));
    QGuiApplication::setApplicationVersion(QStringLiteral(BRULION_VERSION));

    // Polish is the default UI language; the translation is loaded unless the system is English.
    QTranslator translator;
    if (!QLocale().name().startsWith(QLatin1String("en"))
        && translator.load(QStringLiteral(":/i18n/brulion_pl.qm")))
        QCoreApplication::installTranslator(&translator);

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Brulion", "Main");
    return QGuiApplication::exec();
}
