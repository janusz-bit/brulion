// LocalAi (src/app/shell/localai.h): the model on the device as the app keeps it — the chosen
// model and what Settings shows of it, its download (a local HTTP server) and removal, requests
// answered on the worker thread by a fake runner (opened once, again for another model, let go
// after a while without a request), a model with an adapter over another one's file, what is
// refused (no plugin, not downloaded, an unknown model), and the app closing while a model answers.

#include "localai.h"

#include "fileserver.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <mutex>

using namespace Qt::StringLiterals;
using namespace brulion;
using shell::LocalAi;

namespace {

QByteArray sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

/// What the fake runners did, seen from the test's thread.
struct Record {
    std::mutex mutex;
    QList<ai::LocalRunner::Options> opened;
    QList<ai::local::Prompt> prompts;
    QList<Qt::HANDLE> threads;
    std::atomic<int> open{0};       ///< runners alive
    std::atomic<bool> block{false}; ///< generate() waits until its request is cancelled
    std::atomic<bool> blocked{false};
};

class FakeRunner : public ai::LocalRunner {
public:
    FakeRunner(Record *record, const Options &options) : m_record(record), m_options(options) { ++m_record->open; }
    ~FakeRunner() override { --m_record->open; }
    int contextTokens() const override { return m_options.contextTokens; }
    bool sees() const override { return !m_options.projectorPath.isEmpty(); }
    QString device() const override { return u"fake"_s; }
    Result generate(const ai::local::Prompt &prompt, const Params &params) override
    {
        {
            const std::lock_guard lock(m_record->mutex);
            m_record->prompts << prompt;
            m_record->threads << QThread::currentThreadId();
        }
        Result r;
        while (m_record->block.load()) {
            m_record->blocked.store(true);
            if (params.cancel && params.cancel->load()) {
                r.status = Cancelled;
                return r;
            }
            QThread::msleep(2);
        }
        r.status = Ok;
        r.complete = true;
        r.promptTokens = 21;
        r.outputTokens = 3;
        r.pieces = {ai::local::Piece{u"Z urządzenia."_s, false, {}}};
        return r;
    }

private:
    Record *m_record;
    Options m_options;
};

struct Fixture {
    FileServer server;
    QTemporaryDir dir;
    Record record;
    QByteArray seesBytes = QByteArray(120 * 1000, 's');
    QByteArray projectorBytes = QByteArray(30 * 1000, 'p');
    QByteArray readsBytes = QByteArray(90 * 1000, 'r');
    ai::LocalModel sees;
    ai::LocalModel reads;

    Fixture()
    {
        server.files.insert("/sees.gguf", seesBytes);
        server.files.insert("/sees-projector.gguf", projectorBytes);
        server.files.insert("/reads.gguf", readsBytes);
        sees.id = u"sees"_s;
        sees.name = u"Widzący 2B"_s;
        sees.maker = u"Testy"_s;
        sees.format = u"lfm3"_s;
        sees.file = {u"sees.gguf"_s, server.url(u"/sees.gguf"_s), sha256(seesBytes), seesBytes.size()};
        sees.projector = {u"sees-projector.gguf"_s, server.url(u"/sees-projector.gguf"_s), sha256(projectorBytes),
                          projectorBytes.size()};
        sees.license = u"Licencja testowa"_s;
        sees.licenseUrl = QUrl(u"https://example.org/licencja"_s);
        sees.pageUrl = QUrl(u"https://example.org/widzacy"_s);
        reads = sees;
        reads.id = u"reads"_s;
        reads.name = u"Czytający 1.7B"_s;
        reads.file = {u"reads.gguf"_s, server.url(u"/reads.gguf"_s), sha256(readsBytes), readsBytes.size()};
        reads.projector = {};
    }

    LocalAi::Options options()
    {
        LocalAi::Options o;
        o.folder = dir.path() + u"/models"_s;
        o.stateFolder = dir.path() + u"/model-state"_s;
        o.spinWait = true;
        o.models = {sees, reads};
        Record *r = &record;
        o.open = [r](const ai::LocalRunner::Options &runnerOptions, QString *) -> std::unique_ptr<ai::LocalRunner> {
            const std::lock_guard lock(r->mutex);
            r->opened << runnerOptions;
            return std::make_unique<FakeRunner>(r, runnerOptions);
        };
        return o;
    }

