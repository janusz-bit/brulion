// Search by meaning as the app does it (Workspace.semantic, SearchResultModel; docs/UI.md "Search by
// meaning") on a real Workspace, with a fake embedding model: words grouped into a few "meanings"
// (szarlotka, ciasto, jabłka and apple, cake are one), so the hybrid ranking is known in advance.
//
//   * without the plugin or the model, or with the setting off: exactly the full-text search;
//   * on: the notebook is embedded in the background, the hits fused with the notes close in
//     meaning, those without the words marked byMeaning;
//   * an edit embeds that note again, a deleted note is forgotten, the vectors are kept on the
//     device and read back when the notebook opens again (nothing embedded again);
//   * a model that fails: full text, the state "failed"; removing the model deletes its file and the
//     kept vectors; turning it on downloads the model (a local HTTP server).
#include "searchmodel.h"

#include "workspace.h"

#include "semantic/models.h"

#include "fileserver.h"
#include "testtiming.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

using namespace Qt::StringLiterals;
using namespace brulion;
using brulion::content::SearchResultModel;
using brulion::shell::SemanticSearch;
using brulion::shell::Workspace;

namespace {

constexpr int Dim = 8;

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QStringList column(const QAbstractItemModel &model, const QByteArray &role)
{
    const int r = model.roleNames().key(role, -1);
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i)
        out.append(model.data(model.index(i, 0), r).toString());
    return out;
}

QVariant value(const QAbstractItemModel &model, int row, const QByteArray &role)
{
    return model.data(model.index(row, 0), model.roleNames().key(role, -1));
}

/// The "meaning" of a word: 0 baking, 1 tyres, 2 dogs; any other word one of 3–7 by its letters.
int meaningOf(const QString &word)
{
    static const QStringList baking{u"szarlotka"_s, u"szarlotki"_s, u"ciasto"_s, u"ciasta"_s, u"jabłka"_s,
                                    u"jabłkami"_s, u"jabłek"_s, u"apple"_s, u"cake"_s, u"pie"_s, u"kruszonka"_s};
    static const QStringList tyres{u"opony"_s, u"oponę"_s, u"opon"_s, u"tyres"_s, u"tires"_s, u"wymiana"_s};
    static const QStringList dogs{u"pies"_s, u"psem"_s, u"dog"_s, u"spacer"_s};
    if (baking.contains(word))
        return 0;
    if (tyres.contains(word))
        return 1;
    if (dogs.contains(word))
        return 2;
    quint32 h = 2166136261u;
    for (QChar c : word)
        h = (h ^ c.unicode()) * 16777619u;
    return 3 + int(h % (Dim - 3));
}

semantic::Vector fakeVector(const QString &text)
{
    semantic::Vector v(Dim, 0.0f);
    static const QRegularExpression words(u"[\\p{L}]+"_s);
    for (auto it = words.globalMatch(text.toLower()); it.hasNext();)
        v[meaningOf(it.next().captured())] += 1.0f;
    if (std::ranges::all_of(v, [](float x) { return x == 0; }))
        v[Dim - 1] = 1;
    semantic::normalize(v);
    return v;
}

/// What a test counts of the fake model (shared with the runners it opened).
struct Counts {
    std::atomic<int> opened = 0;
    std::atomic<int> passages = 0;
    std::atomic<int> queries = 0;
};

class FakeRunner : public semantic::SemanticRunner {
public:
    explicit FakeRunner(std::shared_ptr<Counts> counts) : m_counts(std::move(counts)) {}
    int dimension() const override { return Dim; }
    QString device() const override { return u"fake"_s; }
    QList<semantic::Vector> embedPassages(const QStringList &passages) override
    {
        m_counts->passages += int(passages.size());
        QList<semantic::Vector> out;
        for (const QString &p : passages)
            out.append(fakeVector(p));
        return out;
    }
    semantic::Vector embedQuery(const QString &query) override
    {
        ++m_counts->queries;
        return fakeVector(query);
    }

private:
    std::shared_ptr<Counts> m_counts;
};

QByteArray sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

} // namespace

