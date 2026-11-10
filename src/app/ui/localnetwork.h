// The QML engine's network access and file URLs (docs/ARCHITECTURE.md, "Data safety"): Brulion
// loads nothing from the network in QML — pictures come from the vault (file: URLs, data: URLs in
// the browser), icons from the resources. Rich text, though, can name a remote <img>, and Qt Quick
// fetches it on its own (Text { textFormat: RichText }); the HTML comes from our renderers, which
// escape everything of the note, but a hole there would make a synced or imported note a tracking
// beacon. So the engine gets an access manager that refuses every URL that is not local: defence
// in depth. Qt Quick reads file: URLs itself (Image's pixmap reader, rich text's QTextDocument),
// never through that manager, and on Windows a file: URL naming another machine is a connection to
// it: the engine's URL guard (RemoteFileUrlGuard) empties such URL properties, except for files of
// the open notebook (a notebook on \\server\share keeps its pictures). An <img> inside rich text is
// resolved by Qt without the guard: that none comes from note text is the renderers' job.
#pragma once

#include <QtQml/qtqmlglobal.h>

#include <QMutex>
#include <QQmlAbstractUrlInterceptor>
#include <QStringList>
#include <QUrl>

#if QT_CONFIG(qml_network)
#include <QNetworkAccessManager>
#include <QQmlNetworkAccessManagerFactory>
#endif

namespace brulion::ui {

/// Does `url` name a file of another machine? A file: URL with a host other than localhost
/// (file://server/share/x) or a path starting with two separators (file:////server/share/x, also
/// with backslashes), which QUrl::toLocalFile() makes a UNC path, and webdavs: (Windows' WebDAV form
/// of one, a local file to QUrl there): Windows opens it by connecting to that machine (SMB or
/// WebDAV, handing it the user's NTLM hash); elsewhere it is not the file named either. Any other
/// URL (qrc:, a file of this machine, http:): false.
bool isRemoteFileUrl(const QUrl &url);

/// The QML engine's URL interceptor (QQmlEngine::addUrlInterceptor, which does not own it: it must
/// outlive the engine) for the URL properties QML resolves — Image's and Loader's source, a
/// TextEdit's document: one naming a file of another machine (isRemoteFileUrl()) becomes an empty
/// URL, with a warning in the log, unless the file lies inside an open notebook (setNotebooks():
/// one on \\server\share, whose pictures the Workspace names file://server/share/…). The QML,
/// scripts and qmldir files the engine loads are left alone (an app installed on a share).
/// Thread-safe (the engine's loader thread asks too).
class RemoteFileUrlGuard : public QQmlAbstractUrlInterceptor {
public:
    RemoteFileUrlGuard() = default;
    Q_DISABLE_COPY_MOVE(RemoteFileUrlGuard)

    /// The folders (local paths) of the open notebooks; "" entries are skipped.
    void setNotebooks(const QStringList &roots);

    QUrl intercept(const QUrl &url, DataType type) override;

private:
    QMutex m_mutex;
    QStringList m_notebooks; ///< each folder cleaned, ending in "/"
    QUrl m_refused;          ///< the last URL emptied (logged once in a row)
};

#if QT_CONFIG(qml_network)

/// A QNetworkAccessManager for local URLs only: file: of this machine (no host but localhost, no
/// UNC path), qrc:, data: and Android's assets: and content: are served as usual; any other request
/// (http:, https:, ftp:, file://server/share/…) is never sent — it is answered at once with a
/// transparent 1×1 PNG (not an error: Qt Quick's rich text can crash when several of its pictures
/// fail) and a warning in the log.
class LocalOnlyNetworkAccessManager : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;

    /// Is `url` served (a local scheme; a file: URL of this machine)?
    static bool isLocal(const QUrl &url);

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override;
};

/// Gives the QML engine LocalOnlyNetworkAccessManagers (QQmlEngine::setNetworkAccessManagerFactory;
/// the factory must outlive the engine).
class LocalOnlyNetworkFactory : public QQmlNetworkAccessManagerFactory {
public:
    LocalOnlyNetworkFactory() = default;
    Q_DISABLE_COPY_MOVE(LocalOnlyNetworkFactory)

    QNetworkAccessManager *create(QObject *parent) override;
};

#endif // QT_CONFIG(qml_network): without it, Qt Quick loads nothing from the network anyway

} // namespace brulion::ui
