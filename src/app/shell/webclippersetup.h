// WebClipperSetup — Settings → Application → "Browser extension (Web Clipper)" (docs/CLIPPER.md): which
// browsers reach Brulion's native messaging host, and a button connecting them, on the desktop. The
// same code as `brulion-cli clip install` (clipper/hostinstall.h): the manifests, in the user's own
// folders (and on Windows the registry), name brulion-cli next to the app — the app's binary itself
// when there is none (its main() hands a browser's launch over to the command line's host), the
// AppImage for an AppImage, a stable link for a program of the Nix store. Manifests of the system
// (the NixOS module, `clip install --system`) count as connected and are never touched here.
#pragma once

#include "clipper/nativehost.h"

#include <QObject>
#include <QString>
#include <QVariantList>

namespace brulion::shell {

class WebClipperSetup : public QObject {
    Q_OBJECT
    /// Desktop systems (not Android, not the browser): native messaging needs a program here.
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    /// [{browserId, browserName, found, connected, broken, statusText}] in allBrowsers()' order.
    Q_PROPERTY(QVariantList browsers READ browsers NOTIFY changed FINAL)
    /// Browsers connected (by the user's or the system's manifest).
    Q_PROPERTY(int connectedCount READ connectedCount NOTIFY changed FINAL)
    /// The program the manifests name.
    Q_PROPERTY(QString program READ program CONSTANT FINAL)
    /// What the last connect or disconnect did ("" before any).
    Q_PROPERTY(QString message READ message NOTIFY changed FINAL)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY changed FINAL)

public:
    /// The folders of this process's environment (clipper::currentEnvironment()).
    explicit WebClipperSetup(QObject *parent = nullptr);
    /// The folders of `env` instead (tests: a private home and a private system's folders).
    explicit WebClipperSetup(const clipper::Environment &env, QObject *parent = nullptr);

    bool available() const;
    QVariantList browsers() const { return m_browsers; }
    int connectedCount() const { return m_connected; }
    QString program() const { return m_program; }
    QString message() const { return m_message; }
    bool messageIsError() const { return m_messageIsError; }

    /// Look at the manifests again.
    Q_INVOKABLE void refresh();
    /// Connect the browsers this user has (and those connected before, to name this program).
    Q_INVOKABLE void connectBrowsers();
    /// Remove the user's own manifests (the system's stay).
    Q_INVOKABLE void disconnectBrowsers();

Q_SIGNALS:
    void changed();

private:
    clipper::Environment m_env;
    QVariantList m_browsers;
    int m_connected = 0;
    QString m_program;
    QString m_message;
    bool m_messageIsError = false;
};

} // namespace brulion::shell
