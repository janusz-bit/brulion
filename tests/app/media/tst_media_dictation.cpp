// Dictation (src/app/media/dictation.h), the AI chat's microphone on the desktop, with a recorder
// fed samples instead of a microphone and a transcriber with a fake recognizer and decoder: not
// ready without a speech model (and says where to get one); dictated text heard in the language
// the dictation started with (or the one the speech settings name); its elapsed time and its
// limit (it stops by itself and is heard); cancelled while recording and while being heard;
// nothing recorded; the recording deleted every time, and what an earlier run left in its folder
// removed when the next dictation starts.
#include "dictation.h"
#include "transcriber.h"
#include "voicerecorder.h"

#include "media/pcm.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <testtiming.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <numbers>

using namespace Qt::StringLiterals;
using namespace brulion;
using media::Dictation;
using media::Transcriber;

namespace {

struct Heard {
    std::mutex mutex;
    QStringList languages;
    std::atomic<bool> release{true};
    std::atomic<bool> started{false};
};

class FakeRunner final : public speech::SpeechRunner {
public:
    explicit FakeRunner(Heard *heard) : m_heard(heard) { }
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
        Result r;
        r.status = samples.isEmpty() ? Failed : Ok;
        r.language = params.language;
        r.segments = {{0, 1000, u" Zadzwonić do mamy"_s}, {1000, 2000, u"w piątek o ósmej."_s}};
        return r;
    }

private:
    Heard *m_heard;
};

ai::LocalModel fakeModel(const QString &folder, bool here)
{
    const QByteArray bytes = "model mowy";
    ai::LocalModel m;
    m.id = u"small"_s;
    m.name = u"Whisper Small"_s;
    m.label = u"Small"_s;
    m.format = u"whisper"_s;
    m.file = {u"ggml-small.bin"_s, QUrl(u"http://127.0.0.1:1/small"_s),
              QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(), bytes.size()};
    if (here) {
        QDir().mkpath(folder);
        QFile f(folder + u"/ggml-small.bin"_s);
        if (f.open(QIODevice::WriteOnly))
            f.write(bytes);
        QFile marker(folder + u"/small.downloaded"_s);
        if (marker.open(QIODevice::WriteOnly))
            marker.write("");
    }
    return m;
}

/// `seconds` of a tone as 16 kHz mono 16-bit samples, fed in pieces of 100 ms.
void speak(media::VoiceRecorder *recorder, double seconds)
{
    constexpr int Rate = 16000;
    QList<float> samples(int(seconds * Rate));
    for (qsizetype i = 0; i < samples.size(); ++i)
        samples[i] = float(0.4 * std::sin(2 * std::numbers::pi * 220.0 * double(i) / Rate));
    const QByteArray bytes = media::toPcm16(samples.constData(), samples.size());
    constexpr qsizetype Piece = Rate / 10 * 2;
    for (qsizetype at = 0; at < bytes.size(); at += Piece) {
        recorder->feed(bytes.mid(at, Piece));
        QCoreApplication::processEvents();
    }
}

} // namespace

class TestMediaDictation : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QVariantMap m_settings;
    Heard m_heard;
    int m_n = 0;

    std::unique_ptr<Transcriber> transcriber(bool modelHere)
    {
        const QString folder = m_dir.filePath(u"modele-%1"_s.arg(++m_n));
        Transcriber::Options o;
        o.folder = folder;
        o.models = {fakeModel(folder, modelHere)};
        o.open = [this](const speech::SpeechRunner::Options &, QString *) -> std::unique_ptr<speech::SpeechRunner> {
            return std::make_unique<FakeRunner>(&m_heard);
        };
        o.decode = [](const QString &file, const std::atomic<bool> *, QString *error,
                      const std::function<void(int)> &) -> std::optional<QList<float>> {
            if (!QFileInfo::exists(file)) {
                *error = u"nie ma nagrania"_s;
                return std::nullopt;
            }
            return QList<float>(16000, 0.1f);
        };
        o.setting = [this](const QString &key, const QVariant &fallback) { return m_settings.value(key, fallback); };
        o.setSetting = [this](const QString &key, const QVariant &value) { m_settings.insert(key, value); };
        return std::make_unique<Transcriber>(o);
    }

    /// A dictation that records what speak() feeds, into its own folder.
    std::unique_ptr<Dictation> dictation(Transcriber *t)
    {
        auto d = std::make_unique<Dictation>(t);
        d->setFolder(m_dir.filePath(u"dyktowanie-%1"_s.arg(++m_n)));
        d->recorder()->useFeed(16000, 1);
        return d;
    }

    static bool empty(const QString &folder) { return QDir(folder).entryList(QDir::Files | QDir::Hidden).isEmpty(); }

