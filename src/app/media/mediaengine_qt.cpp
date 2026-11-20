// MediaEngine, MediaProber and decodeAudio() over Qt Multimedia (BRULION_MULTIMEDIA): QMediaPlayer
// with its sound on the system's default output, a second QMediaPlayer without sound that reads
// files' lengths and first pictures, QAudioDecoder for recordings that are not WAV files.
#include "mediaengine.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

#include <QtMath>

#include <algorithm>
#include <cstring>

using namespace Qt::StringLiterals;

namespace brulion::media {

Q_LOGGING_CATEGORY(lcMedia, "brulion.media")

namespace {

class QtMediaEngine final : public MediaEngine {
public:
    explicit QtMediaEngine(QObject *parent) : MediaEngine(parent)
    {
        m_player.setAudioOutput(&m_audio);
        // The output follows the system's default (headphones plugged in while it plays).
        connect(&m_devices, &QMediaDevices::audioOutputsChanged, this,
                [this] { m_audio.setDevice(QMediaDevices::defaultAudioOutput()); });
        connect(&m_player, &QMediaPlayer::positionChanged, this, &MediaEngine::positionChanged);
        connect(&m_player, &QMediaPlayer::durationChanged, this, &MediaEngine::durationChanged);
        connect(&m_player, &QMediaPlayer::hasVideoChanged, this, &MediaEngine::hasVideoChanged);
        connect(&m_player, &QMediaPlayer::playbackStateChanged, this, &MediaEngine::stateChanged);
        connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia)
                Q_EMIT ended();
            // A position asked for before the file had loaded.
            if ((status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) && m_pending >= 0) {
                const qint64 at = std::exchange(m_pending, -1);
                m_player.setPosition(at);
            }
            Q_EMIT stateChanged();
        });
        connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &text) {
            qCWarning(lcMedia).noquote() << u"cannot play %1: %2"_s.arg(m_player.source().toDisplayString(), text);
            Q_EMIT errorChanged();
            Q_EMIT stateChanged();
        });
    }

    bool available() const override { return true; }
    void setSource(const QUrl &url) override
    {
        m_pending = -1;
        m_player.setSource(url);
        Q_EMIT errorChanged();
    }
    void play() override { m_player.play(); }
    void pause() override { m_player.pause(); }
    void stop() override { m_player.stop(); }
    void setPosition(qint64 ms) override
    {
        const QMediaPlayer::MediaStatus status = m_player.mediaStatus();
        if (status == QMediaPlayer::LoadingMedia || status == QMediaPlayer::NoMedia)
            m_pending = ms;
        else
            m_player.setPosition(ms);
    }
    qint64 position() const override { return m_pending >= 0 ? m_pending : m_player.position(); }
    qint64 duration() const override { return m_player.duration() > 0 ? m_player.duration() : -1; }
    bool playing() const override { return m_player.playbackState() == QMediaPlayer::PlayingState; }
    bool loading() const override
    {
        const QMediaPlayer::MediaStatus status = m_player.mediaStatus();
        return status == QMediaPlayer::LoadingMedia || status == QMediaPlayer::StalledMedia
               || (status == QMediaPlayer::BufferingMedia && playing());
    }
    bool hasVideo() const override { return m_player.hasVideo(); }
    void setRate(double rate) override { m_player.setPlaybackRate(rate); }
    void setVolume(double volume) override
    {
        // (Perceived loudness: the slider is linear in what one hears.)
        m_audio.setVolume(
                float(QAudio::convertVolume(volume, QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale)));
    }
    void setMuted(bool muted) override { m_audio.setMuted(muted); }
    void setVideoOutput(QObject *output) override { m_player.setVideoOutput(output); }
    QString error() const override
    {
        return m_player.error() == QMediaPlayer::NoError ? QString() : m_player.errorString();
    }

private:
    QMediaDevices m_devices;
    QMediaPlayer m_player;
    QAudioOutput m_audio;
    qint64 m_pending = -1;
};

/// The first picture of a video and the length of any file: a player without sound, its pictures
/// into a sink; a file at a time, 15 seconds at most each.
class QtMediaProber final : public MediaProber {
public:
    explicit QtMediaProber(QObject *parent) : MediaProber(parent)
    {
        m_player.setVideoSink(&m_sink);
        m_timeout.setSingleShot(true);
        m_timeout.setInterval(15000);
        connect(&m_timeout, &QTimer::timeout, this, [this] { finish(u"it took too long to open"_s); });
        connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if (m_current.isEmpty())
                return;
            if (status == QMediaPlayer::InvalidMedia) {
                finish(m_player.errorString().isEmpty() ? u"not a file this device plays"_s : m_player.errorString());
            } else if (status == QMediaPlayer::LoadedMedia) {
                m_info.durationMs = m_player.duration() > 0 ? m_player.duration() : -1;
                m_info.hasVideo = m_player.hasVideo();
                if (!m_info.hasVideo) {
                    finish(QString());
                } else {
                    // Its first picture: played (without sound) until one arrives.
                    m_player.play();
                }
            } else if (status == QMediaPlayer::EndOfMedia) {
                finish(QString());
            }
        });
        connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &text) {
            if (!m_current.isEmpty())
                finish(text.isEmpty() ? u"not a file this device plays"_s : text);
        });
        connect(&m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (m_current.isEmpty() || !m_info.hasVideo || !frame.isValid() || !m_info.poster.isNull())
                return;
            QImage image = frame.toImage();
            if (image.isNull())
                return;
            m_info.videoSize = image.size();
            if (image.width() > 960)
                image = image.scaledToWidth(960, Qt::SmoothTransformation);
            m_info.poster = image.convertToFormat(QImage::Format_RGB32);
            finish(QString());
        });
    }

    void probe(const QUrl &url) override
    {
        if (url == m_current || m_queue.contains(url))
            return;
        m_queue.append(url);
        if (m_current.isEmpty())
            next();
    }

