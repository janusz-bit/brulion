// Trilium importer: real exports (fixtures/trilium, made by Trilium servers 0.106, 0.99 and 0.63 —
// see the README there), synthetic exports built in the test (hostile and damaged ones included)
// and the content conversion (CKEditor HTML, Turndown Markdown) on its own.
#include "iotest.h"

#include "io/importer.h"
#include "io/trilium_p.h"
#include "io/zip.h"
#include "markdown/frontmatter.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QThread>

#include <memory>
#include <vector>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;
using iotest::bytes;
namespace trilium = brulion::io::detail::trilium;

namespace {

QByteArray fixture(const QString &name)
{
    QFile f(QStringLiteral(BRULION_TRILIUM_FIXTURES) + u'/' + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

ImportResult importFiles(const QList<InputFile> &files, const ImportOptions &options = iotest::options(u"Trilium"_s))
{
    return runImport(*importerById(u"trilium"_s), files, options);
}

ImportResult importFixture(const QString &name)
{
    return importFiles({bytes(name, fixture(name))});
}

QString bodyOf(const QString &content)
{
    return content.sliced(markdown::parseFrontmatter(content).bodyStart);
}

QVariantMap frontmatterOf(const QString &content)
{
    return markdown::parseFrontmatter(content).data;
}

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

/// Writes every file of `r` below $BRULION_TRILIUM_DUMP/<name>, to look at the output by hand.
void dump(const QString &name, const ImportResult &r)
{
    const QString root = qEnvironmentVariable("BRULION_TRILIUM_DUMP");
    if (root.isEmpty())
        return;
    for (const ImportedFile &f : r.files) {
        const QString path = root + u'/' + name + u'/' + f.path;
        QDir().mkpath(path.section(u'/', 0, -2));
        QFile out(path);
        if (out.open(QIODevice::WriteOnly))
            out.write(f.data);
    }
    QFile w(root + u'/' + name + u"/WARNINGS.txt"_s);
    if (w.open(QIODevice::WriteOnly))
        w.write(r.warnings.join(u'\n').toUtf8());
}

/// Frontmatter values as strings and string lists (YAML readers differ in the list type).
QVariantMap plain(const QVariantMap &map)
{
    QVariantMap out;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        const int type = it->typeId();
        out.insert(it.key(), type == QMetaType::QVariantList || type == QMetaType::QStringList
                                     ? QVariant(it->toStringList())
                                     : QVariant(it->toString()));
    }
    return out;
}

bool hasWarning(const ImportResult &r, const QString &fragment)
{
    return std::any_of(r.warnings.begin(), r.warnings.end(), [&](const QString &w) { return w.contains(fragment); });
}

// --- synthetic exports -----------------------------------------------------------------------------

/// A note of a synthetic `!!!meta.json`.
struct Meta {
    QString id = {};
    QString title = {};
    QString data = {}; ///< dataFileName
    QString dir = {};  ///< dirFileName
    std::vector<Meta> children = {};
    QJsonArray attributes = {};
    QString type = u"text"_s;
    QString mime = u"text/html"_s;
    QString prefix = {};
    bool clone = false;
    QJsonArray attachments = {};
};

QJsonObject label(const QString &name, const QString &value = {})
{
    return QJsonObject{{u"type"_s, u"label"_s}, {u"name"_s, name}, {u"value"_s, value}, {u"isInheritable"_s, false}};
}

QJsonObject relation(const QString &name, const QString &target)
{
    return QJsonObject{
            {u"type"_s, u"relation"_s}, {u"name"_s, name}, {u"value"_s, target}, {u"isInheritable"_s, false}};
}

QJsonObject toJson(const Meta &m)
{
    QJsonObject o{{u"isClone"_s, m.clone}, {u"noteId"_s, m.id}, {u"title"_s, m.title},
                  {u"type"_s, m.type},     {u"mime"_s, m.mime}, {u"attributes"_s, m.attributes},
                  {u"format"_s, u"html"_s}};
    o.insert(u"prefix"_s, m.prefix.isEmpty() ? QJsonValue() : QJsonValue(m.prefix));
    if (!m.data.isEmpty())
        o.insert(u"dataFileName"_s, m.data);
    if (!m.dir.isEmpty())
        o.insert(u"dirFileName"_s, m.dir);
    if (!m.attachments.isEmpty())
        o.insert(u"attachments"_s, m.attachments);
    if (!m.children.empty()) {
        QJsonArray children;
        for (const Meta &c : m.children)
            children.append(toJson(c));
        o.insert(u"children"_s, children);
    }
    return o;
}

QByteArray metaJson(const std::vector<Meta> &top, int formatVersion = 2)
{
    QJsonArray files;
    for (const Meta &m : top)
        files.append(toJson(m));
    return QJsonDocument(QJsonObject{{u"formatVersion"_s, formatVersion},
                                     {u"appVersion"_s, u"0.106.0"_s},
                                     {u"files"_s, files}})
            .toJson();
}

/// A text note's page as the HTML export writes it.
QByteArray page(const QString &title, const QString &content)
{
    return (u"<html>\n<head>\n<meta charset=\"utf-8\">\n<title data-trilium-title>"_s + title.toHtmlEscaped()
            + u"</title>\n</head>\n<body>\n<div class=\"content\">\n<h1 data-trilium-h1>"_s + title.toHtmlEscaped()
            + u"</h1>\n<div class=\"ck-content\">"_s + content + u"</div>\n</div>\n</body>\n</html>\n"_s)
            .toUtf8();
}

ImportResult importZip(const QList<FileEntry> &entries)
{
    return importFiles({iotest::zip(u"trilium.zip"_s, entries)});
}

/// Targets of the wiki links and embeds of a page (`[[target|…]]`, `![[target#…]]`).
QStringList wikiTargets(const QString &text)
{
    static const QRegularExpression link(u"\\[\\[([^\\]|#\\n]+)"_s);
    QStringList out;
    for (auto it = link.globalMatch(text); it.hasNext();)
        out.append(it.next().captured(1).trimmed());
    return out;
}

/// An attachment of a synthetic meta.
QJsonObject attachment(const QString &id, const QString &title, const QString &mime, const QString &file)
{
    return QJsonObject{{u"attachmentId"_s, id},
                       {u"title"_s, title},
                       {u"role"_s, mime.startsWith(u"image/"_s) ? u"image"_s : u"file"_s},
                       {u"mime"_s, mime},
                       {u"dataFileName"_s, file}};
}

/// The conversion of one text note with references resolved by name: `X.html` -> [[X]].
trilium::Resolver testResolver()
{
    trilium::Resolver r;
    r.link = [](const QString &href, const QString &label, bool reference) -> std::optional<QString> {
        static const QRegularExpression scheme(u"^[A-Za-z][A-Za-z0-9+.-]*:"_s);
        if (scheme.match(href).hasMatch())
            return std::nullopt;
        if (href.startsWith(u"#root"_s))
            return label; // a note outside the export
        const QString name = href.section(u'.', 0, 0);
        return u"[["_s + name + (reference ? QString() : u'|' + label) + u"]]"_s;
    };
    r.image = [](const QString &src, const QString &) { return u"![["_s + src + u"]]"_s; };
    r.include = [](const QString &id) { return u"![[note "_s + id + u"]]"_s; };
    return r;
}

} // namespace

class TestIoImportTrilium : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- detection ----------------------------------------------------------------------------------

    void detect_data()
    {
        QTest::addColumn<QList<InputFile>>("files");
        QTest::addColumn<QString>("importer");
        QTest::newRow("HTML export") << QList<InputFile>{bytes(u"a.zip"_s, fixture(u"html.zip"_s))} << u"trilium"_s;
        QTest::newRow("Markdown export") << QList<InputFile>{bytes(u"a.zip"_s, fixture(u"markdown.zip"_s))}
                                         << u"trilium"_s;
        QTest::newRow("0.63 export") << QList<InputFile>{bytes(u"a.zip"_s, fixture(u"0.63-html.zip"_s))}
                                     << u"trilium"_s;
        QTest::newRow("unpacked into a folder")
                << QList<InputFile>{bytes(u"Eksport/!!!meta.json"_s, metaJson({})),
                                    bytes(u"Eksport/a.html"_s, page(u"a"_s, u"<p>x</p>"_s))}
                << u"trilium"_s;
        QTest::newRow("damaged meta") << QList<InputFile>{bytes(u"!!!meta.json"_s, "{ zepsute"),
                                                          bytes(u"a.html"_s, "<p>x</p>")}
                                      << u"trilium"_s;
        QTest::newRow("other app's meta.json")
                << QList<InputFile>{bytes(u"meta.json"_s, metaJson({})), bytes(u"a.html"_s, "<p>x</p>")} << u"html"_s;
        QTest::newRow("plain HTML") << QList<InputFile>{bytes(u"strona.html"_s, "<p>x</p>")} << u"html"_s;
        // Trilium's export of a single note has no meta: the generic importers take it.
        QTest::newRow("single note HTML")
                << QList<InputFile>{bytes(u"o.html"_s, fixture(u"single-ogrod.html"_s))} << u"html"_s;
        QTest::newRow("single note Markdown")
                << QList<InputFile>{bytes(u"o.md"_s, fixture(u"single-ogrod.md"_s))} << u"markdown"_s;
        QTest::newRow("Markdown") << QList<InputFile>{bytes(u"a.md"_s, "# x")} << u"markdown"_s;
    }

    void detect()
    {
        QFETCH(QList<InputFile>, files);
        QFETCH(QString, importer);
        const Importer *found = detectImporter(files);
        QVERIFY(found);
        QCOMPARE(found->id(), importer);
    }

    // --- the real exports ---------------------------------------------------------------------------

    void wholeTree()
    {
        const ImportResult r = importFixture(u"html.zip"_s);
        dump(u"html.zip"_s, r);
        QCOMPARE(iotest::pathsOf(r),
                 sorted({u"Trilium/Archiwum.md"_s,
                         u"Trilium/Archiwum/Bardzo długi tytuł notatki o planowaniu ogrodu warzywnego na sezon 2026 i "
                         "dalej.md"_s,
                         u"Trilium/Archiwum/Zarchiwizowana notatka.md"_s,
                         u"Trilium/Inne typy/Mapa myśli.md"_s,
                         u"Trilium/Inne typy/Mapa relacji.md"_s,
                         u"Trilium/Inne typy/Pusta notatka.md"_s,
                         u"Trilium/Inne typy/Strona WWW.md"_s,
                         u"Trilium/Inne typy/widok.html.md"_s,
                         u"Trilium/Osoby/Jan Kowalski.md"_s,
                         u"Trilium/Projekty.md"_s,
                         u"Trilium/Projekty/Biblioteka.md"_s,
                         u"Trilium/Projekty/Biblioteka/Część I - Rozdział 1 Gleba.md"_s,
                         u"Trilium/Projekty/Biblioteka/Rozdział 2 Nawożenie.md"_s,
                         u"Trilium/Projekty/Diagram przepływu.md"_s,
                         u"Trilium/Projekty/Kod.md"_s,
                         u"Trilium/Projekty/Kod/Analiza plonów.md"_s,
                         u"Trilium/Projekty/Kod/Notatka Markdown.md"_s,
                         u"Trilium/Projekty/Kod/dane.md"_s,
                         u"Trilium/Projekty/Kod/skrypt.js.md"_s,
                         u"Trilium/Projekty/Ogród plan budżet.md"_s,
                         u"Trilium/Projekty/Spotkanie 2026-10-14.md"_s,
                         u"Trilium/Projekty/Szkic grządek.md"_s,
                         u"Trilium/Projekty/Untitled.md"_s,
                         u"Trilium/Projekty/Zadania 2.md"_s,
                         u"Trilium/Projekty/Zadania.md"_s,
                         u"Trilium/Szablony/Osoba (szablon).md"_s,
                         u"assets/Trilium/Inne typy/Mapa myśli.json"_s,
                         u"assets/Trilium/Projekty/Diagram przepływu.svg"_s,
                         u"assets/Trilium/Projekty/Ogród plan budżet/lista zakupów.txt"_s,
                         u"assets/Trilium/Projekty/Ogród plan budżet/zdjęcie grządki.png"_s,
                         u"assets/Trilium/Projekty/Szkic grządek.excalidraw"_s,
                         u"assets/Trilium/Projekty/Szkic grządek.svg"_s,
                         u"assets/Trilium/Projekty/kropka.png"_s,
                         u"assets/Trilium/Projekty/umowa.pdf"_s,
                         u"journals/2025-12-31.md"_s,
                         u"journals/2026-10-14.md"_s,
                         u"journals/2026-10-16.md"_s}));
        // The protected note, three notes without a counterpart, links out of the export.
        QCOMPARE(r.warnings.size(), 3);
        QVERIFY(hasWarning(r, u"1 protected note"_s));
        QVERIFY(hasWarning(r, u"“Wyszukiwanie: projekty”, “Mapa notatek”, “Notatka renderowana”"_s));
        QVERIFY(hasWarning(r, u"only their text was kept"_s));
        // Attachments are the exported bytes.
        const QList<FileEntry> zip = readZip(fixture(u"html.zip"_s));
        QCOMPARE(iotest::fileAt(r, u"assets/Trilium/Projekty/kropka.png"_s)->data,
                 iotest::entryData(zip, u"root/Projekty/kropka.png"_s));
        QCOMPARE(iotest::fileAt(r, u"assets/Trilium/Projekty/Ogród plan budżet/zdjęcie grządki.png"_s)->data,
                 iotest::entryData(zip, u"root/Projekty/Ogród planbudżet_zdjęcie grządki.png"_s));
        QCOMPARE(iotest::fileAt(r, u"assets/Trilium/Projekty/Szkic grządek.excalidraw"_s)->data,
                 iotest::entryData(zip, u"root/Projekty/Szkic grządek.json"_s));
        // Daily notes are appended to existing ones.
        QVERIFY(iotest::fileAt(r, u"journals/2026-10-16.md"_s)->append);
    }

