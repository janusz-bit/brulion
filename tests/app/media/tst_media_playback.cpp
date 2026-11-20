// MediaPlayback (src/app/media/mediaplayback.h), the app's one player, over a fake engine and
// prober: one file plays at a time and the one before keeps its place, a time fragment starts the
// file there and pauses it at its end, the surfaces of a video (the last one attached wins, the one
// before takes the picture back, the last one going pauses a video but never a sound), the views
// count the now-playing bar reads, what a file is (info()) and its poster kept as PNG, speed and
// volume, and the link to a moment.

#include "mediaplayback.h"

#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using media::MediaPlayback;

namespace {

/// An engine that plays nothing but remembers what it was told; the test moves its clock.
class FakeEngine final : public media::MediaEngine {
public:
    QUrl source;
    qint64 pos = 0;
    qint64 dur = 60000;
    bool isPlaying = false;
    bool video = false;
    double rateSet = 1;
    double volumeSet = 1;
    bool mutedSet = false;
    QObject *output = nullptr;
    int setOutputCalls = 0;

    bool available() const override { return true; }
    void setSource(const QUrl &url) override
    {
        source = url;
        pos = 0;
        isPlaying = false;
        Q_EMIT stateChanged();
    }
    void play() override
    {
        isPlaying = true;
        Q_EMIT stateChanged();
    }
    void pause() override
    {
        isPlaying = false;
        Q_EMIT stateChanged();
    }
    void stop() override
    {
        isPlaying = false;
        pos = 0;
        Q_EMIT stateChanged();
    }
    void setPosition(qint64 ms) override
    {
        pos = ms;
        Q_EMIT positionChanged();
    }
    qint64 position() const override { return pos; }
    qint64 duration() const override { return source.isEmpty() ? -1 : dur; }
    bool playing() const override { return isPlaying; }
    bool loading() const override { return false; }
    bool hasVideo() const override { return video; }
    void setRate(double r) override { rateSet = r; }
    void setVolume(double v) override { volumeSet = v; }
    void setMuted(bool m) override { mutedSet = m; }
    void setVideoOutput(QObject *o) override
    {
        output = o;
        ++setOutputCalls;
    }
    QString error() const override { return {}; }

    /// The clock moves on to `ms`.
    void advance(qint64 ms)
    {
        pos = ms;
        Q_EMIT positionChanged();
    }
    void finish()
    {
        pos = dur;
        isPlaying = false;
        Q_EMIT stateChanged();
        Q_EMIT ended();
    }
};

class FakeProber final : public media::MediaProber {
public:
    QList<QUrl> asked;
    void probe(const QUrl &url) override { asked.append(url); }
    void answer(const QUrl &url, const media::MediaInfo &info) { Q_EMIT probed(url, info); }
};

QUrl fileUrl(const QString &name)
{
    return QUrl::fromLocalFile(u"/notatki/assets/"_s + name);
}

} // namespace

class TestMediaPlayback : public QObject {
    Q_OBJECT

private:
    FakeEngine *m_engine = nullptr;
    FakeProber *m_prober = nullptr;
    std::unique_ptr<MediaPlayback> m_player;

    void make()
    {
        auto engine = std::make_unique<FakeEngine>();
        auto prober = std::make_unique<FakeProber>();
        m_engine = engine.get();
        m_prober = prober.get();
        m_player = std::make_unique<MediaPlayback>(std::move(engine), std::move(prober));
        m_player->setPosterFolder(QString());
    }

private Q_SLOTS:
    void init() { make(); }
    void cleanup() { m_player.reset(); }

