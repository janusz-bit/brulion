// Transcriber (src/app/media/transcriber.h), speech recognition's queue in the app, with a fake
// recognizer and decoder: nothing without a model here, the recordings heard one after another
// with the chosen model (or the first one here), the stages and progress, cancelling one waiting
// and one being heard, what Whisper writes over silence left out, the transcript handed to the
// placer (and its refusal: the note no longer has the recording), errors of the decoder and of the
// recognizer, the settings (model, language, transcribing by itself) through the shell's store.

#include "transcriber.h"

#include "speech/speech.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <testtiming.h>

#include <atomic>
#include <mutex>

using namespace Qt::StringLiterals;
using namespace brulion;
using media::Transcriber;

namespace {

/// What the fake recognizer heard and was asked.
struct Heard {
    std::mutex mutex;
    QStringList models;    ///< the model file of each open
    QStringList languages; ///< the language of each transcription
    int opens = 0;
    /// Blocks transcribe() until set (a recording "being heard").
    std::atomic<bool> release{true};
    std::atomic<bool> started{false};
};

class FakeRunner final : public speech::SpeechRunner {
public:
    FakeRunner(Heard *heard, QString model) : m_heard(heard), m_model(std::move(model)) { }
    QString device() const override { return u"fake"_s; }
    Result transcribe(const QList<float> &samples, const Params &params) override
    {
        {
            std::lock_guard lock(m_heard->mutex);
            m_heard->languages.append(params.language);
        }
        m_heard->started = true;
        while (!m_heard->release.load()) {
            if (params.cancel && params.cancel->load())
                return Result{Cancelled, {}, {}, {}, 0, 0};
            QThread::msleep(5);
        }
        if (params.progress) {
            params.progress(50);
            params.progress(100);
        }
        Result r;
        r.status = Ok;
        r.language = u"pl"_s;
        // The "sound" says how many samples it had.
        if (samples.isEmpty()) {
            r.status = Failed;
            r.error = u"rozpoznawanie zawiodło"_s;
            return r;
        }
        r.segments = {{0, 1000, u"[BLANK_AUDIO]"_s}, {1000, 2000, u" Kupić mleko"_s}, {2000, 3000, u"i #chleb."_s}};
        return r;
    }

private:
    Heard *m_heard;
    QString m_model;
};

/// A model "downloaded": its file and the marker LocalModelFiles writes.
ai::LocalModel fakeModel(const QString &folder, const QString &id, const QByteArray &bytes, bool here)
{
    ai::LocalModel m;
    m.id = id;
    m.name = u"Whisper "_s + id;
    m.label = id;
    m.maker = u"OpenAI"_s;
    m.format = u"whisper"_s;
    m.file = {u"ggml-"_s + id + u".bin"_s, QUrl(u"http://127.0.0.1:1/"_s + id),
              QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(), bytes.size()};
    m.license = u"MIT"_s;
    if (here) {
        QDir().mkpath(folder);
        QFile f(folder + u'/' + m.file.name);
        if (f.open(QIODevice::WriteOnly))
            f.write(bytes);
        QFile marker(folder + u'/' + id + u".downloaded"_s);
        if (marker.open(QIODevice::WriteOnly))
            marker.write("");
    }
    return m;
}

struct Placed {
    QString page;
    QString target;
    int line = -1;
    QString text;
};

} // namespace

