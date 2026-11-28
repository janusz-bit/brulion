#ifdef BRULION_HAS_ATRAMENT_EDITOR
#include "atramentmedia.h"

#include "mediaplayback.h"
#include "picturecache.h"

#ifdef BRULION_MULTIMEDIA
#include <QVideoFrame>
#include <QVideoSink>
#endif

using namespace Qt::StringLiterals;
using brulion::media::MediaPlayback;
namespace at = brulion::atrament;

// The sink the player draws a video into while a card shows it, and its last frame.
struct AtramentMediaSource::Video {
#ifdef BRULION_MULTIMEDIA
    QVideoSink sink;
#endif
    std::shared_ptr<const at::ImageData> frame;
};

AtramentMediaSource::AtramentMediaSource(std::function<QUrl(const QString &)> urlOf, QObject *parent)
    : QObject(parent)
    , m_player(MediaPlayback::instance())
    , m_urlOf(std::move(urlOf))
    , m_video(std::make_unique<Video>())
{
    MediaPlayback *p = m_player.data();
    for (auto signal : {&MediaPlayback::sourceChanged, &MediaPlayback::stateChanged, &MediaPlayback::positionChanged,
                        &MediaPlayback::durationChanged, &MediaPlayback::rateChanged, &MediaPlayback::availableChanged})
        connect(p, signal, this, &AtramentMediaSource::changed);
    connect(p, &MediaPlayback::infoChanged, this, &AtramentMediaSource::changed);
#ifdef BRULION_MULTIMEDIA
    connect(&m_video->sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        const QImage image = frame.toImage();
        if (image.isNull())
            return;
        auto data = brulion::editor::atr::PictureCache::imageData(image);
        m_video->frame = std::move(data);
        changed();
    });
#endif
}

AtramentMediaSource::~AtramentMediaSource()
{
#ifdef BRULION_MULTIMEDIA
    if (m_player && !m_videoPath.isEmpty())
        m_player->detachVideo(&m_video->sink);
#endif
}

void AtramentMediaSource::changed()
{
    tellChanged();
}

bool AtramentMediaSource::available() const
{
    return m_player && m_player->available();
}

AtramentMediaSource::State AtramentMediaSource::state() const
{
    State s;
    if (!m_player)
        return s;
    s.path = m_player->path();
    s.playing = m_player->playing();
    s.loading = m_player->loading();
    s.position = m_player->position();
    s.duration = m_player->duration();
    s.rate = m_player->rate();
    return s;
}

AtramentMediaSource::Info AtramentMediaSource::info(const QString &path)
{
    Info i;
    if (!m_player)
        return i;
    const QVariantMap m = m_player->info(path, m_urlOf(path));
    i.state = m.value(u"state"_s, u"unknown"_s).toString();
    i.duration = m.value(u"duration"_s, -1).toDouble();
    i.hasVideo = m.value(u"hasVideo"_s).toBool();
    i.poster = m.value(u"poster"_s).toString();
    i.width = m.value(u"width"_s).toInt();
    i.height = m.value(u"height"_s).toInt();
    return i;
}

double AtramentMediaSource::positionOf(const QString &path) const
{
    return m_player ? m_player->positionOf(path) : 0;
}

QString AtramentMediaSource::timeText(double ms, double totalMs) const
{
    return m_player ? m_player->timeText(ms, totalMs) : QString();
}

QString AtramentMediaSource::rateText(double rate) const
{
    return m_player ? m_player->rateText(rate) : QString();
}

void AtramentMediaSource::toggle(const QString &path, double startMs, double endMs)
{
    if (m_player)
        m_player->toggle(path, m_urlOf(path), startMs, endMs);
}

void AtramentMediaSource::seek(const QString &path, double ms, double endMs)
{
    if (!m_player)
        return;
    if (m_player->path() == path)
        m_player->seek(ms);
    else
        m_player->play(path, m_urlOf(path), ms, endMs);
}

void AtramentMediaSource::cycleRate()
{
    if (m_player)
        m_player->cycleRate();
}

void AtramentMediaSource::addView(const QString &path)
{
    if (m_player)
        m_player->addView(path);
}

void AtramentMediaSource::removeView(const QString &path)
{
    if (m_player)
        m_player->removeView(path);
}

void AtramentMediaSource::showVideo(const QString &path, bool shown)
{
#ifdef BRULION_MULTIMEDIA
    if (!m_player || shown == (m_videoPath == path))
        return;
    if (shown) {
        if (!m_videoPath.isEmpty())
            m_player->detachVideo(&m_video->sink);
        m_videoPath = path;
        m_player->attachVideo(path, &m_video->sink);
    } else {
        m_player->detachVideo(&m_video->sink);
        m_videoPath.clear();
        m_video->frame.reset();
    }
#else
    Q_UNUSED(path)
    Q_UNUSED(shown)
#endif
}

std::shared_ptr<const at::ImageData> AtramentMediaSource::videoFrame() const
{
    return m_video->frame;
}

#endif
