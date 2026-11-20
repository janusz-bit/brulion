#include "workspaceencryption.h"

#include "appsettings.h"
#include "platform.h" // src/platform
#include "workspace.h"

#include "crypto/secure.h"
#include "encryption/fragments.h"
#include "markdown/encrypted.h"
#include "notebook/indexcachekeeper.h"
#include "util/paths.h"
#include "vault/history.h"
#include "vault/vault.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QtGlobal>

#if QT_CONFIG(thread) && !defined(Q_OS_WASM)
#include <QThreadPool>
#define BRULION_ENCRYPTION_THREADS 1
#endif

using namespace Qt::StringLiterals;

namespace brulion::shell {

using crypto::SecureBytes;
using encryption::Failure;

namespace {

constexpr auto kLockMinutes = "privacy/encryptionLockMinutes"_L1;
constexpr auto kRemember = "privacy/rememberPasswords"_L1;
constexpr int DefaultLockMinutes = 5;

/// A path with its last extension replaced by `extension` ("A/b.md", "mdenc" → "A/b.mdenc").
QString withExtension(const QString &path, QStringView extension)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    const qsizetype dot = path.lastIndexOf(u'.');
    const QString stem = dot > slash + 1 ? path.left(dot) : path;
    return stem + u'.' + extension;
}

} // namespace

WorkspaceEncryption::WorkspaceEncryption(Workspace *workspace) : QObject(workspace), m_workspace(workspace)
{
    m_elapsed.start();
    m_expiry.setSingleShot(true);
    connect(&m_expiry, &QTimer::timeout, this, &WorkspaceEncryption::expire);
    connect(workspace, &Workspace::pathRenamed, this, &WorkspaceEncryption::onPathRenamed);
    connect(workspace, &Workspace::folderRenamed, this, &WorkspaceEncryption::onPathRenamed);
    connect(workspace, &Workspace::openChanged, this, &WorkspaceEncryption::reset);
    m_keyring.setTimeout(qint64(DefaultLockMinutes) * 60 * 1000);
}

WorkspaceEncryption::~WorkspaceEncryption() = default;

void WorkspaceEncryption::setSettings(AppSettings *settings)
{
    m_settings = settings;
    if (m_settings) {
        m_lockMinutes = std::clamp(m_settings->value(kLockMinutes, DefaultLockMinutes).toInt(), 1, 240);
        m_remember = m_settings->value(kRemember, true).toBool();
    }
    m_keyring.setTimeout(m_remember ? timeoutMs() : 0);
    Q_EMIT settingsChanged();
}

void WorkspaceEncryption::setClock(std::function<qint64()> clock)
{
    m_clock = std::move(clock);
}

qint64 WorkspaceEncryption::now() const
{
    return m_clock ? m_clock() : m_elapsed.elapsed();
}

int WorkspaceEncryption::lockMinutes() const
{
    return m_lockMinutes;
}

void WorkspaceEncryption::setLockMinutes(int minutes)
{
    minutes = std::clamp(minutes, 1, 240);
    if (minutes == m_lockMinutes)
        return;
    m_lockMinutes = minutes;
    if (m_settings)
        m_settings->setValue(kLockMinutes, minutes);
    m_keyring.setTimeout(m_remember ? timeoutMs() : 0);
    scheduleExpiry();
    Q_EMIT settingsChanged();
}

bool WorkspaceEncryption::rememberPasswords() const
{
    return m_remember;
}

void WorkspaceEncryption::setRememberPasswords(bool on)
{
    if (on == m_remember)
        return;
    m_remember = on;
    if (m_settings)
        m_settings->setValue(kRemember, on);
    m_keyring.setTimeout(on ? timeoutMs() : 0); // off: the remembered ones are forgotten at once
    updateUnlocked();
    Q_EMIT settingsChanged();
}

bool WorkspaceEncryption::expiredNow()
{
    // (Also checked on every use: a timer of a paused app — Android in the background — fires late.)
    if (!m_unlocked || now() - m_lastUse < timeoutMs())
        return false;
    lockAll();
    return true;
}

qint64 WorkspaceEncryption::timeoutMs() const
{
    return qint64(lockMinutes()) * 60 * 1000;
}

bool WorkspaceEncryption::unlocked() const
{
    return m_unlocked;
}

