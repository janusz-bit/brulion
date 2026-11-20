// MediaEngine — what plays one media file for MediaPlayback (mediaplayback.h): Qt Multimedia's
// QMediaPlayer in builds that have it (mediaengine_qt.cpp, BRULION_MULTIMEDIA), nothing otherwise
// (mediaengine_none.cpp: `available` false, the app offers "Open in another app"). Tests give
// MediaPlayback a fake engine. Also the prober that reads a file's length, whether it has a picture
// and its first frame without playing it (MediaProber), and the decoder that turns a recording
// into samples for speech recognition (decodeAudio()).
#pragma once

#include "media/pcm.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QUrl>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace brulion::media {

class MediaEngine : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~MediaEngine() override = default;

    /// Anything plays at all (false: a build without Qt Multimedia).
    virtual bool available() const = 0;
    /// The file to play ("" unloads); it loads in the background (loading()).
    virtual void setSource(const QUrl &url) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    /// Where to go once it can (also before the file has loaded).
    virtual void setPosition(qint64 ms) = 0;
    virtual qint64 position() const = 0;
    /// -1 until known.
    virtual qint64 duration() const = 0;
    virtual bool playing() const = 0;
    /// The file is being opened (or buffered): nothing to play yet.
    virtual bool loading() const = 0;
    virtual bool hasVideo() const = 0;
    virtual void setRate(double rate) = 0;
    virtual void setVolume(double volume) = 0; ///< 0–1
    virtual void setMuted(bool muted) = 0;
    /// Where the picture goes: a QML VideoOutput (qml/media/MediaVideoSurface.qml), or nullptr.
    virtual void setVideoOutput(QObject *output) = 0;
    /// Why the file does not play ("": it does).
    virtual QString error() const = 0;

Q_SIGNALS:
    void positionChanged();
    void durationChanged();
    /// playing() or loading() changed.
    void stateChanged();
    void hasVideoChanged();
    /// It played to the end.
    void ended();
    void errorChanged();
};

/// The engine of this build (QMediaPlayer, or one that plays nothing).
std::unique_ptr<MediaEngine> createMediaEngine(QObject *parent = nullptr);
/// This build plays audio and video (BRULION_MULTIMEDIA).
bool mediaSupported();

/// What a media file is, read without playing it.
struct MediaInfo {
    bool ok = false;
    QString error;
    qint64 durationMs = -1;
    bool hasVideo = false;
    QSize videoSize;
    QImage poster; ///< its first picture (a video), at most 960 px wide
};

/// Reads media files one after another in the background (MediaPlayback::info()).
class MediaProber : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~MediaProber() override = default;
    /// Read `url` (a queue: one file at a time); probed() says what it is.
    virtual void probe(const QUrl &url) = 0;

Q_SIGNALS:
    void probed(const QUrl &url, const brulion::media::MediaInfo &info);
};

std::unique_ptr<MediaProber> createMediaProber(QObject *parent = nullptr);

/// The samples of the sound file `path` as 16 kHz mono for speech recognition: a WAV file read
/// directly (media::readWav — every build), anything else decoded by Qt Multimedia (builds that
/// have it; the calling thread needs an event loop: it runs one while it decodes). nullopt with
/// `error` when it cannot be read or decoded, or `cancel` was set.
std::optional<QList<float>> decodeAudio(const QString &path, const std::atomic<bool> *cancel, QString *error,
                                        const std::function<void(int percent)> &progress = {});

} // namespace brulion::media

Q_DECLARE_METATYPE(brulion::media::MediaInfo)
