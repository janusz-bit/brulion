#include "webclippersetup.h"

#include "clipper/hostinstall.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

using namespace brulion::clipper;

/// The program the manifests name: brulion-cli next to the app, else the app.
QString hostProgram()
{
    const QString dir = QCoreApplication::applicationDirPath();
    const bool windows = currentOs() == Os::Windows;
    const QString cli = dir + (windows ? u"/brulion-cli.exe"_s : u"/brulion-cli"_s);
    if (QFileInfo(cli).isExecutable())
        return defaultHostProgram(QDir::cleanPath(cli));
    return defaultHostProgram(QCoreApplication::applicationFilePath());
}

} // namespace

WebClipperSetup::WebClipperSetup(QObject *parent) : QObject(parent)
{
    if (available()) {
        m_program = hostProgram();
        refresh();
    }
}

bool WebClipperSetup::available() const
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
    return false;
#else
    return true;
#endif
}

void WebClipperSetup::refresh()
{
    if (!available())
        return;
    const Os os = currentOs();
    const Environment env = currentEnvironment();
    QVariantList list;
    int connected = 0;
    for (const Browser b : allBrowsers()) {
        const HostState user = hostState(b, os, Scope::User, env, m_program);
        const HostState system = hostState(b, os, Scope::System, env, m_program);
        const HostState &shown = user.installed ? user : system;
        const bool isConnected = shown.installed && shown.programExists && shown.extensionAllowed;
        const bool broken = shown.installed && !isConnected;
        QString status;
        if (broken)
            status = tr("Connected to a program that is gone: %1").arg(QDir::toNativeSeparators(shown.program));
        else if (user.installed)
            status = tr("Its clips come to Brulion.");
        else if (system.installed)
            status = tr("Its clips come to Brulion (set up for every user of the computer).");
        else if (user.browserFound)
            status = tr("Not connected");
        else
            status = tr("Not installed for you");
        if (isConnected)
            ++connected;
        list.append(QVariantMap{{u"browserId"_s, browserId(b)},
                                {u"browserName"_s, browserName(b)},
                                {u"found"_s, user.browserFound},
                                {u"connected"_s, isConnected},
                                {u"broken"_s, broken},
                                {u"statusText"_s, status}});
    }
    m_browsers = list;
    m_connected = connected;
    Q_EMIT changed();
}

void WebClipperSetup::connectBrowsers()
{
    if (!available())
        return;
    HostSetup setup;
    setup.os = currentOs();
    setup.env = currentEnvironment();
    setup.program = m_program;
    QStringList done;
    QStringList failed;
    for (const Browser b : allBrowsers()) {
        const HostState before = hostState(b, setup.os, Scope::User, setup.env, m_program);
        if (!before.browserFound && !before.installed)
            continue;
        const HostState after = installHost(b, setup);
        if (after.error.isEmpty())
            done.append(browserName(b));
        else
            failed.append(browserName(b) + u": "_s + after.error);
    }
    if (!failed.isEmpty()) {
        m_message = tr("Could not connect %1").arg(failed.join(u"; "_s));
        m_messageIsError = true;
    } else if (done.isEmpty()) {
        m_message = tr("No browser found for you. Install the extension in Chrome, Chromium, Brave, Edge or Firefox, "
                       "or connect one with brulion-cli clip install --browser.");
        m_messageIsError = true;
    } else {
        m_message = tr("Connected: %1. Clips from the extension now come to the notebook open in Brulion.")
                            .arg(done.join(u", "_s));
        m_messageIsError = false;
    }
    refresh();
}

void WebClipperSetup::disconnectBrowsers()
{
    if (!available())
        return;
    HostSetup setup;
    setup.os = currentOs();
    setup.env = currentEnvironment();
    setup.program = m_program;
    QStringList failed;
    for (const Browser b : allBrowsers()) {
        const HostState after = uninstallHost(b, setup);
        if (!after.error.isEmpty())
            failed.append(browserName(b) + u": "_s + after.error);
    }
    m_message = failed.isEmpty() ? tr("The browsers are no longer connected to Brulion.")
                                 : tr("Could not disconnect %1").arg(failed.join(u"; "_s));
    m_messageIsError = !failed.isEmpty();
    refresh();
}

} // namespace brulion::shell
