// Publishing to GitHub Pages against the in-memory GitHub (tests/core/github/fakegithub.h): the
// first publish makes the gh-pages branch without history and turns Pages on, a publish sends only
// what changed (one commit; unchanged files never leave the device, also with an empty manifest),
// removed pages are deleted, a file changed on GitHub is a conflict until "Replace them", a branch
// that moved meanwhile is published on again, docs/ of main, an empty repository, the dry run, a
// GitHub that cannot be reached, tokens that may not push, Pages that cannot be turned on, Polish
// file names, unpublishing, and the token masked in log lines.
#include "publishtest.h"

#include "fakegithub.h"

#include "github/client.h"
#include "log/redact.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;
using namespace brulion::publish;
using githubtest::FakeGitHub;
using publishtest::Notebook;

namespace {

PublishTarget pagesTarget(const QString &repository = u"jan/strona"_s, const QString &branch = u"gh-pages"_s,
                          const QString &folder = {})
{
    PublishTarget t;
    t.id = u"t0000000000a1"_s;
    t.name = u"Strona"_s;
    t.kind = TargetKind::GitHubPages;
    t.repository = repository;
    t.branch = branch;
    t.folder = folder;
    t.scope.add({PublishItem::Kind::Folder, u"Blog"_s});
    t.scope.add({PublishItem::Kind::Page, u"Zażółć gęślą jaźń.md"_s});
    return t;
}

/// The paths a commit's new trees named (from the createTree requests' bodies).
QStringList treePaths(const FakeGitHub &gh)
{
    QStringList out;
    for (const FakeGitHub::LoggedRequest &r : gh.requests) {
        if (r.method != "POST" || !r.path.endsWith(u"/git/trees"_s))
            continue;
        const QJsonArray tree = QJsonDocument::fromJson(r.body).object().value(u"tree"_s).toArray();
        for (const QJsonValue v : tree)
            out.append(v.toObject().value(u"path"_s).toString()
                       + (v.toObject().value(u"sha"_s).isNull() && !v.toObject().contains(u"content"_s)
                                  ? u" (removed)"_s
                                  : QString()));
    }
    out.sort();
    return out;
}

} // namespace

class TestPublishGitHub : public QObject {
    Q_OBJECT

    FakeGitHub *gh = nullptr;
    Publisher *publisher = nullptr;
    PublishCredentials credentials;
    Notebook nb;

    PublishReport publish(const PublishTarget &t, const PublishManifest &m, bool force = false)
    {
        const SiteBuild site = nb.site(t);
        return publishtest::run([&](auto done) { publisher->publish(t, site, m, credentials, force, done); });
    }
    PublishReport preview(const PublishTarget &t, const PublishManifest &m)
    {
        const SiteBuild site = nb.site(t);
        return publishtest::run([&](auto done) { publisher->preview(t, site, m, credentials, done); });
    }

private Q_SLOTS:
    void init()
    {
        gh = new FakeGitHub(this);
        publisher = new Publisher(gh, this);
        publisher->setClock([] { return QDateTime(QDate(2026, 11, 20), QTime(18, 30)); });
        credentials = {};
        credentials.token = gh->token;
        nb = Notebook();
        publishtest::writeBlog(nb);
        nb.write(u"Zażółć gęślą jaźń.md"_s, u"Pchnąć w tę łódź jeża lub ośm skrzyń fig.\n"_s);
        gh->createRepo(u"jan"_s, u"strona"_s, {{u"README.md"_s, QByteArray("# Strona\n")}});
    }
    void cleanup()
    {
        delete publisher;
        delete gh;
    }

