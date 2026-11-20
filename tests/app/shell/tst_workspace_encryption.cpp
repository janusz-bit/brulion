// Encrypted notes on the real Workspace (docs/UI.md "Encrypted notes"): Workspace.encryption and
// PageView's EncryptedPageHost with the block editor — a note Meld Encrypt wrote opened, edited and
// saved encrypted; wrong passwords; the page locked by the timeout, by lockAll() and by a password
// changed elsewhere; "Encrypt page", "Decrypt permanently", "Change password", "New encrypted page";
// fragments opened, saved again, remembered; and that the plaintext never reaches the disk — the
// notebook's files, its version history, the index cache and the rest of the device state are
// searched byte for byte (UTF-8 and UTF-16) after each flow.
#include "encryptedpagehost.h"
#include "pagecontroller.h"
#include "workspace.h"
#include "workspaceencryption.h"

#include "encryption/fragments.h"
#include "encryption/meld.h"
#include "index/vaultindex.h"
#include "markdown/encrypted.h"
#include "vault/history.h"
#include "vault/vault.h"

#include "../../core/encryption/meldfixtures.h"

#include <QDirIterator>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::editor::PageController;
using brulion::shell::EncryptedPageHost;
using brulion::shell::Workspace;
using brulion::shell::WorkspaceEncryption;
namespace crypto = brulion::crypto;
namespace encryption = brulion::encryption;

namespace {

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QByteArray readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/// The files under `dir` holding `secret` (its UTF-8 or UTF-16), as "path" for the failure message.
QStringList filesHolding(const QString &dir, const QString &secret)
{
    const QByteArray utf8 = secret.toUtf8();
    const QByteArray utf16(reinterpret_cast<const char *>(secret.utf16()), secret.size() * 2);
    QStringList out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QByteArray bytes = readRaw(file);
        if (bytes.contains(utf8) || bytes.contains(utf16))
            out.append(file);
    }
    return out;
}

/// The secret is nowhere under the notebook or the device state.
bool nowhere(const QString &root, const QString &state, const QString &secret, QStringList *found)
{
    *found = filesHolding(root, secret) + filesHolding(state, secret);
    return found->isEmpty();
}

} // namespace

