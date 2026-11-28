// WorkspaceSync: end-to-end encryption of the Nextcloud folder set up (docs/SYNC-ENCRYPTION.md):
// a folder with a key file opened by its password or recovery key, an empty one made an encrypted
// notebook. The key derivation runs on a pool thread (PBKDF2: up to two seconds on a phone).
#include "workspacesync.h"

#include "crypto/secure.h"
#include "log/categories.h"
#include "sync/encryptionkeys.h"
#include "sync/sync.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

namespace encryption = sync::encryption;

const QString kReadme = u"README.txt"_s;

QByteArray readmeText()
{
    return "This folder is a Brulion notebook encrypted end to end: its names and files can be read only "
           "with the notebook's password, in Brulion 0.33 or later (docs/SYNC-ENCRYPTION.md).\n\n"
           "Ten folder to notatnik Brulionu zaszyfrowany end-to-end: jego nazwy i pliki da się odczytać "
           "tylko hasłem notatnika, w Brulionie 0.33 lub nowszym.\n"_ba;
}

/// `work` on a pool thread, its result delivered to `receiver`'s thread (dropped if it has gone).
template<typename Result>
void offThread(QObject *receiver, std::function<Result()> work, std::function<void(Result)> done)
{
    // (The guard is looked at on the receiver's thread only: posted through the application.)
    QPointer<QObject> guard(receiver);
    QThreadPool::globalInstance()->start([guard, work = std::move(work), done = std::move(done)]() mutable {
        auto result = std::make_shared<Result>(work());
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, result, done = std::move(done)] {
            if (guard)
                done(std::move(*result));
        }, Qt::QueuedConnection);
    });
}

} // namespace

void WorkspaceSync::setUpEncryption(const QString &url, const QString &user, const QString &password,
                                    const QString &folder, const QString &secret)
{
    const QString base = davUrlFor(url, user, folder);
    const bool sameAccount = sameNextcloudAccount(url, user, m_nextcloud.url, m_nextcloud.user);
    const QString appPassword = !password.isEmpty() ? password : sameAccount ? m_nextcloud.password : QString();
    if (base.isEmpty() || appPassword.isEmpty()) {
        Q_EMIT encryptionFailed(tr("Enter the server address, the user name and an app password."));
        return;
    }
    if (secret.trimmed().isEmpty()) {
        Q_EMIT encryptionFailed(tr("Enter the notebook's password."));
        return;
    }
    auto *client = new sync::WebDavClient(base, user.trimmed(), appPassword, transport(), this);
    ++m_encryptionBusy;
    Q_EMIT encryptionBusyChanged();
    client->get(QString(encryption::KeyFilePath), {}, [this, client, folder, secret](const sync::GetResult &got) {
        if (!got.error.isError())
            openEncryptedFolder(client, folder, got.data, secret);
        else if (got.error.is(sync::WebDavErrorCode::NotFound))
            makeIfEmpty(client, folder, secret);
        else
            failEncryption(client, got.error);
    });
}

void WorkspaceSync::makeIfEmpty(sync::WebDavClient *client, const QString &folder, const QString &password)
{
    // No key file: a new encrypted notebook — into a folder with nothing in it.
    client->listFolder({}, [this, client, folder, password](const sync::ListResult &listed) {
        if (listed.error.isError() && !listed.error.is(sync::WebDavErrorCode::NotFound)) {
            failEncryption(client, listed.error);
        } else if (!listed.entries.isEmpty()) {
            finishEncryption(client);
            Q_EMIT encryptionFailed(tr("The server folder “%1” has files already. Choose a new, empty folder for "
                                       "the encrypted notebook.")
                                            .arg(defaultFolder(folder)));
        } else {
            makeEncryptedFolder(client, folder, password);
        }
    });
}

void WorkspaceSync::failEncryption(sync::WebDavClient *client, const sync::WebDavError &error)
{
    finishEncryption(client);
    Q_EMIT encryptionFailed(errorText(sync::errorCodeName(error.code), error.message));
}

void WorkspaceSync::openEncryptedFolder(sync::WebDavClient *client, const QString &folder, const QByteArray &keyFile,
                                        const QString &secret)
{
    offThread<encryption::Unlocked>(
            this,
            [keyFile, secret] {
                // A recovery key is told by its shape; anything else is a password.
                return encryption::parseRecoveryKey(secret) ? encryption::unlockWithRecoveryKey(keyFile, secret)
                                                            : encryption::unlockWithPassword(keyFile, secret);
            },
            [this, client, folder](encryption::Unlocked unlocked) {
                finishEncryption(client);
                switch (unlocked.error) {
                case encryption::UnlockError::None:
                    qCInfo(logging::lcSync).noquote() << u"Nextcloud sync: the encrypted folder was opened on this device"_s;
                    Q_EMIT encryptionKeyMade(folder, unlocked.notebookKey.toByteArray(), QString());
                    Q_EMIT encryptionReady(QString());
                    return;
                case encryption::UnlockError::WrongSecret:
                    Q_EMIT encryptionFailed(tr("The password or the recovery key does not open this notebook."));
                    return;
                case encryption::UnlockError::UnknownVersion:
                    Q_EMIT encryptionFailed(tr("A newer version of Brulion encrypted this folder: update the app."));
                    return;
                case encryption::UnlockError::Malformed:
                    Q_EMIT encryptionFailed(tr("The folder's key file cannot be read."));
                    return;
                }
            });
}

