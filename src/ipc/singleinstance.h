// SingleInstance — one Brulion per user session on the desktop (QLocalServer/QLocalSocket).
//
//   SingleInstance instance(SingleInstance::keyFor(appDataDir));
//   if (instance.sendToPrimary("activate"))   // another Brulion runs: it shows its window
//       return 0;
//   if (!instance.listen()) {
//       if (!instance.errorString().isEmpty())
//           ;                                  // no safe place for it: run without (log why)
//       else if (instance.sendToPrimary("activate"))
//           return 0;                          // another one started at the same moment and won
//   }
//   connect(&instance, &SingleInstance::messageReceived, ...);
//
// Messages are short lines of UTF-8 ("activate", "capture", "open {…}": ipc/appmessage.h). The
// primary holds a lock file next to the socket; a crashed instance's stale lock and socket are
// replaced, a live primary is never taken over. The socket lives in the user's runtime folder
// ($XDG_RUNTIME_DIR) and nowhere else: in the shared temp folder (a bare key: no runtime folder)
// or a folder others can change (socketFolderProblem()) another user could create the name first
// — keeping Brulion from starting — and receive what a second launch and brulion-cli send, so the
// app runs without a single instance there. Both ends talk only to this user's processes (Linux:
// SO_PEERCRED, trustedPeer()): a launch or brulion-cli sends nothing to another user's socket, the
// primary drops other users' connections. brulion-cli reaches the running app through it
// (`brulion-cli app open …`). Not used on Android (the system keeps one activity) or in the
// browser (the HTML shell handles a second tab). Library brulion_ipc (src/ipc, desktop only).
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

class QLocalServer;
class QLocalSocket;
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
    /// Deliver `message` to a running primary instance of this user. False when there is none,
    /// or with errorString() when another user's process listens at the key (nothing sent).
    bool sendToPrimary(const QByteArray &message, int timeoutMs = 1000);
    /// Does a primary instance of this user listen at this key? (Connects without sending
    /// anything; another user's process: false, with errorString().)
    bool isPrimaryRunning(int timeoutMs = 300) const;
    /// Become the primary instance. False when another instance is the primary already (it won a
    /// simultaneous start: send it the message instead; errorString() is empty), or with
    /// errorString() when the single instance cannot be had here: the key is not in a folder only
    /// this user can change (socketFolderProblem(): no private runtime folder), or the lock file
    /// or the socket cannot be created — run without it.
    bool listen();
    bool isListening() const;
    /// Why the last sendToPrimary(), isPrimaryRunning() or listen() failed, when not because no
    /// primary listened or another one is the primary; "" otherwise.
    QString errorString() const { return m_error; }

Q_SIGNALS:
    void messageReceived(const QByteArray &message);
    /// The primary dropped a connection of another user (`uid`; -1: its credentials could not be
    /// read).
    void refused(qint64 uid);

private:
    /// A connection to this user's primary instance; nullptr when none listens or (errorString())
    /// another user's process does.
    std::unique_ptr<QLocalSocket> connectToPrimary(int timeoutMs) const;

    QString m_key;
    mutable QString m_error;
    QLocalServer *m_server = nullptr;
    std::unique_ptr<QLockFile> m_lock; ///< held while this is the primary instance
};

} // namespace brulion::ipc
