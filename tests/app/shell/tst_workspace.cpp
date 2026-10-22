// Workspace — the shell's EditorHost implementation against a real vault folder: reading and
// writing (atomic, History, change signals), external changes, attachments and asset URLs,
// templates and journals, quick capture, pages (create/rename with link rewriting/delete/restore),
// index-backed suggestions and block ids, configuration, favourites, recent pages and the
// sidebar's folder tree.
#include "foldertreemodel.h"
#include "workspace.h"

#include "markdown/markdown.h"
#include "vault/vault.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::shell::FolderTreeModel;
using brulion::shell::Workspace;

namespace {

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(bytes);
    return true;
}

QByteArray readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QStringList strings(const QList<QVariant> &args)
{
    QStringList out;
    for (const QVariant &v : args)
        out.append(v.toString());
    return out;
}

} // namespace

class TestWorkspace : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    /// A fresh vault folder with `files` (path -> content) and an open Workspace on it.
    std::unique_ptr<Workspace> open(const QHash<QString, QString> &files = {}, QString *rootOut = nullptr)
    {
        const QString root = m_tmp.filePath(u"vault-%1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeRaw(root + u'/' + it.key(), it.value().toUtf8());
        auto ws = std::make_unique<Workspace>();
        ws->setClock([] { return QDateTime(QDate(2026, 10, 14), QTime(10, 30)); });
        QString error;
        if (!ws->open(root, m_tmp.filePath(u"state-%1"_s.arg(m_n)), &error))
            qWarning() << error;
        ws->finishIndexing();
        if (rootOut)
            *rootOut = root;
        return ws;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void openAndClose()
    {
        QString root;
        auto ws = open({ { u"a.md"_s, u"# A"_s } }, &root);
        QVERIFY(ws->isOpen());
        QCOMPARE(ws->name(), QFileInfo(root).fileName());
        QCOMPARE(ws->pageCount(), 1);
        QVERIFY(!ws->indexing());
        QVERIFY(ws->indexAvailable());
        QVERIFY(!ws->isSyncthingFolder());
        QSignalSpy openSpy(ws.get(), &Workspace::openChanged);
        ws->close();
        QVERIFY(!ws->isOpen());
        QCOMPARE(openSpy.size(), 1);
        QVERIFY(ws->readFile(u"a.md"_s).isEmpty());
        QVERIFY(!ws->write(u"a.md"_s, u"x"_s));
        QVERIFY(ws->suggestPages(QString()).isEmpty());

        // A missing folder is created; Syncthing's marker is noticed.
        const QString fresh = m_tmp.filePath(u"nowy/głęboko"_s);
        Workspace other;
        QVERIFY(other.open(fresh, QString()));
        QVERIFY(QFileInfo(fresh).isDir());
        writeRaw(fresh + u"/.stfolder/x"_s, "");
        QVERIFY(other.isSyncthingFolder());
    }

    void backgroundIndexing()
    {
        const QString root = m_tmp.filePath(u"bg"_s);
        for (int i = 0; i < 40; ++i)
            writeRaw(root + u"/Strona %1.md"_s.arg(i), "- [[Strona 0]] #tag\n");
        Workspace ws;
        QSignalSpy indexing(&ws, &Workspace::indexingChanged);
        QVERIFY(ws.open(root, QString()));
        QVERIFY(ws.indexing());
        QTRY_VERIFY(!ws.indexing());
        QCOMPARE(ws.suggestTags(u"ta"_s).size(), 1);
        QCOMPARE(ws.resolveLink(u"strona 0"_s, QString()), u"Strona 0.md"_s);
    }

    void readWriteAtomicWithHistory()
    {
        QString root;
        auto ws = open({}, &root);
        const QString text = u"- Zażółć gęślą jaźń\r\n- druga linia\r\n"_s;
        QVERIFY(ws->write(u"Notatka.md"_s, text));
        bool ok = false;
        QCOMPARE(ws->read(u"Notatka.md"_s, &ok), text);
        QVERIFY(ok);
        QCOMPARE(readRaw(root + u"/Notatka.md"_s), text.toUtf8()); // bytes exactly, CRLF kept
        // Atomic writes leave no temporary files behind.
        const QStringList entries = QDir(root).entryList(QDir::Files | QDir::Hidden);
        QCOMPARE(entries, QStringList{ u"Notatka.md"_s });

        QVERIFY(ws->write(u"Notatka.md"_s, u"- nowa treść\n"_s));
        const auto history = ws->vault()->history()->list(u"Notatka.md"_s);
        QCOMPARE(history.size(), 1);
        QCOMPARE(ws->vault()->history()->read(history.first().id), text);

        ws->read(u"brak.md"_s, &ok);
        QVERIFY(!ok);
        QVERIFY(ws->exists(u"Notatka.md"_s));
        QCOMPARE(ws->pageTitle(u"Projekty/Dom.md"_s), u"Dom"_s);
        QCOMPARE(ws->folderOf(u"Projekty/Dom.md"_s), u"Projekty"_s);
    }

    void readTextStart_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::addColumn<int>("limit");
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("truncated");
        const QByteArray svg = "<svg>\xC5\xBC\xC3\xB3\xC5\x82w</svg>"; // <svg>żółw</svg>
        QTest::newRow("shorter than the limit") << svg << 1000 << u"<svg>żółw</svg>"_s << false;
        QTest::newRow("exactly the limit") << svg << int(svg.size()) << u"<svg>żółw</svg>"_s << false;
        QTest::newRow("cut after a whole character") << svg << 7 << u"<svg>ż"_s << true;
        QTest::newRow("cut inside ż (2 bytes): left out") << svg << 6 << u"<svg>"_s << true;
        const QByteArray euro = "x\xE2\x82\xACy"; // x€y
        QTest::newRow("cut after the first byte of €") << euro << 2 << u"x"_s << true;
        QTest::newRow("cut after the second byte of €") << euro << 3 << u"x"_s << true;
        QTest::newRow("cut after €") << euro << 4 << u"x€"_s << true;
        const QByteArray emoji = "a\xF0\x9F\x8C\xB3\x62"; // a🌳b
        for (const int limit : {2, 3, 4})
            QTest::addRow("cut inside 🌳 (4 bytes) after %d", limit) << emoji << limit << u"a"_s << true;
        QTest::newRow("cut after 🌳") << emoji << 5 << u"a🌳"_s << true;
        QTest::newRow("a BOM is dropped") << QByteArray("\xEF\xBB\xBF\xC5\x82\xC4\x85ka") << 100 << u"łąka"_s << false;
        // The file's own broken end is shown as it is (a replacement character), like readFile().
        QTest::newRow("a file that ends in half a character")
                << QByteArray("ab\xC5") << 100 << QString::fromUtf8("ab\xC5") << false;
        const QByteArray png("\x89PNG\r\n\x1A\n\x00\x00\x00\x0DIHDR", 16);
        QTest::newRow("a binary picture") << png << 6 << QString::fromUtf8(png.left(6)) << true;
        QTest::newRow("nothing at all") << svg << 0 << QString() << true;
        QTest::newRow("an empty file") << QByteArray() << 100 << QString() << false;
    }
    void readTextStart()
    {
        // The picture's text view reads at most what it shows (a photo is never read whole for
        // it) and never shows half a character where the limit cuts one.
        QFETCH(QByteArray, content);
        QFETCH(int, limit);
        QFETCH(QString, text);
        QFETCH(bool, truncated);
        QString root;
        auto ws = open({}, &root);
        const QString path = u"assets/szkic żółwia.svg"_s;
        QVERIFY(writeRaw(root + u'/' + path, content));
        const QVariantMap start = ws->readTextStart(path, limit);
        QVERIFY(start.value(u"ok"_s).toBool());
        QCOMPARE(start.value(u"text"_s).toString(), text);
        QCOMPARE(start.value(u"truncated"_s).toBool(), truncated);
        QCOMPARE(start.value(u"size"_s).toLongLong(), qint64(content.size()));
        QVERIFY(start.value(u"bytes"_s).toLongLong() <= limit);
        QCOMPARE(start.value(u"bytesText"_s).toString(),
                 QLocale().formattedDataSize(start.value(u"bytes"_s).toLongLong(), 1,
                                             QLocale::DataSizeTraditionalFormat));
        QCOMPARE(start.value(u"sizeText"_s).toString(),
                 QLocale().formattedDataSize(content.size(), 1, QLocale::DataSizeTraditionalFormat));
        if (!truncated)
            QCOMPARE(start.value(u"text"_s).toString(), ws->readFile(path)); // the same as the whole file
    }
    void readTextStartOfAMissingFile()
    {
        auto ws = open();
        const QVariantMap start = ws->readTextStart(u"assets/nie ma.svg"_s, 100);
        QVERIFY(!start.value(u"ok"_s).toBool());
        QCOMPARE(start.value(u"text"_s).toString(), QString());
        QVERIFY(!start.value(u"truncated"_s).toBool());
        Workspace closed;
        QVERIFY(!closed.readTextStart(u"a.svg"_s, 100).value(u"ok"_s).toBool());
    }

    void changeSignals()
    {
        QString root;
        auto ws = open({ { u"a.md"_s, u"- a"_s } }, &root);
        QSignalSpy fileChanged(ws.get(), &brulion::EditorHost::fileChanged);
        QSignalSpy filesChanged(ws.get(), &Workspace::filesChanged);
        QSignalSpy revision(ws.get(), &Workspace::revisionChanged);

        // An editor's save is announced too: the same page may be open in a second pane (the
        // side panel), which must reload. The saving editor recognises its own text.
        QVERIFY(ws->write(u"a.md"_s, u"- a2"_s));
        QCOMPARE(fileChanged.size(), 1);
        QCOMPARE(fileChanged.takeFirst().first().toString(), u"a.md"_s);
        QCOMPARE(filesChanged.size(), 1);
        QCOMPARE(revision.size(), 1);
        QVERIFY(ws->write(u"a.md"_s, u"- a2"_s)); // unchanged bytes: nothing happened
        QCOMPARE(fileChanged.size(), 0);

        // So is another view's write.
        QVERIFY(ws->writeFile(u"a.md"_s, u"- a3"_s));
        QCOMPARE(fileChanged.size(), 1);
        QCOMPARE(fileChanged.takeFirst().first().toString(), u"a.md"_s);

        // So is a change by another program (Syncthing, an editor), found by a rescan.
        writeRaw(root + u"/z zewnątrz.md"_s, "- przyszło z Syncthing\n");
        ws->vault()->rescan();
        QCOMPARE(fileChanged.size(), 1);
        QCOMPARE(fileChanged.takeFirst().first().toString(), u"z zewnątrz.md"_s);
        QCOMPARE(ws->resolveLink(u"z zewnątrz"_s, QString()), u"z zewnątrz.md"_s); // indexed

        // Renames report both paths; deletions too.
        QSignalSpy renamed(ws.get(), &Workspace::pathRenamed);
        QSignalSpy removed(ws.get(), &Workspace::pathRemoved);
        QVERIFY(!ws->renamePage(u"a.md"_s, u"b"_s).isEmpty());
        QCOMPARE(strings(renamed.first()), (QStringList{ u"a.md"_s, u"b.md"_s }));
        QCOMPARE(strings(fileChanged.takeFirst()), QStringList{ u"a.md"_s });
        QCOMPARE(strings(fileChanged.takeFirst()), QStringList{ u"b.md"_s });
        QVERIFY(!ws->deletePage(u"b.md"_s).isEmpty());
        QCOMPARE(strings(removed.first()), QStringList{ u"b.md"_s });
    }

    void watchesOpenPagesAndRescansInTheForeground()
    {
        // Other programs that rewrite notes in place (no new file renamed over the old one, so
        // nothing a folder watch sees): the pages open in editors are watched themselves, the
        // rest is found when the app comes back to the foreground.
        QString root;
        auto ws = open({{u"Otwarta.md"_s, u"- otwarta"_s}, {u"Zamknięta.md"_s, u"- zamknięta"_s}}, &root);
        ws->editorOpened(u"Otwarta.md"_s); // as an editor (PageController) does
        bool ok = false;
        QCOMPARE(ws->read(u"Otwarta.md"_s, &ok), u"- otwarta"_s);
        QVERIFY(ok);
        QVERIFY(ws->vault()->watchedPaths().contains(root + u"/Otwarta.md"_s));
        QVERIFY(!ws->vault()->watchedPaths().contains(root + u"/Zamknięta.md"_s));
        QSignalSpy fileChanged(ws.get(), &brulion::EditorHost::fileChanged);

        QVERIFY(writeRaw(root + u"/Otwarta.md"_s, "- otwarta, zmieniona w innym edytorze"));
        QTRY_COMPARE(fileChanged.size(), 1);
        QCOMPARE(fileChanged.takeFirst().first().toString(), u"Otwarta.md"_s);

        // Still in the foreground: a focus change within the app does not rescan.
        ws->applicationStateChanged(Qt::ApplicationActive);
        QVERIFY(writeRaw(root + u"/Zamknięta.md"_s, "- zamknięta, teraz z linkiem do [[Cel]]"));
        ws->applicationStateChanged(Qt::ApplicationActive);
        QCOMPARE(fileChanged.size(), 0);
        // Away and back: the closed page's new content is found and indexed.
        ws->applicationStateChanged(Qt::ApplicationInactive);
        ws->applicationStateChanged(Qt::ApplicationSuspended);
        QCOMPARE(fileChanged.size(), 0);
        ws->applicationStateChanged(Qt::ApplicationActive);
        QCOMPARE(fileChanged.size(), 1);
        QCOMPARE(fileChanged.takeFirst().first().toString(), u"Zamknięta.md"_s);
        QCOMPARE(ws->read(u"Zamknięta.md"_s, &ok), u"- zamknięta, teraz z linkiem do [[Cel]]"_s);
        // A new page open in an editor is watched once its first save created it.
        ws->editorOpened(u"Nowa.md"_s);
        QVERIFY(ws->write(u"Nowa.md"_s, u"- nowa"_s));
        QVERIFY(ws->vault()->watchedPaths().contains(root + u"/Nowa.md"_s));
    }

    void watchesOnlyPagesShownInEditors()
    {
        // Review regression (perf plan N3): every read and save named its page to the vault's
        // bounded list of watched files, so embeds, Tasks, Calendar or an export that read or
        // saved many other pages pushed the pages open in editors out of it.
        QHash<QString, QString> files{{u"Otwarta.md"_s, u"- otwarta"_s}};
        const int others = brulion::Vault::maxWatchedFiles() + 10;
        for (int i = 0; i < others; ++i)
            files.insert(u"Strona %1.md"_s.arg(i), u"- [ ] zadanie %1"_s.arg(i));
        QString root;
        auto ws = open(files, &root);
        const auto watchedPages = [&] {
            QStringList pages = ws->vault()->watchedPaths().filter(u".md"_s);
            for (QString &page : pages)
                page = page.mid(root.size() + 1);
            return pages;
        };
        ws->editorOpened(u"Otwarta.md"_s);
        ws->editorOpened(u"Otwarta.md"_s); // also in the side panel
        for (int i = 0; i < others; ++i) {
            bool ok = false;
            const QString page = u"Strona %1.md"_s.arg(i);
            QCOMPARE(ws->read(page, &ok), u"- [ ] zadanie %1"_s.arg(i));
            QVERIFY(ws->write(page, u"- [x] zadanie %1"_s.arg(i)));
        }
        QCOMPARE(watchedPages(), QStringList{u"Otwarta.md"_s});

        QSignalSpy fileChanged(ws.get(), &brulion::EditorHost::fileChanged);
        QVERIFY(writeRaw(root + u"/Otwarta.md"_s, "- otwarta, zmieniona w innym edytorze"));
        QTRY_COMPARE(fileChanged.size(), 1);
        QCOMPARE(fileChanged.first().first().toString(), u"Otwarta.md"_s);

        ws->editorClosed(u"Otwarta.md"_s);
        QCOMPARE(watchedPages(), QStringList{u"Otwarta.md"_s}); // the side panel still shows it
        ws->editorClosed(u"Otwarta.md"_s);
        QCOMPARE(watchedPages(), QStringList());
        ws->editorClosed(u"Otwarta.md"_s); // unbalanced: ignored
        ws->editorOpened(u"Zażółć.md"_s);
        QVERIFY(ws->write(u"Zażółć.md"_s, u"- nowa"_s));
        QCOMPARE(watchedPages(), QStringList{u"Zażółć.md"_s});

        // A page open while another vault opens is watched there too.
        ws->editorOpened(u"Otwarta.md"_s);
        QString error;
        QVERIFY2(ws->open(root, m_tmp.filePath(u"state-again"_s), &error), qPrintable(error));
        QCOMPARE(watchedPages(), (QStringList{u"Otwarta.md"_s, u"Zażółć.md"_s}));
    }

    void attachments()
    {
        QString root;
        auto ws = open({}, &root);
        const QByteArray png("\x89PNG\r\n\x1a\nfake", 12);
        const QString first = ws->saveAttachment(u"zdjęcie.png"_s, png);
        QCOMPARE(first, u"assets/zdjęcie.png"_s);
        QCOMPARE(readRaw(root + u"/assets/zdjęcie.png"_s), png);
        const QString second = ws->saveAttachment(u"/home/x/zdjęcie.png"_s, "inne");
        QCOMPARE(second, u"assets/zdjęcie 2.png"_s);
        const QString unnamed = ws->saveAttachment(QString(), "x");
        QCOMPARE(unnamed, u"assets/attachment-20261014-103000"_s);
        // Pasted images often come without a name or extension: the type is taken from the
        // bytes, so the embed shows an image and other apps know the file.
        QCOMPARE(ws->saveAttachment(QString(), png), u"assets/attachment-20261014-103000.png"_s);
        QCOMPARE(ws->saveAttachment(u"wklejony obraz"_s, png), u"assets/wklejony obraz.png"_s);
        QCOMPARE(ws->saveAttachment(u"notatki"_s, "zwykły tekst"), u"assets/notatki"_s);
        const QString odd = ws->saveAttachment(u"a:b?c.txt"_s, "x");
        QCOMPARE(odd, u"assets/a b c.txt"_s);

        // Another attachments folder from the vault config.
        QVERIFY(ws->setConfig({ { u"attachmentsFolder"_s, u"załączniki/2026"_s } }));
        QCOMPARE(ws->saveAttachment(u"plik.pdf"_s, "%PDF"), u"załączniki/2026/plik.pdf"_s);

        // file:// URLs on desktop/Android, data: URLs in the browser.
        ws->setDataUrlAssets(false);
        QCOMPARE(ws->assetUrl(first), QUrl::fromLocalFile(QDir(root).absoluteFilePath(u"assets/zdjęcie.png"_s)));
        ws->setDataUrlAssets(true);
        const QUrl data = ws->assetUrl(first);
        QCOMPARE(data.scheme(), u"data"_s);
        QVERIFY(data.toString().startsWith(u"data:image/png;base64,"_s));
        QVERIFY(data.toString().endsWith(QString::fromLatin1(png.toBase64())));
        QVERIFY(ws->assetUrl(u"assets/brak.png"_s).isEmpty());
        // The cache follows changes of the file.
        QVERIFY(ws->vault()->writeBinary(first, "nowe bajty"));
        QVERIFY(ws->assetUrl(first).toString().endsWith(QString::fromLatin1(QByteArray("nowe bajty").toBase64())));

        // The browser: a picture's size comes with its data: URL (the editor gives the embed that
        // size before the picture has loaded); not a picture, or a broken one: unknown.
        const auto pngOf = [](QSize size) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(Qt::darkGreen);
            QBuffer buffer;
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            return buffer.data();
        };
        QVERIFY(ws->vault()->writeBinary(u"assets/mała łąka.png"_s, pngOf(QSize(37, 21))));
        QCOMPARE(ws->assetPixelSize(u"assets/mała łąka.png"_s), QSize(37, 21));
        QVERIFY(!ws->assetPixelSize(first).isValid()); // "nowe bajty"
        QVERIFY(!ws->assetPixelSize(u"assets/notatki"_s).isValid());
        QVERIFY(!ws->assetPixelSize(u"assets/brak.png"_s).isValid());
        QVERIFY(ws->vault()->writeBinary(u"assets/mała łąka.png"_s, pngOf(QSize(5, 300))));
        QCOMPARE(ws->assetPixelSize(u"assets/mała łąka.png"_s), QSize(5, 300));
        // Desktop / Android: the editor reads the local file's header itself.
        ws->setDataUrlAssets(false);
        QVERIFY(!ws->assetPixelSize(u"assets/mała łąka.png"_s).isValid());
    }

    void decodablePictures_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QByteArray>("format"); // what QImageReader has to offer; "" for no picture
        QTest::newRow("png") << u"assets/żółw.png"_s << QByteArray("png");
        QTest::newRow("PNG, upper case") << u"assets/ŁĄKA.PNG"_s << QByteArray("png");
        QTest::newRow("jpg") << u"assets/zdjęcie.jpg"_s << QByteArray("jpg");
        QTest::newRow("jpeg") << u"zdjęcie.jpeg"_s << QByteArray("jpeg");
        QTest::newRow("gif") << u"assets/animacja.gif"_s << QByteArray("gif");
        QTest::newRow("bmp") << u"skan.bmp"_s << QByteArray("bmp");
        QTest::newRow("svg") << u"assets/plan-mieszkania.svg"_s << QByteArray("svg");
        QTest::newRow("ico") << u"assets/favicon.ico"_s << QByteArray("ico");
        QTest::newRow("webp (a plugin: often missing)") << u"assets/łąka.webp"_s << QByteArray("webp");
        QTest::newRow("avif (not in Qt itself)") << u"assets/łąka.avif"_s << QByteArray("avif");
        QTest::newRow("a note") << u"notatka.md"_s << QByteArray();
        QTest::newRow("xpm: Qt decodes it, but it is no attachment picture") << u"ikona.xpm"_s << QByteArray();
        QTest::newRow("empty") << QString() << QByteArray();
    }
    void decodablePictures()
    {
        // The picture view names a format this device cannot decode instead of failing to load it.
        QFETCH(QString, path);
        QFETCH(QByteArray, format);
        auto ws = open();
        const bool expected = !format.isEmpty() && QImageReader::supportedImageFormats().contains(format);
        QCOMPARE(ws->canDecodePicture(path), expected);
        // Qt's own decoders (built into QtGui) are always there.
        if (format == "png" || format == "bmp")
            QVERIFY(ws->canDecodePicture(path));
    }

    void templatesAndJournals()
    {
        auto ws = open({ { u"templates/journal.md"_s, u"## {{weekday}}, {{date:d MMMM yyyy}}\n- \n"_s },
                         { u"templates/Spotkanie.md"_s, u"# {{title}}\n"_s },
                         { u"Inna.md"_s, u""_s } });
        const QVariantList templates = ws->templates();
        QCOMPARE(templates.size(), 2);
        QCOMPARE(templates.first().toMap().value(u"title"_s).toString(), u"journal"_s);
        QCOMPARE(templates.last().toMap().value(u"path"_s).toString(), u"templates/Spotkanie.md"_s);

        QCOMPARE(ws->journalPath(u"2026-10-16"_s), u"journals/2026-10-16.md"_s);
        QVERIFY(ws->journalPath(u"16.10.2026"_s).isEmpty());
        const QString path = ws->ensureJournal(u"2026-10-16"_s);
        QCOMPARE(path, u"journals/2026-10-16.md"_s);
        QCOMPARE(ws->readFile(path), u"## piątek, 16 października 2026\n- \n"_s);
        QCOMPARE(ws->journalDate(path), u"2026-10-16"_s);
        QVERIFY(ws->journalDate(u"Inna.md"_s).isEmpty());

        // An existing journal is never overwritten.
        QVERIFY(ws->writeFile(path, u"- moje\n"_s));
        QCOMPARE(ws->ensureJournal(u"2026-10-16"_s), path);
        QCOMPARE(ws->readFile(path), u"- moje\n"_s);

        // English template names; without a template the journal starts empty.
        ws->setUiLanguage(u"en"_s);
        QCOMPARE(ws->readFile(ws->ensureJournal(u"2026-10-17"_s)), u"## Saturday, 17 October 2026\n- \n"_s);
        QVERIFY(ws->setConfig({ { u"journalTemplate"_s, u"templates/nie ma.md"_s } }));
        QCOMPARE(ws->readFile(ws->ensureJournal(u"2026-10-18"_s)), QString());
        QVERIFY(ws->ensureJournal(u"nonsense"_s).isEmpty());

        // Another file name format and folder.
        QVERIFY(ws->setConfig({ { u"journalsFolder"_s, u"Dziennik"_s }, { u"journalFileFormat"_s, u"dd.MM.yyyy"_s } }));
        QCOMPARE(ws->ensureJournal(u"2026-10-19"_s), u"Dziennik/19.10.2026.md"_s);
        QCOMPARE(ws->journalDate(u"Dziennik/19.10.2026.md"_s), u"2026-10-19"_s);
    }

    void journalUnderAnotherName()
    {
        // A Logseq vault names daily notes "2026_10_14.md"; a vault synced from another device
        // may have "Journals/" capitalised. That day's note exists already: no second one is
        // created next to it, and quick capture appends to it.
        auto ws = open({ { u"journals/2026_10_14.md"_s, u"- z Logseqa\n"_s },
                         { u"Journals/2026-10-15.md"_s, u"- z dużej litery\n"_s },
                         { u"templates/journal.md"_s, u"## Plan dnia\n- \n"_s } });
        QCOMPARE(ws->ensureJournal(u"2026-10-14"_s), u"journals/2026_10_14.md"_s);
        QVERIFY(!ws->exists(u"journals/2026-10-14.md"_s));
        QCOMPARE(ws->captureToJournal(u"Zadzwonić do Żanety"_s), u"journals/2026_10_14.md"_s);
        QCOMPARE(ws->readFile(u"journals/2026_10_14.md"_s), u"- z Logseqa\n- Zadzwonić do Żanety\n"_s);
        QCOMPARE(ws->ensureJournal(u"2026-10-15"_s), u"Journals/2026-10-15.md"_s);
        QCOMPARE(ws->readFile(u"Journals/2026-10-15.md"_s), u"- z dużej litery\n"_s);
        // A day without a note still gets one under the configured name, from the template.
        QCOMPARE(ws->ensureJournal(u"2026-10-16"_s), u"journals/2026-10-16.md"_s);
        QCOMPARE(ws->readFile(u"journals/2026-10-16.md"_s), u"## Plan dnia\n- \n"_s);
        QCOMPARE(ws->vault()->markdownFiles().size(), 4);
    }

    void captureToJournal()
    {
        auto ws = open({ { u"templates/journal.md"_s, u"## Notatki\n- \n"_s } });
        const QString path = ws->captureToJournal(u"Kupić chleb"_s);
        QCOMPARE(path, u"journals/2026-10-14.md"_s);
        // The template's empty bullet is filled, not left above.
        QCOMPARE(ws->readFile(path), u"## Notatki\n- Kupić chleb\n"_s);
        ws->captureToJournal(u"\n  Zadzwonić do Łukasza\nw sprawie remontu\n\n"_s);
        QCOMPARE(ws->readFile(path), u"## Notatki\n- Kupić chleb\n- Zadzwonić do Łukasza\n  w sprawie remontu\n"_s);
        ws->captureToJournal(u"- [ ] zadanie z markerem"_s);
        QVERIFY(ws->readFile(path).endsWith(u"\n- [ ] zadanie z markerem\n"_s));
        QVERIFY(ws->captureToJournal(u"  \n "_s).isEmpty());

        // CRLF files stay CRLF; a missing final newline is added first.
        QVERIFY(ws->writeFile(path, u"- a\r\n- b"_s));
        ws->captureToJournal(u"c"_s);
        QCOMPARE(ws->readFile(path), u"- a\r\n- b\r\n- c\r\n"_s);
    }

    void createPages()
    {
        auto ws = open({ { u"Żółw.md"_s, QString() } });
        QCOMPARE(ws->createPage(u"Notatka"_s), u"Notatka.md"_s);
        QCOMPARE(ws->createPage(u"Notatka"_s), u"Notatka 2.md"_s);
        // A name differing only in letter case is the existing page (one file on Android's shared
        // storage), said in a notice; folders are used as spelled.
        QSignalSpy notices(ws.get(), &Workspace::noticeOccurred);
        QCOMPARE(ws->createPage(u"żółw"_s), u"Żółw.md"_s);
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.first().first().toString().contains(u"Żółw"_s));
        QCOMPARE(ws->createPage(u"Projekty/Dom"_s), u"Projekty/Dom.md"_s);
        QCOMPARE(ws->createPage(u"projekty/DOM"_s), u"Projekty/Dom.md"_s);
        QCOMPARE(ws->createPage(u"PROJEKTY/Kuchnia"_s), u"Projekty/Kuchnia.md"_s);
        QCOMPARE(notices.size(), 2);
        QVERIFY(!ws->exists(u"projekty"_s) && !ws->exists(u"PROJEKTY"_s));
        QCOMPARE(ws->createPage(u"Pomysł: #1?"_s), u"Pomysł 1.md"_s);
        QCOMPARE(ws->createPage(u"W folderze"_s, u"Archiwum"_s), u"Archiwum/W folderze.md"_s);
        QVERIFY(ws->createPage(u"  /  "_s).isEmpty());
        QVERIFY(ws->setConfig({ { u"newPagesFolder"_s, u"Nowe"_s } }));
        QCOMPARE(ws->createPage(u"Gdzie?"_s), u"Nowe/Gdzie.md"_s);
        QCOMPARE(ws->createPage(u"W korzeniu"_s, u""_s), u"W korzeniu.md"_s);
        QVERIFY(ws->createFolder(u"Puste/Głębiej"_s));
        QVERIFY(ws->exists(u"Puste/Głębiej"_s));
        // Typing the extension does not double it.
        QCOMPARE(ws->createPage(u"Łąka.md"_s, u""_s), u"Łąka.md"_s);
        QCOMPARE(ws->createPage(u"Projekty/Ogród.MD"_s, u""_s), u"Projekty/Ogród.md"_s);
        QCOMPARE(ws->renamePage(u"Łąka.md"_s, u"Polana.md"_s), u"Polana.md"_s);
    }

    void createPagesInAMode()
    {
        // The new-page dialog's mode: written only when the page would not get it anyway.
        auto ws = open({{u"Żółw.md"_s, u"- istniejący\n"_s}});
        QCOMPARE(ws->createPage(u"Dokument"_s, QString(), u"document"_s), u"Dokument.md"_s);
        QCOMPARE(ws->readFile(u"Dokument.md"_s), QString()); // the default: nothing written
        QCOMPARE(ws->createPage(u"Konspekt"_s, QString(), u"outline"_s), u"Konspekt.md"_s);
        QCOMPARE(ws->readFile(u"Konspekt.md"_s), u"---\nbrulion: outline\n---\n"_s);
        QCOMPARE(ws->createPage(u"Bez trybu"_s, QString(), QString()), u"Bez trybu.md"_s);
        QCOMPARE(ws->readFile(u"Bez trybu.md"_s), QString());
        QCOMPARE(ws->createPage(u"Dziwny"_s, QString(), u"kanban"_s), u"Dziwny.md"_s); // unknown: the default
        QCOMPARE(ws->readFile(u"Dziwny.md"_s), QString());
        QCOMPARE(brulion::markdown::resolveNoteMode(ws->readFile(u"Konspekt.md"_s), false, ws->vault()->config()).mode,
                 brulion::NoteMode::Outline);
        // With outline as the notebook's default, a document needs the key.
        QVERIFY(ws->setConfig({{u"pageMode"_s, u"outline"_s}}));
        QCOMPARE(ws->createPage(u"Artykuł"_s, u"Teksty"_s, u"document"_s), u"Teksty/Artykuł.md"_s);
        QCOMPARE(ws->readFile(u"Teksty/Artykuł.md"_s), u"---\nbrulion: document\n---\n"_s);
        QCOMPARE(ws->createPage(u"Lista"_s, QString(), u"outline"_s), u"Lista.md"_s);
        QCOMPARE(ws->readFile(u"Lista.md"_s), QString());
        // A letter-case twin opens the existing page, which is never rewritten.
        QCOMPARE(ws->createPage(u"żółw"_s, QString(), u"document"_s), u"Żółw.md"_s);
        QCOMPARE(ws->readFile(u"Żółw.md"_s), u"- istniejący\n"_s);
    }

    void renameWhileIndexing()
    {
        // Right after opening, the index is still being built in the background; a rename
        // must still find and rewrite every link to the page (and to pages in a folder).
        const QString root = m_tmp.filePath(u"wczesna-zmiana"_s);
        writeRaw(root + u"/Dom.md"_s, "- x\n");
        writeRaw(root + u"/Projekty/Ogród.md"_s, "- y\n");
        for (int i = 0; i < 30; ++i)
            writeRaw(root + u"/Notatka %1.md"_s.arg(i), "- [[Dom]] i [[Projekty/Ogród]]\n");
        Workspace ws;
        QVERIFY(ws.open(root, QString()));
        QVERIFY(ws.indexing());
        QCOMPARE(ws.renameFolder(u"Projekty"_s, u"Archiwum"_s), u"Archiwum"_s);
        for (int i = 0; i < 30; ++i)
            QCOMPARE(ws.readFile(u"Notatka %1.md"_s.arg(i)), u"- [[Dom]] i [[Archiwum/Ogród]]\n"_s);
        ws.close();

        Workspace again;
        QVERIFY(again.open(root, QString()));
        QVERIFY(again.indexing());
        QCOMPARE(again.renamePage(u"Dom.md"_s, u"Mieszkanie"_s), u"Mieszkanie.md"_s);
        for (int i = 0; i < 30; ++i)
            QCOMPARE(again.readFile(u"Notatka %1.md"_s.arg(i)), u"- [[Mieszkanie]] i [[Archiwum/Ogród]]\n"_s);
    }

    void renameRewritesLinks()
    {
        auto ws = open({
            { u"Projekty/Dom.md"_s, u"# Dom\n- plan ^plan1\n"_s },
            { u"Dziennik.md"_s, u"- [[Dom]] i [[Dom|mój dom]] oraz [[Dom#Dom]]\n- `[[Dom]]` w kodzie zostaje\n"_s },
            { u"Inne.md"_s, u"- [[Projekty/Dom]] i [tekst](Projekty/Dom.md)\n"_s },
            { u"Obcy.md"_s, u"- [[Domek]]\n"_s },
        });
        QSignalSpy fileChanged(ws.get(), &brulion::EditorHost::fileChanged);
        const QString renamed = ws->renamePage(u"Projekty/Dom.md"_s, u"Mieszkanie"_s);
        QCOMPARE(renamed, u"Projekty/Mieszkanie.md"_s);
        QVERIFY(!ws->exists(u"Projekty/Dom.md"_s));
        QCOMPARE(ws->readFile(u"Dziennik.md"_s),
                 u"- [[Mieszkanie]] i [[Mieszkanie|mój dom]] oraz [[Mieszkanie#Dom]]\n- `[[Dom]]` w kodzie zostaje\n"_s);
        QCOMPARE(ws->readFile(u"Inne.md"_s), u"- [[Projekty/Mieszkanie]] i [tekst](Projekty/Mieszkanie.md)\n"_s);
        QCOMPARE(ws->readFile(u"Obcy.md"_s), u"- [[Domek]]\n"_s);
        QCOMPARE(ws->resolveLink(u"Mieszkanie"_s, u"Dziennik.md"_s), renamed);
        QVERIFY(fileChanged.size() >= 3); // the editor of an affected page reloads
        // History moved with the page.
        QVERIFY(ws->write(renamed, u"# Dom\n- zmiana\n"_s));
        QCOMPARE(ws->vault()->history()->list(renamed).size(), 1);

        // Conflicts and no-ops.
        QVERIFY(ws->renamePage(renamed, u"Mieszkanie"_s) == renamed);
        QVERIFY(ws->renamePage(u"Dziennik.md"_s, u"Inne"_s).isEmpty()); // exists
        QVERIFY(ws->renamePage(u"nie ma.md"_s, u"x"_s).isEmpty());
        QCOMPARE(ws->renamePage(u"Obcy.md"_s, u"obcy"_s), u"obcy.md"_s); // case only
        // A name that differs only in case from another page is refused (one file on Android).
        QSignalSpy errors(ws.get(), &Workspace::errorOccurred);
        QVERIFY(ws->renamePage(u"obcy.md"_s, u"dziennik"_s).isEmpty());
        QVERIFY(ws->renamePage(u"obcy.md"_s, u"INNE"_s).isEmpty());
        QCOMPARE(errors.size(), 2);
        QVERIFY(ws->exists(u"obcy.md"_s));
        QVERIFY(ws->renamePage(u"Projekty/Mieszkanie.md"_s, u"ŻÓŁW"_s) == u"Projekty/ŻÓŁW.md"_s); // other folder: fine
    }

    void renameFolderRewritesPathLinks()
    {
        auto ws = open({ { u"Projekty/Dom.md"_s, u"- x\n"_s }, { u"Indeks.md"_s, u"- [[Projekty/Dom]] i [[Dom]]\n"_s } });
        QCOMPARE(ws->renameFolder(u"Projekty"_s, u"Archiwum"_s), u"Archiwum"_s);
        QVERIFY(ws->exists(u"Archiwum/Dom.md"_s));
        QCOMPARE(ws->readFile(u"Indeks.md"_s), u"- [[Archiwum/Dom]] i [[Dom]]\n"_s);
    }

    void deleteAndRestore()
    {
        auto ws = open({ { u"Zbędna.md"_s, u"- treść\n"_s }, { u"Folder/a.md"_s, u"- a\n"_s } });
        ws->setFavorite(u"Zbędna.md"_s, true);
        const QString trashPath = ws->deletePage(u"Zbędna.md"_s);
        QVERIFY(trashPath.startsWith(u".trash/Zbędna"_s));
        QVERIFY(!ws->exists(u"Zbędna.md"_s));
        QVERIFY(!ws->isFavorite(u"Zbędna.md"_s));
        QCOMPARE(ws->restore(trashPath), u"Zbędna.md"_s);
        QCOMPARE(ws->readFile(u"Zbędna.md"_s), u"- treść\n"_s);

        const QString folderTrash = ws->deletePage(u"Folder"_s);
        QVERIFY(folderTrash.startsWith(u".trash/Folder"_s));
        QVERIFY(!folderTrash.contains(u"a.md"_s));
        QCOMPARE(ws->restore(folderTrash), u"Folder"_s);
        QVERIFY(ws->exists(u"Folder/a.md"_s));
        QVERIFY(ws->deletePage(u"nie istnieje.md"_s).isEmpty());
    }

    void suggestionsAndBlocks()
    {
        auto ws = open({
            { u"Projekty/Remont mieszkania.md"_s, u"---\naliases: [Remont]\n---\n- Budżet 12 000 zł ^budzet1\n- Farba #projekt/dom\n"_s },
            { u"Książki.md"_s, u"- Lalka #czytanie #projekt\n- Solaris — Lem\n"_s },
            { u"Żurek.md"_s, u"- zakwas #kuchnia\n"_s },
        });
        // Pages: title, alias and folder.
        const QVariantList pages = ws->suggestPages(u"rem"_s);
        QVERIFY(!pages.isEmpty());
        QCOMPARE(pages.first().toMap().value(u"path"_s).toString(), u"Projekty/Remont mieszkania.md"_s);
        QCOMPARE(pages.first().toMap().value(u"folder"_s).toString(), u"Projekty"_s);
        QCOMPARE(ws->suggestPages(u"zurek"_s).first().toMap().value(u"title"_s).toString(), u"Żurek"_s);
        QVERIFY(ws->suggestPages(u"x"_s, 0).isEmpty());

        // Tags: prefix first (diacritic-insensitive), then "contains"; with counts.
        const QVariantList tags = ws->suggestTags(u"#proj"_s);
        QCOMPARE(tags.size(), 2);
        const QStringList tagNames{ tags.at(0).toMap().value(u"tag"_s).toString(), tags.at(1).toMap().value(u"tag"_s).toString() };
        QVERIFY(tagNames.contains(u"projekt"_s));
        QVERIFY(tagNames.contains(u"projekt/dom"_s));
        QCOMPARE(ws->suggestTags(u"dom"_s).first().toMap().value(u"tag"_s).toString(), u"projekt/dom"_s);
        QCOMPARE(ws->suggestTags(QString(), 2).size(), 2);

        // Blocks.
        const QVariantList blocks = ws->suggestBlocks(u"budzet"_s);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks.first().toMap().value(u"blockId"_s).toString(), u"budzet1"_s);
        QCOMPARE(blocks.first().toMap().value(u"line"_s).toInt(), 3);
        QCOMPARE(ws->blockText(u"budzet1"_s), u"Budżet 12 000 zł"_s);
        QCOMPARE(ws->blockText(u"((budzet1))"_s), u"Budżet 12 000 zł"_s);
        QVERIFY(ws->blockText(u"nieznany"_s).isEmpty());

        // Links.
        QCOMPARE(ws->resolveLink(u"Remont"_s, u"Książki.md"_s), u"Projekty/Remont mieszkania.md"_s);
        QCOMPARE(ws->resolveLink(u"Remont mieszkania#Budżet"_s, QString()), u"Projekty/Remont mieszkania.md"_s);
        QVERIFY(ws->resolveLink(u"Nie ma takiej"_s, QString()).isEmpty());

        // ensureBlockId writes " ^id" into the file (and an editor of that page hears of it).
        QSignalSpy fileChanged(ws.get(), &brulion::EditorHost::fileChanged);
        const QString id = ws->ensureBlockId(u"Książki.md"_s, 1);
        QVERIFY(QRegularExpression(u"^[a-z0-9]{6}$"_s).match(id).hasMatch());
        QCOMPARE(ws->readFile(u"Książki.md"_s), u"- Lalka #czytanie #projekt\n- Solaris — Lem ^"_s + id + u"\n"_s);
        QCOMPARE(fileChanged.size(), 1);
        QCOMPARE(ws->ensureBlockId(u"Książki.md"_s, 1), id); // existing id, no write
        QCOMPARE(fileChanged.size(), 1);
        QCOMPARE(ws->ensureBlockId(u"Projekty/Remont mieszkania.md"_s, 3), u"budzet1"_s);
        QCOMPARE(ws->blockText(id), u"Solaris — Lem"_s);
        QVERIFY(ws->ensureBlockId(u"nie ma.md"_s, 0).isEmpty());
    }

    void config()
    {
        auto ws = open();
        QSignalSpy changed(ws.get(), &Workspace::configChanged);
        QVariantMap c = ws->vaultConfig();
        QCOMPARE(c.value(u"journalsFolder"_s).toString(), u"journals"_s);
        QCOMPARE(c.value(u"journalFileFormat"_s).toString(), u"yyyy-MM-dd"_s);
        QCOMPARE(c.value(u"attachmentsFolder"_s).toString(), u"assets"_s);
        QCOMPARE(c.value(u"templatesFolder"_s).toString(), u"templates"_s);
        QCOMPARE(c.value(u"journalTemplate"_s).toString(), u"templates/journal.md"_s);
        QCOMPARE(c.value(u"defaultReminderTime"_s).toString(), u"09:00"_s);
        QCOMPARE(c.value(u"weekStartsOn"_s).toInt(), 1);
        QCOMPARE(c.value(u"journalMode"_s).toString(), u"outline"_s);
        QCOMPARE(c.value(u"pageMode"_s).toString(), u"document"_s);

        QVERIFY(ws->setConfig({ { u"defaultReminderTime"_s, u"7:30"_s }, { u"weekStartsOn"_s, 7 } }));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(ws->vaultConfig().value(u"defaultReminderTime"_s).toString(), u"07:30"_s);
        QCOMPARE(ws->vaultConfig().value(u"weekStartsOn"_s).toInt(), 7);
        QVERIFY(ws->exists(u".brulion/config.json"_s));

        QVERIFY(!ws->setConfig({ { u"journalFileFormat"_s, u"HH:mm"_s } })); // not a date
        QVERIFY(!ws->setConfig({ { u"defaultReminderTime"_s, u"25:99"_s } }));
        QVERIFY(!ws->setConfig({ { u"weekStartsOn"_s, 9 } }));
        QVERIFY(ws->setConfig({ { u"weekStartsOn"_s, 7 } })); // unchanged
        QCOMPARE(changed.size(), 1);
        QVERIFY(ws->configWarnings().isEmpty());

        // Note modes (Settings → Notatnik), saved in the synced config.json.
        QVERIFY(ws->setConfig({{u"pageMode"_s, u"outline"_s}, {u"journalMode"_s, u"Document"_s}}));
        QCOMPARE(changed.size(), 2);
        QCOMPARE(ws->vaultConfig().value(u"pageMode"_s).toString(), u"outline"_s);
        QCOMPARE(ws->vaultConfig().value(u"journalMode"_s).toString(), u"document"_s);
        QVERIFY(ws->readFile(u".brulion/config.json"_s).contains(u"\"journalMode\": \"document\""_s));
        QVERIFY(!ws->setConfig({{u"pageMode"_s, u"kanban"_s}}));
        QVERIFY(!ws->setConfig({{u"journalMode"_s, QString()}}));
        QCOMPARE(ws->vaultConfig().value(u"pageMode"_s).toString(), u"outline"_s);
        QCOMPARE(changed.size(), 2);
    }

    void favoritesAndRecentPages()
    {
        auto ws = open({ { u"a.md"_s, QString() }, { u"b.md"_s, QString() }, { u"Folder/c.md"_s, QString() } });
        QSignalSpy fav(ws.get(), &Workspace::favoritesChanged);
        ws->setFavorite(u"b.md"_s, true);
        ws->setFavorite(u"a.md"_s, true);
        ws->setFavorite(u"a.md"_s, true);
        QCOMPARE(fav.size(), 2);
        QCOMPARE(ws->favorites().size(), 2);
        QCOMPARE(ws->favorites().first().toMap().value(u"title"_s).toString(), u"b"_s);
        ws->toggleFavorite(u"b.md"_s);
        QVERIFY(!ws->isFavorite(u"b.md"_s));

        ws->notePageOpened(u"a.md"_s);
        ws->notePageOpened(u"Folder/c.md"_s);
        ws->notePageOpened(u"a.md"_s);
        ws->notePageOpened(u"nie ma.md"_s);
        QCOMPARE(ws->recentPages().size(), 2);
        QCOMPARE(ws->recentPages().first().toMap().value(u"path"_s).toString(), u"a.md"_s);

        // They follow renames and survive reopening (device-local store).
        QCOMPARE(ws->renamePage(u"a.md"_s, u"Ala"_s), u"Ala.md"_s);
        QVERIFY(ws->isFavorite(u"Ala.md"_s));
        QCOMPARE(ws->recentPages().first().toMap().value(u"path"_s).toString(), u"Ala.md"_s);
        const QString root = ws->rootPath();
        const QString state = ws->vault()->deviceStatePath();
        ws->close();
        Workspace again;
        QVERIFY(again.open(root, state));
        QVERIFY(again.isFavorite(u"Ala.md"_s));
        QCOMPARE(again.recentPages().size(), 2);

        // Quick switcher: recent pages first for an empty query, suggestions otherwise.
        again.finishIndexing();
        const QVariantList empty = again.switcherPages(QString());
        QCOMPARE(empty.first().toMap().value(u"path"_s).toString(), u"Ala.md"_s);
        QCOMPARE(empty.size(), 3);
        const QVariantList found = again.switcherPages(u"c"_s);
        QCOMPARE(found.first().toMap().value(u"title"_s).toString(), u"c"_s);
        QCOMPARE(found.first().toMap().value(u"subtitle"_s).toString(), u"Folder"_s);
        QCOMPARE(found.first().toMap().value(u"icon"_s).toString(), u"file-text"_s);
    }

    void navigationIsForwarded()
    {
        auto ws = open();
        QSignalSpy nav(ws.get(), &Workspace::navigationRequested);
        ws->navigate({ { u"view"_s, u"tag"_s }, { u"tag"_s, u"kuchnia"_s } }, true);
        QCOMPARE(nav.size(), 1);
        QCOMPARE(nav.first().at(0).toMap().value(u"tag"_s).toString(), u"kuchnia"_s);
        QCOMPARE(nav.first().at(1).toBool(), true);
        ws->openUrl(QUrl(u"javascript:alert(1)"_s)); // refused (only a warning)
    }

    // The editor sends {view: page, target} for a link to a missing page and {view: page,
    // blockId} for a ((block)) of another page; the shell rejects page routes without a path.
    void navigationResolvesPageRoutes()
    {
        auto ws = open({ { u"Projekty/Remont mieszkania.md"_s, u"- Budżet 12 000 zł ^budzet1\n"_s },
                         { u".brulion/config.json"_s, uR"({"version": 1, "newPagesFolder": "strony"})"_s } });
        QSignalSpy nav(ws.get(), &Workspace::navigationRequested);
        const auto routeOf = [&](const QVariantMap &route) {
            nav.clear();
            ws->navigate(route);
            return nav.size() == 1 ? nav.first().at(0).toMap() : QVariantMap();
        };
        const auto pathOf = [&](const QVariantMap &route) { return routeOf(route).value(u"path"_s).toString(); };

        // A missing page: where it will be created (not created yet), Polish name kept.
        QCOMPARE(pathOf({ { u"view"_s, u"page"_s }, { u"path"_s, QString() }, { u"target"_s, u"Pomysły na prezenty"_s } }),
                 u"strony/Pomysły na prezenty.md"_s);
        QVERIFY(!ws->exists(u"strony/Pomysły na prezenty.md"_s));
        // Folders in the name are kept, a typed ".md" is not doubled, unsafe characters go.
        QCOMPARE(ws->newPagePath(u"Projekty/Łódź"_s), u"strony/Projekty/Łódź.md"_s);
        QCOMPARE(ws->newPagePath(u"Żółw.md"_s), u"strony/Żółw.md"_s);
        QVERIFY(!ws->newPagePath(u"a:b?"_s).contains(u'?'));
        QVERIFY(ws->newPagePath(u"  "_s).isEmpty());
        // Folders as the vault spells them (one folder on case-insensitive storage).
        QVERIFY(ws->createFolder(u"strony/Projekty"_s));
        QCOMPARE(ws->newPagePath(u"projekty/Łódź"_s), u"strony/Projekty/Łódź.md"_s);
        // QML calls it too (GraphView opens a missing page's node at its future path).
        QString fromQml;
        QVERIFY(QMetaObject::invokeMethod(ws.get(), "newPagePath", Q_RETURN_ARG(QString, fromQml),
                                          Q_ARG(QString, u"Ogród zimowy"_s)));
        QCOMPARE(fromQml, u"strony/Ogród zimowy.md"_s);
        // A date: that day's journal page.
        QCOMPARE(pathOf({ { u"view"_s, u"page"_s }, { u"target"_s, u"2026-10-20"_s } }), u"journals/2026-10-20.md"_s);
        // An existing page (e.g. the editor asked while the index was still being built).
        QCOMPARE(pathOf({ { u"view"_s, u"page"_s }, { u"target"_s, u"remont mieszkania"_s } }),
                 u"Projekty/Remont mieszkania.md"_s);
        // A block of another page: the page holding it; the id stays for scrolling to it.
        const QVariantMap block = routeOf({ { u"view"_s, u"page"_s }, { u"blockId"_s, u"budzet1"_s } });
        QCOMPARE(block.value(u"path"_s).toString(), u"Projekty/Remont mieszkania.md"_s);
        QCOMPARE(block.value(u"blockId"_s).toString(), u"budzet1"_s);
        // Unknown block, other views and routes with a path are passed on unchanged.
        QVERIFY(pathOf({ { u"view"_s, u"page"_s }, { u"blockId"_s, u"nieznany"_s } }).isEmpty());
        QCOMPARE(pathOf({ { u"view"_s, u"page"_s }, { u"path"_s, u"x.md"_s }, { u"target"_s, u"y"_s } }), u"x.md"_s);
        QCOMPARE(routeOf({ { u"view"_s, u"tag"_s }, { u"tag"_s, u"dom"_s } }).value(u"tag"_s).toString(), u"dom"_s);
    }

    void folderTree()
    {
        auto ws = open({ { u"b.md"_s, QString() },
                         { u"A.md"_s, QString() },
                         { u"Projekty/Dom.md"_s, QString() },
                         { u"Projekty/Archiwum/Stare.md"_s, QString() },
                         { u"assets/zdjęcie.png"_s, u"x"_s },
                         { u"Notatka.sync-conflict-20261014-101010-ABCDEFG.md"_s, QString() } });
        FolderTreeModel model;
        model.setWorkspace(ws.get());
        const auto paths = [&] {
            QStringList out;
            for (int i = 0; i < model.rowCount(); ++i)
                out.append(model.data(model.index(i), FolderTreeModel::PathRole).toString());
            return out;
        };
        // Folders first, then pages; conflict copies and attachment-only folders hidden.
        QCOMPARE(paths(), (QStringList{ u"Projekty"_s, u"A.md"_s, u"b.md"_s }));
        const int projekty = model.indexOf(u"Projekty"_s);
        QCOMPARE(projekty, 0);
        QVERIFY(model.data(model.index(projekty), FolderTreeModel::HasChildrenRole).toBool());
        QCOMPARE(model.data(model.index(projekty), FolderTreeModel::IconRole).toString(), u"folder"_s);

        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.toggle(projekty);
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(paths(), (QStringList{ u"Projekty"_s, u"Projekty/Archiwum"_s, u"Projekty/Dom.md"_s, u"A.md"_s,
                                        u"b.md"_s }));
        QCOMPARE(model.data(model.index(1), FolderTreeModel::DepthRole).toInt(), 1);
        QCOMPARE(model.data(model.index(2), FolderTreeModel::TitleRole).toString(), u"Dom"_s);
        QCOMPARE(model.data(model.index(0), FolderTreeModel::IconRole).toString(), u"folder-open"_s);
        QVERIFY(model.data(model.index(0), FolderTreeModel::ExpandedRole).toBool());

        QCOMPARE(model.reveal(u"Projekty/Archiwum/Stare.md"_s), 2);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        model.toggle(projekty);
        QCOMPARE(removed.size(), 1);
        QCOMPARE(model.count(), 3);
        // Nested expansion is remembered.
        model.toggle(projekty);
        QCOMPARE(model.count(), 6);
        model.toggle(model.indexOf(u"A.md"_s)); // not a folder: nothing happens
        QCOMPARE(model.count(), 6);

        // Changes in the vault refresh it (debounced); an empty new folder shows.
        ws->createPage(u"Projekty/Nowy"_s);
        QTRY_COMPARE(model.count(), 7);
        QVERIFY(ws->createFolder(u"Pusty"_s));
        QTRY_VERIFY(model.indexOf(u"Pusty"_s) >= 0);
        model.setShowAttachments(true);
        QVERIFY(model.indexOf(u"assets"_s) >= 0);
        model.setExpanded(u"assets"_s, true);
        const int image = model.indexOf(u"assets/zdjęcie.png"_s);
        QVERIFY(image >= 0);
        QCOMPARE(model.data(model.index(image), FolderTreeModel::IconRole).toString(), u"image"_s);

        // The expanded folders survive reopening the vault (device-local store).
        const QString root = ws->rootPath();
        const QString state = ws->vault()->deviceStatePath();
        ws->close();
        QCOMPARE(model.count(), 0);
        QVERIFY(ws->open(root, state));
        QVERIFY(model.isExpanded(u"Projekty"_s));
        QVERIFY(model.indexOf(u"Projekty/Archiwum/Stare.md"_s) >= 0);
        model.collapseAll();
        QCOMPARE(paths(), (QStringList{ u"assets"_s, u"Projekty"_s, u"Pusty"_s, u"A.md"_s, u"b.md"_s }));
    }
};

QTEST_GUILESS_MAIN(TestWorkspace)
#include "tst_workspace.moc"
