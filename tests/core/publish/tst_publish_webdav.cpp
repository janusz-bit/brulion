// Publishing to a WebDAV folder and to Nextcloud against in-memory servers
// (tests/core/sync/fakewebdavserver.h; fakenextcloud.h with the OCS Share API): the first publish
// (the folder made, Polish names percent-encoded), only changed files sent again with If-Match,
// removed pages deleted with their emptied folders, a file changed on the server is a conflict
// (also one that changes between the listing and the upload: a 412), "Replace them", a server
// without ETags on PUT, a sign-in refused, the dry run, unpublishing; Nextcloud's public link made
// with a password and an expiry date, kept, changed, removed — and a password its policy refuses.
#include "publishtest.h"

#include "fakenextcloud.h"
#include "fakewebdavserver.h"

using namespace Qt::StringLiterals;
using namespace brulion::publish;
using publishtest::FakeNextcloud;
using publishtest::Notebook;
using synctest::FakeWebDavServer;

namespace {

PublishTarget davTarget(const FakeWebDavServer &server)
{
    PublishTarget t;
    t.id = u"t0000000000d1"_s;
    t.name = u"Mój serwer"_s;
    t.kind = TargetKind::WebDav;
    t.url = server.url(u"www/strona"_s);
    t.username = server.options().username;
    t.siteUrl = u"https://strona.example.pl/"_s;
    t.scope.add({PublishItem::Kind::Folder, u"Blog"_s});
    t.scope.add({PublishItem::Kind::Page, u"Zażółć gęślą jaźń.md"_s});
    return t;
}

PublishTarget cloudTarget(const FakeNextcloud &cloud)
{
    PublishTarget t;
    t.id = u"t0000000000n1"_s;
    t.name = u"Chmura"_s;
    t.kind = TargetKind::Nextcloud;
    t.server = cloud.server();
    t.username = cloud.dav.options().username;
    t.folder = u"Publiczne/Przepisy babci"_s;
    t.share = true;
    t.scope.add({PublishItem::Kind::Folder, u"Blog"_s});
    return t;
}

} // namespace

class TestPublishWebDav : public QObject {
    Q_OBJECT

    Notebook nb;

    template<typename Transport>
    PublishReport publish(Transport &transport, const PublishTarget &t, const PublishManifest &m,
                          const PublishCredentials &c, bool force = false)
    {
        Publisher publisher(&transport);
        const SiteBuild site = nb.site(t);
        return publishtest::run([&](auto done) { publisher.publish(t, site, m, c, force, done); });
    }

private Q_SLOTS:
    void init()
    {
        nb = Notebook();
        publishtest::writeBlog(nb);
        nb.write(u"Zażółć gęślą jaźń.md"_s, u"Pchnąć w tę łódź jeża.\n"_s);
    }