void WorkspaceEncryption::updateUnlocked()
{
    const bool now = !m_keyring.isEmpty() || !m_pages.isEmpty() || !m_fragments.isEmpty();
    if (now != m_unlocked) {
        m_unlocked = now;
        Q_EMIT unlockedChanged();
    }
    scheduleExpiry();
}

void WorkspaceEncryption::used()
{
    m_lastUse = now();
    m_keyring.touch(m_lastUse);
    updateUnlocked();
}

void WorkspaceEncryption::touch()
{
    if (m_unlocked && !expiredNow())
        used();
}

void WorkspaceEncryption::scheduleExpiry()
{
    if (!m_unlocked) {
        m_expiry.stop();
        return;
    }
    const qint64 left = m_lastUse + timeoutMs() - now();
    m_expiry.start(int(std::clamp<qint64>(left, 1000, 24LL * 3600 * 1000)));
}

void WorkspaceEncryption::expire()
{
    if (!m_unlocked)
        return;
    if (now() - m_lastUse >= timeoutMs())
        lockAll();
    else
        scheduleExpiry();
}

void WorkspaceEncryption::runJob(std::function<void()> work, std::function<void()> done)
{
    ++m_busy;
    if (m_busy == 1)
        Q_EMIT busyChanged();
    QPointer<WorkspaceEncryption> self(this);
    auto finish = [self, done = std::move(done)] {
        if (!self)
            return;
        if (--self->m_busy == 0)
            Q_EMIT self->busyChanged();
        done();
    };
#ifdef BRULION_ENCRYPTION_THREADS
    if (!m_synchronous) {
        QThreadPool::globalInstance()->start([work = std::move(work), finish = std::move(finish), self]() mutable {
            work();
            // (Back on the object's thread; the object may be gone by then: `finish` checks.)
            QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(finish), Qt::QueuedConnection);
        });
        return;
    }
#endif
    // No threads (the browser) or tests: the work one turn later — the spinner shows first — and
    // the result after it, never before the caller has the request's id.
    QTimer::singleShot(0, this, [work = std::move(work), finish = std::move(finish)]() mutable {
        work();
        QTimer::singleShot(0, QCoreApplication::instance(), std::move(finish));
    });
}

QString WorkspaceEncryption::errorFor(Failure failure) const
{
    switch (failure) {
    case Failure::None: return {};
    case Failure::WrongPassword: return tr("Wrong password.");
    case Failure::Malformed: return tr("The encrypted text is damaged and cannot be read.");
    case Failure::Unsupported: return tr("This encrypted text uses a format this version of Brulion does not know.");
    case Failure::TooLarge: return tr("The encrypted text is too large to open.");
    }
    return {};
}

// ---- Fragments ------------------------------------------------------------------------------

QVariantMap WorkspaceEncryption::fragmentInfo(const QString &text) const
{
    const auto span = markdown::encryptedFragmentAt(text, 0, text.size());
    if (!span)
        return {{u"ok"_s, false}};
    return {{u"ok"_s, true},
            {u"hint"_s, span->hint},
            {u"hidden"_s, span->hidden},
            {u"version"_s, int(span->version)},
            {u"old"_s, span->version != markdown::EncryptedVersion::Gamma}};
}

int WorkspaceEncryption::openFragment(const QString &path, const QString &fragment, const QString &password,
                                      bool remember)
{
    const int id = m_nextId++;
    expiredNow();
    const auto span = markdown::encryptedFragmentAt(fragment, 0, fragment.size());
    QList<SecureBytes> tries;
    const bool typed = !password.isEmpty();
    if (typed)
        tries.append(SecureBytes::fromUtf8(password));
    else if (rememberPasswords())
        tries = m_keyring.candidates(path, now(), 2);
    if (!span || tries.isEmpty()) {
        const QString error = !span ? tr("This is not encrypted text Brulion can read.") : QString();
        QTimer::singleShot(0, this, [this, id, error] { Q_EMIT fragmentOpened(id, 0, QString(), error); });
        return id;
    }
    struct Result {
        encryption::Decrypted plain;
        SecureBytes password;
    };
    auto result = std::make_shared<Result>();
    const QString payload = fragment.sliced(span->payloadFrom, span->payloadTo - span->payloadFrom);
    const markdown::EncryptedVersion version = span->version;
    runJob(
            [result, tries, payload, version] {
                for (const SecureBytes &t : tries) {
                    result->plain = encryption::decryptPayload(version, payload, t);
                    if (result->plain.failure != Failure::WrongPassword) {
                        result->password = t;
                        break;
                    }
                }
            },
            [this, id, result, path, fragment, typed, remember] {
                if (!result->plain.ok()) {
                    // Only remembered passwords that did not fit: ask, without an error.
                    const bool silent = !typed && result->plain.failure == Failure::WrongPassword;
                    Q_EMIT fragmentOpened(id, 0, QString(), silent ? QString() : errorFor(result->plain.failure));
                    return;
                }
                const int session = m_nextId++;
                m_fragments.insert(session, {path, fragment, result->password});
                if (remember && rememberPasswords())
                    m_keyring.remember(path, result->password, now());
                used();
                QString text = result->plain.text;
                crypto::wipe(result->plain.text);
                Q_EMIT fragmentOpened(id, session, text, QString());
            });
    return id;
}