    int openedCount()
    {
        const std::lock_guard lock(record.mutex);
        return int(record.opened.size());
    }
};

ai::Settings settingsFor(const QString &model)
{
    ai::Settings s;
    s.provider = u"local"_s;
    s.model = model;
    return s;
}

ai::Request question(const QString &words)
{
    ai::Request request;
    request.effort = u"low"_s;
    request.messages << ai::textMessage(u"user"_s, words);
    return request;
}

/// Sends a request and waits for its answer.
ai::Reply ask(LocalAi &local, const QString &model, const QString &words = u"Co słychać?"_s)
{
    ai::Reply got;
    bool done = false;
    const Qt::HANDLE here = QThread::currentThreadId();
    local.send(settingsFor(model), question(words), [&](const ai::Reply &reply) {
        got = reply;
        done = true;
        if (QThread::currentThreadId() != here)
            got.error = u"answered on another thread"_s;
    });
    if (done) // never before the event loop turns
        got.error = u"answered at once"_s;
    if (!QTest::qWaitFor([&] { return done; }, 20000))
        got.error = u"no answer"_s;
    return got;
}

bool download(LocalAi &local, const QString &id)
{
    QSignalSpy finished(&local, &LocalAi::downloadFinished);
    local.download(id);
    return (!finished.isEmpty() || finished.wait(20000)) && finished[0][1].toBool();
}

} // namespace

