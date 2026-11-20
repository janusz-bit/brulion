// WorkspaceEncryption — encrypted notes of the open notebook (QML: Workspace.encryption; docs/UI.md
// "Encrypted notes", docs/ARCHITECTURE.md "Security"), over the core modules encryption and crypto:
//
//  * fragments of a page (`%%🔐γ … 🔐%%`, Meld Encrypt's markers): openFragment() decrypts one for
//    the editor's dialog (the password typed, or the ones remembered), sealFragment() encrypts the
//    edited text again under the same password, hint and look, encryptFragment() makes a new one
//    ("Encrypt selection", "Insert encrypted text"), changeFragmentPassword();
//  * whole encrypted pages (`.mdenc`): unlockPage() keeps the page's key while it is open —
//    EncryptedPageHost (encryptedpagehost.h) reads and writes the page through it, so the block
//    editor shows the Markdown and every save is encrypted —, encryptPage() ("Encrypt page": the
//    Markdown page becomes `<name>.mdenc`, every link following it, its version history — the
//    unencrypted versions — deleted when asked), decryptPage() ("Decrypt permanently"),
//    changePagePassword(), createPage() ("New encrypted page");
//  * the session: the passwords typed (encryption::Keyring, wiped memory) are remembered for the
//    page they opened and tried — at most two — on the next fragment or page of the notebook; every
//    password and every open page locks after `lockMinutes` without use (an unlock, a save, touch()),
//    on lockAll() (the app lock, closing the notebook, "Lock encrypted notes") and never reaches a
//    file, a log or the index.
//
// The key derivations (PBKDF2-SHA512, up to two million rounds) run on the global thread pool, one
// turn of the event loop later where there are no threads (the browser); every result comes as a
// signal with the request's id. The text a fragment or page holds reaches QML only through
// fragmentOpened() and the page's editor.
#pragma once

#include "encryption/keyring.h"
#include "encryption/meld.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace brulion::shell {

class AppSettings;
class Workspace;

class WorkspaceEncryption : public QObject {
    Q_OBJECT

    /// Minutes an unused password and an open encrypted page stay unlocked (1 … 240; AppSettings
    /// "privacy/encryptionLockMinutes", 5 by default; a device setting).
    Q_PROPERTY(int lockMinutes READ lockMinutes WRITE setLockMinutes NOTIFY settingsChanged FINAL)
    /// Passwords typed are tried on the notebook's other fragments and pages (AppSettings
    /// "privacy/rememberPasswords", on by default); off: every fragment and page asks.
    Q_PROPERTY(bool rememberPasswords READ rememberPasswords WRITE setRememberPasswords NOTIFY settingsChanged FINAL)
    /// Something is unlocked: a remembered password, an open page or fragment.
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY unlockedChanged FINAL)
    /// A key derivation is running.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)