int WorkspaceEncryption::sealFragment(int session, const QString &plaintext)
{
    const int id = m_nextId++;
    expiredNow();
    const auto it = m_fragments.constFind(session);
    if (it == m_fragments.cend()) {
        QTimer::singleShot(0, this, [this, id] {
            Q_EMIT textReady(id, QString(), tr("The encrypted text was locked meanwhile. Open it again."));
        });
        return id;
    }
    const auto span = markdown::encryptedFragmentAt(it->fragment, 0, it->fragment.size());
    const SecureBytes password = it->password;
    auto out = std::make_shared<QString>();
    runJob(
            [out, span, plaintext, password] {
                if (span)
                    *out = encryption::reencryptFragment(*span, plaintext, password);
            },
            [this, id, out, session] {
                if (out->isEmpty()) {
                    Q_EMIT textReady(id, QString(), tr("The encrypted text could not be saved."));
                    return;
                }
                if (auto f = m_fragments.find(session); f != m_fragments.end())
                    f->fragment = *out; // a second save starts from this one
                used();
                Q_EMIT textReady(id, *out, QString());
            });
    return id;
}

int WorkspaceEncryption::changeFragmentPassword(int session, const QString &plaintext, const QString &newPassword,
                                                const QString &hint)
{
    const int id = m_nextId++;
    const auto it = m_fragments.constFind(session);
    QString error;
    if (it == m_fragments.cend())
        error = tr("The encrypted text was locked meanwhile. Open it again.");
    else if (newPassword.isEmpty())
        error = tr("Enter a password.");
    else if (!markdown::isValidEncryptedHint(hint))
        error = tr("A hint cannot contain 🔐, 💡 or a line break.");
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [this, id, error] { Q_EMIT textReady(id, QString(), error); });
        return id;
    }
    const auto span = markdown::encryptedFragmentAt(it->fragment, 0, it->fragment.size());
    const bool hidden = span ? span->hidden : true;
    const SecureBytes password = SecureBytes::fromUtf8(newPassword);
    const QString path = it->path;
    auto out = std::make_shared<QString>();
    runJob([out, plaintext, password, hint,
            hidden] { *out = encryption::encryptFragment(plaintext, password, hint, hidden); },
           [this, id, out, session, password, path] {
               if (auto f = m_fragments.find(session); f != m_fragments.end()) {
                   f->fragment = *out;
                   f->password = password;
               }
               if (rememberPasswords())
                   m_keyring.remember(path, password, now());
               used();
               Q_EMIT textReady(id, *out, out->isEmpty() ? tr("The encrypted text could not be saved.") : QString());
           });
    return id;
}

int WorkspaceEncryption::encryptFragment(const QString &path, const QString &plaintext, const QString &password,
                                         const QString &hint, bool hidden, bool remember)
{
    const int id = m_nextId++;
    QString error;
    if (password.isEmpty())
        error = tr("Enter a password.");
    else if (!markdown::isValidEncryptedHint(hint))
        error = tr("A hint cannot contain 🔐, 💡 or a line break.");
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [this, id, error] { Q_EMIT textReady(id, QString(), error); });
        return id;
    }
    const SecureBytes pw = SecureBytes::fromUtf8(password);
    auto out = std::make_shared<QString>();
    runJob([out, plaintext, pw, hint, hidden] { *out = encryption::encryptFragment(plaintext, pw, hint, hidden); },
           [this, id, out, path, pw, remember] {
               if (remember && rememberPasswords())
                   m_keyring.remember(path, pw, now());
               used();
               Q_EMIT textReady(id, *out, out->isEmpty() ? tr("The encrypted text could not be saved.") : QString());
           });
    return id;
}

