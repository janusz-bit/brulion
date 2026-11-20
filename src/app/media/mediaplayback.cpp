#include "mediaplayback.h"

#include "media/media.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QLocale>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::media {

namespace {

MediaPlayback *g_instance = nullptr;

const QList<double> kRates{0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};

} // namespace

MediaPlayback::MediaPlayback(std::unique_ptr<MediaEngine> engine, std::unique_ptr<MediaProber> prober, QObject *parent)
    : QObject(parent)
    , m_engine(engine ? std::move(engine) : createMediaEngine())
    , m_prober(prober ? std::move(prober) : createMediaProber())
{
    m_engine->setParent(nullptr);
    connect(m_engine.get(), &MediaEngine::positionChanged, this, &MediaPlayback::onPosition);
    connect(m_engine.get(), &MediaEngine::durationChanged, this, &MediaPlayback::durationChanged);
    connect(m_engine.get(), &MediaEngine::hasVideoChanged, this, &MediaPlayback::hasVideoChanged);
    connect(m_engine.get(), &MediaEngine::stateChanged, this, &MediaPlayback::stateChanged);
    connect(m_engine.get(), &MediaEngine::errorChanged, this, &MediaPlayback::errorChanged);
    connect(m_engine.get(), &MediaEngine::ended, this, [this] {
        m_atEnd = true;
        m_positions.remove(m_path);
        Q_EMIT ended(m_path);
    });
    connect(m_prober.get(), &MediaProber::probed, this, &MediaPlayback::onProbed);
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!cache.isEmpty())
        m_posterFolder = cache + u"/media-posters"_s;
}

MediaPlayback::~MediaPlayback()
{
    if (g_instance == this)
        g_instance = nullptr;
    m_engine->setVideoOutput(nullptr);
}

MediaPlayback *MediaPlayback::instance()
{
    if (!g_instance) {
        // Lives as long as the app: let go with it, the player's threads with it.
        g_instance = new MediaPlayback();
        g_instance->setParent(QCoreApplication::instance());
    }
    return g_instance;
}

MediaPlayback *MediaPlayback::create(QQmlEngine *, QJSEngine *)
{
    MediaPlayback *p = instance();
    // (The QML engine must not delete the app's player.)
    QJSEngine::setObjectOwnership(p, QJSEngine::CppOwnership);
    return p;
}

bool MediaPlayback::available() const
{
    return m_engine->available();
}

QString MediaPlayback::title() const
{
    return QFileInfo(m_path).fileName();
}

bool MediaPlayback::playing() const
{
    return !m_path.isEmpty() && m_engine->playing();
}

bool MediaPlayback::loading() const
{
    return !m_path.isEmpty() && m_engine->loading();
}

double MediaPlayback::position() const
{
    return m_path.isEmpty() ? 0.0 : double(m_engine->position());
}

double MediaPlayback::duration() const
{
    if (m_path.isEmpty())
        return -1;
    if (m_engine->duration() > 0)
        return double(m_engine->duration());
    const auto it = m_info.constFind(m_path);
    return it != m_info.cend() && it->media.durationMs > 0 ? double(it->media.durationMs) : -1.0;
}

bool MediaPlayback::hasVideo() const
{
    return !m_path.isEmpty() && m_engine->hasVideo();
}

void MediaPlayback::setRate(double rate)
{
    rate = std::clamp(rate, 0.25, 4.0);
    if (qFuzzyCompare(rate, m_rate))
        return;
    m_rate = rate;
    m_engine->setRate(rate);
    Q_EMIT rateChanged();
}

void MediaPlayback::setVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(volume + 1, m_volume + 1))
        return;
    m_volume = volume;
    m_engine->setVolume(volume);
    // Turned up from silence: heard again.
    if (m_muted && volume > 0) {
        m_muted = false;
        m_engine->setMuted(false);
    }
    Q_EMIT volumeChanged();
}

void MediaPlayback::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
    m_engine->setMuted(muted);
    Q_EMIT volumeChanged();
}

QString MediaPlayback::error() const
{
    return m_path.isEmpty() ? QString() : m_engine->error();
}

void MediaPlayback::remember()
{
    if (m_path.isEmpty())
        return;
    const qint64 at = m_engine->position();
    const qint64 total = m_engine->duration();
    // Played to (almost) the end: from the start next time.
    if (m_atEnd || at <= 0 || (total > 0 && at >= total - 500))
        m_positions.remove(m_path);
    else
        m_positions.insert(m_path, at);
}

