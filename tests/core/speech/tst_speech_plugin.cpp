// The plugin with the speech recognizer (src/core/speech/speechrunner.h: brulion_speech, loaded the
// way the app loads it), in builds that have it: it is found and loaded with llama.cpp's ggml from
// its own folder, a missing model is an error and not a crash. With a model:
//
//   BRULION_SPEECH_MODEL=~/.cache/brulion-whisper/ggml-base-q8_0.bin ctest -R tst_speech_plugin
//
// whisper.cpp hears a generated recording (a tone between silences: no words, no crash, the right
// length), cancels, and — with BRULION_SPEECH_SAMPLE naming a WAV of speech (any rate and channels;
// e.g. `espeak-ng -v pl -w sample.wav "Kupić mleko i chleb jutro rano"`) — writes what it said;
// BRULION_SPEECH_EXPECT, a comma-separated list of words, must all be in it (any case), and
// BRULION_SPEECH_LANGUAGE (default "auto") is the language asked for. The sample is also heard as
// text while recording would hear it (speech/live.h: windows of a few seconds, greedily) — the
// same words must be in that text.
//
// Measuring the threads (docs/PERFORMANCE.md "Speech recognition on a busy processor"):
// BRULION_SPEECH_THREADS=n computes with n threads (default: chosen by the free cores); the times
// and the threads are printed.

#include "media/pcm.h"
#include "speech/live.h"
#include "speech/load.h"
#include "speech/speechrunner.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;
using namespace brulion;
using speech::SpeechRunner;

namespace {

/// `seconds` of silence, a 440 Hz tone, silence again — 16 kHz mono.
QList<float> tone(double seconds)
{
    const int n = int(seconds * media::SpeechSampleRate);
    QList<float> out(n, 0.0f);
    for (int i = n / 3; i < 2 * n / 3; ++i)
        out[i] = 0.3f * float(std::sin(2 * std::numbers::pi * 440.0 * i / media::SpeechSampleRate));
    return out;
}

} // namespace

