// The sync forms: validation of the Nextcloud address, user and folder; the WebDAV address and the
// "Security" page; explained connection errors (401, 403, TLS, DNS, refused, CORS in the browser,
// not WebDAV); NextcloudSetup against the core's in-memory Nextcloud (connected, wrong password,
// missing folder -> create it, missing account), drafts vs. saved settings (default folder
// "Notatki", the password stays out of the drafts), switching off; SyncthingSetup and the
// .stignore helpers (create, keep the user's rules, append what is missing, CRLF).
#include "syncsetup.h"

#include "appsettings.h"
#include "fakewebdavserver.h"
#include "workspace.h"
#include "workspaceintegration.h"

#include "sync/syncthing.h"
#include "vault/vault.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::ioviews::NextcloudSetup;
using brulion::ioviews::SyncthingSetup;
using brulion::shell::AppSettings;
using synctest::FakeWebDavServer;

class TestAppIoSyncSetup : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<AppSettings> settings()
    {
        ++m_n;
        return std::make_unique<AppSettings>(m_tmp.filePath(u"settings-%1.ini"_s.arg(m_n)),
                                             m_tmp.filePath(u"data-%1"_s.arg(m_n)));
    }

    static void fill(NextcloudSetup &form, const QString &password = u"hasło-źdźbło"_s)
    {
        form.setEnabled(true);
        form.setUrl(u"https://chmura.example.pl"_s);
        form.setUser(u"jan"_s);
        form.setPassword(password);
        form.setFolder(u"Notatki"_s);
    }

    static bool waitTest(NextcloudSetup &form)
    {
        QElapsedTimer t;
        t.start();
        QTest::qWait(1);
        while ((form.testing() || form.testResult().isEmpty()) && t.elapsed() < 5000)
            QTest::qWait(5);
        return !form.testing() && !form.testResult().isEmpty();
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    // ---- Validation ---------------------------------------------------------------------------------

    void serverUrl_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<bool>("unencrypted");
        QTest::newRow("https") << u"https://chmura.example.pl"_s << true << false;
        QTest::newRow("no scheme") << u"chmura.example.pl"_s << true << false;
        QTest::newRow("sub path") << u"https://example.pl/nextcloud/"_s << true << false;
        QTest::newRow("pasted web ui") << u"https://chmura.example.pl/index.php/apps/files/"_s << true << false;
        QTest::newRow("idn") << u"https://chmura.zażółć.pl"_s << true << false;
        QTest::newRow("port") << u"https://chmura.example.pl:8443"_s << true << false;
        QTest::newRow("plain http") << u"http://chmura.example.pl"_s << true << true;
        QTest::newRow("http at home") << u"http://192.168.1.20"_s << true << false;
        QTest::newRow("http localhost") << u"http://localhost:8080"_s << true << false;
        QTest::newRow("http lan name") << u"http://nas.local"_s << true << false;
        QTest::newRow("empty") << u"  "_s << false << false;
        QTest::newRow("spaces") << u"https://chmura example.pl"_s << false << false;
        QTest::newRow("ftp") << u"ftp://chmura.example.pl"_s << false << false;
        QTest::newRow("no host") << u"https://"_s << false << false;
    }

    void serverUrl()
    {
        QFETCH(QString, url);
        QFETCH(bool, ok);
        QFETCH(bool, unencrypted);
        QCOMPARE(brulion::ioviews::validateServerUrl(url).isEmpty(), ok);
        QCOMPARE(brulion::ioviews::isUnencryptedUrl(url), unencrypted);
    }

    void userAndFolder()
    {
        QVERIFY(brulion::ioviews::validateUserName(u"jan.kowalski@example.pl"_s).isEmpty());
        QVERIFY(brulion::ioviews::validateUserName(u"Łucja"_s).isEmpty());
        QVERIFY(!brulion::ioviews::validateUserName(u" "_s).isEmpty());
        QVERIFY(!brulion::ioviews::validateUserName(u"jan/x"_s).isEmpty());
        QVERIFY(brulion::ioviews::validateServerFolder(u"Notatki"_s).isEmpty());
        QVERIFY(brulion::ioviews::validateServerFolder(u"Dokumenty/Notatki źródłowe"_s).isEmpty());
        QVERIFY(!brulion::ioviews::validateServerFolder(u""_s).isEmpty());
        QVERIFY(!brulion::ioviews::validateServerFolder(u"/"_s).isEmpty());
        QVERIFY(!brulion::ioviews::validateServerFolder(u"a/../b"_s).isEmpty());
        QVERIFY(!brulion::ioviews::validateServerFolder(u"a:b"_s).isEmpty());
    }

    void securityUrl()
    {
        QCOMPARE(brulion::ioviews::nextcloudSecurityUrl(u"chmura.example.pl/"_s),
                 u"https://chmura.example.pl/index.php/settings/user/security"_s);
        QCOMPARE(brulion::ioviews::nextcloudSecurityUrl(u"https://example.pl/nextcloud/remote.php/dav/files/jan/X"_s),
                 u"https://example.pl/nextcloud/index.php/settings/user/security"_s);
        QCOMPARE(brulion::ioviews::nextcloudSecurityUrl(u""_s), QString());
    }

    void describeErrors_data()
    {
        QTest::addColumn<int>("code");
        QTest::addColumn<QString>("message");
        QTest::addColumn<bool>("browser");
        QTest::addColumn<QString>("expected");
        using C = brulion::sync::WebDavErrorCode;
        QTest::newRow("auth") << int(C::Auth) << u"HTTP 401"_s << false << u"auth"_s;
        QTest::newRow("forbidden") << int(C::Forbidden) << u"HTTP 403"_s << false << u"forbidden"_s;
        QTest::newRow("tls") << int(C::Network) << u"PROPFIND x failed: SSL handshake failed: The certificate has expired"_s
                             << false << u"tls"_s;
        QTest::newRow("dns") << int(C::Network) << u"PROPFIND x failed: Host chmura.example.pl not found"_s << false
                             << u"dns"_s;
        QTest::newRow("refused") << int(C::Network) << u"PROPFIND x failed: Connection refused"_s << false
                                 << u"refused"_s;
        QTest::newRow("other network") << int(C::Network) << u"PROPFIND x failed: Network unreachable"_s << false
                                       << u"network"_s;
        QTest::newRow("cors in the browser") << int(C::Network) << u"PROPFIND x failed: Unknown error"_s << true
                                             << u"cors"_s;
        QTest::newRow("timeout") << int(C::Timeout) << u"timed out"_s << false << u"timeout"_s;
        QTest::newRow("html login page") << int(C::Parse) << u"not a multistatus"_s << false << u"not-webdav"_s;
        QTest::newRow("quota") << int(C::InsufficientStorage) << u"HTTP 507"_s << false << u"insufficient-storage"_s;
        QTest::newRow("server") << int(C::Server) << u"HTTP 503"_s << false << u"server"_s;
    }

    void describeErrors()
    {
        QFETCH(int, code);
        QFETCH(QString, message);
        QFETCH(bool, browser);
        QFETCH(QString, expected);
        brulion::sync::WebDavError error =
                brulion::sync::WebDavError::make(static_cast<brulion::sync::WebDavErrorCode>(code), message);
        const QVariantMap m = brulion::ioviews::describeConnectionError(error, browser, u"chmura.example.pl"_s);
        QCOMPARE(m.value(u"code"_s).toString(), expected);
        QCOMPARE(m.value(u"ok"_s).toBool(), false);
        QVERIFY(!m.value(u"title"_s).toString().isEmpty());
        QVERIFY(!m.value(u"message"_s).toString().isEmpty());
        QCOMPARE(m.value(u"details"_s).toString(), message);
        if (expected == u"auth" || expected == u"tls" || expected == u"cors")
            QVERIFY(!m.value(u"hint"_s).toString().isEmpty());
        if (expected == u"tls" || expected == u"dns")
            QVERIFY(m.value(u"message"_s).toString().contains(u"chmura.example.pl"_s));
    }

    // ---- Drafts and saving ------------------------------------------------------------------------------

    void draftsAndSave()
    {
        auto s = settings();
        NextcloudSetup form;
        form.setSettings(s.get());
        QCOMPARE(form.folder(), u"Notatki"_s); // a new account suggests "Notatki"
        QVERIFY(!form.enabled());
        QVERIFY(!form.dirty());
        QVERIFY(!form.configured());
        QCOMPARE(form.intervalOptions().size(), 6);
        QCOMPARE(form.interval(), 15);
        QVERIFY(form.urlError().isEmpty()); // errors only after a save attempt

        form.setEnabled(true);
        form.setUrl(u"chmura.example.pl"_s);
        QVERIFY(form.dirty());
        QVERIFY(!form.valid());
        QVERIFY(!form.save()); // no user, no password
        QVERIFY(form.showErrors());
        QVERIFY(!form.userError().isEmpty());
        QVERIFY(!form.passwordError().isEmpty());
        QVERIFY(form.urlError().isEmpty());
        QVERIFY(!s->nextcloudEnabled());

        form.setUser(u" Łucja "_s);
        form.setPassword(u"abcde-fghij-klmno-pqrst-uvwxy"_s);
        form.setInterval(60);
        form.setDeviceName(u"Telefon Łucji"_s);
        QCOMPARE(form.davUrl(), u"https://chmura.example.pl/remote.php/dav/files/%C5%81ucja/Notatki"_s);
        QSignalSpy saved(&form, &NextcloudSetup::saved);
        QVERIFY(form.save());
        QCOMPARE(saved.size(), 1);
        QVERIFY(s->nextcloudEnabled());
        QCOMPARE(s->nextcloudUrl(), u"chmura.example.pl"_s);
        QCOMPARE(s->nextcloudUser(), u"Łucja"_s);
        QCOMPARE(s->nextcloudFolder(), u"Notatki"_s);
        QCOMPARE(s->syncIntervalMinutes(), 60);
        QCOMPARE(s->nextcloudPassword(), u"abcde-fghij-klmno-pqrst-uvwxy"_s);
        QCOMPARE(s->value(u"sync/deviceName"_s, QString()).toString(), u"Telefon Łucji"_s);
        QVERIFY(form.password().isEmpty()); // never kept in the form
        QVERIFY(form.hasStoredPassword());
        QVERIFY(form.configured());
        QVERIFY(!form.dirty());
        QVERIFY(form.valid()); // the stored password counts
        QVERIFY(form.storedPasswordApplies());

        // The stored password belongs to this server and user: another address (a typo, a
        // look-alike host) or another user needs the password typed again.
        form.setUrl(u"https://chmura.example.pl/"_s); // same server ("https://" assumed, slash)
        QVERIFY(form.storedPasswordApplies());
        form.setUrl(u"https://chmura.example.pl.evil.example"_s);
        QVERIFY(!form.storedPasswordApplies());
        QVERIFY(!form.valid());
        QVERIFY(!form.save());
        QVERIFY(!form.passwordError().isEmpty()); // shown after the save attempt
        QCOMPARE(s->nextcloudUrl(), u"chmura.example.pl"_s);
        form.setUrl(u"http://chmura.example.pl"_s); // a downgrade to plain http is another server
        QVERIFY(!form.storedPasswordApplies());
        form.setUrl(u"chmura.example.pl"_s);
        form.setUser(u"Jan"_s);
        QVERIFY(!form.storedPasswordApplies());
        form.reload();
        QVERIFY(form.storedPasswordApplies());

        // A second form shows the saved values (the folder as saved, not the suggestion).
        s->setNextcloudFolder(u"Dokumenty/Brulion"_s);
        NextcloudSetup again;
        again.setSettings(s.get());
        QCOMPARE(again.folder(), u"Dokumenty/Brulion"_s);
        QCOMPARE(again.user(), u"Łucja"_s);
        QVERIFY(again.password().isEmpty());

        // Edits are dropped by reload(); switching off works with an incomplete form.
        again.setUrl(u"https://inny.example.pl"_s);
        again.reload();
        QCOMPARE(again.url(), u"chmura.example.pl"_s);
        again.setEnabled(false);
        again.setUser(QString());
        QVERIFY(again.save());
        QVERIFY(!s->nextcloudEnabled());

        // Disconnecting forgets the app password but keeps the address.
        again.disconnectAccount();
        QVERIFY(!s->hasNextcloudPassword());
        QCOMPARE(s->nextcloudUrl(), u"chmura.example.pl"_s);
        QVERIFY(!again.configured());
    }

    void storedPasswordNotSentElsewhere()
    {
        // Saved account at chmura.example.pl; the form now names another server and leaves the
        // password field empty: the connection test must not send the stored password there.
        auto s = settings();
        s->setNextcloudUrl(u"https://chmura.example.pl"_s);
        s->setNextcloudUser(u"jan"_s);
        QVERIFY(s->setNextcloudPassword(u"hasło-źdźbło"_s));
        FakeWebDavServer server;
        server.mkdir(u"Notatki"_s);
        NextcloudSetup form;
        form.setTransport(&server);
        form.setSettings(s.get());
        form.setEnabled(true);
        form.setUrl(u"https://inna-chmura.example.pl"_s);
        form.testConnection();
        QCOMPARE(form.testResult().value(u"code"_s).toString(), u"invalid"_s);
        form.createFolder();
        QTest::qWait(20);
        QVERIFY(server.requests().isEmpty());

        // Saving the other server while switched off forgets the old account's password.
        form.setEnabled(false);
        QVERIFY(form.save());
        QVERIFY(!s->hasNextcloudPassword());

        // Workspace.sync.testConnection follows the same rule.
        brulion::shell::WorkspaceSync sync;
        sync.setTransport(&server);
        brulion::shell::NextcloudSettings n;
        n.enabled = true;
        n.url = u"https://chmura.example.pl"_s;
        n.user = u"jan"_s;
        n.password = u"hasło-źdźbło"_s;
        sync.setNextcloud(n);
        QSignalSpy tested(&sync, &brulion::shell::WorkspaceSync::connectionTested);
        sync.testConnection(u"https://inna-chmura.example.pl"_s, u"jan"_s, QString(), u"Notatki"_s);
        QCOMPARE(tested.size(), 1);
        QVERIFY(!tested.first().at(0).toBool());
        QVERIFY(server.requests().isEmpty());
        sync.testConnection(u"https://chmura.example.pl/"_s, u"jan"_s, QString(), u"Notatki"_s);
        QVERIFY(tested.wait(5000));
        QVERIFY(tested.last().at(0).toBool());
        QVERIFY(!server.requests().isEmpty());
    }

    void unencryptedWarning()
    {
        NextcloudSetup form;
        form.setUrl(u"http://chmura.example.pl"_s);
        QVERIFY(!form.urlWarning().isEmpty());
        form.setUrl(u"https://chmura.example.pl"_s);
        QVERIFY(form.urlWarning().isEmpty());
        QCOMPARE(form.securityUrl(), u"https://chmura.example.pl/index.php/settings/user/security"_s);
    }

    // ---- Connection tests ----------------------------------------------------------------------------

    void connected()
    {
        FakeWebDavServer server;
        server.mkdir(u"Notatki"_s);
        NextcloudSetup form;
        form.setTransport(&server);
        fill(form);
        form.testConnection();
        QVERIFY(form.testing());
        QVERIFY(waitTest(form));
        QVERIFY2(form.testResult().value(u"ok"_s).toBool(), qPrintable(form.testResult().value(u"details"_s).toString()));
        QCOMPARE(form.testResult().value(u"code"_s).toString(), u"ok"_s);
        QVERIFY(form.testResult().value(u"message"_s).toString().contains(u"Notatki"_s));
    }

    void wrongPassword()
    {
        FakeWebDavServer server;
        server.mkdir(u"Notatki"_s);
        NextcloudSetup form;
        form.setTransport(&server);
        fill(form, u"złe hasło"_s);
        form.testConnection();
        QVERIFY(waitTest(form));
        QCOMPARE(form.testResult().value(u"code"_s).toString(), u"auth"_s);
        QCOMPARE(form.testResult().value(u"securityUrl"_s).toString(),
                 u"https://chmura.example.pl/index.php/settings/user/security"_s);
        QVERIFY(form.testResult().value(u"hint"_s).toString().contains(u"Security"_s));
    }

    void missingFolderCreated()
    {
        FakeWebDavServer server;
        NextcloudSetup form;
        form.setTransport(&server);
        fill(form);
        form.setFolder(u"Notatki/Brulion źródła"_s);
        form.testConnection();
        QVERIFY(waitTest(form));
        QCOMPARE(form.testResult().value(u"code"_s).toString(), u"no-folder"_s);
        QVERIFY(form.testResult().value(u"canCreateFolder"_s).toBool());
        form.createFolder();
        QVERIFY(form.testing());
        QTRY_VERIFY(!form.testing());
        QCOMPARE(form.testResult().value(u"code"_s).toString(), u"created"_s);
        QVERIFY(form.testResult().value(u"ok"_s).toBool());
        QVERIFY(server.isDir(u"Notatki/Brulion źródła"_s));
        // The next test finds it.
        form.testConnection();
        QVERIFY(waitTest(form));
        QCOMPARE(form.testResult().value(u"code"_s).toString(), u"ok"_s);
    }

    void missingAccount()
    {
        FakeWebDavServer::Options options;
        options.root = u"/remote.php/dav/files/janek"_s; // "jan" logs in, but his files are elsewhere
        FakeWebDavServer server(options);
        NextcloudSetup form;
        form.setTransport(&server);
        fill(form);
        form.testConnection();
        QVERIFY(waitTest(form));
        QCOMPARE(form.testResult().value(u"code"_s).toString(), u"no-user"_s);
        QVERIFY(form.testResult().value(u"message"_s).toString().contains(u"jan"_s));
        QVERIFY(!form.testResult().value(u"canCreateFolder"_s).toBool());
    }

    void networkFailures()
    {
        FakeWebDavServer server;
        server.hooks.append([](const brulion::sync::HttpRequest &, const QString &) {
            brulion::sync::HttpResponse r;
            r.networkError = u"SSL handshake failed: The issuer certificate of a locally looked up certificate could not be found"_s;
            return std::optional<brulion::sync::HttpResponse>(r);
        });
        NextcloudSetup form;
        form.setTransport(&server);
        fill(form);
        form.testConnection();
        QVERIFY(waitTest(form));
        QCOMPARE(form.testResult().value(u"code"_s).toString(), u"tls"_s);
        // The same failure in the browser is explained as CORS.
        form.setBrowser(true);
        form.testConnection();
        QVERIFY(waitTest(form));
        QCOMPARE(form.testResult().value(u"code"_s).toString(), u"cors"_s);
        QVERIFY(form.testResult().value(u"hint"_s).toString().contains(u"services.brulion-web"_s));
    }

    void invalidFormIsNotSent()
    {
        FakeWebDavServer server;
        NextcloudSetup form;
        form.setTransport(&server);
        form.setUrl(u"https://chmura.example.pl"_s);
        form.testConnection();
        QCOMPARE(form.testResult().value(u"code"_s).toString(), u"invalid"_s);
        QCOMPARE(server.logSize(), 0);
        QVERIFY(!form.testing());
    }

    void staleTestIgnored()
    {
        FakeWebDavServer server;
        server.mkdir(u"Notatki"_s);
        server.setLatency("PROPFIND", 50);
        NextcloudSetup form;
        form.setTransport(&server);
        fill(form);
        form.testConnection();
        form.clearTestResult(); // e.g. the user edited the address meanwhile
        QTRY_VERIFY(!form.testing());
        QTest::qWait(80);
        QVERIFY(form.testResult().isEmpty());
    }

    // ---- Syncthing --------------------------------------------------------------------------------------

    void stignoreHelpers()
    {
        const QString recommended = brulion::sync::stignoreTemplate();
        QCOMPARE(brulion::ioviews::stignoreState(QString(), false, recommended), u"missing"_s);
        QCOMPARE(brulion::ioviews::stignoreState(recommended, true, recommended), u"current"_s);
        QCOMPARE(brulion::ioviews::mergeStignore(QString(), recommended), recommended);
        QCOMPARE(brulion::ioviews::mergeStignore(recommended, recommended), recommended);

        const QString own = u"// moje reguły\n!/ważne\n/Prywatne zdjęcia\n(?d).DS_Store\n"_s;
        QCOMPARE(brulion::ioviews::stignoreState(own, true, recommended), u"outdated"_s);
        const QString merged = brulion::ioviews::mergeStignore(own, recommended);
        QVERIFY(merged.startsWith(own.trimmed())); // the user's rules first, unchanged
        QVERIFY(merged.contains(u"/.trash\n"_s));
        QCOMPARE(merged.count(u"(?d).DS_Store"_s), 1); // not duplicated
        QCOMPARE(brulion::ioviews::stignoreState(merged, true, recommended), u"current"_s);

        const QString crlf = u"/Prywatne\r\n"_s;
        const QString mergedCrlf = brulion::ioviews::mergeStignore(crlf, recommended);
        QVERIFY(mergedCrlf.contains(u"/.trash\r\n"_s));
        QVERIFY(!mergedCrlf.contains(QRegularExpression(u"[^\\r]\\n"_s)));
    }

    void syncthingSetup()
    {
        const QString root = m_tmp.filePath(u"Notatnik Syncthing"_s);
        auto ws = std::make_unique<brulion::shell::Workspace>();
        ws->sync()->setSyncthingDiscovery(false);
        QVERIFY(ws->open(root, root + u"-state"_s));
        auto s = settings();
        SyncthingSetup st;
        st.setSettings(s.get());
        st.setWorkspace(ws.get());
        QCOMPARE(st.stignoreState(), u"missing"_s);
        QCOMPARE(st.defaultUrl(), u"http://127.0.0.1:8384"_s);
        QVERIFY(st.stignorePath().endsWith(u".stignore"_s));
        QVERIFY(st.writeStignore());
        QCOMPARE(st.stignoreState(), u"current"_s);
        QFile f(root + u"/.stignore"_s);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), brulion::sync::stignoreTemplate());
        f.close();

        // The user's own file: rules kept, missing ones appended.
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("/Prywatne\n");
        f.close();
        st.refreshStignore();
        QCOMPARE(st.stignoreState(), u"outdated"_s);
        QVERIFY(st.writeStignore());
        QCOMPARE(st.stignoreState(), u"current"_s);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(f.readAll()).startsWith(u"/Prywatne\n"_s));
        f.close();

        // REST settings.
        st.setApiKey(u"  klucz-źródłowy "_s);
        st.setUrl(u"nie adres"_s);
        QVERIFY(!st.urlError().isEmpty());
        QVERIFY(!st.save());
        st.setUrl(u"http://127.0.0.1:8385"_s);
        QVERIFY(st.dirty());
        QVERIFY(st.save());
        // The key is a secret (it controls Syncthing): an owner-only file, not the QSettings file.
        QCOMPARE(brulion::shell::syncthingApiKey(s.get()), u"klucz-źródłowy"_s);
        QVERIFY(s->value(u"sync/syncthingApiKey"_s, QString()).toString().isEmpty());
        s->sync();
        {
            QFile ini(s->fileName());
            QVERIFY(ini.open(QIODevice::ReadOnly));
            QVERIFY(!QString::fromUtf8(ini.readAll()).contains(u"klucz"_s));
        }
        const QString keyFile = QFileInfo(s->passwordFile()).absolutePath() + u"/syncthing-api-key"_s;
        QVERIFY(QFileInfo::exists(keyFile));