void WorkspaceSync::makeEncryptedFolder(sync::WebDavClient *client, const QString &folder, const QString &password)
{
    if (!encryption::isPasswordAcceptable(password)) {
        finishEncryption(client);
        Q_EMIT encryptionFailed(tr("The notebook's password needs at least %n character(s).", nullptr,
                                   encryption::MinPasswordLength));
        return;
    }
    offThread<std::optional<encryption::NewNotebook>>(
            this, [password] { return encryption::createKeyFile(password); },
            [this, client, folder](std::optional<encryption::NewNotebook> made) {
                if (!made) {
                    finishEncryption(client);
                    Q_EMIT encryptionFailed(tr("The notebook's key could not be made."));
                    return;
                }
                auto notebook = std::make_shared<encryption::NewNotebook>(std::move(*made));
                client->ensureBase([this, client, folder, notebook](const sync::WebDavError &error) {
                    if (error.isError())
                        failEncryption(client, error);
                    else
                        putKeyFile(client, folder, notebook);
                });
            });
}

void WorkspaceSync::putKeyFile(sync::WebDavClient *client, const QString &folder,
                               const std::shared_ptr<sync::encryption::NewNotebook> &notebook)
{
    sync::PutOptions createOnly;
    createOnly.ifNoneMatch = true; // (another device's key file is never replaced)
    client->put(QString(encryption::KeyFilePath), notebook->keyFile, createOnly,
                            [this, client, folder, notebook](const sync::PutResult &put) {
                                if (put.error.isError()) {
                                    failEncryption(client, put.error);
                                    return;
                                }
                                // (Best effort: a note for older versions; the notebook works without it.)
                                client->put(kReadme, readmeText(), {}, [this, client, folder, notebook](const sync::PutResult &) {
                                    finishEncryption(client);
                                    qCInfo(logging::lcSync).noquote()
                                            << u"Nextcloud sync: a new end-to-end encrypted folder was made"_s;
                                    Q_EMIT encryptionKeyMade(folder, notebook->notebookKey.toByteArray(),
                                                             notebook->recoveryKey);
                                    Q_EMIT encryptionReady(notebook->recoveryKey);
                                });
                            });
}

void WorkspaceSync::changeEncryptionPassword(const QString &newPassword)
{
    if (!m_nextcloud.isComplete() || !m_nextcloud.encrypted || m_nextcloud.notebookKey.isEmpty()) {
        Q_EMIT encryptionFailed(tr("This device holds no key of an encrypted folder."));
        return;
    }
    if (!encryption::isPasswordAcceptable(newPassword)) {
        Q_EMIT encryptionFailed(tr("The notebook's password needs at least %n character(s).", nullptr,
                                   encryption::MinPasswordLength));
        return;
    }
    auto *client = new sync::WebDavClient(currentDavUrl(), m_nextcloud.user.trimmed(), m_nextcloud.password,
                                          transport(), this);
    ++m_encryptionBusy;
    Q_EMIT encryptionBusyChanged();
    const crypto::SecureBytes key(m_nextcloud.notebookKey);
    client->get(QString(encryption::KeyFilePath), {}, [this, client, key, newPassword](const sync::GetResult &got) {
        if (got.error.isError()) {
            failEncryption(client, got.error);
            return;
        }
        offThread<std::optional<QByteArray>>(
                this, [keyFile = got.data, key, newPassword] { return encryption::changePassword(keyFile, key, newPassword); },
                [this, client, etag = got.etag](std::optional<QByteArray> changed) {
                    if (!changed) {
                        finishEncryption(client);
                        Q_EMIT encryptionFailed(tr("This device's key does not belong to the folder's key file."));
                        return;
                    }
                    sync::PutOptions sameVersion;
                    sameVersion.ifMatch = etag; // (another device's change in between is never overwritten)
                    client->put(QString(encryption::KeyFilePath), *changed, sameVersion,
                                [this, client](const sync::PutResult &put) {
                                    if (put.error.isError()) {
                                        failEncryption(client, put.error);
                                        return;
                                    }
                                    finishEncryption(client);
                                    qCInfo(logging::lcSync).noquote()
                                            << u"Nextcloud sync: the encrypted folder's password was changed"_s;
                                    Q_EMIT encryptionPasswordChanged();
                                });
                });
    });
}

void WorkspaceSync::finishEncryption(sync::WebDavClient *client)
{
    client->deleteLater();
    --m_encryptionBusy;
    Q_EMIT encryptionBusyChanged();
}

} // namespace brulion::shell
