// CSV, Airtable and Todoist importers: reading (encodings with and without a BOM, windows-1250 /
// ISO-8859-2 of Polish exports, UTF-16, separators, Excel's "sep=", the header row, quoting and
// line breaks in cells, ragged rows), the mapping (titles from a column or a template, folders,
// the body column, inferred property types, lists, tags, links between rows, attachments,
// duplicate and illegal titles), the base and the table page; Airtable's CSV export and its API
// download; Todoist's CSV template and the Sync API's JSON; detection; malformed input. Fixtures
// are built in code after real exports (Excel, Google Sheets, Airtable, Todoist).
#include "iotest.h"

#include "io/csvimport.h"
#include "io/csvimport_p.h"
#include "io/importer.h"
#include "text/decode.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringEncoder>
#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;
using iotest::textAt;

/// QCOMPARE of long texts, printing the whole actual text when they differ.
#define COMPARE_TEXT(actual, expected)                                                                                 \
    do {                                                                                                               \
        const QString a_ = (actual);                                                                                   \
        const QString e_ = (expected);                                                                                 \
        if (a_ != e_)                                                                                                  \
            qWarning().noquote() << "actual:\n" << a_ << "\nexpected:\n" << e_;                                        \
        QCOMPARE(a_, e_);                                                                                              \
    } while (false)

namespace {

ImportResult importAs(const QString &id, const QList<InputFile> &files, const ImportOptions &options)
{
    const Importer *importer = importerById(id);
    if (!importer)
        qFatal("no importer");
    return runImport(*importer, files, options);
}

ImportOptions csvOptions(const QString &folder = u"Import/CSV"_s)
{
    ImportOptions o = iotest::options(folder);
    o.now = QDateTime(QDate(2026, 11, 20), QTime(18, 30));
    return o;
}

QByteArray utf16le(const QString &s, bool bom)
{
    QStringEncoder enc(QStringConverter::Utf16LE);
    QByteArray out = enc(s);
    return bom ? QByteArray("\xFF\xFE") + out : out;
}

/// Polish text in an 8-bit code page (the characters of the fixtures only).
QByteArray legacy(const QString &s, text::TextEncoding encoding)
{
    QByteArray out;
    for (const QChar c : s) {
        if (c.unicode() < 0x80) {
            out += char(c.unicode());
            continue;
        }
        for (int b = 0x80; b < 0x100; ++b) {
            if (text::decodeTextAs(QByteArray(1, char(b)), encoding) == QString(c)) {
                out += char(b);
                break;
            }
        }
    }
    return out;
}

const QString Books = u"Tytuł,Autor,Rok,Cena,Przeczytana,Gatunki,Data zakupu\n"
                      "Pan Tadeusz,Adam Mickiewicz,1834,\"29,99\",tak,\"epopeja, klasyka\",2026-03-15\n"
                      "Lalka,Bolesław Prus,1890,\"34,50\",nie,\"powieść, klasyka\",15.04.2026\n"
                      "Quo vadis,Henryk Sienkiewicz,1896,\"19,90\",tak,\"powieść, historyczna\",20 listopada 2025\n"_s;

} // namespace

