// appupdate — is a newer Brulion released on GitHub?
//
// Only plain versions count ("0.6.3", "v0.6.3"): GitHub's "latest release" is never a draft or a
// pre-release, and an answer that says otherwise, or whose tag is anything else, names no version.
// The address of a release's page is built from its version, never taken from the answer, so a
// wrong answer cannot send the user anywhere but this project's releases.
//
// UpdateChecker asks for the latest release and tells nothing else about the device: no cookies,
// no language, the app's name without its version. At most MaxBytes are read, a stall of TimeoutMs
// fails.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace brulion::appupdate {

/// GitHub's API answer about the project's latest release.
QUrl latestReleaseApiUrl();
/// The page of the release of `version` (normalizedVersion()'s form).
QUrl releasePageUrl(const QString &version);

/// "v0.6.3" and "0.6.3" → "0.6.3"; "" for anything else ("0.7.0-rc.1", "1.2", "latest").
QString normalizedVersion(const QString &text);
/// `candidate` is a later version than `current`; false when either is no plain version.
bool isNewerVersion(const QString &candidate, const QString &current);
/// The version in GitHub's answer about a release; "" when the answer is malformed, the release is
/// a draft or a pre-release, or its tag is no plain version.
QString versionFromRelease(const QByteArray &json);

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 MaxBytes = 2 * 1024 * 1024;
    static constexpr int TimeoutMs = 30 * 1000;

    /// Asks `apiUrl` (tests: a local server) over an access manager of its own, made at the first
    /// check.
    explicit UpdateChecker(const QUrl &apiUrl, QObject *parent = nullptr);
    ~UpdateChecker() override;

    /// checked() or failed() follows from the event loop. Ignored while a check is running.
    void check();
    bool isChecking() const { return m_reply != nullptr; }
    void setMaxBytes(qint64 bytes) { m_maxBytes = bytes; }

Q_SIGNALS:
    /// `version` is the latest release's (normalizedVersion()'s form).
    void checked(const QString &version);
    /// `detail`, for the log: the HTTP status or the network's error text.
    void failed(const QString &detail);

private:
    void finish(QNetworkReply *reply);

    QUrl m_apiUrl;
    QNetworkAccessManager *m_manager = nullptr;
    QPointer<QNetworkReply> m_reply;
    qint64 m_maxBytes = MaxBytes;
    bool m_tooLarge = false;
};

} // namespace brulion::appupdate
