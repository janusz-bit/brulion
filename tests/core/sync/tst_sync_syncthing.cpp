// SyncthingClient against a fake REST API; folder/path helpers; the .stignore template.
#include "synctest.h"

#include "sync/syncthing.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#include <limits>

using namespace synctest;

namespace {

template <typename Result, typename Start>
Result await(Start &&start)
{
    auto result = std::make_shared<std::optional<Result>>();
    start([result](const Result &r) { *result = r; });
    waitFor([result] { return result->has_value(); });
    return result->value_or(Result{});
}

HttpResponse json(const QJsonDocument &doc, int status = 200)
{
    HttpResponse r;
    r.status = status;
    r.headers.insert("content-type", "application/json");
    r.body = doc.toJson(QJsonDocument::Compact);
    return r;
}

QJsonDocument object(const QJsonObject &o)
{
    return QJsonDocument(o);
}

const QJsonArray kFolders = {
    QJsonObject{{u"id"_s, u"notatki-1"_s}, {u"label"_s, u"Notatki"_s}, {u"path"_s, u"/home/jan/Sync/Notatki"_s},
                {u"type"_s, u"sendreceive"_s}, {u"paused"_s, false}},
    QJsonObject{{u"id"_s, u"sync"_s}, {u"label"_s, u"Sync"_s}, {u"path"_s, u"~/Sync/"_s}, {u"type"_s, u"sendreceive"_s}},
    QJsonObject{{u"id"_s, u"android"_s}, {u"label"_s, QString()}, {u"path"_s, u"/storage/emulated/0/Brulion"_s}},
    QJsonObject{{u"label"_s, u"bez id"_s}, {u"path"_s, u"/x"_s}},
};

/// A fake Syncthing REST API (API key "klucz").
class FakeSyncthing : public QObject, public HttpTransport {
public:
    QString apiKey = u"klucz"_s;
    bool down = false;
    QList<HttpRequest> requests;

    void send(const HttpRequest &request, HttpCallback done) override
    {
        requests.append(request);
        HttpResponse r = handle(request);
        QTimer::singleShot(0, this, [done, r] { done(r); });
    }

private:
    HttpResponse handle(const HttpRequest &request) const
    {
        if (down) {
            HttpResponse r;
            r.networkError = u"Connection refused"_s;
            return r;
        }
        if (request.header("x-api-key") != apiKey.toUtf8())
            return json(object({{u"error"_s, u"Forbidden"_s}}), 403);
        const QString path = request.url.path();
        if (path == u"/rest/system/ping"_s)
            return json(object({{u"ping"_s, u"pong"_s}}));
        if (path == u"/rest/config/folders"_s)
            return json(QJsonDocument(kFolders));
        if (path == u"/rest/system/connections"_s) {
            const QJsonObject connections{{u"AAA"_s, QJsonObject{{u"connected"_s, true}}},
                                          {u"BBB"_s, QJsonObject{{u"connected"_s, false}}},
                                          {u"CCC"_s, QJsonObject{{u"connected"_s, true}}}};
            return json(object({{u"total"_s, QJsonObject()}, {u"connections"_s, connections}}));
        }
        if (path == u"/rest/db/status"_s) {
            const QString folder = QUrlQuery(request.url).queryItemValue(u"folder"_s, QUrl::FullyDecoded);
            if (folder == u"notatki-1"_s)
                return json(object({{u"state"_s, u"syncing"_s}, {u"needBytes"_s, 250}, {u"globalBytes"_s, 1000},
                                    {u"errors"_s, 1}, {u"pullErrors"_s, 2}}));
            if (folder == u"idle"_s)
                return json(object({{u"state"_s, u"idle"_s}, {u"needBytes"_s, 0}, {u"globalBytes"_s, 0}, {u"errors"_s, 0}}));
            if (folder == u"ogromny"_s) // garbage: values outside every integer range, negative counts
                return json(object({{u"state"_s, u"syncing"_s}, {u"needBytes"_s, 1e30}, {u"globalBytes"_s, 1e300},
                                    {u"errors"_s, 1e20}, {u"pullErrors"_s, -5}}));
            if (folder == u"zażółć"_s)
                return json(object({{u"state"_s, u"scanning"_s}, {u"needBytes"_s, 0}, {u"globalBytes"_s, 10}}));
            return json(object({{u"error"_s, u"no such folder"_s}}), 404);
        }
        return json(object({}), 404);
    }
};

SyncthingFolder folder(const QString &path, const QString &id = {})
{
    return SyncthingFolder{id.isEmpty() ? path : id, id, path, std::nullopt};
}

} // namespace