#ifdef Q_OS_UNIX
        QCOMPARE(QFileInfo(keyFile).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());
#endif
        QCOMPARE(s->value(u"sync/syncthingUrl"_s, QString()).toString(), u"http://127.0.0.1:8385"_s);
        QVERIFY(!st.dirty());
        // A key left in QSettings by an older version moves into the file on the first read.
        s->setValue(u"sync/syncthingApiKey"_s, u"stary-klucz"_s);
        QCOMPARE(brulion::shell::syncthingApiKey(s.get()), u"stary-klucz"_s);
        QVERIFY(s->value(u"sync/syncthingApiKey"_s, QString()).toString().isEmpty());
        QCOMPARE(brulion::shell::syncthingApiKey(s.get()), u"stary-klucz"_s);
        // Forgetting it removes the file.
        st.reload();
        st.setApiKey(QString());
        QVERIFY(st.save());
        QVERIFY(!QFileInfo::exists(keyFile));
        QVERIFY(brulion::shell::syncthingApiKey(s.get()).isEmpty());

        ws->close();
        QCOMPARE(st.stignoreState(), u"unavailable"_s);
        QVERIFY(!st.writeStignore());
    }
};

QTEST_GUILESS_MAIN(TestAppIoSyncSetup)
#include "tst_appio_syncsetup.moc"