    void pages_data()
    {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("content");
        for (const QString &f : {u"html.zip"_s, u"markdown.zip"_s}) {
            const auto row = [&](const char *name, const QString &path, const QString &content) {
                QTest::addRow("%s %s", qPrintable(f), name) << f << path << content;
            };
            // Every link names the page by its vault path; the children follow the content (as
            // Trilium shows them), image notes embedded, file notes linked.
            row("tags and properties", u"Trilium/Projekty.md"_s,
                u"---\ntags:\n  - projekt\nstatus: w toku\n---\nWszystkie bieżące projekty. Zobacz też "
                "[[Trilium/Archiwum|Archiwum]].\n\n---\n\n"
                "- [[Trilium/Projekty/Ogród plan budżet|Ogród: plan/budżet?]]\n"
                "- [[Trilium/Projekty/Zadania|Zadania]]\n- [[Trilium/Projekty/Zadania 2|Zadania]]\n"
                "- [[Trilium/Projekty/Kod|Kod]]\n- [[Trilium/Projekty/Biblioteka|Biblioteka]]\n"
                "- [[Trilium/Projekty/Diagram przepływu|Diagram przepływu]]\n"
                "- [[Trilium/Projekty/Szkic grządek|Szkic grządek]]\n- ![[assets/Trilium/Projekty/kropka.png]]\n"
                "- [[assets/Trilium/Projekty/umowa.pdf|umowa.pdf]]\n"
                "- [[Trilium/Projekty/Spotkanie 2026-10-14|Spotkanie 2026-10-14]]\n"
                "- [[Trilium/Projekty/Untitled|???]]\n"_s);
            row("done task", u"Trilium/Projekty/Zadania 2.md"_s,
                u"Drugi notatnik o tym samym tytule (rodzeństwo z duplikatem nazwy).\n\n- [x] Zrobione zadanie\n"_s);
            row("folder page", u"Trilium/Projekty/Kod.md"_s,
                u"- [[Trilium/Projekty/Kod/skrypt.js|skrypt.js]]\n- [[Trilium/Projekty/Kod/dane|dane]]\n"
                "- [[Trilium/Projekty/Kod/Notatka Markdown|Notatka Markdown]]\n"
                "- [[Trilium/Projekty/Kod/Analiza plonów|Analiza plonów]]\n"_s);
            row("book with a prefixed child", u"Trilium/Projekty/Biblioteka.md"_s,
                u"- [[Trilium/Projekty/Biblioteka/Część I - Rozdział 1 Gleba|Część I - Rozdział 1: Gleba]]\n"
                "- [[Trilium/Projekty/Biblioteka/Rozdział 2 Nawożenie|Rozdział 2: Nawożenie]]\n"_s);
            // The clone placement and the other children (not the protected one).
            row("clone placement", u"Trilium/Archiwum.md"_s,
                u"Stare rzeczy.\n\n---\n\n- [[Trilium/Projekty/Spotkanie 2026-10-14|2026 - Spotkanie 2026-10-14]]\n"
                "- [[Trilium/Archiwum/Zarchiwizowana notatka|Zarchiwizowana notatka]]\n"
                "- [[Trilium/Archiwum/Bardzo długi tytuł notatki o planowaniu ogrodu warzywnego na sezon 2026 i dalej|"
                "Bardzo długi tytuł notatki o planowaniu ogrodu warzywnego na sezon 2026 i dalej]]\n"_s);
            row("title made of forbidden characters", u"Trilium/Projekty/Untitled.md"_s,
                u"---\naliases:\n  - \"???\"\n---\nTytuł złożony wyłącznie z niedozwolonych znaków.\n"_s);
            row("template", u"Trilium/Szablony/Osoba (szablon).md"_s,
                u"---\ntelefon: \"\"\nurodziny: \"\"\npracodawca: \"\"\n---\nSzablon notatki o osobie.\n"_s);
            row("python code", u"Trilium/Projekty/Kod/Analiza plonów.md"_s,
                u"```python\ndef plon(kg: float) -> str:\n    return f\"Zebrano {kg} kg — świetnie!\"\n```\n"_s);
            row("javascript code", u"Trilium/Projekty/Kod/skrypt.js.md"_s,
                u"```javascript\n// Licznik grządek\nconst grządki = [\"marchew\", \"pietruszka\"];\n"
                "api.log(`Liczba: ${grządki.length}`);\n```\n"_s);
            row("html code", u"Trilium/Inne typy/widok.html.md"_s,
                u"```html\n<p>Widok renderowany: <b>ąęś</b></p>\n```\n"_s);
            row("markdown code", u"Trilium/Projekty/Kod/Notatka Markdown.md"_s,
                u"# Notatka w Markdown\n\nLink do [[Trilium/Projekty/Ogród plan budżet|planu ogrodu]] i "
                "**pogrubienie**.\n\n- [ ] zadanie w Markdown\n- [x] zrobione\n"_s);
            row("mermaid", u"Trilium/Projekty/Diagram przepływu.md"_s,
                u"![[assets/Trilium/Projekty/Diagram przepływu.svg]]\n\n```mermaid\ngraph LR\n    A[Siew] --> B[Zbiór]\n```\n"_s);
            row("canvas", u"Trilium/Projekty/Szkic grządek.md"_s,
                u"![[assets/Trilium/Projekty/Szkic grządek.svg]]\n\n"
                "[[assets/Trilium/Projekty/Szkic grządek.excalidraw|Szkic grządek.excalidraw]]\n"_s);
            row("mind map", u"Trilium/Inne typy/Mapa myśli.md"_s,
                u"- Ogród\n\t- Warzywa\n\t- Kwiaty\n\n[[assets/Trilium/Inne typy/Mapa myśli.json|Mapa myśli.json]]\n"_s);
            row("relation map", u"Trilium/Inne typy/Mapa relacji.md"_s,
                u"- [[Trilium/Osoby/Jan Kowalski|Jan Kowalski]]\n- [[Trilium/Projekty|Projekty]]\n"_s);
            row("web view", u"Trilium/Inne typy/Strona WWW.md"_s, u"<https://example.org/>\n"_s);
            row("empty note", u"Trilium/Inne typy/Pusta notatka.md"_s, QString());
            row("journal", u"journals/2026-10-14.md"_s,
                u"Spotkanie w sprawie działki: [[Trilium/Projekty/Spotkanie 2026-10-14|Spotkanie 2026-10-14]].\n\n"
                "- [x] Zadzwonić do sąsiada\n"_s);
            row("journal with a property", u"journals/2026-10-16.md"_s,
                u"---\nnastrój: dobry\n---\nDziś: **sadzenie czosnku**.\n\n- [ ] Posadzić czosnek o 10:00\n"
                "- [/] Grabienie liści\n\nZobacz [[Trilium/Projekty/Zadania|Zadania]].\n"_s);
            row("journal of another year", u"journals/2025-12-31.md"_s, u"Podsumowanie roku. Sylwester 🎆\n"_s);
            // Trilium has no inline tags: "#archived" in the text is no tag in Brulion either.
            row("hashtag in the text", u"Trilium/Archiwum/Zarchiwizowana notatka.md"_s,
                u"Ukryta w drzewie przez \\#archived, ale eksportowana.\n"_s);
        }
        QTest::newRow("html.zip task states") << u"html.zip"_s << u"Trilium/Projekty/Zadania.md"_s
                                              << u"- [x] Kupić nasiona marchwi\n- [ ] Przekopać grządkę (ok. 2 h)\n"
                                                 "- [/] Zamówić **kompostownik**\n- [ ] Szklarnia?\n- [-] Oczko wodne\n"
                                                 "\t- [ ] zagnieżdżone: wybrać folię\n"_s;
        QTest::newRow("markdown.zip task states") << u"markdown.zip"_s << u"Trilium/Projekty/Zadania.md"_s
                                                  << u"- [x] Kupić nasiona marchwi\n- [ ] Przekopać grządkę (ok. 2 h)\n"
                                                     "- [/] Zamówić **kompostownik**\n- [ ] Szklarnia?\n- [-] Oczko "
                                                     "wodne\n    - [ ] zagnieżdżone: wybrać folię\n"_s;
    }

    void pages()
    {
        QFETCH(QString, fixture);
        QFETCH(QString, path);
        QFETCH(QString, content);
        const ImportResult r = importFixture(fixture);
        QCOMPARE(iotest::textAt(r, path), content);
    }

    void personProperties()
    {
        for (const QString &f : {u"html.zip"_s, u"markdown.zip"_s}) {
            const QString text = iotest::textAt(importFixture(f), u"Trilium/Osoby/Jan Kowalski.md"_s);
            const QVariantMap fm = frontmatterOf(text);
            QCOMPARE(fm.value(u"telefon"_s).toString(), u"000 000 000"_s);
            QCOMPARE(fm.value(u"urodziny"_s).toString(), u"1990-01-31"_s);
            QCOMPARE(fm.value(u"opis"_s).toString(), u"Ma \"cudzysłowy\", przecinki, #hasze i: dwukropki"_s);
            QCOMPARE(fm.value(u"pracodawca"_s).toString(), u"[[Trilium/Projekty|Projekty]]"_s); // a relation
            QVERIFY(!fm.contains(u"template"_s)); // Trilium's own relation
            QCOMPARE(bodyOf(text), u"Fikcyjna osoba do testów.\n"_s);
        }
    }

    void richNote_data()
    {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<QString>("line");
        const QStringList both = {
                u"\\# To nie jest nagłówek"_s,
                u"### Nagłówek trzeci — zażółć gęślą jaźń"_s,
                u"| **Ogórki** \\| gruntowe | 8 zł |"_s,
                u"```python"_s,
                u"```javascript"_s,
                u"const a = 1 < 2 && true;"_s,
                u"```mermaid"_s,
                u"> Cytat: „Kto sieje wiatr, ten zbiera burzę.”"_s,
                u"> [!tip]"_s,
                u"> Wskazówka: podlewaj rano."_s,
                u"> [!warning]"_s,
                u"> Uwaga na **przymrozki**!"_s,
                u"Wzór w linii $E=mc^2$ i blokowy:"_s,
                u"```math"_s,
                u"\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}"_s,
                u"Przypis do tekstu[^1]."_s,
                u"> [!note]- Szczegóły (zwijane)"_s,
                u"> Ukryta treść."_s,
                u"Odnośnik referencyjny: [[Trilium/Projekty/Spotkanie 2026-10-14|Spotkanie 2026-10-14]]."_s,
                u"Zwykły link wewnętrzny z własnym tekstem: [[Trilium/Osoby/Jan Kowalski|autor planu]]; do klonu "
                "przez drugą ścieżkę: [[Trilium/Projekty/Spotkanie 2026-10-14|spotkanie w archiwum]]."_s,
                u"Do innego poddrzewa: [[Trilium/Szablony/Osoba (szablon)|Osoba (szablon)]], do folderu: "
                "[[Trilium/Projekty/Kod|Kod]], do kodu: [[Trilium/Projekty/Kod/skrypt.js|skrypt.js]], do duplikatu: "
                "[[Trilium/Projekty/Zadania 2|Zadania]], do dnia: [[2026-10-16]]."_s,
                u"Do notatki chronionej: Sekretne hasła, do wykluczonej: Nie eksportuj, do nieistniejącej: \\[missing "
                "note\\]."_s,
                u"Załącznik (plik): [[assets/Trilium/Projekty/Ogród plan budżet/lista zakupów.txt|lista zakupów.txt]]."_s,
                u"Podgląd linku: [Example Domain](https://example.org/) w zdaniu."_s,
                u"[Artykuł o ogrodach](https://example.org/artykul)"_s,
                u"Ikona w linii w tekście."_s,
                u"![[assets/Trilium/Projekty/Ogród plan budżet/zdjęcie grządki.png]]"_s,
                u"Zdjęcie grządki (załącznik)"_s,
                u"Obraz z notatki-obrazu w linii: ![[assets/Trilium/Projekty/kropka.png]]"_s,
                u"![[assets/Trilium/Projekty/Diagram przepływu.svg]]"_s,
                u"![[Trilium/Projekty/Zadania]]"_s,
                // The last line of a code block has no indentation of the pretty-printed page.
                u"    A[Start] --> B[Koniec]"_s,
        };
        for (const QString &f : {u"html.zip"_s, u"markdown.zip"_s}) {
            for (const QString &line : both)
                QTest::addRow("%s %s", qPrintable(f), qPrintable(line.left(40))) << f << line;
        }
        QTest::newRow("html inline formatting")
                << u"html.zip"_s
                << u"Plan ogrodu na rok 2026. Tekst **pogrubiony**, *kursywa*, podkreślenie, ~~przekreślenie~~, "
                   "`kod_w_linii()`, H2O, m2, `Ctrl`+`S`, ==zaznaczenie==, czerwony tekst i emoji 🌱."_s;
        QTest::newRow("html escaping") << u"html.zip"_s
                                       << u"Znaki specjalne: \\<script>alert(1)\\</script>, \\*gwiazdki\\*, "
                                          "\\_podkreślniki\\_, [nawiasy], a&b, 100% i \\ukośnik."_s;
        // A list right after another one keeps its own start: the other delimiter.
        QTest::newRow("html lists") << u"html.zip"_s << u"5) Lista od piątki"_s;
        QTest::newRow("html footnote") << u"html.zip"_s << u"[^1]: Treść przypisu z *ogonkami*: ąćęłńóśźż."_s;
        QTest::newRow("markdown inline formatting")
                << u"markdown.zip"_s
                << u"Plan ogrodu na rok 2026. Tekst **pogrubiony**, _kursywa_, podkreślenie, ~~przekreślenie~~, "
                   "`kod_w_linii()`, H2O, m2, `Ctrl`+`S`, ==zaznaczenie==, czerwony tekst i emoji 🌱."_s;
        QTest::newRow("markdown escaping") << u"markdown.zip"_s
                                           << u"Znaki specjalne: \\<script\\>alert(1)\\</script\\>, \\*gwiazdki\\*, "
                                              "\\_podkreślniki\\_, \\[nawiasy\\], a&b, 100% i \\\\ukośnik."_s;
        QTest::newRow("markdown lists") << u"markdown.zip"_s << u"5)  Lista od piątki"_s;
        QTest::newRow("markdown footnote") << u"markdown.zip"_s << u"[^1]: Treść przypisu z _ogonkami_: ąćęłńóśźż."_s;
    }

    void richNote()
    {
        QFETCH(QString, fixture);
        QFETCH(QString, line);
        const ImportResult r = importFixture(fixture);
        const QString text = iotest::textAt(r, u"Trilium/Projekty/Ogród plan budżet.md"_s);
        QVERIFY2(text.split(u'\n').contains(line), qPrintable(text));
    }