void MediaPlayback::load(const QString &path, const QUrl &url)
{
    remember();
    m_engine->pause();
    m_path = path;
    m_url = url;
    m_atEnd = false;
    m_engine->setSource(url);
    m_engine->setRate(m_rate);
    m_engine->setVolume(m_volume);
    m_engine->setMuted(m_muted);
    updateVideoOutput();
    const int views = m_viewCounts.value(path);
    if (views != m_views) {
        m_views = views;
        Q_EMIT viewsChanged();
    }
    Q_EMIT sourceChanged();
    Q_EMIT durationChanged();
    Q_EMIT positionChanged();
    Q_EMIT hasVideoChanged();
    Q_EMIT stateChanged();
    Q_EMIT errorChanged();
}

void MediaPlayback::play(const QString &path, const QUrl &url, double startMs, double endMs)
{
    if (path.isEmpty() || !available())
        return;
    const qint64 start = startMs >= 0 ? qint64(startMs) : -1;
    const qint64 end = endMs >= 0 && endMs > startMs ? qint64(endMs) : -1;
    if (path != m_path || url != m_url) {
        const qint64 left = m_positions.value(path, 0);
        load(path, url);
        m_startMs = start;
        m_endMs = end;
        Q_EMIT sourceChanged();
        m_engine->setPosition(start >= 0 ? start : left);
    } else if (start != m_startMs || end != m_endMs) {
        // The same file from another fragment (another embed of it, a link with #t=).
        m_startMs = start;
        m_endMs = end;
        Q_EMIT sourceChanged();
        if (start >= 0)
            m_engine->setPosition(start);
        m_atEnd = false;
    } else if (m_atEnd) {
        // Played to its end: again from its start.
        m_engine->setPosition(std::max<qint64>(0, m_startMs));
        m_atEnd = false;
    }
    m_engine->play();
}

void MediaPlayback::toggle(const QString &path, const QUrl &url, double startMs, double endMs)
{
    if (path == m_path && playing())
        pause();
    else
        play(path, url, startMs, endMs);
}

void MediaPlayback::pause()
{
    m_engine->pause();
    remember();
}

void MediaPlayback::stop()
{
    if (m_path.isEmpty())
        return;
    remember();
    m_engine->stop();
    m_path.clear();
    updateVideoOutput();
    m_engine->setSource(QUrl());
    m_url.clear();
    m_startMs = -1;
    m_endMs = -1;
    m_atEnd = false;
    if (m_views != 0) {
        m_views = 0;
        Q_EMIT viewsChanged();
    }
    Q_EMIT sourceChanged();
    Q_EMIT stateChanged();
    Q_EMIT positionChanged();
    Q_EMIT durationChanged();
    Q_EMIT hasVideoChanged();
    Q_EMIT errorChanged();
}

void MediaPlayback::seek(double ms)
{
    if (m_path.isEmpty())
        return;
    qint64 at = std::max<qint64>(0, qint64(ms));
    if (m_engine->duration() > 0)
        at = std::min(at, m_engine->duration());
    m_atEnd = false;
    // A seek past the fragment's end lets the file play on.
    if (m_endMs >= 0 && at >= m_endMs) {
        m_endMs = -1;
        Q_EMIT sourceChanged();
    }
    m_engine->setPosition(at);
    Q_EMIT positionChanged();
}

void MediaPlayback::skip(double deltaMs)
{
    seek(position() + deltaMs);
}

void MediaPlayback::cycleRate()
{
    for (double r : kRates) {
        if (r > m_rate + 0.01) {
            setRate(r);
            return;
        }
    }
    setRate(kRates.first());
}

QList<double> MediaPlayback::rates() const
{
    return kRates;
}

QString MediaPlayback::rateText(double rate) const
{
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    return locale.toString(rate, 'g', 3) + u"×"_s;
}

double MediaPlayback::positionOf(const QString &path) const
{
    if (path == m_path && !m_path.isEmpty())
        return position();
    return double(m_positions.value(path, 0));
}

QString MediaPlayback::timeText(double ms, double totalMs) const
{
    return formatTime(qint64(ms), qint64(totalMs));
}

QString MediaPlayback::momentLink(const QString &target, double ms) const
{
    const qint64 at = qint64(ms) / 1000 * 1000;
    const QString fragment = at > 0 ? timeFragment(TimeRange{at, -1}) : QString();
    return u"[["_s + target + (fragment.isEmpty() ? QString() : u'#' + fragment) + u"]]"_s;
}

void MediaPlayback::onPosition()
{
    // The fragment's end: it pauses there (as a browser does with #t=10,20).
    if (m_endMs >= 0 && m_engine->playing() && m_engine->position() >= m_endMs) {
        m_engine->pause();
        m_engine->setPosition(m_endMs);
        m_atEnd = true;
        Q_EMIT ended(m_path);
    }
    Q_EMIT positionChanged();
}

void MediaPlayback::addView(const QString &path)
{
    if (path.isEmpty())
        return;
    ++m_viewCounts[path];
    if (path == m_path) {
        m_views = m_viewCounts.value(path);
        Q_EMIT viewsChanged();
    }
}

