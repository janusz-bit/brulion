// End to end: the app password never reaches the event log. A Logger (temporary folder,
// detailed verbosity, a capturing "stderr" handler before it) watches sync runs of a WebDavClient
// whose base URL was typed with user info, against a server answering 401, 500, a timeout and an
// HTML login page, plus a deliberate qWarning of the request headers. Neither the ring, the
// files, the export, the captured stderr, the sync history (its JSON file) nor the reports'
// messages may contain the password, its percent-encoding, the Basic token or
// "Authorization: Basic".
#include "fakewebdavserver.h"
#include "synctest.h"

#include "log/categories.h"
#include "log/logger.h"
#include "log/redact.h"
#include "sync/synchistory.h"

#include <QFile>
#include <QMutex>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace synctest;

namespace {

const QString kPassword = u"Zażółć-Gęślą-Jaźń-42!"_s;

QMutex g_mutex;
QStringList g_stderr;

void capture(QtMsgType, const QMessageLogContext &, const QString &message)
{
    QMutexLocker locker(&g_mutex);
    g_stderr.append(message);
}

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

FakeWebDavServer::Options serverOptions()
{
    FakeWebDavServer::Options o;
    o.password = kPassword;
    return o;
}

} // namespace

class TestSyncSecrets : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void secretNeverLeaks();
};

void TestSyncSecrets::secretNeverLeaks()
{
    QTemporaryDir dir;
    const QtMessageHandler qtest = qInstallMessageHandler(capture);
    logging::LoggerOptions lo;
    lo.directory = dir.filePath(u"logs"_s);
    lo.sessionInfo = u"tst_sync_secrets"_s;
    logging::Logger *logger = logging::Logger::install(lo);
    logger->setVerbosity(logging::Verbosity::Detailed);

    FakeWebDavServer server(serverOptions());
    server.mkdir(kFolder);
    server.putText(u"Notatki/zdalny.md"_s, u"z serwera\n"_s);
    // The server address as a user might type it: with user name and password in it.
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(kPassword));
    QString base = server.url(kFolder);
    base.replace(u"https://"_s, u"https://jan:%1@"_s.arg(encoded));
    QVERIFY(base.contains(encoded));
    // nextcloudDavUrl() drops it; the raw address still goes to a client here (the worst case).
    QVERIFY(!sync::nextcloudDavUrl(u"https://jan:%1@chmura.example.pl"_s.arg(kPassword), u"jan"_s, kFolder)
                     .contains(kPassword));
    const QString token = QString::fromLatin1(sync::basicAuth(u"jan"_s, kPassword).mid(6));

    const QString root = dir.filePath(u"vault"_s);
    QVERIFY(writeFileBehind(root, u"a.md"_s, "a\n"));
    QVERIFY(writeFileBehind(root, u"Żółw.md"_s, "żółw\n"));
    Vault vault(root, QString());
    QVERIFY(vault.open());
    LocalStore store{QString()};
    WebDavClient client(base, u"jan"_s, kPassword, &server);
    client.setTimeout(150);
    SyncEngine engine(&vault, &client, &store, u"laptop"_s);
    LocalStore historyStore(dir.filePath(u"sync-history.json"_s));
    SyncHistory history(&historyStore);
    QList<SyncReport> reports;
    const auto run = [&] {
        SyncReport r = Device::runSync(engine);
        history.add(r);
        reports.append(r);
    };

    // 1. Wrong password (401).
    server.hooks = {[](const HttpRequest &, const QString &) -> std::optional<HttpResponse> {
        return FakeWebDavServer::status(401);
    }};
    run();
    QVERIFY(reports.last().remoteError.is(WebDavErrorCode::Auth));
    // 2. A server error for one file (500), with the request headers logged by mistake.
    QList<QPair<QByteArray, QByteArray>> headers;
    server.hooks = {[&](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
        headers = request.headers;
        if (request.method == "PUT" && path.endsWith(u"/Żółw.md"_s))
            return FakeWebDavServer::status(500);
        return std::nullopt;
    }};
    run();
    QCOMPARE(reports.last().errors.size(), 1);
    qWarning() << "request headers:" << headers;
    qCDebug(logging::lcNet) << "headers" << headers;
    // 3. A stalled connection: timeout.
    server.hooks = {[](const HttpRequest &, const QString &) { return FakeWebDavServer::noResponse(); }};
    run();
    QVERIFY(reports.last().remoteError.is(WebDavErrorCode::Timeout));
    // 4. A login page instead of WebDAV: parse error.
    server.hooks = {[](const HttpRequest &request, const QString &) -> std::optional<HttpResponse> {
        if (request.method != "PROPFIND")
            return std::nullopt;
        HttpResponse r = FakeWebDavServer::status(200);
        r.headers.insert("content-type", "text/html");
        r.body = "<html><form>login</form></html>";
        return r;
    }};
    run();
    QVERIFY(reports.last().remoteError.is(WebDavErrorCode::Parse));
    QVERIFY(historyStore.flush());

    logger->flush();
    QStringList ring;
    for (const logging::Entry &e : logger->entries())
        ring.append(e.message);
    QString files;
    for (const QString &f : logger->files())
        files += readAll(f);
    const QString exported = logger->exportText({u"Brulion — rejestr zdarzeń"_s});
    QString captured;
    {
        QMutexLocker locker(&g_mutex);
        captured = g_stderr.join(u'\n');
    }
    QString reportTexts;
    for (const SyncReport &r : reports) {
        reportTexts += r.failureMessage() + u'\n';
        for (const SyncFileError &e : r.errors)
            reportTexts += e.message + u'\n';
    }
    const QString historyJson = readAll(dir.filePath(u"sync-history.json"_s));
    logging::Logger::uninstall();
    qInstallMessageHandler(qtest);

    // The scenarios really were logged (so the checks below look at something).
    const QString all = ring.join(u'\n');
    QVERIFY2(all.contains(u"#2 Żółw.md not synced: server (HTTP 500)"_s), qPrintable(all));
    QVERIFY2(all.contains(u"timed out after"_s), qPrintable(all));
    QVERIFY2(all.contains(u"not a WebDAV answer"_s), qPrintable(all));
    QVERIFY2(all.contains(u"#1 failed after"_s) && all.contains(u"auth"_s), qPrintable(all));
    QVERIFY2(all.contains(u"PROPFIND (base) -> 401"_s), qPrintable(all)); // detailed per-request lines
    QVERIFY(all.contains(u"request headers:"_s));
    QCOMPARE(history.runs().size(), 4);
    QVERIFY(historyJson.contains(u"\"auth\""_s));

    const QList<QPair<QString, QString>> places{
            {u"ring"_s, all},        {u"files"_s, files},         {u"export"_s, exported},
            {u"stderr"_s, captured}, {u"history"_s, historyJson}, {u"reports"_s, reportTexts}};
    for (const auto &[name, text] : places) {
        QVERIFY2(!text.isEmpty(), qPrintable(name));
        QVERIFY2(!text.contains(kPassword), qPrintable(name + u": "_s + text));
        QVERIFY2(!text.contains(encoded), qPrintable(name + u": "_s + text));
        QVERIFY2(!text.contains(encoded.toLower()), qPrintable(name));
        QVERIFY2(!text.contains(token), qPrintable(name + u": "_s + text));
        QVERIFY2(!text.contains(u"Authorization: Basic"_s, Qt::CaseInsensitive), qPrintable(name));
    }
}

QTEST_GUILESS_MAIN(TestSyncSecrets)
#include "tst_sync_secrets.moc"