class TestContentSemantic : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString m_root;
    QString m_models;
    QString m_cache;
    QByteArray m_modelBytes = QByteArray(4096, 'm');
    std::shared_ptr<Counts> m_counts;
    std::shared_ptr<std::atomic<bool>> m_fail;
    QHash<QString, bool> m_settings;

    ai::LocalModel fakeModel(const QString &url = u"http://127.0.0.1:1/fake-e5.gguf"_s) const
    {
        ai::LocalModel m;
        m.id = u"fake-e5"_s;
        m.name = u"Fake E5"_s;
        m.maker = u"Test"_s;
        m.format = u"e5"_s;
        m.file = {u"fake-e5.gguf"_s, QUrl(url), sha256(m_modelBytes), m_modelBytes.size()};
        m.license = u"MIT"_s;
        return m;
    }

    /// The device's side of search by meaning for a new Workspace: the fake model (its file in
    /// place when `here`), this test's folders and settings, no delays.
    SemanticSearch::Options options(bool here, const QString &url = u"http://127.0.0.1:1/fake-e5.gguf"_s)
    {
        if (here)
            writeRaw(m_models + u"/fake-e5.gguf"_s, m_modelBytes);
        SemanticSearch::Options o;
        o.folder = m_models;
        o.cacheDir = m_cache;
        o.models = {fakeModel(url)};
        o.startDelayMs = 0;
        o.changeDelayMs = 0;
        o.open = [counts = m_counts, fail = m_fail](const semantic::SemanticRunner::Options &,
                                                    QString *error) -> std::unique_ptr<semantic::SemanticRunner> {
            if (fail->load()) {
                *error = u"the fake model is broken"_s;
                return nullptr;
            }
            ++counts->opened;
            return std::make_unique<FakeRunner>(counts);
        };
        o.setting = [this](const QString &key, const QVariant &fallback) {
            return m_settings.contains(key) ? QVariant(m_settings.value(key)) : fallback;
        };
        o.setSetting = [this](const QString &key, const QVariant &v) { m_settings.insert(key, v.toBool()); };
        return o;
    }

    std::unique_ptr<Workspace> open(const SemanticSearch::Options &o)
    {
        auto ws = std::make_unique<Workspace>();
        ws->setClock([] { return QDateTime(QDate(2026, 10, 14), QTime(10, 30)); });
        ws->semantic()->configure(o);
        QString error;
        if (!ws->open(m_root, m_tmp.filePath(u"state-%1"_s.arg(m_n)), &error))
            qWarning() << error;
        ws->finishIndexing();
        return ws;
    }

    static void waitReady(SemanticSearch *s)
    {
        QTRY_VERIFY_WITH_TIMEOUT(s->state() == u"ready"_s && s->idle(), brulion::test::kRoomyMs);
    }

    /// The rows of `query` once the model has its vector (the list waits for it a moment; a loaded
    /// machine may take longer — the test waits for the vector itself).
    static QStringList search(SearchResultModel &model, const QString &query)
    {
        model.setQuery(query);
        model.refreshNow();
        SemanticSearch *s = model.workspace()->semantic();
        if (s->active()) {
            [&] { QTRY_VERIFY_WITH_TIMEOUT(s->queryVector(query).has_value(), brulion::test::kRoomyMs); }();
            model.refreshNow();
        }
        return column(model, "path");
    }

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_tmp.isValid());
        ++m_n;
        m_root = m_tmp.filePath(u"vault-%1"_s.arg(m_n));
        m_models = m_tmp.filePath(u"models-%1"_s.arg(m_n));
        m_cache = m_tmp.filePath(u"cache-%1"_s.arg(m_n));
        m_counts = std::make_shared<Counts>();
        m_fail = std::make_shared<std::atomic<bool>>(false);
        m_settings.clear();
        QVERIFY(writeRaw(m_root + u"/Szarlotka babci.md"_s, "- jabłka, kruszonka, cynamon\n- piec 45 minut\n"));
        QVERIFY(writeRaw(m_root + u"/Ciasto drożdżowe.md"_s, "- mąka, drożdże, mleko\n- ciasto rośnie godzinę\n"));
        QVERIFY(writeRaw(m_root + u"/Samochód.md"_s, "- wymiana opon zimowych w listopadzie\n"));
        QVERIFY(writeRaw(m_root + u"/Zakupy.md"_s, "- mleko, chleb, masło\n"));
        QVERIFY(writeRaw(m_root + u"/journals/2026-10-13.md"_s, "- spacer z psem nad rzeką\n"));
    }

    // No plugin and no model: the setting cannot work, search is the full text exactly.
    void withoutModelFullText()
    {
        // A build without the plugin: nothing configured but the folders.
        SemanticSearch::Options o = options(false);
        o.open = {};
        o.pluginDirs = {m_tmp.filePath(u"no-plugin-here"_s)};
        auto ws = open(o);
        SemanticSearch *s = ws->semantic();
        QVERIFY(!s->available());
        QCOMPARE(s->state(), u"unavailable"_s);
        s->setEnabled(true);
        QVERIFY(!s->active());
        SearchResultModel model;
        model.setWorkspace(ws.get());
        model.setQuery(u"ciasto"_s);
        model.refreshNow(); // (no wait: nothing to wait for)
        QCOMPARE(column(model, "path"), QStringList{u"Ciasto drożdżowe.md"_s});
        QCOMPARE(value(model, 0, "byMeaning").toBool(), false);
        model.setQuery(u"apple cake"_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 0);

        // The plugin but no model on the device: on means "download it", search stays full text.
        m_settings.clear();
        auto ws2 = open(options(false));
        QVERIFY(ws2->semantic()->available());
        QCOMPARE(ws2->semantic()->state(), u"off"_s);
        ws2->semantic()->setEnabled(true);
        QCOMPARE(ws2->semantic()->state(), u"noModel"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!ws2->semantic()->downloading(), brulion::test::kRoomyMs); // (nothing at :1)
        QVERIFY(!ws2->semantic()->downloadError().isEmpty());
        SearchResultModel model2;
        model2.setWorkspace(ws2.get());
        model2.setQuery(u"ciasto"_s);
        model2.refreshNow();
        QCOMPARE(column(model2, "path"), QStringList{u"Ciasto drożdżowe.md"_s});
        QCOMPARE(m_counts->opened.load(), 0);
    }

    // Off by default; on: the notebook embedded in the background, the hits fused.
    void hybridSearch()
    {
        auto ws = open(options(true));
        SemanticSearch *s = ws->semantic();
        QVERIFY(s->available());
        QVERIFY(s->modelHere());
        QCOMPARE(s->state(), u"off"_s);
        QVERIFY(!s->enabled());
        SearchResultModel model;
        model.setWorkspace(ws.get());
        QCOMPARE(search(model, u"apple cake"_s), QStringList());

        s->setEnabled(true);
        QVERIFY(m_settings.value(u"search/semantic"_s));
        waitReady(s);
        QVERIFY(s->active());
        QCOMPARE(s->notesTotal(), 5);
        QCOMPARE(s->notesDone(), 5);
        QCOMPARE(s->embeddedNotes(), 5);
        QCOMPARE(s->index().size(), 5);

        // Words found nowhere: the notes close in meaning, marked so.
        QStringList rows = search(model, u"apple cake"_s);
        QVERIFY(rows.size() >= 2);
        QCOMPARE(QSet<QString>(rows.cbegin(), rows.cbegin() + 2),
                 (QSet<QString>{u"Szarlotka babci.md"_s, u"Ciasto drożdżowe.md"_s}));
        QVERIFY(value(model, 0, "byMeaning").toBool());
        QVERIFY(!value(model, 0, "excerpt").toString().isEmpty());
        // The words and the meaning: the note with the word and the one with its meaning on top (by
        // meaning first — 0.8 of the score —, the words breaking the tie), only the latter marked.
        rows = search(model, u"ciasto"_s);
        QCOMPARE(QSet<QString>(rows.cbegin(), rows.cbegin() + 2),
                 (QSet<QString>{u"Szarlotka babci.md"_s, u"Ciasto drożdżowe.md"_s}));
        for (int i = 0; i < rows.size(); ++i)
            QCOMPARE(value(model, i, "byMeaning").toBool(), rows.at(i) != u"Ciasto drożdżowe.md"_s);
        // Typed as the note says it, the note with the words wins.
        rows = search(model, u"ciasto rośnie godzinę"_s);
        QCOMPARE(rows.first(), u"Ciasto drożdżowe.md"_s);
        QCOMPARE(value(model, 0, "byMeaning").toBool(), false);
        // Scopes still apply: journals only.
        model.setScope(u"journals"_s);
        rows = search(model, u"dog"_s);
        QCOMPARE(rows.first(), u"journals/2026-10-13.md"_s);
        for (const QString &row : rows)
            QVERIFY(row.startsWith(u"journals/"_s));
        model.setScope(u"all"_s);
        // A query's vector is asked once (kept for the next refresh).
        const int before = m_counts->queries.load();
        search(model, u"ciasto"_s);
        QCOMPARE(m_counts->queries.load(), before);

        // Off again: exactly the full text.
        s->setEnabled(false);
        QVERIFY(!s->active());
        model.setQuery(u"apple cake"_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 0);
        model.setQuery(u"ciasto"_s);
        model.refreshNow();
        QCOMPARE(column(model, "path"), QStringList{u"Ciasto drożdżowe.md"_s});
    }

    // An edit embeds that note again, a deleted one is forgotten; the vectors are kept on the
    // device and read back next time (nothing embedded again).
    void incrementalAndKept()
    {
        m_settings.insert(u"search/semantic"_s, true);
        QString file;
        {
            auto ws = open(options(true));
            SemanticSearch *s = ws->semantic();
            waitReady(s);
            QCOMPARE(s->embeddedNotes(), 5);
            file = s->indexFile();
            QVERIFY(file.startsWith(m_cache));
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(file), brulion::test::kRoomyMs);

            // An edit: that note alone, a moment later; its new meaning is searched.
            QVERIFY(ws->write(u"Zakupy.md"_s, u"- opony na zimę\n"_s));
            QTRY_VERIFY_WITH_TIMEOUT(s->embeddedNotes() == 6 && s->idle(), brulion::test::kRoomyMs);
            SearchResultModel model;
            model.setWorkspace(ws.get());
            const QStringList rows = search(model, u"tyres"_s);
            QVERIFY(rows.mid(0, 2).contains(u"Zakupy.md"_s));
            QVERIFY(rows.mid(0, 2).contains(u"Samochód.md"_s));
            // A new note, then a deleted one.
            QVERIFY(ws->write(u"Pies.md"_s, u"- dog\n"_s));
            QTRY_VERIFY_WITH_TIMEOUT(s->index().contains(u"Pies.md"_s) && s->idle(), brulion::test::kRoomyMs);
            QCOMPARE(s->notesTotal(), 6);
            QVERIFY(!ws->deletePage(u"Ciasto drożdżowe.md"_s).isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!s->index().contains(u"Ciasto drożdżowe.md"_s), brulion::test::kRoomyMs);
            QTRY_VERIFY_WITH_TIMEOUT(s->idle(), brulion::test::kRoomyMs);
            QCOMPARE(s->embeddedNotes(), 7);
            QCOMPARE(s->index().size(), 5);
            QCOMPARE(s->notesTotal(), 5);
            ws->close(); // (writes what is not saved yet)
        }
        // Opened again: read from the device, nothing embedded.
        const int opened = m_counts->opened.load();
        auto ws = open(options(true));
        SemanticSearch *s = ws->semantic();
        waitReady(s);
        QCOMPARE(s->embeddedNotes(), 0);
        QCOMPARE(s->index().size(), 5);
        QVERIFY(s->index().contains(u"Pies.md"_s));
        QCOMPARE(m_counts->opened.load(), opened); // the model is not even loaded
        SearchResultModel model;
        model.setWorkspace(ws.get());
        QCOMPARE(search(model, u"dog"_s).first(), u"Pies.md"_s);
        QCOMPARE(m_counts->opened.load(), opened + 1); // (now: for the query)

        // A note changed while the app was closed: that one alone.
        ws->close();
        QVERIFY(writeRaw(m_root + u"/Samochód.md"_s, "- przegląd i olej\n"));
        auto ws3 = open(options(true));
        waitReady(ws3->semantic());
        QCOMPARE(ws3->semantic()->embeddedNotes(), 1);

        // A damaged file: built again.
        ws3->close();
        QVERIFY(writeRaw(file, "BRSV and then nothing useful"));
        auto ws4 = open(options(true));
        waitReady(ws4->semantic());
        QCOMPARE(ws4->semantic()->embeddedNotes(), 5);
    }

    // The notebook closes while the index thread has not written the last change yet (busy, or at
    // its idle priority on a loaded machine): the close writes it, and the index thread writes
    // nothing after — not over what came meanwhile (CI's Nix builder, 2026-11-22: a damaged file
    // written after the close was overwritten by the earlier save).
    void closeWritesWhatIsOnItsWay()
    {
        m_settings.insert(u"search/semantic"_s, true);
        auto hold = std::make_shared<std::atomic<bool>>(false);
        auto held = std::make_shared<QSemaphore>();
        auto go = std::make_shared<QSemaphore>();
        SemanticSearch::Options o = options(true);
        o.beforeQueuedWrite = [hold, held, go] {
            if (!hold->load())
                return;
            held->release();
            go->acquire();
        };
        auto ws = open(o);
        const auto letGo = qScopeGuard([go] { go->release(100); }); // (a failed check: no hang)
        SemanticSearch *s = ws->semantic();
        waitReady(s);
        const QString file = s->indexFile();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(file), brulion::test::kRoomyMs);
        const auto read = [&file] {
            QFile f(file);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        const QByteArray before = read();

        hold->store(true);
        QVERIFY(ws->write(u"Zakupy.md"_s, u"- opony na zimę\n"_s));
        QTRY_VERIFY_WITH_TIMEOUT(s->embeddedNotes() == 6 && s->idle(), brulion::test::kRoomyMs);
        QVERIFY(held->tryAcquire(1, brulion::test::kRoomyMs)); // its save waits on the index thread
        QCOMPARE(read(), before);
        ws->close();
        const QByteArray closed = read();
        QVERIFY(!closed.isEmpty());
        QVERIFY(closed != before); // the change, written by the close

        QVERIFY(writeRaw(file, "BRSV and then nothing useful"));
        go->release(100);
        ws.reset(); // (its threads end: whatever was queued there ran)
        QCOMPARE(read(), QByteArray("BRSV and then nothing useful"));
    }

    // A model that fails to load: the state says why, search is the full text.
    void failingModel()
    {
        m_fail->store(true);
        m_settings.insert(u"search/semantic"_s, true);
        auto ws = open(options(true));
        SemanticSearch *s = ws->semantic();
        QTRY_COMPARE_WITH_TIMEOUT(s->state(), u"failed"_s, brulion::test::kRoomyMs);
        QCOMPARE(s->error(), u"the fake model is broken"_s);
        QVERIFY(!s->active());
        SearchResultModel model;
        model.setWorkspace(ws.get());
        model.setQuery(u"ciasto"_s);
        model.refreshNow();
        QCOMPARE(column(model, "path"), QStringList{u"Ciasto drożdżowe.md"_s});
        // Off and on again: tried again.
        m_fail->store(false);
        s->setEnabled(false);
        s->setEnabled(true);
        waitReady(s);
        QVERIFY(s->active());
    }

    // Remove: the model's file and every notebook's kept vectors go; the setting is off.
    void removeModel()
    {
        m_settings.insert(u"search/semantic"_s, true);
        auto ws = open(options(true));
        SemanticSearch *s = ws->semantic();
        waitReady(s);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(s->indexFile()), brulion::test::kRoomyMs);
        QVERIFY(writeRaw(m_cache + u"/other-notebook-fake-e5.vectors"_s, "x"));
        QVERIFY(writeRaw(m_cache + u"/other-notebook-another-model.vectors"_s, "x"));
        s->remove();
        QVERIFY(!s->modelHere());
        QVERIFY(!s->enabled());
        QCOMPARE(s->state(), u"off"_s);
        QVERIFY(!QFileInfo::exists(m_models + u"/fake-e5.gguf"_s));
        QVERIFY(!QFileInfo::exists(s->indexFile()));
        QVERIFY(!QFileInfo::exists(m_cache + u"/other-notebook-fake-e5.vectors"_s));
        QVERIFY(QFileInfo::exists(m_cache + u"/other-notebook-another-model.vectors"_s));
    }

    // Turning it on downloads the model (checked), then reads the notebook.
    void downloadThenIndex()
    {
        FileServer server;
        server.files.insert("/fake-e5.gguf", m_modelBytes);
        auto ws = open(options(false, server.url(u"/fake-e5.gguf"_s).toString()));
        SemanticSearch *s = ws->semantic();
        QVERIFY(!s->modelHere());
        s->setEnabled(true);
        QVERIFY(s->downloading() || s->modelHere());
        QTRY_VERIFY_WITH_TIMEOUT(s->modelHere(), brulion::test::kRoomyMs);
        QVERIFY(s->downloadError().isEmpty());
        waitReady(s);
        QCOMPARE(s->embeddedNotes(), 5);
    }
};

QTEST_MAIN(TestContentSemantic)
#include "tst_content_semantic.moc"
