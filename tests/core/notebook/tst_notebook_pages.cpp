// Pages and folders (notebook/pages.h), the code the app's Workspace and brulion-cli share: new
// page paths and pages (letter-case twins, sanitised names, dates as journal pages, the note
// mode), folders (hidden names refused), renames and moves that keep every link and tag pointing
// at the same file (the rows of tests/app/shell tst_shell_workspace renameKeepsTargets/movePath,
// here without the app) with .brulion/config.json following special folders, and deleting to
// the trash.
#include "notebook/pages.h"

#include "index/vaultindex.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;

namespace {

void writeBehind(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    QVERIFY(f.commit());
}

QByteArray readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/// A vault and an index that follows its changes (what Workspace and Notebook do).
struct Env {
    QString root;
    std::unique_ptr<Vault> vault;
    std::unique_ptr<VaultIndex> index;

    void build()
    {
        index = std::make_unique<VaultIndex>(vault->config());
        for (const FileStat &f : vault->files())
            index->update(f.path, paths::isMarkdown(f.path) ? vault->readText(f.path) : QString());
        QObject::connect(vault.get(), &Vault::changed, vault.get(), [this](const QList<VaultChange> &changes) {
            for (const VaultChange &c : changes) {
                if (c.type == ChangeType::Deleted)
                    index->remove(c.path);
                else if (c.type == ChangeType::Renamed)
                    index->rename(c.oldPath, c.path, paths::isMarkdown(c.path) ? vault->readText(c.path) : QString());
                else
                    index->update(c.path, paths::isMarkdown(c.path) ? vault->readText(c.path) : QString());
            }
        });
        QObject::connect(vault.get(), &Vault::configChanged, vault.get(),
                         [this] { index->setConfig(vault->config()); });
    }
};

QString problemString(const PathProblem &p)
{
    static const char *names[] = {"none",       "missing",      "noName",     "intoItself",
                                  "hiddenPath", "hiddenFolder", "notAFolder", "folderExists",
                                  "pageExists", "fileExists",   "failed"};
    return QString::fromLatin1(names[p.kind]) + (p.name.isEmpty() ? QString() : u':' + p.name);
}

} // namespace

class TestNotebookPages : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<Env> open(const QHash<QString, QByteArray> &files)
    {
        auto env = std::make_unique<Env>();
        env->root = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        QDir().mkpath(env->root);
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeBehind(env->root + u'/' + it.key(), it.value());
        env->vault = std::make_unique<Vault>(env->root, m_tmp.filePath(u"s%1"_s.arg(m_n)));
        if (!env->vault->open())
            return nullptr;
        env->build();
        return env;
    }