class TestIoImportCsv : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- Reading -------------------------------------------------------------------------------

    void encodings_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("encoding");
        const QString csv = u"Nazwa;Miasto\nŻółć;Łódź\nźdźbło;Gdańsk\n"_s;
        QTest::newRow("utf-8") << csv.toUtf8() << u"utf-8"_s;
        QTest::newRow("utf-8 bom") << QByteArray("\xEF\xBB\xBF") + csv.toUtf8() << u"utf-8"_s;
        QTest::newRow("utf-16le bom") << utf16le(csv, true) << u"utf-16le"_s;
        QTest::newRow("utf-16le without bom") << utf16le(csv, false) << u"utf-16le"_s;
        QTest::newRow("windows-1250") << legacy(csv, text::TextEncoding::Windows1250) << u"windows-1250"_s;
        QTest::newRow("iso-8859-2") << legacy(csv, text::TextEncoding::Iso8859_2) << u"iso-8859-2"_s;
    }
    void encodings()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, encoding);
        const CsvAnalysis a = analyzeCsv({iotest::bytes(u"miasta.csv"_s, data)}, {});
        QCOMPARE(a.files.size(), 1);
        QCOMPARE(a.files[0].encoding, encoding);
        QCOMPARE(a.files[0].separator, u';');
        QCOMPARE(a.files[0].rows.value(0), (QStringList{u"Żółć"_s, u"Łódź"_s}));
        QCOMPARE(a.files[0].rows.value(1), (QStringList{u"źdźbło"_s, u"Gdańsk"_s}));
        // A chosen encoding wins.
        CsvImportOptions o;
        o.encoding = u"cp1252"_s;
        const CsvAnalysis forced = analyzeCsv(
                {iotest::bytes(u"miasta.csv"_s, legacy(u"A;B\nż;ł\n"_s, text::TextEncoding::Windows1250))}, o);
        QCOMPARE(forced.files[0].rows.value(0).value(0), u"¿"_s);
    }

    void separators_data()
    {
        QTest::addColumn<QString>("csv");
        QTest::addColumn<QChar>("separator");
        QTest::newRow("comma") << u"a,b,c\n1,2,3\n"_s << QChar(u',');
        QTest::newRow("semicolon with commas in text")
                << u"Imię;Opis\nJan;Kowalski, Jan\nAnna;Nowak, Anna\n"_s << QChar(u';');
        QTest::newRow("tab") << u"a\tb\n1\t2\n"_s << QChar(u'\t');
        QTest::newRow("pipe") << u"a|b|c\n1|2|3\n"_s << QChar(u'|');
        QTest::newRow("excel sep line") << u"sep=;\na;b\n1,5;2,5\n"_s << QChar(u';');
        QTest::newRow("quoted separators") << u"\"a;b\",c\n\"1;2\",3\n"_s << QChar(u',');
    }
    void separators()
    {
        QFETCH(QString, csv);
        QFETCH(QChar, separator);
        const CsvAnalysis a = analyzeCsv({iotest::text(u"t.csv"_s, csv)}, {});
        QCOMPARE(a.files.size(), 1);
        QCOMPARE(a.files[0].separator, separator);
        QVERIFY(a.files[0].columns.size() >= 2);
    }

    void headerDetection_data()
    {
        QTest::addColumn<QString>("csv");
        QTest::addColumn<bool>("header");
        QTest::newRow("names over numbers") << u"Nazwa,Ilość\nJabłka,3\nGruszki,5\n"_s << true;
        QTest::newRow("data only") << u"Jabłka,3\nGruszki,5\nŚliwki,7\n"_s << false;
        QTest::newRow("names over dates") << u"Zadanie,Termin\nRaport,2026-11-20\nFaktura,2026-11-25\n"_s << true;
        QTest::newRow("repeated first row values") << u"x,x\na,b\n"_s << false;
        QTest::newRow("text over text") << u"Imię,Nazwisko\nJan,Kowalski\nAnna,Nowak\n"_s << true;
    }
    void headerDetection()
    {
        QFETCH(QString, csv);
        QFETCH(bool, header);
        const CsvAnalysis a = analyzeCsv({iotest::text(u"t.csv"_s, csv)}, {});
        QCOMPARE(a.files[0].header, header);
        if (!header)
            QCOMPARE(a.files[0].columns.value(0).name, u"Column 1"_s);
    }

    void quotingAndMultiline()
    {
        const QString csv = u"Nazwa,Notatki\r\n\"Zakupy\",\"Mleko\r\nChleb, \"\"razowy\"\"\"\r\nPusty,\r\n"_s;
        const CsvAnalysis a = analyzeCsv({iotest::text(u"t.csv"_s, csv)}, {});
        QCOMPARE(a.files[0].rowCount, 2);
        QCOMPARE(a.files[0].rows[0][1], u"Mleko\r\nChleb, \"razowy\""_s);
        const ImportResult r = importAs(u"csv"_s, {iotest::text(u"Lista.csv"_s, csv)}, csvOptions());
        // A multi-line "Notatki" column is the page's text.
        QCOMPARE(textAt(r, u"Import/CSV/Zakupy.md"_s), u"Mleko\nChleb, \"razowy\"\n"_s);
        QCOMPARE(textAt(r, u"Import/CSV/Pusty.md"_s), u""_s);
    }

    // ---- Types ---------------------------------------------------------------------------------

    void cellValues_data()
    {
        QTest::addColumn<QString>("cell");
        QTest::addColumn<QString>("type");
        QTest::addColumn<QVariant>("value");
        QTest::newRow("integer") << u"1834"_s << u"number"_s << QVariant(qlonglong(1834));
        QTest::newRow("decimal comma") << u"29,99"_s << u"number"_s << QVariant(29.99);
        QTest::newRow("spaced thousands") << u"1 234,5"_s << u"number"_s << QVariant(1234.5);
        QTest::newRow("nbsp thousands") << u"12 000"_s << u"number"_s << QVariant(qlonglong(12000));
        QTest::newRow("us thousands") << u"1,234,567.25"_s << u"number"_s << QVariant(1234567.25);
        QTest::newRow("dot thousands") << u"1.234,56"_s << u"number"_s << QVariant(1234.56);
        QTest::newRow("leading zero is text") << u"007"_s << u"number"_s << QVariant();
        QTest::newRow("phone is text") << u"+48 600 100 200"_s << u"number"_s << QVariant();
        QTest::newRow("iso date") << u"2026-11-20"_s << u"date"_s << QVariant(QDate(2026, 11, 20));
        QTest::newRow("polish date") << u"20.11.2026"_s << u"date"_s << QVariant(QDate(2026, 11, 20));
        QTest::newRow("polish month name") << u"20 listopada 2026"_s << u"date"_s << QVariant(QDate(2026, 11, 20));
        QTest::newRow("english month name") << u"Nov 20, 2026"_s << u"date"_s << QVariant(QDate(2026, 11, 20));
        QTest::newRow("short year") << u"20.11.26"_s << u"date"_s << QVariant(QDate(2026, 11, 20));
        QTest::newRow("bad date") << u"31.02.2026"_s << u"date"_s << QVariant();
        QTest::newRow("date-time") << u"2026-11-20 14:30"_s << u"datetime"_s << QVariant(u"2026-11-20T14:30"_s);
        QTest::newRow("date-time pm") << u"11/20/2026 2:30pm"_s << u"datetime"_s << QVariant(u"2026-11-20T14:30"_s);
        // An instant: converted to the zone (Warsaw is UTC+1 in November).
        QTest::newRow("utc instant") << u"2026-11-20T13:30:00.000Z"_s << u"datetime"_s
                                     << QVariant(u"2026-11-20T14:30"_s);
        QTest::newRow("offset instant") << u"2026-11-20T23:30:00-02:00"_s << u"date"_s << QVariant(QDate(2026, 11, 21));
        QTest::newRow("tak") << u"tak"_s << u"checkbox"_s << QVariant(true);
        QTest::newRow("checked") << u"checked"_s << u"checkbox"_s << QVariant(true);
        QTest::newRow("empty checkbox") << u""_s << u"checkbox"_s << QVariant(false);
        QTest::newRow("Nie") << u"Nie"_s << u"checkbox"_s << QVariant(false);
        QTest::newRow("list") << u"epopeja, klasyka,  ,klasyka"_s << u"list"_s
                              << QVariant(QStringList{u"epopeja"_s, u"klasyka"_s});
        QTest::newRow("text keeps lines") << u" a\r\nb "_s << u"text"_s << QVariant(u"a\nb"_s);
    }
    void cellValues()
    {
        QFETCH(QString, cell);
        QFETCH(QString, type);
        QFETCH(QVariant, value);
        const auto t = csvColumnTypeFromName(type);
        QVERIFY(t);
        QCOMPARE(csvCellValue(cell, *t, u", "_s, iotest::warsaw(), true), value);
    }

    void inferredTypes()
    {
        const CsvAnalysis a = analyzeCsv({iotest::text(u"Książki.csv"_s, Books)}, {});
        QCOMPARE(a.files.size(), 1);
        const CsvFileAnalysis &f = a.files[0];
        QCOMPARE(f.titleColumn, u"Tytuł"_s);
        QStringList types;
        for (const CsvColumnInfo &c : f.columns)
            types.append(c.name + u'=' + csvColumnTypeName(c.type));
        QCOMPARE(types, (QStringList{u"Tytuł=text"_s, u"Autor=text"_s, u"Rok=number"_s, u"Cena=number"_s,
                                     u"Przeczytana=checkbox"_s, u"Gatunki=list"_s, u"Data zakupu=date"_s}));
        QCOMPARE(f.titles.mid(0, 3), (QStringList{u"Pan Tadeusz"_s, u"Lalka"_s, u"Quo vadis"_s}));
        QCOMPARE(f.samplePage,
                 u"---\nAutor: Adam Mickiewicz\nRok: 1834\nCena: 29.99\nPrzeczytana: true\nGatunki:\n  - epopeja\n"
                 "  - klasyka\nData zakupu: 2026-03-15\n---\n"_s);
    }

    // ---- Pages ---------------------------------------------------------------------------------

    void pagesWithProperties()
    {
        const ImportResult r = importAs(u"csv"_s, {iotest::text(u"Książki.csv"_s, Books)}, csvOptions(u"Biblioteka"_s));
        QCOMPARE(iotest::pathsOf(r), (QStringList{u"Biblioteka/Książki.base"_s, u"Biblioteka/Lalka.md"_s,
                                                  u"Biblioteka/Pan Tadeusz.md"_s, u"Biblioteka/Quo vadis.md"_s}));
        QCOMPARE(textAt(r, u"Biblioteka/Lalka.md"_s), u"---\nAutor: Bolesław Prus\nRok: 1890\nCena: 34.5\nPrzeczytana: "
                                                      u"false\nGatunki:\n  - powieść\n  - klasyka\n"
                                                      "Data zakupu: 2026-04-15\n---\n"_s);
        QCOMPARE(textAt(r, u"Biblioteka/Quo vadis.md"_s).contains(u"Data zakupu: 2025-11-20"_s), true);
        QCOMPARE(textAt(r, u"Biblioteka/Książki.base"_s), u"filters:\n  and:\n    - file.inFolder(\"Biblioteka\")\n    "
                                                          u"- file.ext == \"md\"\nviews:\n  - name: Książki\n"
                                                          "    order:\n      - file.name\n      - Autor\n      - Rok\n "
                                                          "     - Cena\n      - Przeczytana\n      - Gatunki\n"
                                                          "      - Data zakupu\n    type: table\n"_s);
        QVERIFY(r.warnings.isEmpty());
    }

    void chosenMapping()
    {
        ImportOptions o = csvOptions();
        o.csv.titleTemplate = u"{{Autor}} – {{tytuł}}"_s;
        o.csv.folderTemplate = u"{{Rok}}"_s;
        o.csv.columns.insert(u"Rok"_s, CsvColumnOptions{u"Rok wydania"_s, CsvColumnType::Text});
        o.csv.columns.insert(u"Cena"_s, CsvColumnOptions{{}, CsvColumnType::Skip});
        o.csv.columns.insert(u"Gatunki"_s, CsvColumnOptions{{}, CsvColumnType::Tags});
        o.csv.base = false;
        const ImportResult r = importAs(u"csv"_s, {iotest::text(u"Książki.csv"_s, Books)}, o);
        QCOMPARE(textAt(r, u"Import/CSV/1834/Adam Mickiewicz – Pan Tadeusz.md"_s),
                 u"---\nTytuł: Pan Tadeusz\nAutor: Adam Mickiewicz\nRok wydania: \"1834\"\nPrzeczytana: true\n"
                 "Data zakupu: 2026-03-15\ntags:\n  - epopeja\n  - klasyka\n---\n"_s);
        QVERIFY(!iotest::fileAt(r, u"Import/CSV/Książki.base"_s));
    }

    void titlesThatNeedCare()
    {
        const QString csv = u"Nazwa,Ilość\nZakupy: dom/ogród?,1\nZakupy: dom/ogród?,2\n,3\nCON,4\n"_s;
        const ImportResult r = importAs(u"csv"_s, {iotest::text(u"Lista.csv"_s, csv)}, csvOptions());
        const QStringList paths = iotest::pathsOf(r);
        QVERIFY2(paths.contains(u"Import/CSV/Zakupy dom ogród.md"_s), qPrintable(paths.join(u", "_s)));
        QVERIFY(paths.contains(u"Import/CSV/Zakupy dom ogród 2.md"_s));
        QVERIFY(paths.contains(u"Import/CSV/Lista 3.md"_s));
        QVERIFY(paths.contains(u"Import/CSV/CON_.md"_s));
        QVERIFY(textAt(r, u"Import/CSV/Zakupy dom ogród.md"_s).contains(u"aliases:\n  - \"Zakupy: dom/ogród?\""_s));
        QVERIFY(r.warnings.join(u'\n').contains(u"no title"_s));
        QVERIFY(r.warnings.join(u'\n').contains(u"same title"_s));
    }

    void linksBetweenRows()
    {
        const QString people = u"Imię i nazwisko,Zespół\nJan Kowalski,Backend\nAnna Nowak,Frontend\n"_s;
        const QString projects = u"Projekt,Osoby,Status\nPortal,\"Jan Kowalski, Anna Nowak\",w toku\n"
                                 "Sklep,Anna Nowak,gotowe\n"_s;
        const ImportResult r =
                importAs(u"csv"_s, {iotest::text(u"Osoby.csv"_s, people), iotest::text(u"Projekty.csv"_s, projects)},
                         csvOptions());
        QCOMPARE(textAt(r, u"Import/CSV/Projekty/Portal.md"_s),
                 u"---\nOsoby:\n  - \"[[Jan Kowalski]]\"\n  - \"[[Anna Nowak]]\"\nStatus: w toku\n---\n"_s);
        QVERIFY(iotest::fileAt(r, u"Import/CSV/Osoby/Jan Kowalski.md"_s));
        QVERIFY(iotest::fileAt(r, u"Import/CSV/Projekty/Projekty.base"_s));
    }

    void tablePage()
    {
        ImportOptions o = csvOptions();
        o.csv.mode = CsvImportOptions::Mode::Table;
        const QString csv = u"Nazwa;Wzór;Opis\nA;\"=SUMA(1;2)\";x | y\nB;3;\"dwie\nlinie\"\n"_s;
        const ImportResult r = importAs(u"csv"_s, {iotest::text(u"Arkusz.csv"_s, csv)}, o);
        QCOMPARE(iotest::pathsOf(r), QStringList{u"Import/CSV/Arkusz.md"_s});
        QCOMPARE(textAt(r, u"Import/CSV/Arkusz.md"_s),
                 u"| Nazwa | Wzór        | Opis          |\n| ----- | ----------- | ------------- |\n"
                 "| A     | \\=SUMA(1;2) | x \\| y        |\n| B     | 3           | dwie<br>linie |\n"_s);
    }

    void raggedAndHostileInput_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<int>("files");
        QTest::newRow("empty") << QByteArray() << 0;
        QTest::newRow("header only") << QByteArray("a,b,c\n") << 0;
        QTest::newRow("binary") << QByteArray("\x00\x01\x02\xFF\xFE\x00garbage", 14) << 0;
        QTest::newRow("ragged rows") << QByteArray("a,b\n1\n2,3,4,5\n") << 1;
        QTest::newRow("unclosed quote") << QByteArray("a,b\n\"1,2\n3,4\n") << 1;
        QTest::newRow("blank lines and separators") << QByteArray("a,b\n\n1,2\n,,\n;;\n") << 1;
    }
    void raggedAndHostileInput()
    {
        QFETCH(QByteArray, data);
        QFETCH(int, files);
        const CsvAnalysis a = analyzeCsv({iotest::bytes(u"x.csv"_s, data)}, {});
        QCOMPARE(a.files.size(), files);
        const ImportResult r = importAs(u"csv"_s, {iotest::bytes(u"x.csv"_s, data)}, csvOptions());
        for (const ImportedFile &f : r.files)
            QVERIFY(f.path.startsWith(u"Import/CSV/"_s));
    }

    void bounds()
    {
        QString csv = u"Nazwa,Tekst\n"_s;
        csv += u"a,"_s + QString(MaxCsvImportCellLength + 500, u'x') + u'\n';
        for (int i = 0; i < 300; ++i)
            csv += u"b%1,"_s.arg(i);
        csv += u'\n';
        const CsvAnalysis a = analyzeCsv({iotest::text(u"duzy.csv"_s, csv)}, {});
        QCOMPARE(a.files.size(), 1);
        QVERIFY(a.files[0].truncated);
        QVERIFY(a.files[0].columns.size() <= MaxCsvImportColumns);
        QVERIFY(a.warnings.join(u'\n').contains(u"larger than Brulion imports"_s));
    }

    // ---- Airtable --------------------------------------------------------------------------------

    void airtableCsv()
    {
        const QString csv =
                u"Nazwa,Zdjęcia,Gotowe,Autor,Notatki\n"
                "Kampania jesienna,\"plakat.png (https://v5.airtableusercontent.com/v3/u/1/abc/plakat.png),"
                "brief (wersja 2).pdf "
                "(https://v5.airtableusercontent.com/v3/u/1/def/brief.pdf)\",checked,Ola,\"Plan:\n- druk\"\n"
                "Kampania zimowa,,,Jan,\n"_s;
        QList<InputFile> files{iotest::text(u"Kampanie-Grid view.csv"_s, csv)};
        QCOMPARE(detectImporter(files)->id(), u"airtable"_s);
        // Not downloaded: the addresses stay in the property.
        const ImportResult plain = importAs(u"airtable"_s, files, csvOptions(u"Import/Airtable"_s));
        QCOMPARE(textAt(plain, u"Import/Airtable/Kampania jesienna.md"_s),
                 u"---\nZdjęcia:\n  - https://v5.airtableusercontent.com/v3/u/1/abc/plakat.png\n"
                 "  - https://v5.airtableusercontent.com/v3/u/1/def/brief.pdf\nGotowe: true\nAutor: Ola\nsource: "
                 "Airtable\n---\n"
                 "Plan:\n- druk\n"_s);
        QCOMPARE(textAt(plain, u"Import/Airtable/Kampania zimowa.md"_s),
                 u"---\nGotowe: false\nAutor: Jan\nsource: Airtable\n---\n"_s);
        QVERIFY(iotest::fileAt(plain, u"Import/Airtable/Kampanie.base"_s));
        const CsvAnalysis a = analyzeCsv(files, {}, CsvFlavor::Airtable);
        QCOMPARE(a.files[0].name, u"Kampanie"_s);
        QCOMPARE(a.files[0].attachments.size(), 2);
        QCOMPARE(a.files[0].attachments[1].name, u"brief (wersja 2).pdf"_s);
        // Downloaded: attachments embedded.
        files.append(iotest::bytes(csvAttachmentInputPath(a.files[0].attachments[0].url, u"plakat.png"_s), "PNG"));
        files.append(iotest::bytes(csvAttachmentInputPath(a.files[0].attachments[1].url, u"brief (wersja 2).pdf"_s),
                                   "%PDF"));
        const ImportResult r = importAs(u"airtable"_s, files, csvOptions(u"Import/Airtable"_s));
        QCOMPARE(textAt(r, u"Import/Airtable/Kampania jesienna.md"_s),
                 u"---\nZdjęcia:\n  - \"[[assets/Airtable/plakat.png]]\"\n  - \"[[assets/Airtable/brief (wersja "
                 u"2).pdf]]\"\n"
                 "Gotowe: true\nAutor: Ola\nsource: Airtable\n---\nPlan:\n- druk\n\n![[assets/Airtable/plakat.png]]\n\n"
                 "![[assets/Airtable/brief (wersja 2).pdf]]\n"_s);
        QCOMPARE(iotest::fileAt(r, u"assets/Airtable/plakat.png"_s)->data, QByteArray("PNG"));
    }

    void airtableApiBundle()
    {
        const auto field = [](const QString &id, const QString &name, const QString &type) {
            return QJsonObject{{u"id"_s, id}, {u"name"_s, name}, {u"type"_s, type}};
        };
        const QJsonObject people{
                {u"id"_s, u"tblP"_s},
                {u"name"_s, u"Osoby"_s},
                {u"primaryFieldId"_s, u"fldN"_s},
                {u"fields"_s, QJsonArray{field(u"fldN"_s, u"Imię"_s, u"singleLineText"_s)}},
                {u"records"_s,
                 QJsonArray{QJsonObject{{u"id"_s, u"recA"_s}, {u"fields"_s, QJsonObject{{u"Imię"_s, u"Żaneta"_s}}}},
                            QJsonObject{{u"id"_s, u"recB"_s}, {u"fields"_s, QJsonObject{{u"Imię"_s, u"Łukasz"_s}}}}}}};
        const QJsonObject tasks{
                {u"id"_s, u"tblT"_s},
                {u"name"_s, u"Zadania"_s},
                {u"primaryFieldId"_s, u"fldT"_s},
                {u"fields"_s,
                 QJsonArray{field(u"fldS"_s, u"Stan"_s, u"singleSelect"_s),
                            field(u"fldT"_s, u"Zadanie"_s, u"singleLineText"_s),
                            field(u"fldO"_s, u"Osoby"_s, u"multipleRecordLinks"_s),
                            field(u"fldD"_s, u"Termin"_s, u"date"_s),
                            field(u"fldG"_s, u"Etykiety"_s, u"multipleSelects"_s),
                            field(u"fldC"_s, u"Zrobione"_s, u"checkbox"_s), field(u"fldK"_s, u"Koszt"_s, u"currency"_s),
                            field(u"fldB"_s, u"Przycisk"_s, u"button"_s)}},
                {u"records"_s,
                 QJsonArray{QJsonObject{
                         {u"id"_s, u"rec1"_s},
                         {u"fields"_s, QJsonObject{{u"Zadanie"_s, u"Wysłać fakturę"_s},
                                                   {u"Stan"_s, u"w toku"_s},
                                                   {u"Osoby"_s, QJsonArray{u"recA"_s, u"recB"_s}},
                                                   {u"Termin"_s, u"2026-11-25"_s},
                                                   {u"Etykiety"_s, QJsonArray{u"pilne, ważne"_s, u"finanse"_s}},
                                                   {u"Zrobione"_s, true},
                                                   {u"Koszt"_s, 1250.5},
                                                   {u"Przycisk"_s, QJsonObject{{u"label"_s, u"Otwórz"_s}}}}}}}}};
        const QJsonObject bundle{{u"brulion"_s, u"airtable-export"_s},
                                 {u"version"_s, 1},
                                 {u"bases"_s, QJsonArray{QJsonObject{{u"id"_s, u"appX"_s},
                                                                     {u"name"_s, u"Biuro"_s},
                                                                     {u"tables"_s, QJsonArray{people, tasks}}}}}};
        const QList<InputFile> files{iotest::bytes(u"airtable.json"_s, QJsonDocument(bundle).toJson())};
        QCOMPARE(detectImporter(files)->id(), u"airtable"_s);
        const ImportResult r = importAs(u"airtable"_s, files, csvOptions(u"Import/Airtable"_s));
        QCOMPARE(textAt(r, u"Import/Airtable/Zadania/Wysłać fakturę.md"_s),
                 u"---\nStan: w toku\nOsoby:\n  - \"[[Żaneta]]\"\n  - \"[[Łukasz]]\"\nTermin: 2026-11-25\nEtykiety:\n"
                 "  - pilne, ważne\n  - finanse\nZrobione: true\nKoszt: 1250.5\nsource: Airtable\n---\n"_s);
        QVERIFY(iotest::fileAt(r, u"Import/Airtable/Osoby/Żaneta.md"_s));
        QVERIFY(iotest::fileAt(r, u"Import/Airtable/Zadania/Zadania.base"_s));
    }

    // ---- Todoist ---------------------------------------------------------------------------------

    void todoistTemplate()
    {
        const QString csv =
                u"TYPE,CONTENT,DESCRIPTION,PRIORITY,INDENT,AUTHOR,RESPONSIBLE,DATE,DATE_LANG,TIMEZONE,DURATION,"
                u"DURATION_UNIT,DEADLINE,"
                "DEADLINE_LANG\n"
                "note,Plan remontu mieszkania,,,,,,,,,,,,\n"
                "task,Kupić farbę @zakupy @remont,Biała matowa,1,1,Jan (123),,jutro o 9,pl,Europe/Warsaw,,,,\n"
                "task,Sprawdzić odcień,,4,2,Jan (123),,,,,,,,\n"
                "note,Wzornik w szufladzie,,,,,,,,,,,,\n"
                "section,Łazienka,,,,,,,,,,,,\n"
                "task,Wymienić silikon,,2,1,Jan (123),,every 2 weeks on "
                "saturday,en,Europe/Warsaw,30,minute,2026-12-01,en\n"
                "task,Podlać kwiaty #1,,3,1,Jan (123),,co tydzień,pl,Europe/Warsaw,,,,\n"
                "meta,view_style=list,,,,,,,,,,,,\n"_s;
        const QList<InputFile> files{iotest::text(u"Remont [2203306141].csv"_s, csv)};
        QCOMPARE(detectImporter(files)->id(), u"todoist"_s);
        const ImportResult r = importAs(u"todoist"_s, files, csvOptions(u"Import/Todoist"_s));
        COMPARE_TEXT(textAt(r, u"Import/Todoist/Remont.md"_s),
                     u"---\nsource: Todoist\n---\nPlan remontu mieszkania\n\n"
                     "- [ ] Kupić farbę #zakupy #remont 🔺 📅 2026-11-21 09:00\n  Biała matowa\n"
                     "  - [ ] Sprawdzić odcień\n    - Wzornik w szufladzie\n\n## Łazienka\n\n"
                     "- [ ] Wymienić silikon ⏫ ⏳ 2026-11-21 📅 2026-12-01 🔁 every 2 weeks on Saturday\n"
                     "- [ ] Podlać kwiaty \\#1 🔼 📅 2026-11-20 🔁 every week\n"_s);
        QVERIFY(r.warnings.join(u'\n').contains(u"durations"_s));
    }

    void todoistSyncJson()
    {
        const QJsonObject sync{
                {u"full_sync"_s, true},
                {u"sync_token"_s, u"abc"_s},
                {u"projects"_s,
                 QJsonArray{QJsonObject{{u"id"_s, u"p1"_s}, {u"name"_s, u"Praca"_s}, {u"child_order"_s, 1}},
                            QJsonObject{{u"id"_s, u"p2"_s}, {u"name"_s, u"Klienci"_s}, {u"parent_id"_s, u"p1"_s}},
                            QJsonObject{{u"id"_s, u"p3"_s}, {u"name"_s, u"Stare"_s}, {u"is_deleted"_s, true}}}},
                {u"sections"_s, QJsonArray{QJsonObject{{u"id"_s, u"s1"_s},
                                                       {u"name"_s, u"Na później"_s},
                                                       {u"project_id"_s, u"p1"_s},
                                                       {u"section_order"_s, 1}}}},
                {u"items"_s, QJsonArray{QJsonObject{{u"id"_s, u"i1"_s},
                                                    {u"project_id"_s, u"p1"_s},
                                                    {u"content"_s, u"Raport kwartalny"_s},
                                                    {u"priority"_s, 4},
                                                    {u"child_order"_s, 2},
                                                    {u"labels"_s, QJsonArray{u"biuro"_s}},
                                                    {u"due"_s, QJsonObject{{u"date"_s, u"2026-11-24T08:00:00Z"_s},
                                                                           {u"timezone"_s, u"Europe/Warsaw"_s},
                                                                           {u"string"_s, u"24 lis 9:00"_s},
                                                                           {u"is_recurring"_s, false}}}},
                                        QJsonObject{{u"id"_s, u"i2"_s},
                                                    {u"project_id"_s, u"p1"_s},
                                                    {u"content"_s, u"Stand-up"_s},
                                                    {u"priority"_s, 1},
                                                    {u"child_order"_s, 1},
                                                    {u"due"_s, QJsonObject{{u"date"_s, u"2026-11-23T09:30:00"_s},
                                                                           {u"string"_s, u"every weekday at 9:30"_s},
                                                                           {u"is_recurring"_s, true}}}},
                                        QJsonObject{{u"id"_s, u"i3"_s},
                                                    {u"project_id"_s, u"p1"_s},
                                                    {u"parent_id"_s, u"i1"_s},
                                                    {u"content"_s, u"Zebrać dane"_s},
                                                    {u"checked"_s, true},
                                                    {u"completed_at"_s, u"2026-11-19T22:30:00.000000Z"_s}},
                                        QJsonObject{{u"id"_s, u"i4"_s},
                                                    {u"project_id"_s, u"p1"_s},
                                                    {u"section_id"_s, u"s1"_s},
                                                    {u"content"_s, u"Przegląd umów"_s},
                                                    {u"description"_s, u"Zakres:\n\nwszystkie z 2026"_s},
                                                    {u"deadline"_s, QJsonObject{{u"date"_s, u"2026-12-31"_s}}}},
                                        QJsonObject{{u"id"_s, u"i5"_s},
                                                    {u"project_id"_s, u"p2"_s},
                                                    {u"content"_s, u"Oferta dla ACME"_s}}}},
                {u"notes"_s, QJsonArray{QJsonObject{
                                     {u"id"_s, u"n1"_s},
                                     {u"item_id"_s, u"i1"_s},
                                     {u"content"_s, u"Szablon w dziale"_s},
                                     {u"file_attachment"_s,
                                      QJsonObject{{u"file_name"_s, u"szablon.xlsx"_s},
                                                  {u"file_url"_s, u"https://files.todoist.com/abc/szablon.xlsx"_s}}}}}},
                {u"reminders"_s, QJsonArray{QJsonObject{{u"id"_s, u"r1"_s},
                                                        {u"item_id"_s, u"i1"_s},
                                                        {u"type"_s, u"relative"_s},
                                                        {u"minute_offset"_s, 30}}}}};
        QJsonObject marked = sync;
        marked.insert(u"brulion"_s, u"todoist-export"_s);
        QList<InputFile> files{iotest::bytes(u"todoist.json"_s, QJsonDocument(marked).toJson()),
                               iotest::bytes(csvAttachmentInputPath(u"https://files.todoist.com/abc/szablon.xlsx"_s,
                                                                    u"szablon.xlsx"_s),
                                             "XLSX")};
        QCOMPARE(detectImporter(files)->id(), u"todoist"_s);
        const ImportResult r = importAs(u"todoist"_s, files, csvOptions(u"Import/Todoist"_s));
        COMPARE_TEXT(textAt(r, u"Import/Todoist/Praca.md"_s),
                     u"---\nsource: Todoist\n---\n"
                     "- [ ] Stand-up 📅 2026-11-23 09:30 🔁 every weekday\n"
                     "- [ ] Raport kwartalny #biuro 🔺 📅 2026-11-24 09:00 ⏰ 2026-11-24 08:30\n"
                     "  - Szablon w dziale [[assets/Todoist/szablon.xlsx|szablon.xlsx]]\n"
                     "  - [x] Zebrać dane ✅ 2026-11-19\n\n"
                     "## Na później\n\n- [ ] Przegląd umów 📅 2026-12-31\n  Zakres:\n\n  wszystkie z 2026\n"_s);
        QCOMPARE(textAt(r, u"Import/Todoist/Praca/Klienci.md"_s),
                 u"---\nsource: Todoist\n---\n- [ ] Oferta dla ACME\n"_s);
        QVERIFY(!iotest::fileAt(r, u"Import/Todoist/Stare.md"_s));
        QCOMPARE(iotest::fileAt(r, u"assets/Todoist/szablon.xlsx"_s)->data, QByteArray("XLSX"));
    }

    // ---- Detection -------------------------------------------------------------------------------

    void detection_data()
    {
        QTest::addColumn<QList<InputFile>>("files");
        QTest::addColumn<QString>("id");
        QTest::newRow("plain csv") << QList<InputFile>{iotest::text(u"a.csv"_s, Books)} << u"csv"_s;
        QTest::newRow("tsv") << QList<InputFile>{iotest::text(u"a.tsv"_s, u"a\tb\n1\t2\n"_s)} << u"csv"_s;
        QTest::newRow("todoist") << QList<InputFile>{iotest::text(u"Dom.csv"_s,
                                                                  u"TYPE,CONTENT,PRIORITY,INDENT\ntask,x,4,1\n"_s)}
                                 << u"todoist"_s;
        QTest::newRow("airtable view name")
                << QList<InputFile>{iotest::text(u"Zadania-Grid view.csv"_s, u"Nazwa\nx\n"_s)} << u"airtable"_s;
        QTest::newRow("notion database") << QList<InputFile>{
                iotest::text(u"Zadania 0123456789abcdef0123456789abcdef.csv"_s, Books),
                iotest::text(u"Zadania 0123456789abcdef0123456789abcdef.md"_s,
                             u"# x"_s)} << u"notion"_s;
        QTest::newRow("csv in a vault") << QList<InputFile>{iotest::text(u"dane.csv"_s, Books),
                                                            iotest::text(u"a.md"_s, u"x"_s),
                                                            iotest::text(u"b.md"_s, u"y"_s)}
                                        << u"markdown"_s;
    }
    void detection()
    {
        QFETCH(QList<InputFile>, files);
        QFETCH(QString, id);
        const Importer *importer = detectImporter(files);
        QVERIFY(importer);
        QCOMPARE(importer->id(), id);
    }
};

QTEST_MAIN(TestIoImportCsv)
#include "tst_io_import_csv.moc"
