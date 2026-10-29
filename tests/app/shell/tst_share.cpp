// "Udostępnij → Brulion" on AppController (src/app/shell/appshare.cpp, docs/ANDROID.md): what the
// share dialog shows (pendingShare), saving text to today's journal without date parsing or to a
// new page, files to new pages byte for byte ("Lista zakupów 2" when taken, never over a page
// whose name differs only in letter case) or one journal block each, refused files listed, the
// platform's copies removed (only inside the share inbox), and a share arriving while the
// notebook opens, in the documentation, with no notebook, or through --share. Files opened with
// Brulion on a desktop (--open, a second launch's `share` message): copied, the original never
// changed or removed; a file of the open notebook (also through a linked folder) opens as its page;
// files without an extension typed by their content; opens merged while the dialog waits; the
// message's activation token and folder. The browser's share target (--share-query,
// receiveShareQuery).
#include "appcontroller.h"
#include "workspace.h"

#include "notebook/share.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::shell;

namespace {

bool writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QByteArray readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/// How many times the shell was asked to open the share dialog.
qsizetype shareRequests(const QSignalSpy &spy)
{
    qsizetype n = 0;
    for (const QList<QVariant> &args : spy)
        n += args.value(0).toString() == u"share" ? 1 : 0;
    return n;
}

const QByteArray kShoppingList = "# Lista zakupów\r\n\r\n- [ ] mleko\r\n- [ ] chleb ąę\r\n";

} // namespace