class TestLocalAi : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void howMuchItHolds()
    {
        QCOMPARE(LocalAi::ContextTokens, 16384); // on every device
        QCOMPARE(LocalAi::imageTokensFor(4096), 512);
        QCOMPARE(LocalAi::imageTokensFor(8192), 1024);
        QCOMPARE(LocalAi::imageTokensFor(16384), 1024);
        QCOMPARE(LocalAi::imageTokensFor(1024), 256);
    }

    void withoutAPlugin()
    {
        // A build without the model runner: nothing to choose in the settings, a request refused.
        QTemporaryDir dir;
        LocalAi::Options options;
        options.folder = dir.path() + u"/models"_s;
        options.pluginDirs = {dir.path()};
        LocalAi local(options);
        QVERIFY(!local.available());
        // The models the app knows are there to read about all the same.
        QCOMPARE(local.choices().size(), ai::localModels().size());
        QCOMPARE(local.chosen(), ai::localModels().first().id);
        QVERIFY(!local.modelHere());
        const ai::Reply reply = ask(local, QString());
        QVERIFY(!reply.ok);
        QVERIFY2(reply.error.contains(u"runs no model on the device"), qPrintable(reply.error));
    }

    void theAssistantsOwnModel()
    {
        // What Settings shows of the models the app knows: the assistant's own is LFM3 1.7B by
        // Liquid AI trained further by Brulion, and shares its file with it.
        QTemporaryDir dir;
        LocalAi::Options options;
        options.folder = dir.path() + u"/models"_s;
        options.pluginDirs = {dir.path()};
        LocalAi local(options);
        const QVariantList choices = local.choices();
        QCOMPARE(choices.size(), 3);
        QCOMPARE(choices[2].toMap().value(u"value"_s).toString(), u"brulion-assistant-1.7b"_s);
        // (The models as their maker gives them by their names; this one by its short label, so
        // that the three fit side by side on a narrow phone.)
        QCOMPARE(choices[0].toMap().value(u"text"_s).toString(), u"LFM3-VL 2B"_s);
        QCOMPARE(choices[1].toMap().value(u"text"_s).toString(), u"LFM3 1.7B"_s);
        QCOMPARE(choices[2].toMap().value(u"text"_s).toString(), u"Brulion"_s);
        // The models as their maker gives them.
        QVERIFY(!local.modelAdapted());
        QCOMPARE(local.modelBase(), QString());
        QCOMPARE(local.modelBaseMaker(), QString());
        QCOMPARE(local.modelSharesWith(), QString());
        local.setChosen(u"lfm3-1.7b"_s);
        QVERIFY(!local.modelAdapted());
        QCOMPARE(local.modelMaker(), u"Liquid AI"_s);
        QCOMPARE(local.modelSharesWith(), u"Brulion Assistant 1.7B"_s);

        QSignalSpy changed(&local, &LocalAi::modelChanged);
        local.setChosen(u"brulion-assistant-1.7b"_s);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(local.modelName(), u"Brulion Assistant 1.7B"_s);
        QVERIFY(local.modelAdapted());
        QVERIFY(!local.modelSees());
        QCOMPARE(local.modelBase(), u"LFM3 1.7B"_s);
        QCOMPARE(local.modelBaseMaker(), u"Liquid AI"_s);
        QCOMPARE(local.modelMaker(), u"Brulion"_s);
        QCOMPARE(local.modelLicense(), u"LFM Open License v1.0"_s);
        QVERIFY2(local.modelLicenseUrl().startsWith(u"https://huggingface.co/janusz-bit/brulion-assistant-1.7b/blob/"_s)
                         && local.modelLicenseUrl().endsWith(u"/LICENSE"_s),
                 qPrintable(local.modelLicenseUrl()));
        QCOMPARE(local.modelPageUrl(), u"https://huggingface.co/janusz-bit/brulion-assistant-1.7b"_s);
        QCOMPARE(local.modelSharesWith(), u"LFM3 1.7B"_s);
        // The text model's file and the adapter: 1 146 270 016 B.
        QVERIFY2(local.modelSize().contains(u"1.1") || local.modelSize().contains(u"1,1"),
                 qPrintable(local.modelSize()));
        QCOMPARE(local.modelLeft(), local.modelSize());
    }

    void theChosenModel()
    {
        Fixture f;
        LocalAi local(f.options());
        QVERIFY(local.available());
        QCOMPARE(local.folder(), f.dir.path() + u"/models"_s);
        const QVariantList choices = local.choices();
        QCOMPARE(choices.size(), 2);
        QCOMPARE(choices[0].toMap().value(u"value"_s).toString(), u"sees"_s);
        QCOMPARE(choices[0].toMap().value(u"text"_s).toString(), u"Widzący 2B"_s);
        QCOMPARE(choices[1].toMap().value(u"value"_s).toString(), u"reads"_s);

        // The first model is the default: chosen from the start, and what "" means.
        QCOMPARE(local.chosen(), u"sees"_s);
        QCOMPARE(local.modelName(), u"Widzący 2B"_s);
        QCOMPARE(local.modelMaker(), u"Testy"_s);
        QVERIFY(local.modelSees());
        QCOMPARE(local.modelLicense(), u"Licencja testowa"_s);
        QCOMPARE(local.modelLicenseUrl(), u"https://example.org/licencja"_s);
        QCOMPARE(local.modelPageUrl(), u"https://example.org/widzacy"_s);
        QVERIFY2(local.modelSize().contains(u"146") || local.modelSize().contains(u"147"),
                 qPrintable(local.modelSize())); // 150 000 B
        QVERIFY(!local.modelHere());
        // None of it here: all of it is left to download; it shares no file with the other.
        QCOMPARE(local.modelLeft(), local.modelSize());
        QCOMPARE(local.modelSharesWith(), QString());

        QSignalSpy changed(&local, &LocalAi::modelChanged);
        local.setChosen(u"reads"_s);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(local.chosen(), u"reads"_s);
        QCOMPARE(local.modelName(), u"Czytający 1.7B"_s);
        QVERIFY(!local.modelSees());
        local.setChosen(u"reads"_s);
        QCOMPARE(changed.size(), 1);
        // "" and a model the app does not know: the default.
        local.setChosen(QString());
        QCOMPARE(local.chosen(), u"sees"_s);
        QCOMPARE(changed.size(), 2);
        local.setChosen(u"reads"_s);
        local.setChosen(u"gpt"_s);
        QCOMPARE(local.chosen(), u"sees"_s);
        QCOMPARE(local.modelName(), u"Widzący 2B"_s);
    }

    void downloadsTheChosenModel()
    {
        Fixture f;
        LocalAi local(f.options());
        f.server.slow = true;
        QSignalSpy downloadChanged(&local, &LocalAi::downloadChanged);
        QSignalSpy modelsChanged(&local, &LocalAi::modelsChanged);
        QSignalSpy finished(&local, &LocalAi::downloadFinished);
        QSignalSpy working(&local, &LocalAi::workingChanged);
        QVERIFY(!local.downloadingChosen());
        QVERIFY(!local.working());
        QCOMPARE(local.progress(), 0.0);
        QCOMPARE(local.progressText(), QString());

        // The log tells of a download: what there was to fetch, and how it ended.
        QTest::ignoreMessage(QtInfoMsg, "sees download: 146.5 KB to fetch of 146.5 KB");
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(u"^sees downloaded: 146\\.5 KB in [0-9.]+ m?s$"_s));
        local.download(); // the chosen one: "sees"
        QVERIFY(local.downloadingChosen());
        // A phone keeps its screen on from here to the download's end: said once, not with
        // every step of the progress.
        QVERIFY(local.working());
        QCOMPARE(working.size(), 1);
        QVERIFY(!local.downloadingOther());
        QCOMPARE(local.downloadingId(), u"sees"_s);
        QTRY_VERIFY_WITH_TIMEOUT(local.progress() > 0.1, 20000);
        QVERIFY(local.progress() < 1.0);
        QVERIFY2(local.progressText().contains(u" / ") || local.progressText().contains(u"of")
                         || local.progressText().contains(u"z "),
                 qPrintable(local.progressText()));
        // Looking at the other model meanwhile: it is not the one being downloaded.
        local.setChosen(u"reads"_s);
        QVERIFY(!local.downloadingChosen());
        QVERIFY(local.downloadingOther());
        QCOMPARE(local.progress(), 0.0);
        local.download(); // one at a time: nothing starts
        QCOMPARE(local.downloadingId(), u"sees"_s);
        local.setChosen(u"sees"_s);

        QVERIFY(finished.wait(30000));
        QCOMPARE(finished[0][0].toString(), u"sees"_s);
        QVERIFY2(finished[0][1].toBool(), qPrintable(finished[0][2].toString()));
        QVERIFY(local.modelHere());
        QVERIFY(!local.working());
        QCOMPARE(working.size(), 2);
        QVERIFY(local.has(u"sees"_s));
        QVERIFY(local.has(QString()));
        QVERIFY(!local.has(u"reads"_s));
        QVERIFY(!local.downloadingChosen());
        QCOMPARE(local.downloadError(), QString());
        QVERIFY(!downloadChanged.isEmpty());
        QVERIFY(!modelsChanged.isEmpty());
        QVERIFY(QFile::exists(f.dir.path() + u"/models/sees.gguf"_s));
        QVERIFY(QFile::exists(f.dir.path() + u"/models/sees-projector.gguf"_s));
        QCOMPARE(local.modelLeft(), QString());

        // A download that fails says why — for the model it was, not for the other one.
        f.server.slow = false;
        f.server.files.remove("/reads.gguf");
        local.setChosen(u"reads"_s);
        QTest::ignoreMessage(QtInfoMsg, "reads download: 87.9 KB to fetch of 87.9 KB");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"^reads download failed after [0-9.]+ m?s: .*404"_s));
        QVERIFY(!download(local, u"reads"_s));
        QVERIFY2(local.downloadError().contains(u"404"), qPrintable(local.downloadError()));
        local.setChosen(u"sees"_s);
        QCOMPARE(local.downloadError(), QString());
        // Stopped by the user: no error.
        f.server.files.insert("/reads.gguf", f.readsBytes);
        f.server.slow = true;
        local.setChosen(u"reads"_s);
        finished.clear();
        QCOMPARE(local.modelLeft(), local.modelSize()); // (the failed download fetched nothing)
        QTest::ignoreMessage(QtInfoMsg, "reads download: 87.9 KB to fetch of 87.9 KB");
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(u"^reads download stopped after [0-9.]+ m?s$"_s));
        local.download();
        QTRY_VERIFY_WITH_TIMEOUT(local.progress() > 0.05, 20000);
        QVERIFY(local.working());
        local.cancelDownload();
        QVERIFY(!local.working()); // a download that failed or was stopped lets the screen go too
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished[0][1].toBool());
        QCOMPARE(local.downloadError(), QString());
        QVERIFY(!local.downloadingChosen());
        // What it fetched stays: less is left to download than the whole (said with modelsChanged).
        QVERIFY(!local.modelLeft().isEmpty());
        QVERIFY2(local.modelLeft() != local.modelSize(), qPrintable(local.modelLeft()));
        QVERIFY(!modelsChanged.isEmpty());
    }

    void answersOnTheWorkerThread()
    {
        Fixture f;
        LocalAi local(f.options());
        // Not downloaded yet: said so, with the model's name; nothing opened.
        ai::Reply reply = ask(local, QString());
        QVERIFY(!reply.ok);
        QVERIFY2(reply.error.contains(u"Widzący 2B") && reply.error.contains(u"not on this device yet"),
                 qPrintable(reply.error));
        reply = ask(local, u"gpt"_s);
        QVERIFY2(reply.error.contains(u"no such model on this device"), qPrintable(reply.error));
        QCOMPARE(f.openedCount(), 0);

        QVERIFY(download(local, u"sees"_s));
        QSignalSpy busy(&local, &LocalAi::busyChanged);
        QVERIFY(!local.busy());
        reply = ask(local, QString(), u"Co słychać u żółwia?"_s);
        QVERIFY2(reply.ok, qPrintable(reply.error));
        QCOMPARE(reply.text, u"Z urządzenia."_s);
        QCOMPARE(reply.model, u"sees"_s);
        QCOMPARE(reply.inputTokens, 21);
        QCOMPARE(reply.outputTokens, 3);
        QCOMPARE(busy.size(), 2); // busy, then not
        QVERIFY(!local.busy());
        {
            const std::lock_guard lock(f.record.mutex);
            // Opened with the model's files, the context every device holds, on another thread.
            QCOMPARE(f.record.opened.size(), 1);
            QCOMPARE(f.record.opened[0].modelPath, f.dir.path() + u"/models/sees.gguf"_s);
            QCOMPARE(f.record.opened[0].projectorPath, f.dir.path() + u"/models/sees-projector.gguf"_s);
            QCOMPARE(f.record.opened[0].contextTokens, 16384);
            QCOMPARE(f.record.opened[0].imageTokens, 1024);
            // What it read of a request's system text and tools is kept there between runs.
            QCOMPARE(f.record.opened[0].stateDir, f.dir.path() + u"/model-state"_s);
            QVERIFY(f.record.opened[0].spinWait); // (Android's)
            QCOMPARE(f.record.prompts.size(), 1);
            QVERIFY(std::ranges::any_of(f.record.prompts[0].pieces, [](const ai::local::Piece &p) {
                return p.text.contains(u"Co słychać u żółwia?");
            }));
            QVERIFY(f.record.threads[0] != QThread::currentThreadId());
        }
        // The model stays open for the next request …
        QVERIFY(ask(local, u"sees"_s).ok);
        QCOMPARE(f.openedCount(), 1);
        QCOMPARE(f.record.open.load(), 1);
        // … and another model takes its place (one in memory at a time).
        QVERIFY(download(local, u"reads"_s));
        reply = ask(local, u"reads"_s);
        QVERIFY2(reply.ok, qPrintable(reply.error));
        QCOMPARE(reply.model, u"reads"_s);
        QCOMPARE(f.openedCount(), 2);
        QCOMPARE(f.record.open.load(), 1);
        {
            const std::lock_guard lock(f.record.mutex);
            QCOMPARE(f.record.opened[1].modelPath, f.dir.path() + u"/models/reads.gguf"_s);
            QCOMPARE(f.record.opened[1].projectorPath, QString()); // it reads text only
        }
        // Several requests are answered one after another, each once.
        int answers = 0;
        for (int i = 0; i < 5; ++i)
            local.send(settingsFor(u"reads"_s), question(u"pytanie %1"_s.arg(i)), [&](const ai::Reply &r) {
                if (r.ok)
                    ++answers;
            });
        QSignalSpy working(&local, &LocalAi::workingChanged);
        QVERIFY(local.busy());
        QVERIFY(local.working()); // from the first request to the last answer
        QTRY_COMPARE_WITH_TIMEOUT(answers, 5, 20000);
        QVERIFY(!local.busy());
        QVERIFY(!local.working());
        QCOMPARE(working.size(), 1);
        QCOMPARE(f.openedCount(), 2);
    }

    void aModelThatCannotBeOpened()
    {
        Fixture f;
        LocalAi::Options options = f.options();
        options.open = [](const ai::LocalRunner::Options &, QString *error) -> std::unique_ptr<ai::LocalRunner> {
            *error = u"this device's processor is too old to run a model"_s;
            return nullptr;
        };
        LocalAi local(options);
        QVERIFY(download(local, u"sees"_s));
        const ai::Reply reply = ask(local, u"sees"_s);
        QVERIFY(!reply.ok);
        QCOMPARE(reply.error, u"this device's processor is too old to run a model"_s);
        QVERIFY(!local.busy());
    }

    void leavesMemoryAfterAWhile()
    {
        Fixture f;
        LocalAi::Options options = f.options();
        options.idleMs = 150;
        LocalAi local(options);
        QVERIFY(download(local, u"sees"_s));
        QVERIFY(ask(local, u"sees"_s).ok);
        QCOMPARE(f.record.open.load(), 1);
        // No request for a while: the model is let go; the next one opens it again.
        QTRY_COMPARE_WITH_TIMEOUT(f.record.open.load(), 0, 5000);
        QVERIFY(ask(local, u"sees"_s).ok);
        QCOMPARE(f.openedCount(), 2);
        QCOMPARE(f.record.open.load(), 1);
    }

    void anAdaptedModel()
    {
        // The text model with an adapter over it, trained to answer at once.
        Fixture f;
        const QByteArray adapterBytes(20 * 1000, 'a');
        f.server.files.insert("/adapted-lora.gguf", adapterBytes);
        ai::LocalModel adapted = f.reads;
        adapted.id = u"adapted"_s;
        adapted.name = u"Dostrojony 1.7B"_s;
        adapted.maker = u"Brulion"_s;
        adapted.baseName = f.reads.name;
        adapted.baseMaker = f.reads.maker;
        adapted.adapter = {u"adapted-lora.gguf"_s, f.server.url(u"/adapted-lora.gguf"_s), sha256(adapterBytes),
                           adapterBytes.size()};
        adapted.thinks = false;
        LocalAi::Options options = f.options();
        options.models << adapted;
        LocalAi local(options);
        // Each of the two says it shares a file with the other; the model that sees, with none.
        QCOMPARE(local.modelSharesWith(), QString());
        local.setChosen(u"reads"_s);
        QCOMPARE(local.modelSharesWith(), u"Dostrojony 1.7B"_s);
        QVERIFY(!local.modelAdapted());
        QCOMPARE(local.modelBase(), QString());
        local.setChosen(u"adapted"_s);
        QCOMPARE(local.modelSharesWith(), u"Czytający 1.7B"_s);
        // Whose model it is: the model under it by its maker, trained further by another.
        QVERIFY(local.modelAdapted());
        QVERIFY(!local.modelSees());
        QCOMPARE(local.modelBase(), u"Czytający 1.7B"_s);
        QCOMPARE(local.modelBaseMaker(), u"Testy"_s);
        QCOMPARE(local.modelMaker(), u"Brulion"_s);
        QCOMPARE(local.modelLeft(), local.modelSize()); // none of it here yet
        local.setChosen(u"reads"_s);
        QVERIFY(download(local, u"reads"_s));
        QSignalSpy modelChanged(&local, &LocalAi::modelChanged);
        QSignalSpy modelsChanged(&local, &LocalAi::modelsChanged);
        local.setChosen(u"adapted"_s);
        QCOMPARE(modelChanged.size(), 1);
        QCOMPARE(modelsChanged.size(), 1);
        QVERIFY2(local.modelSize().contains(u"107"), qPrintable(local.modelSize())); // 110 000 B, its files together
        QVERIFY(!local.modelHere());
        // The file the model alone brought is here: only the adapter is left (20 000 B).
        QVERIFY2(local.modelLeft().contains(u"19.5") || local.modelLeft().contains(u"19,5"),
                 qPrintable(local.modelLeft()));
        // What it shares with the model alone is not fetched again.
        const qsizetype before = f.server.requests.size();
        QTest::ignoreMessage(QtInfoMsg, "adapted download: 19.5 KB to fetch of 107.4 KB");
        QVERIFY(download(local, u"adapted"_s));
        QCOMPARE(f.server.requests.size(), before + 1);
        QCOMPARE(f.server.requests.last().path, QByteArray("/adapted-lora.gguf"));
        QVERIFY(local.modelHere());
        QCOMPARE(local.modelLeft(), QString());

        // Opened with the adapter; a request that lets it think gets an answer without thoughts
        // (the model alone still thinks).
        const auto thinking = [&](const QString &model) {
            ai::Request request;
            request.messages << ai::textMessage(u"user"_s, u"Co zaplanowałem na środę?"_s);
            bool done = false;
            local.send(settingsFor(model), request, [&](const ai::Reply &) { done = true; });
            return QTest::qWaitFor([&] { return done; }, 20000);
        };
        QVERIFY(thinking(u"adapted"_s));
        QVERIFY(thinking(u"reads"_s));
        {
            const std::lock_guard lock(f.record.mutex);
            QCOMPARE(f.record.opened.size(), 2);
            QCOMPARE(f.record.opened[0].modelPath, f.dir.path() + u"/models/reads.gguf"_s);
            QCOMPARE(f.record.opened[0].adapterPath, f.dir.path() + u"/models/adapted-lora.gguf"_s);
            QCOMPARE(f.record.opened[1].adapterPath, QString());
            QVERIFY(!f.record.prompts[0].thinking);
            QVERIFY(f.record.prompts[1].thinking);
        }

        // The model alone removed: the file the adapted one needs stays.
        local.remove(u"reads"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!local.has(u"reads"_s), 10000);
        QVERIFY(local.has(u"adapted"_s));
        QVERIFY(QFile::exists(f.dir.path() + u"/models/reads.gguf"_s));
        QVERIFY(ask(local, u"adapted"_s).ok);
        // The model alone is not on the device now, but all its files are: nothing is left.
        local.setChosen(u"reads"_s);
        QVERIFY(!local.modelHere());
        QCOMPARE(local.modelLeft(), QString());
        local.setChosen(u"adapted"_s);
        local.remove(u"adapted"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!local.has(u"adapted"_s), 10000);
        QVERIFY(!QFile::exists(f.dir.path() + u"/models/reads.gguf"_s));
        QVERIFY(!QFile::exists(f.dir.path() + u"/models/adapted-lora.gguf"_s));
        QCOMPARE(local.modelLeft(), local.modelSize()); // all of it to download again
    }

    void removeStopsItsAnswer()
    {
        Fixture f;
        LocalAi local(f.options());
        QVERIFY(download(local, u"sees"_s));
        QVERIFY(ask(local, u"sees"_s).ok);
        // (A state the runner kept on disk.)
        QVERIFY(QDir().mkpath(f.dir.path() + u"/model-state"_s));
        QFile state(f.dir.path() + u"/model-state/0123abcd.state"_s);
        QVERIFY(state.open(QIODevice::WriteOnly));
        state.write("BRST");
        state.close();
        // While it answers, the model is removed: the answer ends, the model leaves memory, its
        // files go, and the states kept on disk with them.
        f.record.block.store(true);
        ai::Reply got;
        bool done = false;
        local.send(settingsFor(u"sees"_s), question(u"Długie pytanie"_s), [&](const ai::Reply &reply) {
            got = reply;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(f.record.blocked.load(), 10000);
        local.setChosen(u"sees"_s);
        local.remove();
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(!got.ok);
        QCOMPARE(got.error, u"stopped"_s);
        f.record.block.store(false);
        QTRY_VERIFY_WITH_TIMEOUT(!local.modelHere(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(f.record.open.load(), 0, 5000);
        QVERIFY(!QFile::exists(f.dir.path() + u"/models/sees.gguf"_s));
        QVERIFY(!QFile::exists(f.dir.path() + u"/models/sees-projector.gguf"_s));
        QVERIFY(!QFileInfo::exists(f.dir.path() + u"/model-state"_s));
        // A request for it now: not on this device.
        const ai::Reply reply = ask(local, u"sees"_s);
        QVERIFY2(reply.error.contains(u"not on this device yet"), qPrintable(reply.error));
        // Removing a model that is not there, or one the app does not know: nothing happens.
        local.remove(u"reads"_s);
        local.remove(u"gpt"_s);
        QTest::qWait(50);
        QVERIFY(!local.has(u"reads"_s));
    }

    void closingWhileItAnswers()
    {
        Fixture f;
        f.record.block.store(true);
        bool answered = false;
        QElapsedTimer timer;
        {
            LocalAi local(f.options());
            QVERIFY(download(local, u"sees"_s));
            local.send(settingsFor(u"sees"_s), question(u"Pytanie bez końca"_s),
                       [&](const ai::Reply &) { answered = true; });
            QTRY_VERIFY_WITH_TIMEOUT(f.record.blocked.load(), 10000);
            timer.start();
        } // the app closes: the answer is told to stop, the thread ends
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(f.record.open.load(), 0);
        QVERIFY(!answered); // nobody is left to hear it
    }
};

QTEST_GUILESS_MAIN(TestLocalAi)
#include "tst_localai.moc"