class TestMediaTranscriber : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QVariantMap m_settings;
    Heard m_heard;
    QList<Placed> m_placed;
    bool m_placeOk = true;

    std::unique_ptr<Transcriber> make(const QList<ai::LocalModel> &models, const QString &folder)
    {
        Transcriber::Options o;
        o.folder = folder;
        o.models = models;
        o.open = [this](const speech::SpeechRunner::Options &options,
                        QString *error) -> std::unique_ptr<speech::SpeechRunner> {
            std::lock_guard lock(m_heard.mutex);
            ++m_heard.opens;
            m_heard.models.append(QFileInfo(options.modelPath).fileName());
            if (options.modelPath.contains(u"broken"_s)) {
                *error = u"the speech model cannot be read"_s;
                return nullptr;
            }
            return std::make_unique<FakeRunner>(&m_heard, options.modelPath);
        };
        o.decode = [](const QString &file, const std::atomic<bool> *, QString *error,
                      const std::function<void(int)> &progress) -> std::optional<QList<float>> {
            if (file.endsWith(u".bad"_s)) {
                *error = u"nie da się odczytać nagrania"_s;
                return std::nullopt;
            }
            if (progress)
                progress(100);
            return QList<float>(file.endsWith(u".silent"_s) ? 0 : 16000, 0.1f);
        };
        o.setting = [this](const QString &key, const QVariant &fallback) { return m_settings.value(key, fallback); };
        o.setSetting = [this](const QString &key, const QVariant &value) { m_settings.insert(key, value); };
        o.filePath = [this](const QString &attachment) { return m_dir.filePath(attachment); };
        auto t = std::make_unique<Transcriber>(o);
        t->setPlacer([this](const QString &page, const QString &target, int line, const QString &text) {
            m_placed.append({page, target, line, text});
            return m_placeOk;
        });
        return t;
    }

    void touch(const QString &name)
    {
        QDir().mkpath(QFileInfo(m_dir.filePath(name)).absolutePath());
        QFile f(m_dir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("RIFF");
    }

private Q_SLOTS:
    void init()
    {
        m_settings.clear();
        m_placed.clear();
        m_placeOk = true;
        m_heard.release = true;
        m_heard.started = false;
        std::lock_guard lock(m_heard.mutex);
        m_heard.models.clear();
        m_heard.languages.clear();
        m_heard.opens = 0;
    }

    void noModel()
    {
        const QString folder = m_dir.filePath(u"brak"_s);
        auto t = make({fakeModel(folder, u"small"_s, "mały", false)}, folder);
        QVERIFY(t->available());
        QVERIFY(!t->ready());
        QVERIFY(!t->modelHere());
        touch(u"assets/a.m4a"_s);
        QVERIFY(!t->transcribe(u"assets/a.m4a"_s, u"Notatka.md"_s, u"a.m4a"_s, 0));
        QVERIFY(!t->busy());
    }

    void settings()
    {
        const QString folder = m_dir.filePath(u"ustawienia"_s);
        auto t = make({fakeModel(folder, u"small"_s, "mały", false), fakeModel(folder, u"base"_s, "bazowy", true)},
                      folder);
        QCOMPARE(t->chosen(), u"small"_s);
        QVERIFY(!t->modelHere());
        // The first one here when the chosen one is not.
        QVERIFY(t->ready());
        QCOMPARE(t->choices().size(), 2);
        QSignalSpy changed(t.get(), &Transcriber::modelChanged);
        t->setChosen(u"base"_s);
        QCOMPARE(changed.size(), 1);
        QVERIFY(t->modelHere());
        QCOMPARE(m_settings.value(u"speech/model"_s).toString(), u"base"_s);
        t->setChosen(u"nieznany"_s);
        QCOMPARE(t->chosen(), u"small"_s);

        QCOMPARE(t->language(), u"auto"_s);
        t->setLanguage(u"pl"_s);
        QCOMPARE(t->language(), u"pl"_s);
        QCOMPARE(m_settings.value(u"speech/language"_s).toString(), u"pl"_s);
        t->setLanguage(u"de"_s);
        QCOMPARE(t->language(), u"auto"_s);
        QVERIFY(t->automatic());
        t->setAutomatic(false);
        QVERIFY(!t->automatic());

        // A new one reads them back.
        m_settings.insert(u"speech/model"_s, u"base"_s);
        auto again = make({fakeModel(folder, u"small"_s, "mały", false), fakeModel(folder, u"base"_s, "bazowy", true)},
                          folder);
        QCOMPARE(again->chosen(), u"base"_s);
        QVERIFY(!again->automatic());
    }

    void transcribesInOrder()
    {
        const QString folder = m_dir.filePath(u"kolejka"_s);
        auto t = make({fakeModel(folder, u"small"_s, "mały", true)}, folder);
        m_settings.insert(u"speech/language"_s, u"pl"_s);
        touch(u"assets/pierwsze.m4a"_s);
        touch(u"assets/drugie.ogg"_s);
        QSignalSpy done(t.get(), &Transcriber::transcribed);
        QVERIFY(t->transcribe(u"assets/pierwsze.m4a"_s, u"Dziennik.md"_s, u"pierwsze.m4a"_s, 3));
        QVERIFY(t->transcribe(u"assets/drugie.ogg"_s, u"Dziennik.md"_s, u"drugie.ogg"_s, 7));
        // Already queued: not twice.
        QVERIFY(!t->transcribe(u"assets/drugie.ogg"_s, u"Dziennik.md"_s, u"drugie.ogg"_s, 7));
        // No such file.
        QVERIFY(!t->transcribe(u"assets/nie ma.m4a"_s, u"Dziennik.md"_s, u"nie ma.m4a"_s, 1));
        QVERIFY(t->busy());
        QCOMPARE(t->queue(), (QStringList{u"assets/pierwsze.m4a"_s, u"assets/drugie.ogg"_s}));
        QVERIFY(t->isQueued(u"assets/drugie.ogg"_s));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, test::kRoomyMs);
        QVERIFY(!t->busy());
        QCOMPARE(done.at(0).at(0).toString(), u"assets/pierwsze.m4a"_s);
        QVERIFY(done.at(0).at(2).toBool());
        QCOMPARE(done.at(1).at(0).toString(), u"assets/drugie.ogg"_s);
        QCOMPARE(m_placed.size(), 2);
        QCOMPARE(m_placed.at(0).page, u"Dziennik.md"_s);
        QCOMPARE(m_placed.at(0).target, u"pierwsze.m4a"_s);
        QCOMPARE(m_placed.at(0).line, 3);
        // What Whisper writes for no speech goes; a heard tag is no tag.
        QCOMPARE(m_placed.at(0).text, u"Kupić mleko i \\#chleb."_s);
        QCOMPARE(m_placed.at(1).line, 7);
        // One model opened for both, the language asked.
        QCOMPARE(m_heard.opens, 1);
        QCOMPARE(m_heard.languages, (QStringList{u"pl"_s, u"pl"_s}));
    }

    void cancels()
    {
        const QString folder = m_dir.filePath(u"anulowanie"_s);
        auto t = make({fakeModel(folder, u"small"_s, "mały", true)}, folder);
        touch(u"assets/długie.m4a"_s);
        touch(u"assets/czeka.m4a"_s);
        QSignalSpy done(t.get(), &Transcriber::transcribed);
        QSignalSpy jobs(t.get(), &Transcriber::jobsChanged);
        m_heard.release = false;
        QVERIFY(t->transcribe(u"assets/długie.m4a"_s, u"N.md"_s, u"długie.m4a"_s, 0));
        QVERIFY(t->transcribe(u"assets/czeka.m4a"_s, u"N.md"_s, u"czeka.m4a"_s, 0));
        QTRY_VERIFY_WITH_TIMEOUT(m_heard.started.load(), test::kRoomyMs);
        QTRY_COMPARE_WITH_TIMEOUT(t->jobStage(), u"hearing"_s, test::kRoomyMs);
        // The one waiting goes at once, quietly.
        t->cancel(u"assets/czeka.m4a"_s);
        QCOMPARE(t->queue(), QStringList{u"assets/długie.m4a"_s});
        QCOMPARE(done.size(), 1);
        QVERIFY(!done.first().at(2).toBool());
        QCOMPARE(done.first().at(4).toString(), QString());
        // The one being heard ends when the recognizer sees it.
        t->cancel(u"assets/długie.m4a"_s);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, test::kRoomyMs);
        QVERIFY(!done.at(1).at(2).toBool());
        QCOMPARE(done.at(1).at(4).toString(), QString());
        QVERIFY(m_placed.isEmpty());
        QVERIFY(!t->busy());
        QVERIFY(!jobs.isEmpty());
        m_heard.release = true;
    }

    void errors()
    {
        const QString folder = m_dir.filePath(u"błędy"_s);
        auto t = make({fakeModel(folder, u"small"_s, "mały", true)}, folder);
        touch(u"assets/zły.bad"_s);
        touch(u"assets/cisza.silent"_s);
        touch(u"assets/dobre.m4a"_s);
        QSignalSpy done(t.get(), &Transcriber::transcribed);
        QVERIFY(t->transcribe(u"assets/zły.bad"_s, u"N.md"_s, u"zły.bad"_s, 0));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, test::kRoomyMs);
        QCOMPARE(done.at(0).at(4).toString(), u"nie da się odczytać nagrania"_s);
        QVERIFY(t->transcribe(u"assets/cisza.silent"_s, u"N.md"_s, u"cisza.silent"_s, 0));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, test::kRoomyMs);
        QCOMPARE(done.at(1).at(4).toString(), u"rozpoznawanie zawiodło"_s);
        // The note no longer has the recording: the text comes back with the error.
        m_placeOk = false;
        QVERIFY(t->transcribe(u"assets/dobre.m4a"_s, u"N.md"_s, u"dobre.m4a"_s, 0));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, test::kRoomyMs);
        QVERIFY(!done.at(2).at(2).toBool());
        QCOMPARE(done.at(2).at(3).toString(), u"Kupić mleko i \\#chleb."_s);
        QVERIFY(!done.at(2).at(4).toString().isEmpty());
    }

    void brokenModel()
    {
        const QString folder = m_dir.filePath(u"zepsuty"_s);
        auto t = make({fakeModel(folder, u"broken"_s, "zepsuty", true)}, folder);
        touch(u"assets/x.m4a"_s);
        QSignalSpy done(t.get(), &Transcriber::transcribed);
        QVERIFY(t->transcribe(u"assets/x.m4a"_s, u"N.md"_s, u"x.m4a"_s, 0));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, test::kRoomyMs);
        QCOMPARE(done.first().at(4).toString(), u"the speech model cannot be read"_s);
    }

    void removeStopsAndDeletes()
    {
        const QString folder = m_dir.filePath(u"usuwanie"_s);
        auto t = make({fakeModel(folder, u"small"_s, "mały", true)}, folder);
        QVERIFY(t->modelHere());
        QSignalSpy models(t.get(), &Transcriber::modelsChanged);
        t->remove();
        QTRY_VERIFY_WITH_TIMEOUT(!t->modelHere(), test::kRoomyMs);
        QVERIFY(!QFileInfo::exists(folder + u"/ggml-small.bin"_s));
        QVERIFY(!t->ready());
    }
};

QTEST_GUILESS_MAIN(TestMediaTranscriber)
#include "tst_media_transcriber.moc"