class TestShare : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_counter = 0;

    std::unique_ptr<AppController> makeApp(const QStringList &args = {}, const QStringList &openFiles = {},
                                           const QString &workingDirectory = {})
    {
        AppController::Options o;
        o.settingsFile = m_tmp.filePath(u"settings-%1.ini"_s.arg(++m_counter));
        o.dataDir = m_tmp.filePath(u"data-%1"_s.arg(m_counter));
        o.arguments = args;
        o.openFiles = openFiles;
        o.workingDirectory = workingDirectory;
        auto app = std::make_unique<AppController>(o);
        app->setClock([] { return QDateTime(QDate(2026, 10, 14), QTime(10, 0)); });
        return app;
    }
    /// Files opened with Brulion as main() hands them over (--open, a second launch's message).
    static QVariantMap openedShare(const QStringList &files, const QString &workingDirectory = QString())
    {
        return brulion::notebook::sharedContentToJson(brulion::notebook::openedFilesShare(files, workingDirectory))
                .toVariantMap();
    }
    /// A folder outside every notebook with `files` (name -> bytes); its path.
    QString outsideFolder(const QString &name, const QList<std::pair<QString, QByteArray>> &files)
    {
        const QString dir = m_tmp.filePath(name);
        for (const auto &[file, bytes] : files)
            writeFile(dir + u'/' + file, bytes);
        return dir;
    }
    static qsizetype toastsWith(const QSignalSpy &spy, const QString &text)
    {
        qsizetype n = 0;
        for (const QList<QVariant> &args : spy)
            n += args.value(0).toString() == text ? 1 : 0;
        return n;
    }
    std::unique_ptr<AppController> openApp(const QString &name, const QStringList &args = {})
    {
        auto app = makeApp(args);
        app->start();
        if (app->status() == u"onboarding")
            app->openVault(m_tmp.filePath(name));
        [&] { QTRY_COMPARE(app->status(), u"ready"_s); }();
        return app;
    }
    /// A share as the platform hands it over: `files` (name -> bytes) copied into a new folder
    /// of the app's share inbox.
    QVariantMap fileShare(AppController &app, const QList<std::pair<QString, QByteArray>> &files,
                          const QString &mime = QString())
    {
        const QString dir = app.shareInboxPath() + u"/1792000000000-%1"_s.arg(++m_counter);
        QJsonArray entries;
        for (qsizetype i = 0; i < files.size(); ++i) {
            const QString path = u"%1/%2/%3"_s.arg(dir).arg(i).arg(files.at(i).first);
            writeFile(path, files.at(i).second);
            QJsonObject entry{
                    {u"path"_s, path}, {u"name"_s, files.at(i).first}, {u"size"_s, files.at(i).second.size()}};
            if (!mime.isEmpty())
                entry.insert(u"mime"_s, mime);
            entries.append(entry);
        }
        return QJsonObject{{u"dir"_s, dir}, {u"files"_s, entries}}.toVariantMap();
    }
    QString journal(AppController &app) { return app.workspace()->readFile(u"journals/2026-10-14.md"_s); }
    QByteArray rawPage(AppController &app, const QString &path)
    {
        return readRaw(QDir(app.workspace()->rootPath()).filePath(path));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/brulion-test-bus");
    }

    void textToTheJournal()
    {
        auto app = openApp(u"tekst"_s);
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        QSignalSpy changed(app.get(), &AppController::pendingShareChanged);
        // A browser shares a page: its title and its link (in the text, as Android does).
        app->receiveShare(
                {{u"subject"_s, u"Przepis na żurek jutro o 18"_s}, {u"text"_s, u"https://kuchnia.pl/żurek"_s}});
        QCOMPARE(shareRequests(ui), 1);
        QVERIFY(changed.size() >= 1);
        const QVariantMap pending = app->pendingShare();
        QCOMPARE(pending.value(u"kind"_s).toString(), u"text"_s);
        QCOMPARE(pending.value(u"target"_s).toString(), u"journal"_s);
        QCOMPARE(pending.value(u"text"_s).toString(), u"[Przepis na żurek jutro o 18](https://kuchnia.pl/żurek)"_s);
        QCOMPARE(pending.value(u"title"_s).toString(), u"Przepis na żurek jutro o 18"_s);
        QCOMPARE(pending.value(u"files"_s).toList().size(), 0);

        // Saved as edited in the dialog, and never through quick capture's dates.
        const QVariantMap saved =
                app->saveShare({{u"target"_s, u"journal"_s},
                                {u"text"_s, QString(pending.value(u"text"_s).toString() + u"\nDo zrobienia"_s)}});
        QVERIFY(saved.value(u"ok"_s).toBool());
        QCOMPARE(saved.value(u"count"_s).toInt(), 1);
        QVERIFY(journal(*app).contains(
                u"- [Przepis na żurek jutro o 18](https://kuchnia.pl/żurek)\n  Do zrobienia\n"_s));
        QVERIFY(!journal(*app).contains(u"⏰"_s));
        QVERIFY(!journal(*app).contains(u"📅"_s));
        QVERIFY(app->pendingShare().isEmpty());
        QCOMPARE(app->currentView(), u"journal"_s);
        // Nothing waits any more.
        QVERIFY(!app->saveShare({{u"target"_s, u"journal"_s}, {u"text"_s, u"x"_s}}).value(u"ok"_s).toBool());
    }

    void textToANewPage()
    {
        auto app = openApp(u"tekst-strona"_s);
        app->receiveShare({{u"text"_s, u"Zażółć gęślą jaźń\r\nDruga linia"_s}});
        const QVariantMap pending = app->pendingShare();
        QCOMPARE(pending.value(u"title"_s).toString(), u"Zażółć gęślą jaźń"_s);
        const QVariantMap saved = app->saveShare(
                {{u"target"_s, u"page"_s}, {u"title"_s, QString()}, {u"text"_s, pending.value(u"text"_s)}});
        QVERIFY(saved.value(u"ok"_s).toBool());
        const QString path = saved.value(u"path"_s).toString();
        QVERIFY(path.endsWith(u"Zażółć gęślą jaźń.md"_s));
        QCOMPARE(rawPage(*app, path), u"Zażółć gęślą jaźń\r\nDruga linia\r\n"_s.toUtf8());
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), path);
        // Blank text saves nothing.
        app->receiveShare({{u"subject"_s, u"Tytuł"_s}});
        QVERIFY(!app->saveShare({{u"target"_s, u"page"_s}, {u"text"_s, u"  "_s}}).value(u"ok"_s).toBool());
        QVERIFY(!app->pendingShare().isEmpty());
        app->dismissShare();
        QVERIFY(app->pendingShare().isEmpty());
    }

    void fileToANewPage()
    {
        auto app = openApp(u"plik"_s);
        QVariantMap share = fileShare(*app, {{u"Lista zakupów.md"_s, kShoppingList}}, u"text/markdown"_s);
        const QString dir = share.value(u"dir"_s).toString();
        QVERIFY(QFileInfo::exists(dir));
        app->receiveShare(share);
        QVariantMap pending = app->pendingShare();
        QCOMPARE(pending.value(u"kind"_s).toString(), u"files"_s);
        QCOMPARE(pending.value(u"readable"_s).toInt(), 1);
        QCOMPARE(pending.value(u"target"_s).toString(), u"page"_s);
        QCOMPARE(pending.value(u"title"_s).toString(), u"Lista zakupów"_s);
        const QVariantMap file = pending.value(u"files"_s).toList().value(0).toMap();
        QCOMPARE(file.value(u"name"_s).toString(), u"Lista zakupów.md"_s);
        QCOMPARE(file.value(u"error"_s).toString(), QString());
        QVERIFY(file.value(u"preview"_s).toString().startsWith(u"# Lista zakupów\n\n- [ ] mleko"_s));

        QVariantMap saved = app->saveShare({{u"target"_s, u"page"_s}, {u"title"_s, u"Lista zakupów"_s}});
        QVERIFY(saved.value(u"ok"_s).toBool());
        const QString first = saved.value(u"path"_s).toString();
        QVERIFY(first.endsWith(u"Lista zakupów.md"_s));
        QCOMPARE(rawPage(*app, first), kShoppingList); // byte for byte, CRLF kept
        QVERIFY(!QFileInfo::exists(dir));              // the copies are gone
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), first);

        // The same name again: "Lista zakupów 2", the first page untouched.
        app->receiveShare(fileShare(*app, {{u"Lista zakupów.md"_s, "inna lista\n"}}));
        saved = app->saveShare({{u"target"_s, u"page"_s}, {u"title"_s, u"Lista zakupów"_s}});
        QVERIFY(saved.value(u"ok"_s).toBool());
        QVERIFY(saved.value(u"path"_s).toString().endsWith(u"Lista zakupów 2.md"_s));
        QCOMPARE(rawPage(*app, first), kShoppingList);

        // A name differing only in letter case is that page: nothing written, the share waits,
        // the journal still takes it.
        share = fileShare(*app, {{u"lista.md"_s, "- [ ] mleko\n  - 2 l\n"}});
        app->receiveShare(share);
        saved = app->saveShare({{u"target"_s, u"page"_s}, {u"title"_s, u"LISTA ZAKUPÓW"_s}});
        QVERIFY(!saved.value(u"ok"_s).toBool());
        QVERIFY(saved.value(u"error"_s).toString().contains(u"Lista zakupów"_s));
        QCOMPARE(rawPage(*app, first), kShoppingList);
        QVERIFY(!app->pendingShare().isEmpty());
        QVERIFY(QFileInfo::exists(share.value(u"dir"_s).toString()));
        saved = app->saveShare({{u"target"_s, u"journal"_s}, {u"title"_s, u"LISTA ZAKUPÓW"_s}});
        QVERIFY(saved.value(u"ok"_s).toBool());
        QVERIFY(journal(*app).contains(u"- **LISTA ZAKUPÓW**\n  - [ ] mleko\n    - 2 l\n"_s));
        QVERIFY(!QFileInfo::exists(share.value(u"dir"_s).toString()));
    }

    void severalFiles()
    {
        auto app = openApp(u"pliki"_s);
        QByteArray binary("dane\0\x01\x02", 7);
        app->receiveShare(fileShare(*app, {{u"Dom.md"_s, "---\ntags: dom\n---\n- remont\n"},
                                           {u"zdjęcie.png"_s, "\x89PNG"},
                                           {u"dane.txt"_s, binary},
                                           {u"Łódź.txt"_s, "Za\xBF\xF3\xB3\xE6 \xB9\x9C\xBF\x9F\r\n"}}));
        const QVariantMap pending = app->pendingShare();
        QCOMPARE(pending.value(u"readable"_s).toInt(), 2);
        QCOMPARE(pending.value(u"target"_s).toString(), u"journal"_s);
        const QVariantList files = pending.value(u"files"_s).toList();
        QCOMPARE(files.size(), 4);
        QVERIFY(!files.at(1).toMap().value(u"error"_s).toString().isEmpty()); // not a text file
        QVERIFY(!files.at(2).toMap().value(u"error"_s).toString().isEmpty()); // NUL bytes
        QCOMPARE(files.at(3).toMap().value(u"title"_s).toString(), u"Łódź"_s);
        QVERIFY(files.at(3).toMap().value(u"preview"_s).toString().startsWith(u"Zażółć ąśżź"_s)); // windows-1250

        const QVariantMap saved = app->saveShare({{u"target"_s, u"journal"_s}});
        QVERIFY(saved.value(u"ok"_s).toBool());
        QCOMPARE(saved.value(u"count"_s).toInt(), 2);
        QVERIFY(journal(*app).contains(u"- **Dom**\n  - remont\n- **Łódź**\n  Zażółć ąśżź\n"_s));
        QVERIFY(!journal(*app).contains(u"tags: dom"_s));

        // As pages: one per file, a taken letter-case twin skipped.
        QVERIFY(app->workspace()->write(u"dom.md"_s, u"- mój dom\n"_s));
        app->receiveShare(fileShare(*app, {{u"Dom.md"_s, "- nowy\n"}, {u"Ogród.md"_s, "- grządki\n"}}));
        const QVariantMap pages = app->saveShare({{u"target"_s, u"page"_s}, {u"title"_s, u"pominięty"_s}});
        QVERIFY(pages.value(u"ok"_s).toBool());
        QCOMPARE(pages.value(u"count"_s).toInt(), 1);
        QCOMPARE(pages.value(u"skipped"_s).toStringList(), QStringList{u"Dom"_s});
        QVERIFY(pages.value(u"path"_s).toString().endsWith(u"Ogród.md"_s));
        QCOMPARE(app->workspace()->readFile(u"dom.md"_s), u"- mój dom\n"_s);
    }

    void nothingSavable()
    {
        auto app = openApp(u"nic"_s);
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        app->receiveShare({});
        app->receiveShare({{u"text"_s, u" \n "_s}, {u"files"_s, QVariantList{}}});
        QCOMPARE(shareRequests(ui), 0);
        QCOMPARE(toasts.size(), 2);
        QVERIFY(app->pendingShare().isEmpty());
        // Only refused files: listed, and saving says so.
        app->receiveShare(fileShare(*app, {{u"film.mp4"_s, "...."}}, u"video/mp4"_s));
        QCOMPARE(app->pendingShare().value(u"readable"_s).toInt(), 0);
        QVERIFY(!app->saveShare({{u"target"_s, u"page"_s}}).value(u"ok"_s).toBool());
        // A file too large for Brulion.
        app->receiveShare(fileShare(*app, {{u"duży.md"_s, QByteArray(2 * 1024 * 1024 + 1, 'a')}}));
        QVERIFY(app->pendingShare()
                        .value(u"files"_s)
                        .toList()
                        .value(0)
                        .toMap()
                        .value(u"error"_s)
                        .toString()
                        .contains(u"2"_s));
        app->dismissShare();
    }

    void textWithRefusedFiles()
    {
        // A link shared with a preview image (ClipData) or an attachment that cannot be read: no
        // file can be saved, so the link and its text are what the dialog saves.
        auto app = openApp(u"tekst-i-obraz"_s);
        QVariantMap share = fileShare(*app, {{u"podgląd.png"_s, "\x89PNG"}}, u"image/png"_s);
        share.insert(u"subject"_s, u"Artykuł o żurku"_s);
        share.insert(u"text"_s, u"https://a.pl/żurek"_s);
        QVariantList files = share.value(u"files"_s).toList();
        files.append(QVariantMap{{u"name"_s, u"załącznik.md"_s}, {u"error"_s, u"unreadable"_s}});
        files.append(QVariantMap{{u"name"_s, u"duży.txt"_s}, {u"error"_s, u"tooLarge"_s}});
        share.insert(u"files"_s, files);
        const QString dir = share.value(u"dir"_s).toString();
        app->receiveShare(share);
        const QVariantMap pending = app->pendingShare();
        QCOMPARE(pending.value(u"kind"_s).toString(), u"text"_s);
        QCOMPARE(pending.value(u"readable"_s).toInt(), 0);
        QCOMPARE(pending.value(u"target"_s).toString(), u"journal"_s);
        QCOMPARE(pending.value(u"text"_s).toString(), u"[Artykuł o żurku](https://a.pl/żurek)"_s);
        QCOMPARE(pending.value(u"title"_s).toString(), u"Artykuł o żurku"_s);
        // The refused files are still listed, with their reasons.
        const QVariantList listed = pending.value(u"files"_s).toList();
        QCOMPARE(listed.size(), 3);
        for (const QVariant &file : listed)
            QVERIFY(!file.toMap().value(u"error"_s).toString().isEmpty());

        const QVariantMap saved = app->saveShare({{u"target"_s, u"journal"_s}, {u"text"_s, pending.value(u"text"_s)}});
        QVERIFY(saved.value(u"ok"_s).toBool());
        QCOMPARE(saved.value(u"count"_s).toInt(), 1);
        QVERIFY(journal(*app).contains(u"- [Artykuł o żurku](https://a.pl/żurek)\n"_s));
        QVERIFY(!QFileInfo::exists(dir));

        // As a new page too.
        app->receiveShare(
                {{u"text"_s, u"Notatka z poczty"_s},
                 {u"files"_s, QVariantList{QVariantMap{{u"name"_s, u"x.txt"_s}, {u"error"_s, u"unreadable"_s}}}}});
        QCOMPARE(app->pendingShare().value(u"kind"_s).toString(), u"text"_s);
        const QVariantMap page =
                app->saveShare({{u"target"_s, u"page"_s}, {u"title"_s, QString()}, {u"text"_s, u"Notatka z poczty"_s}});
        QVERIFY(page.value(u"ok"_s).toBool());
        QVERIFY(page.value(u"path"_s).toString().endsWith(u"Notatka z poczty.md"_s));

        // A readable file wins over the text that came with it.
        QVariantMap both = fileShare(*app, {{u"Plan.md"_s, "- punkt\n"}});
        both.insert(u"text"_s, u"temat wiadomości"_s);
        app->receiveShare(both);
        QCOMPARE(app->pendingShare().value(u"kind"_s).toString(), u"files"_s);
        QCOMPARE(app->pendingShare().value(u"text"_s).toString(), QString());
        app->dismissShare();
    }

    void openedFileToANewPage()
    {
        auto app = openApp(u"otwarte"_s);
        const QByteArray bytes = "# Zażółć\r\n\r\n- [ ] gęślą jaźń\r\n";
        const QString dir = outsideFolder(u"pobrane"_s, {{u"Zażółć gęślą jaźń.md"_s, bytes}});
        const QString original = dir + u"/Zażółć gęślą jaźń.md"_s;
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        QSignalSpy shown(app.get(), &AppController::showWindowRequested);
        app->receiveShare(openedShare({u"Zażółć gęślą jaźń.md"_s}, dir));
        QCOMPARE(shareRequests(ui), 1);
        QVERIFY(shown.size() >= 1);
        const QVariantMap pending = app->pendingShare();
        QCOMPARE(pending.value(u"kind"_s).toString(), u"files"_s);
        QCOMPARE(pending.value(u"source"_s).toString(), u"open"_s);
        QCOMPARE(pending.value(u"target"_s).toString(), u"page"_s);
        QCOMPARE(pending.value(u"title"_s).toString(), u"Zażółć gęślą jaźń"_s);
        QCOMPARE(pending.value(u"readable"_s).toInt(), 1);
        const QVariantMap saved = app->saveShare({{u"target"_s, u"page"_s}, {u"title"_s, u"Zażółć gęślą jaźń"_s}});
        QVERIFY2(saved.value(u"ok"_s).toBool(), qPrintable(saved.value(u"error"_s).toString()));
        QCOMPARE(rawPage(*app, saved.value(u"path"_s).toString()), bytes);
        // The opened file is the user's: never changed, never removed (nor its folder).
        QCOMPARE(readRaw(original), bytes);
        QVERIFY(QFileInfo::exists(dir));
        QVERIFY(app->pendingShare().isEmpty());
        // Cancel leaves it too.
        app->receiveShare(openedShare({original}));
        app->dismissShare();
        QCOMPARE(readRaw(original), bytes);
    }

    void openedFileInTheNotebookOpensIt()
    {
        auto app = openApp(u"z notatnika"_s);
        const QString root = app->workspace()->rootPath();
        QVERIFY(writeFile(root + u"/Projekty/Plan.md"_s, "# Plan\n- jutro\n"));
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        QSignalSpy changed(app.get(), &AppController::pendingShareChanged);
        app->receiveShare(openedShare({root + u"/Projekty/Plan.md"_s}));
        QCOMPARE(app->currentRoute().value(u"view"_s).toString(), u"page"_s);
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"Projekty/Plan.md"_s);
        QCOMPARE(shareRequests(ui), 0);
        QVERIFY(app->pendingShare().isEmpty());
        QVERIFY(changed.size() >= 1);
        // Not copied: no second page.
        QVERIFY(!QFileInfo::exists(root + u"/Plan.md"_s));
        QCOMPARE(readRaw(root + u"/Projekty/Plan.md"_s), QByteArray("# Plan\n- jutro\n"));

        // A text file or a file in a folder the notebook hides is no page: offered as a copy.
        QVERIFY(writeFile(root + u"/notatka.txt"_s, "tekst\n"));
        QVERIFY(writeFile(root + u"/.obsidian/x.md"_s, "x\n"));
        app->receiveShare(openedShare({root + u"/notatka.txt"_s, root + u"/.obsidian/x.md"_s}));
        QCOMPARE(shareRequests(ui), 1);
        QCOMPARE(app->pendingShare().value(u"files"_s).toList().size(), 2);
        app->dismissShare();
        QVERIFY(QFileInfo::exists(root + u"/notatka.txt"_s));