private Q_SLOTS:
    void newPagePaths_data()
    {
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("path");
        QTest::newRow("name") << u"Dom"_s << u"Dom.md"_s;
        QTest::newRow("with folders") << u"Projekty/Dom"_s << u"Projekty/Dom.md"_s;
        QTest::newRow("folder spelled by the vault") << u"projekty/Nowy"_s << u"Projekty/Nowy.md"_s;
        QTest::newRow("sanitised") << u"Ważne: plan?/Łódź"_s << u"Ważne plan/Łódź.md"_s;
        QTest::newRow("typed with .md") << u"Notatka.md"_s << u"Notatka.md"_s;
        QTest::newRow("date") << u"2026-10-20"_s << u"journals/2026-10-20.md"_s;
        QTest::newRow("empty") << u"  "_s << QString();
        QTest::newRow("only slashes") << u"/ / /"_s << QString();
    }
    void newPagePaths()
    {
        QFETCH(QString, target);
        QFETCH(QString, path);
        auto env = open({{u"Projekty/Dom.md"_s, "x"}});
        QVERIFY(env);
        QCOMPARE(newPagePath(*env->vault, target), path);
    }

    void createPages()
    {
        auto env = open({{u"Projekty/Dom.md"_s, "- dom\n"}});
        QVERIFY(env);
        const CreatedPage page = createPage(*env->vault, u"Projekty/Ogród"_s, QString());
        QCOMPARE(page.path, u"Projekty/Ogród.md"_s);
        QVERIFY(!page.existing);
        QVERIFY(QFile::exists(env->root + u"/Projekty/Ogród.md"_s));
        // A letter-case twin is that page: nothing written.
        const CreatedPage twin = createPage(*env->vault, u"projekty/dom"_s, QString());
        QCOMPARE(twin.path, u"Projekty/Dom.md"_s);
        QVERIFY(twin.existing);
        QCOMPARE(twin.wanted, u"projekty/dom.md"_s); // as asked for
        QCOMPARE(readRaw(env->root + u"/Projekty/Dom.md"_s), "- dom\n"_ba);
        // Taken: "… 2".
        QCOMPARE(createPage(*env->vault, u"Projekty/Ogród"_s, QString()).path, u"Projekty/Ogród 2.md"_s);
        // Sanitised, with content and a mode that differs from the default (pages: document).
        const CreatedPage plan = createPage(*env->vault, u"Ważne: plan?"_s, u""_s, NoteMode::Outline, u"- a\n"_s);
        QCOMPARE(plan.path, u"Ważne plan.md"_s);
        QCOMPARE(env->vault->readText(plan.path), u"---\nbrulion: outline\n---\n- a\n"_s);
        // An empty name.
        const CreatedPage none = createPage(*env->vault, u" / "_s, QString());
        QVERIFY(none.path.isEmpty());
        QCOMPARE(none.problem.kind, PathProblem::NoName);
    }

    void createFolders_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("problem");
        QTest::newRow("nested") << u"Projekty/Nowy/Głęboki"_s << u"Projekty/Nowy/Głęboki"_s << u"none"_s;
        QTest::newRow("sanitised") << u"Plany: 2026?"_s << u"Plany 2026"_s << u"none"_s;
        QTest::newRow("spelled by the vault") << u"projekty/x"_s << u"Projekty/x"_s << u"none"_s;
        QTest::newRow("hidden") << u".ukryty"_s << QString() << u"hiddenFolder:.ukryty"_s;
        QTest::newRow("ignored") << u"a/node_modules"_s << QString() << u"hiddenFolder:node_modules"_s;
        QTest::newRow("a file") << u"Plan.md"_s << QString() << u"notAFolder:Plan.md"_s;
        QTest::newRow("empty") << u" / . / .."_s << QString() << u"noName"_s;
    }
    void createFolders()
    {
        QFETCH(QString, input);
        QFETCH(QString, path);
        QFETCH(QString, problem);
        auto env = open({{u"Projekty/Dom.md"_s, "x"}, {u"Plan.md"_s, "x"}});
        QVERIFY(env);
        const CreatedFolder folder = createFolder(*env->vault, input);
        QCOMPARE(folder.path, path);
        QCOMPARE(problemString(folder.problem), problem);
        if (!path.isEmpty())
            QVERIFY(QFileInfo(env->root + u'/' + path).isDir());
    }

    void folderListAndInfo()
    {
        auto env = open({{u"Projekty 2/a.md"_s, "x"},
                         {u"Projekty/Dom.md"_s, "x"},
                         {u"Projekty/Dom/Kuchnia.md"_s, "x"},
                         {u"Projekty/plan.png"_s, "png"},
                         {u"journals/2026-10-14.md"_s, "x"},
                         {u"Ćwiczenia/x.md"_s, "x"},
                         {u".git/HEAD"_s, "x"}});
        QVERIFY(env);
        QCOMPARE(folderPaths(*env->vault),
                 (QStringList{u"Ćwiczenia"_s, u"journals"_s, u"Projekty"_s, u"Projekty/Dom"_s, u"Projekty 2"_s}));
        const FolderInfo info = folderInfo(*env->vault, u"Projekty"_s);
        QVERIFY(info.exists);
        QCOMPARE(info.pages, 2);
        QCOMPARE(info.attachments, 1);
        QCOMPARE(info.folders, 1);
        QVERIFY(info.specials.isEmpty());
        QCOMPARE(folderInfo(*env->vault, u"journals"_s).specials.first().special, u"journals"_s);
        QVERIFY(!folderInfo(*env->vault, u"Brak"_s).exists);
    }

    // The rows of the app's tst_shell_workspace renameKeepsTargets, through the core directly.
    void renameKeepsTargets_data()
    {
        QTest::addColumn<QStringList>("files"); // "path=content"
        QTest::addColumn<QString>("action");    // "page:<path>:<name>", "folder:…", "title:…", "move:<path>:<folder>"
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
        QTest::newRow("slash moves the page")
                << QStringList{u"Projekty/Dom.md=x"_s, u"Notatka.md=[[Projekty/Dom]] [[Dom]]"_s}
                << u"page:Projekty/Dom.md:Archiwum/Stary dom"_s << u"Archiwum/Stary dom.md"_s
                << QStringList{u"Notatka.md=[[Archiwum/Stary dom]] [[Stary dom]]"_s};
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
                               u"Łąka.md=﻿- [[Żółw]]\r\n- [[Zwierzęta/Żółw]] #żółw\r\n"_s}
                << u"move:Zwierzęta/Żółw.md:Gady"_s << u"Gady/Żółw.md"_s
                << QStringList{u"Łąka.md=﻿- [[Żółw]]\r\n- [[Gady/Żółw]] #żółw\r\n"_s};
    }
    void renameKeepsTargets()
    {
        QFETCH(QStringList, files);
        QFETCH(QString, action);
        QFETCH(QString, newPath);
        QFETCH(QStringList, expected);
        QHash<QString, QByteArray> map;
        for (const QString &f : std::as_const(files))
            map.insert(f.section(u'=', 0, 0), f.section(u'=', 1).toUtf8());
        auto env = open(map);
        QVERIFY(env);
        const QString kind = action.section(u':', 0, 0);
        const QString path = action.section(u':', 1, 1);
        const QString arg = action.section(u':', 2);
        const bool slashMoves = kind != u"title";
        const QString to = kind == u"move"
                                   ? (arg.isEmpty() ? paths::basename(path) : arg + u'/' + paths::basename(path))
                                   : renamedPath(path, arg, kind == u"folder", slashMoves);
        const RelocationImpact impact = relocationImpact(*env->vault, env->index.get(), path, to);
        QVERIFY(impact.ok);
        QCOMPARE(impact.path, newPath);
        const Relocation moved = kind == u"folder" ? renameFolder(*env->vault, env->index.get(), path, arg, slashMoves)
                                 : kind == u"move" ? movePath(*env->vault, env->index.get(), path, arg)
                                                   : renamePage(*env->vault, env->index.get(), path, arg, slashMoves);
        QCOMPARE(moved.path, newPath);
        QVERIFY(moved.problem.isOk());
        QVERIFY(env->vault->exists(newPath));
        QVERIFY(!env->vault->exists(path));
        for (const QString &e : std::as_const(expected)) {
            const QString file = e.section(u'=', 0, 0);
            QCOMPARE(readRaw(env->root + u'/' + file), e.section(u'=', 1).toUtf8()); // bytes: EOLs, BOM
        }
        QCOMPARE(impact.notes, int(expected.size()));
        QCOMPARE(moved.rewritten.size(), expected.size());
    }

    void moveProblems_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("expected"); // "" = refused
        QTest::addColumn<QString>("problem");
        QTest::newRow("page into a subfolder")
                << u"Notatka.md"_s << u"Projekty/Dom"_s << u"Projekty/Dom/Notatka.md"_s << u"none"_s;
        QTest::newRow("page to the root") << u"Archiwum/Stare.md"_s << QString() << u"Stare.md"_s << u"none"_s;
        QTest::newRow("folder into a folder") << u"Projekty/Dom"_s << u"Archiwum"_s << u"Archiwum/Dom"_s << u"none"_s;
        QTest::newRow("attachment") << u"assets/plan.png"_s << u"Projekty"_s << u"Projekty/plan.png"_s << u"none"_s;
        QTest::newRow("folder spelled in another case")
                << u"Notatka.md"_s << u"archiwum"_s << u"Archiwum/Notatka.md"_s << u"none"_s;
        QTest::newRow("where it is") << u"Projekty/Plan.md"_s << u"Projekty"_s << u"Projekty/Plan.md"_s << u"none"_s;
        QTest::newRow("into itself") << u"Projekty"_s << u"Projekty/Dom"_s << QString() << u"intoItself"_s;
        QTest::newRow("onto an existing name") << u"Plan.md"_s << u"Projekty"_s << QString() << u"pageExists:Plan"_s;
        QTest::newRow("onto a case twin") << u"Notatka.md"_s << u"Inne"_s << QString() << u"pageExists:notatka"_s;
        QTest::newRow("folder onto a case twin")
                << u"Archiwum"_s << u"Inne"_s << QString() << u"folderExists:archiwum"_s;
        QTest::newRow("into .git") << u"Notatka.md"_s << u".git"_s << QString() << u"hiddenPath:Notatka.md"_s;
        QTest::newRow("into a new hidden folder")
                << u"Notatka.md"_s << u".ukryty"_s << QString() << u"hiddenFolder:.ukryty"_s;
        QTest::newRow("into a file") << u"Notatka.md"_s << u"Plan.md"_s << QString() << u"notAFolder:Plan.md"_s;
        QTest::newRow("missing") << u"Brak.md"_s << QString() << QString() << u"missing:Brak"_s;
    }
    void moveProblems()
    {
        QFETCH(QString, path);
        QFETCH(QString, folder);
        QFETCH(QString, expected);
        QFETCH(QString, problem);
        auto env = open({{u"Notatka.md"_s, "- [[Plan]] i [[Dom]]\n"},
                         {u"Plan.md"_s, "x"},
                         {u"Projekty/Plan.md"_s, "- [[Notatka]]\n"},
                         {u"Projekty/Dom/Kuchnia.md"_s, "x"},
                         {u"Archiwum/Stare.md"_s, "x"},
                         {u"Inne/notatka.md"_s, "x"},
                         {u"Inne/archiwum/x.md"_s, "x"},
                         {u"assets/plan.png"_s, "png"},
                         {u".git/config"_s, "x"}});
        QVERIFY(env);
        const Relocation moved = movePath(*env->vault, env->index.get(), path, folder);
        QCOMPARE(moved.path, expected);
        QCOMPARE(problemString(moved.problem), problem);
        if (!expected.isEmpty())
            QVERIFY(env->vault->exists(expected));
        if (env->vault->exists(u"Notatka.md"_s))
            QCOMPARE(env->index->resolveLink(u"Plan"_s, u"Notatka.md"_s).value_or(QString()), u"Plan.md"_s);
    }

    void renameProblems()
    {
        auto env = open({{u"A/a.md"_s, "x"}});
        QVERIFY(env);
        QCOMPARE(problemString(renameFolder(*env->vault, env->index.get(), u"A"_s, u".ukryty"_s, false).problem),
                 u"hiddenFolder:.ukryty"_s);
        QCOMPARE(problemString(renameFolder(*env->vault, env->index.get(), u"A"_s, u"Ok/node_modules"_s, true).problem),
                 u"hiddenFolder:node_modules"_s);
        QCOMPARE(problemString(renameFolder(*env->vault, env->index.get(), u"A"_s, u"  "_s, false).problem),
                 u"noName"_s);
        QCOMPARE(problemString(renamePage(*env->vault, env->index.get(), u"Brak.md"_s, u"x"_s, false).problem),
                 u"missing:Brak"_s);
        QCOMPARE(problemString(renamePage(*env->vault, env->index.get(), u"A/a.md"_s, u"   "_s, false).problem),
                 u"noName"_s);
        // A rename that changes only the letter case is fine (one file on case-insensitive storage).
        const Relocation caseOnly = renamePage(*env->vault, env->index.get(), u"A/a.md"_s, u"A"_s, false);
        QCOMPARE(caseOnly.path, u"A/A.md"_s);
        QVERIFY(QFile::exists(env->root + u"/A/A.md"_s));
        // Without a "/" moving, the name is one folder name.
        QCOMPARE(renameFolder(*env->vault, env->index.get(), u"A"_s, u"Ok/node_modules"_s, false).path,
                 u"Ok node_modules"_s);
    }

    void specialFoldersFollow()
    {
        auto env = open({{u"journals/2026-10-13.md"_s, "- wczoraj\n"},
                         {u"templates/journal.md"_s, "- szablon\n"},
                         {u"assets/plan.png"_s, "png"},
                         {u"Dom.md"_s, "![[plan.png]]\n"}});
        QVERIFY(env);
        const Relocation journals = renameFolder(*env->vault, env->index.get(), u"journals"_s, u"Dziennik"_s, false);
        QCOMPARE(journals.path, u"Dziennik"_s);
        QCOMPARE(journals.rebased.size(), 1);
        QCOMPARE(journals.rebased.first().key, u"journalsFolder"_s);
        QCOMPARE(env->vault->config().journalsFolder, u"Dziennik"_s);
        QVERIFY(journals.configError.isEmpty());
        const Relocation templates = renameFolder(*env->vault, env->index.get(), u"templates"_s, u"Szablony"_s, false);
        QCOMPARE(templates.rebased.size(), 2); // the folder and the journal template in it
        QCOMPARE(env->vault->config().journalTemplate, u"Szablony/journal.md"_s);
        const Relocation assets = movePath(*env->vault, env->index.get(), u"assets"_s, u"Pliki"_s);
        QCOMPARE(assets.path, u"Pliki/assets"_s);
        QCOMPARE(env->vault->config().attachmentsFolder, u"Pliki/assets"_s);
        QCOMPARE(env->index->resolveLink(u"plan.png"_s, u"Dom.md"_s).value_or(QString()), u"Pliki/assets/plan.png"_s);
        // Others leave the settings alone.
        QVERIFY(!createFolder(*env->vault, u"Inny"_s).path.isEmpty());
        QVERIFY(renameFolder(*env->vault, env->index.get(), u"Inny"_s, u"Inny 2"_s, false).rebased.isEmpty());
        QCOMPARE(specialsWithin(env->vault->config(), u"Pliki"_s).first().special, u"attachments"_s);
    }

    void deleteToTheTrash()
    {
        auto env = open({{u"Projekty/Dom.md"_s, "x"}, {u"Notatka.md"_s, "y"}});
        QVERIFY(env);
        const DeletedPath page = deletePath(*env->vault, u"Notatka.md"_s);
        QCOMPARE(page.trashPath, u".trash/Notatka.md"_s);
        QVERIFY(!page.folder);
        QVERIFY(QFile::exists(env->root + u"/.trash/Notatka.md"_s));
        const DeletedPath folder = deletePath(*env->vault, u"Projekty"_s);
        QCOMPARE(folder.trashPath, u".trash/Projekty"_s);
        QVERIFY(folder.folder);
        QCOMPARE(problemString(deletePath(*env->vault, u"Brak.md"_s).problem), u"missing:Brak"_s);
        QVERIFY(!env->index->note(u"Notatka.md"_s));
    }

    void renamedPaths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("isDir");
        QTest::addColumn<bool>("slashMoves");
        QTest::addColumn<QString>("result");
        QTest::newRow("page") << u"A/Dom.md"_s << u"Ogród"_s << false << false << u"A/Ogród.md"_s;
        QTest::newRow("typed with .md") << u"A/Dom.md"_s << u"Ogród.md"_s << false << false << u"A/Ogród.md"_s;
        QTest::newRow("attachment keeps its type") << u"a/plan.png"_s << u"rzut"_s << false << false << u"a/rzut.png"_s;
        QTest::newRow("slash moves") << u"A/Dom.md"_s << u"B/C/Dom"_s << false << true << u"B/C/Dom.md"_s;
        QTest::newRow("slash sanitised") << u"A/Dom.md"_s << u"TCP/IP"_s << false << false << u"A/TCP IP.md"_s;
        QTest::newRow("folder") << u"A/B"_s << u"Nowy?"_s << true << false << u"A/Nowy"_s;
        QTest::newRow("empty") << u"A/Dom.md"_s << u" "_s << false << false << QString();
    }
    void renamedPaths()
    {
        QFETCH(QString, path);
        QFETCH(QString, name);
        QFETCH(bool, isDir);
        QFETCH(bool, slashMoves);
        QFETCH(QString, result);
        QCOMPARE(renamedPath(path, name, isDir, slashMoves), result);
    }
};

QTEST_GUILESS_MAIN(TestNotebookPages)
#include "tst_notebook_pages.moc"
