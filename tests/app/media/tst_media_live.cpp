// LiveTranscript (src/app/media/livetranscript.h), text while recording, with a recorder fed samples
// instead of a microphone and a transcriber whose fake recognizer reads what each second "says"
// from the samples themselves (second n of the recording is a level of n hundredths, heard as
// "słowo<n>."): the text grows window by window, what is kept stays and is never said twice or
// lost at a seam, the tentative end is replaced; the language of "auto" fixed by the first window;
// nothing heard with the setting off or no model; a transcription waiting goes first; the recording
// ending stops it (the text stays until the next one starts again from nothing); a recognizer that
// falls behind twice gives up for the recording and marks the model too slow.
#include "livetranscript.h"
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

using namespace Qt::StringLiterals;
using namespace brulion;
using media::LiveTranscript;
using media::Transcriber;

namespace {

constexpr int Rate = 16000;

struct Heard {
    std::mutex mutex;
    QStringList languages;
    QList<bool> live;
    std::atomic<bool> release{true};
    std::atomic<int> windows{0};
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
            m_heard->live.append(params.live);
        }
        while (!m_heard->release.load()) {
            if (params.cancel && params.cancel->load())
                return Result{Cancelled, {}, {}, {}, 0, 0, 0};
            QThread::msleep(5);
        }
        ++m_heard->windows;
        Result r;
        r.status = Ok;
        r.language = params.language == u"auto" ? u"pl"_s : params.language;
        r.audioMs = samples.size() * 1000 / Rate;
        // A segment per whole second heard, its word the level of that second's middle.
        for (qsizetype from = 0; from + Rate <= samples.size(); from += Rate) {
            const int n = int(std::lround(samples.at(from + Rate / 2) * 100.0f));
            const qint64 startMs = from * 1000 / Rate;
            r.segments.append({startMs, startMs + 1000, u"słowo%1."_s.arg(n)});
        }
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

/// The recording's seconds [from, to): second n a level of (n + 1) / 100, fed 100 ms at a time.
void speak(media::VoiceRecorder *recorder, int from, int to)
{
    for (int second = from; second < to; ++second) {
        const QList<float> samples(Rate, float(second + 1) / 100.0f);
        const QByteArray bytes = media::toPcm16(samples.constData(), samples.size());
        constexpr qsizetype Piece = Rate / 10 * 2;
        for (qsizetype at = 0; at < bytes.size(); at += Piece) {
            recorder->feed(bytes.mid(at, Piece));
            QCoreApplication::processEvents();
        }
    }
}

/// "słowo1. słowo2. … słowo<n>."
QString words(int from, int to)
{
    QStringList out;
    for (int n = from; n <= to; ++n)
        out.append(u"słowo%1."_s.arg(n));
    return out.join(u' ');
}

} // namespace

class TestMediaLive : public QObject {
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
        o.decode = [](const QString &, const std::atomic<bool> *, QString *,
                      const std::function<void(int)> &) -> std::optional<QList<float>> {
            return QList<float>(Rate, 0.5f);
        };
        o.setting = [this](const QString &key, const QVariant &fallback) { return m_settings.value(key, fallback); };
        o.setSetting = [this](const QString &key, const QVariant &value) { m_settings.insert(key, value); };
        o.filePath = [this](const QString &attachment) { return m_dir.filePath(attachment); };
        return std::make_unique<Transcriber>(o);
    }

    std::unique_ptr<media::VoiceRecorder> recorder()
    {
        auto r = std::make_unique<media::VoiceRecorder>();
        r->setFolder(m_dir.filePath(u"nagrania-%1"_s.arg(++m_n)));
        r->setWavOnly(true);
        r->useFeed(Rate, 1);
        return r;
    }

    /// Waits until every window due is heard.
    static void settle(Transcriber *t)
    {
        QTRY_VERIFY_WITH_TIMEOUT(!t->hearingLive(), test::kRoomyMs);
        QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(!t->hearingLive(), test::kRoomyMs);
    }