#ifdef Q_OS_UNIX
        // Through a linked folder: the same file (canonical paths).
        const QString link = m_tmp.filePath(u"dowiązanie do notatnika"_s);
        QVERIFY(QFile::link(root, link));
        app->goHome();
        app->receiveShare(openedShare({link + u"/Projekty/Plan.md"_s}));
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"Projekty/Plan.md"_s);
        QCOMPARE(shareRequests(ui), 1);
        QVERIFY(app->pendingShare().isEmpty());
#endif
    }

    void openedMixed()
    {
        auto app = openApp(u"mieszane"_s);
        const QString root = app->workspace()->rootPath();
        QVERIFY(writeFile(root + u"/Żółw.md"_s, "# Żółw\n"));
        const QString dir = outsideFolder(u"mieszane-z-zewnątrz"_s, {{u"Łódź.md"_s, "# Łódź\n"}});
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->receiveShare(openedShare({dir + u"/Łódź.md"_s, root + u"/Żółw.md"_s}));
        // The notebook's page opens, the dialog offers the other file over it.
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"Żółw.md"_s);
        QCOMPARE(shareRequests(ui), 1);
        const QVariantList files = app->pendingShare().value(u"files"_s).toList();
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().toMap().value(u"name"_s).toString(), u"Łódź.md"_s);
        QCOMPARE(app->pendingShare().value(u"target"_s).toString(), u"page"_s);
    }

    void extensionlessTextAndPicture()
    {
        auto app = openApp(u"bez rozszerzenia"_s);
        const QByteArray png = QByteArray::fromHex("89504e470d0a1a0a0000000d4948445200000001000000010806000000");
        const QString dir =
                outsideFolder(u"bez-rozszerzen"_s,
                              {{u"notatka"_s, "Zakupy: mleko, chleb\nąę\n"}, {u"obraz"_s, png}, {u"obraz.png"_s, png}});
        app->receiveShare(openedShare({u"notatka"_s, u"obraz"_s, u"obraz.png"_s}, dir));
        const QVariantList files = app->pendingShare().value(u"files"_s).toList();
        QCOMPARE(files.size(), 3);
        QCOMPARE(files.at(0).toMap().value(u"error"_s).toString(), QString());
        QVERIFY(files.at(0).toMap().value(u"preview"_s).toString().contains(u"mleko"_s));
        QCOMPARE(files.at(1).toMap().value(u"error"_s).toString(), u"Not a text file"_s);
        QCOMPARE(files.at(2).toMap().value(u"error"_s).toString(), u"Not a text file"_s);
        QCOMPARE(app->pendingShare().value(u"readable"_s).toInt(), 1);
        app->dismissShare();
        // A missing file and another computer's file cannot be read, a folder is no text file.
        app->receiveShare(openedShare({dir + u"/nie ma.md"_s, u"smb://serwer/a.md"_s, dir}));
        const QVariantList refused = app->pendingShare().value(u"files"_s).toList();
        QCOMPARE(refused.size(), 3);
        QCOMPARE(refused.at(0).toMap().value(u"error"_s).toString(), u"Could not be read"_s);
        QCOMPARE(refused.at(1).toMap().value(u"error"_s).toString(), u"Could not be read"_s);
        QCOMPARE(refused.at(1).toMap().value(u"name"_s).toString(), u"a.md"_s);
        QCOMPARE(refused.at(2).toMap().value(u"error"_s).toString(), u"Not a text file"_s);
        QCOMPARE(app->pendingShare().value(u"readable"_s).toInt(), 0);
        QVERIFY(!app->saveShare({{u"target"_s, u"page"_s}}).value(u"ok"_s).toBool());
        app->dismissShare();
        QVERIFY(QFileInfo::exists(dir + u"/notatka"_s));
    }

    void opensMerge()
    {
        auto app = openApp(u"scalanie"_s);
        const QString dir = outsideFolder(u"scalanie-pliki"_s, {{u"a.md"_s, "a\n"}, {u"b.md"_s, "b\n"}});
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        // Windows: one launch per selected file, each sends its own.
        app->receiveShare(openedShare({dir + u"/a.md"_s}));
        QCOMPARE(app->pendingShare().value(u"target"_s).toString(), u"page"_s);
        app->receiveShare(openedShare({dir + u"/b.md"_s, dir + u"/a.md"_s}));
        QVariantList files = app->pendingShare().value(u"files"_s).toList();
        QCOMPARE(files.size(), 2);
        QCOMPARE(files.at(0).toMap().value(u"name"_s).toString(), u"a.md"_s);
        QCOMPARE(files.at(1).toMap().value(u"name"_s).toString(), u"b.md"_s);
        QCOMPARE(app->pendingShare().value(u"target"_s).toString(), u"journal"_s);
        QCOMPARE(shareRequests(ui), 2); // the dialog reloads
        // Both to the journal, one block each.
        QVERIFY(app->saveShare({{u"target"_s, u"journal"_s}}).value(u"ok"_s).toBool());
        QVERIFY(journal(*app).contains(u"- **a**\n  a\n- **b**\n  b\n"_s));
        // Another app's share replaces opened files; opened files replace another app's share.
        app->receiveShare(openedShare({dir + u"/a.md"_s}));
        app->receiveShare({{u"text"_s, u"nowszy"_s}});
        QCOMPARE(app->pendingShare().value(u"kind"_s).toString(), u"text"_s);
        QCOMPARE(app->pendingShare().value(u"source"_s).toString(), QString());
        app->receiveShare(openedShare({dir + u"/b.md"_s}));
        files = app->pendingShare().value(u"files"_s).toList();
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().toMap().value(u"name"_s).toString(), u"b.md"_s);
        app->dismissShare();
        QVERIFY(QFileInfo::exists(dir + u"/a.md"_s) && QFileInfo::exists(dir + u"/b.md"_s));
    }

    void instanceMessageShare()
    {
        auto app = openApp(u"komunikat"_s);
        const QString dir = outsideFolder(u"komunikat-pliki"_s, {{u"Łódź.md"_s, "# Łódź\n"}});
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        qunsetenv("XDG_ACTIVATION_TOKEN");
        const QJsonObject share = QJsonObject::fromVariantMap(openedShare({dir + u"/Łódź.md"_s}));
        QVERIFY(app->handleInstanceMessage("share", {{u"share"_s, share}, {u"activationToken"_s, u"abc_123"_s}}));
        QCOMPARE(shareRequests(ui), 1);
        QCOMPARE(app->pendingShare().value(u"files"_s).toList().size(), 1);
        QCOMPARE(qgetenv("XDG_ACTIVATION_TOKEN"), QByteArray("abc_123"));
        qunsetenv("XDG_ACTIVATION_TOKEN");
        app->dismissShare();
        // Tokens that are no tokens are ignored (also on other verbs).
        for (const QString &bad : {u"a b"_s, QString(600, u'x'), u"zażółć"_s, u"a\nb"_s, QString()}) {
            QVERIFY(app->handleInstanceMessage("activate", {{u"activationToken"_s, bad}}));
            QVERIFY2(qgetenv("XDG_ACTIVATION_TOKEN").isEmpty(), qPrintable(bad));
        }
        QVERIFY(app->handleInstanceMessage("today", {{u"activationToken"_s, u"t-1"_s}}));
        QCOMPARE(qgetenv("XDG_ACTIVATION_TOKEN"), QByteArray("t-1"));
        qunsetenv("XDG_ACTIVATION_TOKEN");
        // Not an object: nothing to save.
        QVERIFY(app->handleInstanceMessage("share", {{u"share"_s, u"x"_s}}));
        QCOMPARE(toastsWith(toasts, u"Nothing to save."_s), 1);
        QVERIFY(app->pendingShare().isEmpty());
        // A message never names a folder to remove, not even one inside the share inbox.
        const QString copies = app->shareInboxPath() + u"/1-1"_s;
        QVERIFY(writeFile(copies + u"/0/a.md"_s, "a\n"));
        QJsonObject forged = QJsonObject::fromVariantMap(openedShare({copies + u"/0/a.md"_s}));
        forged.insert(u"dir"_s, copies);
        QVERIFY(app->handleInstanceMessage("share", {{u"share"_s, forged}}));
        QCOMPARE(app->pendingShare().value(u"readable"_s).toInt(), 1);
        app->dismissShare();
        QVERIFY(QFileInfo::exists(copies + u"/0/a.md"_s));
        // An unknown verb still changes nothing.
        QVERIFY(!app->handleInstanceMessage("share-files", {{u"share"_s, share}}));
        QVERIFY(app->pendingShare().isEmpty());
    }

    void startWithOpenFiles()
    {
        // `brulion --open "Lista zakupów.md"` from a terminal in another folder, Brulion not running.
        const QString vault = m_tmp.filePath(u"start z plikiem"_s);
        QDir().mkpath(vault);
        const QString dir = outsideFolder(u"start-folder roboczy"_s, {{u"Lista zakupów.md"_s, kShoppingList}});
        auto app = makeApp({u"--vault"_s, vault}, {u"Lista zakupów.md"_s}, dir);
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->start();
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTRY_COMPARE(shareRequests(ui), 1);
        const QVariantMap pending = app->pendingShare();
        QCOMPARE(pending.value(u"source"_s).toString(), u"open"_s);
        QCOMPARE(pending.value(u"title"_s).toString(), u"Lista zakupów"_s);
        QCOMPARE(pending.value(u"files"_s).toList().first().toMap().value(u"name"_s).toString(), u"Lista zakupów.md"_s);
        // The page from the open notebook given at start opens (no dialog).
        app.reset();
        QVERIFY(writeFile(vault + u"/Strona.md"_s, "# Strona\n"));
        auto second = makeApp({u"--vault"_s, vault}, {u"Strona.md"_s}, vault);
        QSignalSpy ui2(second.get(), &AppController::uiActionRequested);
        second->start();
        QTRY_COMPARE(second->status(), u"ready"_s);
        QTRY_COMPARE(second->currentRoute().value(u"path"_s).toString(), u"Strona.md"_s);
        QCOMPARE(shareRequests(ui2), 0);
    }

    void openWithoutANotebook()
    {
        // The start screen: a notice, the opened file waits; the notebook opened, a file of it
        // that was opened meanwhile opens as its page and the other one is offered.
        auto app = makeApp();
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        app->start();
        QCOMPARE(app->status(), u"onboarding"_s);
        const QString dir = outsideFolder(u"bez notatnika"_s, {{u"Łąka.md"_s, "# Łąka\n"}});
        const QString vault = m_tmp.filePath(u"później otwarty"_s);
        QVERIFY(writeFile(vault + u"/Strona.md"_s, "# Strona\n"));
        app->receiveShare(openedShare({dir + u"/Łąka.md"_s, vault + u"/Strona.md"_s}));
        QCOMPARE(shareRequests(ui), 0);
        QCOMPARE(toasts.size(), 1);
        QCOMPARE(app->pendingShare().value(u"files"_s).toList().size(), 2);
        app->openVault(vault);
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTRY_COMPARE(shareRequests(ui), 1);
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"Strona.md"_s);
        const QVariantList files = app->pendingShare().value(u"files"_s).toList();
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().toMap().value(u"name"_s).toString(), u"Łąka.md"_s);
    }

    void startWithoutOpenFiles()
    {
        // `brulion --open` with no files (a menu launch of `Exec=brulion --open %F`), or with
        // empty ones only: a plain start.
        const QString vault = m_tmp.filePath(u"bez plików"_s);
        QDir().mkpath(vault);
        for (const QStringList &files : {QStringList(), QStringList{QString(), QString()}}) {
            auto app = makeApp({u"--vault"_s, vault}, files, m_tmp.path());
            QSignalSpy ui(app.get(), &AppController::uiActionRequested);
            QSignalSpy toasts(app.get(), &AppController::toastRequested);
            app->start();
            QTRY_COMPARE(app->status(), u"ready"_s);
            QTest::qWait(50);
            QCOMPARE(shareRequests(ui), 0);
            QCOMPARE(toastsWith(toasts, u"Nothing to save."_s), 0);
            QVERIFY(app->pendingShare().isEmpty());
        }
    }

    void shareQuery()
    {
        auto app = openApp(u"przegladarka"_s);
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        app->receiveShareQuery(
                u"share-title=Przepis+na+%C5%BCurek&share-text=Zobacz+to+https%3A%2F%2Fkuchnia.pl%2F%C5%BCurek"_s);
        QCOMPARE(shareRequests(ui), 1);
        QVariantMap pending = app->pendingShare();
        QCOMPARE(pending.value(u"kind"_s).toString(), u"text"_s);
        QCOMPARE(pending.value(u"source"_s).toString(), QString());
        QCOMPARE(pending.value(u"text"_s).toString(), u"[Przepis na żurek](https://kuchnia.pl/żurek) — Zobacz to"_s);
        QCOMPARE(pending.value(u"title"_s).toString(), u"Przepis na żurek"_s);
        QVERIFY(app->saveShare({{u"target"_s, u"journal"_s}, {u"text"_s, pending.value(u"text"_s)}})
                        .value(u"ok"_s)
                        .toBool());
        QVERIFY(journal(*app).contains(u"- [Przepis na żurek](https://kuchnia.pl/żurek) — Zobacz to\n"_s));
        // No share parameters: nothing to save, nothing waits.
        for (const QString &query : {QString(), u"nosw=1&selftest=share"_s, u"share-text=&share-title=+"_s}) {
            app->receiveShareQuery(query);
            QVERIFY(app->pendingShare().isEmpty());
        }
        QCOMPARE(toastsWith(toasts, u"Nothing to save."_s), 3);

        // --share-query at start (wasm/index.html), shown once the notebook is ready.
        const QString vault = m_tmp.filePath(u"przegladarka-start"_s);
        QDir().mkpath(vault);
        auto started = makeApp({u"--vault"_s, vault, u"--share-query"_s, u"share-text=Za%C5%BC%C3%B3%C5%82%C4%87"_s});
        QSignalSpy ui2(started.get(), &AppController::uiActionRequested);
        started->start();
        QTRY_COMPARE(started->status(), u"ready"_s);
        QTRY_COMPARE(shareRequests(ui2), 1);
        QCOMPARE(started->pendingShare().value(u"text"_s).toString(), u"Zażółć"_s);
    }

    void copiesRemovedOnlyInTheInbox()
    {
        auto app = openApp(u"porzadek"_s);
        // "Cancel": the copies go.
        QVariantMap share = fileShare(*app, {{u"a.md"_s, "a\n"}});
        app->receiveShare(share);
        app->dismissShare();
        QVERIFY(!QFileInfo::exists(share.value(u"dir"_s).toString()));
        // A newer share replaces the waiting one, whose copies go too.
        const QVariantMap older = fileShare(*app, {{u"b.md"_s, "b\n"}});
        app->receiveShare(older);
        app->receiveShare({{u"text"_s, u"nowszy"_s}});
        QVERIFY(!QFileInfo::exists(older.value(u"dir"_s).toString()));
        QCOMPARE(app->pendingShare().value(u"text"_s).toString(), u"nowszy"_s);
        app->dismissShare();
        // A folder outside the inbox (any JSON may name one) is never removed, nor the inbox itself.
        const QString outside = m_tmp.filePath(u"cudzy folder"_s);
        QVERIFY(writeFile(outside + u"/ważne.md"_s, "ważne\n"));
        app->receiveShare({{u"dir"_s, outside},
                           {u"files"_s, QVariantList{QVariantMap{{u"path"_s, QString(outside + u"/ważne.md"_s)}}}}});
        app->dismissShare();
        QVERIFY(QFileInfo::exists(outside + u"/ważne.md"_s));
        QVERIFY(writeFile(app->shareInboxPath() + u"/zostaje.md"_s, "x"));
        const QStringList notCopies{app->shareInboxPath(), app->shareInboxPath() + u"/../share-in"_s,
                                    app->shareInboxPath() + u"/x/../.."_s};
        for (const QString &dir : notCopies) {
            app->receiveShare({{u"dir"_s, dir}, {u"text"_s, u"t"_s}});
            app->dismissShare();
        }
        QVERIFY(QFileInfo::exists(app->shareInboxPath() + u"/zostaje.md"_s));
        QVERIFY(QFileInfo::exists(app->workspace()->rootPath()));
    }

    void waitsForTheNotebook()
    {
        // Shared right at a cold start: the dialog opens once the notebook is ready.
        const QString vault = m_tmp.filePath(u"czeka"_s);
        QDir().mkpath(vault);
        auto app = makeApp({u"--vault"_s, vault});
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->start();
        QCOMPARE(app->status(), u"loading"_s);
        app->receiveShare({{u"text"_s, u"Zanotuj to"_s}});
        QCOMPARE(shareRequests(ui), 0);
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTRY_COMPARE(shareRequests(ui), 1);
        QCOMPARE(app->pendingShare().value(u"text"_s).toString(), u"Zanotuj to"_s);
    }

    void developerOption()
    {
        const QString vault = m_tmp.filePath(u"opcja"_s);
        QDir().mkpath(vault);
        const QByteArray json = "{\"subject\":\"Wideo\",\"text\":\"Zobacz to https://yt.be/abc\"}";
        auto app = makeApp({u"--vault"_s, vault, u"--share"_s, QString::fromUtf8(json)});
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->start();
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTRY_COMPARE(shareRequests(ui), 1);
        QCOMPARE(app->pendingShare().value(u"text"_s).toString(), u"[Wideo](https://yt.be/abc) — Zobacz to"_s);
        // The platform's JSON as the Android bridge hands it over.
        app->receiveShareJson("{\"text\":\"z JSON-a — ąę\"}");
        QCOMPARE(app->pendingShare().value(u"text"_s).toString(), u"z JSON-a — ąę"_s);
        QCOMPARE(shareRequests(ui), 2);
    }

    void withoutANotebook()
    {
        // The start screen: a notice, and the share waits for a notebook.
        auto app = makeApp();
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        app->start();
        QCOMPARE(app->status(), u"onboarding"_s);
        app->receiveShare({{u"text"_s, u"Na później"_s}});
        QCOMPARE(shareRequests(ui), 0);
        QCOMPARE(toasts.size(), 1);
        QVERIFY(!app->saveShare({{u"target"_s, u"journal"_s}, {u"text"_s, u"Na później"_s}}).value(u"ok"_s).toBool());
        app->openVault(m_tmp.filePath(u"nowy"_s));
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTRY_COMPARE(shareRequests(ui), 1);
        QVERIFY(app->saveShare({{u"target"_s, u"journal"_s}, {u"text"_s, u"Na później"_s}}).value(u"ok"_s).toBool());
        QVERIFY(journal(*app).contains(u"- Na później\n"_s));
    }

    void fromTheDocumentation()
    {
        // Shared notes never land in the documentation: back to the user's notebook first.
        auto app = openApp(u"moje"_s);
        const QString mine = QDir(m_tmp.filePath(u"moje"_s)).absolutePath();
        app->openDocsVault();
        QTRY_VERIFY(app->inDocsVault());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->receiveShare({{u"text"_s, u"Do mojego notatnika"_s}});
        QVERIFY(!app->saveShare({{u"target"_s, u"journal"_s}, {u"text"_s, u"x"_s}}).value(u"ok"_s).toBool());
        QTRY_COMPARE(app->workspace()->rootPath(), mine);
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTRY_COMPARE(shareRequests(ui), 1);
        QVERIFY(!app->inDocsVault());
        QVERIFY(app->saveShare({{u"target"_s, u"journal"_s}, {u"text"_s, u"Do mojego notatnika"_s}})
                        .value(u"ok"_s)
                        .toBool());
        QVERIFY(journal(*app).contains(u"- Do mojego notatnika\n"_s));
    }
};

QTEST_GUILESS_MAIN(TestShare)
#include "tst_share.moc"