void WorkspaceEncryption::closeFragment(int session)
{
    if (m_fragments.remove(session))
        updateUnlocked();
}

bool WorkspaceEncryption::validHint(const QString &hint) const
{
    return markdown::isValidEncryptedHint(hint);
}

// ---- Pages ----------------------------------------------------------------------------------

bool WorkspaceEncryption::isEncryptedPath(const QString &path) const
{
    return markdown::isEncryptedNotePath(path);
}

QString WorkspaceEncryption::pageName(const QString &path) const
{
    const QString name = paths::pageTitle(path);
    if (!isEncryptedPath(name))
        return name;
    return name.left(name.lastIndexOf(u'.'));
}

QVariantMap WorkspaceEncryption::pageInfo(const QString &path) const
{
    bool exists = false;
    const QString file = isEncryptedPath(path) ? m_workspace->read(path, &exists) : QString();
    Failure failure = Failure::None;
    const auto note = exists ? encryption::readNoteFile(file, &failure) : std::nullopt;
    if (!note)
        return {{u"ok"_s, false},
                {u"exists"_s, exists},
                {u"error"_s, exists ? errorFor(failure) : tr("The page does not exist.")}};
    return {{u"ok"_s, true},         {u"exists"_s, true},           {u"blank"_s, note->blank},
            {u"hint"_s, note->hint}, {u"version"_s, note->version}, {u"error"_s, QString()}};
}

int WorkspaceEncryption::unlockPage(const QString &path, const QString &password, bool remember)
{
    const int id = m_nextId++;
    expiredNow();
    bool exists = false;
    const QString file = isEncryptedPath(path) ? m_workspace->read(path, &exists) : QString();
    const bool typed = !password.isEmpty();
    QList<SecureBytes> tries;
    if (typed)
        tries.append(SecureBytes::fromUtf8(password));
    else if (rememberPasswords() && !file.isEmpty())
        tries = m_keyring.candidates(path, now(), 2);
    if (!exists || tries.isEmpty()) {
        const QString error = !exists ? tr("The page does not exist.") : QString();
        QTimer::singleShot(0, this, [this, id, error] { Q_EMIT pageUnlocked(id, false, error); });
        return id;
    }
    struct Result {
        encryption::OpenedNote note;
        SecureBytes password;
    };
    auto result = std::make_shared<Result>();
    runJob(
            [result, tries, file] {
                for (const SecureBytes &t : tries) {
                    result->note = encryption::openNote(file, t);
                    if (result->note.failure != Failure::WrongPassword) {
                        result->password = t;
                        break;
                    }
                }
            },
            [this, id, result, path, file, typed, remember] {
                if (!result->note.ok()) {
                    const bool silent = !typed && result->note.failure == Failure::WrongPassword;
                    Q_EMIT pageUnlocked(id, false, silent ? QString() : errorFor(result->note.failure));
                    return;
                }
                auto session = std::make_shared<PageSession>();
                session->key = result->note.key;
                session->password = result->password;
                session->hint = result->note.hint;
                session->lastFile = file;
                session->lastText = result->note.text;
                crypto::wipe(result->note.text);
                m_pages.insert(path, session);
                if (remember && rememberPasswords())
                    m_keyring.remember(path, result->password, now());
                used();
                Q_EMIT pageUnlocked(id, true, QString());
            });
    return id;
}

bool WorkspaceEncryption::isPageUnlocked(const QString &path) const
{
    return m_pages.contains(path);
}

WorkspaceEncryption::PageSession *WorkspaceEncryption::pageSession(const QString &path)
{
    if (expiredNow())
        return nullptr;
    const auto it = m_pages.constFind(path);
    if (it == m_pages.cend())
        return nullptr;
    used();
    return it->get();
}

void WorkspaceEncryption::lockPage(const QString &path)
{
    if (!m_pages.contains(path))
        return;
    Q_EMIT pageLocking(path);
    m_workspace->waitForWrites();
    m_pages.remove(path);
    Q_EMIT pageLocked(path, QString());
    updateUnlocked();
}

