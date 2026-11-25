// Where a recording's transcript goes (src/core/media/transcript.h): the text made a plain
// paragraph (no Markdown out of words heard), placed under the block that embeds the recording —
// a list item's first child, a paragraph's next paragraph — in outlines and documents, with tabs or
// spaces, CRLF, several embeds of the same file, links written another way; every other byte stays.
// With times: segments made sentences (Whisper's cuts mended, a run without punctuation cut), the
// sentences paragraphs (a pause, half a minute, many characters), each paragraph of a longer
// transcript starting with a link to its moment written the way the embed names the recording.
#include "media/transcript.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using media::TimedText;

class TestMediaTranscript : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void text_data()
    {
        QTest::addColumn<QStringList>("segments");
        QTest::addColumn<QString>("text");
        QTest::newRow("segments joined") << QStringList{u" Dzień dobry."_s, u"Kupić mleko. "_s}
                                         << u"Dzień dobry. Kupić mleko."_s;
        QTest::newRow("white space collapsed") << QStringList{u"Ala\n ma\t\tkota"_s} << u"Ala ma kota"_s;
        QTest::newRow("control characters") << QStringList{u"za\u0007żółć\u200Bgęślą"_s} << u"za żółćgęślą"_s;
        QTest::newRow("a heading") << QStringList{u"# Punkt pierwszy"_s} << u"\\# Punkt pierwszy"_s;
        QTest::newRow("a list") << QStringList{u"- jajka"_s} << u"\\- jajka"_s;
        QTest::newRow("a plus") << QStringList{u"+ dodatek"_s} << u"\\+ dodatek"_s;
        QTest::newRow("a star") << QStringList{u"* gwiazdka"_s} << u"\\* gwiazdka"_s;
        QTest::newRow("an ordered list") << QStringList{u"1. Pierwsze"_s} << u"1\\. Pierwsze"_s;
        QTest::newRow("an ordered list with a parenthesis") << QStringList{u"12) Dwanaście"_s} << u"12\\) Dwanaście"_s;
        QTest::newRow("a number is no list") << QStringList{u"2026 rok"_s} << u"2026 rok"_s;
        QTest::newRow("a quote") << QStringList{u"> cytat"_s} << u"\\> cytat"_s;
        QTest::newRow("a table") << QStringList{u"| a |"_s} << u"\\| a |"_s;
        QTest::newRow("a fence") << QStringList{u"```kod"_s} << u"\\```kod"_s;
        QTest::newRow("a formula") << QStringList{u"$$ x $$"_s} << u"\\$$ x $$"_s;
        QTest::newRow("a rule") << QStringList{u"---"_s} << u"\\---"_s;
        QTest::newRow("a link inside") << QStringList{u"Zobacz [[Strona]] i ((abc)) oraz {{data}}"_s}
                                       << u"Zobacz \\[[Strona]] i \\((abc)) oraz \\{{data}}"_s;
        QTest::newRow("a tag inside") << QStringList{u"Ważne #projekt teraz"_s} << u"Ważne \\#projekt teraz"_s;
        QTest::newRow("a tag at the start") << QStringList{u"#zakupy chleb"_s} << u"\\#zakupy chleb"_s;
        QTest::newRow("a number sign before a digit") << QStringList{u"pokój #12"_s} << u"pokój #12"_s;
        QTest::newRow("a backslash") << QStringList{u"C:\\Windows"_s} << u"C:\\\\Windows"_s;
        QTest::newRow("an emoji stays whole")
                << QStringList{u"👨‍👩‍👧 rodzina"_s} << u"👨‍👩‍👧 rodzina"_s;
        QTest::newRow("blank") << QStringList{u"  "_s, u"\n"_s} << QString();
        QTest::newRow("nothing") << QStringList{} << QString();
    }
    void text()
    {
        QFETCH(QStringList, segments);
        QFETCH(QString, text);
        QCOMPARE(media::transcriptText(segments), text);
    }

    void place_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("target");
        QTest::addColumn<int>("near");
        QTest::addColumn<QString>("result");
        QTest::addColumn<int>("line");
        const QString t = u"Kupić mleko i chleb."_s;
        QTest::newRow("an outline item gets a child")
                << u"- Rano\n- ![[Nagranie 2026-11-20 18.30.m4a]]\n- Wieczorem\n"_s
                << u"Nagranie 2026-11-20 18.30.m4a"_s << 1
                << u"- Rano\n- ![[Nagranie 2026-11-20 18.30.m4a]]\n\t- Kupić mleko i chleb.\n- Wieczorem\n"_s << 2;
        QTest::newRow("before the item's other children")
                << u"- ![[a.ogg]]\n  - stare\n"_s << u"a.ogg"_s << 0
                << u"- ![[a.ogg]]\n  - Kupić mleko i chleb.\n  - stare\n"_s << 1;
        QTest::newRow("a nested item, tabs")
                << u"- Dziennik\n\t- Notatka ![[g.mp3]]\n"_s << u"g.mp3"_s << 1
                << u"- Dziennik\n\t- Notatka ![[g.mp3]]\n\t\t- Kupić mleko i chleb.\n"_s << 2;
        QTest::newRow("a task") << u"- [ ] Oddzwonić ![[r.m4a]]\n"_s << u"r.m4a"_s << 0
                                << u"- [ ] Oddzwonić ![[r.m4a]]\n\t- Kupić mleko i chleb.\n"_s << 1;
        QTest::newRow("a document paragraph gets the next paragraph")
                << u"# Spotkanie\n\n![[Nagranie.m4a]]\n\nDalej tekst.\n"_s << u"Nagranie.m4a"_s << 2
                << u"# Spotkanie\n\n![[Nagranie.m4a]]\n\nKupić mleko i chleb.\n\nDalej tekst.\n"_s << 4;
        QTest::newRow("the last paragraph without a final line break")
                << u"Tekst\n\n![[n.wav]]"_s << u"n.wav"_s << 2 << u"Tekst\n\n![[n.wav]]\n\nKupić mleko i chleb."_s << 4;
        QTest::newRow("CRLF") << u"- a\r\n- ![[n.ogg]]\r\n"_s << u"n.ogg"_s << 1
                              << u"- a\r\n- ![[n.ogg]]\r\n\t- Kupić mleko i chleb.\r\n"_s << 2;
        QTest::newRow("a Markdown embed")
                << u"- ![nagranie](assets/Nagranie%201.m4a)\n"_s << u"assets/Nagranie%201.m4a"_s << 0
                << u"- ![nagranie](assets/Nagranie%201.m4a)\n\t- Kupić mleko i chleb.\n"_s << 1;
        QTest::newRow("decoded target matches")
                << u"- ![x](assets/Nagranie%201.m4a)\n"_s << u"assets/Nagranie 1.m4a"_s << 0
                << u"- ![x](assets/Nagranie%201.m4a)\n\t- Kupić mleko i chleb.\n"_s << 1;
        QTest::newRow("by the file's name") << u"- ![[assets/głos.ogg]]\n"_s << u"głos.ogg"_s << 0
                                            << u"- ![[assets/głos.ogg]]\n\t- Kupić mleko i chleb.\n"_s << 1;
        QTest::newRow("a time fragment") << u"- ![[w.mp3#t=83]]\n"_s << u"w.mp3"_s << 0
                                         << u"- ![[w.mp3#t=83]]\n\t- Kupić mleko i chleb.\n"_s << 1;
        QTest::newRow("the nearest of two embeds")
                << u"- ![[n.ogg]]\n- x\n- y\n- ![[n.ogg]]\n"_s << u"n.ogg"_s << 3
                << u"- ![[n.ogg]]\n- x\n- y\n- ![[n.ogg]]\n\t- Kupić mleko i chleb.\n"_s << 4;
        QTest::newRow("the first without a line")
                << u"- ![[n.ogg]]\n- x\n- ![[n.ogg]]\n"_s << u"n.ogg"_s << -1
                << u"- ![[n.ogg]]\n\t- Kupić mleko i chleb.\n- x\n- ![[n.ogg]]\n"_s << 1;
        QTest::newRow("a link is no embed") << u"- [[n.ogg]]\n- ![[n.ogg]]\n"_s << u"n.ogg"_s << 0
                                            << u"- [[n.ogg]]\n- ![[n.ogg]]\n\t- Kupić mleko i chleb.\n"_s << 2;
        QTest::newRow("in code it is no embed") << u"- `![[n.ogg]]`\n"_s << u"n.ogg"_s << 0 << QString() << -1;
        QTest::newRow("after frontmatter") << u"---\ntags: głos\n---\n- ![[n.ogg]]\n"_s << u"n.ogg"_s << 3
                                           << u"---\ntags: głos\n---\n- ![[n.ogg]]\n\t- Kupić mleko i chleb.\n"_s << 4;
        QTest::newRow("not there") << u"- nic\n"_s << u"n.ogg"_s << 0 << QString() << -1;
        QTest::newRow("another file") << u"- ![[m.ogg]]\n"_s << u"n.ogg"_s << 0 << QString() << -1;
        QTest::newRow("empty note") << QString() << u"n.ogg"_s << 0 << QString() << -1;
        Q_UNUSED(t)
    }
    void place()
    {
        QFETCH(QString, content);
        QFETCH(QString, target);
        QFETCH(int, near);
        QFETCH(QString, result);
        QFETCH(int, line);
        const media::TranscriptPlace p =
                media::placeTranscript(content, target, near, media::transcriptText({u"Kupić mleko i chleb."_s}));
        QCOMPARE(p.content, result);
        QCOMPARE(p.line, line);
    }

    void endsSentence_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("ends");
        QTest::newRow("a full stop") << u"Kupić mleko."_s << true;
        QTest::newRow("a question") << u"Czy już? "_s << true;
        QTest::newRow("an exclamation in quotes") << u"Powiedział „Dość!”"_s << true;
        QTest::newRow("an ellipsis") << u"I wtedy…"_s << true;
        QTest::newRow("in brackets") << u"(tak jak wczoraj.)"_s << true;
        QTest::newRow("a comma") << u"Najpierw,"_s << false;
        QTest::newRow("a word") << u"i potem"_s << false;
        QTest::newRow("full-width") << u"はい。"_s << true;
        QTest::newRow("nothing") << QString() << false;
    }
    void endsSentence()
    {
        QFETCH(QString, text);
        QFETCH(bool, ends);
        QCOMPARE(media::endsSentence(text), ends);
    }

    void sentences()
    {
        // Whisper's cut in the middle of a sentence is mended; blank segments go.
        const QList<TimedText> segments{{0, 2000, u" Dzień dobry,"_s},  {2000, 4200, u"dziś omówimy"_s},
                                        {4200, 5000, u" "_s},           {5000, 7000, u"plan na jutro."_s},
                                        {7500, 9000, u"Żółwie też?"_s}, {9000, 9800, u"Tak"_s}};
        QCOMPARE(media::transcriptSentences(segments),
                 (QList<TimedText>{{0, 7000, u"Dzień dobry, dziś omówimy plan na jutro."_s},
                                   {7500, 9000, u"Żółwie też?"_s},
                                   {9000, 9800, u"Tak"_s}}));
        // Speech without punctuation: cut once a run is long.
        QList<TimedText> run;
        for (int i = 0; i < 10; ++i)
            run.append(TimedText{i * 10000, i * 10000 + 10000, u"słowo"_s});
        const QList<TimedText> cut = media::transcriptSentences(run, 30000);
        QCOMPARE(cut.size(), 4);
        QCOMPARE(cut.at(0), (TimedText{0, 30000, u"słowo słowo słowo"_s}));
        QCOMPARE(cut.at(3), (TimedText{90000, 100000, u"słowo"_s}));
        QVERIFY(media::transcriptSentences({}).isEmpty());
    }

    void paragraphs()
    {
        // A pause after a sentence's end starts a paragraph; a pause inside a sentence does not.
        const QList<TimedText> paused{{0, 3000, u"Pierwsza sprawa."_s},
                                      {6000, 8000, u"Druga sprawa,"_s},
                                      {11000, 12000, u"po przerwie."_s},
                                      {12000, 14000, u"I trzecia."_s}};
        QCOMPARE(media::transcriptParagraphs(paused),
                 (QList<TimedText>{{0, 3000, u"Pierwsza sprawa."_s},
                                   {6000, 14000, u"Druga sprawa, po przerwie. I trzecia."_s}}));
        // Half a minute of talk without pauses: a paragraph at the first sentence's end after it.
        QList<TimedText> talk;
        for (int i = 0; i < 12; ++i)
            talk.append(TimedText{i * 7000, i * 7000 + 7000, u"Zdanie %1."_s.arg(i + 1)});
        const QList<TimedText> p = media::transcriptParagraphs(talk);
        QCOMPARE(p.size(), 3);
        QCOMPARE(p.at(0).startMs, 0);
        QCOMPARE(p.at(0).text, u"Zdanie 1. Zdanie 2. Zdanie 3. Zdanie 4. Zdanie 5."_s);
        QCOMPARE(p.at(1).startMs, 35000);
        QCOMPARE(p.at(2).startMs, 70000);
        QCOMPARE(p.at(2).endMs, 84000);
        // Many characters: a paragraph sooner.
        media::ParagraphRules rules;
        rules.lengthChars = 20;
        QCOMPARE(media::transcriptParagraphs(talk, rules).size(), 5);
        // One sentence: one paragraph.
        QCOMPARE(media::transcriptParagraphs({{0, 4000, u"Kupić mleko."_s}}),
                 (QList<TimedText>{{0, 4000, u"Kupić mleko."_s}}));
        QVERIFY(media::transcriptParagraphs({{0, 1000, u"  "_s}}).isEmpty());
    }

    void momentLink_data()
    {
        QTest::addColumn<bool>("wiki");
        QTest::addColumn<QString>("target");
        QTest::addColumn<qint64>("duration");
        QTest::addColumn<qint64>("ms");
        QTest::addColumn<QString>("link");
        QTest::newRow("wiki") << true << u"Nagranie 2026-11-20 18.30.m4a"_s << qint64(120000) << qint64(83400)
                              << u"[[Nagranie 2026-11-20 18.30.m4a#t=83|1:23]]"_s;
        QTest::newRow("the start") << true << u"a.ogg"_s << qint64(60000) << qint64(0) << u"[[a.ogg#t=0|0:00]]"_s;
        QTest::newRow("an hour long") << true << u"wykład.mp3"_s << qint64(4000000) << qint64(83000)
                                      << u"[[wykład.mp3#t=83|0:01:23]]"_s;
        QTest::newRow("Markdown") << false << u"assets/Nagranie 1 (rano).m4a"_s << qint64(-1) << qint64(5999)
                                  << u"[0:05](assets/Nagranie%201%20%28rano%29.m4a#t=5)"_s;
        QTest::newRow("Markdown keeps letters")
                << false << u"głos#1.ogg"_s << qint64(-1) << qint64(61000) << u"[1:01](głos%231.ogg#t=61)"_s;
        QTest::newRow("negative") << true << u"a.ogg"_s << qint64(-1) << qint64(-5) << u"[[a.ogg#t=0|0:00]]"_s;
        QTest::newRow("no target") << true << QString() << qint64(-1) << qint64(1000) << QString();
    }
    void momentLink()
    {
        QFETCH(bool, wiki);
        QFETCH(QString, target);
        QFETCH(qint64, duration);
        QFETCH(qint64, ms);
        QFETCH(QString, link);
        QCOMPARE(media::momentLink(media::MomentLinks{wiki, target, duration}, ms), link);
    }

    void blocks()
    {
        const media::MomentLinks links{true, u"n.m4a"_s, 90000};
        // One paragraph: the text alone, as before.
        QCOMPARE(media::transcriptBlocks({{0, 4000, u"# Kupić mleko"_s}}, links), QStringList{u"\\# Kupić mleko"_s});
        // More: each one's moment first; blank ones go.
        QCOMPARE(media::transcriptBlocks({{0, 31000, u"Pierwsza część."_s},
                                          {31000, 32000, u" "_s},
                                          {32000, 70000, u"Druga #część [[tu]]."_s}},
                                         links),
                 (QStringList{u"[[n.m4a#t=0|0:00]] Pierwsza część."_s,
                              u"[[n.m4a#t=32|0:32]] Druga \\#część \\[[tu]]."_s}));
        QVERIFY(media::transcriptBlocks({}, links).isEmpty());
    }

    void placeTimed_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("result");
        QTest::addColumn<int>("line");
        QTest::newRow("an outline item gets its children")
                << u"- Rano\n- ![[Nagranie 2026-11-20 18.30.m4a]]\n\t- stare\n- Wieczorem\n"_s
                << u"Nagranie 2026-11-20 18.30.m4a"_s
                << u"- Rano\n- ![[Nagranie 2026-11-20 18.30.m4a]]\n"
                   u"\t- [[Nagranie 2026-11-20 18.30.m4a#t=0|0:00]] Dzień dobry.\n"
                   u"\t- [[Nagranie 2026-11-20 18.30.m4a#t=34|0:34]] Kupić mleko i chleb.\n"
                   u"\t- stare\n- Wieczorem\n"_s
                << 2;
        QTest::newRow("a document gets paragraphs")
                << u"# Spotkanie\r\n\r\n![[Nagranie.m4a]]\r\n\r\nDalej tekst.\r\n"_s << u"Nagranie.m4a"_s
                << u"# Spotkanie\r\n\r\n![[Nagranie.m4a]]\r\n\r\n[[Nagranie.m4a#t=0|0:00]] Dzień dobry.\r\n\r\n"
                   u"[[Nagranie.m4a#t=34|0:34]] Kupić mleko i chleb.\r\n\r\nDalej tekst.\r\n"_s
                << 4;
        QTest::newRow("a Markdown embed gets Markdown links")
                << u"- ![nagranie](assets/Nagranie%201.m4a#t=10)\n"_s << u"assets/Nagranie 1.m4a"_s
                << u"- ![nagranie](assets/Nagranie%201.m4a#t=10)\n\t- [0:00](assets/Nagranie%201.m4a#t=0) Dzień "
                   u"dobry.\n"
                   u"\t- [0:34](assets/Nagranie%201.m4a#t=34) Kupić mleko i chleb.\n"_s
                << 1;
        QTest::newRow("a path in the embed")
                << u"- ![[assets/głos.ogg]]\n"_s << u"głos.ogg"_s
                << u"- ![[assets/głos.ogg]]\n\t- [[assets/głos.ogg#t=0|0:00]] Dzień dobry.\n"
                   u"\t- [[assets/głos.ogg#t=34|0:34]] Kupić mleko i chleb.\n"_s
                << 1;
        QTest::newRow("not there") << u"- nic\n"_s << u"n.ogg"_s << QString() << -1;
    }
    void placeTimed()
    {
        QFETCH(QString, content);
        QFETCH(QString, target);
        QFETCH(QString, result);
        QFETCH(int, line);
        const QList<TimedText> paragraphs{{0, 30000, u"Dzień dobry."_s}, {34200, 61000, u"Kupić mleko i chleb."_s}};
        const media::TranscriptPlace p = media::placeTranscript(content, target, -1, paragraphs, 61000);
        QCOMPARE(p.content, result);
        QCOMPARE(p.line, line);
    }

    void placeTimedShort()
    {
        // One paragraph: the transcript as it has always been (no link: the player is right above).
        const media::TranscriptPlace p =
                media::placeTranscript(u"- ![[n.ogg]]\n"_s, u"n.ogg"_s, 0, {{0, 5000, u"Kupić mleko."_s}}, 5000);
        QCOMPARE(p.content, u"- ![[n.ogg]]\n\t- Kupić mleko.\n"_s);
        QCOMPARE(p.line, 1);
        // Nothing heard: not placed.
        QCOMPARE(media::placeTranscript(u"- ![[n.ogg]]\n"_s, u"n.ogg"_s, 0, QList<TimedText>{}, 5000).line, -1);
        QCOMPARE(media::placeTranscript(u"- ![[n.ogg]]\n"_s, u""_s, 0, {{0, 5000, u"x"_s}}, 5000).line, -1);
    }

    void blank()
    {
        QCOMPARE(media::placeTranscript(u"- ![[n.ogg]]\n"_s, u"n.ogg"_s, 0, u"  "_s).line, -1);
        QCOMPARE(media::placeTranscript(u"- ![[n.ogg]]\n"_s, u""_s, 0, u"x"_s).line, -1);
    }
};

QTEST_GUILESS_MAIN(TestMediaTranscript)
#include "tst_media_transcript.moc"
