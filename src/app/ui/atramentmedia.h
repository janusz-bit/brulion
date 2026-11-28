// AtramentMediaSource — the app's one player (MediaPlayback, src/app/media) as the page drawn by
// Atrament sees it (editor/atrament/mediasource.h; docs/ATRAMENT.md step 5, "Sounds and videos"):
// its state and actions, a file's info (read in the background, onChanged when known), and a
// video's picture — a QVideoSink attached to the player while a card shows the video (where Qt
// Multimedia is built), its frames made Atrament pictures.
#pragma once

#ifdef BRULION_HAS_ATRAMENT_EDITOR

#include "mediasource.h"

#include <QObject>
#include <QPointer>
#include <QUrl>

#include <functional>
#include <memory>

namespace brulion::media {
class MediaPlayback;
}

class AtramentMediaSource final : public QObject, public brulion::editor::atr::MediaSource {
public:
    // `urlOf`: a vault path's URL (EditorHost::assetUrl), what the player loads.
    explicit AtramentMediaSource(std::function<QUrl(const QString &)> urlOf, QObject *parent = nullptr);
    ~AtramentMediaSource() override;

    bool available() const override;
    State state() const override;
    Info info(const QString &path) override;
    double positionOf(const QString &path) const override;
    QString timeText(double ms, double totalMs) const override;
    QString rateText(double rate) const override;
    void toggle(const QString &path, double startMs, double endMs) override;
    void seek(const QString &path, double ms, double endMs) override;
    void cycleRate() override;
    void addView(const QString &path) override;
    void removeView(const QString &path) override;
    void showVideo(const QString &path, bool shown) override;
    std::shared_ptr<const brulion::atrament::ImageData> videoFrame() const override;

private:
    void changed();
    QPointer<brulion::media::MediaPlayback> m_player;
    std::function<QUrl(const QString &)> m_urlOf;
    QString m_videoPath; // the video a card shows (its sink attached)
    struct Video;
    std::unique_ptr<Video> m_video;
};

#endif
