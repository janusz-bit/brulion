// Importers of other apps' exports: Notion, Evernote, Google Keep, Joplin, Simplenote, Roam.
#include "iotest.h"

#include "io/importer.h"
#include "io/zip.h"
#include "markdown/frontmatter.h"

#include <QCryptographicHash>
#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;
using iotest::bytes;
using iotest::text;

namespace {

QString bodyOf(const QString &content)
{
    return content.sliced(markdown::parseFrontmatter(content).bodyStart);
}

QVariantMap frontmatterOf(const QString &content)
{
    return markdown::parseFrontmatter(content).data;
}

ImportResult importWith(const QString &id, const QList<InputFile> &files, const ImportOptions &o = iotest::options())
{
    return runImport(*importerById(id), files, o);
}

/// JSON written with single quotes (no raw strings: moc cannot parse them).
QByteArray json(const QString &singleQuoted)
{
    return QString(singleQuoted).replace(u'\'', u'"').toUtf8();
}

QString md5(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

// Notion ids used in the fixtures.
const QString ProjId = u"0123456789abcdef0123456789abcdef"_s;
const QString DomId = u"11112222333344445555666677778888"_s;
const QString DbId = u"fedcba9876543210fedcba9876543210"_s;
const QString RowId = u"aaaabbbbccccddddeeeeffff00001111"_s;

QByteArray notionExport()
{
    const QString proj = u"Projekty "_s + ProjId;
    const QString enc = u"Projekty%20"_s + ProjId;
    const QByteArray part = writeZip({
            {proj + u".md"_s, (u"# Projekty\n\nLista: [Zadania]("_s + enc + u"/Zadania%20"_s + DbId
                               + u".csv)\n\n[Dom]("_s + enc + u"/Dom%20"_s + DomId + u".md)\n\n![szkic]("_s + enc
                               + u"/szkic.png)\n\nLink: [Dom online](https://www.notion.so/Dom-"_s + DomId + u")\n"_s)
                                      .toUtf8()},
            {proj + u"/Dom "_s + DomId + u".md"_s, "# Dom\n\nOpis domu ąę\n"},
            {proj + u"/szkic.png"_s, "PNG"},
            {proj + u"/Zadania "_s + DbId + u".csv"_s, "Name,Status\nKup farbę,Done\n"},
            {proj + u"/Zadania "_s + DbId + u"_all.csv"_s,
             "Name,Status,Tags\nKup farbę,Done,\"dom, remont\"\nPomaluj|ściany,Todo,\n"},
            {proj + u"/Zadania "_s + DbId + u"/Kup farbę "_s + RowId + u".md"_s,
             "# Kup farbę\n\nStatus: Done\nTags: dom, remont\n\nTreść zadania\n"},
    });
    return writeZip({{u"Export-1234-Part-1.zip"_s, part}});
}

QByteArray enex()
{
    const QString h1 = md5("PNGDATA");
    const QString h2 = md5("%PDF-1.4");
    const QString xml =
            u"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE en-export SYSTEM \"http://xml.evernote.com/pub/evernote-export4.dtd\">\n"
            "<en-export export-date=\"20261014T101010Z\" application=\"Evernote\" version=\"10.0\">\n"
            "<note><title>Zakupy na weekend</title><created>20261014T080000Z</created>"
            "<updated>20261015T093000Z</updated><tag>dom</tag><tag>lista zakupów</tag>"
            "<note-attributes><author>Jan</author><source-url>https://example.com/przepis</source-url>"
            "<reminder-time>20261016T070000Z</reminder-time></note-attributes>"
            "<content><![CDATA[<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
            "<!DOCTYPE en-note SYSTEM \"http://xml.evernote.com/pub/enml2.dtd\">\n"
            "<en-note><div>Lista <b>ważna</b>:</div><div><en-todo checked=\"true\"/>mleko</div>"
            "<div><en-todo/>chleb</div><div><en-media hash=\"HASH1\" type=\"image/png\"/></div>"
            "<div><en-media hash=\"HASH2\" type=\"application/pdf\"></en-media></div>"
            "<div style=\"--en-codeblock:true;\"><div>int x = 1;</div><div>&lt;b&gt;</div></div>"
            "<en-crypt cipher=\"AES\">c2VjcmV0</en-crypt>"
            "<div><a href=\"evernote:///view/1/s1/abc/abc/\">Inna notatka</a></div></en-note>]]></content>"
            "<resource><data encoding=\"base64\">UE5HREFU\nQQ==</data><mime>image/png</mime>"
            "<resource-attributes><file-name>zdjęcie.png</file-name></resource-attributes></resource>"
            "<resource><data encoding=\"base64\">JVBERi0xLjQ=</data><mime>application/pdf</mime></resource>"
            "<resource><data encoding=\"base64\">dW51c2Vk</data><mime>text/plain</mime>"
            "<resource-attributes><file-name>notatki.txt</file-name></resource-attributes></resource>"
            "</note>\n"
            "<note><title>Inna notatka</title><content><![CDATA[<en-note>Treść</en-note>]]></content>"
            "<task><title>Zadzwoń</title><taskStatus>open</taskStatus><dueDate>20261020T100000Z</dueDate></task>"
            "<task><title>Zrobione</title><taskStatus>completed</taskStatus></task></note>\n"
            "</en-export>\n"_s;
    return QString(xml).replace(u"HASH1"_s, h1).replace(u"HASH2"_s, h2).toUtf8();
}

QString joplinItem(const QString &title, const QString &body, const QStringList &meta)
{
    QString out;
    if (!title.isEmpty())
        out += title + u"\n\n"_s;
    if (!body.isEmpty())
        out += body + u"\n\n"_s;
    return out + meta.join(u'\n');
}

QByteArray jex()
{
    const auto member = [](const QString &name, const QString &content) {
        return iotest::TarMember{name.toUtf8(), content.toUtf8(), '0', {}};
    };
    const QString f1 = u"f0000000000000000000000000000001"_s;
    const QString f2 = u"f0000000000000000000000000000002"_s;
    const QString n1 = u"a0000000000000000000000000000001"_s;
    const QString n2 = u"a0000000000000000000000000000002"_s;
    const QString r1 = u"b0000000000000000000000000000001"_s;
    const QString r2 = u"b0000000000000000000000000000002"_s;
    const QString t1 = u"c0000000000000000000000000000001"_s;
    const QString nt = u"d0000000000000000000000000000001"_s;
    return iotest::tarArchive({
            member(f1 + u".md"_s, joplinItem(u"Praca"_s, {}, {u"id: "_s + f1, u"parent_id: "_s, u"type_: 2"_s})),
            member(f2 + u".md"_s,
                   joplinItem(u"Projekty"_s, {}, {u"id: "_s + f2, u"parent_id: "_s + f1, u"type_: 2"_s})),
            member(n1 + u".md"_s,
                   joplinItem(u"Spotkanie"_s,
                              u"Notatka ze spotkania ąę.\n\n![wykres.png](:/"_s + r1 + u")\n\nZobacz [Plan](:/"_s + n2
                                      + u") i [plik](:/"_s + r2 + u").\n\n<img src=\":/"_s + r1
                                      + u"\" width=\"100\"/>"_s,
                              {u"id: "_s + n1, u"parent_id: "_s + f2, u"created_time: 2026-10-14T08:00:00.000Z"_s,
                               u"updated_time: 2026-10-14T09:00:00.000Z"_s, u"is_todo: 0"_s,
                               u"source_url: https://example.com"_s, u"type_: 1"_s})),
            member(n2 + u".md"_s, joplinItem(u"Plan"_s, u"Kroki."_s,
                                             {u"id: "_s + n2, u"parent_id: "_s + f1, u"is_todo: 1"_s,
                                              u"todo_due: 1792134000000"_s, u"todo_completed: 0"_s, u"type_: 1"_s})),
            member(r1 + u".md"_s, joplinItem(u"wykres.png"_s, {},
                                             {u"id: "_s + r1, u"mime: image/png"_s, u"filename: "_s,
                                              u"file_extension: png"_s, u"type_: 4"_s})),
            member(u"resources/"_s + r1 + u".png"_s, u"PNG"_s),
            member(r2 + u".md"_s, joplinItem(u"raport.pdf"_s, {},
                                             {u"id: "_s + r2, u"mime: application/pdf"_s, u"filename: raport.pdf"_s,
                                              u"file_extension: pdf"_s, u"type_: 4"_s})),
            member(u"resources/"_s + r2 + u".pdf"_s, u"%PDF"_s),
            member(t1 + u".md"_s, joplinItem(u"ważne"_s, {}, {u"id: "_s + t1, u"type_: 5"_s})),
            member(nt + u".md"_s,
                   joplinItem({}, {}, {u"id: "_s + nt, u"note_id: "_s + n1, u"tag_id: "_s + t1, u"type_: 6"_s})),
    });
}

} // namespace

class TestIoImportApps : public QObject {
    Q_OBJECT

public:
    /// A system zone with DST, so tests catch timestamps read through the system zone (the
    /// importers convert to ImportOptions::timeZone only).
    static void initMain() { qputenv("TZ", "Europe/Warsaw"); }

private Q_SLOTS:
    // --- Notion -----------------------------------------------------------------------------------