    void webdav()
    {
        FakeWebDavServer server;
        const PublishTarget t = davTarget(server);
        const PublishCredentials c{{}, server.options().password, {}};
        // The dry run of a folder that does not exist yet: everything new, nothing written.
        {
            Publisher publisher(&server);
            const SiteBuild site = nb.site(t);
            const PublishReport r = publishtest::run([&](auto done) { publisher.preview(t, site, {}, c, done); });
            QVERIFY2(r.ok, qPrintable(r.errorCode));
            QVERIFY(r.plan.remoteChecked);
            QCOMPARE(r.uploaded, int(site.files.size()));
            QCOMPARE(r.url, u"https://strona.example.pl/"_s);
            QVERIFY(server.requests("PUT").isEmpty());
            QVERIFY(server.requests("MKCOL").isEmpty());
        }
        PublishReport r = publish(server, t, {}, c);
        QVERIFY2(r.ok, qPrintable(r.errorCode + u' ' + r.error));
        const SiteBuild site = nb.site(t);
        QCOMPARE(r.uploaded, int(site.files.size()));
        QVERIFY(server.text(u"www/strona/Zażółć gęślą jaźń.html"_s).has_value());
        QCOMPARE(server.bytes(u"www/strona/assets/kot.png"_s).value(), nb.files.value(u"assets/kot.png"_s));
        QVERIFY(!server.text(u"www/strona/.nojekyll"_s).has_value()); // GitHub's only
        // New files only where nothing is (If-None-Match: *).
        for (const FakeWebDavServer::LoggedRequest &put : server.requests("PUT"))
            QCOMPARE(put.header("if-none-match"), QByteArray("*"));
        QCOMPARE(r.manifest.files.value(u"index.html"_s).remote, server.etag(u"www/strona/index.html"_s));
        PublishManifest manifest = r.manifest;

        // Unchanged: nothing sent.
        server.clearLog();
        r = publish(server, t, manifest, c);
        QVERIFY(r.ok);
        QCOMPARE(r.uploaded + r.updated + r.deleted, 0);
        QVERIFY(server.requests("PUT").isEmpty());
        manifest = r.manifest;

        // A page changed and one removed: two PUTs with If-Match, a DELETE, the emptied folder gone.
        nb.write(u"Zażółć gęślą jaźń.md"_s, u"Nowa treść.\n"_s);
        nb.write(u"Blog/Archiwum/Stary wpis.md"_s, u"Stary.\n"_s);
        r = publish(server, t, manifest, c);
        QVERIFY(r.ok);
        manifest = r.manifest;
        QVERIFY(server.isDir(u"www/strona/Blog/Archiwum"_s));
        nb.remove(u"Blog/Archiwum/Stary wpis.md"_s);
        server.clearLog();
        r = publish(server, t, manifest, c);
        QVERIFY(r.ok);
        QCOMPARE(r.deleted, 1);
        QVERIFY(!server.isDir(u"www/strona/Blog/Archiwum"_s));
        QVERIFY(server.isDir(u"www/strona/Blog"_s));
        for (const FakeWebDavServer::LoggedRequest &put : server.requests("PUT"))
            QVERIFY(!put.header("if-match").isEmpty());
        manifest = r.manifest;

        // Someone changes a published file on the server: a conflict, nothing replaced.
        server.putText(u"www/strona/style.css"_s, u"body{color:red}"_s);
        nb.write(u"Zażółć gęślą jaźń.md"_s, u"Jeszcze nowsza.\n"_s);
        server.clearLog();
        r = publish(server, t, manifest, c);
        QCOMPARE(r.errorCode, u"conflicts"_s);
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.conflicts.first().path, u"style.css"_s);
        QVERIFY(server.requests("PUT").isEmpty());
        // "Replace them": the version seen is the one replaced (If-Match), nothing else.
        r = publish(server, t, manifest, c, true);
        QVERIFY(r.ok);
        QVERIFY(server.text(u"www/strona/style.css"_s).value() != u"body{color:red}"_s);
        manifest = r.manifest;

        // A file that changes between the listing and the upload: the server's 412, never overwritten.
        nb.write(u"Zażółć gęślą jaźń.md"_s, u"Wyścig.\n"_s);
        bool raced = false;
        server.hooks.append([&](const brulion::sync::HttpRequest &request,
                                const QString &path) -> std::optional<brulion::sync::HttpResponse> {
            if (request.method == "PUT" && path.endsWith(u"/Zażółć gęślą jaźń.html"_s) && !raced) {
                raced = true;
                server.putText(u"www/strona/Zażółć gęślą jaźń.html"_s, u"<p>cudza</p>"_s);
            }
            return std::nullopt;
        });
        r = publish(server, t, manifest, c);
        QVERIFY(raced);
        QVERIFY(!r.ok);
        QCOMPARE(r.errorCode, u"conflicts"_s);
        QCOMPARE(server.text(u"www/strona/Zażółć gęślą jaźń.html"_s).value(), u"<p>cudza</p>"_s);
        QVERIFY(r.manifestChanged); // what did go up is recorded
        server.hooks.clear();

