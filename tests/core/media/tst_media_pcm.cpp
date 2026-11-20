// Sound as numbers (src/core/media/pcm.h): WAV files written and read back in every sample format
// a recorder or another program writes, damaged and cut-off files, the mix to one channel, the
// resampler (a tone keeps its pitch and level, what lies above the new rate's half is gone), and
// the meter's level.
#include "media/pcm.h"

#include <QTest>
#include <QtEndian>

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;
using namespace brulion;

namespace {

QList<float> sine(double hz, int rate, double seconds, float amplitude)
{
    const int n = int(seconds * rate);
    QList<float> out(n);
    for (int i = 0; i < n; ++i)
        out[i] = amplitude * float(std::sin(2 * std::numbers::pi * hz * i / rate));
    return out;
}

/// The amplitude of `hz` in `samples` (a single-bin DFT over whole periods' worth of samples).
double amplitudeAt(const QList<float> &samples, int rate, double hz)
{
    double re = 0;
    double im = 0;
    for (qsizetype i = 0; i < samples.size(); ++i) {
        const double a = 2 * std::numbers::pi * hz * double(i) / rate;
        re += samples[i] * std::cos(a);
        im += samples[i] * std::sin(a);
    }
    return 2 * std::sqrt(re * re + im * im) / double(samples.size());
}

void putU16(QByteArray &b, quint16 v)
{
    char c[2];
    qToLittleEndian(v, c);
    b.append(c, 2);
}
void putU32(QByteArray &b, quint32 v)
{
    char c[4];
    qToLittleEndian(v, c);
    b.append(c, 4);
}

/// A WAV file by hand: format tag, channels, rate, bits, the data chunk's size as written (-1: the
/// real size) and its bytes; `extensible` writes WAVE_FORMAT_EXTENSIBLE with `format` as the
/// sub-format; `extraChunk` a LIST chunk of odd size before the data.
QByteArray wav(quint16 format, quint16 channels, quint32 rate, quint16 bits, const QByteArray &data,
               qint64 statedSize = -1, bool extensible = false, bool extraChunk = false)
{
    QByteArray fmt;
    putU16(fmt, extensible ? 0xFFFE : format);
    putU16(fmt, channels);
    putU32(fmt, rate);
    putU32(fmt, rate * channels * bits / 8);
    putU16(fmt, quint16(channels * bits / 8));
    putU16(fmt, bits);
    if (extensible) {
        putU16(fmt, 22);
        putU16(fmt, bits);
        putU32(fmt, 0);
        putU16(fmt, format);
        fmt.append("\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71", 14);
    }
    QByteArray body = "WAVE";
    body += "fmt ";
    putU32(body, quint32(fmt.size()));
    body += fmt;
    if (extraChunk) {
        body += "LIST";
        putU32(body, 5);
        body += QByteArray("INFOx", 5);
        body += '\0'; // (padding to an even size)
    }
    body += "data";
    putU32(body, quint32(statedSize < 0 ? data.size() : statedSize));
    body += data;
    QByteArray out = "RIFF";
    putU32(out, quint32(body.size()));
    return out + body;
}

} // namespace