    void notion()
    {
        const ImportResult r =
                importWith(u"notion"_s, {bytes(u"Export-1234.zip"_s, notionExport())}, iotest::options(u"Notion"_s));
        QCOMPARE(iotest::pathsOf(r),
                 QStringList({u"Notion/Projekty.md"_s, u"Notion/Projekty/Dom.md"_s, u"Notion/Projekty/Zadania.md"_s,
                              u"Notion/Projekty/Zadania/Kup farbę.md"_s, u"assets/Notion/Projekty/szkic.png"_s}));
        QCOMPARE(iotest::textAt(r, u"Notion/Projekty.md"_s),
                 u"Lista: [[Zadania]]\n\n[[Dom]]\n\n![szkic](../assets/Notion/Projekty/szkic.png)\n\n"
                 "Link: [[Dom|Dom online]]\n"_s);
        QCOMPARE(iotest::textAt(r, u"Notion/Projekty/Dom.md"_s), u"Opis domu ąę\n"_s);
        const QString row = iotest::textAt(r, u"Notion/Projekty/Zadania/Kup farbę.md"_s);
        QCOMPARE(frontmatterOf(row).value(u"Status"_s).toString(), u"Done"_s);
        QCOMPARE(frontmatterOf(row).value(u"tags"_s).toStringList(), QStringList({u"dom"_s, u"remont"_s}));
        QCOMPARE(bodyOf(row), u"Treść zadania\n"_s);
        QCOMPARE(iotest::textAt(r, u"Notion/Projekty/Zadania.md"_s),
                 u"| Name | Status | Tags |\n| --- | --- | --- |\n| [[Kup farbę]] | Done | dom, remont |\n"
                 "| Pomaluj\\|ściany | Todo |  |\n"_s);
        QVERIFY(r.warnings.isEmpty());
    }