    void firstPublish()
    {
        const PublishTarget t = pagesTarget();
        // The dry run sends nothing that writes.
        PublishReport r = preview(t, {});
        QVERIFY2(r.ok, qPrintable(r.errorCode));
        QVERIFY(r.plan.remoteChecked);
        QCOMPARE(r.uploaded, int(nb.site(t).files.size()));
        QCOMPARE(gh->count("POST"), 0);
        QCOMPARE(gh->count("PATCH"), 0);

        gh->clearLog();
        r = publish(t, {});
        QVERIFY2(r.ok, qPrintable(r.errorCode + u": "_s + r.error));
        const SiteBuild site = nb.site(t);
        QCOMPARE(r.uploaded, int(site.files.size()));
        // A new branch without history, holding the site only.
        const QString head = gh->head(u"jan/strona"_s, u"gh-pages"_s);
        QVERIFY(!head.isEmpty());
        QVERIFY(gh->parentsOf(head).isEmpty());
        QCOMPARE(r.commit, head);
        QCOMPARE(r.commitUrl, u"https://github.com/jan/strona/commit/"_s + head);
        QVERIFY(gh->messageOf(head).startsWith(u"Publish from Brulion: "_s));
        const QMap<QString, QByteArray> files = gh->files(u"jan/strona"_s, u"gh-pages"_s);
        QCOMPARE(files.size(), site.files.size());
        QVERIFY(files.contains(u".nojekyll"_s));
        QVERIFY(files.contains(u"Zażółć gęślą jaźń.html"_s));
        QCOMPARE(files.value(u"assets/kot.png"_s), nb.files.value(u"assets/kot.png"_s));
        QVERIFY(!files.contains(u"README.md"_s)); // main's files are main's
        QVERIFY(gh->file(u"jan/strona"_s, u"main"_s, u"README.md"_s).has_value());
        // The picture as a blob, the text inline in the tree: one blob request.
        QCOMPARE(gh->count("POST", u"/git/blobs"_s), 1);
        QCOMPARE(gh->count("POST", u"/git/commits"_s), 1);
        // Pages turned on, its address reported.
        QVERIFY(gh->repo(u"jan/strona"_s)->pages);
        QCOMPARE(gh->repo(u"jan/strona"_s)->pagesBranch, u"gh-pages"_s);
        QCOMPARE(gh->repo(u"jan/strona"_s)->pagesPath, u"/"_s);
        QCOMPARE(r.url, u"https://jan.github.io/strona/"_s);
        QCOMPARE(r.manifest.url, r.url);
        QCOMPARE(r.manifest.files.size(), site.files.size());
        QCOMPARE(r.manifest.identity, identity(t));
        QCOMPARE(r.manifest.files.value(u"style.css"_s).remote, site.file(u"style.css"_s)->gitSha);
        QVERIFY(r.warningCodes.isEmpty());
    }

    void incremental()
    {
        const PublishTarget t = pagesTarget();
        PublishReport r = publish(t, {});
        QVERIFY(r.ok);
        PublishManifest manifest = r.manifest;
        // Nothing changed: no commit at all.
        gh->clearLog();
        r = publish(t, manifest);
        QVERIFY(r.ok);
        QCOMPARE(r.uploaded + r.updated + r.deleted, 0);
        QCOMPARE(gh->count("POST", u"/git/commits"_s), 0);
        QCOMPARE(gh->count("POST", u"/git/trees"_s), 0);
        manifest = r.manifest;
        // One page changed: a commit with what that changes (the page, the search index) only.
        nb.write(u"Zażółć gęślą jaźń.md"_s, u"Nowa treść o źdźble.\n"_s);
        gh->clearLog();
        const QString before = gh->head(u"jan/strona"_s, u"gh-pages"_s);
        r = publish(t, manifest);
        QVERIFY(r.ok);
        QCOMPARE(r.updated, 2);
        QCOMPARE(treePaths(*gh), (QStringList{u"Zażółć gęślą jaźń.html"_s, u"search-index.js"_s}));
        QCOMPARE(gh->count("POST", u"/git/blobs"_s), 0);
        QCOMPARE(gh->parentsOf(r.commit), QStringList{before});
        QVERIFY(gh->messageOf(r.commit).contains(u"2 changed"_s));
        manifest = r.manifest;
        // A page gone from the site: deleted in the next commit.
        nb.remove(u"Blog/Drugi wpis.md"_s);
        gh->clearLog();
        r = publish(t, manifest);
        QVERIFY(r.ok);
        QCOMPARE(r.deleted, 1);
        QVERIFY(treePaths(*gh).contains(u"Blog/Drugi wpis.html (removed)"_s));
        QVERIFY(!gh->files(u"jan/strona"_s, u"gh-pages"_s).contains(u"Blog/Drugi wpis.html"_s));
        // Another device (no manifest): what GitHub has already is not sent again.
        gh->clearLog();
        r = publish(t, {});
        QVERIFY(r.ok);
        QCOMPARE(r.uploaded + r.updated, 0);
        QCOMPARE(gh->count("POST", u"/git/commits"_s), 0);
    }