private Q_SLOTS:
    void init()
    {
        m_settings.clear();
        m_heard.release = true;
        m_heard.started = false;
        std::lock_guard lock(m_heard.mutex);
        m_heard.languages.clear();
    }

    void notReadyWithoutAModel()
    {
        auto t = transcriber(false);
        auto d = dictation(t.get());
        if (!d->recorder()->available())
            QSKIP("this build records nothing (no Qt Multimedia)");
        QVERIFY(!d->ready());
        QSignalSpy failed(d.get(), &Dictation::failed);
        QVERIFY(!d->start(u"pl-PL"_s));
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains(u"Speech"_s));
        QVERIFY(!d->listening());
        QCOMPARE(d->maxMs(), Dictation::MaxMs);
    }

    void dictates()
    {
        auto t = transcriber(true);
        auto d = dictation(t.get());
        if (!d->recorder()->available())
            QSKIP("this build records nothing (no Qt Multimedia)");
        QVERIFY(d->ready());
        // What an app that ended meanwhile left there goes.
        QDir().mkpath(d->folder());
        QFile old(d->folder() + u"/Dictation 2026-11-22 10.00.wav"_s);
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("RIFF");
        old.close();
        QSignalSpy recognized(d.get(), &Dictation::recognized);
        QSignalSpy listening(d.get(), &Dictation::listeningChanged);
        QSignalSpy elapsed(d.get(), &Dictation::elapsedChanged);
        QVERIFY(d->start(u"pl-PL"_s));
        QVERIFY(d->listening());
        QVERIFY(!d->recognizing());
        QVERIFY(!old.exists());
        QVERIFY(!d->start(u"pl-PL"_s)); // (one at a time)
        speak(d->recorder(), 0.6);
        QVERIFY(d->elapsedMs() >= 500);
        QVERIFY(!elapsed.isEmpty());
        d->stop();
        QVERIFY(!d->listening());
        QTRY_VERIFY_WITH_TIMEOUT(!recognized.isEmpty(), test::kRoomyMs);
        QCOMPARE(recognized.first().first().toString(), u"Zadzwonić do mamy w piątek o ósmej."_s);
        QVERIFY(!d->recognizing());
        QVERIFY(listening.size() >= 2);
        // Heard in the language it started with; the recording gone.
        QCOMPARE(m_heard.languages, QStringList{u"pl"_s});
        QTRY_VERIFY(empty(d->folder()));

        // The speech settings' language wins over the dictation's.
        t->setLanguage(u"en"_s);
        recognized.clear();
        QVERIFY(d->start(u"pl-PL"_s));
        speak(d->recorder(), 0.3);
        d->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!recognized.isEmpty(), test::kRoomyMs);
        QCOMPARE(m_heard.languages.last(), u"en"_s);
        QTRY_VERIFY(empty(d->folder()));
    }

    void stopsAtItsLimit()
    {
        auto t = transcriber(true);
        auto d = dictation(t.get());
        if (!d->recorder()->available())
            QSKIP("this build records nothing (no Qt Multimedia)");
        d->setMaxMs(400);
        QSignalSpy recognized(d.get(), &Dictation::recognized);
        QVERIFY(d->start(u"en-GB"_s));
        speak(d->recorder(), 1.0);
        // It stopped by itself at 400 ms and is heard.
        QVERIFY(!d->listening());
        QTRY_VERIFY_WITH_TIMEOUT(!recognized.isEmpty(), test::kRoomyMs);
        QCOMPARE(m_heard.languages, QStringList{u"en"_s});
        QTRY_VERIFY(empty(d->folder()));
    }

    void cancelled()
    {
        auto t = transcriber(true);
        auto d = dictation(t.get());
        if (!d->recorder()->available())
            QSKIP("this build records nothing (no Qt Multimedia)");
        QSignalSpy failed(d.get(), &Dictation::failed);
        QSignalSpy recognized(d.get(), &Dictation::recognized);
        // While recording.
        QVERIFY(d->start(u"pl-PL"_s));
        speak(d->recorder(), 0.3);
        d->cancel();
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().toString(), QString());
        QVERIFY(!d->listening());
        QTRY_VERIFY(empty(d->folder()));
        // While being heard: nothing comes of it, the recording goes once the recognizer lets go.
        m_heard.release = false;
        QVERIFY(d->start(u"pl-PL"_s));
        speak(d->recorder(), 0.3);
        d->stop();
        QTRY_VERIFY_WITH_TIMEOUT(m_heard.started.load(), test::kRoomyMs);
        QVERIFY(d->recognizing());
        d->cancel();
        QVERIFY(!d->recognizing());
        QCOMPARE(failed.size(), 2);
        m_heard.release = true;
        QTRY_VERIFY_WITH_TIMEOUT(!t->busy(), test::kRoomyMs);
        QTRY_VERIFY(empty(d->folder()));
        QVERIFY(recognized.isEmpty());
        // Cancelled when nothing runs: nothing happens.
        d->cancel();
        QCOMPARE(failed.size(), 2);
    }

    void nothingRecorded()
    {
        auto t = transcriber(true);
        auto d = dictation(t.get());
        if (!d->recorder()->available())
            QSKIP("this build records nothing (no Qt Multimedia)");
        QSignalSpy failed(d.get(), &Dictation::failed);
        QVERIFY(d->start(u"pl-PL"_s));
        d->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty(), test::kRoomyMs);
        QVERIFY(!failed.first().first().toString().isEmpty());
        QVERIFY(!d->listening());
        QVERIFY(!d->recognizing());
        QVERIFY(empty(d->folder()));
    }

    void transcriberGoesFirst()
    {
        auto t = transcriber(true);
        auto d = dictation(t.get());
        t.reset();
        QVERIFY(!d->ready());
        QVERIFY(!d->start(u"pl-PL"_s));
    }
};

QTEST_MAIN(TestMediaDictation)
#include "tst_media_dictation.moc"
