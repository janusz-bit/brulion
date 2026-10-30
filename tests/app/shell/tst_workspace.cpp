// Workspace — the shell's EditorHost implementation against a real vault folder: reading and
// writing (atomic, History, change signals), external changes, attachments and asset URLs,
// templates and journals, quick capture, pages (create/rename with link rewriting/delete/restore),
// index-backed suggestions and block ids, configuration, favourites, recent pages and the
// sidebar's folder tree; the calendar ranges and queries it keeps caches of.
#include "foldertreemodel.h"
#include "workspace.h"

#include "filesystemtraits.h"
#include "index/index.h"
#include "markdown/markdown.h"
#include "notebook/json.h"
#include "reminders/calendar.h"
#include "vault/vault.h"

#include <QAbstractItemModelTester>
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

    /// folderInfo()'s specials as "special:path".
    static QStringList specials(const QVariantMap &info)
    {
        QStringList out;
        for (const QVariant &v : info.value(u"specials"_s).toList())
            out.append(v.toMap().value(u"special"_s).toString() + u':' + v.toMap().value(u"path"_s).toString());
        return out;
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

    void importAttachment_data()
    {
        // A file picked for a drawing's backdrop (Workspace::importAttachment).
        QTest::addColumn<QString>("source"); // "vault:<path>" a file of the vault, "out:<name>" one outside
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("expected");
        QTest::newRow("inside the vault: used where it is")
                << u"vault:Wykłady/Fizyka – wykład 3.pdf"_s << QByteArray("%PDF-1.7 wykład")
                << u"Wykłady/Fizyka – wykład 3.pdf"_s;
        QTest::newRow("same bytes in the attachments folder: reused")
                << u"out:slajdy.pdf"_s << QByteArray("%PDF-1.7 slajdy") << u"assets/slajdy.pdf"_s;
        QTest::newRow("another file of that name: a free name")
                << u"out:slajdy.pdf"_s << QByteArray("%PDF-1.7 inne slajdy") << u"assets/slajdy 2.pdf"_s;
        QTest::newRow("a new file: copied")
                << u"out:zdjęcie tablicy.jpg"_s << QByteArray("\xff\xd8\xff jpeg") << u"assets/zdjęcie tablicy.jpg"_s;
        QTest::newRow("missing file") << u"out:-"_s << QByteArray() << QString();
    }

    void importAttachment()
    {
        QFETCH(QString, source);
        QFETCH(QByteArray, bytes);
        QFETCH(QString, expected);
        QString root;
        auto ws = open({}, &root);
        // Already there: the lecture's slides.
        QVERIFY(ws->vault()->createBinary(u"assets/slajdy.pdf"_s, "%PDF-1.7 slajdy") == u"assets/slajdy.pdf"_s);
        QUrl url;
        if (source.startsWith(u"vault:"_s)) {
            QVERIFY(writeRaw(root + u'/' + source.mid(6), bytes));
            url = QUrl::fromLocalFile(root + u'/' + source.mid(6));
        } else if (source == u"out:-") {
            url = QUrl::fromLocalFile(m_tmp.filePath(u"nie ma.pdf"_s));
        } else {
            const QString file = m_tmp.filePath(u"pobrane-%1/"_s.arg(m_n) + source.mid(4));
            QVERIFY(writeRaw(file, bytes));
            url = QUrl::fromLocalFile(file);
        }
        QCOMPARE(ws->importAttachment(url), expected);
        if (!expected.isEmpty())
            QCOMPARE(readRaw(root + u'/' + expected), bytes);
        // By bytes (the browser's picker): the same rules but the vault's own files.
        if (source.startsWith(u"out:"_s) && !bytes.isEmpty()) {
            const QString again = ws->importAttachment(source.mid(4), bytes);
            QCOMPARE(again, expected); // (now it is there: reused)
        }
        // The file system path of a vault path.
        QCOMPARE(ws->absoluteFilePath(u"assets/slajdy.pdf"_s), QDir(root).absoluteFilePath(u"assets/slajdy.pdf"_s));
        QCOMPARE(ws->absoluteFilePath(QString()), QString());
    }

    void importedCopyDiscarded()
    {
        // The pick of a drawing's backdrop abandoned (the page picker cancelled, a locked PDF):
        // only the copy importAttachment() stored for it goes, never a file of the notebook or an
        // attachment it reused.
        QString root;
        auto ws = open({}, &root);
        QVERIFY(ws->vault()->createBinary(u"assets/slajdy.pdf"_s, "%PDF-1.7 slajdy") == u"assets/slajdy.pdf"_s);
        QVERIFY(writeRaw(root + u"/Wykłady/Fizyka – wykład 3.pdf"_s, "%PDF-1.7 wykład"));
        const QString outside = m_tmp.filePath(u"pobrane-porzucone/"_s);
        QVERIFY(writeRaw(outside + u"Wykład 5 – fale.pdf"_s, "%PDF-1.7 fale"));
        QVERIFY(writeRaw(outside + u"slajdy.pdf"_s, "%PDF-1.7 slajdy"));

        // A copy, then abandoned: gone (the picked file stays where it was).
        const QString copy = ws->importAttachment(QUrl::fromLocalFile(outside + u"Wykład 5 – fale.pdf"_s));
        QCOMPARE(copy, u"assets/Wykład 5 – fale.pdf"_s);
        ws->discardImport(copy);
        QVERIFY(!ws->exists(copy));
        QVERIFY(!QFileInfo::exists(root + u"/assets/Wykład 5 – fale.pdf"_s));
        QVERIFY(QFileInfo::exists(outside + u"Wykład 5 – fale.pdf"_s));
        QVERIFY(ws->vault()->trash().isEmpty()); // (not to the trash: it was only a copy)
        ws->discardImport(copy);                 // twice: nothing

        // The notebook's own file and a reused attachment stay.
        const QString own = ws->importAttachment(QUrl::fromLocalFile(root + u"/Wykłady/Fizyka – wykład 3.pdf"_s));
        QCOMPARE(own, u"Wykłady/Fizyka – wykład 3.pdf"_s);
        ws->discardImport(own);
        QVERIFY(ws->exists(own));
        const QString reused = ws->importAttachment(QUrl::fromLocalFile(outside + u"slajdy.pdf"_s));
        QCOMPARE(reused, u"assets/slajdy.pdf"_s);
        ws->discardImport(reused);
        QVERIFY(ws->exists(reused));
        // Nor any other path.
        QVERIFY(ws->write(u"Notatka.md"_s, u"- zostaje"_s));
        ws->discardImport(u"Notatka.md"_s);
        QVERIFY(ws->exists(u"Notatka.md"_s));

        // A copy that a later pick used again (its drawings made): kept.
        const QString kept = ws->importAttachment(QUrl::fromLocalFile(outside + u"Wykład 5 – fale.pdf"_s));
        QCOMPARE(kept, u"assets/Wykład 5 – fale.pdf"_s);
        QCOMPARE(ws->importAttachment(QUrl::fromLocalFile(outside + u"Wykład 5 – fale.pdf"_s)), kept);
        ws->discardImport(kept);
        QVERIFY(ws->exists(kept));

        // The browser's pick (bytes): the same.
        const QString picked = ws->importAttachment(u"tablica – ćwiczenia.png"_s, QByteArray("\x89PNG tablica"));
        QCOMPARE(picked, u"assets/tablica – ćwiczenia.png"_s);
        ws->discardImport(picked);
        QVERIFY(!ws->exists(picked));

        // Another notebook opened meanwhile: the old pick's path means nothing there.
        const QString before = ws->importAttachment(QUrl::fromLocalFile(outside + u"Wykład 5 – fale.pdf"_s));
        QVERIFY(ws->exists(before));
        QString error;
        QVERIFY2(ws->open(root, m_tmp.filePath(u"state-porzucone"_s), &error), qPrintable(error));
        ws->discardImport(before);
        QVERIFY(ws->exists(before));
    }

    void importAndroidDocument()
    {
        // Android's picker names the notebook's own files by content:// URIs: one that says which
        // file it is (Downloads' "raw:" path here; the phone's storage in tst_documentpaths) is used
        // where it is — no copy in the attachments folder.
        QString root;
        auto ws = open({}, &root);
        QVERIFY(writeRaw(root + u"/Wykłady/Fizyka – wykład 3.pdf"_s, "%PDF-1.7 wykład"));
        const QString canonical = QFileInfo(root).canonicalFilePath() + u"/Wykłady/Fizyka – wykład 3.pdf"_s;
        const QUrl document(u"content://com.android.providers.downloads.documents/document/raw%3A"_s
                            + QString::fromLatin1(QUrl::toPercentEncoding(canonical)));
        QCOMPARE(ws->importAttachment(document), u"Wykłady/Fizyka – wykład 3.pdf"_s);
        QVERIFY(!QFileInfo::exists(root + u"/assets/Fizyka – wykład 3.pdf"_s));
        ws->discardImport(u"Wykłady/Fizyka – wykład 3.pdf"_s);
        QVERIFY(ws->exists(u"Wykłady/Fizyka – wykład 3.pdf"_s));
        // A document that names no file (MediaStore) is read as a document — which this desktop
        // cannot: nothing stored.
        QCOMPARE(ws->importAttachment(QUrl(u"content://com.android.providers.media.documents/document/image%3A42"_s)),
                 QString());
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
        BRULION_SKIP_IF_CASE_INSENSITIVE(m_tmp.path()); // "journals/" next to "Journals/"
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
        QVERIFY(!ws->createFolder(u"Puste/Głębiej"_s).isEmpty());
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
        // A tag names a page: pages are offered too, after the tags that start with the text, once
        // (a tag naming a page is that page), never by a fuzzy match.
        const QVariantList zurek = ws->suggestTags(u"zur"_s);
        QCOMPARE(zurek.size(), 1);
        QCOMPARE(zurek.first().toMap().value(u"tag"_s).toString(), u"Żurek"_s);
        QCOMPARE(zurek.first().toMap().value(u"kind"_s).toString(), u"page"_s);
        QCOMPARE(zurek.first().toMap().value(u"path"_s).toString(), u"Żurek.md"_s);
        const QVariantList remont = ws->suggestTags(u"remont"_s);
        QVERIFY(!remont.isEmpty());
        for (const QVariant &v : remont)
            QCOMPARE(v.toMap().value(u"path"_s).toString(), u"Projekty/Remont mieszkania.md"_s);
        QVERIFY(ws->suggestTags(u"qqq"_s).isEmpty());

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

    void configFromObsidian()
    {
        // An Obsidian vault without .brulion/config.json: its settings are used, nothing written.
        QString root;
        auto ws = open({{u".obsidian/daily-notes.json"_s, uR"({"folder":"Dziennik","format":"DD.MM.YYYY"})"_s},
                        {u".obsidian/app.json"_s, uR"({"attachmentFolderPath":"Załączniki"})"_s},
                        {u"Dziennik/14.10.2026.md"_s, u"- środa"_s}},
                       &root);
        QCOMPARE(ws->vaultConfig().value(u"journalsFolder"_s).toString(), u"Dziennik"_s);
        QCOMPARE(ws->vaultConfig().value(u"journalFileFormat"_s).toString(), u"dd.MM.yyyy"_s);
        QCOMPARE(ws->vaultConfig().value(u"attachmentsFolder"_s).toString(), u"Załączniki"_s);
        QCOMPARE(ws->configFromObsidian(), (QStringList{u"journalsFolder"_s, u"journalFileFormat"_s,
                                                         u"journalTemplate"_s, u"attachmentsFolder"_s}));
        QCOMPARE(ws->journalPath(u"2026-10-14"_s), u"Dziennik/14.10.2026.md"_s);
        QVERIFY(!ws->exists(u".brulion/config.json"_s));
        // A setting set to the value it has writes nothing.
        QVERIFY(ws->setConfig({{u"journalsFolder"_s, u"Dziennik"_s}}));
        QVERIFY(!QFileInfo::exists(root + u"/.brulion/config.json"_s));

        // "Save to the notebook": the values in use become the file's.
        QSignalSpy changed(ws.get(), &Workspace::configChanged);
        QVERIFY(ws->saveAdoptedConfig());
        QVERIFY(ws->readFile(u".brulion/config.json"_s).contains(u"\"journalFileFormat\": \"dd.MM.yyyy\""_s));
        QVERIFY(ws->configFromObsidian().isEmpty());
        QCOMPARE(ws->vaultConfig().value(u"journalsFolder"_s).toString(), u"Dziennik"_s);
        QCOMPARE(changed.size(), 1);
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
        // A tag opens the page it names (here one not written yet); the overview stays a tag route.
        ws->navigate({ { u"view"_s, u"tag"_s }, { u"tag"_s, u"kuchnia"_s } }, true);
        QCOMPARE(nav.size(), 1);
        QCOMPARE(nav.first().at(0).toMap(),
                 QVariantMap({{u"view"_s, u"page"_s}, {u"path"_s, u"kuchnia.md"_s}, {u"fromTag"_s, u"kuchnia"_s}}));
        QCOMPARE(nav.first().at(1).toBool(), true);
        ws->navigate({{u"view"_s, u"tag"_s}, {u"tag"_s, u"kuchnia"_s}, {u"overview"_s, true}});
        QCOMPARE(nav.last().at(0).toMap().value(u"view"_s).toString(), u"tag"_s);
        QCOMPARE(nav.last().at(0).toMap().value(u"tag"_s).toString(), u"kuchnia"_s);
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
        QVERIFY(!ws->createFolder(u"strony/Projekty"_s).isEmpty());
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
        QCOMPARE(routeOf({{u"view"_s, u"tags"_s}}).value(u"view"_s).toString(), u"tags"_s);
    }

    // [[Page#^id]] after the block was moved to another page: the route follows the id (the
    // index knows where it is now); ids the linked page has, unknown ids and conflict copies do not
    // move it.
    void movedBlocksAreFollowed()
    {
        auto ws =
                open({{u"Projekty/Remont.md"_s, u"- Budżet ^budzet1\n"_s},
                      {u"Archiwum/Stary remont.md"_s, u"- Przeniesiony blok z łazienką ^moved1\n- \n  id:: pusty1\n"_s},
                      {u"Notatki.sync-conflict-20261014-101010-ABCDEFG.md"_s, u"- kopia ^konfl1\n"_s}});
        QSignalSpy nav(ws.get(), &Workspace::navigationRequested);
        const auto routeOf = [&](const QString &path, const QString &id) {
            nav.clear();
            ws->navigate({{u"view"_s, u"page"_s}, {u"path"_s, path}, {u"target"_s, u"Remont"_s}, {u"blockId"_s, id}});
            return nav.size() == 1 ? nav.first().at(0).toMap() : QVariantMap();
        };
        const QVariantMap moved = routeOf(u"Projekty/Remont.md"_s, u"moved1"_s);
        QCOMPARE(moved.value(u"path"_s).toString(), u"Archiwum/Stary remont.md"_s);
        QCOMPARE(moved.value(u"blockId"_s).toString(), u"moved1"_s);
        QVERIFY(!moved.contains(u"target"_s));
        // A block without text is found too (blockIdTaken: the index, not the block's text).
        QCOMPARE(routeOf(u"Projekty/Remont.md"_s, u"pusty1"_s).value(u"path"_s).toString(),
                 u"Archiwum/Stary remont.md"_s);
        QVERIFY(ws->blockIdTaken(u"pusty1"_s));
        QVERIFY(ws->blockText(u"pusty1"_s).isEmpty());
        QVERIFY(ws->blockIdTaken(u"budzet1"_s));
        QVERIFY(!ws->blockIdTaken(u"konfl1"_s)); // conflict copies are no pages
        QVERIFY(!ws->blockIdTaken(QString()));
        // Still on the linked page, unknown, or only in a conflict copy: the route is unchanged.
        QCOMPARE(routeOf(u"Projekty/Remont.md"_s, u"budzet1"_s).value(u"path"_s).toString(), u"Projekty/Remont.md"_s);
        QCOMPARE(routeOf(u"Projekty/Remont.md"_s, u"nieznany"_s).value(u"path"_s).toString(), u"Projekty/Remont.md"_s);
        QCOMPARE(routeOf(u"Projekty/Remont.md"_s, u"konfl1"_s).value(u"path"_s).toString(), u"Projekty/Remont.md"_s);
        // A page not written yet keeps its route as well.
        QCOMPARE(routeOf(u"Nowa.md"_s, u"moved1"_s).value(u"path"_s).toString(), u"Nowa.md"_s);
        // The label of [[Page#^id]]: the page's block, else the block where the id is now.
        QCOMPARE(ws->linkedBlockText(u"Projekty/Remont.md"_s, u"budzet1"_s), u"Budżet"_s);
        QCOMPARE(ws->linkedBlockText(u"Projekty/Remont.md"_s, u"moved1"_s), u"Przeniesiony blok z łazienką"_s);
        QVERIFY(ws->linkedBlockText(u"Nowa.md"_s, u"moved1"_s).isEmpty());
        QVERIFY(ws->linkedBlockText(u"Projekty/Remont.md"_s, u"nieznany"_s).isEmpty());
    }

    void blockIdsRightAfterOpening()
    {
        // Right after opening the index is still being built: an id in a file not read yet is
        // unknown until EditorHost::finishIndexing(), which the editor calls before it decides
        // that an id is free (a new one) or nowhere (a link to it).
        const QString root = m_tmp.filePath(u"wczesne-id"_s);
        for (int i = 0; i < 30; ++i)
            writeRaw(root + u"/Notatka %1.md"_s.arg(i), "- tekst\n");
        writeRaw(root + u"/Późna.md"_s, "- Blok z identyfikatorem ^pozny1\n");
        Workspace ws;
        QVERIFY(ws.open(root, QString()));
        QVERIFY(ws.indexing());
        QVERIFY(!ws.blockIdTaken(u"pozny1"_s)); // nothing read yet (the first chunk waits for the event loop)
        brulion::EditorHost *host = &ws;
        host->finishIndexing();
        QVERIFY(!ws.indexing());
        QVERIFY(host->blockIdTaken(u"pozny1"_s));
    }

    // A tag names a page (docs/SYNTAX.md §3): like [[tag]] written at the vault root; a missing
    // page goes where a link's would (nested tags: folders; a date: the journal page).
    void tagRoutesOpenPages()
    {
        auto ws = open({{u"Projekty/Dom.md"_s, u"---\naliases: [Remont]\n---\n- x\n"_s},
                        {u"Archiwum/Stare/Dom.md"_s, u"- stary\n"_s},
                        {u"Ludzie/Jan Kowalski.md"_s, u"- tel.\n"_s},
                        {u"Notatki.md"_s, u"- #Praca na dziś\n- #praca jutro i #dom\n"_s},
                        {u".brulion/config.json"_s, uR"({"version": 1, "newPagesFolder": "strony"})"_s}});
        QSignalSpy nav(ws.get(), &Workspace::navigationRequested);
        const auto pathOf = [&](const QString &tag) {
            nav.clear();
            ws->navigate({{u"view"_s, u"tag"_s}, {u"tag"_s, tag}});
            const QVariantMap r = nav.size() == 1 ? nav.first().at(0).toMap() : QVariantMap();
            return r.value(u"view"_s) == u"page"_s ? r.value(u"path"_s).toString() : QString();
        };
        QCOMPARE(pathOf(u"Dom"_s), u"Projekty/Dom.md"_s);     // the shortest path
        QCOMPARE(pathOf(u"#remont"_s), u"Projekty/Dom.md"_s); // an alias
        QCOMPARE(pathOf(u"archiwum/stare/dom"_s), u"Archiwum/Stare/Dom.md"_s);
        QCOMPARE(pathOf(u"Jan Kowalski"_s), u"Ludzie/Jan Kowalski.md"_s);
        // Missing pages are not created: the first spelling names the file whichever was clicked.
        QCOMPARE(pathOf(u"praca"_s), u"strony/Praca.md"_s);
        QCOMPARE(pathOf(u"Nowy/Tag/"_s), u"strony/Nowy/Tag.md"_s);
        QCOMPARE(pathOf(u"2026-10-13"_s), u"journals/2026-10-13.md"_s);
        QCOMPARE(pathOf(u"żółw"_s), u"strony/żółw.md"_s);
        QVERIFY(!ws->exists(u"strony/Praca.md"_s));
        QCOMPARE(ws->tagPage(u"#[[Jan Kowalski]]"_s), u"Ludzie/Jan Kowalski.md"_s);
        QVERIFY(ws->tagPage(u"#"_s).isEmpty());
        // Folders as the vault spells them (case-insensitive storage).
        QVERIFY(!ws->createFolder(u"strony/Projekty"_s).isEmpty());
        QCOMPARE(ws->tagPage(u"projekty/łódź"_s), u"strony/Projekty/łódź.md"_s);
    }

    void renameRewritesTags()
    {
        auto ws = open({
                {u"Projekty/Dom.md"_s, u"# Dom\n- mój #dom ^d1\n"_s},
                {u"Dziennik.md"_s, u"- #dom i #Dom, #[[dom]] (#DOM)\n- `#dom` w kodzie, #dom/kuchnia, #domek\n"_s},
                {u"Lista.md"_s, u"---\ntags: [dom, zakupy]\n---\n- a\n  tags:: dom, pilne\n"_s},
                {u"Obcy.md"_s, u"- [[Dom]] tylko link\n"_s},
        });
        QCOMPARE(ws->renamePage(u"Projekty/Dom.md"_s, u"Nowy dom"_s), u"Projekty/Nowy dom.md"_s);
        QCOMPARE(
                ws->readFile(u"Dziennik.md"_s),
                u"- #[[Nowy dom]] i #[[Nowy dom]], #[[Nowy dom]] (#[[Nowy dom]])\n- `#dom` w kodzie, #dom/kuchnia, #domek\n"_s);
        QCOMPARE(ws->readFile(u"Lista.md"_s), u"---\ntags: [Nowy dom, zakupy]\n---\n- a\n  tags:: Nowy dom, pilne\n"_s);
        QCOMPARE(ws->readFile(u"Projekty/Nowy dom.md"_s), u"# Dom\n- mój #[[Nowy dom]] ^d1\n"_s);
        QCOMPARE(ws->readFile(u"Obcy.md"_s), u"- [[Nowy dom]] tylko link\n"_s);
        QCOMPARE(ws->tagPage(u"Nowy dom"_s), u"Projekty/Nowy dom.md"_s);
        // One history step per rewritten file.
        QCOMPARE(ws->vault()->history()->list(u"Dziennik.md"_s).size(), 1);
    }

    // With a duplicate base name, #dom names the shorter path: renaming the other page keeps it.
    void renameDuplicateBaseNameKeepsOtherTags()
    {
        auto ws = open({{u"Projekty/Dom.md"_s, u"- x\n"_s},
                        {u"Archiwum/Stare/Dom.md"_s, u"- y\n"_s},
                        {u"Notatki.md"_s, u"- #dom i #Archiwum/Stare/Dom oraz #Stare/Dom\n"_s}});
        QCOMPARE(ws->renamePage(u"Archiwum/Stare/Dom.md"_s, u"Stary"_s), u"Archiwum/Stare/Stary.md"_s);
        QCOMPARE(ws->readFile(u"Notatki.md"_s), u"- #dom i #Archiwum/Stare/Stary oraz #Stare/Stary\n"_s);
    }

    // A path-suffix tag names the shortest page ending so (#Stare/Dom: Inne/Stare/Dom.md, not
    // Archiwum/Stare/Dom.md): renaming or moving the other page leaves it alone.
    void renameKeepsSuffixTagsOfOtherPages()
    {
        const QHash<QString, QString> files{{u"Archiwum/Stare/Dom.md"_s, u"- a\n"_s},
                                            {u"Inne/Stare/Dom.md"_s, u"- b\n"_s},
                                            {u"Notatki.md"_s, u"- #Stare/Dom i #Archiwum/Stare/Dom\n"_s}};
        auto ws = open(files);
        QCOMPARE(ws->tagPage(u"Stare/Dom"_s), u"Inne/Stare/Dom.md"_s);
        QCOMPARE(ws->renamePage(u"Archiwum/Stare/Dom.md"_s, u"Stary"_s), u"Archiwum/Stare/Stary.md"_s);
        QCOMPARE(ws->readFile(u"Notatki.md"_s), u"- #Stare/Dom i #Archiwum/Stare/Stary\n"_s);
        QCOMPARE(ws->tagPage(u"Stare/Dom"_s), u"Inne/Stare/Dom.md"_s);

        auto moved = open(files);
        QCOMPARE(moved->renameFolder(u"Archiwum"_s, u"Archiwa"_s), u"Archiwa"_s);
        QCOMPARE(moved->readFile(u"Notatki.md"_s), u"- #Stare/Dom i #Archiwa/Stare/Dom\n"_s);
        QCOMPARE(moved->tagPage(u"Stare/Dom"_s), u"Inne/Stare/Dom.md"_s);
    }

    // "#" completion: one entry per tag key, and diacritics count — #lodz does not name Łódź, so
    // the page is offered too; a tag in use (here one matched inside its name) stays a tag.
    void suggestTagsByTagKey()
    {
        auto ws = open({{u"Łódź.md"_s, u"- miasto\n"_s},
                        {u"projekt-dom.md"_s, u"- x\n"_s},
                        {u"Notatki.md"_s, u"- #lodz i #projekt-dom\n- #Projekt-Dom\n"_s}});
        const auto entries = [&](const QString &typed) {
            QStringList out;
            for (const QVariant &v : ws->suggestTags(typed)) {
                const QVariantMap m = v.toMap();
                out.append(m.value(u"tag"_s).toString() + u' ' + m.value(u"kind"_s).toString() + u' '
                           + m.value(u"count"_s).toString() + u' ' + m.value(u"path"_s).toString());
            }
            return out;
        };
        const QStringList lodz{u"lodz tag 1 "_s, u"Łódź page 0 Łódź.md"_s};
        QCOMPARE(entries(u"lod"_s), lodz);
        QCOMPARE(entries(u"#łód"_s), lodz);
        QCOMPARE(entries(u"dom"_s), QStringList{u"projekt-dom tag 2 projekt-dom.md"_s});
    }

    void renameFolderRewritesPathTags()
    {
        auto ws = open({{u"Projekty/Dom.md"_s, u"- x\n"_s},
                        {u"Indeks.md"_s, u"- #Projekty/Dom, #dom i #[[Projekty/Dom]]\n"_s}});
        QCOMPARE(ws->renameFolder(u"Projekty"_s, u"Prace domowe"_s), u"Prace domowe"_s);
        QCOMPARE(ws->readFile(u"Indeks.md"_s), u"- #[[Prace domowe/Dom]], #dom i #[[Prace domowe/Dom]]\n"_s);
    }

    // ---- folders and moves ---------------------------------------------------------------------

    void createFolderSanitizes_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("expected"); // "" = refused
        QTest::newRow("reserved characters") << u"a:b?"_s << u"a b"_s;
        QTest::newRow("nested, Polish") << u"Zażółć/Gęślą jaźń"_s << u"Zażółć/Gęślą jaźń"_s;
        QTest::newRow("as spelled in the vault") << u"projekty/Nowy"_s << u"Projekty/Nowy"_s;
        QTest::newRow("blanks and slashes") << u"  /Nowy//Głębiej/ "_s << u"Nowy/Głębiej"_s;
        QTest::newRow("backslashes") << u"Raz\\Dwa"_s << u"Raz/Dwa"_s;
        QTest::newRow("hidden") << u".ukryty"_s << QString();
        QTest::newRow("hidden below") << u"Projekty/.ukryty"_s << QString();
        QTest::newRow("ignored logseq") << u"logseq"_s << QString();
        QTest::newRow("ignored node_modules") << u"Kod/node_modules"_s << QString();
        // Hidden once sanitised: the check is on the name the folder would get too.
        QTest::newRow("ignored after sanitising") << u"logseq:"_s << QString();
        QTest::newRow("ignored below after sanitising") << u"Kod/node_modules?"_s << QString();
        QTest::newRow("trash") << u".trash"_s << QString();
        QTest::newRow("empty") << u" / "_s << QString();
        QTest::newRow("a file") << u"Dom.md"_s << QString();
    }
    void createFolderSanitizes()
    {
        QFETCH(QString, typed);
        QFETCH(QString, expected);
        auto ws = open({{u"Projekty/Dom.md"_s, u"x"_s}, {u"Dom.md"_s, u"x"_s}});
        QSignalSpy errors(ws.get(), &Workspace::errorOccurred);
        QSignalSpy files(ws.get(), &Workspace::filesChanged);
        QCOMPARE(ws->createFolder(typed), expected);
        if (expected.isEmpty()) {
            QCOMPARE(errors.size(), typed.trimmed() == u"/" ? 0 : 1);
            if (typed.contains(u"ukryty"_s) || typed.contains(u"logseq"_s) || typed.contains(u"node_modules"_s))
                QVERIFY(errors.first().first().toString().contains(u"hidden"_s));
            QVERIFY(!ws->exists(u"logseq"_s) && !ws->exists(u"Kod/node_modules"_s));
        } else {
            QVERIFY(errors.isEmpty());
            QVERIFY(ws->vault()->stat(expected)->isDir);
            QCOMPARE(files.size(), 1); // the tree hears of it
        }
    }

    void renameKeepsTargets_data()
    {
        QTest::addColumn<QStringList>("files"); // "path=content"
        // "page:<path>:<new name>" and "folder:<path>:<name>" (the Rename dialog: a "/" moves),
        // "title:<path>:<new title>" (the page header's title edit: a "/" is part of the title),
        // "move:<path>:<folder>".
        QTest::addColumn<QString>("action");
        QTest::addColumn<QString>("newPath");
        QTest::addColumn<QStringList>("expected"); // "path=content" after
        QTest::newRow("rename next to a root page of that name")
                << QStringList{u"Ogród.md=x"_s, u"Projekty/Dom.md=x"_s,
                               u"Notatka.md=[[Dom]] [[Dom#Kuchnia|kuchnia]] ![[Dom#^abc1]] [[Ogród]] #dom"_s}
                << u"page:Projekty/Dom.md:Ogród"_s << u"Projekty/Ogród.md"_s
                << QStringList{
                           u"Notatka.md=[[Projekty/Ogród]] [[Projekty/Ogród#Kuchnia|kuchnia]] ![[Projekty/Ogród#^abc1]] [[Ogród]] #Projekty/Ogród"_s};
        QTest::newRow("folder rename keeps a bare link")
                << QStringList{u"A/Dom.md=x"_s, u"Bardzo/Dom.md=x"_s, u"Notatka.md=- [[Dom]]\r\n- [[Bardzo/Dom]]\r\n"_s}
                << u"folder:A:Archiwum"_s << u"Archiwum"_s
                << QStringList{u"Notatka.md=- [[Archiwum/Dom]]\r\n- [[Bardzo/Dom]]\r\n"_s};
        QTest::newRow("move to a shallower folder qualifies the other page")
                << QStringList{u"Projekty/Dom.md=x"_s, u"Archiwum/Stare/Dom.md=x"_s,
                               u"Notatka.md=[[Dom]] i [x](Dom.md) i #dom"_s}
                << u"move:Archiwum/Stare/Dom.md:"_s << u"Dom.md"_s
                << QStringList{u"Notatka.md=[[Projekty/Dom]] i [x](Projekty/Dom.md) i #Projekty/Dom"_s};
        QTest::newRow("attachments in a renamed folder")
                << QStringList{u"Projekty/plan.png=png"_s,
                               u"Notatka.md=![[Projekty/plan.png]] ![](Projekty/plan.png)"_s}
                << u"folder:Projekty:Prace domowe"_s << u"Prace domowe"_s
                << QStringList{u"Notatka.md=![[Prace domowe/plan.png]] ![](Prace%20domowe/plan.png)"_s};
        QTest::newRow("renamed attachment")
                << QStringList{u"assets/plan.png=png"_s, u"Notatka.md=![[plan.png|300]] ![plan](assets/plan.png)"_s}
                << u"page:assets/plan.png:rzut"_s << u"assets/rzut.png"_s
                << QStringList{u"Notatka.md=![[rzut.png|300]] ![plan](assets/rzut.png)"_s};
        QTest::newRow("slash moves the page")
                << QStringList{u"Projekty/Dom.md=x"_s, u"Notatka.md=[[Projekty/Dom]] [[Dom]]"_s}
                << u"page:Projekty/Dom.md:Archiwum/Stary dom"_s << u"Archiwum/Stary dom.md"_s
                << QStringList{u"Notatka.md=[[Archiwum/Stary dom]] [[Stary dom]]"_s};
        QTest::newRow("slash moves the folder")
                << QStringList{u"A/B/Dom.md=x"_s, u"Notatka.md=[[A/B/Dom]]"_s} << u"folder:A/B:Archiwum/B2"_s
                << u"Archiwum/B2"_s << QStringList{u"Notatka.md=[[Archiwum/B2/Dom]]"_s};
        QTest::newRow("slash in a title stays in the folder")
                << QStringList{u"Projekty/Sieci.md=x"_s, u"Sieci.md=y"_s,
                               u"Notatka.md=[[Projekty/Sieci]] i #Projekty/Sieci"_s}
                << u"title:Projekty/Sieci.md:TCP/IP"_s << u"Projekty/TCP IP.md"_s
                << QStringList{u"Notatka.md=[[Projekty/TCP IP]] i #[[Projekty/TCP IP]]"_s};
        QTest::newRow("moved page's relative links")
                << QStringList{u"Projekty/B.md=x"_s, u"Xy/B.md=x"_s, u"Projekty/A.md=[[B]] [[#Nagłówek]]"_s}
                << u"move:Projekty/A.md:Archiwum"_s << u"Archiwum/A.md"_s
                << QStringList{u"Archiwum/A.md=[[Projekty/B]] [[#Nagłówek]]"_s};
        QTest::newRow("Polish names, CRLF and a BOM kept")
                << QStringList{u"Żółw.md=x"_s, u"Zwierzęta/Żółw.md=y"_s,
                               u"Łąka.md=\uFEFF- [[Żółw]]\r\n- [[Zwierzęta/Żółw]] #żółw\r\n"_s}
                << u"move:Zwierzęta/Żółw.md:Gady"_s << u"Gady/Żółw.md"_s
                << QStringList{u"Łąka.md=\uFEFF- [[Żółw]]\r\n- [[Gady/Żółw]] #żółw\r\n"_s};
    }
    void renameKeepsTargets()
    {
        QFETCH(QStringList, files);
        QFETCH(QString, action);
        QFETCH(QString, newPath);
        QFETCH(QStringList, expected);
        QHash<QString, QString> map;
        for (const QString &f : std::as_const(files))
            map.insert(f.section(u'=', 0, 0), f.section(u'=', 1));
        QString root;
        auto ws = open(map, &root);
        const QString kind = action.section(u':', 0, 0);
        const QString path = action.section(u':', 1, 1);
        const QString arg = action.section(u':', 2);
        const bool slashMoves = kind != u"title";
        // The impact is known before.
        const QVariantMap impact = kind == u"move" ? ws->moveImpact(path, arg)
                                                   : ws->renameImpact(path, arg, kind == u"folder", slashMoves);
        QVERIFY(impact.value(u"ok"_s).toBool());
        QCOMPARE(impact.value(u"path"_s).toString(), newPath);
        const QString result = kind == u"folder" ? ws->renameFolder(path, arg, slashMoves)
                               : kind == u"move" ? ws->movePath(path, arg)
                                                 : ws->renamePage(path, arg, slashMoves);
        QCOMPARE(result, newPath);
        QVERIFY(ws->exists(newPath));
        QVERIFY(!ws->exists(path));
        int changedNotes = 0;
        for (const QString &e : std::as_const(expected)) {
            const QString file = e.section(u'=', 0, 0);
            const QString text = e.section(u'=', 1);
            // The bytes on disk: line endings and a BOM kept.
            QFile disk(root + u'/' + file);
            QVERIFY(disk.open(QIODevice::ReadOnly));
            QCOMPARE(disk.readAll(), text.toUtf8()); // (QString::fromUtf8 would drop a BOM)
            ++changedNotes;
        }
        QCOMPARE(impact.value(u"notes"_s).toInt(), changedNotes);
    }

    void movePath_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("expected"); // "" = refused
        QTest::addColumn<bool>("error");
        QTest::newRow("page into a subfolder")
                << u"Notatka.md"_s << u"Projekty/Dom"_s << u"Projekty/Dom/Notatka.md"_s << false;
        QTest::newRow("page to the root") << u"Archiwum/Stare.md"_s << QString() << u"Stare.md"_s << false;
        QTest::newRow("folder into a folder") << u"Projekty/Dom"_s << u"Archiwum"_s << u"Archiwum/Dom"_s << false;
        QTest::newRow("empty folder") << u"Pusty"_s << u"Archiwum"_s << u"Archiwum/Pusty"_s << false;
        QTest::newRow("attachment") << u"assets/plan.png"_s << u"Projekty"_s << u"Projekty/plan.png"_s << false;
        QTest::newRow("into a new folder")
                << u"Notatka.md"_s << u"Nowy/Głęboko"_s << u"Nowy/Głęboko/Notatka.md"_s << false;
        QTest::newRow("folder spelled in another case")
                << u"Notatka.md"_s << u"archiwum"_s << u"Archiwum/Notatka.md"_s << false;
        QTest::newRow("where it is") << u"Projekty/Plan.md"_s << u"Projekty"_s << u"Projekty/Plan.md"_s << false;
        QTest::newRow("into itself") << u"Projekty"_s << u"Projekty/Dom"_s << QString() << true;
        QTest::newRow("into its own folder name") << u"Projekty"_s << u"Projekty"_s << QString() << true;
        QTest::newRow("onto an existing name") << u"Plan.md"_s << u"Projekty"_s << QString() << true;
        QTest::newRow("onto a case twin") << u"Notatka.md"_s << u"Inne"_s << QString() << true;
        QTest::newRow("folder onto a case twin") << u"Archiwum"_s << u"Inne"_s << QString() << true;
        QTest::newRow("into .git") << u"Notatka.md"_s << u".git"_s << QString() << true;
        QTest::newRow("into logseq") << u"Notatka.md"_s << u"logseq"_s << QString() << true;
        QTest::newRow("into a new hidden folder") << u"Notatka.md"_s << u".ukryty"_s << QString() << true;
        QTest::newRow("into a file") << u"Notatka.md"_s << u"Plan.md"_s << QString() << true;
        QTest::newRow("missing") << u"Brak.md"_s << QString() << QString() << false;
    }
    void movePath()
    {
        QFETCH(QString, path);
        QFETCH(QString, folder);
        QFETCH(QString, expected);
        QFETCH(bool, error);
        QString root;
        auto ws = open({{u"Notatka.md"_s, u"- [[Plan]] i [[Dom]]\n"_s},
                        {u"Plan.md"_s, u"x"_s},
                        {u"Projekty/Plan.md"_s, u"- [[Notatka]]\n"_s},
                        {u"Projekty/Dom/Kuchnia.md"_s, u"x"_s},
                        {u"Archiwum/Stare.md"_s, u"x"_s},
                        {u"Inne/notatka.md"_s, u"x"_s},
                        {u"Inne/archiwum/x.md"_s, u"x"_s},
                        {u"assets/plan.png"_s, u"png"_s},
                        {u".git/config"_s, u"x"_s},
                        {u"logseq/config.edn"_s, u"x"_s}},
                       &root);
        QDir(root).mkpath(u"Pusty"_s);
        ws->vault()->rescan();
        QSignalSpy errors(ws.get(), &Workspace::errorOccurred);
        const QVariantMap impact = ws->moveImpact(path, folder);
        QCOMPARE(ws->movePath(path, folder), expected);
        QCOMPARE(errors.size(), error ? 1 : 0);
        QCOMPARE(impact.value(u"ok"_s).toBool(), !expected.isEmpty());
        if (error)
            QCOMPARE(impact.value(u"error"_s).toString(), errors.first().first().toString());
        if (!expected.isEmpty())
            QVERIFY(ws->exists(expected));
        // Whatever happened, the links still reach their pages.
        if (ws->exists(u"Notatka.md"_s))
            QCOMPARE(ws->resolveLink(u"Plan"_s, u"Notatka.md"_s), u"Plan.md"_s);
    }

    // requestMoveImpact(): moveImpact() delivered by a signal, never from inside the request; a
    // new request drops the one in progress; the impact is kept per target until the notebook
    // changes.
    void requestMoveImpact()
    {
        auto ws = open({{u"Notatka.md"_s, u"- [[Projekty/Dom]] i #[[Projekty/Dom]] [[Plan]]\n"_s},
                        {u"Zapytania.md"_s, u"```query\npath:Projekty/ ref:[[Projekty/Dom]]\n```\n"_s},
                        {u"Projekty/Dom.md"_s, u"- [[Plan]]\n"_s},
                        {u"Projekty/Łąka.md"_s, u"x"_s},
                        {u"Plan.md"_s, u"x"_s},
                        {u"Archiwum/Stare.md"_s, u"x"_s}});
        QSignalSpy ready(ws.get(), &Workspace::moveImpactReady);
        const QVariantMap expected = ws->moveImpact(u"Projekty"_s, u"Archiwum"_s);
        QVERIFY(expected.value(u"ok"_s).toBool());
        QCOMPARE(expected.value(u"notes"_s).toInt(), 1); // path:Projekty/ (the links still resolve)
        ws->requestMoveImpact(u"Projekty"_s, u"Archiwum"_s);
        QCOMPARE(ready.size(), 0); // not from inside the request
        QVERIFY(ready.wait());
        QCOMPARE(ready.size(), 1);
        QCOMPARE(ready.first().at(0).toString(), u"Projekty"_s);
        QCOMPARE(ready.first().at(1).toString(), u"Archiwum"_s);
        QCOMPARE(ready.first().at(2).toMap(), expected);

        // A new target drops the request in progress; a cancelled one says nothing.
        ready.clear();
        ws->requestMoveImpact(u"Projekty"_s, QString());
        ws->requestMoveImpact(u"Projekty/Łąka.md"_s, u"Archiwum"_s);
        QVERIFY(ready.wait());
        QTest::qWait(50);
        QCOMPARE(ready.size(), 1);
        QCOMPARE(ready.first().at(0).toString(), u"Projekty/Łąka.md"_s);
        QCOMPARE(ready.first().at(2).toMap().value(u"path"_s).toString(), u"Archiwum/Łąka.md"_s);
        ready.clear();
        ws->requestMoveImpact(u"Projekty"_s, QString());
        ws->cancelMoveImpact();
        QTest::qWait(50);
        QCOMPARE(ready.size(), 0);

        // A refusal comes the same way.
        ws->requestMoveImpact(u"Projekty"_s, u"Projekty/Dom"_s);
        QVERIFY(ready.wait());
        QVERIFY(!ready.first().at(2).toMap().value(u"ok"_s).toBool());
        QCOMPARE(ready.first().at(2).toMap().value(u"error"_s).toString(), u"A folder cannot be moved into itself."_s);

        // New settings: the special folders that follow are the new ones.
        QVERIFY(ws->moveImpact(u"Projekty"_s, u"Archiwum"_s).value(u"specials"_s).toStringList().isEmpty());
        QVERIFY(ws->setConfig({{u"newPagesFolder"_s, u"Projekty"_s}}));
        QCOMPARE(ws->moveImpact(u"Projekty"_s, u"Archiwum"_s).value(u"specials"_s).toStringList(),
                 (QStringList{u"newPages"_s}));

        // A note saved since: planned anew (one more query names the folder's page).
        QVERIFY(ws->write(u"Nowa.md"_s, u"```query\npath:Projekty/Łąka\n```\n"_s));
        ready.clear();
        ws->requestMoveImpact(u"Projekty"_s, u"Archiwum"_s);
        QVERIFY(ready.wait());
        QCOMPARE(ready.first().at(2).toMap().value(u"notes"_s).toInt(), 2);
        QCOMPARE(ws->moveImpact(u"Projekty"_s, u"Archiwum"_s).value(u"notes"_s).toInt(), 2);
        // The move itself rewrites what was planned.
        QCOMPARE(ws->movePath(u"Projekty"_s, u"Archiwum"_s), u"Archiwum/Projekty"_s);
        QCOMPARE(ws->read(u"Nowa.md"_s), u"```query\npath:Archiwum/Projekty/Łąka\n```\n"_s);
    }

    // Favourites, recent pages, fold state, history and the expanded folders follow a folder
    // move; the tree, the trash count and the page count hear of folder changes.
    void moveKeepsLocalState()
    {
        auto ws = open({{u"Projekty/Dom/Kuchnia.md"_s, u"- a\n  - b\n"_s}, {u"Projekty/Plan.md"_s, u"x"_s}});
        ws->setFavorite(u"Projekty/Dom/Kuchnia.md"_s, true);
        ws->notePageOpened(u"Projekty/Plan.md"_s);
        ws->setFoldState(u"Projekty/Dom/Kuchnia.md"_s, {0});
        QVERIFY(ws->write(u"Projekty/Dom/Kuchnia.md"_s, u"- a\n  - b\n- c\n"_s));
        FolderTreeModel tree;
        tree.setWorkspace(ws.get());
        tree.setExpanded(u"Projekty"_s, true);
        tree.setExpanded(u"Projekty/Dom"_s, true);
        QSignalSpy renamed(ws.get(), &Workspace::folderRenamed);
        QCOMPARE(ws->movePath(u"Projekty"_s, u"Archiwum"_s), u"Archiwum/Projekty"_s);
        QCOMPARE(renamed.size(), 1);
        QCOMPARE(strings(renamed.first()), (QStringList{u"Projekty"_s, u"Archiwum/Projekty"_s}));
        QVERIFY(ws->isFavorite(u"Archiwum/Projekty/Dom/Kuchnia.md"_s));
        QVERIFY(!ws->isFavorite(u"Projekty/Dom/Kuchnia.md"_s));
        QCOMPARE(ws->recentPages().first().toMap().value(u"path"_s).toString(), u"Archiwum/Projekty/Plan.md"_s);
        QCOMPARE(ws->foldState(u"Archiwum/Projekty/Dom/Kuchnia.md"_s), (QVariantList{0}));
        QCOMPARE(ws->vault()->history()->list(u"Archiwum/Projekty/Dom/Kuchnia.md"_s).size(), 1);
        // The tree keeps the moved folders open.
        QVERIFY(tree.isExpanded(u"Archiwum/Projekty"_s));
        QVERIFY(tree.isExpanded(u"Archiwum/Projekty/Dom"_s));
        QVERIFY(!tree.isExpanded(u"Projekty"_s));
    }

    void folderChangesRefresh()
    {
        QString root;
        auto ws = open({{u"a.md"_s, u"x"_s}}, &root);
        QVERIFY(!ws->createFolder(u"Pusty/Głębszy"_s).isEmpty());
        QSignalSpy files(ws.get(), &Workspace::filesChanged);
        QSignalSpy trash(ws.get(), &Workspace::trashChanged);
        QSignalSpy revision(ws.get(), &Workspace::revisionChanged);
        // An empty folder deleted: no file changes, but the lists hear of it.
        const QString trashed = ws->deletePage(u"Pusty/Głębszy"_s);
        QVERIFY(!trashed.isEmpty());
        QCOMPARE(files.size(), 1);
        QVERIFY(trash.size() >= 1);
        QVERIFY(revision.size() >= 1);
        // ... restored, renamed, moved.
        files.clear();
        QCOMPARE(ws->restore(trashed), u"Pusty/Głębszy"_s);
        QCOMPARE(files.size(), 1);
        files.clear();
        QCOMPARE(ws->renameFolder(u"Pusty"_s, u"Pełny?"_s), u"Pełny"_s);
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().first().toStringList(), (QStringList{u"Pusty"_s, u"Pełny"_s}));
        QVERIFY(ws->exists(u"Pełny/Głębszy"_s));
        // A hidden name is refused (in the Rename dialog a "/" moves it).
        QSignalSpy errors(ws.get(), &Workspace::errorOccurred);
        QVERIFY(ws->renameFolder(u"Pełny"_s, u".ukryty"_s).isEmpty());
        QVERIFY(ws->renameFolder(u"Pełny"_s, u"Ok/node_modules"_s, true).isEmpty());
        QCOMPARE(errors.size(), 2);
        // Without it a "/" is part of the name.
        QCOMPARE(ws->renameFolder(u"Pełny"_s, u"Ok/node_modules"_s), u"Ok node_modules"_s);
        QCOMPARE(ws->renameImpact(u"Ok node_modules"_s, u"Raz/Dwa"_s, true).value(u"path"_s).toString(), u"Raz Dwa"_s);
        QCOMPARE(ws->renameImpact(u"Ok node_modules"_s, u"Raz/Dwa"_s, true, true).value(u"path"_s).toString(),
                 u"Raz/Dwa"_s);
        QCOMPARE(errors.size(), 2);
    }

    // Renaming or moving a special folder (or one holding it) updates .brulion/config.json in the
    // same operation, and says so.
    void renameSpecialFoldersUpdatesConfig()
    {
        auto ws = open({{u"journals/2026-10-13.md"_s, u"- wczoraj [[Dom]]\n"_s},
                        {u"templates/journal.md"_s, u"- szablon {{date}}\n"_s},
                        {u"assets/plan.png"_s, u"png"_s},
                        {u"Dom.md"_s, u"![[plan.png]]\n"_s}});
        QSignalSpy notices(ws.get(), &Workspace::noticeOccurred);
        QCOMPARE(specials(ws->folderInfo(u"journals"_s)), (QStringList{u"journals:journals"_s}));
        QCOMPARE(ws->renameImpact(u"journals"_s, u"Dziennik"_s, true).value(u"specials"_s).toStringList(),
                 (QStringList{u"journals"_s}));
        QCOMPARE(ws->renameFolder(u"journals"_s, u"Dziennik"_s), u"Dziennik"_s);
        QCOMPARE(ws->vaultConfig().value(u"journalsFolder"_s).toString(), u"Dziennik"_s);
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.first().first().toString().contains(u"Dziennik"_s));
        QCOMPARE(ws->journalDate(u"Dziennik/2026-10-13.md"_s), u"2026-10-13"_s);
        QCOMPARE(ws->ensureJournal(u"2026-10-14"_s), u"Dziennik/2026-10-14.md"_s);
        QVERIFY(!ws->exists(u"journals"_s));
        // Templates and the journal template; then the attachments folder moved into another.
        QCOMPARE(ws->renameFolder(u"templates"_s, u"Szablony"_s), u"Szablony"_s);
        QCOMPARE(ws->vaultConfig().value(u"templatesFolder"_s).toString(), u"Szablony"_s);
        QCOMPARE(ws->vaultConfig().value(u"journalTemplate"_s).toString(), u"Szablony/journal.md"_s);
        QVERIFY(ws->readFile(u"Dziennik/2026-10-15.md"_s).isEmpty());
        QVERIFY(ws->readFile(ws->ensureJournal(u"2026-10-15"_s)).startsWith(u"- szablon"_s));
        QCOMPARE(ws->movePath(u"assets"_s, u"Pliki"_s), u"Pliki/assets"_s);
        QCOMPARE(ws->vaultConfig().value(u"attachmentsFolder"_s).toString(), u"Pliki/assets"_s);
        QCOMPARE(ws->resolveLink(u"plan.png"_s, u"Dom.md"_s), u"Pliki/assets/plan.png"_s);
        QCOMPARE(specials(ws->folderInfo(u"Pliki"_s)), (QStringList{u"attachments:Pliki/assets"_s}));
        // The template moved on its own: the settings say so before.
        QCOMPARE(ws->moveImpact(u"Szablony/journal.md"_s, QString()).value(u"specials"_s).toStringList(),
                 (QStringList{u"journalTemplate"_s}));
        QCOMPARE(specials(ws->folderInfo(u"Szablony/journal.md"_s)),
                 (QStringList{u"journalTemplate:Szablony/journal.md"_s}));
        QCOMPARE(ws->movePath(u"Szablony/journal.md"_s, QString()), u"journal.md"_s);
        QCOMPARE(ws->vaultConfig().value(u"journalTemplate"_s).toString(), u"journal.md"_s);
        // The new pages' folder, moved with its parent.
        QVERIFY(ws->setConfig({{u"newPagesFolder"_s, u"Strony/Nowe"_s}}));
        QVERIFY(!ws->createFolder(u"Strony/Nowe"_s).isEmpty());
        QCOMPARE(ws->renameFolder(u"Strony"_s, u"Notatki"_s), u"Notatki"_s);
        QCOMPARE(ws->vaultConfig().value(u"newPagesFolder"_s).toString(), u"Notatki/Nowe"_s);
        QCOMPARE(ws->createPage(u"Gdzie"_s), u"Notatki/Nowe/Gdzie.md"_s);
        // A folder holding several: all of them are named, and all follow.
        QCOMPARE(ws->movePath(u"Dziennik"_s, u"Notatki"_s), u"Notatki/Dziennik"_s);
        QCOMPARE(specials(ws->folderInfo(u"Notatki"_s)),
                 (QStringList{u"journals:Notatki/Dziennik"_s, u"newPages:Notatki/Nowe"_s}));
        QCOMPARE(ws->moveImpact(u"Notatki"_s, u"Archiwum"_s).value(u"specials"_s).toStringList(),
                 (QStringList{u"journals"_s, u"newPages"_s}));
        notices.clear();
        QCOMPARE(ws->movePath(u"Notatki"_s, u"Archiwum"_s), u"Archiwum/Notatki"_s);
        QCOMPARE(ws->vaultConfig().value(u"journalsFolder"_s).toString(), u"Archiwum/Notatki/Dziennik"_s);
        QCOMPARE(ws->vaultConfig().value(u"newPagesFolder"_s).toString(), u"Archiwum/Notatki/Nowe"_s);
        QCOMPARE(notices.size(), 1);
        // Other folders leave the settings alone.
        notices.clear();
        QVERIFY(!ws->createFolder(u"Inny"_s).isEmpty());
        QCOMPARE(ws->renameFolder(u"Inny"_s, u"Inny 2"_s), u"Inny 2"_s);
        QVERIFY(notices.isEmpty());
    }

    void folderQueries()
    {
        auto ws = open({{u"Projekty/Dom.md"_s, u"x"_s},
                        {u"Projekty/Dom/Kuchnia.md"_s, u"x"_s},
                        {u"Projekty/plan.png"_s, u"png"_s},
                        {u"Projekty/Notatka.sync-conflict-20261014-101010-ABCDEFG.md"_s, u"x"_s},
                        {u"Żagle/a.md"_s, u"x"_s},
                        {u"Zakupy/b.md"_s, u"x"_s},
                        {u"Łódź/c.md"_s, u"x"_s},
                        {u"Rozdział 10/d.md"_s, u"x"_s},
                        {u"Rozdział 2/e.md"_s, u"x"_s},
                        {u"assets/x.png"_s, u"png"_s},
                        {u"journals/2026-10-14.md"_s, u"x"_s},
                        {u"strona.md"_s, u"x"_s}});
        QCOMPARE(ws->folderPaths(),
                 (QStringList{u"assets"_s, u"journals"_s, u"Łódź"_s, u"Projekty"_s, u"Projekty/Dom"_s, u"Rozdział 2"_s,
                              u"Rozdział 10"_s, u"Zakupy"_s, u"Żagle"_s}));
        const QVariantMap info = ws->folderInfo(u"Projekty"_s);
        QCOMPARE(info.value(u"exists"_s).toBool(), true);
        QCOMPARE(info.value(u"pages"_s).toInt(), 2);
        QCOMPARE(info.value(u"attachments"_s).toInt(), 1);
        QCOMPARE(info.value(u"folders"_s).toInt(), 1);
        QCOMPARE(specials(info), QStringList());
        QCOMPARE(ws->folderInfo(u"Brak"_s).value(u"exists"_s).toBool(), false);
        QStringList names;
        for (const QVariant &v : ws->subfolders(QString()))
            names.append(v.toMap().value(u"name"_s).toString() + u' ' + v.toMap().value(u"pages"_s).toString());
        // Attachment-only folders left out; journals counted without their days.
        QCOMPARE(names, (QStringList{u"journals 0"_s, u"Łódź 1"_s, u"Projekty 2"_s, u"Rozdział 2 1"_s,
                                     u"Rozdział 10 1"_s, u"Zakupy 1"_s, u"Żagle 1"_s}));
        QCOMPARE(ws->subfolders(u"Projekty"_s).size(), 1);
        QCOMPARE(ws->pageCount(), 10);
        QCOMPARE(ws->pagesWithoutJournals(), 8);
        // The shortest [[target]] for completion and "Kopiuj link".
        QCOMPARE(ws->linkTarget(u"Projekty/Dom/Kuchnia.md"_s), u"Kuchnia"_s);
        QCOMPARE(ws->linkTarget(u"Brak.md"_s), QString());
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
        QVERIFY(!ws->createFolder(u"Pusty"_s).isEmpty());
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

    // The tree follows the vault with inserted and removed rows (never a reset while rows stay:
    // the sidebar keeps its scroll position), in every shape: changes in collapsed and expanded,
    // nested folders, renames, moves, deletions. A shadow list kept only from the signals must
    // equal the model afterwards; QAbstractItemModelTester checks the signals' consistency.
    void folderTreeDiffs()
    {
        auto ws = open({{u"A/a1.md"_s, u"x"_s},
                        {u"A/B/b1.md"_s, u"x"_s},
                        {u"A/B/C/c1.md"_s, u"x"_s},
                        {u"D/d1.md"_s, u"x"_s},
                        {u"z.md"_s, u"x"_s}});
        FolderTreeModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setWorkspace(ws.get());
        const auto pathAt = [&](int row) { return model.data(model.index(row), FolderTreeModel::PathRole).toString(); };
        const auto paths = [&] {
            QStringList out;
            for (int i = 0; i < model.rowCount(); ++i)
                out.append(pathAt(i));
            return out;
        };
        QStringList shadow = paths();
        int resets = 0;
        connect(&model, &QAbstractItemModel::rowsInserted, this, [&](const QModelIndex &, int first, int last) {
            for (int i = first; i <= last; ++i)
                shadow.insert(i, pathAt(i));
        });
        connect(&model, &QAbstractItemModel::rowsRemoved, this,
                [&](const QModelIndex &, int first, int last) { shadow.remove(first, last - first + 1); });
        connect(&model, &QAbstractItemModel::modelReset, this, [&] {
            ++resets;
            shadow = paths();
        });
        model.setExpanded(u"A"_s, true);
        model.setExpanded(u"A/B"_s, true);
        model.setExpanded(u"A/B/C"_s, true);
        QCOMPARE(paths(), (QStringList{u"A"_s, u"A/B"_s, u"A/B/C"_s, u"A/B/C/c1.md"_s, u"A/B/b1.md"_s, u"A/a1.md"_s,
                                       u"D"_s, u"z.md"_s}));
        const auto settle = [&] {
            model.refresh();
            QCOMPARE(shadow, paths());
        };
        // A page in a deeply expanded folder, one in a collapsed folder, one at the root.
        ws->createPage(u"A/B/C/c0"_s);
        ws->createPage(u"D/d0"_s);
        ws->createPage(u"m"_s);
        settle();
        QVERIFY(paths().contains(u"A/B/C/c0.md"_s) && paths().contains(u"m.md"_s) && !paths().contains(u"D/d0.md"_s));
        // A nested folder renamed: its rows go and come back under the new name, still open.
        QCOMPARE(ws->renameFolder(u"A/B"_s, u"Bę"_s), u"A/Bę"_s);
        settle();
        QVERIFY(model.isExpanded(u"A/Bę/C"_s));
        QVERIFY(paths().contains(u"A/Bę/C/c1.md"_s));
        // Moved to another folder (collapsed there: its rows leave the list).
        QCOMPARE(ws->movePath(u"A/Bę"_s, u"D"_s), u"D/Bę"_s);
        settle();
        QVERIFY(!paths().contains(u"A/Bę"_s));
        model.setExpanded(u"D"_s, true);
        QCOMPARE(shadow, paths());
        QVERIFY(paths().contains(u"D/Bę/C/c1.md"_s));
        // Deleted: gone, and its expanded state forgotten.
        QVERIFY(!ws->deletePage(u"D/Bę"_s).isEmpty());
        settle();
        QVERIFY(!model.isExpanded(u"D/Bę"_s) && !model.isExpanded(u"D/Bę/C"_s));
        // Pages renamed into another place in the order, a folder created at the top.
        QCOMPARE(ws->renamePage(u"z.md"_s, u"Ą"_s), u"Ą.md"_s);
        QVERIFY(!ws->createFolder(u"0 Pierwszy"_s).isEmpty());
        settle();
        QCOMPARE(paths().first(), u"0 Pierwszy"_s);
        QCOMPARE(resets, 0);
        // Everything gone: a reset, and back.
        for (const QString &p : paths()) {
            if (!p.contains(u'/'))
                ws->deletePage(p);
        }
        settle();
        QCOMPARE(model.rowCount(), 0);
        ws->createPage(u"nowa"_s);
        settle();
        QCOMPARE(paths(), QStringList{u"nowa.md"_s});
    }

    // Polish order with numbers by value; the journals folder first, its days newest first with
    // their dates; a changed journals folder in the settings moves the pin.
    void folderTreeOrderAndJournals()
    {
        auto ws = open({{u"Żagle.md"_s, u"x"_s},
                        {u"Zakupy.md"_s, u"x"_s},
                        {u"Łódź.md"_s, u"x"_s},
                        {u"Ćwiczenia.md"_s, u"x"_s},
                        {u"Rozdział 10.md"_s, u"x"_s},
                        {u"Rozdział 2.md"_s, u"x"_s},
                        {u"Archiwum/a.md"_s, u"x"_s},
                        {u"Notatki/n.md"_s, u"x"_s},
                        {u"journals/2026-10-12.md"_s, u"x"_s},
                        {u"journals/2026-10-14.md"_s, u"x"_s},
                        {u"journals/2025-01-01.md"_s, u"x"_s},
                        {u"journals/Notatka.md"_s, u"x"_s}});
        FolderTreeModel model;
        model.setWorkspace(ws.get());
        const auto paths = [&] {
            QStringList out;
            for (int i = 0; i < model.rowCount(); ++i)
                out.append(model.data(model.index(i), FolderTreeModel::PathRole).toString());
            return out;
        };
        QCOMPARE(paths(), (QStringList{u"journals"_s, u"Archiwum"_s, u"Notatki"_s, u"Ćwiczenia.md"_s, u"Łódź.md"_s,
                                       u"Rozdział 2.md"_s, u"Rozdział 10.md"_s, u"Zakupy.md"_s, u"Żagle.md"_s}));
        QCOMPARE(model.data(model.index(0), FolderTreeModel::SpecialRole).toString(), u"journals"_s);
        QCOMPARE(model.data(model.index(0), FolderTreeModel::IconRole).toString(), u"calendar-days"_s);
        QVERIFY(!model.isExpanded(u"journals"_s)); // collapsed by default
        model.toggle(0);
        QCOMPARE(paths().mid(1, 4), (QStringList{u"journals/2026-10-14.md"_s, u"journals/2026-10-12.md"_s,
                                                 u"journals/2025-01-01.md"_s, u"journals/Notatka.md"_s}));
        QCOMPARE(model.data(model.index(1), FolderTreeModel::JournalDateRole).toString(), u"2026-10-14"_s);
        QCOMPARE(model.data(model.index(1), FolderTreeModel::IconRole).toString(), u"calendar"_s);
        QCOMPARE(model.data(model.index(4), FolderTreeModel::JournalDateRole).toString(), QString());
        // Another journals folder in the settings: the pin moves.
        QVERIFY(ws->setConfig({{u"journalsFolder"_s, u"Notatki"_s}}));
        model.refresh();
        QCOMPARE(paths().first(), u"Notatki"_s);
        QCOMPARE(model.data(model.index(0), FolderTreeModel::SpecialRole).toString(), u"journals"_s);
        QCOMPARE(ws->pagesWithoutJournals(), 12);
    }

    // calendarItems() and runQuery() keep caches of the last ranges and queries: after writes they
    // give what the stateless functions give; the oldest are dropped; close() forgets them.
    void cachedListsFollowWrites()
    {
        auto ws = open({{u"journals/2026-10-14.md"_s, u"- [ ] Odebrać paczkę 📅 2026-10-14 ⏰ 2026-10-14 17:30\n"_s},
                        {u"Dom.md"_s, u"- [ ] Malowanie #remont 📅 2026-10-16\n- Wizyta ⏰ 2026-10-15 09:00\n"_s}});
        brulion::VaultIndex *index = ws->index();
        const auto stateless = [&](const QString &from, const QString &to) {
            QVariantList out;
            for (const brulion::CalendarItem &item : brulion::calendarItems(
                         *index, QDate::fromString(from, Qt::ISODate), QDate::fromString(to, Qt::ISODate)))
                out.append(brulion::notebook::calendarItemJson(item).toVariantMap());
            return out;
        };
        const auto query = [&](const QString &source, const QString &page = QString()) {
            const auto parsed = brulion::parseQuery(source);
            brulion::QueryOptions options;
            options.currentPath = page;
            options.now = QTime(10, 30);
            const brulion::QueryResult r = brulion::runQuery(std::get<brulion::Query>(parsed), *index,
                                                             QDate(2026, 10, 14), Qt::Monday, options);
            QStringList out{QString::number(r.total)};
            for (const brulion::QueryItem &item : r.items)
                out << brulion::notebook::queryItemJson(*index, item).value(u"path"_s).toString()
                                + brulion::notebook::queryItemJson(*index, item).value(u"text"_s).toString();
            return out;
        };
        const auto shown = [&](const QVariantMap &result) {
            QStringList out{QString::number(result.value(u"total"_s).toInt())};
            for (const QVariant &v : result.value(u"items"_s).toList())
                out << v.toMap().value(u"path"_s).toString() + v.toMap().value(u"text"_s).toString();
            return out;
        };
        QCOMPARE(ws->calendarItems(u"2026-10-12"_s, u"2026-10-18"_s), stateless(u"2026-10-12"_s, u"2026-10-18"_s));
        QCOMPARE(shown(ws->runQuery(u"tag:remont"_s)), query(u"tag:remont"_s));
        QCOMPARE(shown(ws->runQuery(u"page:this"_s, u"Dom.md"_s)), query(u"page:this"_s, u"Dom.md"_s));
        QVERIFY(ws->write(u"Dom.md"_s, u"- [ ] Malowanie salonu #remont 📅 2026-10-17\n- [ ] Żółw #remont\n"_s));
        QVERIFY(ws->write(u"Nowa.md"_s, u"- [ ] W nowej #remont ⏰ 2026-10-13 08:00\n"_s));
        QCOMPARE(ws->calendarItems(u"2026-10-12"_s, u"2026-10-18"_s), stateless(u"2026-10-12"_s, u"2026-10-18"_s));
        QCOMPARE(shown(ws->runQuery(u"tag:remont"_s)), query(u"tag:remont"_s));
        QCOMPARE(shown(ws->runQuery(u"page:this"_s, u"Dom.md"_s)), query(u"page:this"_s, u"Dom.md"_s));
        QCOMPARE(ws->runQuery(u"tag:remont"_s).value(u"total"_s).toInt(), 3);
        QCOMPARE(ws->cachedLists(), (QStringList{u"2026-10-12|2026-10-18"_s, u"tag:remont\x1f"_s,
                                                 u"page:this\x1f"
                                                 u"Dom.md"_s}));

        // The most recently asked are kept: four ranges, eight queries.
        for (int d = 1; d <= Workspace::MaxCachedRanges + 1; ++d) {
            const QString day = u"2026-10-%1"_s.arg(d, 2, 10, QChar(u'0'));
            QCOMPARE(ws->calendarItems(day, day), stateless(day, day));
        }
        for (int q = 0; q < Workspace::MaxCachedQueries + 2; ++q)
            QCOMPARE(shown(ws->runQuery(u"tag:remont limit:%1"_s.arg(q + 1))),
                     query(u"tag:remont limit:%1"_s.arg(q + 1)));
        const QStringList kept = ws->cachedLists();
        QCOMPARE(kept.size(), Workspace::MaxCachedRanges + Workspace::MaxCachedQueries);
        QCOMPARE(kept.first(), u"2026-10-05|2026-10-05"_s);
        QVERIFY(!kept.contains(u"2026-10-12|2026-10-18"_s));
        QVERIFY(!kept.contains(u"tag:remont limit:1\x1f"_s));
        QCOMPARE(kept.at(Workspace::MaxCachedRanges), u"tag:remont limit:10\x1f"_s);
        // Errors are not cached; close() drops the caches.
        QVERIFY(!ws->runQuery(u"limit:dużo"_s).value(u"ok"_s).toBool());
        QCOMPARE(ws->cachedLists(), kept);
        ws->close();
        QVERIFY(ws->cachedLists().isEmpty());
    }

    // A query matching more than MaxCachedQueryMatches is not kept (its cache would hold every
    // match for the session, whatever its limit:): computed afresh, the same result; smaller
    // queries are kept.
    void broadQueriesAreNotKept()
    {
        QString many;
        for (int i = 0; i <= Workspace::MaxCachedQueryMatches; ++i)
            many += u"- wiersz %1 o żółtej łodzi\n"_s.arg(i);
        auto ws = open({{u"Łódź.md"_s, many}, {u"Dom.md"_s, u"- [ ] Malowanie #remont\n- inna łódź\n"_s}});
        const QVariantMap broad = ws->runQuery(u"\"łodzi\" limit:3"_s);
        QVERIFY(broad.value(u"ok"_s).toBool());
        QCOMPARE(broad.value(u"total"_s).toInt(), Workspace::MaxCachedQueryMatches + 1);
        QCOMPARE(broad.value(u"items"_s).toList().size(), 3);
        QVERIFY(ws->cachedLists().isEmpty());
        const QVariantMap narrow = ws->runQuery(u"tag:remont"_s);
        QCOMPARE(narrow.value(u"total"_s).toInt(), 1);
        QCOMPARE(ws->cachedLists(), QStringList{u"tag:remont\x1f"_s});
        // Again after a write: computed afresh, still not kept.
        QVERIFY(ws->write(u"Dom.md"_s, u"- [ ] Malowanie #remont\n- żółta łodzi przystań\n"_s));
        const QVariantMap again = ws->runQuery(u"\"łodzi\" limit:3"_s);
        QCOMPARE(again.value(u"total"_s).toInt(), Workspace::MaxCachedQueryMatches + 2);
        QCOMPARE(again.value(u"items"_s).toList().size(), 3);
        QCOMPARE(ws->cachedLists(), QStringList{u"tag:remont\x1f"_s});
    }
};

QTEST_GUILESS_MAIN(TestWorkspace)
#include "tst_workspace.moc"