void WorkspaceEncryption::pageLockedOut(const QString &path)
{
    if (!m_pages.remove(path))
        return;
    Q_EMIT pageLocked(path, tr("This encrypted page was changed elsewhere under another password. Unlock it again "
                               "with that password."));
    updateUnlocked();
}

QString WorkspaceEncryption::encryptedPathFor(const QString &path) const
{
    if (!paths::isMarkdown(path))
        return {};
    const QString out = withExtension(path, markdown::EncryptedNoteExtension);
    const Vault *vault = m_workspace->vault();
    return vault && !vault->exists(vault->existingCase(out)) ? out : QString();
}

QString WorkspaceEncryption::decryptedPathFor(const QString &path) const
{
    if (!isEncryptedPath(path))
        return {};
    const QString out = withExtension(path, u"md");
    const Vault *vault = m_workspace->vault();
    return vault && !vault->exists(vault->existingCase(out)) ? out : QString();
}

int WorkspaceEncryption::encryptPage(const QString &path, const QString &password, const QString &hint,
                                     bool deleteHistory)
{
    const int id = m_nextId++;
    const QString target = encryptedPathFor(path);
    QString error;
    if (!m_workspace->vault() || !m_workspace->exists(path))
        error = tr("The page does not exist.");
    else if (target.isEmpty())
        error = paths::isMarkdown(path) ? tr("“%1” exists already.").arg(paths::basename(withExtension(path, u"mdenc")))
                                        : tr("Only a Markdown page can be encrypted.");
    else if (password.isEmpty())
        error = tr("Enter a password.");
    else if (!markdown::isValidEncryptedHint(hint))
        error = tr("A hint cannot contain 🔐, 💡 or a line break.");
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [this, id, error] { Q_EMIT pageConverted(id, QString(), error); });
        return id;
    }
    // Every editor writes what it holds first: the text encrypted is the page's latest.
    Q_EMIT m_workspace->saveOpenEditorsRequested();
    m_workspace->waitForWrites();
    bool ok = false;
    const QString text = m_workspace->read(path, &ok);
    struct Result {
        encryption::NoteKey key;
        QString file;
    };
    auto result = std::make_shared<Result>();
    const SecureBytes pw = SecureBytes::fromUtf8(password);
    runJob(
            [result, text, pw, hint] {
                result->key = encryption::deriveNoteKey(pw);
                result->file = encryption::writeNoteFile(text, result->key, hint);
            },
            [this, id, result, path, text, pw, hint, deleteHistory] {
                // Over the Markdown in place (only while it still holds what was encrypted), then
                // renamed: the plaintext never gets the encrypted note's name, and links follow.
                const auto written = m_workspace->writeIfUnchanged(path, result->file, text);
                if (written != EditorHost::WriteResult::Written) {
                    Q_EMIT pageConverted(id, QString(),
                                         written == EditorHost::WriteResult::Changed
                                                 ? tr("The page changed meanwhile. Try again.")
                                                 : tr("The encrypted page could not be written."));
                    return;
                }
                const QString target = encryptedPathFor(path);
                const QString moved = target.isEmpty() ? QString() : m_workspace->relocate(path, target);
                if (moved.isEmpty()) {
                    // Back to the Markdown it was (the rename failed: the error was told).
                    m_workspace->writeIfUnchanged(path, text, result->file);
                    Q_EMIT pageConverted(id, QString(), tr("The page could not be renamed to an encrypted page."));
                    return;
                }
                // The versions in History are the unencrypted page: deleted when asked.
                if (deleteHistory && m_workspace->vault() && m_workspace->vault()->history())
                    m_workspace->vault()->history()->remove(moved);
                // The index cache on the device held the page's text: written again without it.
                if (m_workspace->m_cacheKeeper)
                    m_workspace->m_cacheKeeper->saveIfDirty();
                auto session = std::make_shared<PageSession>();
                session->key = result->key;
                session->password = pw;
                session->hint = hint;
                session->lastFile = result->file;
                session->lastText = text;
                m_pages.insert(moved, session);
                if (rememberPasswords())
                    m_keyring.remember(moved, pw, now());
                used();
                Q_EMIT pageConverted(id, moved, QString());
            });
    return id;
}

