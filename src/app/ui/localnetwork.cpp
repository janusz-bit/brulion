#include "localnetwork.h"

#include <QDebug>
#include <QDir>
#include <QMutexLocker>

#include <algorithm>
#include <utility>

#if QT_CONFIG(qml_network)
#include <QMetaObject>
#include <QNetworkReply>

#include <cstring>
#endif

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

bool isSeparator(QChar c)
{
    return c == u'/' || c == u'\\';
}

/// `url` for the log: no user name, password, query or fragment.
QString logged(const QUrl &url)
{
    return url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
}

/// A local path cleaned as Windows reads it (backslashes too, "..") and ending in "/", so that a
/// folder's own files start with its form.
QString folderForm(QString path)
{
    path = QDir::cleanPath(path.replace(u'\\', u'/'));
    return path.endsWith(u'/') ? path : path + u'/';
}

#if QT_CONFIG(qml_network)
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
#endif

} // namespace

bool isRemoteFileUrl(const QUrl &url)
{
    const QString scheme = url.scheme();
    if (scheme.compare(u"webdavs", Qt::CaseInsensitive) == 0)
        return true;
    if (scheme.compare(u"file", Qt::CaseInsensitive) != 0)
        return false;
    const QString host = url.host();
    if (!host.isEmpty() && host.compare(u"localhost", Qt::CaseInsensitive) != 0)
        return true;
    const QString path = url.path();
    return path.size() >= 2 && isSeparator(path.at(0)) && isSeparator(path.at(1));
}

void RemoteFileUrlGuard::setNotebooks(const QStringList &roots)
{
    QStringList folders;
    for (const QString &root : roots) {
        if (!root.isEmpty())
            folders.append(folderForm(root));
    }
    const QMutexLocker lock(&m_mutex);
    m_notebooks = std::move(folders);
    m_refused.clear();
}

QUrl RemoteFileUrlGuard::intercept(const QUrl &url, DataType type)
{
    if (type != UrlString || !isRemoteFileUrl(url))
        return url;
    // A file of an open notebook: the path Qt would open (QUrl::toLocalFile(), whatever the
    // spelling) inside the notebook's folder, case-insensitive as Windows' paths are.
    const QString local = url.toLocalFile();
    const QString path = local.isEmpty() ? QString() : folderForm(local);
    {
        const QMutexLocker lock(&m_mutex);
        if (!path.isEmpty() && std::ranges::any_of(m_notebooks, [&path](const QString &folder) {
                return path.startsWith(folder, Qt::CaseInsensitive);
            }))
            return url;
        if (url == m_refused) // said already (Image resolves its source twice, a binding again)
            return {};
        m_refused = url;
    }
    qWarning().noquote() << u"QML asked for %1 — not loaded: a file of another machine, not of the open notebook"_s.arg(
            logged(url));
    return {};
}

#if QT_CONFIG(qml_network)

bool LocalOnlyNetworkAccessManager::isLocal(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme == u"file")
        return !isRemoteFileUrl(url);
    return scheme == u"qrc" || scheme == u"data" || scheme == u"assets" || scheme == u"content";
}

QNetworkReply *LocalOnlyNetworkAccessManager::createRequest(Operation op, const QNetworkRequest &request,
                                                            QIODevice *outgoingData)
{
    if (isLocal(request.url()))
        return QNetworkAccessManager::createRequest(op, request, outgoingData);
    // (An empty URL is one the guard emptied: it said why.)
    if (!request.url().isEmpty())
        qWarning().noquote() << u"QML asked for %1 — not loaded: Brulion loads nothing from the network there"_s.arg(
                logged(request.url()));
    return new BlankPictureReply(op, request, this);
}

QNetworkAccessManager *LocalOnlyNetworkFactory::create(QObject *parent)
{
    return new LocalOnlyNetworkAccessManager(parent);
}

#endif

} // namespace brulion::ui
