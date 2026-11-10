#include "platform.h"

#include <QGuiApplication>
#include <QStandardPaths>
#include <QSysInfo>
#include <QWindow>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace Qt::StringLiterals;

namespace brulion::platform {

QString targetName()
{
#if defined(Q_OS_ANDROID)
    return u"android"_s;
#elif defined(Q_OS_WASM)
    return u"wasm"_s;
#elif defined(Q_OS_WIN)
    return u"windows"_s;
#else
    return u"linux"_s;
#endif
}

QString logDirectory()
{
#if defined(Q_OS_WASM)
    return {};
#elif defined(Q_OS_ANDROID)
    const QString files = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return files.isEmpty() ? QString() : files + u"/logs"_s;
#else
    QString state = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    if (state.isEmpty())
        state = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return state.isEmpty() ? QString() : state + u"/logs"_s;
#endif
}

QString deviceDescription()
{
#if defined(Q_OS_WASM)
    return u"WebAssembly (%1)"_s.arg(QSysInfo::buildCpuArchitecture());
#else
    // simplified(): Qt leaves a double space where it knows no codename ("Android  (16.0)").
    QString product = QSysInfo::prettyProductName().simplified();
    if (product.isEmpty())
        product = QSysInfo::productType();
#if defined(Q_OS_ANDROID)
    return u"%1, %2"_s.arg(product, QSysInfo::currentCpuArchitecture());
#else
    const QString kernelVersion = QSysInfo::kernelVersion();
    const QString kernel = QSysInfo::kernelType() == u"linux" && !kernelVersion.isEmpty()
                                   ? u" (Linux %1)"_s.arg(kernelVersion.section(u'-', 0, 0))
                                   : QString();
    return u"%1%2, %3"_s.arg(product, kernel, QSysInfo::currentCpuArchitecture());
#endif
#endif
}

void allowForegroundActivation()
{
#ifdef Q_OS_WIN
    AllowSetForegroundWindow(ASFW_ANY);
#endif
}

void endStartupNotification(const QByteArray &startupId)
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (startupId.isEmpty() || QGuiApplication::platformName() != u"xcb")
        return;
    QWindow window;
    window.setFlags(Qt::ToolTip | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    window.setGeometry(-16, -16, 1, 1);
    window.show(); // QXcbScreen::windowShown(): "remove: ID=<DESKTOP_STARTUP_ID>", flushed
    window.close();
#else
    Q_UNUSED(startupId)
#endif
}

} // namespace brulion::platform