    void oneAtATime()
    {
        const QString a = u"assets/Wykład żółwia.mp3"_s;
        const QString b = u"assets/Nagranie 2026-11-20 18.30.m4a"_s;
        QSignalSpy source(m_player.get(), &MediaPlayback::sourceChanged);
        QVERIFY(m_player->available());
        QCOMPARE(m_player->path(), QString());
        QCOMPARE(m_player->duration(), -1.0);

        m_player->play(a, fileUrl(u"Wykład żółwia.mp3"_s));
        QCOMPARE(m_player->path(), a);
        QCOMPARE(m_player->title(), u"Wykład żółwia.mp3"_s);
        QVERIFY(m_player->playing());
        QCOMPARE(m_engine->source, fileUrl(u"Wykład żółwia.mp3"_s));
        QVERIFY(!source.isEmpty());
        m_engine->advance(12000);
        QCOMPARE(m_player->position(), 12000.0);

        // Another file: the first one pauses and keeps its place.
        m_player->play(b, fileUrl(u"Nagranie 2026-11-20 18.30.m4a"_s));
        QCOMPARE(m_player->path(), b);
        QVERIFY(m_player->playing());
        QCOMPARE(m_engine->position(), 0);
        QCOMPARE(m_player->positionOf(a), 12000.0);
        m_engine->advance(3000);
        m_player->pause();
        QVERIFY(!m_player->playing());
        QCOMPARE(m_player->positionOf(b), 3000.0);

        // Back to the first: where it was.
        m_player->play(a, fileUrl(u"Wykład żółwia.mp3"_s));
        QCOMPARE(m_engine->position(), 12000);
        QCOMPARE(m_player->positionOf(b), 3000.0);

        // toggle(): pause the one playing, play another.
        m_player->toggle(a, fileUrl(u"Wykład żółwia.mp3"_s));
        QVERIFY(!m_player->playing());
        m_player->toggle(a, fileUrl(u"Wykład żółwia.mp3"_s));
        QVERIFY(m_player->playing());

        // Played to the end: from the start next time, and the place is forgotten.
        QSignalSpy ended(m_player.get(), &MediaPlayback::ended);
        m_engine->finish();
        QCOMPARE(ended.size(), 1);
        QCOMPARE(m_player->positionOf(a), 60000.0); // (loaded: its position)
        m_player->play(a, fileUrl(u"Wykład żółwia.mp3"_s));
        QCOMPARE(m_engine->position(), 0);
    }

    void stopForgetsTheSource()
    {
        m_player->play(u"a.ogg"_s, fileUrl(u"a.ogg"_s));
        m_engine->advance(5000);
        m_player->stop();
        QCOMPARE(m_player->path(), QString());
        QVERIFY(!m_player->playing());
        QCOMPARE(m_engine->source, QUrl());
        QCOMPARE(m_player->positionOf(u"a.ogg"_s), 5000.0);
        m_player->stop(); // (nothing loaded: nothing happens)
    }

    void fragments()
    {
        // `#t=10,20`: starts at 10 s, pauses at 20 s; again: from 10 s.
        m_player->play(u"w.mp3"_s, fileUrl(u"w.mp3"_s), 10000, 20000);
        QCOMPARE(m_engine->position(), 10000);
        QCOMPARE(m_player->endPosition(), 20000.0);
        m_engine->advance(15000);
        QVERIFY(m_player->playing());
        QSignalSpy ended(m_player.get(), &MediaPlayback::ended);
        m_engine->advance(20040);
        QVERIFY(!m_player->playing());
        QCOMPARE(m_engine->position(), 20000);
        QCOMPARE(ended.size(), 1);
        m_player->play(u"w.mp3"_s, fileUrl(u"w.mp3"_s), 10000, 20000);
        QCOMPARE(m_engine->position(), 10000);
        QVERIFY(m_player->playing());
        // A seek past the end lets it play on.
        m_player->seek(30000);
        QCOMPARE(m_player->endPosition(), -1.0);
        m_engine->advance(31000);
        QVERIFY(m_player->playing());
        // Another embed of the same file at another moment: there.
        m_player->play(u"w.mp3"_s, fileUrl(u"w.mp3"_s), 5000, -1);
        QCOMPARE(m_engine->position(), 5000);
        // An end not after the start is none.
        m_player->play(u"x.mp3"_s, fileUrl(u"x.mp3"_s), 8000, 8000);
        QCOMPARE(m_player->endPosition(), -1.0);
    }

    void seekAndSkip()
    {
        m_player->seek(1000); // (nothing loaded)
        m_player->play(u"a.ogg"_s, fileUrl(u"a.ogg"_s));
        m_player->seek(-500);
        QCOMPARE(m_engine->position(), 0);
        m_player->seek(90000);
        QCOMPARE(m_engine->position(), 60000);
        m_player->seek(20000);
        m_player->skip(10000);
        QCOMPARE(m_engine->position(), 30000);
        m_player->skip(-40000);
        QCOMPARE(m_engine->position(), 0);
    }

