// Shares from other apps (notebook/share.h, "Udostępnij → Brulion"): the platform's JSON read
// defensively and written back (round trip), one capture text from a title, a text and a link (the
// previous app's share target cases), the browser's share target query (form encoding, malformed
// escapes, invalid UTF-8, unknown and repeated keys, the length cap), files opened with Brulion on a
// desktop (`--open`: the command line split, relative paths, file URLs, other URLs, duplicates, the
// caps, two opens merged) and which of them are pages of the open notebook, which files are
// accepted (text types only, size cap, NUL bytes), decoding (UTF-8 with and without a BOM, UTF-16,
// windows-1250/1252, line endings kept), a new page's title and content, and the journal block of
// a shared file.
#include "notebook/share.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

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

    void fromQuery_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("subject");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("url");
        // The previous app's case (archive/web-v1 share-target.test.ts), with this app's names.
        QTest::newRow("title, text and url") << u"share-title=Artyku%C5%82&share-text=Ciekawe&share-url=https%3A%2F%2Fa.pl"_s
                                             << u"Artykuł"_s << u"Ciekawe"_s << u"https://a.pl"_s;
        QTest::newRow("raw utf-8") << u"share-text=Zażółć gęślą jaźń"_s << QString() << u"Zażółć gęślą jaźń"_s
                                   << QString();
        QTest::newRow("encoded utf-8")
                << u"share-text=Za%C5%BC%C3%B3%C5%82%C4%87+g%C4%99%C5%9Bl%C4%85+ja%C5%BA%C5%84"_s << QString()
                << u"Zażółć gęślą jaźń"_s << QString();
        QTest::newRow("upper-case escapes") << u"share-text=%C5%81%c3%b3d%C5%BA"_s << QString() << u"Łódź"_s
                                            << QString();
        QTest::newRow("leading question mark") << u"?share-title=Tytu%C5%82"_s << u"Tytuł"_s << QString() << QString();
        QTest::newRow("plus and an encoded plus") << u"share-text=a+b%2Bc"_s << QString() << u"a b+c"_s << QString();
        QTest::newRow("percent without hex digits")
                << u"share-text=100%+pewne%zz"_s << QString() << u"100% pewne%zz"_s << QString();
        QTest::newRow("percent at the end") << u"share-text=50%"_s << QString() << u"50%"_s << QString();
        QTest::newRow("truncated sequence") << u"share-text=%C5"_s << QString() << u"\uFFFD"_s << QString();
        QTest::newRow("invalid utf-8") << u"share-text=%E2%28%A1"_s << QString() << u"\uFFFD(\uFFFD"_s << QString();
        QTest::newRow("line breaks kept") << u"share-text=a%0D%0Ab"_s << QString() << u"a\r\nb"_s << QString();
        QTest::newRow("encoded ampersand and equals")
                << u"share-text=Ala+%26+kot+%3D+2"_s << QString() << u"Ala & kot = 2"_s << QString();
        QTest::newRow("unknown keys left out")
                << u"share-text=x&nosw&v=3&selftest=share&title=y&text=z"_s << QString() << u"x"_s << QString();
        QTest::newRow("repeated key: the first") << u"share-text=pierwszy&share-text=drugi"_s << QString()
                                                 << u"pierwszy"_s << QString();
        QTest::newRow("keys are case-sensitive") << u"Share-Text=x&SHARE-URL=y"_s << QString() << QString() << QString();
        QTest::newRow("encoded key") << u"share%2Dtext=x"_s << QString() << u"x"_s << QString();
        QTest::newRow("key without a value") << u"share-text"_s << QString() << QString() << QString();
        QTest::newRow("empty values") << u"share-title=&share-text=&share-url="_s << QString() << QString()
                                      << QString();
        QTest::newRow("ampersands only") << u"&&&"_s << QString() << QString() << QString();
        QTest::newRow("equals only") << u"="_s << QString() << QString() << QString();
        QTest::newRow("percent only") << u"%"_s << QString() << QString() << QString();
        QTest::newRow("empty") << QString() << QString() << QString() << QString();
        QTest::newRow("value with equals signs") << u"share-url=https://a.pl/?q=1&share-title=T"_s << u"T"_s
                                                 << QString() << u"https://a.pl/?q=1"_s;
    }
    void fromQuery()
    {
        QFETCH(QString, query);
        QFETCH(QString, subject);
        QFETCH(QString, text);
        QFETCH(QString, url);
        const SharedContent share = sharedContentFromQuery(query);
        QCOMPARE(share.subject, subject);
        QCOMPARE(share.text, text);
        QCOMPARE(share.url, url);
        QVERIFY(share.files.isEmpty());
        QCOMPARE(share.source, QString());
        QCOMPARE(share.dir, QString());
        QCOMPARE(share.isEmpty(), subject.isEmpty() && text.isEmpty() && url.isEmpty());
    }

    void fromQueryLongText()
    {
        // Cut to SharedTextMaxChars characters ("ą" is one UTF-16 unit).
        QString query = u"share-text="_s;
        query += QString(SharedTextMaxChars + 1, u'ą');
        QCOMPARE(sharedContentFromQuery(query).text.size(), SharedTextMaxChars);
        // Never between the halves of a surrogate pair: an emoji across the limit goes as a whole.
        QString emoji = u"share-text="_s + QString(SharedTextMaxChars - 1, u'a') + u"😀b"_s;
        const QString cut = sharedContentFromQuery(emoji).text;
        QCOMPARE(cut.size(), SharedTextMaxChars - 1);
        QVERIFY(!cut.back().isHighSurrogate());
        // An emoji that fits ends the text whole.
        emoji = u"share-text="_s + QString(SharedTextMaxChars - 2, u'a') + u"😀b"_s;
        QCOMPARE(sharedContentFromQuery(emoji).text.right(2), u"😀"_s);
    }

    void fromQueryComposes()
    {
        // Chrome on Android: a page's title, the link in the text.
        const SharedContent share = sharedContentFromQuery(
                u"share-title=Przepis+na+%C5%BCurek&share-text=Zobacz+to+https%3A%2F%2Fkuchnia.pl%2F%C5%BCurek"_s);
        QCOMPARE(notebook::composeSharedText(share.subject, share.text, share.url),
                 u"[Przepis na żurek](https://kuchnia.pl/żurek) — Zobacz to"_s);
        const SharedContent link =
                sharedContentFromQuery(u"share-title=%C5%81%C3%B3d%C5%BA&share-url=https%3A%2F%2Fl.pl%2Fx"_s);
        QCOMPARE(notebook::composeSharedText(link.subject, link.text, link.url), u"[Łódź](https://l.pl/x)"_s);
    }

    void toJsonRoundTrip()
    {
        SharedContent share;
        share.subject = u"Artykuł o Łodzi"_s;
        share.text = u"Zażółć gęślą jaźń\r\ndruga linia"_s;
        share.url = u"https://pl.wikipedia.org/wiki/Łódź"_s;
        share.dir = u"/dane/share-in/1792000000000-1"_s;
        share.source = u"open"_s;
        share.files = {
                SharedFile{u"/home/ala/Lista zakupów.md"_s, u"Lista zakupów.md"_s, u"text/markdown"_s, 120, QString()},
                SharedFile{QString(), u"duży.txt"_s, QString(), 9'999'999, u"tooLarge"_s},
                SharedFile{u"/home/ala/b.txt"_s, QString(), QString(), -1, QString()},
                SharedFile{QString(), u"21.md"_s, QString(), -1, u"tooMany"_s},
        };
        const QJsonObject json = sharedContentToJson(share);
        QVERIFY(!json.value(u"files"_s).toArray().at(2).toObject().contains(u"size"_s)); // -1 left out
        QVERIFY(!json.value(u"files"_s).toArray().at(2).toObject().contains(u"name"_s)); // "" left out
        const SharedContent back = sharedContentFromJson(json);
        QCOMPARE(back.subject, share.subject);
        QCOMPARE(back.text, share.text);
        QCOMPARE(back.url, share.url);
        QCOMPARE(back.dir, share.dir);
        QCOMPARE(back.source, share.source);
        QCOMPARE(back.files.size(), share.files.size());
        for (qsizetype i = 0; i < share.files.size(); ++i) {
            QCOMPARE(back.files.at(i).path, share.files.at(i).path);
            QCOMPARE(back.files.at(i).name, share.files.at(i).name);
            QCOMPARE(back.files.at(i).mime, share.files.at(i).mime);
            QCOMPARE(back.files.at(i).size, share.files.at(i).size);
            QCOMPARE(back.files.at(i).error, share.files.at(i).error);
        }
        // Also through the bytes a second launch sends.
        const QByteArray bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QCOMPARE(sharedContentFromJson(bytes).files.first().name, u"Lista zakupów.md"_s);
        // An empty share is an empty object.
        QCOMPARE(sharedContentToJson(SharedContent{}), QJsonObject());
        // Only "open" is a source.
        for (const QJsonValue &source : {QJsonValue(u"web"_s), QJsonValue(42), QJsonValue(u"OPEN"_s), QJsonValue()}) {
            QCOMPARE(sharedContentFromJson(QJsonObject{{u"text"_s, u"t"_s}, {u"source"_s, source}}).source, QString());
        }
    }

    void splitOpenArguments_data()
    {
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<QStringList>("options");
        QTest::addColumn<QStringList>("files");
        QTest::newRow("program only") << QStringList{u"brulion"_s} << QStringList{u"brulion"_s} << QStringList();
        QTest::newRow("open without files")
                << QStringList{u"brulion"_s, u"--open"_s} << QStringList{u"brulion"_s} << QStringList();
        QTest::newRow("options, then files")
                << QStringList{u"brulion"_s, u"--vault"_s, u"/v"_s, u"--open"_s, u"a.md"_s, u"--capture"_s}
                << QStringList{u"brulion"_s, u"--vault"_s, u"/v"_s} << QStringList{u"a.md"_s, u"--capture"_s};
        QTest::newRow("a second --open is a file")
                << QStringList{u"brulion"_s, u"--open"_s, u"Żółw.md"_s, u"--open"_s}
                << QStringList{u"brulion"_s} << QStringList{u"Żółw.md"_s, u"--open"_s};
        QTest::newRow("no --open: positional arguments stay options")
                << QStringList{u"brulion"_s, u"a.md"_s, u"--today"_s} << QStringList{u"brulion"_s, u"a.md"_s, u"--today"_s}
                << QStringList();
        QTest::newRow("--open=x is no separator")
                << QStringList{u"brulion"_s, u"--open=a.md"_s} << QStringList{u"brulion"_s, u"--open=a.md"_s}
                << QStringList();
        QTest::newRow("empty") << QStringList() << QStringList() << QStringList();
    }
    void splitOpenArguments()
    {
        QFETCH(QStringList, arguments);
        QFETCH(QStringList, options);
        QFETCH(QStringList, files);
        const OpenArguments split = notebook::splitOpenArguments(arguments);
        QCOMPARE(split.options, options);
        QCOMPARE(split.files, files);
    }

    void openedFilesShare_data()
    {
        // "<wd>" is the working directory (a temporary folder: absolute on every system).
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<QStringList>("paths");  // "" for an entry without a path
        QTest::addColumn<QStringList>("names");
        QTest::addColumn<QStringList>("errors");
        QTest::newRow("relative") << QStringList{u"Lista zakupów.md"_s} << QStringList{u"<wd>/Lista zakupów.md"_s}
                                  << QStringList{u"Lista zakupów.md"_s} << QStringList{QString()};
        QTest::newRow("parent folder") << QStringList{u"../a.md"_s} << QStringList{u"<up>/a.md"_s}
                                       << QStringList{u"a.md"_s} << QStringList{QString()};
        QTest::newRow("cleaned") << QStringList{u"x/./y//z.txt"_s} << QStringList{u"<wd>/x/y/z.txt"_s}
                                 << QStringList{u"z.txt"_s} << QStringList{QString()};
        QTest::newRow("absolute") << QStringList{u"<wd>/Notatki/Łódź.md"_s} << QStringList{u"<wd>/Notatki/Łódź.md"_s}
                                  << QStringList{u"Łódź.md"_s} << QStringList{QString()};
        QTest::newRow("file url, percent-encoded")
                << QStringList{u"file://<wd>/Za%C5%BC%C3%B3%C5%82%C4%87.md"_s} << QStringList{u"<wd>/Zażółć.md"_s}
                << QStringList{u"Zażółć.md"_s} << QStringList{QString()};
        QTest::newRow("file url, raw letters") << QStringList{u"file://<wd>/gęś.txt"_s} << QStringList{u"<wd>/gęś.txt"_s}
                                               << QStringList{u"gęś.txt"_s} << QStringList{QString()};
        QTest::newRow("web address") << QStringList{u"https://example.com/notatki/a.md"_s} << QStringList{QString()}
                                     << QStringList{u"a.md"_s} << QStringList{u"unreadable"_s};
        QTest::newRow("another computer's file") << QStringList{u"smb://serwer/udział/Żółw.md"_s}
                                                 << QStringList{QString()} << QStringList{u"Żółw.md"_s}
                                                 << QStringList{u"unreadable"_s};
        QTest::newRow("a colon in a name is a path")
                << QStringList{u"notatka:ważna.md"_s} << QStringList{u"<wd>/notatka:ważna.md"_s}
                << QStringList{u"notatka:ważna.md"_s} << QStringList{QString()};
        QTest::newRow("empty skipped") << QStringList{QString(), u"a.md"_s, QString()} << QStringList{u"<wd>/a.md"_s}
                                       << QStringList{u"a.md"_s} << QStringList{QString()};
        QTest::newRow("the same file twice")
                << QStringList{u"a.md"_s, u"<wd>/a.md"_s, u"./a.md"_s, u"file://<wd>/a.md"_s}
                << QStringList{u"<wd>/a.md"_s} << QStringList{u"a.md"_s} << QStringList{QString()};
        QTest::newRow("a name with a line break and quotes")
                << QStringList{u"dwie\nlinie \"cytat\".md"_s} << QStringList{u"<wd>/dwie\nlinie \"cytat\".md"_s}
                << QStringList{u"dwie\nlinie \"cytat\".md"_s} << QStringList{QString()};
        QTest::newRow("an option-like name") << QStringList{u"--capture"_s} << QStringList{u"<wd>/--capture"_s}
                                             << QStringList{u"--capture"_s} << QStringList{QString()};
        QTest::newRow("none") << QStringList() << QStringList() << QStringList() << QStringList();
    }
    void openedFilesShare()
    {
        QFETCH(QStringList, arguments);
        QFETCH(QStringList, paths);
        QFETCH(QStringList, names);
        QFETCH(QStringList, errors);
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString wd = QDir(tmp.path()).filePath(u"praca"_s);
        const QString up = QDir::cleanPath(tmp.path());
        const auto expand = [&](QString s) {
            QString root = wd;
            // A file URL's path starts with "/" (file:///C:/… on Windows).
            if (s.startsWith(u"file://<wd>"_s) && !root.startsWith(u'/'))
                root.prepend(u'/');
            return s.replace(u"<wd>"_s, s.startsWith(u"file:"_s) ? root : wd).replace(u"<up>"_s, up);
        };
        QStringList args;
        for (const QString &a : arguments)
            args.append(expand(a));
        const SharedContent share = notebook::openedFilesShare(args, wd);
        QCOMPARE(share.source, u"open"_s);
        QCOMPARE(share.dir, QString());
        QVERIFY(share.subject.isEmpty() && share.text.isEmpty() && share.url.isEmpty());
        QCOMPARE(share.files.size(), paths.size());
        for (qsizetype i = 0; i < paths.size(); ++i) {
            QCOMPARE(share.files.at(i).path, expand(paths.at(i)));
            QCOMPARE(share.files.at(i).name, names.at(i));
            QCOMPARE(share.files.at(i).error, errors.at(i));
            QCOMPARE(share.files.at(i).mime, QString());
            QCOMPARE(share.files.at(i).size, qint64(-1));
        }
        // What a second launch sends reads back the same.
        const SharedContent back = sharedContentFromJson(sharedContentToJson(share));
        QCOMPARE(back.files.size(), share.files.size());
        QCOMPARE(back.source, u"open"_s);
    }

    void openedFilesShareCaps()
    {
        QStringList args;
        for (int i = 0; i < 150; ++i)
            args.append(u"/notatki/%1.md"_s.arg(i));
        const SharedContent share = notebook::openedFilesShare(args, u"/"_s);
        QCOMPARE(share.files.size(), SharedEntriesMax);
        for (int i = 0; i < SharedFilesMax; ++i)
            QCOMPARE(share.files.at(i).error, QString());
        QCOMPARE(share.files.at(SharedFilesMax).error, u"tooMany"_s);
        QCOMPARE(share.files.at(SharedFilesMax).path, QString());
        QCOMPARE(share.files.at(SharedFilesMax).name, u"20.md"_s);
        QCOMPARE(share.files.last().name, u"99.md"_s);
    }

    void mergeOpenedShares()
    {
        SharedContent waiting = notebook::openedFilesShare({u"/n/a.md"_s, u"/n/b.md"_s}, u"/"_s);
        SharedContent newer = notebook::openedFilesShare({u"/n/b.md"_s, u"/n/c.md"_s}, u"/"_s);
        SharedContent merged = notebook::mergeOpenedShares(waiting, newer);
        QCOMPARE(merged.source, u"open"_s);
        QCOMPARE(merged.dir, QString());
        QCOMPARE(merged.files.size(), 3);
        QCOMPARE(merged.files.at(0).path, u"/n/a.md"_s);
        QCOMPARE(merged.files.at(1).path, u"/n/b.md"_s);
        QCOMPARE(merged.files.at(2).path, u"/n/c.md"_s);

        // The caps again: 15 + 15 -> 20 readable, 10 "tooMany".
        QStringList first;
        QStringList second;
        for (int i = 0; i < 15; ++i) {
            first.append(u"/n/pierwszy-%1.md"_s.arg(i));
            second.append(u"/n/drugi-%1.md"_s.arg(i));
        }
        merged = notebook::mergeOpenedShares(notebook::openedFilesShare(first, u"/"_s),
                                             notebook::openedFilesShare(second, u"/"_s));
        QCOMPARE(merged.files.size(), 30);
        QCOMPARE(int(std::count_if(merged.files.cbegin(), merged.files.cend(),
                                   [](const SharedFile &f) { return f.error.isEmpty(); })),
                 SharedFilesMax);
        QCOMPARE(merged.files.at(20).error, u"tooMany"_s);
        QCOMPARE(merged.files.at(20).name, u"drugi-5.md"_s);

        // Text from the newer share when it has some, else the waiting one's.
        waiting.subject = u"Stary"_s;
        QCOMPARE(notebook::mergeOpenedShares(waiting, newer).subject, u"Stary"_s);
        newer.text = u"Nowy tekst ą"_s;
        merged = notebook::mergeOpenedShares(waiting, newer);
        QCOMPARE(merged.subject, QString());
        QCOMPARE(merged.text, u"Nowy tekst ą"_s);
        // A dir of either is never kept.
        waiting.dir = u"/dane/share-in/1"_s;
        QCOMPARE(notebook::mergeOpenedShares(waiting, newer).dir, QString());
        // Entries without a path (another computer's file, a file over the cap) are listed once
        // by name and reason.
        merged = notebook::mergeOpenedShares(notebook::openedFilesShare({u"smb://s/a.md"_s}, u"/"_s),
                                             notebook::openedFilesShare({u"smb://s/a.md"_s, u"smb://s/b.md"_s},
                                                                        u"/"_s));
        QCOMPARE(merged.files.size(), 2);
        QCOMPARE(merged.files.at(1).name, u"b.md"_s);
    }

    void pageForOpenedFile_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("root");
        QTest::addColumn<bool>("ignoreCase");
        QTest::addColumn<QString>("page");
        QTest::newRow("in a folder") << u"/v/Projekty/Plan.md"_s << u"/v"_s << false << u"Projekty/Plan.md"_s;
        QTest::newRow("at the root, polish") << u"/v/Żółw.md"_s << u"/v"_s << false << u"Żółw.md"_s;
        QTest::newRow("root with a slash") << u"/v/a.md"_s << u"/v/"_s << false << u"a.md"_s;
        QTest::newRow("markdown extension") << u"/v/a.markdown"_s << u"/v"_s << false << u"a.markdown"_s;
        QTest::newRow("upper-case extension") << u"/v/A.MD"_s << u"/v"_s << false << u"A.MD"_s;
        QTest::newRow("journal") << u"/v/journals/2026-10-14.md"_s << u"/v"_s << false << u"journals/2026-10-14.md"_s;
        QTest::newRow("a sibling folder") << u"/v2/a.md"_s << u"/v"_s << false << QString();
        QTest::newRow("the folder itself") << u"/v"_s << u"/v"_s << false << QString();
        QTest::newRow("the folder with a slash") << u"/v/"_s << u"/v"_s << false << QString();
        QTest::newRow("outside") << u"/home/ala/a.md"_s << u"/v"_s << false << QString();
        QTest::newRow("a text file") << u"/v/notatka.txt"_s << u"/v"_s << false << QString();
        QTest::newRow("no extension") << u"/v/README"_s << u"/v"_s << false << QString();
        QTest::newRow("obsidian settings") << u"/v/.obsidian/x.md"_s << u"/v"_s << false << QString();
        QTest::newRow("trash") << u"/v/.trash/x.md"_s << u"/v"_s << false << QString();
        QTest::newRow("brulion's folder") << u"/v/.brulion/x.md"_s << u"/v"_s << false << QString();
        QTest::newRow("logseq") << u"/v/logseq/custom.md"_s << u"/v"_s << false << QString();
        QTest::newRow("a hidden folder") << u"/v/a/.ukryty/x.md"_s << u"/v"_s << false << QString();
        QTest::newRow("a syncthing temporary file")
                << u"/v/~syncthing~a.md.tmp"_s << u"/v"_s << false << QString();
        QTest::newRow("a conflict copy is a page")
                << u"/v/a.sync-conflict-20261028-101010-ABCDEFG.md"_s << u"/v"_s << false
                << u"a.sync-conflict-20261028-101010-ABCDEFG.md"_s;
        QTest::newRow("not clean") << u"/v/a/../b.md"_s << u"/v"_s << false << QString();
        QTest::newRow("double slash") << u"/v//b.md"_s << u"/v"_s << false << QString();
        QTest::newRow("letter case of the folder") << u"/V/a.md"_s << u"/v"_s << false << QString();
        QTest::newRow("letter case ignored")
                << u"C:/Users/ala/NOTATKI/Plan.md"_s << u"C:/Users/ala/Notatki"_s << true << u"Plan.md"_s;
        QTest::newRow("windows sibling") << u"C:/Notatki2/a.md"_s << u"C:/Notatki"_s << true << QString();
        QTest::newRow("root of a drive") << u"D:/a.md"_s << u"D:/"_s << true << u"a.md"_s;
        QTest::newRow("the system root") << u"/a.md"_s << u"/"_s << false << u"a.md"_s;
        QTest::newRow("empty file") << QString() << u"/v"_s << false << QString();
        QTest::newRow("empty root") << u"/v/a.md"_s << QString() << false << QString();
    }
    void pageForOpenedFile()
    {
        QFETCH(QString, file);
        QFETCH(QString, root);
        QFETCH(bool, ignoreCase);
        QFETCH(QString, page);
        QCOMPARE(notebook::pageForOpenedFile(file, root, ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive), page);
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
