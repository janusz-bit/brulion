// MsTodoImport — "Sign in to Microsoft" on the Microsoft To Do card of the import (QML:
// `MsTodoImport { settings: AppSettings; importer: importController }`): the device-code sign-in
// (mstodo::DeviceCodeLogin) with the user's own app registration, the download of every list
// (mstodo::TodoDownload), then the bundle goes to the ImportController (setInput), whose preview,
// collision policy and report do the rest — exactly as for a picked Graph Explorer file. Input
// that reaches the ImportController another way while signing in (picked files, a typed path)
// cancels the sign-in.
//
//   clientId, authority   the app registration's Application (client) ID and the authority
//                         ("consumers" by default), remembered on this device in AppSettings
//                         ("import/mstodo/clientId", "import/mstodo/authority" — one registration
//                         serves every notebook); clientIdError / authorityError once showErrors
//   state                 "idle" | "starting" (asking for a code) | "waiting" (userCode and
//                         verificationUrl shown, secondsLeft counting down) | "downloading"
//                         (progressText) | "error" (errorText, errorCode)
//   signIn(), cancel(), copyCode()
//
// The access token lives in memory only for the download (never logged, never written; no
// refresh token). Not offered in the browser (WebAssembly): Microsoft's device-code endpoints
// refuse requests from web pages (CORS) — the QML shows the file import there.
#pragma once

#include "appsettings.h"
#include "importcontroller.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <memory>

namespace brulion::sync {
class HttpTransport;
class NetworkHttpTransport;
} // namespace brulion::sync

namespace brulion::mstodo {
class DeviceCodeLogin;
class TodoDownload;
} // namespace brulion::mstodo

namespace brulion::ioviews {

class MsTodoImport : public QObject {
    Q_OBJECT

    Q_PROPERTY(brulion::shell::AppSettings *settings READ settings WRITE setSettings NOTIFY settingsChanged FINAL)
    Q_PROPERTY(brulion::ioviews::ImportController *importer READ importer WRITE setImporter NOTIFY importerChanged FINAL)
    Q_PROPERTY(QString clientId READ clientId WRITE setClientId NOTIFY clientIdChanged FINAL)
    Q_PROPERTY(QString authority READ authority WRITE setAuthority NOTIFY authorityChanged FINAL)
    Q_PROPERTY(QString clientIdError READ clientIdError NOTIFY validationChanged FINAL)
    Q_PROPERTY(QString authorityError READ authorityError NOTIFY validationChanged FINAL)
    Q_PROPERTY(bool showErrors READ showErrors WRITE setShowErrors NOTIFY validationChanged FINAL)
    Q_PROPERTY(QString defaultAuthority READ defaultAuthority CONSTANT FINAL)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString userCode READ userCode NOTIFY codeChanged FINAL)
    /// The address to open ("https://microsoft.com/devicelogin") and its short form for display.
    Q_PROPERTY(QString verificationUrl READ verificationUrl NOTIFY codeChanged FINAL)
    Q_PROPERTY(QString verificationText READ verificationText NOTIFY codeChanged FINAL)
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY secondsLeftChanged FINAL)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged FINAL)

public:
    explicit MsTodoImport(QObject *parent = nullptr);
    ~MsTodoImport() override;

    shell::AppSettings *settings() const { return m_settings; }
    void setSettings(shell::AppSettings *settings);
    ImportController *importer() const { return m_importer; }
    void setImporter(ImportController *importer);
    /// HTTP for the sign-in and the download (tests: a fake Microsoft). Not owned; set before
    /// signIn(). Default: an own NetworkHttpTransport.
    void setTransport(sync::HttpTransport *transport);
    /// Length of one second of the sign-in's polling and of Retry-After in ms (tests).
    void setTimeUnitMs(int ms) { m_unitMs = qMax(1, ms); }

    QString clientId() const { return m_clientId; }
    void setClientId(const QString &id);
    QString authority() const { return m_authority; }
    void setAuthority(const QString &authority);
    QString clientIdError() const;
    QString authorityError() const;
    bool showErrors() const { return m_showErrors; }
    void setShowErrors(bool on);
    QString defaultAuthority() const;
    QString state() const { return m_state; }
    bool busy() const;
    QString userCode() const { return m_userCode; }
    QString verificationUrl() const { return m_verificationUrl; }
    QString verificationText() const;
    int secondsLeft() const { return m_secondsLeft; }
    QString progressText() const { return m_progressText; }
    QString errorText() const { return m_errorText; }
    QString errorCode() const { return m_errorCode; }

    /// Sign in and download (fields valid: else showErrors turns on and nothing starts).
    Q_INVOKABLE void signIn();
    /// Stop signing in or downloading; nothing is imported.
    Q_INVOKABLE void cancel();
    /// The user code to the clipboard; false when there is none.
    Q_INVOKABLE bool copyCode();

Q_SIGNALS:
    void settingsChanged();
    void importerChanged();
    void clientIdChanged();
    void authorityChanged();
    void validationChanged();
    void stateChanged();
    void codeChanged();
    void secondsLeftChanged();
    void progressChanged();
    /// The lists were downloaded and handed to the importer.
    void downloaded();

private:
    void setState(const QString &state, const QString &errorCode = {}, const QString &errorText = {});
    void setProgress(const QString &text);
    void setCode(const QString &code, const QString &url, int seconds);
    void ensureClients();

    QPointer<shell::AppSettings> m_settings;
    QPointer<ImportController> m_importer;
    sync::HttpTransport *m_transport = nullptr;
    std::unique_ptr<sync::NetworkHttpTransport> m_ownTransport;
    std::unique_ptr<mstodo::DeviceCodeLogin> m_login;
    std::unique_ptr<mstodo::TodoDownload> m_download;
    QTimer m_countdown;
    QString m_clientId;
    QString m_authority;
    bool m_showErrors = false;
    QString m_state = QStringLiteral("idle");
    QString m_userCode;
    QString m_verificationUrl;
    int m_secondsLeft = 0;
    QString m_progressText;
    QString m_errorText;
    QString m_errorCode;
    int m_unitMs = 1000;
};

} // namespace brulion::ioviews
