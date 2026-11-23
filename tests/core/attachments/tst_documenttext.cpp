// The text of documents for the search (attachments/documenttext.h, docs/UI.md "Searching
// attachments"): DOCX, PPTX, ODT and EPUB built here from their XML, Polish text, reading order,
// what is left out; limits and hostile files (a "zip bomb", a forged size, millions of entries,
// entities, encrypted and unsupported parts, truncated archives and XML, random damage); the record
// made of it and its search. The XML is written with single-quoted attributes in plain string
// literals (moc reads this file).
#include "attachments/attachments.h"
#include "io/zip.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>

using namespace brulion;
using namespace brulion::attachments;
using namespace Qt::StringLiterals;
using Status = DocumentText::Status;

namespace {

const QString W = u"xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main' "
                  "xmlns:mc='http://schemas.openxmlformats.org/markup-compatibility/2006'"_s;
const QString A = u"xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main' "
                  "xmlns:p='http://schemas.openxmlformats.org/presentationml/2006/main' "
                  "xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships'"_s;
const QString Rels = u"xmlns='http://schemas.openxmlformats.org/package/2006/relationships'"_s;
const QString Odf = u"xmlns:office='urn:oasis:names:tc:opendocument:xmlns:office:1.0' "
                    "xmlns:text='urn:oasis:names:tc:opendocument:xmlns:text:1.0' "
                    "xmlns:dc='http://purl.org/dc/elements/1.1/'"_s;

io::FileEntry entry(const QString &path, const QString &xml)
{
    return {path, xml.toUtf8()};
}

QByteArray zip(const QList<io::FileEntry> &entries)
{
    return io::writeZip(entries);
}

std::optional<DocumentText> read(const QByteArray &bytes, DocumentFormat format, const DocumentLimits &limits = {},
                                 const std::function<bool()> &cancelled = {})
{
    QBuffer device;
    device.setData(bytes);
    device.open(QIODevice::ReadOnly);
    return extractDocumentText(device, format, limits, cancelled);
}

DocumentText mustRead(const QByteArray &bytes, DocumentFormat format, const DocumentLimits &limits = {})
{
    const std::optional<DocumentText> text = read(bytes, format, limits);
    return text ? *text : DocumentText{};
}

QString docxBody(const QString &body)
{
    return u"<?xml version='1.0' encoding='UTF-8' standalone='yes'?><w:document "_s + W + u"><w:body>"_s + body
           + u"</w:body></w:document>"_s;
}

QString para(const QString &text)
{
    return u"<w:p><w:r><w:t xml:space='preserve'>"_s + text.toHtmlEscaped() + u"</w:t></w:r></w:p>"_s;
}

QByteArray docx(const QString &body, const QList<io::FileEntry> &more = {})
{
    QList<io::FileEntry> entries = {
            entry(u"[Content_Types].xml"_s, u"<Types xmlns='http://schemas.openxmlformats.org/package/2006/content-types'/>"_s),
            entry(u"_rels/.rels"_s,
                  u"<Relationships "_s + Rels
                          + u"><Relationship Id='rId1' Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument' Target='word/document.xml'/></Relationships>"_s),
            entry(u"word/document.xml"_s, docxBody(body)),
            {u"word/media/image1.png"_s, QByteArray(2000, '\x7f')},
    };
    entries += more;
    return zip(entries);
}

QString slide(const QStringList &paragraphs)
{
    QString body;
    for (const QString &p : paragraphs)
        body += u"<a:p><a:r><a:t>"_s + p.toHtmlEscaped() + u"</a:t></a:r></a:p>"_s;
    return u"<p:sld "_s + A + u"><p:cSld><p:spTree><p:sp><p:txBody>"_s + body + u"</p:txBody></p:sp></p:spTree></p:cSld></p:sld>"_s;
}

QList<io::FileEntry> slideEntries()
{
    // Listed in the order 2, 10, 1 (the reading order is the list's, not the file names').
    return {
            entry(u"ppt/presentation.xml"_s,
                  u"<p:presentation "_s + A
                          + u"><p:sldIdLst><p:sldId id='256' r:id='rId5'/><p:sldId id='257' r:id='rId9'/><p:sldId id='258' r:id='rId3'/></p:sldIdLst></p:presentation>"_s),
            entry(u"ppt/_rels/presentation.xml.rels"_s,
                  u"<Relationships "_s + Rels
                          + u"><Relationship Id='rId3' Type='x/slide' Target='slides/slide1.xml'/>"
                            "<Relationship Id='rId5' Type='x/slide' Target='slides/slide2.xml'/>"
                            "<Relationship Id='rId9' Type='x/slide' Target='/ppt/slides/slide10.xml'/></Relationships>"_s),
            entry(u"ppt/slides/slide1.xml"_s, slide({u"Pierwszy slajd"_s, u"Żółć i gęślą"_s})),
            entry(u"ppt/slides/slide2.xml"_s, slide({u"Drugi slajd"_s, u"Zażółć gęślą jaźń"_s})),
            entry(u"ppt/slides/slide10.xml"_s,
                  u"<p:sld "_s + A + u"><p:cSld><p:spTree><p:sp><p:txBody><a:p><a:r><a:t>Dziesiąty</a:t></a:r><a:br/><a:r><a:t>po łamaniu</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>"_s),
    };
}

QByteArray odt(const QString &body, const QList<io::FileEntry> &more = {})
{
    QList<io::FileEntry> entries = {
            {u"mimetype"_s, "application/vnd.oasis.opendocument.text"},
            entry(u"content.xml"_s,
                  u"<office:document-content "_s + Odf + u"><office:body><office:text>"_s + body
                          + u"</office:text></office:body></office:document-content>"_s),
            entry(u"META-INF/manifest.xml"_s, u"<manifest/>"_s),
    };
    entries += more;
    return zip(entries);
}

QByteArray epub(const QList<io::FileEntry> &more = {}, const QString &spine = {})
{
    const QString itemrefs = spine.isEmpty() ? u"<itemref idref='ch1'/><itemref idref='ncx'/><itemref idref='ch2'/>"_s
                                             : spine;
    QList<io::FileEntry> entries = {
            {u"mimetype"_s, "application/epub+zip"},
            entry(u"META-INF/container.xml"_s,
                  u"<container xmlns='urn:oasis:names:tc:opendocument:xmlns:container'><rootfiles>"
                  "<rootfile full-path='OEBPS/content.opf' media-type='application/oebps-package+xml'/>"
                  "</rootfiles></container>"_s),
            entry(u"OEBPS/content.opf"_s,
                  u"<package xmlns='http://www.idpf.org/2007/opf'><manifest>"
                  "<item id='ch2' href='text/ch2.xhtml' media-type='application/xhtml+xml'/>"
                  "<item id='ch1' href='text/rozdzia%C5%82%201.xhtml#start' media-type='application/xhtml+xml'/>"
                  "<item id='ncx' href='toc.ncx' media-type='application/x-dtbncx+xml'/>"
                  "<item id='css' href='style.css' media-type='text/css'/></manifest><spine toc='ncx'>"_s
                          + itemrefs + u"</spine></package>"_s),
            entry(u"OEBPS/text/ch2.xhtml"_s,
                  u"<html xmlns='http://www.w3.org/1999/xhtml'><body><h1>Rozdział drugi</h1><p>Wąż i jeż.</p></body></html>"_s),
            entry(u"OEBPS/text/rozdział 1.xhtml"_s,
                  u"<?xml version='1.0'?><!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.1//EN' 'http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd'>"
                  "<html xmlns='http://www.w3.org/1999/xhtml'><head><title>Tytuł głowy</title>"
                  "<style>p { color: red }</style><script>var ukryte = 'skrypt';</script></head>"
                  "<body><h1>Rozdział pierwszy</h1><p>Zażółć&#160;gęślą <b>jaźń</b> i&#322;&#261;ka.</p>"
                  "<p>Druga<br/>linia</p><svg><text>napis svg</text></svg></body></html>"_s),
            entry(u"OEBPS/toc.ncx"_s, u"<ncx><navMap><navLabel><text>Spis</text></navLabel></navMap></ncx>"_s),
            entry(u"OEBPS/style.css"_s, u"p { color: blue }"_s),
    };
    entries += more;
    return zip(entries);
}

/// The headers of the central directory whose name is `name`: `patch` is given the offset of each.
void patchCentral(QByteArray &zipBytes, const QByteArray &name, const std::function<void(char *)> &patch)
{
    qsizetype at = 0;
    bool found = false;
    while ((at = zipBytes.indexOf("PK\x01\x02", at)) >= 0) {
        const auto *h = reinterpret_cast<const unsigned char *>(zipBytes.constData() + at);
        const int length = h[28] | (h[29] << 8);
        if (zipBytes.mid(at + 46, length) == name) {
            patch(zipBytes.data() + at);
            found = true;
        }
        at += 4;
    }
    QVERIFY2(found, name.constData());
}

void putU32(char *p, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = char((v >> (8 * i)) & 0xFF);
}

} // namespace