    void notionDuplicateTitles()
    {
        const QByteArray zip = writeZip({
                {u"Untitled 11111111111111111111111111111111.md"_s, "a"},
                {u"Untitled 22222222222222222222222222222222.md"_s, "b"},
                {u"Indeks 33333333333333333333333333333333.md"_s,
                 "[x](Untitled%2022222222222222222222222222222222.md)"},
        });
        const ImportResult r = importWith(u"notion"_s, {bytes(u"n.zip"_s, zip)});
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Indeks.md"_s, u"Untitled 2.md"_s, u"Untitled.md"_s}));
        QCOMPARE(iotest::textAt(r, u"Indeks.md"_s), u"[[Untitled 2|x]]\n"_s);
    }

    void notionPlainPageKeepsColonLines()
    {
        const QByteArray zip = writeZip(
                {{u"Notatka 44444444444444444444444444444444.md"_s, "# Notatka\n\nUwaga: ważne\nKolor: zielony\n"}});
        const ImportResult r = importWith(u"notion"_s, {bytes(u"n.zip"_s, zip)});
        QCOMPARE(iotest::textAt(r, u"Notatka.md"_s), u"Uwaga: ważne\nKolor: zielony\n"_s);
    }

    // --- Evernote ---------------------------------------------------------------------------------

