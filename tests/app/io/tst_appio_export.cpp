// ExportController and selectExportFiles(): what each scope exports (the whole notebook with its
// config, a folder with the pictures its pages link to from elsewhere, one page with its
// attachments), the summary and its errors, producing a Markdown ZIP / tasks CSV / single HTML in
// the background, saving to a picked file and to the export folder, and resetting on changes.
#include "exportcontroller.h"
#include "workspace.h"

#include "io/zip.h"
#include "vault/vault.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::FileStat;
using brulion::ioviews::ExportController;

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

FileStat file(const QString &path, qint64 size = 10)
{
    FileStat f;
    f.path = path;
    f.size = size;
    return f;
}

FileStat dir(const QString &path)
{
    FileStat f;
    f.path = path;
    f.isDir = true;
    return f;
}

QStringList zipPaths(const QByteArray &zip)
{
    QStringList out;
    for (const brulion::io::FileEntry &e : brulion::io::readZip(zip))
        out.append(e.path);
    out.sort();
    return out;
}

} // namespace

class TestAppIoExport : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<brulion::shell::Workspace> open(const QHash<QString, QByteArray> &files, QString *rootOut = nullptr)
    {
        const QString root = m_tmp.filePath(u"Mój brulion %1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeRaw(root + u'/' + it.key(), it.value());
        auto ws = std::make_unique<brulion::shell::Workspace>();
        ws->setClock([] { return QDateTime(QDate(2026, 10, 15), QTime(9, 30)); });
        ws->sync()->setSyncthingDiscovery(false);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        if (rootOut)
            *rootOut = root;
        return ws;
    }

    static QHash<QString, QByteArray> sampleVault()
    {
        return {
            { u"journals/2026-10-14.md"_s, "- [ ] Zadzwonić do Łucji 📅 2026-10-16\n- spotkanie ⏰ 2026-10-15 18:00\n" },
            { u"Projekty/Remont.md"_s, "# Remont\n- [ ] kupić farbę\n- ![[assets/kuchnia.jpg]]\n- [[Zakupy]]\n" },
            { u"Projekty/Ogród/Grządki.md"_s, "- marchew, pietruszka\n" },
            { u"Zakupy.md"_s, "- mleko\n- ![[paragon.png]]\n" },
            { u"assets/kuchnia.jpg"_s, "JPEG" },
            { u"assets/paragon.png"_s, "PNG" },
            { u"assets/niepotrzebne.png"_s, "PNG2" },
            { u".brulion/config.json"_s, "{\"journalsFolder\": \"journals\"}\n" },
        };
    }

    static bool waitState(ExportController &c, const QString &state, int ms = 10000)
    {
        QElapsedTimer t;
        t.start();
        while (c.state() != state && t.elapsed() < ms)
            QTest::qWait(10);
        if (c.state() != state)
            qWarning() << "state" << c.state() << "error" << c.errorText();
        return c.state() == state;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QStandardPaths::setTestModeEnabled(true);
    }

    void selection_data()
    {
        QTest::addColumn<QString>("scope");
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("page");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("vault") << u"vault"_s << QString() << QString()
                               << QStringList{ u"Projekty/Ogród/Grządki.md"_s, u"Projekty/Remont.md"_s,
                                               u"assets/kuchnia.jpg"_s, u"assets/niepotrzebne.png"_s,
                                               u"assets/paragon.png"_s, u"Zakupy.md"_s };
        QTest::newRow("folder with linked picture")
                << u"folder"_s << u"Projekty"_s << QString()
                << QStringList{ u"Projekty/Ogród/Grządki.md"_s, u"Projekty/Remont.md"_s, u"assets/kuchnia.jpg"_s };
        QTest::newRow("subfolder") << u"folder"_s << u"Projekty/Ogród/"_s << QString()
                                   << QStringList{ u"Projekty/Ogród/Grządki.md"_s };
        QTest::newRow("page with picture") << u"page"_s << QString() << u"Zakupy.md"_s
                                           << QStringList{ u"assets/paragon.png"_s, u"Zakupy.md"_s };
        QTest::newRow("missing page") << u"page"_s << QString() << u"Nie ma.md"_s << QStringList{};
        QTest::newRow("folder prefix is not a folder")
                << u"folder"_s << u"Proj"_s << QString() << QStringList{};
    }

    void selection()
    {
        QFETCH(QString, scope);
        QFETCH(QString, folder);
        QFETCH(QString, page);
        QFETCH(QStringList, expected);
        const QList<FileStat> files{ dir(u"Projekty"_s), file(u"Projekty/Remont.md"_s, 100),
                                     dir(u"Projekty/Ogród"_s), file(u"Projekty/Ogród/Grządki.md"_s, 20),
                                     file(u"Zakupy.md"_s, 30), file(u"assets/kuchnia.jpg"_s, 1000),
                                     file(u"assets/paragon.png"_s, 500), file(u"assets/niepotrzebne.png"_s, 7) };
        const auto links = [](const QString &p) -> QList<QString> {
            if (p == u"Projekty/Remont.md")
                return { u"assets/kuchnia.jpg"_s, u"Zakupy.md"_s, QString() };
            if (p == u"Zakupy.md")
                return { u"assets/paragon.png"_s, u"assets/nie-ma.png"_s };
            return {};
        };
        const brulion::ioviews::ExportSelection s = brulion::ioviews::selectExportFiles(files, scope, folder, page, links);
        QStringList got = s.paths;
        got.sort();
        expected.sort();
        QCOMPARE(got, expected);
        int pages = 0;
        for (const QString &p : expected)
            pages += p.endsWith(u".md"_s) ? 1 : 0;
        QCOMPARE(s.pages, pages);
        QCOMPARE(s.attachments, int(expected.size()) - pages);
    }

    void formats()
    {
        ExportController c;
        const QVariantList formats = c.formats();
        QStringList ids;
        for (const QVariant &v : formats) {
            ids.append(v.toMap().value(u"id"_s).toString());
            QVERIFY(!v.toMap().value(u"description"_s).toString().isEmpty());
            QVERIFY(v.toMap().value(u"ext"_s).toString().startsWith(u'.'));
        }
        QCOMPARE(ids, QStringList({ u"markdown-zip"_s, u"obsidian-zip"_s, u"logseq-zip"_s, u"html-site"_s,
                                    u"html-single"_s, u"json"_s, u"ics"_s, u"opml"_s, u"tasks-csv"_s }));
        c.setFormat(u"nie-ma"_s);
        QCOMPARE(c.format(), u"markdown-zip"_s);
        c.setScope(u"cokolwiek"_s);
        QCOMPARE(c.scope(), u"vault"_s);
        QCOMPARE(c.summary().value(u"valid"_s).toBool(), false);
    }

    void wholeVaultZip()
    {
        QString root;
        auto ws = open(sampleVault(), &root);
        ExportController c;
        c.setReadChunk(2);
        c.setWorkspace(ws.get());
        QVERIFY(c.summary().value(u"valid"_s).toBool());
        QCOMPARE(c.summary().value(u"pages"_s).toInt(), 4);
        QCOMPARE(c.summary().value(u"attachments"_s).toInt(), 3);
        QVERIFY(c.folders().contains(u"Projekty/Ogród"_s));
        QSignalSpy progress(&c, &ExportController::progressChanged);
        c.prepare();
        QCOMPARE(c.state(), u"preparing"_s);
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY(progress.size() >= 4);
        QVERIFY(c.fileName().endsWith(u".zip"_s));
        QVERIFY(!c.fileSizeText().isEmpty());
        const QStringList entries = zipPaths(c.result().first().data);
        QVERIFY2(entries.size() >= 7, qPrintable(entries.join(u", "_s)));
        bool hasJournal = false, hasConfig = false;
        for (const QString &e : entries) {
            hasJournal |= e.endsWith(u"journals/2026-10-14.md"_s);
            hasConfig |= e.endsWith(u".brulion/config.json"_s);
        }
        QVERIFY(hasJournal);
        QVERIFY(hasConfig);

        const QString target = m_tmp.filePath(u"eksport/kopia zapasowa.zip"_s);
        QSignalSpy saved(&c, &ExportController::saved);
        QVERIFY(c.saveTo(QUrl::fromLocalFile(target)));
        QCOMPARE(c.state(), u"saved"_s);
        QCOMPARE(saved.size(), 1);
        QVERIFY(QFileInfo(target).size() > 0);
        QCOMPARE(QDir::fromNativeSeparators(c.savedTo()), target);
        // Changing an option drops the produced file.
        c.setScope(u"page"_s);
        QCOMPARE(c.state(), u"idle"_s);
        QVERIFY(c.result().isEmpty());
    }

    void folderAndPage()
    {
        auto ws = open(sampleVault());
        ExportController c;
        c.setWorkspace(ws.get());
        c.setScope(u"folder"_s);
        QTRY_VERIFY(!c.summary().value(u"valid"_s).toBool());
        QVERIFY(!c.summary().value(u"error"_s).toString().isEmpty());
        c.setFolder(u"Projekty"_s);
        QTRY_COMPARE(c.summary().value(u"files"_s).toInt(), 3);
        QCOMPARE(c.summary().value(u"attachments"_s).toInt(), 1);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        const QStringList entries = zipPaths(c.result().first().data);
        QVERIFY2(entries.size() == 3, qPrintable(entries.join(u", "_s)));
        // The file is named after the folder.
        QVERIFY2(c.fileName().startsWith(u"Projekty"_s), qPrintable(c.fileName()));

        c.setScope(u"page"_s);
        c.setPage(u"Zakupy.md"_s);
        c.setFormat(u"html-single"_s);
        QTRY_COMPARE(c.summary().value(u"files"_s).toInt(), 2);
        QCOMPARE(c.pageTitle(), u"Zakupy"_s);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY(c.fileName().endsWith(u".html"_s));
        const QString html = QString::fromUtf8(c.result().first().data);
        QVERIFY(html.contains(u"mleko"_s));
        QVERIFY(!html.contains(u"marchew"_s));

        c.setPage(u"Nie ma.md"_s);
        QTRY_VERIFY(!c.summary().value(u"valid"_s).toBool());
        c.prepare();
        QCOMPARE(c.state(), u"error"_s);
    }

    void tasksCsvToExportFolder()
    {
        auto ws = open(sampleVault());
        ExportController c;
        c.setWorkspace(ws.get());
        c.setFormat(u"tasks-csv"_s);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        const QString csv = QString::fromUtf8(c.result().first().data);
        QVERIFY2(csv.contains(u"Zadzwonić do Łucji"_s), qPrintable(csv));
        QVERIFY(csv.contains(u"kupić farbę"_s));
        QVERIFY(!c.exportFolder().isEmpty());
        QVERIFY(c.saveToExportFolder());
        const QString first = QDir::fromNativeSeparators(c.savedTo());
        QVERIFY(QFileInfo::exists(first));
        // A second save never overwrites the first.
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY(c.saveToExportFolder());
        QVERIFY(QDir::fromNativeSeparators(c.savedTo()) != first);
        QVERIFY(QFileInfo::exists(QDir::fromNativeSeparators(c.savedTo())));
        QFile::remove(first);
        QFile::remove(QDir::fromNativeSeparators(c.savedTo()));
        // Saving into a folder that cannot exist fails with a message.
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY(!c.saveTo(QUrl::fromLocalFile(u"/proc/nie/wolno.csv"_s)));
        QCOMPARE(c.state(), u"error"_s);
        QVERIFY(!c.errorText().isEmpty());
        QVERIFY(!c.saveInBrowser()); // desktop build
    }

    void cancelWhileReading()
    {
        QHash<QString, QByteArray> files;
        for (int i = 0; i < 200; ++i)
            files.insert(u"Notatki/Strona %1.md"_s.arg(i), "- zażółć\n");
        auto ws = open(files);
        ExportController c;
        c.setReadChunk(5);
        c.setWorkspace(ws.get());
        c.prepare();
        QTRY_VERIFY(c.progress() > 0);
        c.cancel();
        QCOMPARE(c.state(), u"idle"_s);
        QTest::qWait(100);
        QCOMPARE(c.state(), u"idle"_s);
        QVERIFY(c.result().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAppIoExport)
#include "tst_appio_export.moc"
