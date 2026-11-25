// MediaPlayback — the app's one player of audio and video (QML singleton `MediaPlayback`;
// docs/UI.md "Audio and video"): every media embed in a note (qml/media/MediaEmbedTile.qml), the
// player view of a media file (qml/content/ContentMediaView.qml) and the now-playing bar
// (qml/media/MediaNowPlaying.qml) show and steer this one player, so only one file plays at a
// time — starting another pauses the one before, which keeps where it was (positionOf()).
//
// What happens off screen: a sound plays on when its block scrolls away or its page closes (the
// now-playing bar shows it while no view does: `views` is 0); a video plays only where it is seen
// — when the last surface showing it goes (its block scrolled away, the view closed), it pauses.
//
// A file's length, whether it has a picture and its first frame (a video's poster) are read in the
// background, a file at a time, without playing it (info(): {state, duration, hasVideo, poster,
// width, height}; infoChanged(path)), the poster kept as a PNG in the cache folder.
//
// The engine is Qt Multimedia's (mediaengine.h; `available` false in a build without it: the views
// offer "Open in another app"), made with the first file played or looked at — not with the app,
// which would load its FFmpeg backend and connect to the system's sound server at every start. A time fragment
// (`#t=83`, `#t=10,20`: core media::parseTimeFragment) starts the file there and, with an end, pauses it there.
#pragma once

#include "mediaengine.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace brulion::media {

class MediaPlayback : public QObject {
    Q_OBJECT

    /// This build plays audio and video — and, once something was played or looked at, this system
    /// too (a build whose backend Qt cannot load here turns it false then: availableChanged()).
    Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)
    /// The vault path of the file loaded ("": none) — the one playing or paused.
    Q_PROPERTY(QString path READ path NOTIFY sourceChanged FINAL)
    /// Its file name, for the now-playing bar.
    Q_PROPERTY(QString title READ title NOTIFY sourceChanged FINAL)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged FINAL)
    /// It is being opened or buffered.
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged FINAL)
    /// In milliseconds (a real: QML has no 64-bit integers); duration -1 until known.
    Q_PROPERTY(double position READ position NOTIFY positionChanged FINAL)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged FINAL)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged FINAL)
    /// The speed (rates(): 0.5 … 2), the loudness (0–1) and the mute — for every file.
    Q_PROPERTY(double rate READ rate WRITE setRate NOTIFY rateChanged FINAL)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged FINAL)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumeChanged FINAL)
    /// Why the file does not play ("": it does).
    Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)
    /// The views showing the loaded file (embeds on screen, the player view): 0 — the now-playing
    /// bar shows it instead.
    Q_PROPERTY(int views READ views NOTIFY viewsChanged FINAL)
    /// Bumped when a file's info() changed (bindings read it to look again).
    Q_PROPERTY(int infoRevision READ infoRevision NOTIFY infoChanged FINAL)
    /// The time fragment's end the file pauses at (-1: none).
    Q_PROPERTY(double endPosition READ endPosition NOTIFY sourceChanged FINAL)

