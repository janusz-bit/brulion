// The QML engine's network access (docs/ARCHITECTURE.md, "Data safety"): Brulion loads nothing from
// the network in QML — pictures come from the vault (file: URLs, data: URLs in the browser), icons
// from the resources. Rich text, though, can name a remote <img>, and Qt Quick fetches it on its
// own (Text { textFormat: RichText }); the HTML comes from our renderers, which escape everything
// of the note, but a hole there would make a synced or imported note a tracking beacon. So the
// engine gets an access manager that refuses every URL that is not local: defence in depth.
#pragma once

#include <QtQml/qtqmlglobal.h>

#if QT_CONFIG(qml_network)
#include <QNetworkAccessManager>
#include <QQmlNetworkAccessManagerFactory>

namespace brulion::ui {

/// A QNetworkAccessManager for local URLs only: file:, qrc:, data: and Android's assets: and
/// content: are served as usual; any other request (http:, https:, ftp:, …) is never sent — it is
/// answered at once with a transparent 1×1 PNG (not an error: Qt Quick's rich text can crash
/// when several of its pictures fail) and a warning in the log.
class LocalOnlyNetworkAccessManager : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;

    /// Is `url` served (a local scheme)?
    static bool isLocal(const QUrl &url);

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override;
};

/// Gives the QML engine LocalOnlyNetworkAccessManagers (QQmlEngine::setNetworkAccessManagerFactory;
/// the factory must outlive the engine).
class LocalOnlyNetworkFactory : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager *create(QObject *parent) override;
};

} // namespace brulion::ui

#endif // QT_CONFIG(qml_network): without it, Qt Quick loads nothing from the network anyway