        // Unpublish: every file we put there goes, the folder's own stay.
        server.putText(u"www/strona/.htaccess"_s, u"Options -Indexes"_s);
        Publisher publisher(&server);
        const PublishManifest last = r.manifest;
        r = publishtest::run([&](auto done) { publisher.unpublish(t, last, c, false, done); });
        QVERIFY2(r.ok || r.errorCode == u"conflicts"_s, qPrintable(r.errorCode));
        QVERIFY(!server.text(u"www/strona/index.html"_s).has_value());
        QVERIFY(server.text(u"www/strona/.htaccess"_s).has_value());
        QVERIFY(!server.isDir(u"www/strona/assets"_s));
    }

    void serverWithoutEtagsOnPut()
    {
        FakeWebDavServer::Options options;
        options.putEtag = false;
        FakeWebDavServer server(options);
        const PublishTarget t = davTarget(server);
        const PublishCredentials c{{}, server.options().password, {}};
        PublishReport r = publish(server, t, {}, c);
        QVERIFY(r.ok);
        // The listing after the uploads gave the ETags: the next publish sees its own files.
        QCOMPARE(r.manifest.files.value(u"index.html"_s).remote, server.etag(u"www/strona/index.html"_s));
        server.clearLog();
        r = publish(server, t, r.manifest, c);
        QVERIFY2(r.ok, qPrintable(r.errorCode));
        QCOMPARE(r.uploaded + r.updated, 0);
        QVERIFY(r.conflicts.isEmpty());
    }

    void refused()
    {
        FakeWebDavServer server;
        const PublishTarget t = davTarget(server);
        PublishReport r = publish(server, t, {}, PublishCredentials{{}, u"złe hasło"_s, {}});
        QCOMPARE(r.errorCode, u"auth"_s);
        QVERIFY(server.requests("PUT").isEmpty());
        // The dry run shows the local plan and says why the server was not checked.
        Publisher publisher(&server);
        const SiteBuild site = nb.site(t);
        r = publishtest::run(
                [&](auto done) { publisher.preview(t, site, {}, PublishCredentials{{}, u"złe"_s, {}}, done); });
        QVERIFY(r.ok);
        QVERIFY(!r.plan.remoteChecked);
        QCOMPARE(r.remoteErrorCode, u"auth"_s);
        QCOMPARE(r.uploaded, int(site.files.size()));
    }

    void nextcloud()
    {
        FakeNextcloud cloud;
        PublishTarget t = cloudTarget(cloud);
        t.sharePassword = true;
        t.shareExpires = QDate(2026, 12, 31);
        PublishCredentials c{{}, cloud.dav.options().password, u"Pieczątka-123"_s};
        PublishReport r = publish(cloud, t, {}, c);
        QVERIFY2(r.ok, qPrintable(r.errorCode + u' ' + r.error));
        QVERIFY(cloud.dav.text(u"Publiczne/Przepisy babci/index.html"_s).has_value());
        QCOMPARE(cloud.shares.size(), 1);
        const QString id = cloud.shares.firstKey();
        const FakeNextcloud::Share share = cloud.shares.first();
        QCOMPARE(share.path, u"/Publiczne/Przepisy babci"_s);
        QCOMPARE(share.type, 3);
        QCOMPARE(share.password, u"Pieczątka-123"_s);
        QCOMPARE(share.expires, QDate(2026, 12, 31));
        QCOMPARE(r.url, u"https://chmura.example.pl/s/"_s + share.token);
        QCOMPARE(r.manifest.shareId, id);
        QCOMPARE(r.manifest.shareUrl, r.url);
        QVERIFY(!r.manifest.sharePasswordHash.isEmpty());
        QVERIFY(!r.manifest.sharePasswordHash.contains(u"Pieczątka"_s));
        PublishManifest manifest = r.manifest;

        // Again: the same link, nothing changed on it.
        cloud.ocsLog.clear();
        r = publish(cloud, t, manifest, c);
        QVERIFY(r.ok);
        QCOMPARE(cloud.shares.size(), 1);
        QCOMPARE(cloud.ocsCount("POST"), 0);
        QCOMPARE(cloud.ocsCount("PUT"), 0);
        QCOMPARE(r.url, manifest.shareUrl);
        manifest = r.manifest;

        // A new password and no expiry: the link changed in place.
        c.sharePassword = u"Nowe-hasło-456"_s;
        t.shareExpires = {};
        r = publish(cloud, t, manifest, c);
        QVERIFY(r.ok);
        QCOMPARE(cloud.shares.value(id).password, u"Nowe-hasło-456"_s);
        QVERIFY(!cloud.shares.value(id).expires.isValid());
        QCOMPARE(cloud.ocsCount("PUT"), 2);
        manifest = r.manifest;

        // The link removed on the server meanwhile: made again.
        cloud.shares.clear();
        r = publish(cloud, t, manifest, c);
        QVERIFY(r.ok);
        QCOMPARE(cloud.shares.size(), 1);
        QVERIFY(r.manifest.shareId != id);
        manifest = r.manifest;

        // No public link any more: removed, the files stay.
        t.share = false;
        r = publish(cloud, t, manifest, c);
        QVERIFY(r.ok);
        QVERIFY(cloud.shares.isEmpty());
        QVERIFY(r.manifest.shareId.isEmpty());
        QVERIFY(cloud.dav.text(u"Publiczne/Przepisy babci/index.html"_s).has_value());
        manifest = r.manifest;

        // Back on, then unpublished: the link and the files go.
        t.share = true;
        r = publish(cloud, t, manifest, c);
        QVERIFY(r.ok);
        QCOMPARE(cloud.shares.size(), 1);
        Publisher publisher(&cloud);
        r = publishtest::run([&](auto done) { publisher.unpublish(t, r.manifest, c, false, done); });
        QVERIFY2(r.ok, qPrintable(r.errorCode));
        QVERIFY(cloud.shares.isEmpty());
        QVERIFY(!cloud.dav.text(u"Publiczne/Przepisy babci/index.html"_s).has_value());
        QVERIFY(r.manifest.isEmpty());
    }

    void nextcloudRefusals()
    {
        FakeNextcloud cloud;
        cloud.minPasswordLength = 10;
        PublishTarget t = cloudTarget(cloud);
        t.sharePassword = true;
        // A password the server's policy refuses: the files are up, the link is not; its words told.
        PublishReport r = publish(cloud, t, {}, PublishCredentials{{}, cloud.dav.options().password, u"krótkie"_s});
        QVERIFY(!r.ok);
        QCOMPARE(r.errorCode, u"share"_s);
        QVERIFY(r.error.contains(u"at least 10 characters"_s));
        QVERIFY(r.manifestChanged);
        QVERIFY(cloud.dav.text(u"Publiczne/Przepisy babci/index.html"_s).has_value());
        QVERIFY(cloud.shares.isEmpty());
        QVERIFY(errorText(r.errorCode, r.error).contains(u"at least 10 characters"_s));
        const PublishManifest published = r.manifest;
        // No password for the account: refused before anything is sent.
        cloud.dav.clearLog();
        r = publish(cloud, t, {}, PublishCredentials{});
        QCOMPARE(r.errorCode, u"no-password"_s);
        QCOMPARE(cloud.dav.logSize(), 0);
        // A link with a password but none on this device: the files go up, no link without it.
        r = publish(cloud, t, published, PublishCredentials{{}, cloud.dav.options().password, {}});
        QVERIFY2(r.ok, qPrintable(r.errorCode));
        QVERIFY(r.warningCodes.contains(u"no-share-password"_s));
        QVERIFY(cloud.shares.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPublishWebDav)
#include "tst_publish_webdav.moc"
