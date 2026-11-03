// Which attachments may be handed to another app (src/platform/externalfiles.h), pure, on every
// platform: each type of the allow-list with bytes of its format (Office Open XML, OpenDocument,
// EPUB, the old Office formats, RTF, audio, video, text in UTF-8 and with a byte order mark), the
// extension in any case and with Polish names; refused: names off the list (programs, scripts,
// shortcuts, HTML, archives, pictures, notes), a program or a page renamed to a document, an
// OpenDocument naming another type, a zip without an office part, text with NUL bytes or broken
// UTF-8, a name without an extension, nothing read.
#include "externalfiles.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::platform;

namespace {

/// A zip's first local header naming `entry`, its data `data` stored after it.
QByteArray zipEntry(const QByteArray &entry, const QByteArray &data)
{
    QByteArray out("PK\x03\x04", 4);
    out += QByteArray(26, '\0'); // version, flags, method, times, crc, sizes, name and extra lengths
    out += entry;
    out += data;
    return out;
}

QByteArray ooxml(const QByteArray &mainPart)
{
    return zipEntry("[Content_Types].xml", "<?xml version=\"1.0\"?><Types/>")
           + zipEntry("_rels/.rels", "<Relationships/>") + zipEntry(mainPart, "<x/>");
}

QByteArray openDocument(const QByteArray &mime)
{
    return zipEntry("mimetype", mime) + zipEntry("content.xml", "<office:document-content/>");
}

} // namespace

