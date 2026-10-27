// Shares from other apps (notebook/share.h, "Udostępnij → Brulion"): the platform's JSON read
// defensively, one capture text from a title, a text and a link (the previous app's share target
// cases), which files are accepted (text types only, size cap, NUL bytes), decoding (UTF-8 with and
// without a BOM, UTF-16, windows-1250/1252, line endings kept), a new page's title and content,
// and the journal block of a shared file.
#include "notebook/share.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;

Q_DECLARE_METATYPE(brulion::notebook::SharedFileProblem)

class TestNotebookShare : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void composeSharedText_data()
    {
        QTest::addColumn<QString>("subject");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("expected");
        // The previous app's cases (archive/web-v1 share-target.test.ts).
        QTest::newRow("title and url") << u"Przepis na żurek"_s << QString() << u"https://kuchnia.pl/zurek"_s
                                       << u"[Przepis na żurek](https://kuchnia.pl/zurek)"_s;
        QTest::newRow("url in the text") << u"Wideo"_s << u"Zobacz to https://yt.be/abc"_s << QString()
                                         << u"[Wideo](https://yt.be/abc) — Zobacz to"_s;
        QTest::newRow("bare url") << QString() << u"https://a.pl"_s << QString() << u"<https://a.pl>"_s;
        QTest::newRow("title equals text") << u"Notatka"_s << u"Notatka"_s << QString() << u"Notatka"_s;
        QTest::newRow("brackets in the title") << u"Tytuł [x]"_s << u"tekst https://b.pl"_s << u"https://b.pl"_s
                                               << u"[Tytuł x](https://b.pl) — tekst"_s;
        QTest::newRow("plain text") << QString() << u"Sam tekst"_s << QString() << u"Sam tekst"_s;
        // More.
        QTest::newRow("empty") << QString() << QString() << QString() << QString();
        QTest::newRow("blanks only") << u"  \t"_s << u"\n \n"_s << u" "_s << QString();
        QTest::newRow("lines kept") << QString() << u"Pierwsza linia\nDruga — ąę\n\nCzwarta"_s << QString()
                                    << u"Pierwsza linia\nDruga — ąę\n\nCzwarta"_s;
        QTest::newRow("polish letters in the url")
                << u"Łódź – Wikipedia"_s << u"https://pl.wikipedia.org/wiki/Łódź"_s << QString()
                << u"[Łódź – Wikipedia](https://pl.wikipedia.org/wiki/Łódź)"_s;
        QTest::newRow("parentheses in the url")
                << u"Kraków"_s << u"https://pl.wikipedia.org/wiki/Kraków_(ujednoznacznienie)"_s << QString()
                << u"[Kraków](https://pl.wikipedia.org/wiki/Kraków_%28ujednoznacznienie%29)"_s;
        QTest::newRow("link in parentheses") << QString() << u"Ciekawy artykuł (https://a.pl/x)"_s << QString()
                                             << u"<https://a.pl/x> — Ciekawy artykuł"_s;
        QTest::newRow("link in parentheses, then a dot")
                << u"Źródło"_s << u"Ciekawy artykuł (https://a.pl/x). Dalej"_s << QString()
                << u"[Źródło](https://a.pl/x) — Ciekawy artykuł. Dalej"_s;
        QTest::newRow("link ends a parenthesis") << QString() << u"Żółw (zob. https://a.pl/żółw)"_s << QString()
                                                 << u"<https://a.pl/żółw> — Żółw (zob.)"_s;
        QTest::newRow("parenthesised link with parentheses")
                << u"Kraków"_s << u"Hasło (https://pl.wikipedia.org/wiki/Kraków_(ujednoznacznienie))"_s << QString()
                << u"[Kraków](https://pl.wikipedia.org/wiki/Kraków_%28ujednoznacznienie%29) — Hasło"_s;
        QTest::newRow("link in square brackets")
                << QString() << u"Spis [https://a.pl/spis]"_s << QString() << u"<https://a.pl/spis> — Spis"_s;
        QTest::newRow("link in angle brackets")
                << u"Strona"_s << u"<https://a.pl/x>"_s << QString() << u"[Strona](https://a.pl/x)"_s;
        QTest::newRow("subject equals the url")
                << u"https://a.pl/x"_s << u"https://a.pl/x"_s << QString() << u"<https://a.pl/x>"_s;
        QTest::newRow("http") << QString() << u"Strona http://stara.pl/ do zobaczenia"_s << QString()
                              << u"<http://stara.pl/> — Strona do zobaczenia"_s;
        QTest::newRow("ftp is text") << u"Pliki"_s << u"ftp://serwer.pl/plik"_s << QString()
                                     << u"Pliki — ftp://serwer.pl/plik"_s;
        QTest::newRow("sentence dot after the link")
                << QString() << u"Przeczytaj https://a.pl/ś."_s << QString() << u"<https://a.pl/ś> — Przeczytaj"_s;
        QTest::newRow("title inside the text")
                << u"Zakupy"_s << u"Zakupy: mleko, chleb"_s << QString() << u"Zakupy: mleko, chleb"_s;
        QTest::newRow("title whitespace collapsed")
                << u"  Dwa\n  wiersze  "_s << QString() << u"https://c.pl"_s << u"[Dwa wiersze](https://c.pl)"_s;
        QTest::newRow("title only") << u"Zażółć gęślą jaźń"_s << QString() << QString() << u"Zażółć gęślą jaźń"_s;
        QTest::newRow("only brackets as title") << u"[]"_s << QString() << u"https://d.pl"_s << u"<https://d.pl>"_s;
        QTest::newRow("angle bracket in url")
                << QString() << QString() << u"https://e.pl/a>b"_s << u"<https://e.pl/a%3Eb>"_s;
    }
    void composeSharedText()
    {
        QFETCH(QString, subject);
        QFETCH(QString, text);
        QFETCH(QString, url);
        QFETCH(QString, expected);
        QCOMPARE(notebook::composeSharedText(subject, text, url), expected);
    }

    void checkSharedFile_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("mime");
        QTest::addColumn<qint64>("size");
        QTest::addColumn<SharedFileProblem>("expected");
        QTest::newRow(".md") << u"Lista zakupów.md"_s << QString() << qint64(120) << SharedFileProblem::None;
        QTest::newRow(".MD") << u"NOTATKA.MD"_s << u"application/octet-stream"_s << qint64(1)
                             << SharedFileProblem::None;
        QTest::newRow(".markdown") << u"a.markdown"_s << QString() << qint64(-1) << SharedFileProblem::None;
        QTest::newRow(".txt") << u"żółw.txt"_s << u"text/plain"_s << qint64(0) << SharedFileProblem::None;
        QTest::newRow("text/plain without a name")
                << QString() << u"text/plain"_s << qint64(10) << SharedFileProblem::None;
        QTest::newRow("text/markdown") << u"bez rozszerzenia"_s << u"Text/Markdown"_s << qint64(10)
                                       << SharedFileProblem::None;
        QTest::newRow("image") << u"zdjęcie.png"_s << u"image/png"_s << qint64(10) << SharedFileProblem::NotText;
        QTest::newRow("nothing known") << QString() << QString() << qint64(10) << SharedFileProblem::NotText;
        QTest::newRow("pdf") << u"umowa.pdf"_s << u"application/pdf"_s << qint64(10) << SharedFileProblem::NotText;
        QTest::newRow("at the cap") << u"duży.md"_s << QString() << SharedFileMaxBytes << SharedFileProblem::None;
        QTest::newRow("over the cap") << u"duży.md"_s << QString() << SharedFileMaxBytes + 1
                                      << SharedFileProblem::TooLarge;
        QTest::newRow("not text and large")
                << u"film.mp4"_s << u"video/mp4"_s << SharedFileMaxBytes * 40 << SharedFileProblem::NotText;
    }
    void checkSharedFile()
    {
        QFETCH(QString, name);
        QFETCH(QString, mime);
        QFETCH(qint64, size);
        QFETCH(SharedFileProblem, expected);
        QCOMPARE(notebook::checkSharedFile(name, mime, size), expected);
    }

    void errorsAndBinary()
    {
        QCOMPARE(sharedFileError(SharedFileProblem::None), QString());
        QCOMPARE(sharedFileError(SharedFileProblem::NotText), u"notText"_s);
        QCOMPARE(sharedFileError(SharedFileProblem::TooLarge), u"tooLarge"_s);
        QCOMPARE(sharedFileError(SharedFileProblem::Binary), u"binary"_s);
        QVERIFY(!looksBinary(QByteArray()));
        QVERIFY(!looksBinary("Zwykły tekst\r\n"));
        QVERIFY(looksBinary(QByteArray("PNG\0\0\x01", 6)));
        QVERIFY(!looksBinary(QByteArray("\xFF\xFEZ\0a\0", 6)));
        QVERIFY(!looksBinary(QByteArray("\xFE\xFF\0Z\0a", 6)));
    }

    void decodeSharedBytes_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("expected");
        QTest::newRow("utf-8") << u"Zażółć gęślą jaźń\n"_s.toUtf8() << u"Zażółć gęślą jaźń\n"_s;
        QTest::newRow("utf-8 bom") << QByteArray("\xEF\xBB\xBF") + u"ąę"_s.toUtf8() << u"ąę"_s;
        QTest::newRow("utf-16 le") << QByteArray("\xFF\xFE\x05\x01\x0A\x00", 6) << u"ą\n"_s;
        QTest::newRow("utf-16 be") << QByteArray("\xFE\xFF\x01\x42\x00\x0A", 6) << u"ł\n"_s;
        QTest::newRow("windows-1250") << QByteArray("Za\xBF\xF3\xB3\xE6 \xB9\x9C\xBF\x9F") << u"Zażółć ąśżź"_s;
        QTest::newRow("windows-1252") << QByteArray("caf\xE9 \x80") << u"café €"_s;
        QTest::newRow("crlf kept") << QByteArray("a\r\nb\r\n") << u"a\r\nb\r\n"_s;
        QTest::newRow("invalid utf-8 -> legacy") << QByteArray("\xC3(\xB9\xB3") << u"Ă(ął"_s;
        QTest::newRow("empty") << QByteArray() << QString();
    }
    void decodeSharedBytes()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, expected);
        QCOMPARE(notebook::decodeSharedBytes(bytes), expected);
    }

    void sharedPageTitle_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("file name") << u"Lista zakupów.md"_s << u"# Inny tytuł"_s << u"Lista zakupów"_s;
        QTest::newRow("upper-case extension") << u"notatka.TXT"_s << QString() << u"notatka"_s;
        QTest::newRow("markdown extension") << u"Plan.markdown"_s << QString() << u"Plan"_s;
        QTest::newRow("other extension kept") << u"wersja 1.2"_s << QString() << u"wersja 1.2"_s;
        QTest::newRow("sanitised") << u"a:b?c*.md"_s << QString() << u"a b c"_s;
        QTest::newRow("folder in the name") << u"Download/Łódź.md"_s << QString() << u"Łódź"_s;
        QTest::newRow("heading") << QString() << u"# Zażółć gęślą jaźń\nreszta"_s << u"Zażółć gęślą jaźń"_s;
        QTest::newRow("checkbox") << QString() << u"\n\n- [ ] zadanie do zrobienia\n"_s << u"zadanie do zrobienia"_s;
        QTest::newRow("numbered") << QString() << u"1. Pierwszy punkt"_s << u"Pierwszy punkt"_s;
        QTest::newRow("emphasis and link")
                << QString() << u"**Ważne**: [strona](https://a.pl) i [[Dom|dom]]"_s << u"Ważne strona i dom"_s;
        QTest::newRow("frontmatter skipped") << QString() << u"---\ntags: a\n---\n## Treść"_s << u"Treść"_s;
        QTest::newRow("reserved characters in text") << QString() << u"Co/kiedy? #pilne"_s << u"Co kiedy pilne"_s;
        QTest::newRow("long line cut on a word")
                << QString()
                << u"Bardzo długi pierwszy wiersz notatki, który zdecydowanie przekracza sześćdziesiąt znaków"_s
                << u"Bardzo długi pierwszy wiersz notatki, który zdecydowanie"_s;
        QTest::newRow("one long word") << QString() << QString(70, u'ż') << QString(60, u'ż');
        QTest::newRow("bare link dropped") << QString() << u"https://a.pl/x\nZobacz https://b.pl"_s << u"Zobacz"_s;
        QTest::newRow("autolink dropped") << QString() << u"<https://a.pl> — opis"_s << u"opis"_s;
        QTest::newRow("nothing") << QString() << u"  \n\n"_s << u"Udostępnione"_s;
        QTest::newRow(".md alone") << u".md"_s << QString() << u"Udostępnione"_s;
        QTest::newRow("only markers") << QString() << u"- \n#\n---"_s << u"Udostępnione"_s;
        QTest::newRow(".md alone, text") << u".md"_s << u"Z treści"_s << u"Z treści"_s;
    }
    void sharedPageTitle()
    {
        QFETCH(QString, fileName);
        QFETCH(QString, text);
        QFETCH(QString, expected);
        const QString title = notebook::sharedPageTitle(fileName, text, u"Udostępnione"_s);
        QCOMPARE(title, expected);
        QVERIFY(title.size() <= 180);
        QVERIFY(!title.contains(u'/'));
    }

    void sharedPageContent_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("kept") << u"# Tytuł\n\n- a\n"_s << u"# Tytuł\n\n- a\n"_s;
        QTest::newRow("newline added") << u"jedna linia"_s << u"jedna linia\n"_s;
        QTest::newRow("crlf added") << u"a\r\nb"_s << u"a\r\nb\r\n"_s;
        QTest::newRow("crlf kept") << u"a\r\nb\r\n"_s << u"a\r\nb\r\n"_s;
        QTest::newRow("txt not reformatted")
                << u"  wcięcie\tz tabem  \n*nie lista"_s << u"  wcięcie\tz tabem  \n*nie lista\n"_s;
        QTest::newRow("blank") << u" \n\t\n"_s << QString();
        QTest::newRow("empty") << QString() << QString();
    }
    void sharedPageContent()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(notebook::sharedPageContent(text), expected);
    }

    void sharedFileBlock_data()
    {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("lines below the title")
                << u"Lista zakupów"_s << u"mleko\r\nchleb\r\n"_s << u"- **Lista zakupów**\n  mleko\n  chleb"_s;
        QTest::newRow("nested list kept")
                << u"Plan"_s << u"- a\n  - b\n\tc"_s << u"- **Plan**\n  - a\n    - b\n  \tc"_s;
        QTest::newRow("blank runs as one") << u"Ż"_s << u"\n\na\n\n\n\nb\n\n"_s << u"- **Ż**\n  a\n\n  b"_s;
        QTest::newRow("frontmatter dropped")
                << u"Notatka"_s << u"---\ntags: x\n---\ntreść"_s << u"- **Notatka**\n  treść"_s;
        QTest::newRow("empty file") << u"Pusty"_s << QString() << u"- **Pusty**"_s;
        QTest::newRow("trailing spaces dropped") << u"T"_s << u"a   \nb\t"_s << u"- **T**\n  a\n  b"_s;
        QTest::newRow("asterisks in the title") << u"**Gwiazdki**"_s << u"x"_s << u"- **Gwiazdki**\n  x"_s;
    }
    void sharedFileBlock()
    {
        QFETCH(QString, title);
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(notebook::sharedFileBlock(title, text), expected);
    }

    void fromJson()
    {
        const QByteArray json =
                "{\"subject\":\"Artykuł\",\"text\":\"Zażółć https://a.pl\",\"dir\":\"/x/share-in/1\",\n"
                "            "
                "\"files\":[{\"path\":\"/x/share-in/1/0/Łódź.md\",\"name\":\"Łódź.md\",\"mime\":\"text/"
                "markdown\",\"size\":12},\n"
                "                     {\"name\":\"duży.txt\",\"error\":\"tooLarge\",\"size\":9999999},\n"
                "                     \"nie obiekt\",\n"
                "                     {\"path\":\"/x/b.txt\",\"name\":7,\"mime\":null,\"size\":\"10\"}]}";
        const SharedContent share = sharedContentFromJson(json);
        QCOMPARE(share.subject, u"Artykuł"_s);
        QCOMPARE(share.text, u"Zażółć https://a.pl"_s);
        QCOMPARE(share.url, QString());
        QCOMPARE(share.dir, u"/x/share-in/1"_s);
        QCOMPARE(share.files.size(), 3);
        QCOMPARE(share.files.at(0).path, u"/x/share-in/1/0/Łódź.md"_s);
        QCOMPARE(share.files.at(0).name, u"Łódź.md"_s);
        QCOMPARE(share.files.at(0).mime, u"text/markdown"_s);
        QCOMPARE(share.files.at(0).size, qint64(12));
        QCOMPARE(share.files.at(0).error, QString());
        QCOMPARE(share.files.at(1).error, u"tooLarge"_s);
        QCOMPARE(share.files.at(1).path, QString());
        // Wrong types count as absent.
        QCOMPARE(share.files.at(2).path, u"/x/b.txt"_s);
        QCOMPARE(share.files.at(2).name, QString());
        QCOMPARE(share.files.at(2).mime, QString());
        QCOMPARE(share.files.at(2).size, qint64(-1));
        QVERIFY(!share.isEmpty());
    }

    void fromJsonMalformed_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<bool>("empty");
        QTest::newRow("empty input") << QByteArray() << true;
        QTest::newRow("broken") << QByteArray("{\"text\":\"ą") << true;
        QTest::newRow("array") << QByteArray("[\"text\"]") << true;
        QTest::newRow("string") << QByteArray("\"tekst\"") << true;
        QTest::newRow("empty object") << QByteArray("{}") << true;
        QTest::newRow("wrong types") << QByteArray("{\"text\":1,\"subject\":[],\"files\":{\"a\":1}}") << true;
        QTest::newRow("files not objects") << QByteArray("{\"files\":[1,\"a\",null,{}]}") << true;
        QTest::newRow("blank text") << QByteArray("{\"text\":\"  \\n\"}") << true;
        QTest::newRow("subject only") << QByteArray("{\"subject\":\"Ś\"}") << false;
        QTest::newRow("file without path") << QByteArray("{\"files\":[{\"name\":\"a.md\"}]}") << false;
        QTest::newRow("nul in a string") << QByteArray("{\"text\":\"a\\u0000b\"}") << false;
    }
    void fromJsonMalformed()
    {
        QFETCH(QByteArray, json);
        QFETCH(bool, empty);
        QCOMPARE(sharedContentFromJson(json).isEmpty(), empty);
    }

    void fromJsonFileWithoutPathIsUnreadable()
    {
        const SharedContent share = sharedContentFromJson(QByteArray("{\"files\":[{\"name\":\"a.md\"}]}"));
        QCOMPARE(share.files.size(), 1);
        QCOMPARE(share.files.first().error, u"unreadable"_s);
    }

    void fromJsonTooManyFiles()
    {
        QJsonArray files;
        for (int i = 0; i < 150; ++i)
            files.append(QJsonObject{{u"path"_s, u"/x/%1.md"_s.arg(i)}, {u"name"_s, u"%1.md"_s.arg(i)}});
        const SharedContent share = sharedContentFromJson(QJsonObject{{u"files"_s, files}});
        QCOMPARE(share.files.size(), 100);
        for (int i = 0; i < SharedFilesMax; ++i)
            QCOMPARE(share.files.at(i).error, QString());
        QCOMPARE(share.files.at(SharedFilesMax).error, u"tooMany"_s);
        QCOMPARE(share.files.at(SharedFilesMax).path, QString());
        QCOMPARE(share.files.last().name, u"99.md"_s);
    }
};

QTEST_GUILESS_MAIN(TestNotebookShare)
#include "tst_notebook_share.moc"
