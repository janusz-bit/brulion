#include "localnetwork.h"

#if QT_CONFIG(qml_network)
#include <QDebug>
#include <QMetaObject>
#include <QNetworkReply>

#include <algorithm>
#include <cstring>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

/// The answer to a refused request: a transparent 1×1 PNG, at once (the signals come from the
/// event loop, as for a real reply). Not an error: Qt Quick 6.11 can crash when several pictures
/// of one rich text fail to load (QQuickText::resourceRequestFinished; seen now and then with two
/// pictures failing at once), and a note must never be able to crash the app.
class BlankPictureReply : public QNetworkReply {
public:
    BlankPictureReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QObject *parent)
        : QNetworkReply(parent)
        , m_data(QByteArray::fromBase64(
                  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgAAIAAAUAAen63NgAAAAASUVORK5CYII="))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(op);
        setHeader(QNetworkRequest::ContentTypeHeader, u"image/png"_s);
        setHeader(QNetworkRequest::ContentLengthHeader, m_data.size());
        open(QIODevice::ReadOnly);
        setFinished(true);
        QMetaObject::invokeMethod(
                this,
                [this] {
                    Q_EMIT metaDataChanged();
                    Q_EMIT readyRead();
                    Q_EMIT finished();
                },
                Qt::QueuedConnection);
    }

    void abort() override { }

    qint64 bytesAvailable() const override { return m_data.size() - m_pos + QNetworkReply::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (m_pos >= m_data.size())
            return -1;
        const qint64 n = std::min<qint64>(maxSize, m_data.size() - m_pos);
        std::memcpy(data, m_data.constData() + m_pos, size_t(n));
        m_pos += n;
        return n;
    }

private:
    QByteArray m_data;
    qint64 m_pos = 0;
};

bool isSeparator(QChar c)
{
    return c == u'/' || c == u'\\';
}

} // namespace

bool LocalOnlyNetworkAccessManager::isLocal(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme == u"file") {
        // Only a file of this machine. A host other than localhost (file://server/share/x) or a path
        // starting with two separators (file:////server/share/x, also with backslashes) is a UNC
        // path to QUrl::toLocalFile(): on Windows opening it connects to that server (SMB, handing
        // it the user's NTLM hash); elsewhere it is not the file named either.
        const QString host = url.host();
        const QString path = url.path();
        const bool unc = path.size() >= 2 && isSeparator(path.at(0)) && isSeparator(path.at(1));
        return (host.isEmpty() || host.compare(u"localhost", Qt::CaseInsensitive) == 0) && !unc;
    }
    return scheme == u"qrc" || scheme == u"data" || scheme == u"assets" || scheme == u"content";
}

QNetworkReply *LocalOnlyNetworkAccessManager::createRequest(Operation op, const QNetworkRequest &request,
                                                            QIODevice *outgoingData)
{
    if (isLocal(request.url()))
        return QNetworkAccessManager::createRequest(op, request, outgoingData);
    qWarning().noquote() << u"QML asked for %1 — not loaded: Brulion loads nothing from the network there"_s.arg(
            request.url().toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment));
    return new BlankPictureReply(op, request, this);
}

QNetworkAccessManager *LocalOnlyNetworkFactory::create(QObject *parent)
{
    return new LocalOnlyNetworkAccessManager(parent);
}

} // namespace brulion::ui

#endif
