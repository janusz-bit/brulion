// Qt Multimedia behind the app's player and recorder (src/app/media: mediaengine_qt.cpp,
// voicerecorder_qt.cpp), on files made here — no sound card, no network, no files of the
// repository: a tone written as WAV (core media::wavFile) and the same tone recorded by this
// build's own recorder (a fed microphone) as M4A and Ogg where it encodes. Each is probed (length,
// no picture), played to its end, decoded to 16 kHz mono for speech recognition (the tone's pitch
// kept), and recorded with pauses (the pause leaves no sound) or cancelled (no file left). With
// BRULION_MEDIA_SAMPLES naming a folder of other files (mp3, mp4, webm, …), each is probed too and
// what it is printed (a video's first frame must come). Skipped in a build without Qt Multimedia.

#include "mediaengine.h"
#include "voicerecorder.h"

#include "media/pcm.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <testtiming.h>

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;
using namespace brulion;

namespace {

/// `seconds` of a sine at `hz`, at `rate`, `channels` (the same in each).
media::Pcm tone(double hz, int rate, int channels, double seconds)
{
    media::Pcm pcm;
    pcm.sampleRate = rate;
    pcm.channels = channels;
    const int frames = int(seconds * rate);
    pcm.samples.resize(qsizetype(frames) * channels);
    for (int f = 0; f < frames; ++f) {
        const float v = 0.4f * float(std::sin(2 * std::numbers::pi * hz * f / rate));
        for (int c = 0; c < channels; ++c)
            pcm.samples[qsizetype(f) * channels + c] = v;
    }
    return pcm;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

/// The strongest of `candidates` (Hz) in `samples` at 16 kHz (a single-bin DFT each).
double strongest(const QList<float> &samples, const QList<double> &candidates)
{
    double best = 0;
    double bestPower = -1;
    for (const double hz : candidates) {
        double re = 0;
        double im = 0;
        for (qsizetype i = 0; i < samples.size(); ++i) {
            const double a = 2 * std::numbers::pi * hz * double(i) / media::SpeechSampleRate;
            re += samples[i] * std::cos(a);
            im += samples[i] * std::sin(a);
        }
        const double power = re * re + im * im;
        if (power > bestPower) {
            bestPower = power;
            best = hz;
        }
    }
    return best;
}

/// Records `seconds` of a tone through `recorder`'s feed (in pieces, as a microphone gives it).
void feedTone(media::VoiceRecorder &recorder, double hz, double seconds)
{
    const media::Pcm pcm = tone(hz, 48000, 1, seconds);
    const QByteArray bytes = media::toPcm16(pcm.samples.constData(), pcm.samples.size());
    constexpr qsizetype Piece = 48000 / 10 * 2; // 100 ms
    for (qsizetype at = 0; at < bytes.size(); at += Piece) {
        recorder.feed(bytes.mid(at, Piece));
        QCoreApplication::processEvents();
    }
}

} // namespace

class TestMediaEngine : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_wav;

    /// Probes `path`: what it is.
    media::MediaInfo probe(const QString &path)
    {
        std::unique_ptr<media::MediaProber> prober = media::createMediaProber();
        QSignalSpy probed(prober.get(), &media::MediaProber::probed);
        prober->probe(QUrl::fromLocalFile(path));
        if (!probed.wait(test::kRoomyMs))
            return {};
        return probed.first().at(1).value<media::MediaInfo>();
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (!media::mediaSupported())
            QSKIP("this build has no Qt Multimedia (BRULION_MULTIMEDIA)");
        QVERIFY(m_dir.isValid());
        m_wav = m_dir.filePath(u"ton żółwia 440 Hz.wav"_s);
        QVERIFY(writeFile(m_wav, media::wavFile(tone(440, 44100, 2, 2.0))));
    }

    void probesWav()
    {
        const media::MediaInfo info = probe(m_wav);
        QVERIFY2(info.ok, qPrintable(info.error));
        QVERIFY2(std::abs(info.durationMs - 2000) < 100, qPrintable(QString::number(info.durationMs)));
        QVERIFY(!info.hasVideo);
        QVERIFY(info.poster.isNull());
    }

