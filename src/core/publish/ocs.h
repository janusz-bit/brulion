// Nextcloud's OCS Share API (files_sharing v1, https://docs.nextcloud.com/server/latest/
// developer_manual/client_apis/OCS/ocs-share-api.html) as publishing uses it: the public link of a
// folder — found, made (read-only, optionally with a password and an expiry date), changed and
// removed. HTTP through sync::HttpTransport with Basic auth (an app password); the password is
// masked in every log line. Part of the publish module — see publish/publish.h.
#pragma once

#include "log/redact.h"
#include "sync/http.h"

#include <QDate>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace brulion::publish {

/// A public link share.
struct Share {
    QString id;
    QString url; ///< "https://cloud.example.com/s/AbCdEf"
    QString token;
    QString path;  ///< the shared folder, from the user's root ("/Publiczne/Przepisy")
    QDate expires; ///< invalid: none
    bool hasPassword = false;
};

/// A failed OCS call: the HTTP status (0: no answer), OCS's status code and message.
struct OcsError {
    int status = 0;
    int ocsStatus = 0;
    QString message; ///< technical (redacted): Nextcloud's own words or the transport's
    bool timedOut = false;
    bool isError() const { return status == 0 || status >= 400 || (ocsStatus != 0 && ocsStatus >= 400); }
    /// "auth" | "forbidden" | "not-found" | "share" (refused: a weak password, a policy) | "server"
    /// | "network" | "timeout" | "parse".
    QString code() const;
};

class OcsShareClient : public QObject {
    Q_OBJECT
public:
    template<typename T>
    using Done = std::function<void(const std::optional<OcsError> &, const T &)>;
    using Finished = std::function<void(const std::optional<OcsError> &)>;

    /// `server`: as the user typed it (sync::nextcloudDavUrl's rules). `transport` is not owned.
    OcsShareClient(const QString &server, const QString &username, const QString &password,
                   sync::HttpTransport *transport, QObject *parent = nullptr);
    ~OcsShareClient() override;
    Q_DISABLE_COPY_MOVE(OcsShareClient)

    /// "https://cloud.example.com" (sub-path kept, no trailing slash).
    QString serverUrl() const { return m_server; }
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    /// The public link of `path` (nullopt: none).
    void find(const QString &path, Done<std::optional<Share>> done);
    /// A share by id (nullopt: gone).
    void get(const QString &id, Done<std::optional<Share>> done);
    /// A read-only public link to the folder `path`; `password` "" for none, `expires` invalid for none.
    void create(const QString &path, const QString &password, const QDate &expires, Done<Share> done);
    /// Set the link's password ("" removes it).
    void setPassword(const QString &id, const QString &password, Finished done);
    /// Set its expiry date (invalid removes it).
    void setExpires(const QString &id, const QDate &expires, Finished done);
    void remove(const QString &id, Finished done);

private:
    void call(const QByteArray &method, const QString &path, const QList<std::pair<QString, QString>> &form,
              std::function<void(const std::optional<OcsError> &, const QJsonValue &data)> done);

    QString m_server;
    QByteArray m_authorization;
    logging::ScopedSecret m_secretPassword;
    logging::ScopedSecret m_secretToken;
    sync::HttpTransport *m_transport;
    int m_timeoutMs = 60000;
};

/// The shared folder's path as OCS names it: "/" + the folder ("Publiczne/Przepisy" →
/// "/Publiczne/Przepisy").
QString sharePath(const QString &folder);

} // namespace brulion::publish
