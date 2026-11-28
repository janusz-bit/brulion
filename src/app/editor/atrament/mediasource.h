// MediaSource — the app's one player of sounds and videos as the page drawn by Atrament sees it
// (docs/ATRAMENT.md step 5, "Sounds and videos"): MediaPlayback (src/app/media/mediaplayback.h)
// behind AtramentView, a fake in the tests — the editor's library knows no player. One file plays
// at a time: a card shows the player's state while its file is the one loaded, else where its file
// was left (positionOf) and the length the file's info says.
#pragma once

#include "base/geometry.h"

#include <QString>

#include <functional>
#include <map>
#include <memory>

namespace brulion::atrament {
struct ImageData;
}

namespace brulion::editor::atr {

class MediaSource {
public:
    // What a file is (MediaPlayback::info), read in the background: `state` "unknown" until it is
    // known (onChanged then), "ready" or "failed".
    struct Info {
        QString state = QStringLiteral("unknown");
        double duration = -1; // ms; −1 unknown
        bool hasVideo = false;
        QString poster; // the URL of a video's first picture ("": none)
        int width = 0, height = 0;
    };
    // The player.
    struct State {
        QString path; // the file loaded ("": none)
        bool playing = false;
        bool loading = false;
        double position = 0; // ms
        double duration = 0; // ms; 0 unknown
        double rate = 1;
    };

    virtual ~MediaSource() = default;
    // Something plays here (false: a build without Qt Multimedia, the browser — the card offers
    // another app).
    virtual bool available() const = 0;
    virtual State state() const = 0;
    // What `path` is; asked for in the background when it is not known yet.
    virtual Info info(const QString &path) = 0;
    // Where `path` was left (0: never played, or to its end).
    virtual double positionOf(const QString &path) const = 0;
    virtual QString timeText(double ms, double totalMs) const = 0; // "1:23"
    virtual QString rateText(double rate) const = 0;               // "1.5×"
    // Plays `path` from `startMs` (≥ 0: its fragment's start) when it is not playing, else pauses.
    virtual void toggle(const QString &path, double startMs, double endMs) = 0;
    // Goes to `ms` of `path`: the loaded file seeks, another one starts there.
    virtual void seek(const QString &path, double ms, double endMs) = 0;
    virtual void cycleRate() = 0;
    // The cards showing `path` (the now-playing bar shows a sound no view shows).
    virtual void addView(const QString &path) = 0;
    virtual void removeView(const QString &path) = 0;
    // The loaded video's picture while `path` shows it (null: none yet); showVideo(path, false)
    // when its card goes (a video plays only where it is seen).
    virtual void showVideo(const QString &path, bool shown) = 0;
    virtual std::shared_ptr<const atrament::ImageData> videoFrame() const = 0;

    // The views drawing cards listen (each by its key): the player's state or a file's info
    // changed, a new picture of the video — tellChanged() tells them all.
    void listen(const void *key, std::function<void()> changed) { m_listeners[key] = std::move(changed); }
    void unlisten(const void *key) { m_listeners.erase(key); }

protected:
    void tellChanged() const
    {
        const auto listeners = m_listeners; // (a listener may stop listening)
        for (const auto &[key, changed] : listeners)
            changed();
    }

private:
    std::map<const void *, std::function<void()>> m_listeners;
};

} // namespace brulion::editor::atr