    void probesAQueue()
    {
        // One prober, many files asked at once (a page of embeds): each answered, in order, the
        // same file asked twice answered once.
        QStringList files;
        for (int i = 1; i <= 4; ++i) {
            const QString path = m_dir.filePath(u"kolejka %1.wav"_s.arg(i));
            QVERIFY(writeFile(path, media::wavFile(tone(300 + 100 * i, 22050, 1, 0.5 * i))));
            files << path;
        }
        std::unique_ptr<media::MediaProber> prober = media::createMediaProber();
        QSignalSpy probed(prober.get(), &media::MediaProber::probed);
        for (const QString &f : files)
            prober->probe(QUrl::fromLocalFile(f));
        prober->probe(QUrl::fromLocalFile(files.first()));
        QTRY_COMPARE_WITH_TIMEOUT(probed.size(), files.size(), test::kRoomyMs);
        for (int i = 0; i < files.size(); ++i) {
            QCOMPARE(probed.at(i).at(0).toUrl(), QUrl::fromLocalFile(files.at(i)));
            const media::MediaInfo info = probed.at(i).at(1).value<media::MediaInfo>();
            QVERIFY2(info.ok, qPrintable(info.error));
            QVERIFY2(std::abs(info.durationMs - 500 * (i + 1)) < 100, qPrintable(QString::number(info.durationMs)));
        }
        const QString folder = qEnvironmentVariable("BRULION_MEDIA_SAMPLES");
        if (folder.isEmpty())
            return;
        probed.clear();
        const QFileInfoList samples = QDir(folder).entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &f : samples)
            prober->probe(QUrl::fromLocalFile(f.absoluteFilePath()));
        QTRY_COMPARE_WITH_TIMEOUT(probed.size(), samples.size(), test::kRoomyMs);
        for (const QList<QVariant> &answer : std::as_const(probed)) {
            const media::MediaInfo info = answer.at(1).value<media::MediaInfo>();
            QVERIFY2(info.ok, qPrintable(answer.at(0).toUrl().fileName() + u": "_s + info.error));
        }
    }

    void probesBrokenFile()
    {
        const QString broken = m_dir.filePath(u"zepsuty.mp3"_s);
        QVERIFY(writeFile(broken, QByteArray(2000, 'x')));
        const media::MediaInfo info = probe(broken);
        QVERIFY(!info.ok);
        QVERIFY(!info.error.isEmpty());
    }

    void playsToTheEnd()
    {
        // No sound card here (or one nobody hears): the clock still runs, the file plays through.
        std::unique_ptr<media::MediaEngine> engine = media::createMediaEngine();
        QVERIFY(engine->available());
        QSignalSpy ended(engine.get(), &media::MediaEngine::ended);
        engine->setMuted(true); // (the machine running the tests may have speakers)
        engine->setSource(QUrl::fromLocalFile(m_wav));
        engine->setRate(2.0);
        engine->setPosition(500);
        engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(engine->playing() || !ended.isEmpty() || !engine->error().isEmpty(), test::kRoomyMs);
        QVERIFY2(engine->error().isEmpty(), qPrintable(engine->error()));
        QTRY_VERIFY_WITH_TIMEOUT(!ended.isEmpty() || !engine->error().isEmpty(), test::kRoomyMs);
        QVERIFY2(engine->error().isEmpty(), qPrintable(engine->error()));
        QVERIFY(std::abs(engine->duration() - 2000) < 100);
        QVERIFY(!engine->playing());
    }

    void decodesWav()
    {
        QString error;
        const std::optional<QList<float>> samples = media::decodeAudio(m_wav, nullptr, &error);
        QVERIFY2(samples, qPrintable(error));
        QVERIFY(std::abs(samples->size() - 2 * media::SpeechSampleRate) < 100);
        QCOMPARE(strongest(*samples, {220, 330, 440, 550, 880}), 440.0);
    }

    void records_data()
    {
        QTest::addColumn<bool>("wavOnly");
        QTest::newRow("best format") << false;
        QTest::newRow("WAV") << true;
    }
    void records()
    {
        QFETCH(bool, wavOnly);
        media::VoiceRecorder recorder;
        recorder.setFolder(m_dir.filePath(u"nagrania"_s));
        recorder.setNamePrefix(u"Nagranie"_s);
        recorder.useFeed(48000, 1);
        recorder.setWavOnly(wavOnly);
        QSignalSpy finished(&recorder, &media::VoiceRecorder::finished);
        QSignalSpy failed(&recorder, &media::VoiceRecorder::failed);
        recorder.start();
        QCOMPARE(recorder.state(), u"recording"_s);
        if (wavOnly)
            QCOMPARE(recorder.format(), u"wav"_s);
        else
            qInfo("records as %s", qPrintable(recorder.format()));
        QVERIFY(recorder.filePath().contains(u"/Nagranie "_s));
        QVERIFY(recorder.filePath().endsWith(u'.' + recorder.format()));
        feedTone(recorder, 440, 1.5);
        QVERIFY(recorder.level() > 0.5);
        // A level for each piece (100 ms each here), at most LevelCount of them.
        QVERIFY(recorder.levels().size() >= 10 && recorder.levels().size() <= media::VoiceRecorder::LevelCount);
        // Paused: what comes is not recorded.
        recorder.pause();
        QCOMPARE(recorder.state(), u"paused"_s);
        feedTone(recorder, 880, 1.0);
        recorder.resume();
        feedTone(recorder, 440, 1.0);
        QVERIFY2(std::abs(recorder.duration() - 2500) < 50, qPrintable(QString::number(recorder.duration())));
        recorder.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() || !failed.isEmpty(), test::kRoomyMs);
        QVERIFY2(failed.isEmpty(), qPrintable(recorder.error()));
        QCOMPARE(recorder.state(), u"done"_s);
        const QString file = finished.first().at(0).toString();
        QCOMPARE(file, recorder.filePath());
        QVERIFY(QFileInfo(file).size() > 1000);

        // What it wrote plays, and holds the tone without the paused part.
        const media::MediaInfo info = probe(file);
        QVERIFY2(info.ok, qPrintable(info.error));
        QVERIFY2(std::abs(info.durationMs - 2500) < 200, qPrintable(QString::number(info.durationMs)));
        QString error;
        const std::optional<QList<float>> samples = media::decodeAudio(file, nullptr, &error);
        QVERIFY2(samples, qPrintable(error));
        QCOMPARE(strongest(*samples, {220, 440, 660, 880}), 440.0);
    }

    void cancels()
    {
        media::VoiceRecorder recorder;
        recorder.setFolder(m_dir.filePath(u"anulowane"_s));
        recorder.useFeed(16000, 1);
        recorder.start();
        feedTone(recorder, 440, 0.5);
        const QString file = recorder.filePath();
        QVERIFY(QFileInfo::exists(file));
        recorder.cancel();
        QCOMPARE(recorder.state(), u"idle"_s);
        QVERIFY(!QFileInfo::exists(file));
        QCOMPARE(recorder.duration(), 0.0);
    }

    void nothingRecorded()
    {
        media::VoiceRecorder recorder;
        recorder.setFolder(m_dir.filePath(u"puste"_s));
        recorder.useFeed(16000, 1);
        recorder.setWavOnly(true);
        QSignalSpy failed(&recorder, &media::VoiceRecorder::failed);
        recorder.start();
        recorder.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty(), test::kRoomyMs);
        QCOMPARE(recorder.state(), u"failed"_s);
        QVERIFY(recorder.filePath().isEmpty());
        QVERIFY(QDir(m_dir.filePath(u"puste"_s)).isEmpty());
    }

    void samples()
    {
        const QString folder = qEnvironmentVariable("BRULION_MEDIA_SAMPLES");
        if (folder.isEmpty())
            QSKIP("BRULION_MEDIA_SAMPLES is not set: no other files to probe");
        const QFileInfoList files = QDir(folder).entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &f : files) {
            const media::MediaInfo info = probe(f.absoluteFilePath());
            qInfo("%s: %s, %lld ms, %s, poster %dx%d", qPrintable(f.fileName()),
                  info.ok ? "ok" : qPrintable(info.error), info.durationMs, info.hasVideo ? "video" : "sound",
                  info.poster.width(), info.poster.height());
            QVERIFY2(info.ok, qPrintable(f.fileName() + u": "_s + info.error));
            QVERIFY(info.durationMs > 0);
            if (info.hasVideo)
                QVERIFY2(!info.poster.isNull(), qPrintable(f.fileName()));
        }
    }
};

QTEST_MAIN(TestMediaEngine)
#include "tst_media_engine.moc"