int WorkspaceEncryption::decryptPage(const QString &path)
{
    const int id = m_nextId++;
    const auto it = m_pages.constFind(path);
    const QString target = decryptedPathFor(path);
    QString error;
    if (it == m_pages.cend())
        error = tr("Unlock the page first.");
    else if (target.isEmpty())
        error = tr("“%1” exists already.").arg(paths::basename(withExtension(path, u"md")));
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [this, id, error] { Q_EMIT pageConverted(id, QString(), error); });
        return id;
    }
    Q_EMIT m_workspace->saveOpenEditorsRequested();
    m_workspace->waitForWrites();
    const std::shared_ptr<PageSession> session = *it;
    // The file as it is now, opened with the page's key (a change made elsewhere is taken in).
    const QString file = m_workspace->read(path);
    encryption::OpenedNote note = encryption::openNoteWithKey(file, session->key);
    if (!note.ok())
        note = encryption::openNote(file, session->password);
    if (!note.ok()) {
        QTimer::singleShot(0, this, [this, id, f = note.failure] { Q_EMIT pageConverted(id, QString(), errorFor(f)); });
        return id;
    }
    // Renamed first, then written: a failed write leaves the page encrypted under its new name.
    Q_EMIT pageLocking(path);
    m_workspace->waitForWrites();
    m_pages.remove(path);
    const QString moved = m_workspace->relocate(path, target);
    if (moved.isEmpty()) {
        m_pages.insert(path, session);
        QTimer::singleShot(0, this, [this, id] {
            Q_EMIT pageConverted(id, QString(), tr("The page could not be renamed to a Markdown page."));
        });
        return id;
    }
    const auto written = m_workspace->writeIfUnchanged(moved, note.text, file);
    crypto::wipe(note.text);
    Q_EMIT pageLocked(path, QString());
    updateUnlocked();
    QTimer::singleShot(0, this, [this, id, moved, written] {
        Q_EMIT pageConverted(id, written == EditorHost::WriteResult::Written ? moved : QString(),
                             written == EditorHost::WriteResult::Written
                                     ? QString()
                                     : tr("The page was renamed but its text could not be written: it is still "
                                          "encrypted."));
    });
    return id;
}

int WorkspaceEncryption::changePagePassword(const QString &path, const QString &newPassword, const QString &hint,
                                            bool deleteHistory)
{
    const int id = m_nextId++;
    QString error;
    if (!m_pages.contains(path))
        error = tr("Unlock the page first.");
    else if (newPassword.isEmpty())
        error = tr("Enter a password.");
    else if (!markdown::isValidEncryptedHint(hint))
        error = tr("A hint cannot contain 🔐, 💡 or a line break.");
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [this, id, error] { Q_EMIT pageConverted(id, QString(), error); });
        return id;
    }
    Q_EMIT m_workspace->saveOpenEditorsRequested();
    m_workspace->waitForWrites();
    const SecureBytes pw = SecureBytes::fromUtf8(newPassword);
    auto key = std::make_shared<encryption::NoteKey>();
    runJob([key, pw] { *key = encryption::deriveNoteKey(pw); },
           [this, id, key, pw, path, hint, deleteHistory] {
               const auto it = m_pages.constFind(path);
               if (it == m_pages.cend()) {
                   Q_EMIT pageConverted(id, QString(), tr("The page was locked meanwhile. Unlock it again."));
                   return;
               }
               PageSession &s = **it;
               const QString file = encryption::writeNoteFile(s.lastText, *key, hint);
               const auto written = m_workspace->writeIfUnchanged(path, file, s.lastFile);
               if (written != EditorHost::WriteResult::Written) {
                   Q_EMIT pageConverted(id, QString(), tr("The page changed meanwhile. Try again."));
                   return;
               }
               s.key = *key;
               s.password = pw;
               s.hint = hint;
               s.lastFile = file;
               // The older versions open with the old password: deleted when asked.
               if (deleteHistory && m_workspace->vault() && m_workspace->vault()->history())
                   m_workspace->vault()->history()->remove(path);
               m_keyring.forget(path);
               if (rememberPasswords())
                   m_keyring.remember(path, pw, now());
               used();
               Q_EMIT pageConverted(id, path, QString());
           });
    return id;
}