    void richNoteFrontmatter()
    {
        const QString text = iotest::textAt(importFixture(u"html.zip"_s), u"Trilium/Projekty/Ogród plan budżet.md"_s);
        const QVariantMap fm = frontmatterOf(text);
        QCOMPARE(fm.value(u"aliases"_s).toStringList(), QStringList({u"Ogród: plan/budżet?"_s}));
        QCOMPARE(fm.value(u"tags"_s).toStringList(), QStringList({u"ważne"_s}));
        QCOMPARE(fm.value(u"priorytet"_s).toString(), u"wysoki"_s);
        QCOMPARE(fm.value(u"autor"_s).toString(), u"[[Trilium/Osoby/Jan Kowalski|Jan Kowalski]]"_s);
        QCOMPARE(fm.value(u"inspiracja"_s).toString(),
                 u"[[Trilium/Projekty/Biblioteka/Część I - Rozdział 1 Gleba|Rozdział 1: Gleba]]"_s);
        QVERIFY(!fm.contains(u"iconClass"_s));
        QVERIFY(!fm.contains(u"includeNoteLink"_s));
        // Every attachment is shown by the content, none is listed again.
        QVERIFY(!text.contains(u"zdjęcie grządki.png|"_s));
    }

    void markdownExportMatchesHtmlExport()
    {
        const ImportResult html = importFixture(u"html.zip"_s);
        const ImportResult md = importFixture(u"markdown.zip"_s);
        dump(u"markdown.zip"_s, md);
        QCOMPARE(iotest::pathsOf(md), iotest::pathsOf(html));
        QCOMPARE(md.warnings, html.warnings);
    }

    void subtree()
    {
        for (const QString &f : {u"subtree-projekty-html.zip"_s, u"subtree-projekty-markdown.zip"_s}) {
            const ImportResult r = importFixture(f);
            dump(f, r);
            QVERIFY(iotest::fileAt(r, u"Trilium/Projekty.md"_s));
            QVERIFY(iotest::fileAt(r, u"Trilium/Projekty/Kod/skrypt.js.md"_s));
            QVERIFY(!iotest::pathsOf(r).join(u'\n').contains(u"journals/"_s));
            const QString text = iotest::textAt(r, u"Trilium/Projekty/Ogród plan budżet.md"_s);
            // Links to notes outside the subtree keep their text; relations to them are gone.
            QVERIFY2(text.contains(u"Zwykły link wewnętrzny z własnym tekstem: autor planu;"_s), qPrintable(text));
            QVERIFY(text.contains(u"Do innego poddrzewa: Osoba (szablon), do folderu: [[Trilium/Projekty/Kod|Kod]]"_s));
            QVERIFY(text.contains(u"do dnia: 16 - Piątek."_s));
            QVERIFY(!frontmatterOf(text).contains(u"autor"_s));
            QCOMPARE(frontmatterOf(text).value(u"inspiracja"_s).toString(),
                     u"[[Trilium/Projekty/Biblioteka/Część I - Rozdział 1 Gleba|Rozdział 1: Gleba]]"_s);
            QCOMPARE(r.warnings.size(), 1);
            QVERIFY(hasWarning(r, u"only their text was kept"_s));
        }
    }

    void protectedSession()
    {
        // Exported while a protected session was open: the protected note is there, the clone
        // whose first place is outside the export is a whole note with its prefix.
        const ImportResult r = importFixture(u"archiwum-protected-session.zip"_s);
        QCOMPARE(iotest::pathsOf(r),
                 sorted({u"Trilium/Archiwum.md"_s, u"Trilium/Archiwum/2026 - Spotkanie 2026-10-14.md"_s,
                         u"Trilium/Archiwum/Bardzo długi tytuł notatki o planowaniu ogrodu warzywnego na sezon 2026 i "
                         "dalej.md"_s,
                         u"Trilium/Archiwum/Sekretne hasła.md"_s, u"Trilium/Archiwum/Zarchiwizowana notatka.md"_s}));
        QCOMPARE(frontmatterOf(iotest::textAt(r, u"Trilium/Archiwum/2026 - Spotkanie 2026-10-14.md"_s))
                         .value(u"aliases"_s)
                         .toStringList(),
                 QStringList({u"Spotkanie 2026-10-14"_s}));
        QVERIFY(r.warnings.isEmpty());
    }

    void olderVersions_data()
    {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("line");
        // 0.99 points the mermaid image at the note's .dat file: its rendered SVG is shown.
        QTest::newRow("0.99 html mermaid image")
                << u"0.99-subtree-projekty-html.zip"_s << u"Trilium/Projekty/Ogród plan budżet.md"_s
                << u"![[assets/Trilium/Projekty/Diagram przepływu.svg]]"_s;
        QTest::newRow("0.99 html attachment named by its title")
                << u"0.99-subtree-projekty-html.zip"_s << u"Trilium/Projekty/Ogród plan budżet.md"_s
                << u"![[assets/Trilium/Projekty/Ogród plan budżet/zdjęcie grządki.png]]"_s;
        QTest::newRow("0.99 markdown unescaped html")
                << u"0.99-subtree-projekty-markdown.zip"_s << u"Trilium/Projekty/Ogród plan budżet.md"_s
                << u"Znaki specjalne: \\<script>alert(1)\\</script>, \\*gwiazdki\\*, \\_podkreślniki\\_, "
                   u"\\[nawiasy\\], "
                   "a&b, 100% i \\\\ukośnik."_s;
        QTest::newRow("0.99 markdown code note (.mkd)")
                << u"0.99-subtree-projekty-markdown.zip"_s << u"Trilium/Projekty/Kod/Notatka Markdown.md"_s
                << u"Link do [[Trilium/Projekty/Ogród plan budżet|planu ogrodu]] i **pogrubienie**."_s;
        QTest::newRow("0.63 html links")
                << u"0.63-html.zip"_s << u"Trilium/Projekty/Ogród plan budżet.md"_s
                << u"Linki: [[Trilium/Projekty/Spotkanie 2026-10-14|Spotkanie 2026-10-14]], [[Trilium/Osoby/Jan "
                   "Kowalski|autor]], [[Trilium/Projekty/Kod|Kod]], [[Trilium/Projekty/Zadania 2|Zadania]], Sekretne "
                   "hasła, [[2026-10-16]], [[Trilium/Projekty/Bardzo długi tytuł notatki o planowaniu ogrodu "
                   "warzywnego "
                   "na sezon 2026 i dalej|Bardzo długi tytuł notatki o planowaniu ogrodu warzywnego na sezon 2026 i "
                   "dalej]], [example.org](https://example.org/), załącznik [[assets/Trilium/Projekty/Ogród plan "
                   "budżet/lista zakupów.txt|lista zakupów.txt]]."_s;
        QTest::newRow("0.63 html old markup")
                << u"0.63-html.zip"_s << u"Trilium/Projekty/Ogród plan budżet.md"_s
                << u"Tekst **pogrubiony**, *kursywa*, podkreślenie, ~~przekreślenie~~, `kod()`, H2O, ==zaznaczenie==, "
                   "czerwony 🌱."_s;
        QTest::newRow("0.63 html math") << u"0.63-html.zip"_s << u"Trilium/Projekty/Ogród plan budżet.md"_s
                                        << u"Wzór $E=mc^2$."_s;
        QTest::newRow("0.63 markdown strike and literal tag")
                << u"0.63-markdown.zip"_s << u"Trilium/Projekty/Ogród plan budżet.md"_s
                << u"Tekst **pogrubiony**, _kursywa_, podkreślenie, ~~przekreślenie~~, `kod()`, H2O, zaznaczenie, "
                   "czerwony 🌱."_s;
        QTest::newRow("0.63 markdown math")
                << u"0.63-markdown.zip"_s << u"Trilium/Projekty/Ogród plan budżet.md"_s << u"Wzór $E=mc^2$."_s;
        // Attachment names with spaces, unencoded (not valid CommonMark).
        QTest::newRow("0.63 markdown raw attachment names")
                << u"0.63-markdown.zip"_s << u"Trilium/Projekty/Ogród plan budżet.md"_s
                << u"![[assets/Trilium/Projekty/Ogród plan budżet/zdjęcie grządki.png]]"_s;
        QTest::newRow("0.63 day note") << u"0.63-html.zip"_s << u"journals/2026-10-16.md"_s
                                       << u"- [ ] Posadzić czosnek"_s;
        QTest::newRow("0.63 mermaid without image")
                << u"0.63-html.zip"_s << u"Trilium/Projekty/Diagram przepływu.md"_s << u"```mermaid"_s;
    }

    void olderVersions()
    {
        QFETCH(QString, fixture);
        QFETCH(QString, path);
        QFETCH(QString, line);
        const ImportResult r = importFixture(fixture);
        dump(fixture, r);
        const QString text = iotest::textAt(r, path);
        QVERIFY2(text.split(u'\n').contains(line), qPrintable(text));
        // The automatic relations of old versions are not properties.
        QVERIFY(!text.contains(u"internalLink"_s) && !text.contains(u"imageLink"_s));
    }

    void olderVersionsListNoAttachmentTwice()
    {
        // The attachments shown in the text are not listed again at its end.
        for (const QString &f : {u"0.63-html.zip"_s, u"0.63-markdown.zip"_s, u"0.99-subtree-projekty-markdown.zip"_s}) {
            const QString text = iotest::textAt(importFixture(f), u"Trilium/Projekty/Ogród plan budżet.md"_s);
            QCOMPARE(text.count(u"zdjęcie grządki.png"_s), 1);
            QCOMPARE(text.count(u"lista zakupów.txt|"_s), 1);
        }
    }

    void importsIntoTheVaultRoot()
    {
        ImportOptions o = iotest::options();
        o.config.journalsFolder = u"Dziennik"_s;
        o.config.journalFileFormat = u"dd.MM.yyyy"_s;
        const ImportResult r = importFiles({bytes(u"a.zip"_s, fixture(u"html.zip"_s))}, o);
        QVERIFY(iotest::fileAt(r, u"Projekty.md"_s));
        QVERIFY(iotest::fileAt(r, u"assets/Trilium/Projekty/kropka.png"_s));
        QVERIFY(iotest::fileAt(r, u"Dziennik/16.10.2026.md"_s));
        QVERIFY(iotest::textAt(r, u"Projekty/Ogród plan budżet.md"_s).contains(u"do dnia: [[16.10.2026]]"_s));
    }

    void existingVaultPages()
    {
        // A second import into the same folder: pages and attachments get new names, links follow.
        ImportOptions o = iotest::options(u"Trilium"_s);
        o.exists = [](const VaultPath &p) {
            return p == u"Trilium/Projekty/Zadania.md" || p == u"assets/Trilium/Projekty/kropka.png";
        };
        const ImportResult r = importFiles({bytes(u"a.zip"_s, fixture(u"html.zip"_s))}, o);
        QVERIFY(iotest::fileAt(r, u"Trilium/Projekty/Zadania 2.md"_s));
        QVERIFY(iotest::fileAt(r, u"Trilium/Projekty/Zadania 3.md"_s));
        QVERIFY(!iotest::fileAt(r, u"Trilium/Projekty/Zadania.md"_s));
        QVERIFY(iotest::fileAt(r, u"assets/Trilium/Projekty/kropka 2.png"_s));
        QVERIFY(r.warnings.size() == 3); // nothing renamed afterwards
        const QString text = iotest::textAt(r, u"Trilium/Projekty/Ogród plan budżet.md"_s);
        QVERIFY2(text.contains(u"![[Trilium/Projekty/Zadania 2]]"_s), qPrintable(text));
        QVERIFY(text.contains(u"do duplikatu: [[Trilium/Projekty/Zadania 3|Zadania]]"_s));
        QVERIFY(text.contains(u"![[assets/Trilium/Projekty/kropka 2.png]]"_s));
    }

    void twoExportsAtOnce()
    {
        // Both unpack next to each other: the second export's files are not lost silently.
        const ImportResult r = importFiles({bytes(u"a.zip"_s, fixture(u"html.zip"_s)),
                                            bytes(u"b.zip"_s, fixture(u"archiwum-protected-session.zip"_s))});
        QVERIFY(iotest::fileAt(r, u"Trilium/Projekty.md"_s));
        QVERIFY(hasWarning(r, u"appears twice"_s));
        QVERIFY(hasWarning(r, u"not part of the Trilium export"_s));
    }

    // --- synthetic exports ------------------------------------------------------------------------

    void brokenMeta_data()
    {
        QTest::addColumn<QByteArray>("meta");
        QTest::newRow("not json") << QByteArray("{ \"formatVersion\": 2, \"files\": [ ");
        QTest::newRow("array") << QByteArray("[1, 2, 3]");
        QTest::newRow("files not a list") << QByteArray("{\"formatVersion\": 2, \"files\": {\"a\": 1}}");
        QTest::newRow("empty") << QByteArray();
    }

    void brokenMeta()
    {
        QFETCH(QByteArray, meta);
        const ImportResult r = importZip({{u"!!!meta.json"_s, meta}, {u"Notatka.html"_s, page(u"N"_s, u"<p>x</p>"_s)}});
        QVERIFY(r.files.isEmpty());
        QCOMPARE(r.warnings.size(), 1);
        QVERIFY2(r.warnings.first().contains(u"!!!meta.json"_s), qPrintable(r.warnings.first()));
    }

    void shareExport()
    {
        QJsonObject meta{{u"formatVersion"_s, 2},
                         {u"files"_s,
                          QJsonArray{toJson(Meta{u"a1"_s, u"Strona"_s, u"Strona.html"_s, {}, {}}),
                                     QJsonObject{{u"noImport"_s, true}, {u"dataFileName"_s, u"assets/style.css"_s}}}}};
        const ImportResult r = importZip({{u"!!!meta.json"_s, QJsonDocument(meta).toJson()},
                                          {u"Strona.html"_s, "<html><body>strona</body></html>"},
                                          {u"assets/style.css"_s, "body{}"}});
        QVERIFY(r.files.isEmpty());
        QCOMPARE(r.warnings.size(), 1);
        QVERIFY(r.warnings.first().contains(u"web publishing"_s));
    }

    void unknownFormatVersion()
    {
        const ImportResult r =
                importZip({{u"!!!meta.json"_s, metaJson({Meta{u"a1"_s, u"Nowa"_s, u"Nowa.html"_s, {}, {}}}, 7)},
                           {u"Nowa.html"_s, page(u"Nowa"_s, u"<p>treść</p>"_s)}});
        QCOMPARE(iotest::textAt(r, u"Trilium/Nowa.md"_s), u"treść\n"_s);
        QCOMPARE(r.warnings.size(), 1);
        QVERIFY(r.warnings.first().contains(u"(7)"_s));
    }