public:
    explicit WorkspaceEncryption(Workspace *workspace);
    ~WorkspaceEncryption() override;

    void setSettings(AppSettings *settings);
    /// Run every job at once on the calling thread (tests).
    void setSynchronous(bool synchronous) { m_synchronous = synchronous; }
    /// The monotonic clock (tests move it; ms).
    void setClock(std::function<qint64()> clock);

    int lockMinutes() const;
    void setLockMinutes(int minutes);
    bool rememberPasswords() const;
    void setRememberPasswords(bool on);
    bool unlocked() const;
    bool busy() const { return m_busy > 0; }

    // ---- Fragments -------------------------------------------------------------------------------
    /// Is `text` exactly one encrypted fragment (markdown/encrypted.h)? { ok, hint, hidden, version
    /// (0 … 3), old (a version Brulion reads only: the next save makes it v3) }.
    Q_INVOKABLE QVariantMap fragmentInfo(const QString &text) const;
    /// Decrypt the fragment `fragment` (its text as written) of the page `path` with `password`, or
    /// with "" the passwords remembered (nothing asked when none fits). Returns the request's id;
    /// fragmentOpened(id, session, text, error) follows: session > 0 — open (closeFragment() it);
    /// 0 — not opened, error "" when only the remembered passwords did not fit (ask then).
    /// `remember`: keep the password for the notebook's other fragments and pages.
    Q_INVOKABLE int openFragment(const QString &path, const QString &fragment, const QString &password, bool remember);
    /// The open fragment's text `plaintext` encrypted again (its password, hint and look; v3):
    /// textReady(id, fragment, error) with the new fragment's text.
    Q_INVOKABLE int sealFragment(int session, const QString &plaintext);
    /// The open fragment's text `plaintext` under a new password and hint: textReady(id, fragment,
    /// error); the session keeps the new password.
    Q_INVOKABLE int changeFragmentPassword(int session, const QString &plaintext, const QString &newPassword,
                                           const QString &hint);
    /// A new fragment of `plaintext` under `password` (hidden: an Obsidian comment `%%…%%`, else a
    /// visible `🔐γ … 🔐`), for the page `path`: textReady(id, fragment, error).
    Q_INVOKABLE int encryptFragment(const QString &path, const QString &plaintext, const QString &password,
                                    const QString &hint, bool hidden, bool remember);
    /// The dialog of fragment `session` closed: its password forgotten (the keyring keeps its own).
    Q_INVOKABLE void closeFragment(int session);
    /// Is `hint` allowed in a fragment (no 🔐, 💡 or line break)?
    Q_INVOKABLE bool validHint(const QString &hint) const;

    // ---- Pages -----------------------------------------------------------------------------------
    /// A `.mdenc` / `.encrypted` path.
    Q_INVOKABLE bool isEncryptedPath(const QString &path) const;
    /// The encrypted page's file read (not decrypted): { ok, blank (no password chosen yet), hint,
    /// version, error (translated: a damaged or unknown file) }.
    Q_INVOKABLE QVariantMap pageInfo(const QString &path) const;
    /// Unlock the page `path` with `password` (or "" the remembered ones): pageUnlocked(id, ok,
    /// error) follows. A blank page takes `password` as its new one. While unlocked its editor
    /// reads and writes it through EncryptedPageHost.
    Q_INVOKABLE int unlockPage(const QString &path, const QString &password, bool remember);
    Q_INVOKABLE bool isPageUnlocked(const QString &path) const;
    /// Lock the page now (its editors save first: pageLocking, then pageLocked).
    Q_INVOKABLE void lockPage(const QString &path);
    /// "Encrypt page": the Markdown page `path` becomes `<name>.mdenc` under `password` — written
    /// over in place, then renamed with every link and tag following, unlocked; with
    /// `deleteHistory` its version history (the unencrypted versions) is deleted. pageConverted(id,
    /// newPath, error).
    Q_INVOKABLE int encryptPage(const QString &path, const QString &password, const QString &hint, bool deleteHistory);
    /// "Decrypt permanently": the unlocked page `path` becomes `<name>.md`: pageConverted(id, newPath, error).
    Q_INVOKABLE int decryptPage(const QString &path);
    /// "Change password" of the unlocked page `path`: pageConverted(id, path, error). With
    /// `deleteHistory` its older versions (still readable with the old password) are deleted.
    Q_INVOKABLE int changePagePassword(const QString &path, const QString &newPassword, const QString &hint,
                                       bool deleteHistory);
    /// "New encrypted page": `<title>.mdenc` in `folder` ("" the new pages' folder), unlocked:
    /// pageConverted(id, path, error).
    Q_INVOKABLE int createPage(const QString &title, const QString &folder, const QString &password,
                               const QString &hint);
    /// The path "Encrypt page" gives `path` ("Sekret.md" → "Sekret.mdenc"), "" when that name is taken.
    Q_INVOKABLE QString encryptedPathFor(const QString &path) const;
    /// The path "Decrypt permanently" gives ("Sekret.mdenc" → "Sekret.md"), "" when that name is taken.
    Q_INVOKABLE QString decryptedPathFor(const QString &path) const;

    // ---- The session -----------------------------------------------------------------------------
    /// Lock every page, forget every password (the app lock, closing the notebook, the command).
    Q_INVOKABLE void lockAll();
    /// Something was done in unlocked content: the timeout starts again.
    Q_INVOKABLE void touch();
    /// Copy decrypted text to the clipboard marked as sensitive (KDE's and Windows' clipboard
    /// histories and Android 13's preview leave it out). False when there is no clipboard.
    Q_INVOKABLE bool copySecret(const QString &text);

    /// An unlocked page as EncryptedPageHost uses it (nullptr: locked). The last file text read or
    /// written and the Markdown it holds: what the editor's compare-and-swap compares.
    struct PageSession {
        encryption::NoteKey key;
        crypto::SecureBytes password;
        QString hint;
        QString lastFile;
        QString lastText;
        bool foreign = false; ///< changed elsewhere under another password: never written again
    };
    PageSession *pageSession(const QString &path);
    /// The page `path` could not be read with its key and password any more (another password set
    /// elsewhere): locked, the editor told (pageLocked with a reason).
    void pageLockedOut(const QString &path);
    /// The notebook closed or opened: everything locked.
    void reset();

Q_SIGNALS:
    void settingsChanged();
    void unlockedChanged();
    void busyChanged();
    void fragmentOpened(int id, int session, const QString &text, const QString &error);
    void textReady(int id, const QString &text, const QString &error);
    void pageUnlocked(int id, bool ok, const QString &error);
    void pageConverted(int id, const QString &path, const QString &error);
    /// The page is about to lock: its editors save now (the key still works).
    void pageLocking(const QString &path);
    /// The page locked (`reason` translated, "" for the timeout or lockAll()).
    void pageLocked(const QString &path, const QString &reason);
    /// Everything locked (the timeout, lockAll()): open fragment dialogs close and forget their text.
    void allLocked();

private:
    struct FragmentSession {
        QString path;
        QString fragment;
        crypto::SecureBytes password;
    };
    qint64 now() const;
    qint64 timeoutMs() const;
    /// `work` on a worker thread, then `done` here (unless this object went).
    void runJob(std::function<void()> work, std::function<void()> done);
    void used();
    /// Past the lock time without use: everything locked now (true).
    bool expiredNow();
    void scheduleExpiry();
    void expire();
    void updateUnlocked();
    void onPathRenamed(const QString &oldPath, const QString &newPath);
    QString errorFor(encryption::Failure failure) const;

    Workspace *m_workspace;
    QPointer<AppSettings> m_settings;
    encryption::Keyring m_keyring;
    QHash<int, FragmentSession> m_fragments;
    QHash<QString, std::shared_ptr<PageSession>> m_pages;
    int m_nextId = 1;
    int m_busy = 0;
    bool m_synchronous = false;
    bool m_unlocked = false;
    int m_lockMinutes = 5;
    bool m_remember = true;
    qint64 m_lastUse = 0;
    QElapsedTimer m_elapsed;
    std::function<qint64()> m_clock;
    QTimer m_expiry;
};

} // namespace brulion::shell