class TestExternalFiles : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void type_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QByteArray>("head");
        QTest::addColumn<QString>("mime"); // "" refused
        QTest::addColumn<QString>("kind");

        const QByteArray docx = ooxml("word/document.xml");
        QTest::newRow("PDF") << u"wykład.pdf"_s << "%PDF-1.7\n%\xe2\xe3"_ba << u"application/pdf"_s << u"pdf"_s;
        QTest::newRow("PDF after junk") << u"skan.PDF"_s << QByteArray(500, ' ') + "%PDF-1.4" << u"application/pdf"_s
                                        << u"pdf"_s;
        QTest::newRow("PDF too far in") << u"skan.pdf"_s << QByteArray(2000, ' ') + "%PDF-1.4" << QString()
                                        << QString();
        QTest::newRow("DOCX") << u"Sprawozdanie – źródła.docx"_s << docx
                              << u"application/vnd.openxmlformats-officedocument.wordprocessingml.document"_s
                              << u"document"_s;
        QTest::newRow("DOCX upper case") << u"RAPORT.DOCX"_s << docx
                                         << u"application/vnd.openxmlformats-officedocument.wordprocessingml.document"_s
                                         << u"document"_s;
        QTest::newRow("XLSX") << u"budżet.xlsx"_s << ooxml("xl/workbook.xml")
                              << u"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"_s
                              << u"spreadsheet"_s;
        QTest::newRow("PPTX") << u"slajdy.pptx"_s << ooxml("ppt/presentation.xml")
                              << u"application/vnd.openxmlformats-officedocument.presentationml.presentation"_s
                              << u"presentation"_s;
        QTest::newRow("ODT") << u"notatka.odt"_s << openDocument("application/vnd.oasis.opendocument.text")
                             << u"application/vnd.oasis.opendocument.text"_s << u"document"_s;
        QTest::newRow("ODS") << u"arkusz.ods"_s << openDocument("application/vnd.oasis.opendocument.spreadsheet")
                             << u"application/vnd.oasis.opendocument.spreadsheet"_s << u"spreadsheet"_s;
        QTest::newRow("ODP") << u"pokaz.odp"_s << openDocument("application/vnd.oasis.opendocument.presentation")
                             << u"application/vnd.oasis.opendocument.presentation"_s << u"presentation"_s;
        QTest::newRow("EPUB") << u"książka.epub"_s << openDocument("application/epub+zip") << u"application/epub+zip"_s
                              << u"ebook"_s;
        const QByteArray ole("\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1\0\0\0\0", 12);
        QTest::newRow("DOC") << u"stary.doc"_s << ole << u"application/msword"_s << u"document"_s;
        QTest::newRow("XLS") << u"stary.xls"_s << ole << u"application/vnd.ms-excel"_s << u"spreadsheet"_s;
        QTest::newRow("PPT") << u"stary.ppt"_s << ole << u"application/vnd.ms-powerpoint"_s << u"presentation"_s;
        QTest::newRow("RTF") << u"list.rtf"_s << "{\\rtf1\\ansi Zażółć}"_ba << u"application/rtf"_s << u"document"_s;
        QTest::newRow("MP3 with a tag") << u"wykład.mp3"_s << "ID3\x04\0\0\0\0\0\0"_ba << u"audio/mpeg"_s << u"audio"_s;
        QTest::newRow("MP3 frame") << u"nagranie.mp3"_s << QByteArray("\xff\xfb\x90\x64", 4) << u"audio/mpeg"_s
                                   << u"audio"_s;
        const QByteArray ftyp = QByteArray("\0\0\0\x20", 4) + "ftypisom" + QByteArray(20, '\0');
        QTest::newRow("M4A") << u"głos.m4a"_s << ftyp << u"audio/mp4"_s << u"audio"_s;
        QTest::newRow("MP4") << u"film.mp4"_s << ftyp << u"video/mp4"_s << u"video"_s;
        QTest::newRow("MOV") << u"film.mov"_s << ftyp << u"video/quicktime"_s << u"video"_s;
        QTest::newRow("OGG") << u"dźwięk.ogg"_s << "OggS\0\x02"_ba << u"audio/ogg"_s << u"audio"_s;
        QTest::newRow("OPUS") << u"notatka.opus"_s << "OggS\0\x02"_ba << u"audio/ogg"_s << u"audio"_s;
        QTest::newRow("WAV") << u"dzwonek.wav"_s << "RIFF\x24\0\0\0WAVEfmt "_ba << u"audio/wav"_s << u"audio"_s;
        QTest::newRow("FLAC") << u"utwór.flac"_s << "fLaC\0\0\0\x22"_ba << u"audio/flac"_s << u"audio"_s;
        const QByteArray ebml("\x1a\x45\xdf\xa3\x9f\x42\x86\x81", 8);
        QTest::newRow("WEBM") << u"klip.webm"_s << ebml << u"video/webm"_s << u"video"_s;
        QTest::newRow("MKV") << u"klip.mkv"_s << ebml << u"video/x-matroska"_s << u"video"_s;
        QTest::newRow("TXT, UTF-8") << u"notatki.txt"_s << "Zażółć gęślą jaźń\n"_ba << u"text/plain"_s << u"text"_s;
        QTest::newRow("CSV") << u"dane.csv"_s << "imię;nazwisko\nŁucja;Żak\n"_ba << u"text/csv"_s << u"spreadsheet"_s;
        QTest::newRow("TSV") << u"dane.tsv"_s << "a\tb\n"_ba << u"text/tab-separated-values"_s << u"spreadsheet"_s;
        QTest::newRow("TXT, UTF-8 cut by the head")
                << u"długi.txt"_s << QByteArray("abc\xc5", 4) << u"text/plain"_s << u"text"_s;
        QTest::newRow("TXT, UTF-16 BOM") << u"utf16.txt"_s << QByteArray("\xff\xfez\0", 4) << u"text/plain"_s
                                         << u"text"_s;
        QTest::newRow("empty TXT") << u"pusty.txt"_s << QByteArray() << u"text/plain"_s << u"text"_s;

        // Refused: never on the list.
        for (const char *name :
             {"program.exe", "skrypt.sh",          "skrypt.bat",     "skrypt.ps1",   "skrót.lnk",  "strona.html",
              "strona.htm",  "obraz.svg",          "archiwum.zip",   "archiwum.jar", "pakiet.apk", "pakiet.deb",
              "notatka.md",  "zdjęcie.png",        "makro.docm",     "makro.xlsm",   "dysk.iso",   "plik.desktop",
              "obraz.svgz",  "aplikacja.appimage", "biblioteka.dll", "skrypt.js",    "skrypt.vbs"})
            QTest::newRow(name) << QString::fromUtf8(name) << docx << QString() << QString();
        // Refused: the bytes do not match the name.
        QTest::newRow("a program renamed to .docx")
                << u"faktura.docx"_s << "MZ\x90\0\x03\0\0\0"_ba << QString() << QString();
        QTest::newRow("an ELF renamed to .pdf") << u"skan.pdf"_s
                                                << "\x7f"
                                                   "ELF\x02\x01"_ba
                                                << QString() << QString();
        QTest::newRow("HTML renamed to .txt")
                << u"strona.txt"_s << QByteArray("<html>\0<script>", 15) << QString() << QString();
        QTest::newRow("HTML renamed to .doc")
                << u"strona.doc"_s << "<html><script>alert(1)</script>"_ba << QString() << QString();
        QTest::newRow("a jar renamed to .docx")
                << u"gra.docx"_s << zipEntry("META-INF/MANIFEST.MF", "Main-Class: x") << QString() << QString();
        QTest::newRow("a jar renamed to .odt")
                << u"gra.odt"_s << zipEntry("META-INF/MANIFEST.MF", "Main-Class: x") << QString() << QString();
        QTest::newRow("ODT naming a spreadsheet")
                << u"notatka.odt"_s << openDocument("application/vnd.oasis.opendocument.spreadsheet") << QString()
                << QString();
        QTest::newRow("XLSX with a Word part only") << u"budżet.xlsx"_s << docx << QString() << QString();
        QTest::newRow("binary TXT") << u"dane.txt"_s << QByteArray("ab\0cd", 5) << QString() << QString();
        QTest::newRow("broken UTF-8 TXT")
                << u"dane.txt"_s << QByteArray("a\xff\xfe\xfd b", 6) << QString() << QString();
        QTest::newRow("MP3 of zeros") << u"cisza.mp3"_s << QByteArray(16, '\0') << QString() << QString();
        QTest::newRow("nothing read") << u"raport.docx"_s << QByteArray() << QString() << QString();
        QTest::newRow("no extension") << u"README"_s << "Zażółć"_ba << QString() << QString();
        QTest::newRow("only a dot") << u".docx"_s << docx << QString() << QString();
        QTest::newRow("ends with a dot") << u"raport."_s << docx << QString() << QString();
        QTest::newRow("double extension") << u"raport.docx.exe"_s << docx << QString() << QString();
        QTest::newRow("empty name") << QString() << docx << QString() << QString();
    }

    void type()
    {
        QFETCH(QString, name);
        QFETCH(QByteArray, head);
        QFETCH(QString, mime);
        QFETCH(QString, kind);
        const ExternalFileType type = externalFileType(name, head);
        QCOMPARE(type.mimeType, mime);
        QCOMPARE(type.kind, kind);
        QCOMPARE(type.allowed(), !mime.isEmpty());
    }

    void onlyTheHeadIsRead()
    {
        // An office part beyond the first 64 KiB does not count.
        QByteArray far = zipEntry("[Content_Types].xml", "x") + QByteArray(ExternalFileHeadBytes, 'x');
        far += zipEntry("word/document.xml", "<x/>");
        QVERIFY(!externalFileType(u"raport.docx", far).allowed());
        // Text with a NUL past the head is still text.
        QByteArray text(ExternalFileHeadBytes, 'a');
        text += '\0';
        QVERIFY(externalFileType(u"długi.txt", text).allowed());
    }
};

QTEST_GUILESS_MAIN(TestExternalFiles)
#include "tst_externalfiles.moc"