public:
    explicit MediaPlayback(std::unique_ptr<MediaEngine> engine = {}, std::unique_ptr<MediaProber> prober = {},
                           QObject *parent = nullptr);
    ~MediaPlayback() override;

    /// The app's player (made with the build's engine on first use).
    static MediaPlayback *instance();
    /// QML: the app's player.
    static MediaPlayback *create(QQmlEngine *, QJSEngine *);

    bool available() const;
    /// The engine or the prober exists (tests: neither is made before something is played or
    /// looked at).
    bool engineMade() const { return m_engine != nullptr || m_prober != nullptr; }
    QString path() const { return m_path; }
    QString title() const;
    bool playing() const;
    bool loading() const;
    double position() const;
    double duration() const;
    bool hasVideo() const;
    double rate() const { return m_rate; }
    void setRate(double rate);
    double volume() const { return m_volume; }
    void setVolume(double volume);
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    QString error() const;
    int views() const { return m_views; }
    int infoRevision() const { return m_infoRevision; }
    double endPosition() const { return double(m_endMs); }

    /// Plays the file at vault path `path` (`url`: Workspace.assetUrl): the loaded one goes on
    /// (from the start of its fragment when it stopped at its end), another one is loaded — the
    /// one before paused and its place remembered — and starts at `startMs` (≥ 0: the embed's
    /// `#t=`), else where it was left, and pauses at `endMs` (≥ 0).
    Q_INVOKABLE void play(const QString &path, const QUrl &url, double startMs = -1, double endMs = -1);
    /// Plays `path` from `ms` whatever it does now — loaded or not, playing or paused, at a
    /// fragment's end or not (a transcript's moment link clicked: docs/UI.md "Voice notes"); no
    /// fragment's end pauses it.
    Q_INVOKABLE void playAt(const QString &path, const QUrl &url, double ms);
    /// play() when it is not playing `path`, else pause().
    Q_INVOKABLE void toggle(const QString &path, const QUrl &url, double startMs = -1, double endMs = -1);
    Q_INVOKABLE void pause();
    /// Unloads the file (the now-playing bar's close): its place is remembered.
    Q_INVOKABLE void stop();
    /// Goes to `ms` of the loaded file.
    Q_INVOKABLE void seek(double ms);
    /// Forward or back by `deltaMs`.
    Q_INVOKABLE void skip(double deltaMs);
    /// The next speed of rates() (after the last: the first).
    Q_INVOKABLE void cycleRate();
    /// The speeds offered: 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2.
    Q_INVOKABLE QList<double> rates() const;
    /// "1×", "1.5×" (the speed button's text).
    Q_INVOKABLE QString rateText(double rate) const;
    /// Where `path` was left (0: never played, or played to the end); the loaded one: position.
    Q_INVOKABLE double positionOf(const QString &path) const;
    /// "1:23" / "1:02:03" (core media::formatTime).
    Q_INVOKABLE QString timeText(double ms, double totalMs) const;
    /// A link to the moment `ms` of the file `target` names: "[[Nagranie.m4a#t=83]]" (a whole
    /// second; the start: no fragment).
    Q_INVOKABLE QString momentLink(const QString &target, double ms) const;

    /// A view shows `path` (MediaEmbedTile, ContentMediaView); removeView() when it goes.
    Q_INVOKABLE void addView(const QString &path);
    Q_INVOKABLE void removeView(const QString &path);
    /// `output` (a QML VideoOutput) shows the loaded file's picture when `path` is it — the last
    /// one attached wins (the full screen over the embed), the one before takes it back when that
    /// one goes; detachVideo() when it goes — the last of the loaded file's: a video then pauses
    /// (it plays only where it is seen).
    Q_INVOKABLE void attachVideo(const QString &path, QObject *output);
    Q_INVOKABLE void detachVideo(QObject *output);

    /// What the file at `path` is (`url`: Workspace.assetUrl), read in the background on first ask:
    /// {state: "unknown" (this build reads nothing) | "loading" | "ready" | "failed", duration (ms,
    /// -1 unknown), hasVideo, poster (a file URL of its first picture, "" none), width, height,
    /// error}; infoChanged(path) when it is known.
    Q_INVOKABLE QVariantMap info(const QString &path, const QUrl &url);
    /// The file at `path` changed (or went): what was read of it is read again when asked.
    Q_INVOKABLE void forget(const QString &path);

    /// The folder the posters are kept in (default: the app's cache folder; tests).
    void setPosterFolder(const QString &folder) { m_posterFolder = folder; }

Q_SIGNALS:
    void availableChanged();
    void sourceChanged();
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void hasVideoChanged();
    void rateChanged();
    void volumeChanged();
    void errorChanged();
    void viewsChanged();
    void infoChanged(const QString &path);
    /// The loaded file played to its end (or its fragment's).
    void ended(const QString &path);

private:
    void load(const QString &path, const QUrl &url);
    void onPosition();
    void onProbed(const QUrl &url, const MediaInfo &info);
    void remember();
    QString posterPath(const QString &path, const QUrl &url) const;

    /// The build's engine / prober, made when first needed (the constructor's: from the start).
    MediaEngine *engine();
    MediaProber *prober();
    void adopt(std::unique_ptr<MediaEngine> engine);
    void adopt(std::unique_ptr<MediaProber> prober);

    std::unique_ptr<MediaEngine> m_engine; ///< null until engine()
    std::unique_ptr<MediaProber> m_prober; ///< null until prober()
    QString m_path;
    QUrl m_url;
    qint64 m_startMs = -1;
    qint64 m_endMs = -1;
    bool m_atEnd = false; ///< paused at the fragment's end (or the file's)
    double m_rate = 1.0;
    double m_volume = 1.0;
    bool m_muted = false;
    int m_views = 0;
    QHash<QString, int> m_viewCounts;
    /// The surfaces attached, the last on top, each with the file it shows.
    struct Surface {
        QPointer<QObject> output;
        QString path;
    };
    QList<Surface> m_surfaces;
    /// The surface the engine draws into now (the top one of the loaded file's).
    QPointer<QObject> m_videoOutput;
    void updateVideoOutput();
    QHash<QString, qint64> m_positions; ///< where other files were left
    struct Info {
        QString state; ///< "loading", "ready", "failed"
        MediaInfo media;
        QString poster; ///< file URL
        QUrl url;
    };
    QHash<QString, Info> m_info;    ///< by vault path
    QHash<QUrl, QString> m_probing; ///< url → vault path
    int m_infoRevision = 0;
    QString m_posterFolder;
};

} // namespace brulion::media