    void evernote()
    {
        const ImportResult r = importWith(u"evernote"_s, {bytes(u"Moje notatki.enex"_s, enex())});
        const QString pdf = u"assets/Evernote/attachment-"_s + md5("%PDF-1.4").left(8) + u".pdf"_s;
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Inna notatka.md"_s, u"Zakupy na weekend.md"_s, pdf,
                                                  u"assets/Evernote/notatki.txt"_s, u"assets/Evernote/zdjęcie.png"_s}));
        const QString note = iotest::textAt(r, u"Zakupy na weekend.md"_s);
        const QVariantMap fm = frontmatterOf(note);
        QCOMPARE(fm.value(u"created"_s).toString(), u"2026-10-14 10:00"_s);
        QCOMPARE(fm.value(u"updated"_s).toString(), u"2026-10-15 11:30"_s);
        QCOMPARE(fm.value(u"tags"_s).toStringList(), QStringList({u"dom"_s, u"lista-zakupów"_s}));
        QCOMPARE(fm.value(u"source"_s).toString(), u"https://example.com/przepis"_s);
        QCOMPARE(fm.value(u"author"_s).toString(), u"Jan"_s);
        QCOMPARE(bodyOf(note), u"- [ ] Zakupy na weekend ⏰ 2026-10-16 09:00\n\nLista **ważna**:\n\n- [x] mleko\n"
                               "- [ ] chleb\n\n![[assets/Evernote/zdjęcie.png]]\n![["_s
                                       + pdf
                                       + u"]]\n\n```\nint x = 1;\n<b>\n```\n\n[[Inna notatka]]\n\n"
                                         "[[assets/Evernote/notatki.txt|notatki.txt]]\n"_s);
        QCOMPARE(iotest::fileAt(r, u"assets/Evernote/zdjęcie.png"_s)->data, QByteArray("PNGDATA"));
        QCOMPARE(iotest::textAt(r, u"Inna notatka.md"_s),
                 u"Treść\n\n- [ ] Zadzwoń 📅 2026-10-20 12:00\n- [x] Zrobione\n"_s);
        QCOMPARE(r.warnings.size(), 1); // encrypted part
    }

    void evernoteMalformed()
    {
        QByteArray truncated = enex();
        truncated.truncate(truncated.indexOf("<note><title>Inna"));
        ImportResult r = importWith(u"evernote"_s, {bytes(u"a.enex"_s, truncated)});
        QCOMPARE(iotest::pathsOf(r).filter(u".md"_s), QStringList({u"Zakupy na weekend.md"_s}));
        QCOMPARE(r.warnings.size(), 2); // incomplete file, encrypted part
        r = importWith(u"evernote"_s, {bytes(u"b.enex"_s, "<en-export><note><title>A</title><x></note>")});
        QCOMPARE(r.warnings.size(), 1);
        r = importWith(u"evernote"_s, {bytes(u"c.enex"_s, "not xml at all <<< &&")});
        QCOMPARE(r.files.size(), 0);
        QCOMPARE(r.warnings.size(), 1);
    }

    void evernoteUtcTimesInLocalDstGap()
    {
        // 02:30 on 2026-03-29 does not exist as a Warsaw wall-clock time (spring forward); as UTC
        // it is 04:30 CEST. Parsing the digits as a system-local time first shifts it by an hour.
        const QByteArray note = "<en-export><note><title>Zmiana czasu</title><created>20260329T023000Z</created>"
                                "<updated>20261025T023000Z</updated>"
                                "<content><![CDATA[<en-note>x</en-note>]]></content></note></en-export>";
        ImportOptions o = iotest::options();
        const ImportResult r = importWith(u"evernote"_s, {bytes(u"a.enex"_s, note)}, o);
        const QVariantMap fm = frontmatterOf(iotest::textAt(r, u"Zmiana czasu.md"_s));
        QCOMPARE(fm.value(u"created"_s).toString(), u"2026-03-29 04:30"_s);
        QCOMPARE(fm.value(u"updated"_s).toString(), u"2026-10-25 03:30"_s);
    }

    void sanitisedTitlesBecomeAliases()
    {
        // A title that cannot be a file name ("/", ":", "?") is kept as an alias.
        const QByteArray note = "<en-export><note><title>Plan: A/B?</title><content><![CDATA[<en-note>x</en-note>]]>"
                                "</content></note><note><title>Zwykły</title><content><![CDATA[<en-note>y</en-note>]]>"
                                "</content></note></en-export>";
        const ImportResult r = importWith(u"evernote"_s, {bytes(u"a.enex"_s, note)});
        QCOMPARE(frontmatterOf(iotest::textAt(r, u"Plan A B.md"_s)).value(u"aliases"_s).toStringList(),
                 QStringList({u"Plan: A/B?"_s}));
        QVERIFY(!frontmatterOf(iotest::textAt(r, u"Zwykły.md"_s)).contains(u"aliases"_s));
        const ImportResult k = importWith(
                u"keep"_s,
                {bytes(u"Keep/a.json"_s,
                       json(u"{'title':'Co? Gdzie?','textContent':'x','isTrashed':false,'userEditedTimestampUsec':1}"_s))});
        QCOMPARE(frontmatterOf(iotest::textAt(k, u"Co Gdzie.md"_s)).value(u"aliases"_s).toStringList(),
                 QStringList({u"Co? Gdzie?"_s}));
    }

    void evernoteNotebooks()
    {
        const QByteArray note = "<en-export><note><title>Notatka</title><content><![CDATA[<en-note>x</en-note>]]>"
                                "</content></note></en-export>";
        const ImportResult r = importWith(u"evernote"_s, {bytes(u"Praca.enex"_s, note), bytes(u"Dom.enex"_s, note)});
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Dom/Notatka.md"_s, u"Praca/Notatka.md"_s}));
    }

    // --- Google Keep ------------------------------------------------------------------------------

    void keep()
    {
        const QList<InputFile> files = {
                bytes(u"Takeout/Keep/Zakupy.json"_s,
                      json(u"{'color':'YELLOW','isTrashed':false,'isPinned':true,'isArchived':false,'title':'Zakupy',"
                           "                          "
                           "'userEditedTimestampUsec':1792060200000000,'createdTimestampUsec':1791964800000000,"
                           "                          "
                           "'listContent':[{'text':'mleko','isChecked':true},{'text':'chleb\\nrazowy','isChecked':"
                           "false}],"
                           "                          'labels':[{'name':'Dom'},{'name':'Moje zakupy'}],"
                           "                          "
                           "'attachments':[{'filePath':'zdjecie.jpg','mimetype':'image/jpeg'}],"
                           "                          'annotations':[{'description':'','source':'WEBLINK','title':'Przepis','url':'https://example.com/p'}]}"_s)),
                bytes(u"Takeout/Keep/zdjecie.jpeg"_s, "JPEG"),
                bytes(u"Takeout/Keep/bez tytułu.json"_s,
                      json(u"{'textContent':'Pierwsza linia "
                           u"notatki\\nDruga','title':'','isTrashed':false,'isArchived':true,"
                           "                          'color':'DEFAULT','userEditedTimestampUsec':1792060200000000}"_s)),
                bytes(u"Takeout/Keep/kosz.json"_s,
                      json(u"{'textContent':'usunięte','isTrashed':true,'userEditedTimestampUsec':1}"_s)),
                text(u"Takeout/Keep/zly.json"_s, u"{not json"_s),
                text(u"Takeout/Keep/Labels.txt"_s, u"Dom\n"_s),
                bytes(u"Takeout/Keep/brak.json"_s,
                      json(u"{'title':'Brak','textContent':'x','attachments':[{'filePath':'nie-ma.png'}],'isTrashed':false}"_s)),
        };
        const ImportResult r = importWith(u"keep"_s, files, iotest::options(u"Keep"_s));
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Keep/Brak.md"_s, u"Keep/Pierwsza linia notatki.md"_s,
                                                  u"Keep/Zakupy.md"_s, u"assets/Keep/zdjecie.jpeg"_s}));
        const QString zakupy = iotest::textAt(r, u"Keep/Zakupy.md"_s);
        const QVariantMap fm = frontmatterOf(zakupy);
        QCOMPARE(fm.value(u"created"_s).toString(), u"2026-10-14 10:00"_s);
        QCOMPARE(fm.value(u"updated"_s).toString(), u"2026-10-15 12:30"_s);
        QCOMPARE(fm.value(u"tags"_s).toStringList(), QStringList({u"Dom"_s, u"Moje-zakupy"_s}));
        QCOMPARE(fm.value(u"pinned"_s).toBool(), true);
        QCOMPARE(fm.value(u"color"_s).toString(), u"yellow"_s);
        QVERIFY(!fm.contains(u"archived"_s));
        QCOMPARE(bodyOf(zakupy), u"- [x] mleko\n- [ ] chleb\n  razowy\n\n![[assets/Keep/zdjecie.jpeg]]\n\n"
                                 "- [Przepis](https://example.com/p)\n"_s);
        const QString other = iotest::textAt(r, u"Keep/Pierwsza linia notatki.md"_s);
        QCOMPARE(frontmatterOf(other).value(u"archived"_s).toBool(), true);
        QVERIFY(!frontmatterOf(other).contains(u"color"_s));
        QCOMPARE(bodyOf(other), u"Pierwsza linia notatki\nDruga\n"_s);
        QCOMPARE(r.warnings.size(), 2); // invalid JSON, missing attachment
    }

    // --- Joplin -----------------------------------------------------------------------------------

    void joplinJex()
    {
        const ImportResult r = importWith(u"joplin"_s, {bytes(u"eksport.jex"_s, jex())});
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Praca/Plan.md"_s, u"Praca/Projekty/Spotkanie.md"_s,
                                                  u"assets/Joplin/raport.pdf"_s, u"assets/Joplin/wykres.png"_s}));
        const QString note = iotest::textAt(r, u"Praca/Projekty/Spotkanie.md"_s);
        const QVariantMap fm = frontmatterOf(note);
        QCOMPARE(fm.value(u"created"_s).toString(), u"2026-10-14 10:00"_s);
        QCOMPARE(fm.value(u"updated"_s).toString(), u"2026-10-14 11:00"_s);
        QCOMPARE(fm.value(u"tags"_s).toStringList(), QStringList({u"ważne"_s}));
        QCOMPARE(fm.value(u"source"_s).toString(), u"https://example.com"_s);
        QCOMPARE(bodyOf(note), u"Notatka ze spotkania ąę.\n\n![[assets/Joplin/wykres.png]]\n\nZobacz [[Plan]] i "
                               "[[assets/Joplin/raport.pdf|plik]].\n\n![[assets/Joplin/wykres.png]]\n"_s);
        QCOMPARE(iotest::textAt(r, u"Praca/Plan.md"_s), u"- [ ] Plan ⏰ 2026-10-16 09:00\n\nKroki.\n"_s);
        QCOMPARE(iotest::fileAt(r, u"assets/Joplin/wykres.png"_s)->data, QByteArray("PNG"));
    }

    void joplinMarkdownExport()
    {
        const QList<InputFile> files = {
                text(u"export/Praca/Zadanie.md"_s,
                     u"---\ntitle: Zadanie\nupdated: 2026-10-14 08:00:00Z\ncreated: 2026-10-13 08:00:00Z\n"
                     "tags:\n  - praca\nlatitude: 0.00000000\ncompleted?: no\ndue: 2026-10-16 07:00:00Z\n---\n\n"
                     "Opis ![](../_resources/foto.jpg)\n"_s),
                bytes(u"export/_resources/foto.jpg"_s, "JPG"),
        };
        const ImportResult r = importWith(u"joplin"_s, files);
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Praca/Zadanie.md"_s, u"assets/Joplin/foto.jpg"_s}));
        const QString note = iotest::textAt(r, u"Praca/Zadanie.md"_s);
        const QVariantMap fm = frontmatterOf(note);
        QCOMPARE(fm.value(u"updated"_s).toString(), u"2026-10-14 10:00"_s);
        QCOMPARE(fm.value(u"created"_s).toString(), u"2026-10-13 10:00"_s);
        QCOMPARE(fm.value(u"tags"_s).toStringList(), QStringList({u"praca"_s}));
        QVERIFY(!fm.contains(u"title"_s));
        QVERIFY(!fm.contains(u"latitude"_s));
        QCOMPARE(bodyOf(note), u"- [ ] Zadanie ⏰ 2026-10-16 09:00\n\nOpis ![](../assets/Joplin/foto.jpg)\n"_s);
    }

    void joplinTrashSkipped()
    {
        // Joplin 3 keeps deleted notes and notebooks with a deleted_time (the trash).
        const auto member = [](const QString &name, const QString &content) {
            return iotest::TarMember{name.toUtf8(), content.toUtf8(), '0', {}};
        };
        const QString f = u"f0000000000000000000000000000009"_s;
        const QByteArray tar = iotest::tarArchive({
                member(f + u".md"_s,
                       joplinItem(u"Kosz"_s, {}, {u"id: "_s + f, u"deleted_time: 1792134000000"_s, u"type_: 2"_s})),
                member(u"a0000000000000000000000000000007.md"_s,
                       joplinItem(u"W koszu"_s, u"x"_s,
                                  {u"id: a0000000000000000000000000000007"_s, u"deleted_time: 1792134000000"_s,
                                   u"type_: 1"_s})),
                member(u"a0000000000000000000000000000008.md"_s,
                       joplinItem(u"W usuniętym notatniku"_s, u"y"_s,
                                  {u"id: a0000000000000000000000000000008"_s, u"parent_id: "_s + f,
                                   u"deleted_time: 0"_s, u"type_: 1"_s})),
                member(u"a0000000000000000000000000000006.md"_s,
                       joplinItem(u"Zostaje"_s, u"z"_s,
                                  {u"id: a0000000000000000000000000000006"_s, u"deleted_time: 0"_s, u"type_: 1"_s})),
        });
        const ImportResult r = importWith(u"joplin"_s, {bytes(u"a.jex"_s, tar)});
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Zostaje.md"_s}));
    }

    void joplinMalformedItems()
    {
        const ImportResult r = importWith(
                u"joplin"_s,
                {text(u"0123456789abcdef0123456789abcdef.md"_s, u"Tylko tytuł bez metadanych"_s),
                 text(u"a0000000000000000000000000000009.md"_s,
                      u"Sierota\n\nTreść\n\nid: a0000000000000000000000000000009\nparent_id: ffffffffffffffffffffffffffffffff\ntype_: 1"_s)});
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Sierota.md"_s}));
    }

    // --- Simplenote -------------------------------------------------------------------------------

    void simplenote()
    {
        const QByteArray data =
                json(u"{'activeNotes':["
                     "                      {'id':'x1','content':'Lista zadań\\r\\n\\r\\n- mleko\\r\\n- #dom "
                     "tag','creationDate':'2026-10-14T08:00:00.000Z',"
                     "                       "
                     "'lastModified':'2026-10-15T08:00:00.000Z','tags':['dom','praca'],'pinned':true},"
                     "                      {'id':'x2','content':'','creationDate':'2026-10-14T08:00:00.000Z'}],"
                     "                    'trashedNotes':[{'id':'x3','content':'kosz'}]}"_s);
        const ImportResult r = importWith(u"simplenote"_s, {bytes(u"source/notes.json"_s, data),
                                                            text(u"source/notes/Lista zadań.txt"_s, u"ignored"_s)});
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Lista zadań.md"_s, u"Simplenote 2026-10-14 10-00.md"_s}));
        const QString note = iotest::textAt(r, u"Lista zadań.md"_s);
        const QVariantMap fm = frontmatterOf(note);
        QCOMPARE(fm.value(u"created"_s).toString(), u"2026-10-14 10:00"_s);
        QCOMPARE(fm.value(u"updated"_s).toString(), u"2026-10-15 10:00"_s);
        QCOMPARE(fm.value(u"tags"_s).toStringList(), QStringList({u"dom"_s, u"praca"_s}));
        QCOMPARE(fm.value(u"pinned"_s).toBool(), true);
        QCOMPARE(bodyOf(note), u"- mleko\n- #dom tag\n"_s);
        QVERIFY(r.warnings.isEmpty());
        QCOMPARE(importWith(u"simplenote"_s, {text(u"notes.json"_s, u"{broken"_s)}).warnings.size(), 2);
    }

    void simplenoteFirstLineNeverLost()
    {
        // The title is the first line shortened to 80 characters; the line itself must stay in
        // the note when it is longer than the title or is not a plain title line (a task).
        const QString longLine = u"Zażółć gęślą jaźń "_s.repeated(6) + u"koniec pierwszej linii"_s;
        const QByteArray data = json(u"{'activeNotes':[{'id':'a1','content':'"_s + longLine
                                     + u"\\ndruga linia'},{'id':'a2','content':'- [ ] kup mleko\\n- [x] chleb'},"
                                       "{'id':'a3','content':'# Nagłówek\\ntreść'}]}"_s);
        const ImportResult r = importWith(u"simplenote"_s, {bytes(u"notes.json"_s, data)});
        QCOMPARE(r.files.size(), 3);
        bool sawLong = false;
        for (const ImportedFile &f : r.files) {
            const QString body = bodyOf(QString::fromUtf8(f.data));
            if (f.path.startsWith(u"Zażółć"_s)) {
                sawLong = true;
                QCOMPARE(body, longLine + u"\ndruga linia\n"_s);
            }
        }
        QVERIFY(sawLong);
        QCOMPARE(bodyOf(iotest::textAt(r, u"kup mleko.md"_s)), u"- [ ] kup mleko\n- [x] chleb\n"_s);
        QCOMPARE(bodyOf(iotest::textAt(r, u"Nagłówek.md"_s)), u"treść\n"_s);
    }

    void titlesNeverSplitSurrogatePairs()
    {
        // 1 + 60 emoji (2 UTF-16 code units each): cutting at 80 code units would split a pair.
        const QString content = u"a"_s + u"😀"_s.repeated(60);
        const QByteArray data = json(u"{'activeNotes':[{'id':'e1','content':'"_s + content + u"'}]}"_s);
        const ImportResult r = importWith(u"simplenote"_s, {bytes(u"notes.json"_s, data)});
        QCOMPARE(r.files.size(), 1);
        const QString path = r.files.first().path;
        QCOMPARE(QString::fromUtf8(path.toUtf8()), path); // no lone surrogate
        QCOMPARE(path, u"a"_s + u"😀"_s.repeated(39) + u".md"_s);
    }

    void longNamesFitFileSystems()
    {
        // ext4 / F2FS / exFAT allow 255 bytes per file name; Polish letters take 2 bytes in UTF-8,
        // emoji 4. Every segment (with room for a " 2" suffix and the extension) must fit.
        const QString polish = u"Zażółć gęślą jaźń "_s.repeated(12).trimmed();
        const QString emoji = u"😀"_s.repeated(100);
        const QByteArray data = json(u"[{'title':'"_s + polish
                                     + u"','children':[{'string':'a','uid':'u0000001'}]},"
                                       "{'title':'"_s
                                     + emoji + u"/"_s + polish + u"','children':[{'string':'b','uid':'u0000002'}]}]"_s);
        const ImportResult r = importWith(u"roam"_s, {bytes(u"g.json"_s, data)});
        QCOMPARE(r.files.size(), 2);
        for (const ImportedFile &f : r.files) {
            QCOMPARE(QString::fromUtf8(f.path.toUtf8()), f.path);
            for (const QString &segment : f.path.split(u'/')) // name <= 200 bytes, plus ".md"
                QVERIFY2(segment.toUtf8().size() <= 203, qPrintable(segment));
            QVERIFY(f.path.endsWith(u".md"_s));
        }
    }

    // --- Roam -------------------------------------------------------------------------------------

    void roam()
    {
        const QByteArray data =
                json(u"["
                     "          {'title':'Projekty/Dom','create-time':1,'children':["
                     "             {'string':'{{[[TODO]]}} Kup farbę ^^ważne^^','uid':'abc_DEF12','children':["
                     "                 {'string':'__kursywa__ i [[October 14th, 2026]]','uid':'child0001'}]},"
                     "             {'string':'Nagłówek','uid':'head00001','heading':2},"
                     "             {'string':'Kod:\\n```js\\nlet x = __y__;\\n```','uid':'code00001'},"
                     "             {'string':'[alias]([[Inna strona]]) i ((abc_DEF12))','uid':'ref000001'}]},"
                     "          {'title':'October 14th, 2026','uid':'10-14-2026','children':["
                     "             {'string':'{{[[DONE]]}} rano','uid':'day000001'},"
                     "             {'string':'{{embed: ((abc_DEF12))}}','uid':'day000002'}]},"
                     "          {'title':'October 15th, 2026','uid':'10-15-2026','children':[]},"
                     "          {'title':'Pusta'},"
                     "          {'title':'Co? Jak?','children':[{'string':'x','uid':'q00000001'}]}"
                     "        ]"_s);
        const ImportResult r = importWith(u"roam"_s, {bytes(u"roam-export.json"_s, data)}, iotest::options(u"Roam"_s));
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Roam/Co Jak.md"_s, u"Roam/Projekty/Dom.md"_s, u"Roam/Pusta.md"_s,
                                                  u"journals/2026-10-14.md"_s}));
        QCOMPARE(iotest::textAt(r, u"Roam/Projekty/Dom.md"_s),
                 u"- [ ] Kup farbę ==ważne== ^abc-DEF12\n\t- *kursywa* i [[2026-10-14]]\n- ## Nagłówek\n- Kod:\n"
                 "  ```js\n  let x = __y__;\n  ```\n- [[Inna strona|alias]] i ((abc-DEF12))\n"_s);
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-14.md"_s), u"- [x] rano\n- !((abc-DEF12))\n"_s);
        QCOMPARE(frontmatterOf(iotest::textAt(r, u"Roam/Co Jak.md"_s)).value(u"aliases"_s).toStringList(),
                 QStringList({u"Co? Jak?"_s}));
        QCOMPARE(iotest::textAt(r, u"Roam/Pusta.md"_s), QString());
        QCOMPARE(importWith(u"roam"_s, {text(u"x.json"_s, u"[{\"title\": broken"_s)}).warnings.size(), 1);
    }
};

QTEST_MAIN(TestIoImportApps)
#include "tst_io_import_apps.moc"