    void conflicts()
    {
        const PublishTarget t = pagesTarget();
        PublishReport r = publish(t, {});
        QVERIFY(r.ok);
        const PublishManifest manifest = r.manifest;
        // Someone edits a published file on GitHub.
        gh->push(u"jan/strona"_s, u"gh-pages"_s, {{u"style.css"_s, QByteArray("body{color:red}")}});
        nb.write(u"Zażółć gęślą jaźń.md"_s, u"Zmiana.\n"_s);
        gh->clearLog();
        r = publish(t, manifest);
        QVERIFY(!r.ok);
        QCOMPARE(r.errorCode, u"conflicts"_s);
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.conflicts.first().path, u"style.css"_s);
        QVERIFY(!r.manifestChanged);
        QCOMPARE(gh->count("POST", u"/git/commits"_s), 0);
        QCOMPARE(errorText(r.errorCode).isEmpty(), false);
        // The dry run lists it.
        r = preview(t, manifest);
        QVERIFY(r.ok);
        QCOMPARE(r.conflicts.size(), 1);
        // "Replace them".
        r = publish(t, manifest, true);
        QVERIFY(r.ok);
        QVERIFY(gh->file(u"jan/strona"_s, u"gh-pages"_s, u"style.css"_s).value() != "body{color:red}");
        // A file there that we never published, with other bytes: a conflict too.
        gh->push(u"jan/strona"_s, u"gh-pages"_s, {{u"index.html"_s, QByteArray("<p>stara strona</p>")}});
        r = publish(t, {});
        QCOMPARE(r.errorCode, u"conflicts"_s);
    }

    void branchMoved()
    {
        const PublishTarget t = pagesTarget();
        PublishReport r = publish(t, {});
        QVERIFY(r.ok);
        nb.write(u"Zażółć gęślą jaźń.md"_s, u"Zmiana w czasie, gdy ktoś inny pcha.\n"_s);
        bool pushed = false;
        gh->hook = [&](const brulion::sync::HttpRequest &request,
                       const FakeGitHub::LoggedRequest &logged) -> std::optional<brulion::sync::HttpResponse> {
            if (!pushed && request.method == "PATCH" && logged.path.contains(u"/git/refs/heads/gh-pages"_s)) {
                pushed = true;
                gh->push(u"jan/strona"_s, u"gh-pages"_s, {{u"CNAME"_s, QByteArray("strona.example.pl\n")}});
            }
            return std::nullopt;
        };
        gh->clearLog();
        r = publish(t, r.manifest);
        QVERIFY2(r.ok, qPrintable(r.errorCode));
        QVERIFY(pushed);
        QCOMPARE(gh->count("PATCH"), 2);
        // Both: the other push's file and ours.
        QVERIFY(gh->file(u"jan/strona"_s, u"gh-pages"_s, u"CNAME"_s).has_value());
        QVERIFY(QString::fromUtf8(gh->file(u"jan/strona"_s, u"gh-pages"_s, u"Zażółć gęślą jaźń.html"_s).value())
                        .contains(u"pcha"_s));
        // A branch that keeps moving: told, nothing forced.
        gh->hook = [&](const brulion::sync::HttpRequest &request,
                       const FakeGitHub::LoggedRequest &logged) -> std::optional<brulion::sync::HttpResponse> {
            if (request.method == "PATCH" && logged.path.contains(u"/git/refs/heads/gh-pages"_s))
                gh->push(u"jan/strona"_s, u"gh-pages"_s,
                         {{u"CNAME"_s, QByteArray(QByteArray::number(gh->count("PATCH")))}});
            return std::nullopt;
        };
        nb.write(u"Zażółć gęślą jaźń.md"_s, u"Jeszcze raz.\n"_s);
        r = publish(t, r.manifest);
        QVERIFY(!r.ok);
        QCOMPARE(r.errorCode, u"not-fast-forward"_s);
    }

    void docsOfMain()
    {
        gh->push(u"jan/strona"_s, u"main"_s, {{u"src/main.cpp"_s, QByteArray("int main() {}\n")}});
        const PublishTarget t = pagesTarget(u"jan/strona"_s, u"main"_s, u"docs"_s);
        const PublishReport r = publish(t, {});
        QVERIFY2(r.ok, qPrintable(r.errorCode));
        const QMap<QString, QByteArray> files = gh->files(u"jan/strona"_s, u"main"_s);
        QVERIFY(files.contains(u"docs/index.html"_s));
        QVERIFY(files.contains(u"docs/.nojekyll"_s));
        QVERIFY(files.contains(u"src/main.cpp"_s));
        QVERIFY(files.contains(u"README.md"_s));
        QCOMPARE(gh->repo(u"jan/strona"_s)->pagesPath, u"/docs"_s);
        QCOMPARE(r.url, u"https://jan.github.io/strona/"_s);
        // A folder Pages cannot serve: published, Pages left alone, said so.
        const PublishTarget other = pagesTarget(u"jan/strona"_s, u"main"_s, u"strona/blog"_s);
        gh->repo(u"jan/strona"_s)->pages = false;
        const PublishReport r2 = publish(other, {});
        QVERIFY(r2.ok);
        QCOMPARE(r2.warningCodes, QStringList{u"pages-folder"_s});
        QVERIFY(!gh->repo(u"jan/strona"_s)->pages);
        QVERIFY(!warningText(r2.warningCodes.first()).isEmpty());
    }

    void pagesElsewhere()
    {
        FakeGitHub::Repo *repo = gh->repo(u"jan/strona"_s);
        repo->pages = true;
        repo->pagesBranch = u"main"_s;
        repo->pagesPath = u"/"_s;
        repo->pagesCname = u"strona.example.pl"_s;
        const PublishReport r = publish(pagesTarget(), {});
        QVERIFY(r.ok);
        QCOMPARE(r.warningCodes, QStringList{u"pages-elsewhere"_s});
        QCOMPARE(repo->pagesBranch, u"main"_s); // never changed by itself
    }

    void emptyRepository()
    {
        gh->createRepo(u"jan"_s, u"pusta"_s);
        const PublishTarget t = pagesTarget(u"jan/pusta"_s, u"gh-pages"_s);
        const PublishReport r = publish(t, {});
        QVERIFY2(r.ok, qPrintable(r.errorCode + u" "_s + r.error));
        QVERIFY(gh->files(u"jan/pusta"_s, u"gh-pages"_s).contains(u"index.html"_s));
        QCOMPARE(gh->count("PUT", u"/contents/"_s), 1);
    }

    void refusals()
    {
        // No token: nothing sent that writes.
        credentials.token.clear();
        PublishReport r = publish(pagesTarget(), {});
        QCOMPARE(r.errorCode, u"no-token"_s);
        QVERIFY(!errorText(r.errorCode).isEmpty());
        // The dry run works without one (a public repository).
        r = preview(pagesTarget(), {});
        QVERIFY(r.ok);
        // A token GitHub does not accept.
        credentials.token = u"ghp_wrong"_s;
        r = publish(pagesTarget(), {});
        QCOMPARE(r.errorCode, u"auth"_s);
        // Someone else's repository: no push.
        credentials.token = gh->token;
        gh->createRepo(u"ola"_s, u"jej-strona"_s, {{u"a.md"_s, QByteArray("a")}});
        r = publish(pagesTarget(u"ola/jej-strona"_s), {});
        QCOMPARE(r.errorCode, u"no-push"_s);
        // A repository that is not there.
        r = publish(pagesTarget(u"jan/nie-ma"_s), {});
        QCOMPARE(r.errorCode, u"not-found"_s);
        // An invalid target is refused before anything is sent.
        gh->clearLog();
        r = publish(pagesTarget(u"jan"_s), {});
        QCOMPARE(r.errorCode, u"bad-repository"_s);
        QCOMPARE(gh->requests.size(), 0);
        // Pages that cannot be turned on: the site is published, the user told.
        gh->hook = [](const brulion::sync::HttpRequest &request,
                      const FakeGitHub::LoggedRequest &logged) -> std::optional<brulion::sync::HttpResponse> {
            if (request.method == "POST" && logged.path.endsWith(u"/pages"_s)) {
                brulion::sync::HttpResponse forbidden;
                forbidden.status = 403;
                forbidden.body = R"({"message":"Resource not accessible by personal access token"})";
                return forbidden;
            }
            return std::nullopt;
        };
        r = publish(pagesTarget(), {});
        QVERIFY(r.ok);
        QCOMPARE(r.warningCodes, QStringList{u"pages-not-enabled"_s});
        QCOMPARE(r.url, u"https://jan.github.io/strona/"_s);
    }

    void offlinePreview()
    {
        const PublishTarget t = pagesTarget();
        PublishReport r = publish(t, {});
        QVERIFY(r.ok);
        const PublishManifest manifest = r.manifest;
        nb.write(u"Zażółć gęślą jaźń.md"_s, u"Bez sieci.\n"_s);
        gh->hook = [](const brulion::sync::HttpRequest &,
                      const FakeGitHub::LoggedRequest &) -> std::optional<brulion::sync::HttpResponse> {
            brulion::sync::HttpResponse none;
            none.networkError = u"Host not found"_s;
            return none;
        };
        r = preview(t, manifest);
        QVERIFY(r.ok);
        QVERIFY(!r.plan.remoteChecked);
        QCOMPARE(r.remoteErrorCode, u"network"_s);
        QCOMPARE(r.updated, 2);
        QCOMPARE(r.url, u"https://jan.github.io/strona/"_s);
        // Publishing offline fails, the manifest kept.
        r = publish(t, manifest);
        QCOMPARE(r.errorCode, u"network"_s);
        QVERIFY(!r.manifestChanged);
    }

    void unpublish()
    {
        const PublishTarget t = pagesTarget();
        PublishReport r = publish(t, {});
        QVERIFY(r.ok);
        gh->push(u"jan/strona"_s, u"gh-pages"_s, {{u"CNAME"_s, QByteArray("strona.example.pl\n")}});
        r = publishtest::run([&](auto done) { publisher->unpublish(t, r.manifest, credentials, false, done); });
        QVERIFY2(r.ok, qPrintable(r.errorCode));
        QCOMPARE(r.deleted, int(nb.site(t).files.size()));
        QVERIFY(r.manifest.files.isEmpty());
        QCOMPARE(gh->files(u"jan/strona"_s, u"gh-pages"_s).keys(), QStringList{u"CNAME"_s});
        QVERIFY(gh->messageOf(r.commit).startsWith(u"Unpublish"_s));
    }

    void movedTarget()
    {
        PublishTarget t = pagesTarget();
        PublishReport r = publish(t, {});
        QVERIFY(r.ok);
        // The same target pointed at another branch: its manifest does not apply there.
        t.branch = u"strona"_s;
        r = publish(t, r.manifest);
        QVERIFY(r.ok);
        QVERIFY(r.warningCodes.contains(u"moved"_s));
        QCOMPARE(r.manifest.identity, identity(t));
    }

    void tokenMasked()
    {
        PublishReport r = publish(pagesTarget(), {});
        QVERIFY(r.ok);
        // While a client holds the token it is masked in every log line.
        brulion::github::GitHubClient client(gh);
        client.setToken(gh->token);
        QVERIFY(!brulion::logging::redact(u"Bearer "_s + gh->token).contains(gh->token));
    }

    void cancel()
    {
        gh->stallNext = true;
        const SiteBuild site = nb.site(pagesTarget());
        std::optional<PublishReport> out;
        publisher->publish(pagesTarget(), site, {}, credentials, false, [&](const PublishReport &r) { out = r; });
        QVERIFY(publisher->busy());
        publisher->cancel();
        QTRY_VERIFY(out.has_value());
        QCOMPARE(out->errorCode, u"cancelled"_s);
        QVERIFY(!publisher->busy());
    }
};

QTEST_GUILESS_MAIN(TestPublishGitHub)
#include "tst_publish_github.moc"
