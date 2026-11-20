// MediaEngine, MediaProber and decodeAudio() in a build without Qt Multimedia (BRULION_MULTIMEDIA
// off): nothing plays (media embeds are chips with "Open in another app", the player view says so),
// nothing is probed, and only WAV recordings can be transcribed.
#include "mediaengine.h"

#include <QFile>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace brulion::media {

namespace {

class NoMediaEngine final : public MediaEngine {
public:
    using MediaEngine::MediaEngine;
    bool available() const override { return false; }
    void setSource(const QUrl &) override { }
    void play() override { }
    void pause() override { }
    void stop() override { }
    void setPosition(qint64) override { }
    qint64 position() const override { return 0; }
    qint64 duration() const override { return -1; }
    bool playing() const override { return false; }
    bool loading() const override { return false; }
    bool hasVideo() const override { return false; }
    void setRate(double) override { }
    void setVolume(double) override { }
    void setMuted(bool) override { }
    void setVideoOutput(QObject *) override { }
    QString error() const override { return u"this build of Brulion plays no audio or video"_s; }
};

class NoMediaProber final : public MediaProber {
public:
    using MediaProber::MediaProber;
    void probe(const QUrl &) override { }
};

} // namespace

std::unique_ptr<MediaEngine> createMediaEngine(QObject *parent)
{
    return std::make_unique<NoMediaEngine>(parent);
}

bool mediaSupported()
{
    return false;
}

std::unique_ptr<MediaProber> createMediaProber(QObject *parent)
{
    return std::make_unique<NoMediaProber>(parent);
}

std::optional<QList<float>> decodeAudio(const QString &path, const std::atomic<bool> *cancel, QString *error,
                                        const std::function<void(int)> &)
{
    const auto failed = [error](const QString &why) {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (cancel && cancel->load())
        return failed(u"stopped"_s);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failed(u"cannot read %1"_s.arg(QFileInfo(path).fileName()));
    QString why;
    const std::optional<Pcm> pcm = readWav(file.readAll(), &why);
    if (!pcm)
        return failed(u"this build of Brulion decodes only WAV recordings (%1)"_s.arg(why));
    return resample(toMono(pcm->samples, pcm->channels), pcm->sampleRate, SpeechSampleRate);
}

} // namespace brulion::media