int WorkspaceEncryption::createPage(const QString &title, const QString &folder, const QString &password,
                                    const QString &hint)
{
    const int id = m_nextId++;
    Vault *vault = m_workspace->vault();
    QString error;
    const QString name = paths::sanitizeFileName(title.trimmed());
    if (!vault)
        error = tr("No notebook is open.");
    else if (name.isEmpty())
        error = tr("Enter a name.");
    else if (password.isEmpty())
        error = tr("Enter a password.");
    else if (!markdown::isValidEncryptedHint(hint))
        error = tr("A hint cannot contain 🔐, 💡 or a line break.");
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [this, id, error] { Q_EMIT pageConverted(id, QString(), error); });
        return id;
    }
    const QString base = paths::normalize(folder.isEmpty() ? vault->config().newPagesFolder : folder);
    const SecureBytes pw = SecureBytes::fromUtf8(password);
    struct Result {
        encryption::NoteKey key;
        QString file;
    };
    auto result = std::make_shared<Result>();
    runJob(
            [result, pw, hint] {
                result->key = encryption::deriveNoteKey(pw);
                result->file = encryption::writeNoteFile(QString(), result->key, hint);
            },
            [this, id, result, base, name, pw, hint] {
                Vault *v = m_workspace->vault();
                if (!v) {
                    Q_EMIT pageConverted(id, QString(), tr("No notebook is open."));
                    return;
                }
                const QString wanted = paths::join(base, name + u'.' + markdown::EncryptedNoteExtension);
                const QString path =
                        paths::uniquePath(wanted, [v](const VaultPath &p) { return v->exists(v->existingCase(p)); });
                const VaultPath created = v->createBinary(path, result->file.toUtf8(), ChangeSource::User);
                if (created.isEmpty()) {
                    Q_EMIT pageConverted(id, QString(), tr("Could not create the page: %1").arg(v->errorString()));
                    return;
                }
                auto session = std::make_shared<PageSession>();
                session->key = result->key;
                session->password = pw;
                session->hint = hint;
                session->lastFile = result->file;
                m_pages.insert(created, session);
                if (rememberPasswords())
                    m_keyring.remember(created, pw, now());
                used();
                Q_EMIT pageConverted(id, created, QString());
            });
    return id;
}

// ---- The session ----------------------------------------------------------------------------

void WorkspaceEncryption::lockAll()
{
    const QStringList pages = m_pages.keys();
    for (const QString &path : pages)
        Q_EMIT pageLocking(path);
    if (!pages.isEmpty())
        m_workspace->waitForWrites();
    m_pages.clear();
    m_keyring.clear();
    m_fragments.clear();
    for (const QString &path : pages)
        Q_EMIT pageLocked(path, QString());
    Q_EMIT allLocked();
    updateUnlocked();
}

void WorkspaceEncryption::reset()
{
    m_pages.clear();
    m_keyring.clear();
    m_fragments.clear();
    Q_EMIT allLocked();
    updateUnlocked();
}

void WorkspaceEncryption::onPathRenamed(const QString &oldPath, const QString &newPath)
{
    m_keyring.renamed(oldPath, newPath);
    const QString folder = oldPath + u'/';
    QHash<QString, std::shared_ptr<PageSession>> moved;
    for (auto it = m_pages.begin(); it != m_pages.end();) {
        if (it.key() == oldPath || it.key().startsWith(folder)) {
            const QString to = it.key() == oldPath ? newPath : newPath + u'/' + it.key().sliced(folder.size());
            // Still an encrypted page under its new name: it stays unlocked.
            if (isEncryptedPath(to))
                moved.insert(to, it.value());
            it = m_pages.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = moved.cbegin(); it != moved.cend(); ++it)
        m_pages.insert(it.key(), it.value());
    updateUnlocked();
}

bool WorkspaceEncryption::copySecret(const QString &text)
{
    // Android 13+: the clipboard's preview and the keyboard's suggestions leave it out.
    if (platform::copySensitiveText(text))
        return true;
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    auto *data = new QMimeData;
    data->setText(text);
    // KDE's Klipper and other X11/Wayland clipboard managers: a password manager's secret.
    data->setData(u"x-kde-passwordManagerHint"_s, "secret");
    // Windows: neither the clipboard history (Win+V) nor the cloud clipboard keeps it.
    const QByteArray no(4, '\0');
    data->setData(u"application/x-qt-windows-mime;value=\"ExcludeClipboardContentFromMonitorProcessing\""_s, no);
    data->setData(u"application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\""_s, no);
    data->setData(u"application/x-qt-windows-mime;value=\"CanUploadToCloudClipboard\""_s, no);
    clipboard->setMimeData(data);
    return true;
}

} // namespace brulion::shell
