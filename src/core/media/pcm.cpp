#include "media/pcm.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

using namespace Qt::StringLiterals;

namespace brulion::media {

namespace {

constexpr quint16 FormatPcm = 1;
constexpr quint16 FormatFloat = 3;
constexpr quint16 FormatExtensible = 0xFFFE;

void putU16(QByteArray &out, quint16 v)
{
    char b[2];
    qToLittleEndian(v, b);
    out.append(b, 2);
}

void putU32(QByteArray &out, quint32 v)
{
    char b[4];
    qToLittleEndian(v, b);
    out.append(b, 4);
}

quint16 u16(const char *p)
{
    return qFromLittleEndian<quint16>(p);
}

quint32 u32(const char *p)
{
    return qFromLittleEndian<quint32>(p);
}

bool fail(QString *error, const QString &why)
{
    if (error)
        *error = why;
    return false;
}

double peakLevel(double peak)
{
    if (peak <= 0)
        return 0;
    const double db = 20.0 * std::log10(std::min(1.0, peak));
    return std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
}

} // namespace

QByteArray wavHeader(int sampleRate, int channels, qint64 dataBytes)
{
    const quint32 data = quint32(std::clamp<qint64>(dataBytes, 0, 0xFFFFFFFFLL - 36));
    const quint16 ch = quint16(std::clamp(channels, 1, 16));
    const quint32 rate = quint32(std::max(1, sampleRate));
    QByteArray out;
    out.reserve(44);
    out.append("RIFF", 4);
    putU32(out, 36 + data);
    out.append("WAVE", 4);
    out.append("fmt ", 4);
    putU32(out, 16);
    putU16(out, FormatPcm);
    putU16(out, ch);
    putU32(out, rate);
    putU32(out, rate * ch * 2);
    putU16(out, quint16(ch * 2));
    putU16(out, 16);
    out.append("data", 4);
    putU32(out, data);
    return out;
}

QByteArray toPcm16(const float *samples, qsizetype count)
{
    QByteArray out(count * 2, Qt::Uninitialized);
    char *p = out.data();
    for (qsizetype i = 0; i < count; ++i) {
        const float s = std::isfinite(samples[i]) ? std::clamp(samples[i], -1.0f, 1.0f) : 0.0f;
        const auto v = qint16(std::lround(s * (s < 0 ? 32768.0f : 32767.0f)));
        qToLittleEndian(v, p + 2 * i);
    }
    return out;
}

QByteArray wavFile(const Pcm &pcm)
{
    const QByteArray data = toPcm16(pcm.samples.constData(), pcm.samples.size());
    return wavHeader(pcm.sampleRate, pcm.channels, data.size()) + data;
}

std::optional<Pcm> readWav(const QByteArray &bytes, QString *error)
{
    const char *d = bytes.constData();
    const qint64 size = bytes.size();
    if (size < 12 || (std::memcmp(d, "RIFF", 4) != 0 && std::memcmp(d, "RF64", 4) != 0)
        || std::memcmp(d + 8, "WAVE", 4) != 0) {
        fail(error, u"not a WAV file"_s);
        return std::nullopt;
    }
    const bool rf64 = std::memcmp(d, "RF64", 4) == 0;
    quint16 format = 0;
    int channels = 0;
    int rate = 0;
    int bits = 0;
    bool haveFormat = false;
    qint64 rf64Data = -1; // RF64: the data's size from its ds64 chunk
    qint64 at = 12;
    while (at + 8 <= size) {
        const char *chunk = d + at;
        const quint32 chunkSize = u32(chunk + 4);
        const qint64 body = at + 8;
        if (std::memcmp(chunk, "ds64", 4) == 0 && chunkSize >= 16 && body + 16 <= size) {
            rf64Data = qint64(qFromLittleEndian<quint64>(d + body + 8));
        } else if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (chunkSize < 16 || body + 16 > size) {
                fail(error, u"the WAV file's format is cut off"_s);
                return std::nullopt;
            }
            format = u16(d + body);
            channels = u16(d + body + 2);
            rate = int(u32(d + body + 4));
            bits = u16(d + body + 14);
            if (format == FormatExtensible) {
                if (chunkSize < 40 || body + 40 > size) {
                    fail(error, u"the WAV file's format is cut off"_s);
                    return std::nullopt;
                }
                format = u16(d + body + 24); // the sub-format GUID's first two bytes
            }
            haveFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (!haveFormat) {
                fail(error, u"the WAV file has no format before its samples"_s);
                return std::nullopt;
            }
            if (format != FormatPcm && format != FormatFloat) {
                fail(error, u"the WAV file is compressed (format %1)"_s.arg(format));
                return std::nullopt;
            }
            const bool floats = format == FormatFloat;
            if ((floats && bits != 32 && bits != 64)
                || (!floats && bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
                fail(error, u"the WAV file has %1-bit samples"_s.arg(bits));
                return std::nullopt;
            }
            if (channels < 1 || channels > 16 || rate < 1 || rate > 768000) {
                fail(error, u"the WAV file has %1 channels at %2 Hz"_s.arg(channels).arg(rate));
                return std::nullopt;
            }
            qint64 length = chunkSize;
            if (rf64 && chunkSize == 0xFFFFFFFFu && rf64Data >= 0)
                length = rf64Data;
            else if (chunkSize == 0 || chunkSize == 0xFFFFFFFFu)
                length = size - body;
            length = std::min(length, size - body);
            const int width = bits / 8;
            const qint64 frames = length / (qint64(width) * channels);
            Pcm pcm;
            pcm.sampleRate = rate;
            pcm.channels = channels;
            pcm.samples.resize(frames * channels);
            float *out = pcm.samples.data();
            const char *p = d + body;
            for (qint64 i = 0; i < frames * channels; ++i, p += width) {
                float v = 0;
                if (floats && bits == 32) {
                    v = qFromLittleEndian<float>(p);
                } else if (floats) {
                    v = float(qFromLittleEndian<double>(p));
                } else if (bits == 8) {
                    v = (float(quint8(*p)) - 128.0f) / 128.0f;
                } else if (bits == 16) {
                    v = float(qFromLittleEndian<qint16>(p)) / 32768.0f;
                } else if (bits == 24) {
                    const qint32 s = qint32(quint32(quint8(p[0])) << 8 | quint32(quint8(p[1])) << 16
                                            | quint32(quint8(p[2])) << 24)
                                     >> 8;
                    v = float(s) / 8388608.0f;
                } else {
                    v = float(double(qFromLittleEndian<qint32>(p)) / 2147483648.0);
                }
                out[i] = std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;
            }
            return pcm;
        }
        // Chunks are padded to an even size.
        at = body + qint64(chunkSize) + (chunkSize & 1);
    }
    fail(error, haveFormat ? u"the WAV file has no samples"_s : u"the WAV file has no format"_s);
    return std::nullopt;
}

QList<float> toMono(const QList<float> &interleaved, int channels)
{
    if (channels <= 1)
        return interleaved;
    const qsizetype frames = interleaved.size() / channels;
    QList<float> out(frames);
    const float *in = interleaved.constData();
    for (qsizetype f = 0; f < frames; ++f) {
        float sum = 0;
        for (int c = 0; c < channels; ++c)
            sum += in[f * channels + c];
        out[f] = sum / float(channels);
    }
    return out;
}

QList<float> resample(const QList<float> &mono, int fromRate, int toRate)
{
    if (fromRate <= 0 || toRate <= 0 || fromRate == toRate || mono.isEmpty())
        return mono;
    // A windowed sinc (Blackman) of HalfTaps zero crossings either side, its cut-off below the
    // lower rate's half (0.95 of it: room for the window's own slope). The kernel comes from a
    // table (TableSteps points per zero crossing, linearly interpolated): an hour of 48 kHz
    // sound is some 100 taps for each of 58 million samples out.
    constexpr int HalfTaps = 16;
    constexpr int TableSteps = 256;
    static const QList<float> kernel = [] {
        QList<float> table(HalfTaps * TableSteps + 2);
        for (qsizetype i = 0; i < table.size(); ++i) {
            const double x = double(i) / TableSteps;
            if (x >= HalfTaps) {
                table[i] = 0;
                continue;
            }
            const double sinc = x < 1e-9 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
            const double w = 0.42 + 0.5 * std::cos(std::numbers::pi * x / HalfTaps)
                             + 0.08 * std::cos(2 * std::numbers::pi * x / HalfTaps);
            table[i] = float(sinc * w);
        }
        return table;
    }();
    const double ratio = double(toRate) / double(fromRate);
    const double cutoff = 0.95 * std::min(1.0, ratio); // of the input's Nyquist rate
    const double step = 1.0 / ratio;                   // input samples per output sample
    const double reach = HalfTaps / cutoff;            // input samples either side
    const qsizetype inCount = mono.size();
    const auto outCount = qsizetype(std::floor(double(inCount) * ratio));
    QList<float> out(outCount);
    const float *in = mono.constData();
    const float *table = kernel.constData();
    const double scale = cutoff * TableSteps;
    for (qsizetype o = 0; o < outCount; ++o) {
        const double centre = double(o) * step;
        const auto first = qsizetype(std::max(0.0, std::ceil(centre - reach)));
        const auto last = qsizetype(std::min(double(inCount - 1), std::floor(centre + reach)));
        float sum = 0;
        float weights = 0;
        for (qsizetype i = first; i <= last; ++i) {
            const double t = std::abs(double(i) - centre) * scale;
            const auto k = qsizetype(t);
            if (k >= HalfTaps * TableSteps)
                continue;
            const float f = float(t - double(k));
            const float w = table[k] + f * (table[k + 1] - table[k]);
            sum += w * in[i];
            weights += w;
        }
        // (Normalised by the taps' sum: the edges, where the kernel is cut, keep their level.)
        out[o] = weights != 0 ? sum / weights : 0.0f;
    }
    return out;
}

double meterLevel(const float *samples, qsizetype count)
{
    double peak = 0;
    for (qsizetype i = 0; i < count; ++i) {
        if (std::isfinite(samples[i]))
            peak = std::max(peak, double(std::abs(samples[i])));
    }
    return peakLevel(peak);
}

double meterLevel(const qint16 *samples, qsizetype count)
{
    int peak = 0;
    for (qsizetype i = 0; i < count; ++i)
        peak = std::max(peak, std::abs(int(samples[i])));
    return peakLevel(double(peak) / 32768.0);
}

} // namespace brulion::media