    void speedAndVolume()
    {
        QCOMPARE(m_player->rate(), 1.0);
        m_player->cycleRate();
        QCOMPARE(m_player->rate(), 1.25);
        m_player->setRate(2.0);
        m_player->cycleRate();
        QCOMPARE(m_player->rate(), 0.5);
        QCOMPARE(m_engine->rateSet, 0.5);
        m_player->setRate(100);
        QCOMPARE(m_player->rate(), 4.0);
        QCOMPARE(m_player->rates().size(), 7);
        QVERIFY(m_player->rateText(1.5).endsWith(u"×"_s));
        QVERIFY(m_player->rateText(1.5).startsWith(u"1"_s));

        // A file loaded later gets them too.
        m_player->setRate(1.5);
        m_player->setVolume(0.3);
        m_player->setMuted(true);
        m_player->play(u"a.ogg"_s, fileUrl(u"a.ogg"_s));
        QCOMPARE(m_engine->rateSet, 1.5);
        QCOMPARE(m_engine->volumeSet, 0.3);
        QVERIFY(m_engine->mutedSet);
        // Turned up from silence: heard.
        m_player->setVolume(0.6);
        QVERIFY(!m_player->muted());
        QVERIFY(!m_engine->mutedSet);
        m_player->setVolume(-2);
        QCOMPARE(m_player->volume(), 0.0);
    }

    void videoSurfaces()
    {
        QObject embed;
        QObject fullscreen;
        m_engine->video = true;
        // The embed's surface, attached before the file plays: it gets the picture once loaded.
        m_player->attachVideo(u"film.mp4"_s, &embed);
        QCOMPARE(m_engine->output, nullptr);
        m_player->play(u"film.mp4"_s, fileUrl(u"film.mp4"_s));
        QCOMPARE(m_engine->output, &embed);
        // Full screen over it: the picture moves there…
        m_player->attachVideo(u"film.mp4"_s, &fullscreen);
        QCOMPARE(m_engine->output, &fullscreen);
        // …and back when it closes, still playing.
        m_player->detachVideo(&fullscreen);
        QCOMPARE(m_engine->output, &embed);
        QVERIFY(m_player->playing());
        // The last surface goes (the block scrolled away): a video pauses.
        m_player->detachVideo(&embed);
        QCOMPARE(m_engine->output, nullptr);
        QVERIFY(!m_player->playing());
        QCOMPARE(m_player->path(), u"film.mp4"_s);
        // A surface of another file does not take this one's picture.
        QObject other;
        m_player->attachVideo(u"inny.mp4"_s, &other);
        QCOMPARE(m_engine->output, nullptr);
        // Detaching what is not attached changes nothing.
        QObject stranger;
        m_player->detachVideo(&stranger);
        m_player->detachVideo(nullptr);
        m_player->attachVideo(u"film.mp4"_s, nullptr);
    }

    void soundPlaysOnUnseen()
    {
        QObject surface;
        m_engine->video = false;
        m_player->attachVideo(u"a.ogg"_s, &surface);
        m_player->play(u"a.ogg"_s, fileUrl(u"a.ogg"_s));
        m_player->detachVideo(&surface);
        QVERIFY(m_player->playing());
    }

    void views()
    {
        QSignalSpy views(m_player.get(), &MediaPlayback::viewsChanged);
        m_player->addView(u"a.ogg"_s);
        m_player->addView(u"a.ogg"_s);
        m_player->addView(u"b.ogg"_s);
        QCOMPARE(m_player->views(), 0); // (nothing loaded)
        m_player->play(u"a.ogg"_s, fileUrl(u"a.ogg"_s));
        QCOMPARE(m_player->views(), 2);
        m_player->removeView(u"a.ogg"_s);
        QCOMPARE(m_player->views(), 1);
        m_player->removeView(u"a.ogg"_s);
        QCOMPARE(m_player->views(), 0);
        m_player->removeView(u"a.ogg"_s); // (one too many: nothing)
        QCOMPARE(m_player->views(), 0);
        m_player->play(u"b.ogg"_s, fileUrl(u"b.ogg"_s));
        QCOMPARE(m_player->views(), 1);
        m_player->addView(QString());
        QVERIFY(!views.isEmpty());
        m_player->stop();
        QCOMPARE(m_player->views(), 0);
    }