class TestWorkspaceEncryption : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    int m_n = 0;

    struct Opened {
        std::unique_ptr<Workspace> ws;
        QString root;
        QString state;
    };
    Opened openNotebook(const QList<std::pair<QString, QByteArray>> &files)
    {
        Opened o;
        o.root = m_tmp.filePath(u"notes%1"_s.arg(++m_n));
        o.state = o.root + u"-state"_s;
        for (const auto &[path, bytes] : files)
            writeRaw(o.root + u'/' + path, bytes);
        o.ws = std::make_unique<Workspace>();
        o.ws->sync()->setSyncthingDiscovery(false);
        o.ws->encryption()->setSynchronous(true);
        if (!o.ws->open(o.root, o.state))
            qWarning("cannot open %s", qPrintable(o.root));
        o.ws->finishIndexing();
        return o;
    }

    /// Unlock `path` with `password` and wait for the answer.
    static bool unlock(WorkspaceEncryption *enc, const QString &path, const QString &password, QString *error = nullptr)
    {
        QSignalSpy answered(enc, &WorkspaceEncryption::pageUnlocked);
        enc->unlockPage(path, password, true);
        if (!answered.wait(30000))
            return false;
        if (error)
            *error = answered.first().at(2).toString();
        return answered.first().at(1).toBool();
    }

    static QString converted(WorkspaceEncryption *enc, int id, QSignalSpy &spy, QString *error)
    {
        if (spy.isEmpty() && !spy.wait(30000))
            return {};
        for (const QList<QVariant> &args : std::as_const(spy)) {
            if (args.at(0).toInt() == id) {
                *error = args.at(2).toString();
                return args.at(1).toString();
            }
        }
        Q_UNUSED(enc);
        return {};
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void aMeldNoteOpensEditsAndSavesEncrypted()
    {
        Opened o = openNotebook(
                {{u"Sejf.mdenc"_s, meldfixtures::FileV3.toString().toUtf8()}, {u"Inna.md"_s, "- zwykła strona\n"}});
        WorkspaceEncryption *enc = o.ws->encryption();
        QVERIFY(enc->isEncryptedPath(u"Sejf.mdenc"_s));
        const QVariantMap info = enc->pageInfo(u"Sejf.mdenc"_s);
        QVERIFY(info.value(u"ok"_s).toBool());
        QCOMPARE(info.value(u"hint"_s).toString(), u"Podpowiedź: zwierzę"_s);

        EncryptedPageHost host;
        host.setInner(o.ws.get());
        host.setEncryption(enc);
        // Locked: the editor sees no page and cannot write one.
        bool ok = true;
        QVERIFY(host.read(u"Sejf.mdenc"_s, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(!host.write(u"Sejf.mdenc"_s, u"- nadpisane\n"_s));

        QString error;
        QVERIFY(!unlock(enc, u"Sejf.mdenc"_s, u"złe hasło"_s, &error));
        QCOMPARE(error, u"Wrong password."_s);
        QVERIFY(unlock(enc, u"Sejf.mdenc"_s, meldfixtures::Password.toString()));
        QVERIFY(enc->isPageUnlocked(u"Sejf.mdenc"_s));

        auto editor = std::make_unique<PageController>();
        editor->setSaveDelay(100000);
        editor->setHost(&host);
        editor->setPath(u"Sejf.mdenc"_s);
        QCOMPARE(editor->text(), meldfixtures::Text.toString());
        // Other pages pass through unchanged.
        QCOMPARE(host.read(u"Inna.md"_s), u"- zwykła strona\n"_s);

        const QString secret = u"Szyfr do sejfu: 7319 żółty"_s;
        editor->setEditText(0, secret, int(secret.size()));
        QVERIFY(editor->dirty());
        editor->save();
        o.ws->waitForWrites();
        QTRY_VERIFY(!editor->dirty());

        // On disk: v3 JSON as the plugin writes it, the same salt, readable with the password.
        const QString file = QString::fromUtf8(readRaw(o.root + u"/Sejf.mdenc"_s));
        const auto read = encryption::readNoteFile(file);
        QVERIFY(read);
        QCOMPARE(read->version, u"3.0"_s);
        QCOMPARE(read->hint, u"Podpowiedź: zwierzę"_s);
        QVERIFY(file.contains(u"\"salt\": \"40ShtIjS+ZA7VMyYOnQgdw==\""_s));
        const encryption::Decrypted back =
                encryption::decryptNote(file, crypto::SecureBytes::fromUtf8(meldfixtures::Password));
        QVERIFY(back.ok());
        QVERIFY(back.text.contains(secret));
        // Nowhere else: not the notebook, not History, not the index or its cache on the device.
        QStringList found;
        QVERIFY2(nowhere(o.root, o.state, u"7319 żółty"_s, &found), qPrintable(found.join(u", "_s)));
        QVERIFY(o.ws->index()->content(u"Sejf.mdenc"_s).isEmpty());
        QVERIFY(o.ws->index()->search(u"żółty"_s, 10).isEmpty());

        // A plaintext write from an editor that does not decrypt (the journal stream, a view) is refused.
        QSignalSpy errors(o.ws.get(), &Workspace::errorOccurred);
        QVERIFY(!o.ws->writeFile(u"Sejf.mdenc"_s, u"- jawny tekst\n"_s));
        QCOMPARE(errors.size(), 1);
        QVERIFY(encryption::readNoteFile(QString::fromUtf8(readRaw(o.root + u"/Sejf.mdenc"_s))));

        // Locked by lockAll(): the edits typed meanwhile are saved first, then nothing reads.
        const QString later = u"Dopisane przed blokadą"_s;
        editor->setEditText(0, later, int(later.size()));
        QSignalSpy locked(enc, &WorkspaceEncryption::pageLocked);
        enc->lockAll();
        QCOMPARE(locked.size(), 1);
        QVERIFY(!enc->isPageUnlocked(u"Sejf.mdenc"_s));
        QTRY_VERIFY(!editor->dirty());
        const encryption::Decrypted afterLock =
                encryption::decryptNote(QString::fromUtf8(readRaw(o.root + u"/Sejf.mdenc"_s)),
                                        crypto::SecureBytes::fromUtf8(meldfixtures::Password));
        QVERIFY(afterLock.text.contains(later));
        QVERIFY(host.read(u"Sejf.mdenc"_s, &ok).isEmpty() && !ok);
        editor.reset();
        QVERIFY2(nowhere(o.root, o.state, later, &found), qPrintable(found.join(u", "_s)));
    }

    void rememberedPasswordAndTimeout()
    {
        Opened o = openNotebook({{u"A.mdenc"_s, meldfixtures::FileV3.toString().toUtf8()},
                                 {u"B.mdenc"_s, meldfixtures::FileV2.toString().toUtf8()}});
        WorkspaceEncryption *enc = o.ws->encryption();
        qint64 clock = 1000;
        enc->setClock([&clock] { return clock; });
        QVERIFY(unlock(enc, u"A.mdenc"_s, meldfixtures::Password.toString()));
        // The next page tries the remembered password: no question.
        QVERIFY(unlock(enc, u"B.mdenc"_s, QString()));
        QVERIFY(enc->unlocked());
        // Without use for the lock time: everything locks.
        clock += qint64(enc->lockMinutes()) * 60 * 1000 + 1;
        QSignalSpy locked(enc, &WorkspaceEncryption::allLocked);
        enc->touch(); // a use after the lock time (the app's timer fired late, or not at all)
        QCOMPARE(locked.size(), 1);
        QVERIFY(!enc->unlocked());
        QVERIFY(!enc->isPageUnlocked(u"A.mdenc"_s));
        QString error = u"x"_s;
        QVERIFY(!unlock(enc, u"B.mdenc"_s, QString(), &error));
        QCOMPARE(error, QString()); // nothing remembered: the page asks, without an error
    }

    void aPasswordChangedElsewhereLocksThePage()
    {
        Opened o = openNotebook({{u"S.mdenc"_s, meldfixtures::FileV3.toString().toUtf8()}});
        WorkspaceEncryption *enc = o.ws->encryption();
        EncryptedPageHost host;
        host.setInner(o.ws.get());
        host.setEncryption(enc);
        QVERIFY(unlock(enc, u"S.mdenc"_s, meldfixtures::Password.toString()));
        QCOMPARE(host.read(u"S.mdenc"_s), meldfixtures::Text.toString());
        // Another device saved it under another password (and a new salt).
        const QString other =
                encryption::writeNoteFile(u"Z innego urządzenia"_s, crypto::SecureBytes::fromUtf8(u"inne"_s), {});
        QVERIFY(writeRaw(o.root + u"/S.mdenc"_s, other.toUtf8()));
        QSignalSpy locked(enc, &WorkspaceEncryption::pageLocked);
        bool ok = false;
        // What the editor had stays on screen; then the page locks with a reason, and is never written.
        QCOMPARE(host.read(u"S.mdenc"_s, &ok), meldfixtures::Text.toString());
        QVERIFY(!host.write(u"S.mdenc"_s, u"nadpisane"_s));
        QVERIFY(locked.wait(5000));
        QVERIFY(!locked.first().at(1).toString().isEmpty());
        QCOMPARE(QString::fromUtf8(readRaw(o.root + u"/S.mdenc"_s)), other);
        // Re-saved elsewhere under the same password (a new salt): read with the password.
        QVERIFY(unlock(enc, u"S.mdenc"_s, u"inne"_s));
        const QString again = encryption::writeNoteFile(u"Jeszcze raz"_s, crypto::SecureBytes::fromUtf8(u"inne"_s), {});
        QVERIFY(writeRaw(o.root + u"/S.mdenc"_s, again.toUtf8()));
        QCOMPARE(host.read(u"S.mdenc"_s), u"Jeszcze raz"_s);
    }

    void encryptAndDecryptAPage()
    {
        const QString secret = u"Hasło do banku: kołowrotek-42"_s;
        Opened o = openNotebook(
                {{u"Prywatne.md"_s, (u"- "_s + secret + u"\n"_s).toUtf8()}, {u"Spis.md"_s, "- zobacz [[Prywatne]]\n"}});
        WorkspaceEncryption *enc = o.ws->encryption();
        // Saved over once more: a version in History holds the plaintext.
        QVERIFY(o.ws->writeFile(u"Prywatne.md"_s, u"- "_s + secret + u" (poprawione)\n"_s));
        QVERIFY(!o.ws->vault()->history()->list(u"Prywatne.md"_s).isEmpty());
        QCOMPARE(enc->encryptedPathFor(u"Prywatne.md"_s), u"Prywatne.mdenc"_s);

        QSignalSpy done(enc, &WorkspaceEncryption::pageConverted);
        const int id = enc->encryptPage(u"Prywatne.md"_s, u"tajne hasło"_s, u"bank"_s, true);
        QString error;
        const QString moved = converted(enc, id, done, &error);
        QVERIFY2(!moved.isEmpty(), qPrintable(error));
        QCOMPARE(moved, u"Prywatne.mdenc"_s);
        QVERIFY(!QFileInfo::exists(o.root + u"/Prywatne.md"_s));
        QVERIFY(enc->isPageUnlocked(moved));
        // The link follows the page; the unencrypted versions are gone from History.
        QVERIFY(QString::fromUtf8(readRaw(o.root + u"/Spis.md"_s)).contains(u"[[Prywatne.mdenc]]"_s));
        QVERIFY(o.ws->vault()->history()->list(moved).isEmpty());
        QVERIFY(o.ws->vault()->history()->list(u"Prywatne.md"_s).isEmpty());
        const QString file = QString::fromUtf8(readRaw(o.root + u"/Prywatne.mdenc"_s));
        QCOMPARE(encryption::readNoteFile(file)->hint, u"bank"_s);
        QVERIFY(encryption::decryptNote(file, crypto::SecureBytes::fromUtf8(u"tajne hasło"_s)).text.contains(secret));
        QStringList found;
        // (The index cache is written again without the page: wait for that save.)
        QTRY_VERIFY2_WITH_TIMEOUT(nowhere(o.root, o.state, u"kołowrotek-42"_s, &found), qPrintable(found.join(u", "_s)),
                                  10000);

        // A new password: the old one no longer opens it, nor any version kept.
        QSignalSpy changed(enc, &WorkspaceEncryption::pageConverted);
        const int id2 = enc->changePagePassword(moved, u"nowe hasło"_s, u"nowa"_s, true);
        QCOMPARE(converted(enc, id2, changed, &error), moved);
        const QString file2 = QString::fromUtf8(readRaw(o.root + u"/Prywatne.mdenc"_s));
        QVERIFY(encryption::decryptNote(file2, crypto::SecureBytes::fromUtf8(u"nowe hasło"_s)).ok());
        QCOMPARE(encryption::decryptNote(file2, crypto::SecureBytes::fromUtf8(u"tajne hasło"_s)).failure,
                 encryption::Failure::WrongPassword);
        QVERIFY(o.ws->vault()->history()->list(moved).isEmpty());

        // Decrypted permanently: a Markdown page again, the link following.
        QSignalSpy back(enc, &WorkspaceEncryption::pageConverted);
        const int id3 = enc->decryptPage(moved);
        const QString plain = converted(enc, id3, back, &error);
        QVERIFY2(plain == u"Prywatne.md", qPrintable(error));
        QVERIFY(QString::fromUtf8(readRaw(o.root + u"/Prywatne.md"_s)).contains(secret));
        QVERIFY(!QFileInfo::exists(o.root + u"/Prywatne.mdenc"_s));
        QVERIFY(!enc->isPageUnlocked(moved));
        QVERIFY(QString::fromUtf8(readRaw(o.root + u"/Spis.md"_s)).contains(u"[[Prywatne]]"_s));
    }

    void createANewEncryptedPage()
    {
        Opened o = openNotebook({});
        WorkspaceEncryption *enc = o.ws->encryption();
        QSignalSpy done(enc, &WorkspaceEncryption::pageConverted);
        const int id = enc->createPage(u"Dziennik prywatny"_s, QString(), u"hasło"_s, QString());
        QString error;
        const QString path = converted(enc, id, done, &error);
        QVERIFY2(path.endsWith(u"Dziennik prywatny.mdenc"_s), qPrintable(error + path));
        QVERIFY(enc->isPageUnlocked(path));
        EncryptedPageHost host;
        host.setInner(o.ws.get());
        host.setEncryption(enc);
        auto editor = std::make_unique<PageController>();
        editor->setSaveDelay(100000);
        editor->setHost(&host);
        editor->setPath(path);
        QVERIFY(editor->text().isEmpty());
        const int row = editor->startNewPage(QString());
        QVERIFY(row >= 0);
        editor->setEditText(row, u"Pierwszy wpis: źdźbło"_s, 21);
        editor->save();
        o.ws->waitForWrites();
        QTRY_VERIFY(!editor->dirty());
        const QString file = QString::fromUtf8(readRaw(o.root + u'/' + path));
        QVERIFY(encryption::decryptNote(file, crypto::SecureBytes::fromUtf8(u"hasło"_s)).text.contains(u"źdźbło"_s));
        QStringList found;
        QVERIFY2(nowhere(o.root, o.state, u"źdźbło"_s, &found), qPrintable(found.join(u", "_s)));
        // A blank file (Meld Encrypt's new note before its password): the first password typed is its.
        QVERIFY(writeRaw(o.root + u"/Pusta.mdenc"_s, QByteArray()));
        o.ws->finishIndexing();
        QVERIFY(enc->pageInfo(u"Pusta.mdenc"_s).value(u"blank"_s).toBool());
        QVERIFY(unlock(enc, u"Pusta.mdenc"_s, u"pierwsze"_s));
    }

    void fragmentsOpenSaveAndRemember()
    {
        const QString note =
                u"- Konto: "_s
                + encryption::encryptFragment(u"PIN 4711, żółw"_s, crypto::SecureBytes::fromUtf8(u"p1"_s), u"bank"_s)
                + u"\n"_s;
        Opened o = openNotebook({{u"Konta.md"_s, note.toUtf8()}});
        WorkspaceEncryption *enc = o.ws->encryption();
        const auto span = brulion::markdown::findEncryptedFragments(note).value(0);
        const QString fragment = note.sliced(span.from, span.to - span.from);
        QCOMPARE(enc->fragmentInfo(fragment).value(u"hint"_s).toString(), u"bank"_s);
        QSignalSpy opened(enc, &WorkspaceEncryption::fragmentOpened);
        // Nothing remembered yet: asked, without an error.
        enc->openFragment(u"Konta.md"_s, fragment, QString(), true);
        QVERIFY(opened.wait(10000));
        QCOMPARE(opened.last().at(1).toInt(), 0);
        QVERIFY(opened.last().at(3).toString().isEmpty());
        enc->openFragment(u"Konta.md"_s, fragment, u"złe"_s, true);
        QVERIFY(opened.wait(30000));
        QCOMPARE(opened.last().at(3).toString(), u"Wrong password."_s);
        enc->openFragment(u"Konta.md"_s, fragment, u"p1"_s, true);
        QVERIFY(opened.wait(30000));
        const int session = opened.last().at(1).toInt();
        QVERIFY(session > 0);
        QCOMPARE(opened.last().at(2).toString(), u"PIN 4711, żółw"_s);
        // Saved again: a new fragment with the same password, hint and look.
        QSignalSpy ready(enc, &WorkspaceEncryption::textReady);
        enc->sealFragment(session, u"PIN 1234, żółw"_s);
        QVERIFY(ready.wait(30000));
        const QString sealed = ready.last().at(1).toString();
        const auto again = brulion::markdown::encryptedFragmentAt(sealed, 0, sealed.size());
        QVERIFY(again);
        QCOMPARE(again->hint, u"bank"_s);
        QVERIFY(again->hidden);
        QCOMPARE(encryption::decryptFragment(sealed, *again, crypto::SecureBytes::fromUtf8(u"p1"_s)).text,
                 u"PIN 1234, żółw"_s);
        enc->closeFragment(session);
        // The password was remembered: another fragment of the notebook opens without asking.
        enc->openFragment(u"Inne.md"_s, sealed, QString(), true);
        QVERIFY(opened.wait(30000));
        QVERIFY(opened.last().at(1).toInt() > 0);
        // Remembering switched off: forgotten at once, every fragment asks.
        enc->setRememberPasswords(false);
        enc->openFragment(u"Konta.md"_s, sealed, QString(), true);
        QVERIFY(opened.wait(10000));
        QCOMPARE(opened.last().at(1).toInt(), 0);
        enc->setRememberPasswords(true);
        // A new fragment: hidden or visible, hints checked.
        enc->encryptFragment(u"Konta.md"_s, u"nowy sekret"_s, u"p2"_s, u"zły 💡"_s, true, false);
        QVERIFY(ready.wait(10000));
        QVERIFY(!ready.last().at(2).toString().isEmpty());
        enc->encryptFragment(u"Konta.md"_s, u"nowy sekret"_s, u"p2"_s, QString(), false, false);
        QVERIFY(ready.wait(30000));
        QVERIFY(ready.last().at(1).toString().startsWith(u"🔐γ "_s));
        QStringList found;
        QVERIFY2(nowhere(o.root, o.state, u"PIN 4711"_s, &found), qPrintable(found.join(u", "_s)));
    }
};

QTEST_MAIN(TestWorkspaceEncryption)
#include "tst_workspace_encryption.moc"
