// The core module speech (src/core/speech/speech.h): the Whisper models the app downloads — each
// pinned to one revision with its checksum and size, all multilingual (they hear Polish), MIT —,
// the languages it is asked to hear, and what is kept of the segments a recognizer heard.
#include "speech/speech.h"

#include <QRegularExpression>
#include <QSet>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using speech::Segment;

class TestSpeech : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void models()
    {
        const QList<ai::LocalModel> &models = speech::speechModels();
        QVERIFY(models.size() >= 3);
        QSet<QString> ids;
        QSet<QString> files;
        for (const ai::LocalModel &m : models) {
            QVERIFY2(!ids.contains(m.id), qPrintable(m.id));
            ids.insert(m.id);
            QVERIFY2(!files.contains(m.file.name), qPrintable(m.file.name));
            files.insert(m.file.name);
            QCOMPARE(m.format, u"whisper"_s);
            QVERIFY(!m.name.isEmpty() && !m.label.isEmpty() && !m.maker.isEmpty());
            QCOMPARE(m.license, u"MIT"_s);
            QVERIFY(m.licenseUrl.isValid() && m.pageUrl.isValid());
            // Only the model's own file; English-only models (".en") would not hear Polish.
            QVERIFY(!m.adapted() && !m.sees());
            QVERIFY2(!m.file.name.contains(u".en"_s), qPrintable(m.file.name));
            QVERIFY2(m.file.name.startsWith(u"ggml-"_s) && m.file.name.endsWith(u".bin"_s), qPrintable(m.file.name));
            QVERIFY2(QRegularExpression(u"^[0-9a-f]{64}$"_s).match(QString::fromLatin1(m.file.sha256)).hasMatch(),
                     qPrintable(m.file.name));
            QVERIFY(m.file.bytes > 10'000'000 && m.file.bytes < 2'000'000'000);
            // One revision of the repository: the file behind the address never changes.
            QVERIFY2(QRegularExpression(u"^https://huggingface\\.co/[^/]+/[^/]+/resolve/[0-9a-f]{40}/"_s)
                             .match(m.file.url.toString())
                             .hasMatch(),
                     qPrintable(m.file.url.toString()));
            QVERIFY(m.file.url.toString().endsWith(u'/' + m.file.name));
            QCOMPARE(speech::speechModel(m.id), &m);
        }
        QCOMPARE(speech::speechModel(QString()), &models.first());
        QCOMPARE(speech::speechModel(u"whisper-small"_s)->id, models.first().id);
        QVERIFY(!speech::speechModel(u"lfm3-1.7b"_s));
        QVERIFY(!speech::speechModel(u"whisper-medium.en"_s));
    }

    void languages()
    {
        QCOMPARE(speech::speechLanguages(), (QStringList{u"auto"_s, u"pl"_s, u"en"_s}));
        QCOMPARE(speech::knownLanguage(u"pl"_s), u"pl"_s);
        QCOMPARE(speech::knownLanguage(u"en"_s), u"en"_s);
        QCOMPARE(speech::knownLanguage(u"auto"_s), u"auto"_s);
        QCOMPARE(speech::knownLanguage(QString()), u"auto"_s);
        QCOMPARE(speech::knownLanguage(u"PL"_s), u"auto"_s);
        QCOMPARE(speech::knownLanguage(u"de"_s), u"auto"_s);
    }

    void spokenTexts_data()
    {
        QTest::addColumn<QStringList>("heard");
        QTest::addColumn<QStringList>("kept");
        QTest::newRow("speech") << QStringList{u" Dzień dobry."_s, u"Kupić mleko i chleb. "_s}
                                << QStringList{u"Dzień dobry."_s, u"Kupić mleko i chleb."_s};
        QTest::newRow("blank audio") << QStringList{u"[BLANK_AUDIO]"_s} << QStringList{};
        QTest::newRow("notes in brackets")
                << QStringList{u"[Muzyka]"_s, u"(śmiech)"_s, u"*kaszel*"_s, u"\u266A\u266A"_s, u"..."_s, u"Tak."_s}
                << QStringList{u"Tak."_s};
        QTest::newRow("subtitle credits")
                << QStringList{u"Napisy wykonane przez społeczność Amara.org"_s,
                               u"Subtitles by the Amara.org community"_s, u"Tłumaczenie: Jan Kowalski, przez Zespół"_s}
                << QStringList{};
        QTest::newRow("a bracket inside speech stays")
                << QStringList{u"Spotkanie (w czwartek) o 10."_s} << QStringList{u"Spotkanie (w czwartek) o 10."_s};
        QTest::newRow("napisy as a word")
                << QStringList{u"Napisy na tablicy są stare."_s} << QStringList{u"Napisy na tablicy są stare."_s};
        QTest::newRow("empty") << QStringList{u""_s, u"   "_s} << QStringList{};
    }
    void spokenTexts()
    {
        QFETCH(QStringList, heard);
        QFETCH(QStringList, kept);
        QList<Segment> segments;
        qint64 at = 0;
        for (const QString &text : heard) {
            segments.append(Segment{at, at + 1000, text});
            at += 1000;
        }
        QCOMPARE(speech::spokenTexts(segments), kept);
    }
};

QTEST_GUILESS_MAIN(TestSpeech)
#include "tst_speech.moc"