    void info()
    {
        QTemporaryDir posters;
        m_player->setPosterFolder(posters.path());
        QTemporaryDir files;
        const QString local = files.filePath(u"Wycieczka.mp4"_s);
        QFile f(local);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("fake");
        f.close();
        const QUrl url = QUrl::fromLocalFile(local);

        QVariantMap i = m_player->info(QString(), url);
        QCOMPARE(i.value(u"state"_s).toString(), u"unknown"_s);
        i = m_player->info(u"assets/Wycieczka.mp4"_s, QUrl());
        QCOMPARE(i.value(u"state"_s).toString(), u"unknown"_s);

        QSignalSpy changed(m_player.get(), &MediaPlayback::infoChanged);
        i = m_player->info(u"assets/Wycieczka.mp4"_s, url);
        QCOMPARE(i.value(u"state"_s).toString(), u"loading"_s);
        QCOMPARE(m_prober->asked, QList<QUrl>{url});
        // Asked again while it is read: once.
        m_player->info(u"assets/Wycieczka.mp4"_s, url);
        QCOMPARE(m_prober->asked.size(), 1);

        media::MediaInfo media;
        media.ok = true;
        media.durationMs = 6000;
        media.hasVideo = true;
        media.videoSize = QSize(640, 360);
        media.poster = QImage(64, 36, QImage::Format_RGB32);
        media.poster.fill(Qt::red);
        m_prober->answer(url, media);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().at(0).toString(), u"assets/Wycieczka.mp4"_s);
        i = m_player->info(u"assets/Wycieczka.mp4"_s, url);
        QCOMPARE(i.value(u"state"_s).toString(), u"ready"_s);
        QCOMPARE(i.value(u"duration"_s).toDouble(), 6000.0);
        QVERIFY(i.value(u"hasVideo"_s).toBool());
        QCOMPARE(i.value(u"width"_s).toInt(), 640);
        const QUrl poster(i.value(u"poster"_s).toString());
        QVERIFY(poster.isLocalFile());
        QCOMPARE(QImage(poster.toLocalFile()).size(), QSize(64, 36));

        // The duration shows before the file plays (the player knows it from info()).
        m_engine->dur = -1;
        m_player->play(u"assets/Wycieczka.mp4"_s, url);
        QCOMPARE(m_player->duration(), 6000.0);

        // Another run: the poster kept on disk is there at once.
        make();
        m_player->setPosterFolder(posters.path());
        i = m_player->info(u"assets/Wycieczka.mp4"_s, url);
        QCOMPARE(i.value(u"state"_s).toString(), u"loading"_s);
        QVERIFY(QUrl(i.value(u"poster"_s).toString()).isLocalFile());

        // A file that cannot be read.
        media::MediaInfo broken;
        broken.error = u"not a file this device plays"_s;
        m_prober->answer(url, broken);
        i = m_player->info(u"assets/Wycieczka.mp4"_s, url);
        QCOMPARE(i.value(u"state"_s).toString(), u"failed"_s);
        QCOMPARE(i.value(u"error"_s).toString(), broken.error);

        // forget(): read again when asked.
        m_player->forget(u"assets/Wycieczka.mp4"_s);
        QCOMPARE(m_player->info(u"assets/Wycieczka.mp4"_s, url).value(u"state"_s).toString(), u"loading"_s);
        // An answer nobody asked for is dropped.
        m_prober->answer(QUrl::fromLocalFile(u"/nic.mp3"_s), media);
    }

    void texts()
    {
        QCOMPARE(m_player->timeText(83000, -1), u"1:23"_s);
        QCOMPARE(m_player->timeText(83000, 3600000), u"0:01:23"_s);
        QCOMPARE(m_player->momentLink(u"Wykład żółwia.mp3"_s, 83700), u"[[Wykład żółwia.mp3#t=83]]"_s);
        QCOMPARE(m_player->momentLink(u"a.ogg"_s, 400), u"[[a.ogg]]"_s);
    }
};

QTEST_GUILESS_MAIN(TestMediaPlayback)
#include "tst_media_playback.moc"
