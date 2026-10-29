#include "platform.h"

#include <QStandardPaths>
#include <QSysInfo>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace brulion::platform {

QString targetName()
{
#if defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_WASM)
    return QStringLiteral("wasm");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

QString logDirectory()
{
#if defined(Q_OS_WASM)
    return {};
#elif defined(Q_OS_ANDROID)
    const QString files = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return files.isEmpty() ? QString() : files + QStringLiteral("/logs");
#else
    QString state = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    if (state.isEmpty())
        state = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return state.isEmpty() ? QString() : state + QStringLiteral("/logs");
#endif
}

QString deviceDescription()
{
#if defined(Q_OS_WASM)
    return QStringLiteral("WebAssembly (%1)").arg(QSysInfo::buildCpuArchitecture());
#else
    // simplified(): Qt leaves a double space where it knows no codename ("Android  (16.0)").
    QString product = QSysInfo::prettyProductName().simplified();
    if (product.isEmpty())
        product = QSysInfo::productType();
#if defined(Q_OS_ANDROID)
    return QStringLiteral("%1, %2").arg(product, QSysInfo::currentCpuArchitecture());
#else
    const QString kernel =
            QSysInfo::kernelType()
            == QStringLiteral("linux") && !QSysInfo::kernelVersion().isEmpty()
            ? QStringLiteral(" (Linux %1)").arg(QSysInfo::kernelVersion().section(QLatin1Char('-'), 0, 0))
            : QString();
    return QStringLiteral("%1%2, %3").arg(product, kernel, QSysInfo::currentCpuArchitecture());
#endif
#endif
}

void allowForegroundActivation()
{
#ifdef Q_OS_WIN
    AllowSetForegroundWindow(ASFW_ANY);
#endif
}

} // namespace brulion::platform