class TestMediaPcm : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void header()
    {
        const QByteArray h = media::wavHeader(16000, 1, 32000);
        QCOMPARE(h.size(), 44);
        QVERIFY(h.startsWith("RIFF"));
        QCOMPARE(qFromLittleEndian<quint32>(h.constData() + 4), 36u + 32000u);
        QCOMPARE(h.mid(8, 8), QByteArray("WAVEfmt "));
        QCOMPARE(qFromLittleEndian<quint16>(h.constData() + 20), quint16(1)); // PCM
        QCOMPARE(qFromLittleEndian<quint16>(h.constData() + 22), quint16(1));
        QCOMPARE(qFromLittleEndian<quint32>(h.constData() + 24), 16000u);
        QCOMPARE(qFromLittleEndian<quint32>(h.constData() + 28), 32000u);
        QCOMPARE(qFromLittleEndian<quint16>(h.constData() + 34), quint16(16));
        QCOMPARE(h.mid(36, 4), QByteArray("data"));
        QCOMPARE(qFromLittleEndian<quint32>(h.constData() + 40), 32000u);
    }

    void roundTrip()
    {
        media::Pcm pcm;
        pcm.sampleRate = 48000;
        pcm.channels = 2;
        pcm.samples = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 0.25f, 2.0f /* clamped */, -3.0f};
        const QByteArray file = media::wavFile(pcm);
        QCOMPARE(file.size(), 44 + 16);
        QString error;
        const std::optional<media::Pcm> back = media::readWav(file, &error);
        QVERIFY2(back, qPrintable(error));
        QCOMPARE(back->sampleRate, 48000);
        QCOMPARE(back->channels, 2);
        QCOMPARE(back->frames(), 4);
        const QList<float> want{0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 0.25f, 1.0f, -1.0f};
        for (qsizetype i = 0; i < want.size(); ++i)
            QVERIFY2(std::abs(back->samples[i] - want[i]) < 1.0f / 16000, qPrintable(QString::number(i)));
        // NaN and infinity are silence.
        const QList<float> odd{std::nanf(""), INFINITY};
        const QByteArray bytes = media::toPcm16(odd.constData(), odd.size());
        QCOMPARE(bytes, QByteArray(4, '\0'));
    }

    void durations()
    {
        media::Pcm pcm;
        pcm.sampleRate = 16000;
        pcm.channels = 1;
        pcm.samples = sine(440, 16000, 2.5, 0.5f);
        QCOMPARE(pcm.durationMs(), 2500);
        pcm.channels = 2;
        QCOMPARE(pcm.durationMs(), 1250);
        QCOMPARE(media::Pcm().durationMs(), 0);
    }

    void formats_data()
    {
        QTest::addColumn<QByteArray>("file");
        QTest::addColumn<int>("channels");
        QTest::addColumn<int>("rate");
        QTest::addColumn<QList<float>>("samples");
        QByteArray d8;
        d8 += char(128);
        d8 += char(255);
        d8 += char(0);
        QTest::newRow("8-bit unsigned") << wav(1, 1, 8000, 8, d8) << 1 << 8000
                                        << QList<float>{0.0f, 127.0f / 128.0f, -1.0f};
        QByteArray d16;
        putU16(d16, 0x4000);
        putU16(d16, 0xC000);
        QTest::newRow("16-bit stereo") << wav(1, 2, 44100, 16, d16) << 2 << 44100 << QList<float>{0.5f, -0.5f};
        const QByteArray d24("\x00\x00\x40\x00\x00\xC0", 6);
        QTest::newRow("24-bit") << wav(1, 1, 48000, 24, d24) << 1 << 48000 << QList<float>{0.5f, -0.5f};
        QByteArray d32;
        putU32(d32, 0x40000000u);
        QTest::newRow("32-bit integer") << wav(1, 1, 96000, 32, d32) << 1 << 96000 << QList<float>{0.5f};
        QByteArray f32;
        char fb[4];
        qToLittleEndian(0.25f, fb);
        f32.append(fb, 4);
        qToLittleEndian(-0.75f, fb);
        f32.append(fb, 4);
        QTest::newRow("32-bit float") << wav(3, 1, 22050, 32, f32) << 1 << 22050 << QList<float>{0.25f, -0.75f};
        QByteArray f64;
        char db[8];
        qToLittleEndian(0.125, db);
        f64.append(db, 8);
        QTest::newRow("64-bit float") << wav(3, 1, 16000, 64, f64) << 1 << 16000 << QList<float>{0.125f};
        QTest::newRow("extensible, 16-bit")
                << wav(1, 2, 44100, 16, d16, -1, true) << 2 << 44100 << QList<float>{0.5f, -0.5f};
        QTest::newRow("extensible, float")
                << wav(3, 1, 22050, 32, f32, -1, true) << 1 << 22050 << QList<float>{0.25f, -0.75f};
        QTest::newRow("a chunk of odd size before the data")
                << wav(1, 2, 44100, 16, d16, -1, false, true) << 2 << 44100 << QList<float>{0.5f, -0.5f};
        // A recorder that never wrote its sizes back: to the end of the file.
        QTest::newRow("data size 0") << wav(1, 2, 44100, 16, d16, 0) << 2 << 44100 << QList<float>{0.5f, -0.5f};
        QTest::newRow("data size 0xFFFFFFFF")
                << wav(1, 2, 44100, 16, d16, 0xFFFFFFFFLL) << 2 << 44100 << QList<float>{0.5f, -0.5f};
        // Cut off: what is there, whole frames only.
        QTest::newRow("cut off") << wav(1, 2, 44100, 16, d16 + QByteArray("\x01", 1), 400) << 2 << 44100
                                 << QList<float>{0.5f, -0.5f};
        QTest::newRow("no samples") << wav(1, 1, 16000, 16, QByteArray()) << 1 << 16000 << QList<float>{};
    }
    void formats()
    {
        QFETCH(QByteArray, file);
        QFETCH(int, channels);
        QFETCH(int, rate);
        QFETCH(QList<float>, samples);
        QString error;
        const std::optional<media::Pcm> pcm = media::readWav(file, &error);
        QVERIFY2(pcm, qPrintable(error));
        QCOMPARE(pcm->channels, channels);
        QCOMPARE(pcm->sampleRate, rate);
        QCOMPARE(pcm->samples.size(), samples.size());
        for (qsizetype i = 0; i < samples.size(); ++i)
            QVERIFY2(std::abs(pcm->samples[i] - samples[i]) < 1e-4f, qPrintable(QString::number(pcm->samples[i])));
    }

    void broken_data()
    {
        QTest::addColumn<QByteArray>("file");
        QTest::addColumn<QString>("why");
        QByteArray d16;
        putU16(d16, 0x4000);
        QTest::newRow("empty") << QByteArray() << u"not a WAV"_s;
        QTest::newRow("not RIFF") << QByteArray("OggS\0\0\0\0WAVE", 12) << u"not a WAV"_s;
        QTest::newRow("RIFF but AVI") << QByteArray("RIFF\4\0\0\0AVI ", 12) << u"not a WAV"_s;
        QTest::newRow("μ-law") << wav(7, 1, 8000, 8, d16) << u"compressed"_s;
        QTest::newRow("ADPCM") << wav(2, 1, 8000, 4, d16) << u"compressed"_s;
        QTest::newRow("MP3 in WAV") << wav(0x55, 1, 44100, 0, d16) << u"compressed"_s;
        QTest::newRow("12-bit") << wav(1, 1, 8000, 12, d16) << u"12-bit"_s;
        QTest::newRow("16-bit float") << wav(3, 1, 8000, 16, d16) << u"16-bit"_s;
        QTest::newRow("no channels") << wav(1, 0, 8000, 16, d16) << u"0 channels"_s;
        QTest::newRow("too many channels") << wav(1, 32, 8000, 16, d16) << u"32 channels"_s;
        QTest::newRow("rate 0") << wav(1, 1, 0, 16, d16) << u"0 Hz"_s;
        QByteArray noFmt = "RIFF";
        putU32(noFmt, 12);
        noFmt += "WAVEdata";
        putU32(noFmt, 2);
        noFmt += d16;
        QTest::newRow("data before fmt") << noFmt << u"no format before"_s;
        QByteArray cut = wav(1, 1, 8000, 16, d16).left(30);
        QTest::newRow("cut inside fmt") << cut << u"cut off"_s;
        QTest::newRow("only a header") << QByteArray("RIFF\4\0\0\0WAVE", 12) << u"no format"_s;
    }
    void broken()
    {
        QFETCH(QByteArray, file);
        QFETCH(QString, why);
        QString error;
        QVERIFY(!media::readWav(file, &error));
        QVERIFY2(error.contains(why), qPrintable(error));
        QVERIFY(!media::readWav(file)); // (no error wanted: no crash)
    }

    void mono()
    {
        QCOMPARE(media::toMono({0.5f, -0.5f, 1.0f, 0.0f}, 2), (QList<float>{0.0f, 0.5f}));
        const QList<float> three = media::toMono({0.25f, 0.5f, 0.75f}, 3);
        QCOMPARE(three.size(), 1);
        QVERIFY(std::abs(three.first() - 0.5f) < 1e-6f);
        QCOMPARE(media::toMono({0.25f, 0.5f}, 1), (QList<float>{0.25f, 0.5f}));
        // A frame cut off at the end goes.
        QCOMPARE(media::toMono({1.0f, 1.0f, 0.5f}, 2), (QList<float>{1.0f}));
    }

    void resample_data()
    {
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::newRow("48 kHz to 16 kHz") << 48000 << 16000;
        QTest::newRow("44.1 kHz to 16 kHz") << 44100 << 16000;
        QTest::newRow("8 kHz to 16 kHz") << 8000 << 16000;
        QTest::newRow("22.05 kHz to 16 kHz") << 22050 << 16000;
    }
    void resample()
    {
        QFETCH(int, from);
        QFETCH(int, to);
        // A 1 kHz tone keeps its pitch and level; one above the new rate's half (9 kHz at 16 kHz)
        // does not fold back into what can be heard.
        const QList<float> in = sine(1000, from, 1.0, 0.5f);
        const QList<float> out = media::resample(in, from, to);
        QCOMPARE(out.size(), qsizetype(to));
        // (Away from the edges, where the filter sees only half of its taps.)
        const QList<float> middle = out.mid(to / 10, to * 8 / 10);
        QVERIFY2(std::abs(amplitudeAt(middle, to, 1000) - 0.5) < 0.02,
                 qPrintable(QString::number(amplitudeAt(middle, to, 1000))));
        if (from > to) {
            const QList<float> high = media::resample(sine(9000, from, 1.0, 0.5f), from, to);
            const QList<float> highMiddle = high.mid(to / 10, to * 8 / 10);
            double rms = 0;
            for (const float s : highMiddle)
                rms += double(s) * s;
            rms = std::sqrt(rms / double(highMiddle.size()));
            QVERIFY2(rms < 0.02, qPrintable(QString::number(rms)));
        }
    }

    void resampleEdges()
    {
        const QList<float> in{0.1f, 0.2f, 0.3f};
        QCOMPARE(media::resample(in, 16000, 16000), in);
        QCOMPARE(media::resample(in, 0, 16000), in);
        QCOMPARE(media::resample(in, 16000, -1), in);
        QVERIFY(media::resample({}, 48000, 16000).isEmpty());
        // A constant stays that constant to the edges (the filter's taps are normalised).
        const QList<float> flat(4800, 0.25f);
        const QList<float> out = media::resample(flat, 48000, 16000);
        QCOMPARE(out.size(), 1600);
        for (const float s : out)
            QVERIFY(std::abs(s - 0.25f) < 1e-3f);
    }

    void meter()
    {
        QCOMPARE(media::meterLevel(static_cast<const float *>(nullptr), 0), 0.0);
        const QList<float> silence(100, 0.0f);
        QCOMPARE(media::meterLevel(silence.constData(), silence.size()), 0.0);
        const QList<float> full{0.1f, -1.0f, 0.3f};
        QCOMPARE(media::meterLevel(full.constData(), full.size()), 1.0);
        // -20 dB (0.1): two thirds of the way from -60 dB.
        const QList<float> tenth{0.1f};
        QVERIFY(std::abs(media::meterLevel(tenth.constData(), 1) - 2.0 / 3.0) < 1e-6);
        // Below -60 dB: nothing.
        const QList<float> quiet{0.0005f};
        QCOMPARE(media::meterLevel(quiet.constData(), 1), 0.0);
        const QList<float> nan{std::nanf(""), 0.1f};
        QVERIFY(std::abs(media::meterLevel(nan.constData(), 2) - 2.0 / 3.0) < 1e-6);
        const QList<qint16> s16{0, -32768, 100};
        QCOMPARE(media::meterLevel(s16.constData(), s16.size()), 1.0);
        const QList<qint16> s16tenth{3277};
        QVERIFY(std::abs(media::meterLevel(s16tenth.constData(), 1) - 2.0 / 3.0) < 1e-3);
    }
};

QTEST_GUILESS_MAIN(TestMediaPcm)
#include "tst_media_pcm.moc"
