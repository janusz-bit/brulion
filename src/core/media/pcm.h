#pragma once
// Sound as numbers (part of the media module, media/media.h): WAV files — what the recorder writes
// where the device encodes nothing better, and what speech recognition reads without a decoder —,
// samples as floats in [-1, 1], mono at 16 kHz for Whisper (speech/speech.h), and the level the
// recorder's meter shows.
//
//   std::optional<media::Pcm> pcm = media::readWav(bytes, &error);
//   QList<float> speech = media::resample(media::toMono(pcm->samples, pcm->channels), pcm->sampleRate, 16000);

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace brulion::media {

/// Samples of one recording: interleaved (left, right, left, …) floats in [-1, 1].
struct Pcm {
    int sampleRate = 0;
    int channels = 0;
    QList<float> samples;
    qint64 frames() const { return channels > 0 ? samples.size() / channels : 0; }
    qint64 durationMs() const { return sampleRate > 0 ? frames() * 1000 / sampleRate : 0; }
};

/// The 44 bytes that start a WAV file of 16-bit little-endian samples, `dataBytes` of them (a
/// recorder writes it with 0 first and again with the size once it stops).
QByteArray wavHeader(int sampleRate, int channels, qint64 dataBytes);

/// A whole WAV file of `pcm` as 16-bit samples (each clamped to [-1, 1], rounded).
QByteArray wavFile(const Pcm &pcm);

/// `samples` (floats in [-1, 1]) as 16-bit little-endian bytes, what a WAV file's data holds.
QByteArray toPcm16(const float *samples, qsizetype count);

/// The samples of a WAV file (RIFF/WAVE or RF64): integer PCM of 8, 16, 24 or 32 bits, floats of
/// 32 or 64 bits, also as WAVE_FORMAT_EXTENSIBLE, any number of channels (1–16), any rate
/// (1 Hz – 768 kHz). A data chunk shorter than it says (a recording cut off) gives what is there;
/// a size of 0 or 0xFFFFFFFF in the data chunk (a recorder that never wrote its sizes back) means
/// "to the end of the file". Anything else — compressed formats (μ-law, ADPCM, MP3 in WAV), no
/// `fmt ` before `data`, a truncated header — is nullopt with `error` saying why.
std::optional<Pcm> readWav(const QByteArray &bytes, QString *error = nullptr);

/// Interleaved samples of `channels` channels as one channel: their mean.
QList<float> toMono(const QList<float> &interleaved, int channels);

/// One channel from `fromRate` to `toRate` samples a second: a windowed-sinc resampler (a low-pass
/// below the lower rate's half first, so nothing above it folds back). The same rate: unchanged.
QList<float> resample(const QList<float> &mono, int fromRate, int toRate);

/// The sample rate Whisper listens at.
inline constexpr int SpeechSampleRate = 16000;

/// The level a meter shows for these samples: their peak in decibels below full scale mapped
/// linearly from -60 dB (0) to 0 dB (1); silence 0.
double meterLevel(const float *samples, qsizetype count);
/// The same for 16-bit samples.
double meterLevel(const qint16 *samples, qsizetype count);

} // namespace brulion::media