private Q_SLOTS:
    void init()
    {
        m_settings.clear();
        m_heard.release = true;
        m_heard.windows = 0;
        std::lock_guard lock(m_heard.mutex);
        m_heard.languages.clear();
        m_heard.live.clear();
    }

    void textGrowsWindowByWindow()
    {
        auto t = transcriber(true);
        auto r = recorder();
        if (!r->available())
            QSKIP("this build records nothing (no Qt Multimedia)");
        LiveTranscript live;
        live.setTranscriber(t.get());
        live.setRecorder(r.get());
        QCOMPARE(live.status(), u"off"_s);
        QSignalSpy text(&live, &LiveTranscript::textChanged);
        r->start();
        QCOMPARE(r->state(), u"recording"_s);
        QCOMPARE(live.status(), u"waiting"_s);
        // Not a step yet: nothing heard.
        speak(r.get(), 0, 2);
        QCOMPARE(m_heard.windows.load(), 0);
        // The first window: three seconds, its text tentative.
        speak(r.get(), 2, 3);
        QTRY_COMPARE_WITH_TIMEOUT(live.windows(), 1, test::kRoomyMs);
        QCOMPARE(live.status(), u"hearing"_s);
        QCOMPARE(live.committed(), QString());
        QCOMPARE(live.tentative(), words(1, 3));
        QCOMPARE(live.language(), u"pl"_s);
        QVERIFY(!text.isEmpty());
        // On to 25 seconds, a window at a time: what is kept is never said twice nor lost.
        for (int second = 3; second < 25; ++second) {
            speak(r.get(), second, second + 1);
            settle(t.get());
        }
        QVERIFY2(live.text().startsWith(words(1, 20)), qPrintable(live.text()));
        QVERIFY(!live.committed().isEmpty());
        QVERIFY2(words(1, 25).startsWith(live.text()), qPrintable(live.text()));
        QVERIFY(live.text().endsWith(u"słowo24."_s) || live.text().endsWith(u"słowo25."_s));
        QVERIFY(live.windows() >= 7);
        // Every window was heard live, the first in "auto", the next in the language it heard.
        {
            std::lock_guard lock(m_heard.mutex);
            QVERIFY(!m_heard.live.contains(false));
            QCOMPARE(m_heard.languages.first(), u"auto"_s);
            QCOMPARE(m_heard.languages.last(), u"pl"_s);
        }
        // The small field's end of it.
        live.setTailChars(20);
        QVERIFY(live.tail().startsWith(u"…"_s));
        QVERIFY(live.tail().size() <= 21);

        // The recording ends: no more windows, the text stays until the next one starts.
        const QString heard = live.text();
        r->stop();
        QTRY_COMPARE_WITH_TIMEOUT(r->state(), u"done"_s, test::kRoomyMs);
        QCOMPARE(live.status(), u"off"_s);
        QCOMPARE(live.text(), heard);
        r->start();
        QCOMPARE(live.text(), QString());
        QCOMPARE(live.windows(), 0);
        r->cancel();
    }

    void theSettingsLanguage()
    {
        auto t = transcriber(true);
        t->setLanguage(u"en"_s);
        auto r = recorder();
        if (!r->available())
            QSKIP("this build records nothing (no Qt Multimedia)");
        LiveTranscript live;
        live.setTranscriber(t.get());
        live.setRecorder(r.get());
        r->start();
        QCOMPARE(live.language(), u"en"_s);
        speak(r.get(), 0, 3);
        QTRY_COMPARE_WITH_TIMEOUT(live.windows(), 1, test::kRoomyMs);
        std::lock_guard lock(m_heard.mutex);
        QCOMPARE(m_heard.languages, QStringList{u"en"_s});
        r->cancel();
    }

    void notHeard()
    {
        auto r = recorder();
        if (!r->available())
            QSKIP("this build records nothing (no Qt Multimedia)");
        // No model here.
        {
            auto t = transcriber(false);
            LiveTranscript live;
            live.setTranscriber(t.get());
            live.setRecorder(r.get());
            r->start();
            QCOMPARE(live.status(), u"off"_s);
            speak(r.get(), 0, 4);
            QCOMPARE(m_heard.windows.load(), 0);
            r->cancel();
        }
        // The setting off; the owner's switch off.
        auto t = transcriber(true);
        t->setLive(false);
        LiveTranscript live;
        live.setTranscriber(t.get());
        live.setRecorder(r.get());
        r->start();
        QCOMPARE(live.status(), u"off"_s);
        speak(r.get(), 0, 4);
        QCOMPARE(m_heard.windows.load(), 0);
        r->cancel();
        t->setLive(true);
        live.setEnabled(false);
        r->start();
        speak(r.get(), 0, 4);
        QCOMPARE(m_heard.windows.load(), 0);
        QCOMPARE(live.text(), QString());
        // On again mid-recording: it hears from then on.
        live.setEnabled(true);
        QCOMPARE(live.status(), u"waiting"_s);
        speak(r.get(), 4, 7);
        QTRY_VERIFY_WITH_TIMEOUT(live.windows() >= 1, test::kRoomyMs);
        r->cancel();
    }

    void aTranscriptionGoesFirst()
    {
        auto t = transcriber(true);
        auto r = recorder();
        if (!r->available())
            QSKIP("this build records nothing (no Qt Multimedia)");
        QFile note(m_dir.filePath(u"poprzednia.m4a"_s));
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write("RIFF");
        note.close();
        LiveTranscript live;
        live.setTranscriber(t.get());
        live.setRecorder(r.get());
        m_heard.release = false;
        QSignalSpy transcribed(t.get(), &Transcriber::transcribed);
        QVERIFY(t->transcribe(u"poprzednia.m4a"_s, u"N.md"_s, u"poprzednia.m4a"_s, 0));
        r->start();
        speak(r.get(), 0, 4);
        // No window while it waits or is heard.
        QVERIFY(!t->hearingLive());
        QCOMPARE(live.windows(), 0);
        m_heard.release = true;
        QTRY_COMPARE_WITH_TIMEOUT(transcribed.size(), 1, test::kRoomyMs);
        // Then the next window due: all recorded so far.
        speak(r.get(), 4, 7);
        QTRY_VERIFY_WITH_TIMEOUT(live.windows() >= 1, test::kRoomyMs);
        QVERIFY2(live.text().startsWith(words(1, 4)), qPrintable(live.text()));
        r->cancel();
    }

    void fallsBehind()
    {
        auto t = transcriber(true);
        auto r = recorder();
        if (!r->available())
            QSKIP("this build records nothing (no Qt Multimedia)");
        LiveTranscript live;
        live.setTranscriber(t.get());
        live.setRecorder(r.get());
        live.setLateMs(1500);
        r->start();
        // Each window comes back two seconds behind: twice, and it gives up.
        for (int round = 0; round < 2; ++round) {
            m_heard.release = false;
            const int from = 3 + round * 5;
            speak(r.get(), from - 3, from);
            QTRY_VERIFY_WITH_TIMEOUT(t->hearingLive(), test::kRoomyMs);
            speak(r.get(), from, from + 2);
            m_heard.release = true;
            QTRY_COMPARE_WITH_TIMEOUT(live.windows(), round + 1, test::kRoomyMs);
        }
        QCOMPARE(live.status(), u"slow"_s);
        QVERIFY(t->liveSlow());
        QVERIFY(!t->liveReady());
        const int heard = m_heard.windows.load();
        speak(r.get(), 12, 18);
        QCOMPARE(m_heard.windows.load(), heard);
        // The next recording: off with this model, until tried again.
        r->cancel();
        r->start();
        QCOMPARE(live.status(), u"off"_s);
        r->cancel();
        t->retryLive();
        r->start();
        QCOMPARE(live.status(), u"waiting"_s);
        r->cancel();
    }

    void cancelledWhenTheRecordingEnds()
    {
        auto t = transcriber(true);
        auto r = recorder();
        if (!r->available())
            QSKIP("this build records nothing (no Qt Multimedia)");
        auto live = std::make_unique<LiveTranscript>();
        live->setTranscriber(t.get());
        live->setRecorder(r.get());
        r->start();
        m_heard.release = false;
        speak(r.get(), 0, 3);
        QTRY_VERIFY_WITH_TIMEOUT(t->hearingLive(), test::kRoomyMs);
        // The window being heard is told to stop; what it heard is never shown.
        r->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!t->hearingLive(), test::kRoomyMs);
        QCOMPARE(live->text(), QString());
        m_heard.release = true;
        // Gone while a window is heard: nothing happens after.
        r->start();
        m_heard.release = false;
        speak(r.get(), 0, 3);
        QTRY_VERIFY_WITH_TIMEOUT(t->hearingLive(), test::kRoomyMs);
        live.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!t->hearingLive(), test::kRoomyMs);
        m_heard.release = true;
        r->cancel();
    }
};

QTEST_GUILESS_MAIN(TestMediaLive)
#include "tst_media_live.moc"
