// SingleInstance — one Brulion per user session on the desktop (QLocalServer/QLocalSocket).
//
//   SingleInstance instance(SingleInstance::keyFor(appDataDir));
//   if (instance.sendToPrimary("activate"))   // another Brulion runs: it shows its window
//       return 0;
//   if (!instance.listen() && instance.sendToPrimary("activate"))
//       return 0;                              // another one started at the same moment and won
//   connect(&instance, &SingleInstance::messageReceived, ...);
//
// Messages are short lines of UTF-8 ("activate", "capture", "open {…}": ipc/appmessage.h). The
// primary holds a lock file next to the socket; a crashed instance's stale lock and socket are
// replaced, a live primary is never taken over. The socket lives in the user's runtime folder
// ($XDG_RUNTIME_DIR) when there is one. brulion-cli reaches the running app through it
// (`brulion-cli app open …`). Not used on Android (the system keeps one activity) or in the
// browser (the HTML shell handles a second tab). Library brulion_ipc (src/ipc, desktop only).
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

class QLocalServer;
class QLockFile;

namespace brulion::ipc {

class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString &key, QObject *parent = nullptr);
    ~SingleInstance() override;

    /// Server name (a socket path in the user's runtime folder when there is one) for an app
    /// data folder (instances with other data folders — tests, a portable copy — do not see
    /// each other).
    static QString keyFor(const QString &appDataDir);

    QString key() const { return m_key; }
    /// Deliver `message` to a running primary instance. False when there is none.
    bool sendToPrimary(const QByteArray &message, int timeoutMs = 1000);
    /// Does a primary instance listen at this key? (Connects without sending anything.)
    bool isPrimaryRunning(int timeoutMs = 300) const;
    /// Become the primary instance. False when the socket cannot be created or another
    /// instance listens already (it won a simultaneous start: send it the message instead).
    bool listen();
    bool isListening() const;

Q_SIGNALS:
    void messageReceived(const QByteArray &message);

private:
    QString m_key;
    QLocalServer *m_server = nullptr;
    std::unique_ptr<QLockFile> m_lock; ///< held while this is the primary instance
};

} // namespace brulion::ipc