    void hostilePaths_data()
    {
        QTest::addColumn<QString>("dataFileName");
        QTest::addColumn<QString>("title");
        QTest::newRow("parent dirs") << u"../../../../etc/passwd"_s << u"../../etc/passwd"_s;
        QTest::newRow("absolute") << u"/etc/passwd"_s << u"/etc/passwd"_s;
        QTest::newRow("backslashes") << u"..\\..\\Windows\\win.ini"_s << u"C:\\Windows\\win.ini"_s;
        QTest::newRow("protected folder") << u".brulion/config.json"_s << u".brulion"_s;
        QTest::newRow("control characters") << u"a\u0001b.html"_s << u"tytuł\nz nową\tlinią"_s;
    }

    void hostilePaths()
    {
        QFETCH(QString, dataFileName);
        QFETCH(QString, title);
        const QByteArray meta = metaJson({Meta{u"root"_s,
                                               u"root"_s,
                                               {},
                                               u"../.."_s,
                                               {Meta{u"z1"_s,
                                                     title,
                                                     dataFileName,
                                                     u"../x"_s,
                                                     {Meta{u"z2"_s, u"dziecko"_s, u"../../dziecko.html"_s, {}}}}}}});
        const ImportResult r = importZip({{u"!!!meta.json"_s, meta},
                                          {u"etc/passwd"_s, page(u"x"_s, u"<p>wczytane z archiwum</p>"_s)},
                                          {u"dziecko.html"_s, page(u"x"_s, u"<p>dziecko</p>"_s)}});
        QVERIFY(!r.files.isEmpty());
        for (const ImportedFile &f : r.files) {
            QVERIFY2(f.path.startsWith(u"Trilium/"_s) || f.path.startsWith(u"assets/Trilium/"_s), qPrintable(f.path));
            for (const QString &segment : f.path.split(u'/')) {
                QVERIFY2(!segment.isEmpty() && !segment.startsWith(u'.'), qPrintable(f.path));
                QVERIFY2(!segment.contains(u'\\') && !segment.contains(u'\n') && !segment.contains(u'\t'),
                         qPrintable(f.path));
            }
        }
    }

    void missingFiles()
    {
        const QByteArray meta =
                metaJson({Meta{u"a1"_s, u"Bez pliku"_s, u"Bez pliku.html"_s, {}, {}},
                          Meta{u"a2"_s, u"Obraz"_s, u"brak.png"_s, {}, {}, {}, u"image"_s, u"image/png"_s},
                          Meta{u"a3"_s, u"Jest"_s, u"Jest.html"_s, {}, {}}});
        const ImportResult r = importZip(
                {{u"!!!meta.json"_s, meta}, {u"Jest.html"_s, page(u"Jest"_s, u"<p><img src=\"brak.png\"></p>"_s)}});
        QCOMPARE(iotest::pathsOf(r), sorted({u"Trilium/Bez pliku.md"_s, u"Trilium/Jest.md"_s}));
        QCOMPARE(iotest::textAt(r, u"Trilium/Bez pliku.md"_s), QString());
        QVERIFY(hasWarning(r, u"no file in it"_s));
        QVERIFY(hasWarning(r, u"attachments referenced by notes are missing"_s));
    }

    void namesUniqueIgnoringCase()
    {
        const QByteArray meta = metaJson({Meta{u"a1"_s, u"Dom"_s, u"Dom.html"_s, {}, {}},
                                          Meta{u"a2"_s, u"dom"_s, u"1_dom.html"_s, {}, {}},
                                          Meta{u"a3"_s,
                                               u"DOM"_s,
                                               u"2_DOM.html"_s,
                                               u"DOM"_s,
                                               {Meta{u"a4"_s, u"Pokój"_s, u"Pokój.html"_s, {}, {}}}},
                                          Meta{u"a5"_s, u"Spis"_s, u"Spis.html"_s, {}, {}}});
        const ImportResult r = importZip(
                {{u"!!!meta.json"_s, meta},
                 {u"Dom.html"_s, page(u"Dom"_s, u"<p>1</p>"_s)},
                 {u"1_dom.html"_s, page(u"dom"_s, u"<p>2</p>"_s)},
                 {u"2_DOM.html"_s, page(u"DOM"_s, u"<p>3</p>"_s)},
                 {u"DOM/Pokój.html"_s, page(u"Pokój"_s, u"<p>4</p>"_s)},
                 {u"Spis.html"_s,
                  page(u"Spis"_s, u"<p><a class=\"reference-link\" href=\"1_dom.html\">dom</a>, <a "
                                  "href=\"DOM/Pok%C3%B3j.html\">pokój</a>, <a href=\"#root/a3\">trzeci</a></p>"_s)}});
        QCOMPARE(iotest::pathsOf(r), sorted({u"Trilium/DOM 3.md"_s, u"Trilium/DOM 3/Pokój.md"_s, u"Trilium/Dom.md"_s,
                                             u"Trilium/Spis.md"_s, u"Trilium/dom 2.md"_s}));
        // The link text as written ("pokój", not the title "Pokój").
        QCOMPARE(iotest::textAt(r, u"Trilium/Spis.md"_s),
                 u"[[Trilium/dom 2|dom]], [[Trilium/DOM 3/Pokój|pokój]], [[Trilium/DOM 3|trzeci]]\n"_s);
    }

    void clonesInFolders()
    {
        // "Ludzie" has no content but holds a clone: it gets a page listing its children.
        const QByteArray meta = metaJson(
                {Meta{u"p1"_s,
                      u"Praca"_s,
                      u"Praca.html"_s,
                      u"Praca"_s,
                      {Meta{u"n1"_s, u"Anna"_s, u"Anna.html"_s, {}, {}}}},
                 Meta{u"l1"_s,
                      u"Ludzie"_s,
                      {},
                      u"Ludzie"_s,
                      {Meta{u"n2"_s, u"Bartek"_s, u"Bartek.html"_s, {}, {}},
                       Meta{u"n1"_s, u"Anna"_s, u"Anna.clone.html"_s, {}, {}, {}, u"text"_s, {}, u"Szefowa"_s, true}}},
                 Meta{u"x1"_s, u"Inne"_s, {}, u"Inne"_s, {Meta{u"n3"_s, u"Cecylia"_s, u"Cecylia.html"_s, {}, {}}}}});
        const ImportResult r = importZip({{u"!!!meta.json"_s, meta},
                                          {u"Praca.html"_s, page(u"Praca"_s, u"<p>praca</p>"_s)},
                                          {u"Praca/Anna.html"_s, page(u"Anna"_s, u"<p>Anna</p>"_s)},
                                          {u"Ludzie/Bartek.html"_s, page(u"Bartek"_s, u"<p>Bartek</p>"_s)},
                                          {u"Ludzie/Anna.clone.html"_s, page(u"Anna"_s, u"<p>This is a clone</p>"_s)},
                                          {u"Inne/Cecylia.html"_s, page(u"Cecylia"_s, u"<p>C</p>"_s)}});
        QCOMPARE(iotest::pathsOf(r),
                 sorted({u"Trilium/Inne/Cecylia.md"_s, u"Trilium/Ludzie.md"_s, u"Trilium/Ludzie/Bartek.md"_s,
                         u"Trilium/Praca.md"_s, u"Trilium/Praca/Anna.md"_s}));
        QCOMPARE(iotest::textAt(r, u"Trilium/Ludzie.md"_s),
                 u"- [[Trilium/Ludzie/Bartek|Bartek]]\n- [[Trilium/Praca/Anna|Szefowa - Anna]]\n"_s);
    }

    void labels_data()
    {
        QTest::addColumn<QJsonArray>("attributes");
        QTest::addColumn<QVariantMap>("expected");
        QTest::newRow("tags") << QJsonArray{label(u"ważne"_s), label(u"do zrobienia"_s), label(u"1234"_s)}
                              << QVariantMap{{u"tags"_s, QStringList{u"ważne"_s, u"do-zrobienia"_s}}};
        QTest::newRow("system labels dropped") << QJsonArray{label(u"iconClass"_s, u"bx bx-star"_s),
                                                             label(u"archived"_s),
                                                             label(u"sorted"_s),
                                                             label(u"cssClass"_s, u"x"_s),
                                                             label(u"shareAlias"_s, u"a"_s),
                                                             label(u"board:groupBy"_s, u"status"_s),
                                                             label(u"runOnNoteChange"_s),
                                                             label(u"widget"_s),
                                                             label(u"shared"_s),
                                                             label(u"bookZoomLevel"_s, u"2"_s),
                                                             label(u"noteInfoWidgetDisabled"_s)}
                                               << QVariantMap{{u"tags"_s, QStringList{u"shared"_s}}};
        QTest::newRow("properties") << QJsonArray{label(u"status"_s, u"w toku"_s), label(u"kolor"_s, u"zielony"_s),
                                                  label(u"kolor"_s, u"żółty"_s),
                                                  label(u"pageUrl"_s, u"https://x.org"_s)}
                                    << QVariantMap{{u"status"_s, u"w toku"_s},
                                                   {u"kolor"_s, QStringList{u"zielony"_s, u"żółty"_s}},
                                                   {u"source"_s, u"https://x.org"_s}};
        QTest::newRow("relations") << QJsonArray{relation(u"autor"_s, u"b1"_s), relation(u"dzień"_s, u"d1"_s),
                                                 relation(u"internalLink"_s, u"b1"_s), relation(u"template"_s, u"b1"_s),
                                                 relation(u"brak"_s, u"nieznana"_s)}
                                   << QVariantMap{{u"autor"_s, u"[[Trilium/Beata|Beata]]"_s},
                                                  {u"dzień"_s, u"[[2026-10-14]]"_s}};
        QTest::newRow("tags label merged") << QJsonArray{label(u"tags"_s, u"a, b c"_s), label(u"x"_s)}
                                           << QVariantMap{{u"tags"_s, QStringList{u"a"_s, u"b-c"_s, u"x"_s}}};
    }

    void labels()
    {
        QFETCH(QJsonArray, attributes);
        QFETCH(QVariantMap, expected);
        Meta note{u"a1"_s, u"Notatka"_s, u"Notatka.html"_s, {}, {}, attributes};
        const QByteArray meta = metaJson({note, Meta{u"b1"_s, u"Beata"_s, u"Beata.html"_s, {}, {}},
                                          Meta{u"d1"_s,
                                               u"14 - Środa"_s,
                                               u"14.html"_s,
                                               {},
                                               {},
                                               QJsonArray{label(u"dateNote"_s, u"2026-10-14"_s)}}});
        const ImportResult r = importZip({{u"!!!meta.json"_s, meta},
                                          {u"Notatka.html"_s, page(u"Notatka"_s, u"<p>x</p>"_s)},
                                          {u"Beata.html"_s, page(u"Beata"_s, u"<p>b</p>"_s)},
                                          {u"14.html"_s, page(u"14 - Środa"_s, u"<p>dzień</p>"_s)}});
        QCOMPARE(plain(frontmatterOf(iotest::textAt(r, u"Trilium/Notatka.md"_s))), plain(expected));
    }

