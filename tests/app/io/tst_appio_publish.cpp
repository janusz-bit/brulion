// PublishController (the Publish tab of Import and export) over a real Workspace: targets made from
// the form's fields (checked, their secrets in owner-only files, never in the store), items added
// and removed, the options, the dry run read in chunks with its rows (files, left out, counts), a
// publish to GitHub Pages against the in-memory GitHub (tests/core/github/fakegithub.h) with the
// manifest and the last status kept, a conflict and "Replace them", unpublishing, a route's item
// waiting for a new target, a folder target, and password-protected pages (their password kept
// like the others, a publish without it stopped).
#include "publishcontroller.h"
#include "workspace.h"

#include "fakegithub.h"

#include "publish/publish.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::ioviews::PublishController;

namespace {

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QStringList column(const QVariantList &rows, const QString &key)
{
    QStringList out;
    for (const QVariant &row : rows)
        out.append(row.toMap().value(key).toString());
    return out;
}

} // namespace

class TestAppIoPublish : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<brulion::shell::Workspace> open(const QHash<QString, QByteArray> &files)
    {
        const QString root = m_tmp.filePath(u"Mój brulion %1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeRaw(root + u'/' + it.key(), it.value());
        auto ws = std::make_unique<brulion::shell::Workspace>();
        ws->setClock([] { return QDateTime(QDate(2026, 11, 20), QTime(18, 30)); });
        ws->sync()->setSyncthingDiscovery(false);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        return ws;
    }

    static QHash<QString, QByteArray> blog()
    {
        return {
                {u"Blog/Pierwszy wpis.md"_s,
                 "# Pierwszy\nZażółć gęślą jaźń, [[Drugi wpis]] i [[Szkic]].\n![[kot.png]]\n"},
                {u"Blog/Drugi wpis.md"_s, "Drugi.\n"},
                {u"Blog/Szkic.md"_s, "---\npublish: false\n---\nNie.\n"},
                {u"assets/kot.png"_s, "\x89PNG kot"},
                {u"Przepisy/Bigos.md"_s, "Bigos #przepisy\n"},
        };
    }

    static bool waitState(PublishController &c, const QString &state, int ms = 15000)
    {
        QElapsedTimer t;
        t.start();
        while (c.state() != state && t.elapsed() < ms * BRULION_TEST_SLOWDOWN)
            QTest::qWait(10);
        if (c.state() != state)
            qWarning() << "state" << c.state() << "error" << c.errorText();
        return c.state() == state;
    }

    QVariantMap githubFields(const QString &name = u"Strona"_s) const
    {
        return {{u"name"_s, name},
                {u"kind"_s, u"github"_s},
                {u"repository"_s, u"jan/strona"_s},
                {u"branch"_s, u"gh-pages"_s},
                {u"folder"_s, QString()},
                {u"content"_s, u"site"_s}};
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QStandardPaths::setTestModeEnabled(true);
    }

    void targetsAndSecrets()
    {
        auto ws = open(blog());
        PublishController c;
        c.setAppDataDir(m_tmp.filePath(u"appdata1"_s));
        c.setWorkspace(ws.get());
        QVERIFY(c.targets().isEmpty());
        QVERIFY(c.current().isEmpty());
        // The form's checks, in words.
        QVariantMap fields = githubFields();
        fields.insert(u"repository"_s, u"jan"_s);
        QVERIFY(!c.checkFields(fields).isEmpty());
        QVERIFY(c.saveTarget(fields).isEmpty());
        fields = githubFields();
        fields.insert(u"repository"_s, u"https://github.com/jan/strona"_s); // pasted: understood
        QVERIFY(c.checkFields(fields).isEmpty());
        const QString id = c.saveTarget(fields);
        QVERIFY(!id.isEmpty());
        QCOMPARE(c.current(), id);
        QCOMPARE(c.targets().size(), 1);
        QCOMPARE(c.currentTarget().value(u"address"_s).toString(), u"jan/strona · gh-pages"_s);
        QVERIFY(c.currentTarget().value(u"needsToken"_s).toBool());
        // A name taken.
        QVERIFY(!c.checkFields(githubFields()).isEmpty());
        // Nextcloud with its passwords: owner-only files, never in the store.
        QVariantMap cloud{
                {u"name"_s, u"Chmura"_s},   {u"kind"_s, u"nextcloud"_s},          {u"server"_s, u"chmura.example.pl"_s},
                {u"username"_s, u"jan"_s},  {u"folder"_s, u"Publiczne/Strona"_s}, {u"share"_s, true},
                {u"sharePassword"_s, true}, {u"shareExpires"_s, u"2026-13-40"_s}};
        QVERIFY(!c.checkFields(cloud).isEmpty()); // not a date
        cloud.insert(u"shareExpires"_s, u"2026-12-31"_s);
        const QString cloudId = c.saveTarget(cloud, u"hasło-aplikacji"_s, u"Pieczątka-123"_s);
        QVERIFY(!cloudId.isEmpty());
        const QVariantMap form = c.targetFields(cloudId);
        QVERIFY(form.value(u"hasPassword"_s).toBool());
        QVERIFY(form.value(u"hasSharePassword"_s).toBool());
        QCOMPARE(form.value(u"shareExpires"_s).toString(), u"2026-12-31"_s);
        QCOMPARE(brulion::publish::readSecret(brulion::publish::secretFile(c.appDataDir(), cloudId, u"share"_s)),
                 u"Pieczątka-123"_s);
        QFile store(ws->vault()->deviceStatePath() + u"/publish/targets.json"_s);
        QVERIFY(store.open(QIODevice::ReadOnly));
        const QByteArray json = store.readAll();
        store.close(); // on Windows an open file cannot be replaced: saveTarget() below writes it
        QVERIFY(!json.contains("Pieczątka"));
        QVERIFY(!json.contains("hasło"));
        // Saving again without passwords keeps them; removing the target forgets them.
        cloud.insert(u"id"_s, cloudId);
        QCOMPARE(c.saveTarget(cloud), cloudId);
        QVERIFY(c.targetFields(cloudId).value(u"hasPassword"_s).toBool());
        c.removeTarget(cloudId);
        QCOMPARE(c.targets().size(), 1);
        QVERIFY(brulion::publish::readSecret(brulion::publish::secretFile(c.appDataDir(), cloudId, u"password"_s))
                        .isEmpty());
        // Another controller (another visit of the view, the command line) sees the same targets.
        PublishController other;
        other.setAppDataDir(c.appDataDir());
        other.setWorkspace(ws.get());
        QCOMPARE(other.targets().size(), 1);
    }

    void previewAndPublishToGitHub()
    {
        auto ws = open(blog());
        githubtest::FakeGitHub gh;
        gh.createRepo(u"jan"_s, u"strona"_s, {{u"README.md"_s, QByteArray("# Strona\n")}});
        PublishController c;
        c.setAppDataDir(m_tmp.filePath(u"appdata2"_s));
        c.setTransport(&gh);
        c.setReadChunk(1); // the reading's chunks really run
        c.setWorkspace(ws.get());
        QVERIFY(brulion::publish::writeSecret(brulion::publish::githubTokenFile(c.appDataDir()), gh.token));
        const QString id = c.saveTarget(githubFields());
        QVERIFY(!id.isEmpty());
        QVERIFY(!c.currentTarget().value(u"needsToken"_s).toBool());
        // Nothing chosen yet.
        c.preview();
        QCOMPARE(c.state(), u"error"_s);
        c.dismiss();
        // The folder: added and previewed.
        c.addItem(u"folder"_s, u"Blog"_s);
        QVERIFY(waitState(c, u"previewed"_s));
        QCOMPARE(c.currentTarget().value(u"itemCount"_s).toInt(), 1);
        QVariantMap plan = c.plan();
        QVERIFY(plan.value(u"remoteChecked"_s).toBool());
        QCOMPARE(plan.value(u"pages"_s).toInt(), 2);
        QCOMPARE(plan.value(u"attachments"_s).toInt(), 1);
        const QStringList files = column(plan.value(u"files"_s).toList(), u"path"_s);
        QVERIFY(files.contains(u"Blog/Pierwszy wpis.html"_s));
        QVERIFY(files.contains(u".nojekyll"_s));
        QCOMPARE(column(plan.value(u"leftOut"_s).toList(), u"path"_s), QStringList{u"Blog/Szkic.md"_s});
        QCOMPARE(gh.count("POST"), 0);
        // Publish.
        QSignalSpy finished(&c, &PublishController::finished);
        c.publish(false);
        QVERIFY(waitState(c, u"published"_s));
        QCOMPARE(c.url(), u"https://jan.github.io/strona/"_s);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(1).toString(), u"success"_s);
        QVERIFY(gh.files(u"jan/strona"_s, u"gh-pages"_s).contains(u"Blog/Drugi wpis.html"_s));
        QVERIFY(c.currentTarget().value(u"published"_s).toBool());
        QCOMPARE(c.currentTarget().value(u"url"_s).toString(), c.url());
        QVERIFY(!brulion::publish::PublishStore(ws->vault()->deviceStatePath()).manifest(id).files.isEmpty());
        // Unchanged: nothing sent; the plan says so.
        c.dismiss();
        c.preview();
        QVERIFY(waitState(c, u"previewed"_s));
        QVERIFY(!c.plan().value(u"hasChanges"_s).toBool());
        // A file changed on GitHub: a conflict, then "Replace them".
        gh.push(u"jan/strona"_s, u"gh-pages"_s, {{u"style.css"_s, QByteArray("body{}")}});
        QVERIFY(ws->vault()->writeText(u"Blog/Drugi wpis.md"_s, u"Drugi, nowy.\n"_s));
        ws->finishIndexing();
        c.publish(false);
        QVERIFY(waitState(c, u"conflicts"_s));
        QCOMPARE(column(c.plan().value(u"conflicts"_s).toList(), u"path"_s), QStringList{u"style.css"_s});
        c.publish(true);
        QVERIFY(waitState(c, u"published"_s));
        QVERIFY(gh.file(u"jan/strona"_s, u"gh-pages"_s, u"style.css"_s).value() != "body{}");
        // An option and the content.
        c.setOption(u"linkedPages"_s, true);
        QVERIFY(c.currentTarget().value(u"linkedPages"_s).toBool());
        c.setContent(u"document"_s);
        QCOMPARE(c.currentTarget().value(u"content"_s).toString(), u"document"_s);
        c.setContent(u"site"_s);
        // Unpublish.
        c.unpublish(false);
        QVERIFY(waitState(c, u"unpublished"_s));
        QVERIFY(!gh.files(u"jan/strona"_s, u"gh-pages"_s).contains(u"index.html"_s));
        QVERIFY(!c.currentTarget().value(u"published"_s).toBool());
        // Removing an item.
        c.removeItem(0);
        QCOMPARE(c.currentTarget().value(u"itemCount"_s).toInt(), 0);
    }

    void routeWaitsForATarget()
    {
        auto ws = open(blog());
        PublishController c;
        c.setAppDataDir(m_tmp.filePath(u"appdata3"_s));
        c.setWorkspace(ws.get());
        // No target: the page waits for one.
        QVERIFY(c.addFromRoute({{u"add"_s, u"page"_s}, {u"path"_s, u"Przepisy/Bigos.md"_s}}));
        const QString www = m_tmp.filePath(u"www łąka"_s);
        const QString id = c.saveTarget({{u"name"_s, u"Katalog"_s},
                                         {u"kind"_s, u"folder"_s},
                                         {u"path"_s, QUrl::fromLocalFile(www).toString()}});
        QVERIFY(!id.isEmpty());
        QCOMPARE(brulion::publish::PublishStore(ws->vault()->deviceStatePath()).target(id)->path, www);
        c.addPending();
        QVERIFY(waitState(c, u"previewed"_s));
        QCOMPARE(c.currentTarget().value(u"itemCount"_s).toInt(), 1);
        c.publish(false);
        QVERIFY(waitState(c, u"published"_s));
        QVERIFY(QFileInfo::exists(www + u"/Przepisy/Bigos.html"_s));
        // With a target the route's item goes to it.
        c.dismiss();
        QVERIFY(!c.addFromRoute({{u"add"_s, u"tag"_s}, {u"tag"_s, u"przepisy"_s}}));
        QVERIFY(waitState(c, u"previewed"_s));
        QCOMPARE(c.currentTarget().value(u"itemCount"_s).toInt(), 2);
        // A route without an item does nothing.
        QVERIFY(!c.addFromRoute({{u"tab"_s, u"publish"_s}}));
        // Tags for the picker.
        const QVariantList tags = c.tagRows(u"prz"_s);
        QCOMPARE(column(tags, u"path"_s), QStringList{u"przepisy"_s});
    }

    void protectedPages()
    {
        auto ws = open(blog());
        PublishController c;
        c.setAppDataDir(m_tmp.filePath(u"appdata4"_s));
        c.setWorkspace(ws.get());
        const QString www = m_tmp.filePath(u"www chroniona"_s);
        QVariantMap fields{{u"name"_s, u"Rodzina"_s},
                           {u"kind"_s, u"folder"_s},
                           {u"path"_s, www},
                           {u"protect"_s, true},
                           {u"pagesPasswordGiven"_s, false}};
        QVERIFY(!c.checkFields(fields).isEmpty()); // the pages' password first
        fields.insert(u"pagesPasswordGiven"_s, true);
        QVERIFY(c.checkFields(fields).isEmpty());
        const QString id = c.saveTarget(fields, {}, {}, u"Źdźbło łąki"_s);
        QVERIFY(!id.isEmpty());
        QVERIFY(c.targetFields(id).value(u"protect"_s).toBool());
        QVERIFY(c.targetFields(id).value(u"hasPagesPassword"_s).toBool());
        QVERIFY(!c.currentTarget().value(u"needsPagesPassword"_s).toBool());
        fields.insert(u"id"_s, id);
        fields.insert(u"pagesPasswordGiven"_s, false);
        QVERIFY(c.checkFields(fields).isEmpty()); // one is kept
        QFile store(ws->vault()->deviceStatePath() + u"/publish/targets.json"_s);
        QVERIFY(store.open(QIODevice::ReadOnly));
        const QByteArray json = store.readAll();
        store.close();
        QVERIFY(json.contains("protectSalt"));
        QVERIFY(!json.contains("Źdźbło"));
        // Preview and publish: only ciphertext in the folder.
        c.addItem(u"folder"_s, u"Blog"_s);
        QVERIFY(waitState(c, u"previewed"_s));
        QVERIFY(c.plan().value(u"protectedPages"_s).toBool());
        c.publish(false);
        QVERIFY(waitState(c, u"published"_s));
        QFile index(www + u"/index.html"_s);
        QVERIFY(index.open(QIODevice::ReadOnly));
        const QByteArray lock = index.readAll();
        QVERIFY(lock.contains("brulion-sealed"));
        QVERIFY(!lock.contains("Zażółć"));
        QVERIFY(!lock.contains("Pierwszy"));
        QVERIFY(!QFileInfo::exists(www + u"/Blog"_s));
        // The password forgotten: the card says so, a publish stops.
        c.dismiss();
        fields.insert(u"forgetPagesPassword"_s, true);
        QCOMPARE(c.saveTarget(fields), id);
        QVERIFY(c.currentTarget().value(u"needsPagesPassword"_s).toBool());
        c.publish(false);
        QVERIFY(waitState(c, u"error"_s));
        QCOMPARE(c.errorText(), brulion::publish::errorText(u"no-page-password"_s));
        // Protection off: no password needed, none kept.
        fields.remove(u"forgetPagesPassword"_s);
        QVERIFY(brulion::publish::writeSecret(brulion::publish::secretFile(c.appDataDir(), id, u"pages"_s),
                                              u"jeszcze jedno"_s));
        fields.insert(u"protect"_s, false);
        QCOMPARE(c.saveTarget(fields), id);
        QVERIFY(!c.currentTarget().value(u"needsPagesPassword"_s).toBool());
        QVERIFY(brulion::publish::readSecret(brulion::publish::secretFile(c.appDataDir(), id, u"pages"_s)).isEmpty());
    }
};

QTEST_MAIN(TestAppIoPublish)
#include "tst_appio_publish.moc"