class TestSpeechPlugin : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void isFound()
    {
        const QString dir = QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR);
        QVERIFY(speech::hasSpeechRunner({dir}));
        QTemporaryDir empty;
        QVERIFY(!speech::hasSpeechRunner({empty.path()}));
        QVERIFY(!speech::hasSpeechRunner({}));
        QVERIFY(speech::hasSpeechRunner({empty.path(), dir}));
        // By its name alone (Android): nothing to look at, so it is taken to be there.
        QVERIFY(speech::hasSpeechRunner({QString()}));

        QString error;
        SpeechRunner::Options options;
        options.modelPath = u"/nonexistent/ggml-small.bin"_s;
        QVERIFY(!speech::loadSpeechRunner({empty.path()}, options, &error));
        QVERIFY2(error.contains(u"recognizes no speech"), qPrintable(error));
    }

    void missingAndBrokenModel()
    {
        // The plugin loads ggml from next to itself and a processor plugin this machine runs; the
        // model's file is missing, then no model at all.
        const QString dir = QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR);
        QString error;
        SpeechRunner::Options options;
        options.modelPath = u"/nonexistent/ggml-small.bin"_s;
        QVERIFY(!speech::loadSpeechRunner({dir}, options, &error));
        QVERIFY2(error.contains(u"not on this device") || error.contains(u"too old"), qPrintable(error));

        QTemporaryDir tmp;
        const QString broken = tmp.filePath(u"ggml-zły model.bin"_s);
        QFile f(broken);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'x'));
        f.close();
        options.modelPath = broken;
        error.clear();
        QVERIFY(!speech::loadSpeechRunner({dir}, options, &error));
        QVERIFY2(error.contains(u"cannot be read") || error.contains(u"too old"), qPrintable(error));
    }

    void hears()
    {
        const QString model = qEnvironmentVariable("BRULION_SPEECH_MODEL");
        if (model.isEmpty())
            QSKIP("BRULION_SPEECH_MODEL is not set: no Whisper model to run");
        QString error;
        const std::unique_ptr<SpeechRunner> runner = open(model, &error);
        QVERIFY2(runner, qPrintable(error));
        qInfo("%s", qPrintable(runner->device()));

        // A tone: no words worth keeping, but the recording is heard to its end.
        QList<int> progress;
        SpeechRunner::Params params;
        params.language = u"pl"_s;
        params.progress = [&progress](int p) { progress.append(p); };
        const SpeechRunner::Result r = runner->transcribe(tone(4.0), params);
        QVERIFY2(r.status == SpeechRunner::Ok, qPrintable(r.error));
        QCOMPARE(r.audioMs, 4000);
        QCOMPARE(r.language, u"pl"_s);
        QVERIFY(!progress.isEmpty() && progress.last() == 100);
        QVERIFY(r.threads >= 1 && r.threads <= speech::MaxSpeechThreads);
        for (const speech::Segment &s : r.segments)
            QVERIFY(s.startMs >= 0 && s.endMs >= s.startMs && s.endMs <= 4500);

        // Cancelled before it starts: it says so.
        std::atomic<bool> cancel{true};
        SpeechRunner::Params cancelled;
        cancelled.cancel = &cancel;
        QCOMPARE(runner->transcribe(tone(4.0), cancelled).status, SpeechRunner::Cancelled);

        // Nothing to hear.
        QCOMPARE(runner->transcribe({}, params).status, SpeechRunner::Ok);

        const QString sample = qEnvironmentVariable("BRULION_SPEECH_SAMPLE");
        if (sample.isEmpty())
            return;
        const QList<float> samples = readSample(sample, &error);
        QVERIFY2(!samples.isEmpty(), qPrintable(error));
        SpeechRunner::Params speech;
        speech.language = qEnvironmentVariable("BRULION_SPEECH_LANGUAGE", u"auto"_s);
        QList<speech::Segment> live;
        speech.heard = [&live](const speech::Segment &s) { live.append(s); };
        const SpeechRunner::Result heard = runner->transcribe(samples, speech);
        QVERIFY2(heard.status == SpeechRunner::Ok, qPrintable(heard.error));
        QCOMPARE(live, heard.segments);
        const QString text = speech::spokenTexts(heard.segments).join(u' ');
        qInfo("heard (%s, %lld ms of sound in %lld ms, %d threads): %s", qPrintable(heard.language), heard.audioMs,
              heard.elapsedMs, heard.threads, qPrintable(text));
        expectWords(text);
    }

    void hearsLive()
    {
        const QString model = qEnvironmentVariable("BRULION_SPEECH_MODEL");
        const QString sample = qEnvironmentVariable("BRULION_SPEECH_SAMPLE");
        if (model.isEmpty() || sample.isEmpty())
            QSKIP("BRULION_SPEECH_MODEL and BRULION_SPEECH_SAMPLE are not both set");
        QString error;
        const std::unique_ptr<SpeechRunner> runner = open(model, &error);
        QVERIFY2(runner, qPrintable(error));
        const QList<float> samples = readSample(sample, &error);
        QVERIFY2(!samples.isEmpty(), qPrintable(error));

        // The recording as the microphone would give it, a tenth of a second at a time; a window
        // heard whenever one is due (as if hearing took no time: the windows a fast device hears).
        speech::LiveWindows live(media::SpeechSampleRate);
        SpeechRunner::Params params;
        params.language = qEnvironmentVariable("BRULION_SPEECH_LANGUAGE", u"auto"_s);
        params.live = true;
        const qsizetype piece = media::SpeechSampleRate / 10;
        int windows = 0;
        qint64 longest = 0;
        qint64 total = 0;
        for (qsizetype at = 0; at < samples.size(); at += piece) {
            live.append(samples.constData() + at, std::min(piece, samples.size() - at));
            if (!live.due())
                continue;
            const speech::LiveWindows::Window w = live.take();
            const SpeechRunner::Result r = runner->transcribe(w.samples, params);
            QVERIFY2(r.status == SpeechRunner::Ok, qPrintable(r.error));
            live.heard(w, r.segments);
            ++windows;
            longest = std::max(longest, r.elapsedMs);
            total += r.elapsedMs;
        }
        // What is left once the recording ends: heard as one last window.
        if (const speech::LiveWindows::Window w = live.take(); !w.isNull()) {
            const SpeechRunner::Result r = runner->transcribe(w.samples, params);
            QVERIFY2(r.status == SpeechRunner::Ok, qPrintable(r.error));
            live.heard(w, r.segments);
            ++windows;
            total += r.elapsedMs;
        }
        qInfo("live: %d windows of %lld ms of sound, %lld ms in all, the longest %lld ms: %s", windows,
              live.recordedMs(), total, longest, qPrintable(live.text()));
        QVERIFY(windows >= int(samples.size() / media::SpeechSampleRate / 3));
        expectWords(live.text());
    }

private:
    static std::unique_ptr<SpeechRunner> open(const QString &model, QString *error)
    {
        SpeechRunner::Options options;
        options.modelPath = model;
        options.threads = qEnvironmentVariableIntValue("BRULION_SPEECH_THREADS");
        return speech::loadSpeechRunner({QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR)}, options, error);
    }

    /// A WAV of any rate and channels as 16 kHz mono; empty with `error`.
    static QList<float> readSample(const QString &path, QString *error)
    {
        QFile wav(path);
        if (!wav.open(QIODevice::ReadOnly)) {
            *error = path + u": "_s + wav.errorString();
            return {};
        }
        const std::optional<media::Pcm> pcm = media::readWav(wav.readAll(), error);
        if (!pcm)
            return {};
        return media::resample(media::toMono(pcm->samples, pcm->channels), pcm->sampleRate, media::SpeechSampleRate);
    }

    static void expectWords(const QString &text)
    {
        const QStringList expected = qEnvironmentVariable("BRULION_SPEECH_EXPECT").split(u',', Qt::SkipEmptyParts);
        for (const QString &word : expected)
            QVERIFY2(text.contains(word.trimmed(), Qt::CaseInsensitive), qPrintable(word + u" in: "_s + text));
    }
};

QTEST_GUILESS_MAIN(TestSpeechPlugin)
#include "tst_speech_plugin.moc"