    void calendar()
    {
        // Calendar, year and month notes are folders only; a day's child is a page in the mirrored
        // folder, linked from the day; an empty day makes no daily note.
        const QJsonArray dayLabel{label(u"dateNote"_s, u"2026-10-16"_s)};
        const QByteArray meta =
                metaJson({Meta{u"c1"_s,
                               u"Kalendarz"_s,
                               {},
                               u"Kalendarz"_s,
                               {Meta{u"y1"_s,
                                     u"2026"_s,
                                     {},
                                     u"2026"_s,
                                     {Meta{u"m1"_s,
                                           u"10 - Październik"_s,
                                           u"10 - Październik.html"_s,
                                           u"10 - Październik"_s,
                                           {Meta{u"d1"_s,
                                                 u"16 - Piątek"_s,
                                                 u"16 - Piątek.html"_s,
                                                 u"16 - Piątek"_s,
                                                 {Meta{u"s1"_s, u"Spotkanie"_s, u"Spotkanie.html"_s, {}, {}}},
                                                 dayLabel},
                                            Meta{u"d2"_s,
                                                 u"17 - Sobota"_s,
                                                 u"17 - Sobota.html"_s,
                                                 {},
                                                 {},
                                                 QJsonArray{label(u"dateNote"_s, u"2026-10-17"_s)}}},
                                           QJsonArray{label(u"monthNote"_s, u"2026-10"_s)}}},
                                     QJsonArray{label(u"yearNote"_s, u"2026"_s)}}},
                               QJsonArray{label(u"calendarRoot"_s), label(u"sorted"_s)}}});
        const QString month = u"Kalendarz/2026/10 - Październik/"_s;
        const ImportResult r = importZip(
                {{u"!!!meta.json"_s, meta},
                 {month + u"16 - Piątek.html"_s, page(u"16 - Piątek"_s, u"<p>piątek</p>"_s)},
                 {month + u"16 - Piątek/Spotkanie.html"_s, page(u"Spotkanie"_s, u"<p>o 10</p>"_s)},
                 {month + u"17 - Sobota.html"_s, page(u"17 - Sobota"_s, u""_s)},
                 {u"Kalendarz/2026/10 - Październik.html"_s, page(u"10 - Październik"_s, u"<p>plan miesiąca</p>"_s)}});
        QCOMPARE(iotest::pathsOf(r), sorted({u"Trilium/Kalendarz/2026/10 - Październik.md"_s,
                                             u"Trilium/Kalendarz/2026/10 - Październik/16 - Piątek/Spotkanie.md"_s,
                                             u"journals/2026-10-16.md"_s}));
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-16.md"_s),
                 u"piątek\n\n---\n\n- [[Trilium/Kalendarz/2026/10 - Październik/16 - Piątek/Spotkanie|Spotkanie]]\n"_s);
        // The month lists its days (the empty one is no daily note).
        QCOMPARE(iotest::textAt(r, u"Trilium/Kalendarz/2026/10 - Październik.md"_s),
                 u"plan miesiąca\n\n---\n\n- [[2026-10-16]]\n"_s);
    }

    void protectedNotes()
    {
        const QByteArray meta = metaJson({Meta{u"a1"_s,
                                               u"Jawna"_s,
                                               u"Jawna.html"_s,
                                               u"Jawna"_s,
                                               {Meta{u"p1"_s, u"[protected]"_s, {}, {}},
                                                Meta{u"p2"_s,
                                                     u"[protected]"_s,
                                                     {},
                                                     u"[protected]"_s,
                                                     {Meta{u"c1"_s, u"Dziecko"_s, u"Dziecko.html"_s, {}, {}}}}}}});
        const ImportResult r = importZip({{u"!!!meta.json"_s, meta},
                                          {u"Jawna.html"_s, page(u"Jawna"_s, u"<p><a href=\"Jawna/\">tajne</a></p>"_s)},
                                          {u"Jawna/[protected]/Dziecko.html"_s, page(u"Dziecko"_s, u"<p>d</p>"_s)}});
        QCOMPARE(iotest::pathsOf(r), sorted({u"Trilium/Jawna.md"_s, u"Trilium/Jawna/protected/Dziecko.md"_s}));
        QCOMPARE(iotest::textAt(r, u"Trilium/Jawna.md"_s), u"tajne\n"_s);
        QVERIFY(hasWarning(r, u"2 protected note"_s));
        QVERIFY(hasWarning(r, u"only their text was kept"_s));
    }

    void deepTree()
    {
        Meta leaf{u"n60"_s, u"Liść"_s, u"Liść.html"_s, {}, {}};
        QString path = u"Liść.html"_s;
        for (int k = 59; k >= 0; --k) {
            const QString title = u"Poziom "_s + QString::number(k);
            leaf = Meta{u"n"_s + QString::number(k), title, {}, title, {leaf}};
            path = title + u'/' + path;
        }
        const ImportResult r =
                importZip({{u"!!!meta.json"_s, metaJson({leaf})}, {path, page(u"Liść"_s, u"<p>dno</p>"_s)}});
        QCOMPARE(r.files.size(), 1);
        // "Trilium/" + 40 folders + the page.
        QCOMPARE(r.files.first().path.count(u'/'), 41);
        QVERIFY(hasWarning(r, u"nested too deeply"_s));
    }

    void largeNoteKeptAsFile()
    {
        const QByteArray big = page(u"Duża"_s, u"<p>"_s + QString(17 << 20, u'a') + u"</p>"_s);
        const ImportResult r =
                importZip({{u"!!!meta.json"_s, metaJson({Meta{u"a1"_s, u"Duża"_s, u"Duża.html"_s, {}, {}}})},
                           {u"Duża.html"_s, big}});
        QCOMPARE(iotest::textAt(r, u"Trilium/Duża.md"_s), u"[[assets/Trilium/Duża.html|Duża.html]]\n"_s);
        QCOMPARE(iotest::fileAt(r, u"assets/Trilium/Duża.html"_s)->data, big);
        QVERIFY(hasWarning(r, u"too large to convert"_s));
    }

    void imagesAndAttachments()
    {
        const QJsonArray attachments{QJsonObject{{u"attachmentId"_s, u"at1"_s},
                                                 {u"title"_s, u"skan"_s},
                                                 {u"role"_s, u"file"_s},
                                                 {u"mime"_s, u"application/pdf"_s},
                                                 {u"dataFileName"_s, u"Notatka_skan.pdf"_s}},
                                     QJsonObject{{u"attachmentId"_s, u"at2"_s},
                                                 {u"title"_s, u"wykres.png"_s},
                                                 {u"role"_s, u"image"_s},
                                                 {u"mime"_s, u"image/png"_s},
                                                 {u"dataFileName"_s, u"Notatka_wykres.png"_s}}};
        Meta note{u"a1"_s, u"Notatka"_s, u"Notatka.html"_s, {}, {}};
        note.attachments = attachments;
        const QString html =
                u"<p><img src=\"data:image/png;base64,UE5H\"> <img src=\"https://example.org/x.png\" alt=\"zdalny\"> "
                "<img src=\"file:///etc/passwd\"> <img src=\"api/attachments/at2/image/wykres.png\"></p>"
                "<p><a href=\"javascript:alert(1)\">skrypt</a> <script>alert(2)</script><iframe "
                "src=\"https://evil.example\"></iframe></p>"_s;
        const ImportResult r = importZip({{u"!!!meta.json"_s, metaJson({note})},
                                          {u"Notatka.html"_s, page(u"Notatka"_s, html)},
                                          {u"Notatka_skan.pdf"_s, "%PDF-1.4"},
                                          {u"Notatka_wykres.png"_s, "PNG"}});
        QCOMPARE(iotest::pathsOf(r),
                 sorted({u"Trilium/Notatka.md"_s, u"assets/Trilium/Notatka/image-1.png"_s,
                         u"assets/Trilium/Notatka/skan.pdf"_s, u"assets/Trilium/Notatka/wykres.png"_s}));
        QCOMPARE(iotest::textAt(r, u"Trilium/Notatka.md"_s),
                 u"![[assets/Trilium/Notatka/image-1.png]] ![zdalny](https://example.org/x.png) "
                 "![[assets/Trilium/Notatka/wykres.png]]\n\nskrypt\n\n![[assets/Trilium/Notatka/skan.pdf]]\n"_s);
        QCOMPARE(iotest::fileAt(r, u"assets/Trilium/Notatka/image-1.png"_s)->data, QByteArray("PNG"));
    }

    void reimportLinksStayInTheirImport()
    {
        // A second import into the same folder, into a vault that also has pages named like the
        // notes elsewhere: Trilium's links are exact, so every link names a page of its own import.
        const ImportResult first = importFixture(u"html.zip"_s);
        QSet<QString> vault;
        for (const ImportedFile &f : first.files)
            vault.insert(f.path.toCaseFolded());
        for (const QString &p : {u"Zadania.md"_s, u"Projekty.md"_s, u"Notatki/Jan Kowalski.md"_s})
            vault.insert(p.toCaseFolded());
        ImportOptions o = iotest::options(u"Trilium"_s);
        o.exists = [&](const VaultPath &p) { return vault.contains(p.toCaseFolded()); };
        const ImportResult second = importFiles({bytes(u"a.zip"_s, fixture(u"html.zip"_s))}, o);
        QSet<QString> own;
        for (const ImportedFile &f : second.files)
            own.insert(f.path.toCaseFolded());
        int links = 0;
        for (const ImportedFile &f : second.files) {
            if (!f.path.endsWith(u".md"_s))
                continue;
            for (const QString &target : wikiTargets(QString::fromUtf8(f.data))) {
                if (QDate::fromString(target, u"yyyy-MM-dd"_s).isValid())
                    continue; // a daily note
                const QString key = target.toCaseFolded();
                QVERIFY2(own.contains(key) || own.contains(key + u".md"_s), qPrintable(f.path + u" -> "_s + target));
                ++links;
            }
        }
        qInfo("%d links checked", links);
        QVERIFY(links > 40);
        QVERIFY(iotest::textAt(second, u"Trilium/Projekty 2/Kod/Notatka Markdown.md"_s)
                        .contains(u"[[Trilium/Projekty 2/Ogród plan budżet|planu ogrodu]]"_s));
        QCOMPARE(frontmatterOf(iotest::textAt(second, u"Trilium/Projekty 2/Ogród plan budżet.md"_s))
                         .value(u"autor"_s)
                         .toString(),
                 u"[[Trilium/Osoby/Jan Kowalski 2|Jan Kowalski]]"_s);
    }

    void diagramImagesBrulionCannotShow()
    {
        // Mermaid and mind-map SVGs draw their labels in <foreignObject> and style everything with
        // CSS: Qt shows black shapes. They are linked below the source instead of being embedded.
        const QByteArray mermaidSvg =
                "<svg xmlns=\"http://www.w3.org/2000/svg\"><style>#m .node rect{fill:#ECECFF}</style><g "
                "class=\"node\"><rect/><foreignObject width=\"60\" height=\"24\"><div "
                "xmlns=\"http://www.w3.org/1999/xhtml\"><span>Start</span></div></foreignObject></g></svg>";
        const QByteArray mindMapSvg = "<svg xmlns=\"http://www.w3.org/2000/svg\"><rect fill=\"#252526\"/>"
                                      "<foreignObject><div>Ogród</div></foreignObject></svg>";
        const QByteArray canvasSvg = "<svg xmlns=\"http://www.w3.org/2000/svg\"><style class=\"style-fonts\">"
                                     "@font-face{font-family:Virgil}</style><rect width=\"5\" height=\"5\"/></svg>";
        Meta mermaid{u"m1"_s, u"Flow"_s, u"Flow.dat"_s, {}, {}, {}, u"mermaid"_s, u"text/vnd.mermaid"_s};
        mermaid.attachments = {
                attachment(u"a1"_s, u"mermaid-export.svg"_s, u"image/svg+xml"_s, u"Flow_mermaid-export.svg"_s)};
        Meta mindMap{u"m2"_s, u"Mapa"_s, u"Mapa.json"_s, {}, {}, {}, u"mindMap"_s, u"application/json"_s};
        mindMap.attachments = {
                attachment(u"a2"_s, u"mindmap-export.svg"_s, u"image/svg+xml"_s, u"Mapa_mindmap-export.svg"_s)};
        Meta canvas{u"m3"_s, u"Szkic"_s, u"Szkic.json"_s, {}, {}, {}, u"canvas"_s, u"application/json"_s};
        canvas.attachments = {
                attachment(u"a3"_s, u"canvas-export.svg"_s, u"image/svg+xml"_s, u"Szkic_canvas-export.svg"_s)};
        const Meta text{u"t1"_s, u"Opis"_s, u"Opis.html"_s, {}, {}};
        const ImportResult r = importZip(
                {{u"!!!meta.json"_s, metaJson({mermaid, mindMap, canvas, text})},
                 {u"Flow.dat"_s, "graph TD\n    A[Start] --> B[Koniec]\n"},
                 {u"Flow_mermaid-export.svg"_s, mermaidSvg},
                 {u"Mapa.json"_s, R"({"nodeData":{"topic":"Ogród","children":[{"topic":"Warzywa"}]}})"},
                 {u"Mapa_mindmap-export.svg"_s, mindMapSvg},
                 {u"Szkic.json"_s, R"({"type":"excalidraw","elements":[]})"},
                 {u"Szkic_canvas-export.svg"_s, canvasSvg},
                 {u"Opis.html"_s, page(u"Opis"_s, u"<section class=\"include-note\" data-note-id=\"m1\">&nbsp;"
                                                  "</section><p><img src=\"Flow_mermaid-export.svg\"></p>"_s)}});
        QCOMPARE(iotest::textAt(r, u"Trilium/Flow.md"_s),
                 u"```mermaid\ngraph TD\n    A[Start] --> B[Koniec]\n```\n\n[[assets/Trilium/Flow.svg|Flow.svg]]\n"_s);
        QCOMPARE(
                iotest::textAt(r, u"Trilium/Mapa.md"_s),
                u"- Ogród\n\t- Warzywa\n\n[[assets/Trilium/Mapa.svg|Mapa.svg]]\n\n[[assets/Trilium/Mapa.json|Mapa.json]]\n"_s);
        // Excalidraw's SVGs render (their <style> holds only fonts).
        QCOMPARE(iotest::textAt(r, u"Trilium/Szkic.md"_s),
                 u"![[assets/Trilium/Szkic.svg]]\n\n[[assets/Trilium/Szkic.excalidraw|Szkic.excalidraw]]\n"_s);
        // An include-note box and an image of the diagram show its page (with the source).
        QCOMPARE(iotest::textAt(r, u"Trilium/Opis.md"_s), u"![[Trilium/Flow]]\n\n![[Trilium/Flow]]\n"_s);
        QCOMPARE(iotest::fileAt(r, u"assets/Trilium/Flow.svg"_s)->data, mermaidSvg);
    }

    void pageTitledLikeADay()
    {
        // A page "2026-10-16" next to the day note of 2026-10-16: the link to the day names the
        // daily note by its path, so it does not open the page.
        const QByteArray meta = metaJson({Meta{u"p1"_s, u"2026-10-16"_s, u"2026-10-16.html"_s, {}, {}},
                                          Meta{u"d1"_s,
                                               u"16 - Piątek"_s,
                                               u"16.html"_s,
                                               {},
                                               {},
                                               QJsonArray{label(u"dateNote"_s, u"2026-10-16"_s)}},
                                          Meta{u"n1"_s, u"Linki"_s, u"Linki.html"_s, {}, {}}});
        const ImportResult r =
                importZip({{u"!!!meta.json"_s, meta},
                           {u"2026-10-16.html"_s, page(u"2026-10-16"_s, u"<p>strona</p>"_s)},
                           {u"16.html"_s, page(u"16 - Piątek"_s, u"<p>dzień</p>"_s)},
                           {u"Linki.html"_s,
                            page(u"Linki"_s,
                                 u"<p><a href=\"2026-10-16.html\">strona</a>, <a href=\"16.html\">dzień</a></p>"_s)}});
        QCOMPARE(iotest::textAt(r, u"Trilium/Linki.md"_s),
                 u"[[Trilium/2026-10-16|strona]], [[journals/2026-10-16|2026-10-16]]\n"_s);
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-16.md"_s), u"dzień\n"_s);
    }

    void dayNoteTitles()
    {
        // A day note's own title beyond Trilium's "24 - Thursday" is kept as a heading.
        const QByteArray meta = metaJson({Meta{u"d1"_s,
                                               u"24 - Czwartek - Wigilia!"_s,
                                               u"24.html"_s,
                                               {},
                                               {},
                                               QJsonArray{label(u"dateNote"_s, u"2026-12-24"_s)}},
                                          Meta{u"d2"_s,
                                               u"25 - Piątek"_s,
                                               u"25.html"_s,
                                               {},
                                               {},
                                               QJsonArray{label(u"dateNote"_s, u"2026-12-25"_s)}}});
        const ImportResult r = importZip({{u"!!!meta.json"_s, meta},
                                          {u"24.html"_s, page(u"24"_s, u"<p>prezenty</p>"_s)},
                                          {u"25.html"_s, page(u"25"_s, u"<p>święta</p>"_s)}});
        QCOMPARE(iotest::textAt(r, u"journals/2026-12-24.md"_s), u"# Wigilia!\n\nprezenty\n"_s);
        QCOMPARE(iotest::textAt(r, u"journals/2026-12-25.md"_s), u"święta\n"_s);
    }

    void bookOfImages()
    {
        // A collection of images shows them; Trilium's view labels are no properties.
        const Meta book{u"b1"_s,
                        u"Zdjęcia z wycieczki"_s,
                        {},
                        u"Zdjęcia z wycieczki"_s,
                        {Meta{u"i1"_s, u"01.jpeg"_s, u"01.jpeg"_s, {}, {}, {}, u"image"_s, u"image/jpeg"_s},
                         Meta{u"i2"_s, u"02.jpeg"_s, u"02.jpeg"_s, {}, {}, {}, u"image"_s, u"image/jpeg"_s},
                         Meta{u"f1"_s, u"plan.pdf"_s, u"plan.pdf"_s, {}, {}, {}, u"file"_s, u"application/pdf"_s}},
                        QJsonArray{label(u"bookZoomLevel"_s, u"2"_s), label(u"viewType"_s, u"grid"_s)},
                        u"book"_s};
        const ImportResult r = importZip({{u"!!!meta.json"_s, metaJson({book})},
                                          {u"Zdjęcia z wycieczki/01.jpeg"_s, "JPEG1"},
                                          {u"Zdjęcia z wycieczki/02.jpeg"_s, "JPEG2"},
                                          {u"Zdjęcia z wycieczki/plan.pdf"_s, "%PDF"}});
        QCOMPARE(iotest::textAt(r, u"Trilium/Zdjęcia z wycieczki.md"_s),
                 u"- ![[assets/Trilium/Zdjęcia z wycieczki/01.jpeg]]\n- ![[assets/Trilium/Zdjęcia z "
                 u"wycieczki/02.jpeg]]\n"
                 "- [[assets/Trilium/Zdjęcia z wycieczki/plan.pdf|plan.pdf]]\n"_s);
    }

    void childLists()
    {
        // A page lists its children below its content (as Trilium's note list does); with
        // #hideChildrenOverview only what has no other place: clone placements, images, files.
        const auto tree = [](const QJsonArray &attributes) {
            return metaJson({Meta{u"p1"_s,
                                  u"Rodzic"_s,
                                  u"Rodzic.html"_s,
                                  u"Rodzic"_s,
                                  {Meta{u"c1"_s, u"Dziecko"_s, u"Dziecko.html"_s, {}, {}},
                                   Meta{u"i1"_s, u"foto.png"_s, u"foto.png"_s, {}, {}, {}, u"image"_s, u"image/png"_s},
                                   Meta{u"o1"_s, u"Inna"_s, u"Inna.clone.html"_s, {}, {}, {}, u"text"_s, {}, {}, true}},
                                  attributes},
                             Meta{u"o1"_s, u"Inna"_s, u"Inna.html"_s, {}, {}}});
        };
        const QList<FileEntry> files = {{u"Rodzic.html"_s, page(u"Rodzic"_s, u"<ul><li>punkt</li></ul>"_s)},
                                        {u"Rodzic/Dziecko.html"_s, page(u"Dziecko"_s, u"<p>d</p>"_s)},
                                        {u"Rodzic/foto.png"_s, "PNG"},
                                        {u"Inna.html"_s, page(u"Inna"_s, u"<p>i</p>"_s)}};
        ImportResult r = importZip(QList<FileEntry>{{u"!!!meta.json"_s, tree({})}} + files);
        // The rule keeps the content's list and the child list apart.
        QCOMPARE(iotest::textAt(r, u"Trilium/Rodzic.md"_s),
                 u"- punkt\n\n---\n\n- [[Trilium/Rodzic/Dziecko|Dziecko]]\n- ![[assets/Trilium/Rodzic/foto.png]]\n"
                 "- [[Trilium/Inna|Inna]]\n"_s);
        r = importZip(QList<FileEntry>{{u"!!!meta.json"_s, tree({label(u"hideChildrenOverview"_s)})}} + files);
        QCOMPARE(iotest::textAt(r, u"Trilium/Rodzic.md"_s),
                 u"- punkt\n\n---\n\n- ![[assets/Trilium/Rodzic/foto.png]]\n- [[Trilium/Inna|Inna]]\n"_s);
    }

    void emptyCollections()
    {
        // A calendar root without content and children (the journal is not in the export) and an
        // empty collection make no page.
        const QByteArray meta =
                metaJson({Meta{u"c1"_s,
                               u"Journal"_s,
                               u"Journal.dat"_s,
                               {},
                               {},
                               QJsonArray{label(u"calendarRoot"_s), label(u"viewType"_s, u"calendar"_s)},
                               u"book"_s},
                          Meta{u"b1"_s, u"Pusta kolekcja"_s, u"Pusta kolekcja.html"_s, {}, {}, {}, u"book"_s},
                          Meta{u"n1"_s, u"Notatka"_s, u"Notatka.html"_s, {}, {}}});
        const ImportResult r = importZip({{u"!!!meta.json"_s, meta},
                                          {u"Journal.dat"_s, QByteArray()},
                                          {u"Pusta kolekcja.html"_s, page(u"Pusta kolekcja"_s, QString())},
                                          {u"Notatka.html"_s, page(u"Notatka"_s, u"<p>x</p>"_s)}});
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Trilium/Notatka.md"_s}));
    }

    void manySameTitledSiblings()
    {
        // Thousands of untitled notes and of Trilium's default title in one folder: " 2", " 3" …
        // are found without trying every smaller number again.
        constexpr int Count = 10'000;
        std::vector<Meta> notes;
        QList<FileEntry> entries;
        for (int k = 0; k < Count; ++k) {
            const QString file = u"n"_s + QString::number(k) + u".html"_s;
            notes.push_back(Meta{u"n"_s + QString::number(k), k % 2 ? u"nowa notatka"_s : QString(), file, {}, {}});
            entries.append(FileEntry{file, page(u"x"_s, u"<p>x</p>"_s)});
        }
        entries.prepend(FileEntry{u"!!!meta.json"_s, metaJson(notes)});
        const InputFile zip = iotest::zip(u"duży.zip"_s, entries);
        QElapsedTimer timer;
        timer.start();
        const ImportResult r = importFiles({zip});
        const qint64 ms = timer.elapsed();
        qInfo("Imported %d same-titled notes in %lld ms", Count, ms);
        QCOMPARE(r.files.size(), Count);
        QVERIFY(iotest::fileAt(r, u"Trilium/Untitled.md"_s));
        QVERIFY(iotest::fileAt(r, u"Trilium/Untitled 5000.md"_s));
        QVERIFY(iotest::fileAt(r, u"Trilium/nowa notatka 5000.md"_s));
        QVERIFY2(ms < 30000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(ms)));
    }

    void longBacktickRuns()
    {
        // A code note and a mermaid note full of backticks: their fences are found in one pass.
        const QByteArray ticks(1'000'000, '`');
        const QByteArray meta = metaJson(
                {Meta{u"c1"_s, u"Kod"_s, u"Kod.py"_s, {}, {}, {}, u"code"_s, u"text/x-python"_s},
                 Meta{u"m1"_s, u"Diagram"_s, u"Diagram.dat"_s, {}, {}, {}, u"mermaid"_s, u"text/vnd.mermaid"_s}});
        QElapsedTimer timer;
        timer.start();
        const ImportResult r = importZip({{u"!!!meta.json"_s, meta},
                                          {u"Kod.py"_s, "x = 1\n" + ticks},
                                          {u"Diagram.dat"_s, "graph TD\n" + ticks}});
        QVERIFY2(timer.elapsed() < 20000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(iotest::textAt(r, u"Trilium/Kod.md"_s).startsWith(QString(1'000'001, u'`') + u"python\nx = 1\n"_s));
        QVERIFY(iotest::textAt(r, u"Trilium/Diagram.md"_s).startsWith(QString(1'000'001, u'`') + u"mermaid\n"_s));
    }

    void manyAttributes()
    {
        // Tens of thousands of labels on one note: the frontmatter is built in linear time.
        constexpr int Count = 30'000;
        QJsonArray attributes;
        for (int k = 0; k < Count; ++k) {
            attributes.append(label(u"kolor"_s, u"k"_s + QString::number(k)));
            attributes.append(label(u"tag"_s + QString::number(k)));
        }
        const QByteArray meta = metaJson({Meta{u"a1"_s, u"Notatka"_s, u"Notatka.html"_s, {}, {}, attributes}});
        QElapsedTimer timer;
        timer.start();
        const ImportResult r =
                importZip({{u"!!!meta.json"_s, meta}, {u"Notatka.html"_s, page(u"Notatka"_s, u"<p>x</p>"_s)}});
        QVERIFY2(timer.elapsed() < 20000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
        const QVariantMap fm = frontmatterOf(iotest::textAt(r, u"Trilium/Notatka.md"_s));
        QCOMPARE(fm.value(u"kolor"_s).toStringList().size(), Count);
        QCOMPARE(fm.value(u"kolor"_s).toStringList().last(), u"k"_s + QString::number(Count - 1));
        QCOMPARE(fm.value(u"tags"_s).toStringList().size(), Count);
    }

    void deepNestingOnAWorkerStack_data()
    {
        QTest::addColumn<QString>("html");
        QTest::newRow("mixed") << u"<aside class=\"admonition note\"><details><summary>s</summary><blockquote><ul><li>"
                                  "<p>a</p><p>b</p><section>"_s.repeated(3000)
                                          + u"dno"_s;
        QTest::newRow("collapsible blocks") << u"<details><summary>s</summary>"_s.repeated(3000) + u"dno"_s;
        QTest::newRow("lists with blocks") << u"<ul><li><p>a</p><p>b</p>"_s.repeated(3000) + u"dno"_s;
    }

    void deepNestingOnAWorkerStack()
    {
        // The deepest nesting the limits allow converts on a thread with half the stack of an
        // Android worker thread (about 1 MB).
        QFETCH(QString, html);
        QString out;
        std::unique_ptr<QThread> thread(
                QThread::create([&] { out = trilium::htmlNoteToMarkdown(html, testResolver()); }));
        thread->setStackSize(512 * 1024 * BRULION_TEST_SLOWDOWN);
        thread->start();
        if (!thread->wait(QDeadlineTimer(300'000))) {
            (void)thread.release(); // still running: left alone rather than aborting the test run
            QFAIL("the conversion did not finish");
        }
        QVERIFY2(out.contains(u"dno"_s), qPrintable(out.right(200)));
    }

    void longTitlesInADeepTree()
    {
        // 30 levels of 190-letter titles: folders stop growing at about 1 KB of path (Linux and
        // Android allow 4096 bytes for a whole path); deeper notes stay in an ancestor's folder.
        Meta leaf{u"n30"_s, u"Liść"_s, u"Liść.html"_s, {}, {}};
        QString path = u"Liść.html"_s;
        for (int k = 29; k >= 0; --k) {
            const QString dir = u"d"_s + QString::number(k);
            leaf = Meta{u"n"_s + QString::number(k), QString::number(k) + u' ' + QString(190, u'ż'), {}, dir, {leaf}};
            path = dir + u'/' + path;
        }
        const ImportResult r =
                importZip({{u"!!!meta.json"_s, metaJson({leaf})}, {path, page(u"Liść"_s, u"<p>dno</p>"_s)}});
        QCOMPARE(r.files.size(), 1);
        QVERIFY2(r.files.first().path.toUtf8().size() < 1500, qPrintable(r.files.first().path));
        QVERIFY(r.files.first().path.endsWith(u"/Liść.md"_s));
        QVERIFY(hasWarning(r, u"nested too deeply"_s));
    }

    void namesTheVaultIgnores()
    {
        // Folders named like the ones the vault skips ("logseq", "node_modules") get another
        // name; a page on a daily note's path too (an import into the vault root).
        const QByteArray meta = metaJson(
                {Meta{u"l1"_s, u"logseq"_s, {}, u"logseq"_s, {Meta{u"c1"_s, u"Strona"_s, u"Strona.html"_s, {}, {}}}},
                 Meta{u"m1"_s,
                      u"node_modules"_s,
                      u"node_modules.html"_s,
                      u"node_modules"_s,
                      {Meta{u"c2"_s, u"Moduł"_s, u"Moduł.html"_s, {}, {}}}},
                 Meta{u"j1"_s,
                      u"journals"_s,
                      {},
                      u"journals"_s,
                      {Meta{u"p1"_s, u"2026-10-16"_s, u"2026-10-16.html"_s, {}, {}}}},
                 Meta{u"d1"_s,
                      u"16 - Piątek"_s,
                      u"16.html"_s,
                      {},
                      {},
                      QJsonArray{label(u"dateNote"_s, u"2026-10-16"_s)}}});
        const ImportResult r =
                importFiles({iotest::zip(u"t.zip"_s, {{u"!!!meta.json"_s, meta},
                                                      {u"logseq/Strona.html"_s, page(u"Strona"_s, u"<p>s</p>"_s)},
                                                      {u"node_modules.html"_s, page(u"n"_s, u"<p>n</p>"_s)},
                                                      {u"node_modules/Moduł.html"_s, page(u"M"_s, u"<p>m</p>"_s)},
                                                      {u"journals/2026-10-16.html"_s, page(u"p"_s, u"<p>strona</p>"_s)},
                                                      {u"16.html"_s, page(u"d"_s, u"<p>dzień</p>"_s)}})},
                            iotest::options());
        QCOMPARE(iotest::pathsOf(r),
                 sorted({u"journals/2026-10-16 2.md"_s, u"journals/2026-10-16.md"_s, u"logseq 2/Strona.md"_s,
                         u"node_modules 2.md"_s, u"node_modules 2/Moduł.md"_s}));
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-16.md"_s), u"dzień\n"_s);
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-16 2.md"_s), u"strona\n"_s);
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(u'\n')));
    }

    void attachmentNamesStayInTheirFolder()
    {
        // Titles with "/" or ".." name files, not folders.
        Meta note{u"a1"_s, u"Notatka"_s, u"Notatka.html"_s, {}, {}};
        note.attachments = {attachment(u"at1"_s, u"a/b/c.png"_s, u"image/png"_s, u"Notatka_c.png"_s),
                            attachment(u"at2"_s, u"../../x.png"_s, u"image/png"_s, u"Notatka_x.png"_s),
                            attachment(u"at3"_s, u"C:\\Windows\\win.ini"_s, u"text/plain"_s, u"Notatka_w.ini"_s)};
        const Meta file{u"f1"_s,      u"Plik"_s,
                        u"plik.sh"_s, {},
                        {},           QJsonArray{label(u"originalFileName"_s, u"../../../../tmp/x.sh"_s)},
                        u"file"_s,    u"application/x-sh"_s};
        const ImportResult r = importZip({{u"!!!meta.json"_s, metaJson({note, file})},
                                          {u"Notatka.html"_s, page(u"Notatka"_s, u"<p>x</p>"_s)},
                                          {u"Notatka_c.png"_s, "C"},
                                          {u"Notatka_x.png"_s, "X"},
                                          {u"Notatka_w.ini"_s, "W"},
                                          {u"plik.sh"_s, "echo"}});
        QCOMPARE(iotest::pathsOf(r), sorted({u"Trilium/Notatka.md"_s, u"assets/Trilium/Notatka/C Windows win.ini"_s,
                                             u"assets/Trilium/Notatka/a b c.png"_s, u"assets/Trilium/Notatka/x.png"_s,
                                             u"assets/Trilium/tmp x.sh"_s}));
    }

    void windowsReservedTitles()
    {
        // Vaults sync to Windows too: names it reserves get a "_", the title stays an alias;
        // bidirectional controls are dropped from names.
        const QByteArray meta = metaJson({Meta{u"a1"_s, u"CON"_s, u"CON.html"_s, {}, {}},
                                          Meta{u"a2"_s, u"nul.txt"_s, u"nul.txt.html"_s, {}, {}},
                                          Meta{u"a3"_s, u"\u202Efdp.exe"_s, u"x.html"_s, {}, {}},
                                          Meta{u"a4"_s, u"Konsola"_s, u"Konsola.html"_s, {}, {}}});
        const ImportResult r = importZip({{u"!!!meta.json"_s, meta},
                                          {u"CON.html"_s, page(u"CON"_s, u"<p>1</p>"_s)},
                                          {u"nul.txt.html"_s, page(u"nul.txt"_s, u"<p>2</p>"_s)},
                                          {u"x.html"_s, page(u"x"_s, u"<p>3</p>"_s)},
                                          {u"Konsola.html"_s, page(u"Konsola"_s, u"<p>4</p>"_s)}});
        QCOMPARE(iotest::pathsOf(r), sorted({u"Trilium/CON_.md"_s, u"Trilium/Konsola.md"_s, u"Trilium/fdp.exe.md"_s,
                                             u"Trilium/nul_.txt.md"_s}));
        QCOMPARE(frontmatterOf(iotest::textAt(r, u"Trilium/CON_.md"_s)).value(u"aliases"_s).toStringList(),
                 QStringList({u"CON"_s}));
    }

    void damagedUtf8()
    {
        // Trilium writes UTF-8: one damaged byte is one replacement character, not a whole note
        // read in a single-byte code page.
        QByteArray html = page(u"Ogród"_s, u"<p>Zażółć gęślą jaźń</p>"_s);
        html.insert(html.indexOf("Za") + 2, '\xFF');
        QByteArray code = QString(u"# zażółć\nprint(\"gęślą\")\n"_s).toUtf8();
        code.insert(2, '\xFE');
        const QByteArray meta =
                metaJson({Meta{u"a1"_s, u"Ogród"_s, u"Ogród.html"_s, {}, {}},
                          Meta{u"c1"_s, u"Kod"_s, u"Kod.py"_s, {}, {}, {}, u"code"_s, u"text/x-python"_s}});
        const ImportResult r = importZip({{u"!!!meta.json"_s, meta}, {u"Ogród.html"_s, html}, {u"Kod.py"_s, code}});
        QCOMPARE(iotest::textAt(r, u"Trilium/Ogród.md"_s), u"Za\uFFFDżółć gęślą jaźń\n"_s);
        QCOMPARE(iotest::textAt(r, u"Trilium/Kod.md"_s), u"```python\n# \uFFFDzażółć\nprint(\"gęślą\")\n```\n"_s);
    }

    // --- content conversion -----------------------------------------------------------------------

    void htmlConversion_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("markdown");
        QTest::newRow("task states")
                << u"<ul class=\"todo-list\"><li data-trilium-task-state=\"doing\"><label><input type=\"checkbox\">"
                   "<span>w toku</span></label></li><li data-trilium-task-state=\"cancelled\"><label><input "
                   "type=\"checkbox\"><span>anulowane</span></label><ul class=\"todo-list\"><li><label><input "
                   "type=\"checkbox\" checked=\"checked\"><span>pod</span></label></li></ul></li>"
                   "<li data-trilium-task-state=\"maybe\"><label><input type=\"checkbox\"><span>może</span></label>"
                   "</li></ul>"_s
                << u"- [/] w toku\n- [-] anulowane\n\t- [x] pod\n- [ ] może"_s;
        QTest::newRow("admonition") << u"<aside class=\"admonition important\"><p>Ważne <em>bardzo</em></p><p>drugi "
                                       "akapit</p></aside><blockquote><p>cytat</p></blockquote>"_s
                                    << u"> [!important]\n> Ważne *bardzo*\n>\n> drugi akapit\n\n> cytat"_s;
        QTest::newRow("details open with nested admonition")
                << u"<details open><summary>Więcej *info*</summary><aside class=\"admonition tip\"><p>rada</p></aside>"
                   "</details>"_s
                << u"> [!note]+ Więcej \\*info\\*\n> > [!tip]\n> > rada"_s;
        QTest::newRow("math") << u"<p>a <span class=\"math-tex\">\\(x_1 &lt; y`\\)</span></p><span "
                                 "class=\"math-tex\">\\[\\int_0^1 x\\,dx\\]</span>"_s
                              << u"a `` $x_1 < y`$ ``\n\n```math\n\\int_0^1 x\\,dx\n```"_s;
        QTest::newRow("footnotes")
                << u"<p>tekst<span class=\"footnote-reference\" data-footnote-index=\"2\" data-footnote-id=\"ab\"><sup>"
                   "<a href=\"#fnab\">[2]</a></sup></span></p><ol class=\"footnote-section footnotes\"><li "
                   "class=\"footnote-item\" data-footnote-index=\"2\" data-footnote-id=\"ab\"><span "
                   "class=\"footnote-back-link\"><sup><strong><a href=\"#fnrefab\">^</a></strong></sup></span><div "
                   "class=\"footnote-content\"><p>przypis <a href=\"Inna.html\">link</a></p><p>dalej</p></div>"
                   "</li></ol>"_s
                << u"tekst[^2]\n\n[^2]: przypis [[Inna|link]]\n\n    dalej"_s;
        QTest::newRow("include note and previews")
                << u"<section class=\"include-note\" data-note-id=\"Ab12\" data-box-size=\"small\">&nbsp;</section>"
                   "<p><span class=\"link-mention\" data-url=\"https://x.org/?a=1&amp;b=2\" "
                   "data-title=\"X &amp; Y\"></span> i <span class=\"tn-icon bx bx-star\"></span>ikona</p>"
                   "<section class=\"link-embed\" data-url=\"https://y.org\"></section>"_s
                << u"![[note Ab12]]\n\n[X & Y](https://x.org/?a=1&b=2) i ikona\n\n<https://y.org>"_s;
        QTest::newRow("code languages")
                << u"<pre><code class=\"language-text-x-csrc\">int x;</code></pre><pre><code "
                   "class=\"language-text-x-trilium-auto\">auto</code></pre><pre><code "
                   "class=\"language-application-javascript-env-backend\">api.x()</code></pre>"_s
                << u"```c\nint x;\n```\n\n```\nauto\n```\n\n```javascript\napi.x()\n```"_s;
        QTest::newRow("reference and plain links")
                << u"<p><a class=\"reference-link\" href=\"Cel.html\">Stary tytuł</a> <a "
                   "href=\"Cel.html\">własny</a> <a href=\"https://example.org\">www</a></p>"_s
                << u"[[Cel]] [[Cel|własny]] [www](https://example.org)"_s;
        QTest::newRow("figure caption and page break")
                << u"<figure class=\"image\"><img src=\"a.png\"><figcaption>Podpis</figcaption></figure><div "
                   "class=\"page-break\" style=\"page-break-after:always;\"><span style=\"display:none;\">&nbsp;"
                   "</span></div><p>po</p>"_s
                << u"![[a.png]]\n\nPodpis\n\npo"_s;
        QTest::newRow("scripts and unsafe links")
                << u"<p><script>alert(1)</script><a href=\"javascript:alert(2)\">kliknij</a><img src=\"x\" "
                   "onerror=\"alert(3)\"></p><iframe src=\"https://evil.example\"></iframe>"_s
                << u"kliknij![[x]]"_s;
        QTest::newRow("forged markers") << u"<p><img src=\"trilium-raw:0\"> <a href=\"trilium-ref:x\">a</a></p>"_s
                                        << u"![[trilium-raw-:0]] [a](trilium-ref-:x)"_s;
        QTest::newRow("forged block markers") << u"<p>a\uE0F00\uE0F1b</p>"_s << u"a0b"_s;
        // Items holding several blocks keep them, indented below the marker (QTextDocument would
        // move them out of the list).
        QTest::newRow("list item with code")
                << u"<ol><li><p>Krok pierwszy</p><pre><code class=\"language-text-x-sh\">echo raz\n  wcięte  </code>"
                   "</pre></li><li><p>Krok drugi</p></li></ol>"_s
                << u"1. Krok pierwszy\n\n   ```sh\n   echo raz\n     wcięte  \n   ```\n2. Krok drugi"_s;
        QTest::newRow("list item starting with code")
                << u"<ol><li><pre><code>#include &lt;x&gt;  </code></pre><p>po</p></li></ol>"_s
                << u"1. ```\n   #include <x>  \n   ```\n\n   po"_s;
        QTest::newRow("list item with text around a paragraph")
                << u"<ul><li>nazwa to sh (<p>NB: /bin/sh bywa dowiązaniem</p>)</li><li>dalej</li></ul>"_s
                << u"- nazwa to sh (\n\n  NB: /bin/sh bywa dowiązaniem\n\n  )\n- dalej"_s;
        QTest::newRow("list items with a quote, an image and a nested list")
                << u"<ul><li>Krok 3<blockquote><p>cytat</p></blockquote></li><li><p>obraz</p><figure "
                   "class=\"image\"><img src=\"a.png\"></figure><ul><li>pod</li></ul></li></ul>"_s
                << u"- Krok 3\n\n  > cytat\n- obraz\n\n  ![[a.png]]\n\t- pod"_s;
        QTest::newRow("to-do items with blocks")
                << u"<ul class=\"todo-list\"><li><label class=\"todo-list__label\"><input type=\"checkbox\" "
                   "checked=\"checked\"><span class=\"todo-list__label__description\">zrobione</span></label>"
                   "<pre><code>kod</code></pre></li><li data-trilium-task-state=\"doing\"><label><input "
                   "type=\"checkbox\"><span>w toku</span></label><p>opis</p></li></ul>"_s
                << u"- [x] zrobione\n\n  ```\n  kod\n  ```\n- [/] w toku\n\n  opis"_s;
        QTest::newRow("empty to-do items left out")
                << u"<ul class=\"todo-list\"><li><label class=\"todo-list__label\"><input type=\"checkbox\" "
                   "disabled=\"disabled\"><span class=\"todo-list__label__description\">&nbsp;&nbsp;</span>"
                   "</label></li><li><label><input type=\"checkbox\" checked><span>zrobione</span></label></li></ul>"_s
                << u"- [x] zrobione"_s;
        QTest::newRow("lists and quotes next to each other")
                << u"<ol><li>a</li></ol><ol start=\"5\"><li>b</li><li>c</li></ol><ul><li>d</li></ul><ul><li>e</li>"
                   "</ul><blockquote><p>f</p></blockquote><blockquote><p>g</p></blockquote>"_s
                << u"1. a\n\n5) b\n6) c\n\n- d\n\n* e\n\n> f\n\n> g"_s;
        QTest::newRow("quote in a table stays in its cell")
                << u"<table><tr><td><blockquote><p>q</p></blockquote></td><td>x</td></tr></table>"_s
                << u"| q | x |\n| --- | --- |"_s;
        // Footnotes of notes Trilium imported from Markdown.
        QTest::newRow("footnotes of an imported note")
                << u"<p>tekst<a\n  href=\"#fn2b8\"><sup>[1]</sup></a> i dalej</p><ol><li><p><a "
                   "href=\"#fnref2b8\"><strong><sup>^</sup></strong></a></p><p><a "
                   "href=\"https://example.org/x\">źródło</a></p></li></ol>"_s
                << u"tekst[^1] i dalej\n\n[^1]: [źródło](https://example.org/x)"_s;
        // Pretty-printed pages: the indentation after `</pre>` is no part of the code.
        QTest::newRow("code of a pretty-printed page")
                << u"<p>a</p><pre><code>x = 1</code></pre>\n        <p>b</p>"_s << u"a\n\n```\nx = 1\n```\n\nb"_s;
        // Trilium has no inline tags: hashtag-like text stays text.
        QTest::newRow("hashtags")
                << u"<p>Ukryta przez #archived, kolor #a13d5f, kanał #node.js, numer #2, # spacja, C#, (#nawias) i "
                   "<code>#kod</code></p><p>#początek</p>"_s
                << u"Ukryta przez \\#archived, kolor \\#a13d5f, kanał \\#node.js, numer #2, # spacja, C#, (\\#nawias) "
                   u"i "
                   "`#kod`\n\n\\#początek"_s;
        QTest::newRow("code fences longer than the code's backticks")
                << u"<pre><code>a ```` b</code></pre><p><span class=\"math-tex\">\\(x``y\\)</span></p>"_s
                << u"`````\na ```` b\n`````\n\n``` $x``y$ ```"_s;
    }

    void htmlConversion()
    {
        QFETCH(QString, html);
        QFETCH(QString, markdown);
        QCOMPARE(trilium::htmlNoteToMarkdown(html, testResolver()), markdown);
    }

    void pageBody()
    {
        QCOMPARE(trilium::noteBody(QString::fromUtf8(page(u"T"_s, u"<p>x</p><div>y</div>"_s))),
                 u"<p>x</p><div>y</div>"_s);
        QCOMPARE(trilium::noteBody(u"<html><body><h1 data-trilium-h1>T</h1><p>x</p></body></html>"_s), u"<p>x</p>"_s);
        QCOMPARE(trilium::noteBody(u"<p>fragment</p>"_s), u"<p>fragment</p>"_s);
    }

    void markdownConversion_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QString>("expected");
        QTest::newRow("title line removed") << u"# Tytuł\r\nTreść\n"_s << u"Treść"_s;
        QTest::newRow("other heading kept") << u"# Inny\nTreść"_s << u"# Inny\nTreść"_s;
        QTest::newRow("callout type") << u"> [!WARNING]\n> Uwaga"_s << u"> [!warning]\n> Uwaga"_s;
        QTest::newRow("task states") << u"- [?] może\n- [/] w toku\n    - [!] własny"_s
                                     << u"- [ ] może\n- [/] w toku\n    - [ ] własny"_s;
        QTest::newRow("math") << u"Wzór $E=mc^2$ i $$a$$, ale $5 i $10.\n\n$$\\sum x$$"_s
                              << u"Wzór $E=mc^2$ i $$a$$, ale $5 i $10.\n\n```math\n\\sum x\n```"_s;
        QTest::newRow("math of 0.63") << u"Wzór \\\\(E=mc^2\\\\)."_s << u"Wzór $E=mc^2$."_s;
        QTest::newRow("single tilde strike")
                << u"~skreślone~ i ~~podwójne~~, około ~5 km"_s << u"~~skreślone~~ i ~~podwójne~~, około ~5 km"_s;
        QTest::newRow("raw html escaped, autolinks kept")
                << u"Znaki <script>alert(1)</script> i <b> oraz <https://x.org> <a@b.pl>"_s
                << u"Znaki \\<script>alert(1)\\</script> i \\<b> oraz <https://x.org> <a@b.pl>"_s;
        QTest::newRow("inline html islands")
                << u"H<sub>2</sub>O, <kbd>Ctrl</kbd>, <span style=\"color:red\">czerwony</span>, ikona "
                   "<span class=\"tn-icon bx bx-star\"></span> tu, <a class=\"reference-link\" "
                   "href=\"Cel.md\">Cel</a>"_s
                << u"H2O, `Ctrl`, czerwony, ikona tu, [[Cel]]"_s;
        QTest::newRow("html block") << u"Przed\n<figure class=\"image\"><img src=\"a.png\"></figure><section "
                                       "class=\"include-note\" data-note-id=\"N1\">&nbsp;</section>\nPo"_s
                                    << u"Przed\n\n![[a.png]]\n\n![[note N1]]\n\nPo"_s;
        QTest::newRow("details block") << u"<details>\n    <summary>Więcej</summary>\n    <p>Ukryte.</p>\n</details>"_s
                                       << u"> [!note]- Więcej\n> Ukryte."_s;
        QTest::newRow("footnotes")
                << u"Tekst<sup><a href=\"#fnab\">[1]</a></sup>.\n\n1.  <sup><strong><a "
                   "href=\"#fnrefab\">^</a></strong></sup>\n    \n    Przypis _ważny_.\n    \n    Drugi akapit."_s
                << u"Tekst[^1].\n\n[^1]: Przypis _ważny_.\n\n    Drugi akapit."_s;
        QTest::newRow("links") << u"[tekst](Cel.md) ![](obraz.png) [www](https://x.org) [stary](Plik z spacjami.txt) "
                                  "[poza](#root/abc/def)"_s
                               << u"[[Cel|tekst]] ![[obraz.png]] [www](https://x.org) [[Plik z spacjami|stary]] poza"_s;
        QTest::newRow("code untouched")
                << u"```text-x-python\nx = \"<b>$a$</b>\"  \n```\n`$b$ <i>`\n~~~csrc\n~x~\n~~~"_s
                << u"```python\nx = \"<b>$a$</b>\"  \n```\n`$b$ <i>`\n~~~c\n~x~\n~~~"_s;
        // zadam/trilium 0.6x: Turndown escaped the TeX (`\` -> `\\`, `_` -> `\_`, `[` -> `\[`).
        QTest::newRow("math of 0.63 unescaped")
                << u"Makro \\\\(% \\\\f is #1f(#2) \\\\int\\_{-\\\\infty}^\\\\infty \\\\hat\\\\xi\\\\,e^{2 \\\\pi i}\\\\) koniec"_s
                << u"Makro $% \\f is #1f(#2) \\int_{-\\infty}^\\infty \\hat\\xi\\,e^{2 \\pi i}$ koniec"_s;
        QTest::newRow("display math of 0.63") << u"Przed\n\n\\\\\\[\\\\displaystyle \\\\frac{1}{2\\_a}\\\\\\]\n\nPo"_s
                                              << u"Przed\n\n```math\n\\displaystyle \\frac{1}{2_a}\n```\n\nPo"_s;
        QTest::newRow("display math blocks on one line")
                << u"$$a$$ $$\\Gamma(n)$$"_s << u"```math\na\n```\n\n```math\n\\Gamma(n)\n```"_s;
        QTest::newRow("inline math before a letter") << u"$x$y oraz $5 i $10"_s << u"$x$y oraz $5 i $10"_s;
        QTest::newRow("math with angle brackets") << u"$a<b$ i <b>x</b> <i"_s << u"$a<b$ i **x** \\<i"_s;
        QTest::newRow("footnotes of an imported note")
                << u"Tekst.[<sup>[1]</sup>](#fn2b8)\n\n1.  [**<sup>^</sup>**](#fnref2b8)\n    \n    "
                   "[https://x.org/a](https://x.org/a)"_s
                << u"Tekst.[^1]\n\n[^1]: [https://x.org/a](https://x.org/a)"_s;
        QTest::newRow("bracketed superscript")
                << u"Indeks<sup>[2]</sup> i H<sub>2</sub>O"_s << u"Indeks\\[2\\] i H2O"_s;
        QTest::newRow("figures in a table row")
                << u"| <figure class=\"image\"><img src=\"a.png\"></figure> | tekst |\n| --- | --- |\n| x <figure "
                   "class=\"image\"><img src=\"b.png\"><figcaption>Podpis</figcaption></figure> | y |"_s
                << u"| ![[a.png]] | tekst |\n| --- | --- |\n| x ![[b.png]] Podpis | y |"_s;
        QTest::newRow("ordered lists next to each other")
                << u"1.  a\n2.  b\n\n5.  c\n6.  d\n\n7.  e\n\nakapit\n\n1.  f"_s
                << u"1.  a\n2.  b\n\n5)  c\n6)  d\n\n7)  e\n\nakapit\n\n1.  f"_s;
        QTest::newRow("empty to-do items left out") << u"- [ ] \n- [x] zrobione\n- [ ] \u00A0\u00A0\n    - [ ] pod"_s
                                                    << u"- [x] zrobione\n-\n    - [ ] pod"_s;
        QTest::newRow("hashtags") << u"Tag #archived, #2, [link](#frag), [[Cel#Sekcja]] i `#kod`\n#początek"_s
                                  << u"Tag \\#archived, #2, [[#frag|link]], [[Cel#Sekcja]] i `#kod`\n\\#początek"_s;
    }

    void markdownConversion()
    {
        QFETCH(QString, markdown);
        QFETCH(QString, expected);
        QCOMPARE(trilium::markdownNoteToMarkdown(markdown, u"Tytuł"_s, testResolver()), expected);
    }

    void pathologicalContent_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<bool>("markdown");
        QTest::newRow("nested spans") << u"<span>"_s.repeated(5000) + u"x"_s << false;
        QTest::newRow("nested admonitions") << u"<aside class=\"admonition note\"><p>a</p>"_s.repeated(3000) << false;
        QTest::newRow("nested details") << u"<details><summary>s</summary><p>a</p>"_s.repeated(3000) << false;
        QTest::newRow("nested sections") << u"<section><aside>"_s.repeated(3000) << false;
        // QTextDocument itself needs time quadratic in the number of list items (20000: seconds).
        QTest::newRow("task states without checkboxes")
                << u"<ul><li data-trilium-task-state=\"doing\">"_s.repeated(2000) << false;
        QTest::newRow("unclosed math and footnotes")
                << u"<span class=\"math-tex\">\\(x"_s.repeated(5000)
                           + u"<ol class=\"footnote-section\"><li>"_s.repeated(3000)
                << false;
        QTest::newRow("markdown line of islands") << u"<span>a</span><sub>"_s.repeated(20000) << true;
        QTest::newRow("markdown unclosed blocks") << u"<details>\n"_s.repeated(20000) << true;
        QTest::newRow("markdown math and tildes") << u"$a$ ~b~ \\\\(c\\\\) "_s.repeated(20000) << true;
        // About 1 MB nested without end tags (QTextDocument's time grows with the square of the
        // depth; the nesting is cut at 64 levels).
        QTest::newRow("deep spans") << u"<span>"_s.repeated(150'000) + u"x"_s << false;
        QTest::newRow("deep divs") << u"<div>"_s.repeated(150'000) + u"x"_s << false;
        QTest::newRow("deep unclosed lists") << u"<ol><li>"_s.repeated(40'000) + u"x"_s << false;
        QTest::newRow("deep lists with blocks") << u"<ul><li><p>a</p><p>b</p>"_s.repeated(20'000) << false;
        // Long backtick runs: fences and code spans grow in one pass.
        QTest::newRow("backticks in code") << u"<pre><code>"_s + QString(1'000'000, u'`') + u"</code></pre>"_s << false;
        QTest::newRow("backticks in math")
                << u"<span class=\"math-tex\">\\("_s + QString(1'000'000, u'`') + u"\\)</span>"_s << false;
        QTest::newRow("markdown backticks in math") << u"$$"_s + QString(1'000'000, u'`') + u"$$"_s << true;
        QTest::newRow("markdown display math of 0.63") << u"\\\\\\[a\\\\\\] "_s.repeated(100'000) + u"x"_s << true;
    }

    void pathologicalContent()
    {
        // Hostile notes never crash and stay (close to) linear: a generous bound.
        QFETCH(QString, content);
        QFETCH(bool, markdown);
        QElapsedTimer timer;
        timer.start();
        if (markdown)
            trilium::markdownNoteToMarkdown(content, u"T"_s, testResolver());
        else
            trilium::htmlNoteToMarkdown(content, testResolver());
        QVERIFY2(timer.elapsed() < 20000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
    }

    void manyNotes()
    {
        // A large export: 3000 notes in 30 folders, each linking to the previous one, with a
        // to-do list and a table — every link resolves, the import stays fast.
        constexpr int Folders = 30;
        constexpr int PerFolder = 100;
        std::vector<Meta> folders;
        QList<FileEntry> entries;
        for (int f = 0; f < Folders; ++f) {
            Meta folder{u"f"_s + QString::number(f),
                        u"Folder "_s + QString::number(f),
                        {},
                        u"Folder "_s + QString::number(f)};
            for (int k = 0; k < PerFolder; ++k) {
                const int n = f * PerFolder + k;
                const QString title = u"Notatka "_s + QString::number(n);
                folder.children.push_back(Meta{u"n"_s + QString::number(n), title, title + u".html"_s, {}});
                const QString previous = n == 0 ? QString() : u"#root/f0/n"_s + QString::number(n - 1);
                entries.append(
                        FileEntry{folder.title + u'/' + title + u".html"_s,
                                  page(title, u"<p>Treść notatki "_s + QString::number(n)
                                                      + u" — zażółć gęślą jaźń. <a href=\""_s + previous
                                                      + u"\">poprzednia</a></p><ul class=\"todo-list\"><li><label>"
                                                        "<input type=\"checkbox\"><span>zadanie</span></label></li>"
                                                        "</ul><figure class=\"table\"><table><tr><td>a</td><td>b</td>"
                                                        "</tr></table></figure>"_s)});
            }
            folders.push_back(std::move(folder));
        }
        entries.prepend(FileEntry{u"!!!meta.json"_s, metaJson(folders)});
        const InputFile zip = iotest::zip(u"duży.zip"_s, entries);
        QElapsedTimer timer;
        timer.start();
        const ImportResult r = importFiles({zip});
        const qint64 ms = timer.elapsed();
        qInfo("Imported %d notes in %lld ms", Folders * PerFolder, ms);
        QCOMPARE(r.files.size(), Folders * PerFolder);
        QCOMPARE(iotest::textAt(r, u"Trilium/Folder 29/Notatka 2999.md"_s),
                 u"Treść notatki 2999 — zażółć gęślą jaźń. [[Trilium/Folder 29/Notatka 2998|poprzednia]]\n\n"
                 "- [ ] zadanie\n\n| a | b |\n| --- | --- |\n"_s);
        QVERIFY(r.warnings.isEmpty());
        QVERIFY2(ms < 60000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(ms)));
    }

    void languages_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("mime");
        QTest::addColumn<QString>("language");
        QTest::newRow("python") << u"text/x-python"_s << true << u"python"_s;
        QTest::newRow("frontend js") << u"application/javascript;env=frontend"_s << true << u"javascript"_s;
        QTest::newRow("plain") << u"text/plain"_s << true << QString();
        QTest::newRow("c++") << u"text/x-c++src"_s << true << u"cpp"_s;
        QTest::newRow("unknown mime") << u"text/x-zig"_s << true << u"zig"_s;
        QTest::newRow("unknown src mime") << u"text/x-foosrc"_s << true << u"foo"_s;
        QTest::newRow("mermaid") << u"text/vnd.mermaid"_s << true << u"mermaid"_s;
        QTest::newRow("sqlite trilium") << u"text/x-sqlite; schema=trilium"_s << true << u"sql"_s;
        QTest::newRow("class python") << u"text-x-python"_s << false << u"python"_s;
        QTest::newRow("class csrc") << u"csrc"_s << false << u"c"_s;
        QTest::newRow("class auto") << u"text-x-trilium-auto"_s << false << QString();
        QTest::newRow("class plain") << u"plain"_s << false << QString();
        QTest::newRow("class backend") << u"application-javascript-env-backend"_s << false << u"javascript"_s;
        QTest::newRow("fence python") << u"python"_s << false << u"python"_s;
        QTest::newRow("fence custom") << u"calc"_s << false << u"calc"_s;
        QTest::newRow("hostile") << u"x\"><script>"_s << false << u"xscript"_s;
    }

    void languages()
    {
        QFETCH(QString, input);
        QFETCH(bool, mime);
        QFETCH(QString, language);
        QCOMPARE(mime ? trilium::languageForMime(input) : trilium::languageForCodeClass(input), language);
    }
};

QTEST_MAIN(TestIoImportTrilium)
#include "tst_io_import_trilium.moc"
