// An in-memory Nextcloud for the publish module's tests: the files through the sync tests'
// FakeWebDavServer (WebDAV at /remote.php/dav/files/<user>) and the OCS Share API's public links
// (GET / POST /ocs/v2.php/apps/files_sharing/api/v1/shares, GET / PUT / DELETE …/shares/<id>,
// JSON, Basic auth, a password policy), answered asynchronously like the real transport.
#pragma once

#include "fakewebdavserver.h"

#include <QDate>
#include <QMap>
#include <QObject>
#include <QString>

namespace publishtest {

class FakeNextcloud : public QObject, public brulion::sync::HttpTransport {
    Q_OBJECT
public:
    struct Share {
        QString path; ///< "/Publiczne/Strona"
        QString token;
        QString password; ///< as set ("" none)
        QDate expires;
        int type = 3;
    };
    struct LoggedOcs {
        QByteArray method;
        QString path;                  ///< after …/shares ("" or "/<id>")
        QMap<QString, QString> fields; ///< query and form fields, decoded
    };

    explicit FakeNextcloud(QObject *parent = nullptr);
    ~FakeNextcloud() override;

    void send(const brulion::sync::HttpRequest &request, brulion::sync::HttpCallback done) override;

    synctest::FakeWebDavServer dav;
    /// Shares by id.
    QMap<QString, Share> shares;
    /// Passwords shorter than this are refused (Nextcloud's password policy; 0: none).
    int minPasswordLength = 0;
    QList<LoggedOcs> ocsLog;
    int ocsCount(const QByteArray &method) const;
    /// The server's address as a user types it.
    QString server() const { return QStringLiteral("chmura.example.pl"); }

private:
    brulion::sync::HttpResponse ocs(const brulion::sync::HttpRequest &request, LoggedOcs &logged);
    int m_nextId = 16;
};

} // namespace publishtest
