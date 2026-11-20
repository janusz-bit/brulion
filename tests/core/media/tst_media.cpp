// The core module media (src/core/media/media.h): which files play and how (by name), the time
// a link or an embed starts at (W3C Media Fragments' `t=`, also as Obsidian plugins and YouTube
// write it) and back, the player's times as text, and the names voice notes get.
#include "media/media.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using media::TimeRange;

class TestMedia : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void kindOf_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<int>("kind");
        QTest::newRow("mp3") << u"assets/Nagranie 2026-11-20 18.30.mp3"_s << int(media::Kind::Audio);
        QTest::newRow("m4a, upper case") << u"Wykład ŻÓŁW.M4A"_s << int(media::Kind::Audio);
        QTest::newRow("ogg") << u"głos.ogg"_s << int(media::Kind::Audio);
        QTest::newRow("mp4") << u"film.mp4"_s << int(media::Kind::Video);
        QTest::newRow("webm") << u"ekran.webm"_s << int(media::Kind::Video);
        QTest::newRow("mkv in a folder") << u"Wakacje/Zakopane ąę.mkv"_s << int(media::Kind::Video);
        QTest::newRow("picture") << u"okładka.png"_s << int(media::Kind::None);
        QTest::newRow("note") << u"nagranie.mp3.md"_s << int(media::Kind::None);
        QTest::newRow("empty") << QString() << int(media::Kind::None);
    }
    void kindOf()
    {
        QFETCH(QString, path);
        QFETCH(int, kind);
        QCOMPARE(int(media::kindOf(path)), kind);
    }

    void parseTimeFragment_data()
    {
        QTest::addColumn<QString>("fragment");
        QTest::addColumn<qint64>("start");
        QTest::addColumn<qint64>("end");
        // W3C Media Fragments URI 1.0, the temporal dimension (normal play time).
        QTest::newRow("seconds") << u"t=83"_s << 83000LL << -1LL;
        QTest::newRow("fraction") << u"t=83.5"_s << 83500LL << -1LL;
        QTest::newRow("milliseconds") << u"t=1.234"_s << 1234LL << -1LL;
        QTest::newRow("only a fraction") << u"t=.5"_s << 500LL << -1LL;
        QTest::newRow("zero") << u"t=0"_s << 0LL << -1LL;
        QTest::newRow("mm:ss") << u"t=01:23"_s << 83000LL << -1LL;
        QTest::newRow("m:ss (lenient)") << u"t=1:23"_s << 83000LL << -1LL;
        QTest::newRow("hh:mm:ss") << u"t=1:02:03"_s << 3723000LL << -1LL;
        QTest::newRow("hh:mm:ss.f") << u"t=00:01:02.5"_s << 62500LL << -1LL;
        QTest::newRow("long minutes") << u"t=90:00"_s << 5400000LL << -1LL;
        QTest::newRow("npt prefix") << u"t=npt:10"_s << 10000LL << -1LL;
        QTest::newRow("NPT, key in upper case") << u"T=NPT:10"_s << 10000LL << -1LL;
        QTest::newRow("start and end") << u"t=10,20"_s << 10000LL << 20000LL;
        QTest::newRow("only an end") << u"t=,20"_s << -1LL << 20000LL;
        QTest::newRow("start, open end") << u"t=10,"_s << 10000LL << -1LL;
        QTest::newRow("times with colons") << u"t=1:00,1:30"_s << 60000LL << 90000LL;
        QTest::newRow("spaces") << u" t = 12 "_s << 12000LL << -1LL;
        QTest::newRow("percent-encoded") << u"t=1%3A23%2C1%3A30"_s << 83000LL << 90000LL;
        // As YouTube and Obsidian's media plugins write it.
        QTest::newRow("1m23s") << u"t=1m23s"_s << 83000LL << -1LL;
        QTest::newRow("1h2m3s") << u"t=1h2m3s"_s << 3723000LL << -1LL;
        QTest::newRow("90s") << u"t=90s"_s << 90000LL << -1LL;
        QTest::newRow("5m") << u"t=5m"_s << 300000LL << -1LL;
        QTest::newRow("2h") << u"t=2H"_s << 7200000LL << -1LL;
        QTest::newRow("fractional seconds unit") << u"t=1.5s"_s << 1500LL << -1LL;
        // Other keys go with it (`&`), the last valid `t` counts.
        QTest::newRow("with other keys") << u"autoplay=1&t=42&loop"_s << 42000LL << -1LL;
        QTest::newRow("last valid wins") << u"t=10&t=20"_s << 20000LL << -1LL;
        QTest::newRow("an invalid one after a valid one") << u"t=10&t=abc"_s << 10000LL << -1LL;
        // Not a time.
        QTest::newRow("empty") << QString() << -1LL << -1LL;
        QTest::newRow("a heading") << u"Notatki"_s << -1LL << -1LL;
        QTest::newRow("a PDF page") << u"page=3"_s << -1LL << -1LL;
        QTest::newRow("no value") << u"t="_s << -1LL << -1LL;
        QTest::newRow("only a comma") << u"t=,"_s << -1LL << -1LL;
        QTest::newRow("letters") << u"t=abc"_s << -1LL << -1LL;
        QTest::newRow("negative") << u"t=-5"_s << -1LL << -1LL;
        QTest::newRow("end before start") << u"t=20,10"_s << -1LL << -1LL;
        QTest::newRow("end equals start") << u"t=10,10"_s << -1LL << -1LL;
        QTest::newRow("60 seconds after a colon") << u"t=1:60"_s << -1LL << -1LL;
        QTest::newRow("60 minutes with hours") << u"t=1:60:00"_s << -1LL << -1LL;
        QTest::newRow("four fields") << u"t=1:2:3:4"_s << -1LL << -1LL;
        QTest::newRow("three values") << u"t=1,2,3"_s << -1LL << -1LL;
        QTest::newRow("SMPTE") << u"t=smpte-30:0:02:00"_s << -1LL << -1LL;
        QTest::newRow("clock time") << u"t=clock:2026-11-20T18:30:00Z"_s << -1LL << -1LL;
        QTest::newRow("more than a day") << u"t=90000"_s << -1LL << -1LL;
        QTest::newRow("units out of order") << u"t=3s2m"_s << -1LL << -1LL;
        QTest::newRow("a unit twice") << u"t=1m2m"_s << -1LL << -1LL;
        QTest::newRow("fractional minutes") << u"t=1.5m"_s << -1LL << -1LL;
        QTest::newRow("Arabic-Indic digits") << u"t=٣"_s << -1LL << -1LL;
        QTest::newRow("full-width digits") << u"t=３"_s << -1LL << -1LL;
        QTest::newRow("a word with t") << u"tt=10"_s << -1LL << -1LL;
    }
    void parseTimeFragment()
    {
        QFETCH(QString, fragment);
        QFETCH(qint64, start);
        QFETCH(qint64, end);
        const TimeRange r = media::parseTimeFragment(fragment);
        QCOMPARE(r.startMs, start);
        QCOMPARE(r.endMs, end);
        QCOMPARE(r.isValid(), start >= 0 || end >= 0);
    }

    void timeFragment_data()
    {
        QTest::addColumn<qint64>("start");
        QTest::addColumn<qint64>("end");
        QTest::addColumn<QString>("fragment");
        QTest::newRow("whole seconds") << 83000LL << -1LL << u"t=83"_s;
        QTest::newRow("tenths") << 83500LL << -1LL << u"t=83.5"_s;
        QTest::newRow("milliseconds") << 1234LL << -1LL << u"t=1.234"_s;
        QTest::newRow("zero") << 0LL << -1LL << u"t=0"_s;
        QTest::newRow("range") << 10000LL << 20000LL << u"t=10,20"_s;
        QTest::newRow("only an end") << -1LL << 20000LL << u"t=,20"_s;
        QTest::newRow("nothing") << -1LL << -1LL << QString();
        QTest::newRow("end before start") << 20000LL << 10000LL << QString();
    }
    void timeFragment()
    {
        QFETCH(qint64, start);
        QFETCH(qint64, end);
        QFETCH(QString, fragment);
        QCOMPARE(media::timeFragment(TimeRange{start, end}), fragment);
        // What it writes reads back as the same time.
        if (!fragment.isEmpty())
            QCOMPARE(media::parseTimeFragment(fragment), (TimeRange{start, end}));
    }

    void formatTime_data()
    {
        QTest::addColumn<qint64>("ms");
        QTest::addColumn<qint64>("total");
        QTest::addColumn<QString>("text");
        QTest::newRow("start") << 0LL << -1LL << u"0:00"_s;
        QTest::newRow("seconds") << 7400LL << -1LL << u"0:07"_s;
        QTest::newRow("minute") << 83000LL << -1LL << u"1:23"_s;
        QTest::newRow("ten minutes") << 725999LL << -1LL << u"12:05"_s;
        QTest::newRow("an hour") << 3723000LL << -1LL << u"1:02:03"_s;
        QTest::newRow("hours from the total") << 83000LL << 3600000LL << u"0:01:23"_s;
        QTest::newRow("just under an hour") << 3599000LL << 3599999LL << u"59:59"_s;
        QTest::newRow("negative") << -500LL << -1LL << u"0:00"_s;
        QTest::newRow("long") << 36000000LL << -1LL << u"10:00:00"_s;
    }
    void formatTime()
    {
        QFETCH(qint64, ms);
        QFETCH(qint64, total);
        QFETCH(QString, text);
        QCOMPARE(media::formatTime(ms, total), text);
    }

    void recordingName_data()
    {
        QTest::addColumn<QString>("prefix");
        QTest::addColumn<QString>("extension");
        QTest::addColumn<QString>("name");
        const QString when = u"2026-11-20 18.30"_s;
        QTest::newRow("Polish") << u"Nagranie"_s << u"m4a"_s << u"Nagranie "_s + when + u".m4a"_s;
        QTest::newRow("English, dot, upper case") << u"Recording"_s << u".OGG"_s << u"Recording "_s + when + u".ogg"_s;
        QTest::newRow("wav") << u"Nagranie"_s << u"wav"_s << u"Nagranie "_s + when + u".wav"_s;
        QTest::newRow("no extension") << u"Nagranie"_s << QString() << u"Nagranie "_s + when;
        QTest::newRow("characters a file cannot hold")
                << u"Notatka: głos/ćwiczenie?"_s << u"m4a"_s << u"Notatka głos ćwiczenie "_s + when + u".m4a"_s;
        QTest::newRow("no prefix") << QString() << u"m4a"_s << when + u".m4a"_s;
    }
    void recordingName()
    {
        QFETCH(QString, prefix);
        QFETCH(QString, extension);
        QFETCH(QString, name);
        const QDateTime when(QDate(2026, 11, 20), QTime(18, 30, 42));
        QCOMPARE(media::recordingName(prefix, when, extension), name);
    }
};

QTEST_GUILESS_MAIN(TestMedia)
#include "tst_media.moc"