class TestSyncthing : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void usesTheDefaultUrlAndSendsTheApiKey()
    {
        FakeSyncthing fake;
        SyncthingClient c(u"klucz"_s, &fake);
        QCOMPARE(c.url(), u"http://127.0.0.1:8384"_s);
        QVERIFY(await<bool>([&](auto cb) { c.ping(cb); }));
        QCOMPARE(fake.requests.first().url.toString(), u"http://127.0.0.1:8384/rest/system/ping"_s);
        QCOMPARE(fake.requests.first().header("x-api-key"), QByteArray("klucz"));
        QCOMPARE(fake.requests.first().header("accept"), QByteArray("application/json"));
    }

    void listsFolders()
    {
        FakeSyncthing fake;
        SyncthingClient c(u"klucz"_s, &fake, u"http://127.0.0.1:8384/"_s);
        const auto r = await<SyncthingClient::FoldersResult>([&](auto cb) { c.folders(cb); });
        QVERIFY(!r.error.isError());
        const QList<SyncthingFolder> expected = {
            {u"notatki-1"_s, u"Notatki"_s, u"/home/jan/Sync/Notatki"_s, false},
            {u"sync"_s, u"Sync"_s, u"~/Sync/"_s, std::nullopt},
            {u"android"_s, u"android"_s, u"/storage/emulated/0/Brulion"_s, std::nullopt},
        };
        QCOMPARE(r.folders, expected);
    }

    void countsConnectedDevices()
    {
        FakeSyncthing fake;
        SyncthingClient c(u"klucz"_s, &fake);
        const auto r = await<SyncthingClient::ConnectionsResult>([&](auto cb) { c.connections(cb); });
        QCOMPARE(r.connected, 2);
        QVERIFY(!r.error.isError());
    }

    void folderStatusWithCompletionAndSummedErrors()
    {
        FakeSyncthing fake;
        SyncthingClient c(u"klucz"_s, &fake);
        const SyncthingStatus s = await<SyncthingStatus>([&](auto cb) { c.status(u"notatki-1"_s, cb); });
        SyncthingStatus expected;
        expected.state = u"syncing"_s;
        expected.needBytes = 250;
        expected.globalBytes = 1000;
        expected.completion = 75;
        expected.errors = 3;
        expected.folderId = u"notatki-1"_s;
        expected.connected = 2;
        QCOMPARE(s, expected);
        QCOMPARE(await<SyncthingStatus>([&](auto cb) { c.status(u"idle"_s, cb); }).completion, 100.0);
    }

    void outOfRangeNumbersAreClamped()
    {
        FakeSyncthing fake;
        SyncthingClient c(u"klucz"_s, &fake);
        const SyncthingStatus s = await<SyncthingStatus>([&](auto cb) { c.status(u"ogromny"_s, cb); });
        QCOMPARE(s.state, u"syncing"_s);
        QCOMPARE(s.needBytes, std::numeric_limits<qint64>::max());
        QCOMPARE(s.globalBytes, std::numeric_limits<qint64>::max());
        QCOMPARE(s.errors, std::numeric_limits<int>::max());
        QVERIFY(s.completion >= 0.0 && s.completion <= 100.0);
    }

    void folderIdsAreEncoded()
    {
        FakeSyncthing fake;
        SyncthingClient c(u"klucz"_s, &fake);
        const SyncthingStatus s = await<SyncthingStatus>([&](auto cb) { c.status(u"zażółć"_s, cb); });
        QCOMPARE(s.state, u"scanning"_s);
        QCOMPARE(s.completion, 100.0);
        QVERIFY(fake.requests.last().url.toString(QUrl::FullyEncoded).contains(u"folder=za%C5%BC%C3%B3%C5%82%C4%87"_s)
                || fake.requests.first().url.toString(QUrl::FullyEncoded).contains(u"folder=za%C5%BC%C3%B3%C5%82%C4%87"_s));
    }

    void usesTheConfiguredFolderWithoutAnId()
    {
        FakeSyncthing fake;
        SyncthingClient c(u"klucz"_s, &fake);
        c.setFolderId(u"notatki-1"_s);
        QCOMPARE(await<SyncthingStatus>([&](auto cb) { c.status(QString(), cb); }).state, u"syncing"_s);
    }

    void withoutAFolderReportsDaemonReachability()
    {
        FakeSyncthing fake;
        SyncthingClient c(u"klucz"_s, &fake);
        SyncthingStatus expected;
        expected.state = u"idle"_s;
        expected.completion = 100;
        expected.connected = 2;
        QCOMPARE(await<SyncthingStatus>([&](auto cb) { c.status(QString(), cb); }), expected);
    }

    void everyFailureMapsToOffline()
    {
        FakeSyncthing downFake;
        downFake.down = true;
        SyncthingClient down(u"klucz"_s, &downFake);
        QCOMPARE(await<SyncthingStatus>([&](auto cb) { down.status(u"notatki-1"_s, cb); }).state, u"offline"_s);
        const SyncthingStatus daemon = await<SyncthingStatus>([&](auto cb) { down.status(QString(), cb); });
        QCOMPARE(daemon.state, u"offline"_s);
        QVERIFY(!daemon.connected);
        QCOMPARE(daemon.error, u"unreachable"_s);
        QVERIFY(!await<bool>([&](auto cb) { down.ping(cb); }));

        FakeSyncthing fake;
        SyncthingClient wrongKey(u"zły"_s, &fake);
        const SyncthingStatus st = await<SyncthingStatus>([&](auto cb) { wrongKey.status(u"notatki-1"_s, cb); });
        QCOMPARE(st.state, u"offline"_s);
        QVERIFY(st.error.contains(u"403"_s));
        QVERIFY(st.error.contains(u"API key"_s));
        SyncthingClient unknown(u"klucz"_s, &fake);
        QCOMPARE(await<SyncthingStatus>([&](auto cb) { unknown.status(u"nie-ma"_s, cb); }).state, u"offline"_s);
    }

    void foldersFailsWhenUnreachableOrUnauthorised()
    {
        FakeSyncthing downFake;
        downFake.down = true;
        SyncthingClient down(u"klucz"_s, &downFake);
        const auto unreachable = await<SyncthingClient::FoldersResult>([&](auto cb) { down.folders(cb); });
        QVERIFY(unreachable.error.isError());
        QCOMPARE(unreachable.error.status, 0);
        QVERIFY(unreachable.error.message.contains(u"not reachable"_s));
        FakeSyncthing fake;
        SyncthingClient wrong(u"x"_s, &fake);
        const auto forbidden = await<SyncthingClient::FoldersResult>([&](auto cb) { wrong.folders(cb); });
        QCOMPARE(forbidden.error.status, 403);
    }

    void rejectsInvalidJson()
    {
        class HtmlTransport : public QObject, public HttpTransport {
        public:
            void send(const HttpRequest &, HttpCallback done) override
            {
                QTimer::singleShot(0, this, [done] {
                    HttpResponse r;
                    r.status = 200;
                    r.body = "<html>";
                    done(r);
                });
            }
        } transport;
        SyncthingClient c(u"k"_s, &transport);
        const auto r = await<SyncthingClient::FoldersResult>([&](auto cb) { c.folders(cb); });
        QVERIFY(r.error.message.contains(u"invalid JSON"_s));
    }

    void findsTheFolderContainingAPath()
    {
        FakeSyncthing fake;
        SyncthingClient c(u"klucz"_s, &fake);
        using Found = std::pair<std::optional<SyncthingFolder>, SyncthingError>;
        const auto find = [&](const QString &path) {
            return await<Found>([&](auto cb) {
                c.findFolderForPath(path, [cb](const std::optional<SyncthingFolder> &f, const SyncthingError &e) {
                    cb(Found{f, e});
                });
            });
        };
        QCOMPARE(find(u"/home/jan/Sync/Notatki/journals"_s).first->id, u"notatki-1"_s);
        QCOMPARE(find(u"/home/jan/Sync/Inne"_s).first->id, u"sync"_s);
        QCOMPARE(find(u"/storage/emulated/0/Brulion/"_s).first->id, u"android"_s);
        QVERIFY(!find(u"/home/jan/Dokumenty"_s).first);
    }

    // --- pure helpers --------------------------------------------------------------------------
    void normalizeFsPath()
    {
        QCOMPARE(sync::normalizeFsPath(u"/home/jan/Sync//Notatki/"_s), u"/home/jan/Sync/Notatki"_s);
        QCOMPARE(sync::normalizeFsPath(u"~/Sync"_s, u"/home/jan/"_s), u"/home/jan/Sync"_s);
        QCOMPARE(sync::normalizeFsPath(u"~"_s, u"/home/jan"_s), u"/home/jan"_s);
        QCOMPARE(sync::normalizeFsPath(u"C:\\Users\\Jan\\Notatki\\"_s), u"c:/Users/Jan/Notatki"_s);
        QCOMPARE(sync::normalizeFsPath(u"/"_s), u"/"_s);
        QCOMPARE(sync::normalizeFsPath(u"~/x"_s), u"~/x"_s);
        QCOMPARE(sync::normalizeFsPath(u"/Zażół"_s.normalized(QString::NormalizationForm_D)), u"/Zażół"_s);
    }

    void guessHome()
    {
        QCOMPARE(sync::guessHome(u"/home/jan/Sync"_s), std::optional<QString>(u"/home/jan"_s));
        QCOMPARE(sync::guessHome(u"/Users/ania/x"_s), std::optional<QString>(u"/Users/ania"_s));
        QCOMPARE(sync::guessHome(u"/root"_s), std::optional<QString>(u"/root"_s));
        QCOMPARE(sync::guessHome(u"/data/data/pl.brulion/files"_s), std::optional<QString>(u"/data/data/pl.brulion"_s));
        QCOMPARE(sync::guessHome(u"C:\\Users\\Łucja\\Notatki"_s), std::optional<QString>(u"C:/Users/Łucja"_s));
        QVERIFY(!sync::guessHome(u"/storage/emulated/0/x"_s));
        QVERIFY(!sync::guessHome(u"/rooted"_s));
    }

    void findFolderPrefersTheLongestPrefixAndRespectsSegments()
    {
        const QList<SyncthingFolder> folders = {folder(u"/data"_s), folder(u"/data/notes"_s), folder(u"/data/notes-old"_s)};
        QCOMPARE(sync::findFolderForPath(folders, u"/data/notes/a.md"_s)->path, u"/data/notes"_s);
        QCOMPARE(sync::findFolderForPath(folders, u"/data/notes-old"_s)->path, u"/data/notes-old"_s);
        QCOMPARE(sync::findFolderForPath(folders, u"/data/notesy"_s)->path, u"/data"_s);
        QVERIFY(!sync::findFolderForPath(folders, u"/other"_s));
        QVERIFY(!sync::findFolderForPath({}, u"/data"_s));
        QCOMPARE(sync::findFolderForPath({folder(u"/"_s)}, u"/cokolwiek"_s)->path, u"/"_s);
    }

    void findFolderIsCaseInsensitiveOnWindowsAndExpandsHome()
    {
        QVERIFY(sync::findFolderForPath({folder(u"C:\\Users\\Jan\\Sync"_s)}, u"c:/users/jan/sync/Notatki"_s));
        QVERIFY(!sync::findFolderForPath({folder(u"/Home/Jan"_s)}, u"/home/jan/x"_s)); // POSIX: case matters
        QVERIFY(sync::findFolderForPath({folder(u"~/Notatki"_s)}, u"/srv/x/Notatki/a"_s, u"/srv/x"_s));
        QVERIFY(!sync::findFolderForPath({folder(u"~/Notatki"_s)}, u"/storage/emulated/0/Notatki"_s));
    }

    void completionPercent()
    {
        QCOMPARE(sync::completionPercent(0, 0), 100.0);
        QCOMPARE(sync::completionPercent(5, 0), 0.0);
        QCOMPARE(sync::completionPercent(1, 3), 66.6);
        QCOMPARE(sync::completionPercent(10, 5), 0.0);
        QCOMPARE(sync::completionPercent(250, 1000), 75.0);
    }

    void stignoreTemplate()
    {
        const QStringList lines = sync::stignoreTemplate().split(u'\n');
        for (const QString &pattern : {u"/.trash"_s, u"(?d).DS_Store"_s, u"(?d)Thumbs.db"_s, u"/.obsidian/workspace*.json"_s,
                                       u"/logseq/bak"_s, u"/logseq/.recycle"_s, u"*.tmp"_s, u"(?d).brulion-tmp-*"_s})
            QVERIFY2(lines.contains(pattern), qPrintable(pattern));
        bool polishComment = false;
        for (const QString &line : lines) {
            QCOMPARE(line, QString(line).remove(QRegularExpression(u"\\s+$"_s)));
            polishComment = polishComment || (line.startsWith(u"// "_s) && line.contains(u"notatk"_s));
        }
        QVERIFY(polishComment);
        QVERIFY(sync::stignoreTemplate().contains(u"każde urządzenie"_s));
    }
};

QTEST_GUILESS_MAIN(TestSyncthing)
#include "tst_sync_syncthing.moc"