class TestDocumentText : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- names and kinds ----
    void formats_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<int>("format"); // -1: none
        QTest::newRow("docx") << u"zał/Umowa.docx"_s << int(DocumentFormat::Docx);
        QTest::newRow("DOCX upper") << u"UMOWA.DOCX"_s << int(DocumentFormat::Docx);
        QTest::newRow("pptx") << u"Wykład 3.pptx"_s << int(DocumentFormat::Pptx);
        QTest::newRow("odt") << u"a/b.odt"_s << int(DocumentFormat::Odt);
        QTest::newRow("epub") << u"książka.epub"_s << int(DocumentFormat::Epub);
        QTest::newRow("only a suffix") << u".docx"_s << -1;
        QTest::newRow("doc") << u"stare.doc"_s << -1;
        QTest::newRow("macro docm") << u"makro.docm"_s << -1;
        QTest::newRow("xlsx") << u"arkusz.xlsx"_s << -1;
        QTest::newRow("a note") << u"Notatka.md"_s << -1;
        QTest::newRow("a docx folder") << u"docx/Notatka.md"_s << -1;
        QTest::newRow("empty") << QString() << -1;
    }
    void formats()
    {
        QFETCH(QString, path);
        QFETCH(int, format);
        const std::optional<DocumentFormat> f = documentFormatOf(path);
        QCOMPARE(f.has_value(), format >= 0);
        if (f) {
            QCOMPARE(int(*f), format);
            QCOMPARE(documentFormatOf(kindOf(*f)), f);
            QVERIFY(isDocumentKind(kindOf(*f)));
        }
        QVERIFY(!isDocumentKind(Kind::Pdf));
        QVERIFY(!isDocumentKind(Kind::Picture));
    }

    // ---- DOCX ----
    void docxText()
    {
        const QByteArray file = docx(
                u"<w:p><w:r><w:t>Zażółć gęślą jaźń</w:t></w:r><w:r><w:tab/><w:t xml:space='preserve'>Żółw </w:t></w:r>"
                 "<w:r><w:t>idzie</w:t></w:r></w:p>"
                 "<w:p><w:r><w:t>Pierwsza linia</w:t><w:br/><w:t>druga linia</w:t><w:noBreakHyphen/><w:t>x</w:t></w:r></w:p>"
                 // A text box: the same text in the choice and in its fallback — once.
                 "<w:p><w:r><mc:AlternateContent><mc:Choice><w:drawing><w:txbxContent><w:p><w:r><w:t>Napis w ramce</w:t></w:r></w:p>"
                 "</w:txbxContent></w:drawing></mc:Choice><mc:Fallback><w:pict><w:txbxContent><w:p><w:r><w:t>Napis w ramce</w:t></w:r></w:p>"
                 "</w:txbxContent></w:pict></mc:Fallback></mc:AlternateContent></w:r></w:p>"
                 // Tracked changes: the deleted text and the field's code are no text.
                 "<w:p><w:del><w:r><w:delText>usunięte słowo</w:delText></w:r></w:del><w:r><w:instrText>PAGE</w:instrText></w:r>"
                 "<w:ins><w:r><w:t>Zostało</w:t></w:r></w:ins></w:p>"
                 "<w:p/><w:p><w:r><w:t>   </w:t></w:r></w:p>"_s,
                {entry(u"word/footnotes.xml"_s,
                       u"<w:footnotes "_s + W
                               + u"><w:footnote w:type='separator'><w:p><w:r><w:separator/></w:r></w:p></w:footnote>"
                                 "<w:footnote w:id='1'><w:p><w:r><w:t>Przypis o łące</w:t></w:r></w:p></w:footnote></w:footnotes>"_s),
                 entry(u"word/endnotes.xml"_s,
                       u"<w:endnotes "_s + W
                               + u"><w:endnote w:id='1'><w:p><w:r><w:t>Przypis końcowy</w:t></w:r></w:p></w:endnote></w:endnotes>"_s)});
        const DocumentText text = mustRead(file, DocumentFormat::Docx);
        QCOMPARE(text.status, Status::Ok);
        QVERIFY(!text.truncated);
        QCOMPARE(text.pages,
                 QStringList{u"Zażółć gęślą jaźń\tŻółw idzie\nPierwsza linia\ndruga linia-x\nNapis w ramce\nZostało\n"
                             "Przypis o łące\nPrzypis końcowy"_s});
    }
    void docxMainPartFromRelationships()
    {
        // _rels/.rels names the main part: not always word/document.xml.
        QList<io::FileEntry> entries = {
                entry(u"_rels/.rels"_s,
                      u"<Relationships "_s + Rels
                              + u"><Relationship Id='r' Type='http://purl.oclc.org/ooxml/officeDocument/relationships/officeDocument' Target='/word/główny.xml'/></Relationships>"_s),
                entry(u"word/główny.xml"_s, docxBody(para(u"Tekst w innym pliku"_s))),
                entry(u"word/document.xml"_s, docxBody(para(u"Nie ten"_s)))};
        QCOMPARE(mustRead(zip(entries), DocumentFormat::Docx).pages, QStringList{u"Tekst w innym pliku"_s});
    }
    void docxParts()
    {
        QString body;
        for (int i = 0; i < 100; ++i)
            body += para(u"Akapit numer %1: zażółć gęślą jaźń, wąż i jeż w łące."_s.arg(i));
        DocumentLimits limits;
        limits.partChars = 1000;
        const DocumentText text = mustRead(docx(body), DocumentFormat::Docx, limits);
        QCOMPARE(text.status, Status::Ok);
        QVERIFY2(text.pages.size() >= 5 && text.pages.size() <= 10, qPrintable(QString::number(text.pages.size())));
        // Every paragraph is in some page, whole, in order.
        QString all = text.pages.join(u'\n');
        for (int i = 0; i < 100; ++i)
            QVERIFY(all.contains(u"Akapit numer %1: zażółć"_s.arg(i)));
        QVERIFY(all.indexOf(u"numer 7:"_s) < all.indexOf(u"numer 8:"_s));
        for (const QString &page : text.pages)
            QVERIFY(!page.isEmpty());
        // The default part is longer: a few pages for this text.
        QVERIFY(mustRead(docx(body), DocumentFormat::Docx).pages.size() < text.pages.size());
    }
    void docxNoText()
    {
        const DocumentText text = mustRead(docx(u"<w:p/>"_s), DocumentFormat::Docx);
        QCOMPARE(text.status, Status::Ok);
        QVERIFY(text.pages.isEmpty());
    }
    void docxMissingMainPart()
    {
        const QByteArray file = zip({entry(u"word/other.xml"_s, u"<x/>"_s)});
        QCOMPARE(mustRead(file, DocumentFormat::Docx).status, Status::Unreadable);
    }

    // ---- PPTX ----
    void pptxSlidesInTheListsOrder()
    {
        const DocumentText text = mustRead(zip(slideEntries()), DocumentFormat::Pptx);
        QCOMPARE(text.status, Status::Ok);
        QCOMPARE(text.pages, (QStringList{u"Drugi slajd\nZażółć gęślą jaźń"_s, u"Dziesiąty\npo łamaniu"_s,
                                          u"Pierwszy slajd\nŻółć i gęślą"_s}));
    }
    void pptxSlidesByNumber()
    {
        // No slide list: slide1, slide2, slide10 (not slide1, slide10, slide2).
        QList<io::FileEntry> entries = slideEntries();
        entries.removeFirst();
        entries.removeFirst();
        const DocumentText text = mustRead(zip(entries), DocumentFormat::Pptx);
        QCOMPARE(text.status, Status::Ok);
        QCOMPARE(text.pages, (QStringList{u"Pierwszy slajd\nŻółć i gęślą"_s, u"Drugi slajd\nZażółć gęślą jaźń"_s,
                                          u"Dziesiąty\npo łamaniu"_s}));
    }
    void pptxEmptySlideKeepsItsPlace()
    {
        QList<io::FileEntry> entries = slideEntries();
        entries[2] = entry(u"ppt/slides/slide1.xml"_s, slide({}));
        const DocumentText text = mustRead(zip(entries), DocumentFormat::Pptx);
        QCOMPARE(text.pages.size(), 3);
        QCOMPARE(text.pages.at(2), QString());
    }
    void pptxNotAPresentation()
    {
        QCOMPARE(mustRead(zip({entry(u"x.xml"_s, u"<x/>"_s)}), DocumentFormat::Pptx).status, Status::Unreadable);
    }

    // ---- ODT ----
    void odtText()
    {
        const DocumentText text = mustRead(
                odt(u"<text:h text:outline-level='1'>Rozdział pierwszy</text:h>"
                     "<text:p>Zażółć<text:s text:c='3'/>gęślą<text:tab/>jaźń<text:line-break/>nowa linia<text:s/>koniec</text:p>"
                     "<text:tracked-changes><text:changed-region><text:deletion><text:p>skasowane</text:p></text:deletion>"
                     "</text:changed-region></text:tracked-changes>"
                     "<text:p>Przypis<text:note text:note-class='footnote'><text:note-citation>1</text:note-citation>"
                     "<text:note-body><text:p>Treść przypisu o łące</text:p></text:note-body></text:note> dalej</text:p>"
                     "<text:p><office:annotation><dc:creator>Jan Kowalski</dc:creator><dc:date>2024-01-01T10:00:00</dc:date>"
                     "<text:p>Uwaga recenzenta</text:p></office:annotation>Po uwadze</text:p>"
                     "<text:list><text:list-item><text:p>Punkt listy</text:p></text:list-item></text:list>"_s),
                DocumentFormat::Odt);
        QCOMPARE(text.status, Status::Ok);
        QCOMPARE(text.pages,
                 QStringList{u"Rozdział pierwszy\nZażółć   gęślą\tjaźń\nnowa linia koniec\nTreść przypisu o łące\n"
                             "Przypis dalej\nUwaga recenzenta\nPo uwadze\nPunkt listy"_s});
    }
    void odtEncrypted()
    {
        QList<io::FileEntry> entries = {
                {u"mimetype"_s, "application/vnd.oasis.opendocument.text"},
                entry(u"META-INF/manifest.xml"_s,
                      u"<manifest xmlns:manifest='urn:oasis:names:tc:opendocument:xmlns:manifest:1.0'><manifest:file-entry>"
                      "<manifest:encryption-data manifest:checksum='x'/></manifest:file-entry></manifest>"_s),
                {u"content.xml"_s, QByteArray("\x01\x02garbage")}};
        const DocumentText text = mustRead(zip(entries), DocumentFormat::Odt);
        QCOMPARE(text.status, Status::Locked);
        QVERIFY(text.pages.isEmpty());
    }

    // ---- EPUB ----
    void epubSpineOrder()
    {
        const DocumentText text = mustRead(epub(), DocumentFormat::Epub);
        QCOMPARE(text.status, Status::Ok);
        // The spine: ch1 (a percent-encoded name with a fragment), the NCX is no text, ch2. Not the
        // archive's order (ch2 first); no script, style, head, SVG; &nbsp;-like references; <br> splits.
        QCOMPARE(text.pages,
                 (QStringList{u"Rozdział pierwszy\nZażółć gęślą jaźń iłąka.\nDruga\nlinia"_s,
                              u"Rozdział drugi\nWąż i jeż."_s}));
    }
    void epubNamedEntities()
    {
        // EPUB 2's XHTML uses HTML's named entities that XML does not declare.
        QList<io::FileEntry> entries = {
                {u"mimetype"_s, "application/epub+zip"},
                entry(u"META-INF/container.xml"_s,
                      u"<container><rootfiles><rootfile full-path='o.opf'/></rootfiles></container>"_s),
                entry(u"o.opf"_s,
                      u"<package><manifest><item id='a' href='a.xhtml' media-type='application/xhtml+xml'/></manifest>"
                      "<spine><itemref idref='a'/></spine></package>"_s),
                entry(u"a.xhtml"_s,
                      u"<?xml version='1.0'?><!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Strict//EN' 'http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd'>"
                      "<html xmlns='http://www.w3.org/1999/xhtml'><body><p>Zażółć&nbsp;gęślą &mdash; &eacute;lan &hellip; &unknownentity; koniec</p></body></html>"_s)};
        const DocumentText text = mustRead(zip(entries), DocumentFormat::Epub);
        QCOMPARE(text.status, Status::Ok);
        QCOMPARE(text.pages, QStringList{u"Zażółć gęślą — élan … koniec"_s});
    }
    void epubWithoutPackageReadsFilesByName()
    {
        QList<io::FileEntry> entries = {
                {u"mimetype"_s, "application/epub+zip"},
                entry(u"b.xhtml"_s, u"<html><body><p>Drugi plik</p></body></html>"_s),
                entry(u"a.html"_s, u"<html><body><p>Pierwszy plik</p></body></html>"_s)};
        const DocumentText text = mustRead(zip(entries), DocumentFormat::Epub);
        QCOMPARE(text.status, Status::Ok);
        QCOMPARE(text.pages, (QStringList{u"Pierwszy plik"_s, u"Drugi plik"_s}));
    }
    void epubLongFileIsCut()
    {
        QString body;
        for (int i = 0; i < 60; ++i)
            body += u"<p>Akapit %1 o zażółconej gęślej jaźni, długi akapit wypełniający stronę tekstem.</p>"_s.arg(i);
        QList<io::FileEntry> entries = {
                {u"mimetype"_s, "application/epub+zip"},
                entry(u"META-INF/container.xml"_s,
                      u"<container><rootfiles><rootfile full-path='o.opf'/></rootfiles></container>"_s),
                entry(u"o.opf"_s,
                      u"<package><manifest><item id='a' href='a.xhtml' media-type='application/xhtml+xml'/></manifest>"
                      "<spine><itemref idref='a'/></spine></package>"_s),
                entry(u"a.xhtml"_s, u"<html><body>"_s + body + u"</body></html>"_s)};
        DocumentLimits limits;
        limits.epubUnitChars = 1000;
        const DocumentText text = mustRead(zip(entries), DocumentFormat::Epub, limits);
        QVERIFY(text.pages.size() >= 4);
        QVERIFY(text.pages.join(u'\n').contains(u"Akapit 59 o zażółconej"_s));
    }
    void epubMissingFileIsSkipped()
    {
        // A spine item that is not in the archive: the others are read.
        const DocumentText text = mustRead(
                epub({}, u"<itemref idref='ch1'/><itemref idref='ghost'/><itemref idref='ch2'/>"_s), DocumentFormat::Epub);
        QCOMPARE(text.pages.size(), 2);
    }

    // ---- hostile and damaged files ----
    void notAZip_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("text") << QByteArray("To nie jest archiwum ZIP, tylko tekst z ąęś.");
        QTest::newRow("PK only") << QByteArray("PK\x03\x04");
        QTest::newRow("an empty archive") << QByteArray("PK\x05\x06" + QByteArray(18, '\0'));
        // An encrypted DOCX is an OLE2 file.
        QTest::newRow("OLE2") << (QByteArray("\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1") + QByteArray(600, '\0'));
        QTest::newRow("zeros") << QByteArray(5000, '\0');
        QByteArray random;
        for (int i = 0; i < 4000; ++i)
            random.append(char(QRandomGenerator(7).bounded(256) ^ (i * 31)));
        QTest::newRow("random") << random;
    }
    void notAZip()
    {
        QFETCH(QByteArray, bytes);
        for (const DocumentFormat format :
             {DocumentFormat::Docx, DocumentFormat::Pptx, DocumentFormat::Odt, DocumentFormat::Epub}) {
            const DocumentText text = mustRead(bytes, format);
            QCOMPARE(text.status, Status::Unreadable);
            QVERIFY(text.pages.isEmpty());
        }
    }
    void truncatedArchive()
    {
        const QByteArray file = docx(para(u"Zażółć gęślą jaźń"_s));
        // (The central directory is at the end: a cut file has none.)
        for (const qsizetype keep : {file.size() / 2, file.size() - 30, qsizetype(10)})
            QCOMPARE(mustRead(file.left(keep), DocumentFormat::Docx).status, Status::Unreadable);
    }
    void truncatedXmlKeepsWhatWasRead()
    {
        const DocumentText text = mustRead(
                docx(para(u"Pierwszy akapit"_s) + para(u"Drugi akapit"_s) + u"<w:p><w:r><w:t>Trzeci ak"_s), DocumentFormat::Docx);
        QCOMPARE(text.status, Status::Ok);
        QVERIFY2(!text.pages.isEmpty(), "nothing read");
        QVERIFY(text.pages.first().startsWith(u"Pierwszy akapit\nDrugi akapit"_s));
    }
    void notXml()
    {
        const DocumentText text = mustRead(zip({entry(u"word/document.xml"_s, u"to nie jest XML ąęś"_s)}), DocumentFormat::Docx);
        QVERIFY(text.pages.isEmpty());
    }
    void zipBombPart()
    {
        // A part that unpacks to 64 MB of "a" (some 64 KB packed): over the part limit — never unpacked.
        QString xml = docxBody(QString());
        xml.insert(xml.indexOf(u"<w:body>"_s) + 8, u"<w:p><w:r><w:t>"_s + QString(64 << 20, u'a') + u"</w:t></w:r></w:p>"_s);
        const QByteArray file = zip({entry(u"word/document.xml"_s, xml)});
        QVERIFY2(file.size() < (1 << 20), "the fixture is not a bomb");
        QElapsedTimer timer;
        timer.start();
        const DocumentText text = mustRead(file, DocumentFormat::Docx);
        QCOMPARE(text.status, Status::Unreadable);
        QVERIFY(text.pages.isEmpty());
        QVERIFY(timer.elapsed() < 2000 * qMax(1, qEnvironmentVariableIntValue("BRULION_TEST_SLOWDOWN")));
    }
    void forgedSize()
    {
        // The header says 2 GB for a few bytes: refused before memory is set aside for it.
        QByteArray file = docx(para(u"Zażółć gęślą jaźń"_s));
        patchCentral(file, "word/document.xml", [](char *h) { putU32(h + 24, 0x7FFFFFFF); });
        QCOMPARE(mustRead(file, DocumentFormat::Docx).status, Status::Unreadable);
        // A size under the limit but more than any deflate stream gives (a 40-byte part claiming 20 MB).
        QByteArray small = docx(para(u"Zażółć"_s));
        patchCentral(small, "word/document.xml", [](char *h) { putU32(h + 24, 20 << 20); });
        QCOMPARE(mustRead(small, DocumentFormat::Docx).status, Status::Unreadable);
    }
    void forgedSmallerSize()
    {
        // A size smaller than the real one: the part is damaged, not cut quietly into something else.
        QByteArray file = docx(para(u"Zażółć gęślą jaźń, wąż i jeż"_s) + para(QString(500, u'x')));
        patchCentral(file, "word/document.xml", [](char *h) { putU32(h + 24, 40); });
        const DocumentText text = mustRead(file, DocumentFormat::Docx);
        QVERIFY(text.pages.isEmpty() || !text.pages.join(QString()).contains(u"xxxxxxxxxx"_s));
    }
    void tooManyEntries()
    {
        QList<io::FileEntry> entries = {entry(u"word/document.xml"_s, docxBody(para(u"Tekst"_s)))};
        for (int i = 0; i < 100; ++i)
            entries.append({u"junk/%1.bin"_s.arg(i), QByteArray("x")});
        const QByteArray file = zip(entries);
        DocumentLimits limits;
        limits.maxEntries = 50;
        QCOMPARE(mustRead(file, DocumentFormat::Docx, limits).status, Status::Unreadable);
        QCOMPARE(mustRead(file, DocumentFormat::Docx).status, Status::Ok);
        // The directory's own size in the end record: far more than the file holds.
        QByteArray forged = file;
        const qsizetype end = forged.lastIndexOf("PK\x05\x06");
        putU32(forged.data() + end + 12, 0x7FFFFFFF);
        QCOMPARE(mustRead(forged, DocumentFormat::Docx).status, Status::Unreadable);
        // ZIP64 markers: not one of these formats' files.
        forged = file;
        forged[end + 10] = '\xff';
        forged[end + 11] = '\xff';
        QCOMPARE(mustRead(forged, DocumentFormat::Docx).status, Status::Unreadable);
    }
    void fileTooLarge()
    {
        const QByteArray file = docx(para(u"Tekst"_s));
        DocumentLimits limits;
        limits.maxFileBytes = file.size() - 1;
        QCOMPARE(mustRead(file, DocumentFormat::Docx, limits).status, Status::TooLarge);
        limits.maxFileBytes = file.size();
        QCOMPARE(mustRead(file, DocumentFormat::Docx, limits).status, Status::Ok);
    }
    void totalUnpackedLimit()
    {
        // Slides of 1 MB each (before their unpacking they are tiny): the sum stops at 3 MB.
        QList<io::FileEntry> entries;
        QString list;
        QString rels;
        for (int i = 1; i <= 6; ++i) {
            const QString name = u"ppt/slides/slide%1.xml"_s.arg(i);
            entries.append(entry(name, slide({u"Slajd %1 "_s.arg(i) + QString(1 << 20, u'z')})));
            list += u"<p:sldId id='%1' r:id='r%1'/>"_s.arg(i);
            rels += u"<Relationship Id='r%1' Target='slides/slide%1.xml'/>"_s.arg(i);
        }
        entries.append(entry(u"ppt/presentation.xml"_s, u"<p:presentation "_s + A + u"><p:sldIdLst>"_s + list + u"</p:sldIdLst></p:presentation>"_s));
        entries.append(entry(u"ppt/_rels/presentation.xml.rels"_s, u"<Relationships "_s + Rels + u">"_s + rels + u"</Relationships>"_s));
        DocumentLimits limits;
        limits.maxTotalBytes = 3'500'000;
        const DocumentText text = mustRead(zip(entries), DocumentFormat::Pptx, limits);
        QCOMPARE(text.status, Status::Ok);
        QVERIFY(text.truncated);
        QVERIFY(text.pages.size() >= 3);
        QCOMPARE(text.pages.size(), 6); // (the slides are listed; those past the limit have no text)
        QVERIFY(text.pages.at(0).startsWith(u"Slajd 1 "_s));
        QVERIFY(text.pages.at(5).isEmpty());
    }
    void textLimit()
    {
        QString body;
        for (int i = 0; i < 200; ++i)
            body += para(u"Zażółć gęślą jaźń numer %1 — wąż i jeż."_s.arg(i));
        DocumentLimits limits;
        limits.maxTextChars = 1500;
        const DocumentText text = mustRead(docx(body), DocumentFormat::Docx, limits);
        QCOMPARE(text.status, Status::Ok);
        QVERIFY(text.truncated);
        qsizetype total = 0;
        for (const QString &page : text.pages)
            total += page.size();
        QVERIFY2(total <= 1500 + 1, qPrintable(QString::number(total)));
        QVERIFY(total > 1000);
        QVERIFY(text.pages.join(QString()).contains(u"numer 0 "_s));
        QVERIFY(!text.pages.join(QString()).contains(u"numer 199"_s));
    }
    void encryptedEntry()
    {
        // The "encrypted" bit of the entry: its bytes are skipped, never read as XML.
        QByteArray file = docx(para(u"Tajne"_s));
        patchCentral(file, "word/document.xml", [](char *h) { h[8] = char(h[8] | 1); });
        const DocumentText text = mustRead(file, DocumentFormat::Docx);
        QCOMPARE(text.status, Status::Locked);
        QVERIFY(text.pages.isEmpty());
    }
    void unsupportedCompression()
    {
        // Method 99 (AES) / 12 (bzip2): not deflate — the entry is skipped.
        QByteArray file = docx(para(u"Tekst"_s));
        patchCentral(file, "word/document.xml", [](char *h) { h[10] = 12; h[11] = 0; });
        const DocumentText text = mustRead(file, DocumentFormat::Docx);
        QCOMPARE(text.status, Status::Unreadable);
        QVERIFY(text.pages.isEmpty());
    }
    void entitiesNeverExpand_data()
    {
        QTest::addColumn<QString>("doctype");
        QTest::addColumn<QString>("reference");
        // "Billion laughs".
        QString bomb = u"<!DOCTYPE w:document ["_s;
        bomb += u"<!ENTITY a0 'lol lol lol lol lol lol lol lol lol lol'>"_s;
        for (int i = 1; i <= 9; ++i)
            bomb += u"<!ENTITY a%1 '&a%2;&a%2;&a%2;&a%2;&a%2;&a%2;&a%2;&a%2;&a%2;&a%2;'>"_s.arg(i).arg(i - 1);
        bomb += u"]>"_s;
        QTest::newRow("billion laughs") << bomb << u"&a9;"_s;
        QTest::newRow("a file") << u"<!DOCTYPE w:document [<!ENTITY x SYSTEM 'file:///etc/passwd'>]>"_s << u"&x;"_s;
        QTest::newRow("a file, public") << u"<!DOCTYPE w:document [<!ENTITY x PUBLIC 'a' 'file:///etc/hostname'>]>"_s << u"&x;"_s;
        QTest::newRow("a web address") << u"<!DOCTYPE w:document [<!ENTITY x SYSTEM 'http://127.0.0.1:1/x'>]>"_s << u"&x;"_s;
        QTest::newRow("a parameter entity")
                << u"<!DOCTYPE w:document [<!ENTITY % p SYSTEM 'file:///etc/passwd'> %p;]>"_s << u"x"_s;
        QTest::newRow("small own entity") << u"<!DOCTYPE w:document [<!ENTITY x 'ukryte'>]>"_s << u"&x;"_s;
    }
    void entitiesNeverExpand()
    {
        QFETCH(QString, doctype);
        QFETCH(QString, reference);
        const QString xml = u"<?xml version='1.0'?>"_s + doctype + u"<w:document "_s + W
                            + u"><w:body><w:p><w:r><w:t>Przed</w:t></w:r></w:p><w:p><w:r><w:t>"_s + reference
                            + u"</w:t></w:r></w:p></w:body></w:document>"_s;
        QElapsedTimer timer;
        timer.start();
        const DocumentText text = mustRead(zip({entry(u"word/document.xml"_s, xml)}), DocumentFormat::Docx);
        QVERIFY(timer.elapsed() < 2000 * qMax(1, qEnvironmentVariableIntValue("BRULION_TEST_SLOWDOWN")));
        const QString all = text.pages.join(u'\n');
        QVERIFY2(all.size() < 200, qPrintable(QString::number(all.size())));
        QVERIFY(!all.contains(u"root:"_s));
        QVERIFY(!all.contains(u"lol"_s));
        QVERIFY(!all.contains(u"ukryte"_s));
        // The same in an EPUB's XHTML and an ODT.
        const QString xhtml = u"<?xml version='1.0'?>"_s + doctype.replace(u"w:document"_s, u"html"_s)
                              + u"<html><body><p>Przed</p><p>"_s + reference + u"</p></body></html>"_s;
        const DocumentText book = mustRead(
                zip({{u"mimetype"_s, "application/epub+zip"}, entry(u"a.xhtml"_s, xhtml)}), DocumentFormat::Epub);
        const QString bookText = book.pages.join(u'\n');
        QVERIFY(bookText.size() < 200 && !bookText.contains(u"root:"_s) && !bookText.contains(u"lol"_s));
    }
    void randomDamage()
    {
        // Flipped bytes everywhere in valid files: some status, no crash, no hang, no huge text.
        const QList<std::pair<DocumentFormat, QByteArray>> files = {
                {DocumentFormat::Docx, docx(para(u"Zażółć gęślą jaźń"_s) + para(u"Wąż i jeż w łące"_s))},
                {DocumentFormat::Pptx, zip(slideEntries())},
                {DocumentFormat::Odt, odt(u"<text:p>Zażółć gęślą jaźń</text:p>"_s)},
                {DocumentFormat::Epub, epub()}};
        QRandomGenerator random(20261123);
        for (const auto &[format, original] : files) {
            for (int round = 0; round < 150; ++round) {
                QByteArray bytes = original;
                const int flips = 1 + random.bounded(6);
                for (int i = 0; i < flips; ++i)
                    bytes[random.bounded(int(bytes.size()))] = char(random.bounded(256));
                if (round % 5 == 0)
                    bytes.truncate(random.bounded(int(bytes.size())));
                const DocumentText text = mustRead(bytes, format);
                qsizetype total = 0;
                for (const QString &page : text.pages)
                    total += page.size();
                QVERIFY(total < 10'000);
            }
        }
    }
    void damagedXmlInsideAValidArchive()
    {
        // Damage inside the XML (the archive's checksum is right: the parts are written damaged).
        QRandomGenerator random(7);
        const QString good = docxBody(para(u"Zażółć gęślą jaźń"_s) + para(u"Wąż i jeż w łące"_s));
        for (int round = 0; round < 200; ++round) {
            QByteArray xml = good.toUtf8();
            for (int i = 0; i < 1 + random.bounded(5); ++i)
                xml[random.bounded(int(xml.size()))] = char(random.bounded(256));
            if (round % 4 == 0)
                xml.truncate(random.bounded(int(xml.size())));
            const DocumentText text = mustRead(zip({{u"word/document.xml"_s, xml}}), DocumentFormat::Docx);
            QVERIFY(text.pages.join(QString()).size() < 1000);
        }
    }
    void cancelled()
    {
        // Asked every few thousand tokens: a long part is stopped.
        QString body;
        for (int i = 0; i < 20000; ++i)
            body += para(u"Zażółć gęślą jaźń"_s);
        const QByteArray file = docx(body);
        int asked = 0;
        const std::optional<DocumentText> text =
                read(file, DocumentFormat::Docx, {}, [&asked] { return ++asked > 2; });
        QVERIFY(!text);
        QVERIFY(asked >= 3);
        // Already cancelled before it begins to read the parts.
        QVERIFY(!read(docx(para(u"x"_s)), DocumentFormat::Docx, {}, [] { return true; }));
        QVERIFY(read(file, DocumentFormat::Docx, {}, [] { return false; }));
    }
    void unusableDevices()
    {
        QBuffer closed;
        closed.setData(docx(para(u"x"_s)));
        QCOMPARE(extractDocumentText(closed, DocumentFormat::Docx)->status, Status::Unreadable);
        QBuffer writeOnly;
        writeOnly.open(QIODevice::WriteOnly);
        QCOMPARE(extractDocumentText(writeOnly, DocumentFormat::Docx)->status, Status::Unreadable);
    }

    // ---- records and search ----
    void record()
    {
        const DocumentText text = mustRead(zip(slideEntries()), DocumentFormat::Pptx);
        const Record record = documentRecord(DocumentFormat::Pptx, text, 7);
        QCOMPARE(record.kind, Kind::Pptx);
        QCOMPARE(record.extractor, 7u);
        QCOMPARE(record.pageCount, 3);
        QVERIFY(!record.locked && !record.unreadable);
        QCOMPARE(record.pages.size(), 3);
        QCOMPARE(record.pages.at(0).source, PageSource::TextLayer);
        QCOMPARE(record.pages.at(0).text, text.pages.at(0));
        const std::optional<Record> back = decodeRecord(encodeRecord(record));
        QVERIFY(back);
        QCOMPARE(*back, record);
        // One page alone, and the header's kind.
        const QByteArray bytes = encodeRecord(record);
        QCOMPARE(decodeRecordPage(bytes, 2)->text, text.pages.at(1));
        QCOMPARE(decodeRecordInfo(bytes)->kind, Kind::Pptx);
    }
    void recordKindsRoundTrip()
    {
        for (const Kind kind : {Kind::Docx, Kind::Pptx, Kind::Odt, Kind::Epub}) {
            Record r;
            r.kind = kind;
            r.pageCount = 1;
            r.pages = {Page{PageSource::TextLayer, u"Zażółć"_s, {}, {}}};
            QCOMPARE(decodeRecord(encodeRecord(r))->kind, kind);
        }
        // A kind past the known ones is no record.
        Record r;
        r.kind = Kind::Epub;
        QByteArray bytes = encodeRecord(r);
        bytes[12] = 7;
        QVERIFY(!decodeRecord(bytes));
    }
    void recordOfBadFiles()
    {
        DocumentText locked;
        locked.status = Status::Locked;
        const Record l = documentRecord(DocumentFormat::Odt, locked, 1);
        QVERIFY(l.locked && !l.unreadable && l.pages.isEmpty() && l.pageCount == 0);
        DocumentText broken;
        const Record b = documentRecord(DocumentFormat::Docx, broken, 1);
        QVERIFY(!b.locked && b.unreadable && b.pages.isEmpty());
        // A page is cut at the record's limit.
        DocumentText big;
        big.status = Status::Ok;
        big.pages = {QString(MaxPageText + 500, u'a')};
        QCOMPARE(documentRecord(DocumentFormat::Epub, big, 1).pages.at(0).text.size(), MaxPageText);
    }
    void search()
    {
        AttachmentSearch s;
        const DocumentText slides = mustRead(zip(slideEntries()), DocumentFormat::Pptx);
        s.set(AttachmentSearch::prepare(u"wykłady/Algebra.pptx"_s, documentRecord(DocumentFormat::Pptx, slides, 1)));
        const DocumentText doc = mustRead(docx(para(u"Umowa o dzieło: żółw zielony"_s)), DocumentFormat::Docx);
        s.set(AttachmentSearch::prepare(u"umowy/Umowa.docx"_s, documentRecord(DocumentFormat::Docx, doc, 1)));
        Record pdf;
        pdf.kind = Kind::Pdf;
        pdf.pageCount = 1;
        pdf.pages = {Page{PageSource::TextLayer, u"Żółw w pliku PDF"_s, {}, {}}};
        s.set(AttachmentSearch::prepare(u"a.pdf"_s, pdf));
        const AttachmentSearch::PageText pageText = [&](const QString &path, int page) -> std::optional<Page> {
            if (path == u"wykłady/Algebra.pptx"_s)
                return Page{PageSource::TextLayer, slides.pages.value(page - 1), {}, {}};
            if (path == u"umowy/Umowa.docx"_s)
                return Page{PageSource::TextLayer, doc.pages.value(page - 1), {}, {}};
            return std::nullopt;
        };
        // The slide's page, its excerpt and what the viewer would search for (diacritics optional).
        const QList<AttachmentSearch::Hit> slideHits = s.search(u"zazolc gesla"_s, 10, Scope::Documents, pageText);
        QCOMPARE(slideHits.size(), 1);
        QCOMPARE(slideHits.at(0).path, u"wykłady/Algebra.pptx"_s);
        QCOMPARE(slideHits.at(0).kind, Kind::Pptx);
        QCOMPARE(slideHits.at(0).page, 1);
        QCOMPARE(slideHits.at(0).pageCount, 3);
        QVERIFY(slideHits.at(0).excerpt.contains(u"Zażółć gęślą"_s));
        // The kinds: documents are not PDFs.
        const auto paths = [&](Scopes kinds) {
            QStringList out;
            for (const AttachmentSearch::Hit &h : s.search(u"zolw"_s, 10, kinds, pageText))
                out.append(h.path);
            return out;
        };
        QVERIFY(paths(Scope::Documents).contains(u"umowy/Umowa.docx"_s));
        QVERIFY(!paths(Scope::Documents).contains(u"a.pdf"_s));
        QCOMPARE(paths(Scope::Pdf), QStringList{u"a.pdf"_s});
        QVERIFY(paths(AttachmentScopes).contains(u"a.pdf"_s) && paths(AttachmentScopes).contains(u"umowy/Umowa.docx"_s));
        QVERIFY(paths(Scope::Pictures).isEmpty());
        // The file's name.
        const QList<AttachmentSearch::Hit> byName = s.search(u"algebra"_s, 10, Scope::Documents, pageText);
        QCOMPARE(byName.size(), 1);
        QVERIFY(byName.at(0).nameOnly);
    }
    void nameOnlyForAnUnreadableFile()
    {
        AttachmentSearch s;
        DocumentText broken;
        s.set(AttachmentSearch::prepare(u"Zepsuty plik.docx"_s, documentRecord(DocumentFormat::Docx, broken, 1)));
        const QList<AttachmentSearch::Hit> hits = s.search(u"zepsuty"_s, 10);
        QCOMPARE(hits.size(), 1);
        QVERIFY(hits.at(0).nameOnly);
        QCOMPARE(hits.at(0).kind, Kind::Docx);
    }
};

QTEST_GUILESS_MAIN(TestDocumentText)
#include "tst_documenttext.moc"