private:
    void next()
    {
        if (m_queue.isEmpty())
            return;
        m_current = m_queue.takeFirst();
        m_info = MediaInfo();
        m_timeout.start();
        m_player.setSource(m_current);
    }

    void finish(const QString &error)
    {
        if (m_current.isEmpty())
            return;
        m_timeout.stop();
        const QUrl url = std::exchange(m_current, QUrl());
        MediaInfo info = std::exchange(m_info, MediaInfo());
        if (info.durationMs < 0 && m_player.duration() > 0)
            info.durationMs = m_player.duration();
        info.ok = error.isEmpty();
        info.error = error;
        m_player.stop();
        m_player.setSource(QUrl());
        Q_EMIT probed(url, info);
        // (The next file after this event: the player let the last one go.)
        QTimer::singleShot(0, this, [this] { next(); });
    }

    QMediaPlayer m_player;
    QVideoSink m_sink;
    QTimer m_timeout;
    QList<QUrl> m_queue;
    QUrl m_current;
    MediaInfo m_info;
};

} // namespace

std::unique_ptr<MediaEngine> createMediaEngine(QObject *parent)
{
    return std::make_unique<QtMediaEngine>(parent);
}

bool mediaSupported()
{
    return true;
}

std::unique_ptr<MediaProber> createMediaProber(QObject *parent)
{
    return std::make_unique<QtMediaProber>(parent);
}

std::optional<QList<float>> decodeAudio(const QString &path, const std::atomic<bool> *cancel, QString *error,
                                        const std::function<void(int)> &progress)
{
    const auto failed = [error](const QString &why) {
        if (error)
            *error = why;
        return std::nullopt;
    };
    // A WAV file: its samples as they are (no decoder needed, also for the recorder's own).
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return failed(u"cannot read %1"_s.arg(QFileInfo(path).fileName()));
        const QByteArray head = file.peek(12);
        if (head.startsWith("RIFF") || head.startsWith("RF64")) {
            QString why;
            const std::optional<Pcm> pcm = readWav(file.readAll(), &why);
            if (!pcm)
                return failed(why);
            return resample(toMono(pcm->samples, pcm->channels), pcm->sampleRate, SpeechSampleRate);
        }
    }
    QAudioDecoder decoder;
    QAudioFormat format;
    format.setSampleRate(SpeechSampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    decoder.setAudioFormat(format);
    decoder.setSource(QUrl::fromLocalFile(path));
    QList<float> mono;
    QString why;
    QEventLoop loop;
    qint64 totalMs = -1;
    int lastPercent = -1;
    QTimer poll; // (cancel is set from another thread: looked at now and then)
    poll.setInterval(100);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (cancel && cancel->load()) {
            decoder.stop();
            loop.quit();
        }
    });
    QObject::connect(&decoder, &QAudioDecoder::durationChanged, &loop, [&](qint64 ms) { totalMs = ms; });
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        const QAudioBuffer buffer = decoder.read();
        if (!buffer.isValid())
            return;
        const QAudioFormat f = buffer.format();
        const qsizetype frames = buffer.frameCount();
        const int channels = std::max(1, f.channelCount());
        // (Decoders give the asked format where they can; else any other, converted here.)
        QList<float> samples(frames * channels);
        for (qsizetype i = 0; i < frames * channels; ++i)
            samples[i] = f.normalizedSampleValue(buffer.constData<char>() + i * f.bytesPerSample());
        const QList<float> one = toMono(samples, channels);
        mono += f.sampleRate() == SpeechSampleRate ? one : resample(one, f.sampleRate(), SpeechSampleRate);
        if (progress && totalMs > 0) {
            const int percent = int(std::clamp<qint64>(buffer.startTime() / 1000 * 100 / totalMs, 0, 100));
            if (percent != lastPercent)
                progress(lastPercent = percent);
        }
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(
            &decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop, [&](QAudioDecoder::Error) {
                why = decoder.errorString().isEmpty() ? u"the recording cannot be decoded"_s : decoder.errorString();
                loop.quit();
            });
    decoder.start();
    poll.start();
    loop.exec();
    if (cancel && cancel->load())
        return failed(u"stopped"_s);
    if (!why.isEmpty())
        return failed(why);
    return mono;
}

} // namespace brulion::media
