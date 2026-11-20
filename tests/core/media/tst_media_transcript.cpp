// Where a recording's transcript goes (src/core/media/transcript.h): the text made a plain
// paragraph (no Markdown out of words heard), placed under the block that embeds the recording —
// a list item's first child, a paragraph's next paragraph — in outlines and documents, with tabs or
// spaces, CRLF, several embeds of the same file, links written another way; every other byte stays.
#include "media/transcript.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;

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

    void blank()
    {
        QCOMPARE(media::placeTranscript(u"- ![[n.ogg]]\n"_s, u"n.ogg"_s, 0, u"  "_s).line, -1);
        QCOMPARE(media::placeTranscript(u"- ![[n.ogg]]\n"_s, u""_s, 0, u"x"_s).line, -1);
    }
};

QTEST_GUILESS_MAIN(TestMediaTranscript)
#include "tst_media_transcript.moc"