void MediaPlayback::removeView(const QString &path)
{
    auto it = m_viewCounts.find(path);
    if (it == m_viewCounts.end())
        return;
    if (--it.value() <= 0)
        m_viewCounts.erase(it);
    if (path == m_path) {
        m_views = m_viewCounts.value(path);
        Q_EMIT viewsChanged();
    }
}

void MediaPlayback::updateVideoOutput()
{
    QObject *top = nullptr;
    for (qsizetype i = m_surfaces.size() - 1; i >= 0; --i) {
        if (!m_surfaces.at(i).output) {
            m_surfaces.removeAt(i);
            continue;
        }
        if (!top && !m_path.isEmpty() && m_surfaces.at(i).path == m_path)
            top = m_surfaces.at(i).output.data();
    }
    if (top == m_videoOutput.data())
        return;
    m_videoOutput = top;
    m_engine->setVideoOutput(top);
}

void MediaPlayback::attachVideo(const QString &path, QObject *output)
{
    if (!output)
        return;
    m_surfaces.removeIf([output](const Surface &s) { return s.output == output; });
    m_surfaces.append(Surface{output, path});
    updateVideoOutput();
}

void MediaPlayback::detachVideo(QObject *output)
{
    if (!output)
        return;
    const qsizetype before = m_surfaces.size();
    m_surfaces.removeIf([output](const Surface &s) { return s.output == output || !s.output; });
    if (m_surfaces.size() == before && output != m_videoOutput)
        return;
    updateVideoOutput();
    // A video plays only where it is seen.
    if (!m_videoOutput && !m_path.isEmpty() && m_engine->hasVideo() && m_engine->playing())
        pause();
}

QVariantMap MediaPlayback::info(const QString &path, const QUrl &url)
{
    if (path.isEmpty())
        return {{u"state"_s, u"unknown"_s}, {u"duration"_s, -1}, {u"hasVideo"_s, false}, {u"poster"_s, QString()}};
    auto it = m_info.find(path);
    if (it == m_info.end() || (it->url != url && !url.isEmpty())) {
        if (!available() || url.isEmpty())
            return {{u"state"_s, u"unknown"_s}, {u"duration"_s, -1}, {u"hasVideo"_s, false}, {u"poster"_s, QString()}};
        Info fresh;
        fresh.state = u"loading"_s;
        fresh.url = url;
        // A poster kept from an earlier run of the same file (its name, size and time).
        const QString cached = posterPath(path, url);
        if (!cached.isEmpty() && QFileInfo::exists(cached))
            fresh.poster = QUrl::fromLocalFile(cached).toString();
        it = m_info.insert(path, fresh);
        m_probing.insert(url, path);
        m_prober->probe(url);
    }
    const Info &i = it.value();
    return {{u"state"_s, i.state},
            {u"duration"_s, double(i.media.durationMs)},
            {u"hasVideo"_s, i.media.hasVideo},
            {u"poster"_s, i.poster},
            {u"width"_s, i.media.videoSize.width()},
            {u"height"_s, i.media.videoSize.height()},
            {u"error"_s, i.media.error}};
}

void MediaPlayback::forget(const QString &path)
{
    if (m_info.remove(path) > 0) {
        ++m_infoRevision;
        Q_EMIT infoChanged(path);
    }
}

QString MediaPlayback::posterPath(const QString &path, const QUrl &url) const
{
    if (m_posterFolder.isEmpty() || !url.isLocalFile())
        return {};
    const QFileInfo file(url.toLocalFile());
    if (!file.exists())
        return {};
    const QByteArray key = (path + u'\n' + QString::number(file.size()) + u'\n'
                            + QString::number(file.lastModified().toMSecsSinceEpoch()))
                                   .toUtf8();
    return m_posterFolder + u'/' + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex())
           + u".png"_s;
}

void MediaPlayback::onProbed(const QUrl &url, const MediaInfo &media)
{
    const QString path = m_probing.take(url);
    auto it = m_info.find(path);
    if (path.isEmpty() || it == m_info.end() || it->url != url)
        return;
    it->media = media;
    it->state = media.ok ? u"ready"_s : u"failed"_s;
    if (!media.poster.isNull()) {
        const QString file = posterPath(path, url);
        if (!file.isEmpty() && QDir().mkpath(QFileInfo(file).absolutePath()) && media.poster.save(file, "PNG"))
            it->poster = QUrl::fromLocalFile(file).toString() + u"?r="_s + QString::number(++m_infoRevision);
    }
    ++m_infoRevision;
    if (path == m_path)
        Q_EMIT durationChanged();
    Q_EMIT infoChanged(path);
}

} // namespace brulion::media
