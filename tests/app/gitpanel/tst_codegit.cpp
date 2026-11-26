// CodeGit (src/app/gitpanel), the changes panel's C++, with real git in temporary repositories
// (skipped without git on PATH): a folder that is no repository, no folder, a deleted folder, a
// repository without commits, a detached HEAD; the rows the panel's delegates read (Polish names in
// nested folders, renames); refreshes by the watcher when git changes the index or HEAD — also in a
// linked worktree, whose .git is a file — and by the poll; a file's diff; commit, commit and push
// to a bare remote, commit, push and a pull request through a fake gh; branches; worktrees;
// checkpoints of a turn — the id at once, checkpointMade() after, turnChanges, changesSince(),
// restore() — one action after another; the queue dropped when the folder changes.
#include "codegit.h"

#include "testtiming.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

using namespace Qt::StringLiterals;
using brulion::gitpanel::CodeGit;

namespace {

constexpr int Wait = brulion::test::kRoomyMs;

QString gitProgram()
{
    return QStandardPaths::findExecutable(u"git"_s);
}

QByteArray gitIn(const QString &folder, const QStringList &args)
{
    QProcess p;
    p.setProgram(gitProgram());
    p.setArguments(QStringList{u"-C"_s, folder} + args);
    p.start();
    p.closeWriteChannel();
    p.waitForFinished(Wait);
    if (p.exitCode() != 0)
        qWarning() << "git" << args << "failed:" << p.readAllStandardError();
    return p.readAllStandardOutput();
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        qWarning() << "cannot write" << path;
    f.write(content);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
}

class Repo {
public:
    explicit Repo(bool init = true)
    {
        root = QFileInfo(dir.path()).canonicalFilePath();
        if (init) {
            git({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
            git({u"config"_s, u"user.name"_s, u"Testowy Użytkownik"_s});
            git({u"config"_s, u"user.email"_s, u"test@example.com"_s});
        }
    }
    QByteArray git(const QStringList &args) const { return gitIn(root, args); }
    QString text(const QStringList &args) const { return QString::fromUtf8(git(args)).trimmed(); }
    QString path(const QString &rel) const { return root + u'/' + rel; }
    void write(const QString &rel, const QByteArray &content) const { writeFile(path(rel), content); }
    QByteArray read(const QString &rel) const { return readFile(path(rel)); }
    void commitAll(const QString &message) const
    {
        git({u"add"_s, u"-A"_s});
        git({u"commit"_s, u"-q"_s, u"-m"_s, message});
    }

    QTemporaryDir dir;
    QString root;
};

QVariantMap rowOf(const QVariantList &rows, const QString &path)
{
    for (const QVariant &v : rows) {
        const QVariantMap m = v.toMap();
        if (m.value(u"path"_s).toString() == path)
            return m;
    }
    return {};
}

/// The arguments of the last actionFinished() with this action (empty when none).
QList<QVariant> finished(const QSignalSpy &spy, const QString &action)
{
    for (qsizetype i = spy.size() - 1; i >= 0; --i) {
        if (spy.at(i).at(0).toString() == action)
            return spy.at(i);
    }
    return {};
}

} // namespace

class TestCodeGit : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void noFolder();
    void notARepository();
    void deletedFolder();
    void rows();
    void emptyRepository();
    void detachedHead();
    void watcherFollowsGit();
    void watcherInWorktree();
    void poll();
    void diff();
    void commit();
    void commitAndPush();
    void commitPushAndPullRequest();
    void branches();
    void worktrees();
    void turnCheckpoint();
    void actionsInOrder();
    void folderChangeDropsTheQueue();
    void tools();

private:
    QTemporaryDir m_home;
};

void TestCodeGit::initTestCase()
{
    if (gitProgram().isEmpty())
        QSKIP("git is not on PATH");
    const QString global = m_home.filePath(u"gitconfig"_s);
    writeFile(global, "[init]\n\tdefaultBranch = main\n[advice]\n\tdetachedHead = false\n");
    qputenv("GIT_CONFIG_GLOBAL", global.toUtf8());
    qputenv("GIT_CONFIG_NOSYSTEM", "1");
    qputenv("HOME", m_home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", m_home.filePath(u"xdg"_s).toUtf8());
    qputenv("GIT_CEILING_DIRECTORIES",
            (QDir::tempPath() + QDir::listSeparator() + QFileInfo(QDir::tempPath()).canonicalFilePath()).toUtf8());
}

void TestCodeGit::noFolder()
{
    CodeGit git;
    git.refresh();
    QVERIFY(git.loaded());
    QVERIFY(!git.isRepo());
    QVERIFY(git.changes().isEmpty());
    QVERIFY(!git.busy());
}

void TestCodeGit::notARepository()
{
    Repo folder(false);
    writeFile(folder.path(u"notatka.md"_s), "# Zażółć\n");
    CodeGit git;
    QSignalSpy state(&git, &CodeGit::stateChanged);
    git.setFolder(folder.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded(), Wait);
    QVERIFY(!git.isRepo());
    QVERIFY(!git.lastError().isEmpty());
    QVERIFY(git.topLevel().isEmpty());

    // An action there fails without running.
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.commit(u"x"_s, false, false, QString(), QString(), false);
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"commit"_s).isEmpty(), Wait);
    QCOMPARE(finished(actions, u"commit"_s).at(1).toBool(), false);
    QTRY_VERIFY_WITH_TIMEOUT(!git.busy(), Wait);

    // `git init` there: the watched folder sees .git appear.
    gitIn(folder.root, {u"init"_s, u"-q"_s});
    QTRY_VERIFY_WITH_TIMEOUT(git.isRepo(), Wait);
    QCOMPARE(git.topLevel(), folder.root);
}

void TestCodeGit::deletedFolder()
{
    auto repo = std::make_unique<Repo>();
    repo->write(u"a.txt"_s, "a\n");
    repo->commitAll(u"a"_s);
    CodeGit git;
    git.setFolder(repo->root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.isRepo(), Wait);
    repo.reset();
    git.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!git.isRepo(), Wait);
    QVERIFY(git.changes().isEmpty());
    QVERIFY(!git.lastError().isEmpty());
}

void TestCodeGit::rows()
{
    Repo repo;
    repo.write(u"zmieniony.txt"_s, "a\nb\n");
    repo.write(u"stara nazwa.txt"_s, "tekst, który zostaje\nten sam\npo zmianie nazwy\n");
    repo.write(u"usunięty.txt"_s, "x\n");
    repo.commitAll(u"Początek"_s);
    repo.write(u"zmieniony.txt"_s, "a\nB\nc\n");
    repo.git({u"mv"_s, u"stara nazwa.txt"_s, u"nowa nazwa.txt"_s});
    QFile::remove(repo.path(u"usunięty.txt"_s));
    repo.write(u"folder/głęboki/zażółć gęślą.md"_s, "jaźń\n");

    CodeGit git;
    QSignalSpy revision(&git, &CodeGit::revisionChanged);
    git.setFolder(repo.path(u"folder/głęboki"_s)); // any folder of the work tree
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.changeCount() == 4, Wait);
    QVERIFY(git.isRepo());
    QCOMPARE(git.topLevel(), repo.root);
    QCOMPARE(git.branch(), u"main"_s);
    QVERIFY(git.hasCommits());
    QVERIFY(!git.detached());
    QVERIFY(git.upstream().isEmpty());
    QCOMPARE(git.headOid(), repo.text({u"rev-parse"_s, u"HEAD"_s}));
    QVERIFY(revision.count() > 0);

    // Every key the delegates require, on every row.
    const QStringList keys{u"path"_s,      u"oldPath"_s,   u"status"_s, u"letter"_s, u"staged"_s, u"unstaged"_s,
                           u"additions"_s, u"deletions"_s, u"binary"_s, u"name"_s,   u"dir"_s};
    for (const QVariant &v : git.changes()) {
        const QVariantMap m = v.toMap();
        for (const QString &k : keys)
            QVERIFY2(m.contains(k), qPrintable(k));
    }
    const QVariantMap m = rowOf(git.changes(), u"zmieniony.txt"_s);
    QCOMPARE(m.value(u"status"_s).toString(), u"modified"_s);
    QCOMPARE(m.value(u"letter"_s).toString(), u"M"_s);
    QCOMPARE(m.value(u"additions"_s).toInt(), 2);
    QCOMPARE(m.value(u"deletions"_s).toInt(), 1);
    QCOMPARE(m.value(u"staged"_s).toBool(), false);
    QCOMPARE(m.value(u"dir"_s).toString(), QString());
    const QVariantMap r = rowOf(git.changes(), u"nowa nazwa.txt"_s);
    QCOMPARE(r.value(u"status"_s).toString(), u"renamed"_s);
    QCOMPARE(r.value(u"letter"_s).toString(), u"R"_s);
    QCOMPARE(r.value(u"oldPath"_s).toString(), u"stara nazwa.txt"_s);
    QCOMPARE(r.value(u"staged"_s).toBool(), true);
    const QVariantMap d = rowOf(git.changes(), u"usunięty.txt"_s);
    QCOMPARE(d.value(u"status"_s).toString(), u"deleted"_s);
    QCOMPARE(d.value(u"letter"_s).toString(), u"D"_s);
    const QVariantMap u = rowOf(git.changes(), u"folder/głęboki/zażółć gęślą.md"_s);
    QCOMPARE(u.value(u"status"_s).toString(), u"untracked"_s);
    QCOMPARE(u.value(u"letter"_s).toString(), u"U"_s);
    QCOMPARE(u.value(u"name"_s).toString(), u"zażółć gęślą.md"_s);
    QCOMPARE(u.value(u"dir"_s).toString(), u"folder/głęboki"_s);
    QCOMPARE(u.value(u"additions"_s).toInt(), 1);

    // A refresh with nothing new changes nothing.
    const int before = git.revision();
    git.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!git.refreshing(), Wait);
    QCOMPARE(git.revision(), before);
}

void TestCodeGit::emptyRepository()
{
    Repo repo;
    repo.write(u"pierwszy.md"_s, "jeden\ndwa\n");
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.changeCount() == 1, Wait);
    QVERIFY(git.isRepo());
    QVERIFY(!git.hasCommits());
    QCOMPARE(git.branch(), u"main"_s);
    QSignalSpy diff(&git, &CodeGit::diffReady);
    git.requestDiff(u"pierwszy.md"_s, QString());
    QTRY_COMPARE_WITH_TIMEOUT(diff.count(), 1, Wait);
    QCOMPARE(diff.at(0).at(0).toString(), u"pierwszy.md"_s);
    QCOMPARE(diff.at(0).at(1).toString(), QString());
    QCOMPARE(diff.at(0).at(2).toString(), u"jeden\ndwa\n"_s);
}

void TestCodeGit::detachedHead()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.git({u"checkout"_s, u"-q"_s, u"--detach"_s});
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.isRepo(), Wait);
    QVERIFY(git.detached());
    QVERIFY(git.branch().isEmpty());
    QVERIFY(!git.headOid().isEmpty());
}

void TestCodeGit::watcherFollowsGit()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.write(u"a.txt"_s, "b\n");
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.changeCount() == 1, Wait);
    QCOMPARE(rowOf(git.changes(), u"a.txt"_s).value(u"staged"_s).toBool(), false);
    // git writes the index (renamed over the old one): refreshed without being asked.
    repo.git({u"add"_s, u"a.txt"_s});
    QTRY_VERIFY_WITH_TIMEOUT(rowOf(git.changes(), u"a.txt"_s).value(u"staged"_s).toBool(), Wait);
    // HEAD moves to another branch.
    repo.git({u"switch"_s, u"-q"_s, u"-c"_s, u"inna"_s});
    QTRY_COMPARE_WITH_TIMEOUT(git.branch(), u"inna"_s, Wait);
    // A commit empties the list (the index again: the watch survived the first replacement).
    repo.git({u"commit"_s, u"-q"_s, u"-m"_s, u"b"_s});
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 0, Wait);
    // A new file at the top: the top folder's entries are watched too.
    repo.write(u"nowy.txt"_s, "n\n");
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 1, Wait);
    // Settled, the refreshes' own commands write nothing the watcher sees: no refresh follows.
    QTRY_VERIFY_WITH_TIMEOUT(!git.refreshing(), Wait);
    QTest::qWait(500);
    QTRY_VERIFY_WITH_TIMEOUT(!git.refreshing(), Wait);
    QSignalSpy refreshes(&git, &CodeGit::refreshingChanged);
    QTest::qWait(1000);
    QCOMPARE(refreshes.count(), 0);
}

void TestCodeGit::watcherInWorktree()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    QTemporaryDir place;
    const QString wt = QFileInfo(place.path()).canonicalFilePath() + u"/drzewo ćma"_s;
    repo.git({u"worktree"_s, u"add"_s, u"-q"_s, u"-b"_s, u"wt"_s, wt});
    QVERIFY(QFileInfo(wt + u"/.git"_s).isFile());
    writeFile(wt + u"/sub/b.txt"_s, "b\n");
    CodeGit git;
    git.setFolder(wt);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.changeCount() == 1, Wait);
    QCOMPARE(git.branch(), u"wt"_s);
    QCOMPARE(git.topLevel(), wt);
    gitIn(wt, {u"add"_s, u"sub/b.txt"_s});
    QTRY_VERIFY_WITH_TIMEOUT(rowOf(git.changes(), u"sub/b.txt"_s).value(u"staged"_s).toBool(), Wait);
    QCOMPARE(rowOf(git.changes(), u"sub/b.txt"_s).value(u"status"_s).toString(), u"added"_s);
}

void TestCodeGit::poll()
{
    Repo repo;
    repo.write(u"sub/a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.isRepo(), Wait);
    git.setPollInterval(100);
    QCOMPARE(git.pollInterval(), 100);
    // A change inside a folder (no watch there, git not involved): the poll finds it.
    repo.write(u"sub/c.txt"_s, "c\n");
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 1, Wait);
    git.setPollInterval(0);
}

void TestCodeGit::diff()
{
    Repo repo;
    repo.write(u"tekst.md"_s, "Zażółć\ngęślą\n");
    repo.write(u"stara.txt"_s, "stara treść\nkilka linii\nżeby rozpoznać\n");
    repo.write(u"obraz.bin"_s, QByteArray("bin\0ary", 7));
    repo.commitAll(u"a"_s);
    repo.write(u"tekst.md"_s, "Zażółć\njaźń\n");
    repo.git({u"mv"_s, u"stara.txt"_s, u"nowa.txt"_s});
    repo.write(u"obraz.bin"_s, QByteArray("bin\0arx", 7));
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.changeCount() == 3, Wait);
    QSignalSpy ready(&git, &CodeGit::diffReady);
    QSignalSpy failed(&git, &CodeGit::diffFailed);

    git.requestDiff(u"tekst.md"_s, QString());
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, Wait);
    QCOMPARE(ready.at(0).at(1).toString(), u"Zażółć\ngęślą\n"_s);
    QCOMPARE(ready.at(0).at(2).toString(), u"Zażółć\njaźń\n"_s);
    QCOMPARE(ready.at(0).at(3).toBool(), false);

    git.requestDiff(u"nowa.txt"_s, QString());
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, Wait);
    QCOMPARE(ready.at(1).at(1).toString(), u"stara treść\nkilka linii\nżeby rozpoznać\n"_s);
    QCOMPARE(ready.at(1).at(1).toString(), ready.at(1).at(2).toString());

    git.requestDiff(u"obraz.bin"_s, QString());
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, Wait);
    QCOMPARE(ready.at(2).at(3).toBool(), true);

    git.requestDiff(u"../poza.txt"_s, QString());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, Wait);
    QCOMPARE(failed.at(0).at(0).toString(), u"../poza.txt"_s);
}

void TestCodeGit::commit()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.write(u"a.txt"_s, "b\n");
    repo.write(u"nowy ż.txt"_s, "n\n");
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.changeCount() == 2, Wait);
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.commit(u"Poprawka: zażółć gęślą jaźń\n\nOpis."_s, false, false, QString(), QString(), false);
    QVERIFY(git.busy());
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"commit"_s).isEmpty(), Wait);
    const QList<QVariant> done = finished(actions, u"commit"_s);
    QVERIFY2(done.at(1).toBool(), qPrintable(done.at(2).toString()));
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 0, Wait);
    QVERIFY(!git.busy());
    QCOMPARE(repo.text({u"log"_s, u"-1"_s, u"--format=%s"_s}), u"Poprawka: zażółć gęślą jaźń"_s);
    QVERIFY(git.lastError().isEmpty());

    // Nothing to commit: a failure, in lastError until an action succeeds.
    git.commit(u"Znowu"_s, false, false, QString(), QString(), false);
    QTRY_COMPARE_WITH_TIMEOUT(actions.count(), 2, Wait);
    QCOMPARE(actions.at(1).at(1).toBool(), false);
    QCOMPARE(git.lastError(), actions.at(1).at(2).toString());
}

void TestCodeGit::commitAndPush()
{
    QTemporaryDir remoteDir;
    const QString remote = QFileInfo(remoteDir.path()).canonicalFilePath() + u"/r.git"_s;
    gitIn(remoteDir.path(), {u"init"_s, u"-q"_s, u"--bare"_s, u"r.git"_s});
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.git({u"remote"_s, u"add"_s, u"origin"_s, remote});
    repo.write(u"b.txt"_s, "b\n");
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.changeCount() == 1, Wait);
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.commit(u"Druga"_s, true, false, QString(), QString(), false);
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"commit"_s).isEmpty(), Wait);
    QVERIFY2(finished(actions, u"commit"_s).at(1).toBool(),
             qPrintable(finished(actions, u"commit"_s).at(2).toString()));
    QTRY_COMPARE_WITH_TIMEOUT(git.upstream(), u"origin/main"_s, Wait);
    QCOMPARE(git.ahead(), 0);
    QCOMPARE(gitIn(remote, {u"log"_s, u"-1"_s, u"--format=%s"_s, u"main"_s}).trimmed(), QByteArray("Druga"));

    // One more commit made elsewhere: ahead 1, then push().
    repo.write(u"c.txt"_s, "c\n");
    repo.commitAll(u"c"_s);
    git.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(git.ahead(), 1, Wait);
    git.push();
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"push"_s).isEmpty(), Wait);
    QVERIFY(finished(actions, u"push"_s).at(1).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(git.ahead(), 0, Wait);
    git.pull();
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"pull"_s).isEmpty(), Wait);
    QVERIFY(finished(actions, u"pull"_s).at(1).toBool());
}

void TestCodeGit::commitPushAndPullRequest()
{
#ifndef FAKE_GH
    QSKIP("no fake gh in this build");
#else
    QTemporaryDir remoteDir;
    const QString remote = QFileInfo(remoteDir.path()).canonicalFilePath() + u"/r.git"_s;
    gitIn(remoteDir.path(), {u"init"_s, u"-q"_s, u"--bare"_s, u"r.git"_s});
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.git({u"remote"_s, u"add"_s, u"origin"_s, remote});
    repo.git({u"switch"_s, u"-q"_s, u"-c"_s, u"funkcja"_s});
    repo.write(u"b.txt"_s, "b\n");
    // A fake gh (tests/core/gitcli/fake_gh.cpp) writing its arguments outside the work tree.
    QTemporaryDir bin;
    const QString argsFile = bin.filePath(u"gh-args"_s);
    qputenv("FAKE_GH_ARGS", argsFile.toUtf8());
    qputenv("FAKE_GH_MODE", "ok");
    auto unset = qScopeGuard([] {
        qunsetenv("FAKE_GH_ARGS");
        qunsetenv("FAKE_GH_MODE");
    });

    CodeGit git;
    git.setGhProgram(QString::fromUtf8(FAKE_GH));
    QVERIFY(git.ghAvailable());
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.changeCount() == 1, Wait);
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.commit(u"Tytuł z wiadomości\n\nTreść"_s, true, true, QString(), u"Opis PR"_s, true);
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"commit"_s).isEmpty(), Wait);
    const QList<QVariant> done = finished(actions, u"commit"_s);
    QVERIFY2(done.at(1).toBool(), qPrintable(done.at(2).toString()));
    QCOMPARE(done.at(3).toString(), u"https://github.com/ja/repo/pull/17"_s);
    const QList<QByteArray> args = readFile(argsFile).split('\0');
    QCOMPARE(QString::fromUtf8(args.value(3)), u"Tytuł z wiadomości"_s); // --title: the first line
    QCOMPARE(QString::fromUtf8(args.value(5)), u"Opis PR"_s);
    QVERIFY(args.contains("--draft"));
    QVERIFY(args.contains("funkcja")); // --head
    QCOMPARE(gitIn(remote, {u"log"_s, u"-1"_s, u"--format=%s"_s, u"funkcja"_s}).trimmed(),
             QByteArray("Tytuł z wiadomości"));

    // Create PR… alone: nothing to push now, straight to gh.
    git.createPullRequest(u"Drugi"_s, QString(), false);
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"pullRequest"_s).isEmpty(), Wait);
    QVERIFY(finished(actions, u"pullRequest"_s).at(1).toBool());
    QCOMPARE(QString::fromUtf8(readFile(argsFile).split('\0').value(3)), u"Drugi"_s);
#endif
}

void TestCodeGit::branches()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.git({u"branch"_s, u"starsza"_s});
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.isRepo(), Wait);
    QSignalSpy ready(&git, &CodeGit::branchesReady);
    git.branches();
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, Wait);
    QCOMPARE(git.branchList().size(), 2);
    const QVariantMap first = git.branchList().at(0).toMap();
    QCOMPARE(first.value(u"name"_s).toString(), u"main"_s); // the current one first
    QCOMPARE(first.value(u"current"_s).toBool(), true);
    for (const QString &k : {u"name"_s, u"remote"_s, u"current"_s, u"upstream"_s, u"worktreePath"_s})
        QVERIFY(first.contains(k));

    QSignalSpy actions(&git, &CodeGit::actionFinished);
    QVERIFY(git.isValidBranchName(u"funkcja/żółw"_s));
    QVERIFY(!git.isValidBranchName(u"zła nazwa"_s));
    git.createBranch(u"funkcja/żółw"_s);
    QTRY_COMPARE_WITH_TIMEOUT(git.branch(), u"funkcja/żółw"_s, Wait);
    QVERIFY(finished(actions, u"createBranch"_s).at(1).toBool());
    git.checkout(u"starsza"_s);
    QTRY_COMPARE_WITH_TIMEOUT(git.branch(), u"starsza"_s, Wait);
    git.checkout(u"nie-ma-takiej"_s);
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"checkout"_s).isEmpty() && actions.count() == 3, Wait);
    QCOMPARE(actions.at(2).at(1).toBool(), false);
    QCOMPARE(git.branch(), u"starsza"_s);
}

void TestCodeGit::worktrees()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    QTemporaryDir place;
    const QString wt = QFileInfo(place.path()).canonicalFilePath() + u"/wątek 1"_s;
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.isRepo(), Wait);
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    QSignalSpy lists(&git, &CodeGit::worktreesReady);
    git.addWorktree(wt, u"watek/1"_s, QString());
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"addWorktree"_s).isEmpty(), Wait);
    const QList<QVariant> added = finished(actions, u"addWorktree"_s);
    QVERIFY2(added.at(1).toBool(), qPrintable(added.at(2).toString()));
    QCOMPARE(added.at(3).toString(), wt);
    QTRY_VERIFY_WITH_TIMEOUT(lists.count() >= 1 && git.worktreeList().size() == 2, Wait);
    QVERIFY(git.worktreeList().at(0).toMap().value(u"current"_s).toBool());
    QCOMPARE(git.worktreeList().at(1).toMap().value(u"branch"_s).toString(), u"watek/1"_s);
    QCOMPARE(git.worktreeList().at(1).toMap().value(u"current"_s).toBool(), false);

    git.removeWorktree(wt, false);
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"removeWorktree"_s).isEmpty(), Wait);
    QVERIFY(finished(actions, u"removeWorktree"_s).at(1).toBool());
    QVERIFY(!QFileInfo::exists(wt));
    QTRY_COMPARE_WITH_TIMEOUT(git.worktreeList().size(), 1, Wait);
}

void TestCodeGit::turnCheckpoint()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.write(u"usuwany.txt"_s, "u\n");
    repo.commitAll(u"a"_s);
    repo.write(u"przed.txt"_s, "był przed turą\n");
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.isRepo(), Wait);

    QSignalSpy made(&git, &CodeGit::checkpointMade);
    const QString id = git.checkpoint();
    QVERIFY(!id.isEmpty());
    QVERIFY(git.busy());
    QTRY_COMPARE_WITH_TIMEOUT(made.count(), 1, Wait);
    QCOMPARE(made.at(0).at(0).toString(), id);
    QVERIFY2(made.at(0).at(1).toBool(), qPrintable(made.at(0).at(2).toString()));
    QTRY_VERIFY_WITH_TIMEOUT(!git.busy(), Wait);

    git.setTurnCheckpoint(id);
    QCOMPARE(git.turnCheckpoint(), id);
    QTRY_VERIFY_WITH_TIMEOUT(git.turnLoaded(), Wait);
    QVERIFY(git.turnChanges().isEmpty());

    // The turn.
    repo.write(u"a.txt"_s, "a\nagent\n");
    QFile::remove(repo.path(u"usuwany.txt"_s));
    repo.write(u"nowy/zagnieżdżony/plik ś.txt"_s, "n\n");
    git.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(git.turnChanges().size(), 3, Wait);
    QCOMPARE(rowOf(git.turnChanges(), u"a.txt"_s).value(u"additions"_s).toInt(), 1);
    QCOMPARE(rowOf(git.turnChanges(), u"usuwany.txt"_s).value(u"status"_s).toString(), u"deleted"_s);
    QCOMPARE(rowOf(git.turnChanges(), u"nowy/zagnieżdżony/plik ś.txt"_s).value(u"status"_s).toString(), u"added"_s);
    // "All changes" has the file from before the turn too (a refresh the watcher started before the
    // writes may have read the status first: the next one, which refresh() asked for, has them).
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 4, Wait);

    QSignalSpy since(&git, &CodeGit::changesSinceReady);
    git.changesSince(id);
    QTRY_COMPARE_WITH_TIMEOUT(since.count(), 1, Wait);
    QCOMPARE(since.at(0).at(0).toString(), id);
    QCOMPARE(since.at(0).at(1).toList().size(), 3);

    // The turn's diff: against the checkpoint.
    QSignalSpy diff(&git, &CodeGit::diffReady);
    git.requestDiff(u"a.txt"_s, id);
    QTRY_COMPARE_WITH_TIMEOUT(diff.count(), 1, Wait);
    QCOMPARE(diff.at(0).at(1).toString(), u"a\n"_s);
    QCOMPARE(diff.at(0).at(2).toString(), u"a\nagent\n"_s);

    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.restore(id);
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"restore"_s).isEmpty(), Wait);
    QVERIFY2(finished(actions, u"restore"_s).at(1).toBool(),
             qPrintable(finished(actions, u"restore"_s).at(2).toString()));
    QCOMPARE(repo.read(u"a.txt"_s), QByteArray("a\n"));
    QCOMPARE(repo.read(u"usuwany.txt"_s), QByteArray("u\n"));
    QVERIFY(!QFileInfo::exists(repo.path(u"nowy"_s)));
    QCOMPARE(repo.read(u"przed.txt"_s), QByteArray("był przed turą\n"));
    QTRY_VERIFY_WITH_TIMEOUT(git.turnChanges().isEmpty() && !git.refreshing(), Wait);
    QCOMPARE(git.changeCount(), 1);

    git.dropCheckpoints({id});
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"dropCheckpoints"_s).isEmpty(), Wait);
    QVERIFY(finished(actions, u"dropCheckpoints"_s).at(1).toBool());
    QCOMPARE(repo.text({u"for-each-ref"_s, u"refs/brulion/"_s}), QString());
    QVERIFY(git.turnError().isEmpty());
    // The turn's checkpoint is gone: said, not shown as "no changes".
    git.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!git.turnError().isEmpty(), Wait);
    QVERIFY(git.turnLoaded());
    QVERIFY(git.turnChanges().isEmpty());
}

void TestCodeGit::actionsInOrder()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.isRepo(), Wait);
    // A checkpoint, a change, a restore: asked at once, run one after another.
    QStringList order;
    connect(&git, &CodeGit::checkpointMade, this,
            [&order](const QString &, bool ok) { order.append(ok ? u"checkpoint"_s : u"checkpoint failed"_s); });
    connect(&git, &CodeGit::actionFinished, this,
            [&order](const QString &action, bool ok) { order.append(ok ? action : action + u" failed"_s); });
    QSignalSpy busy(&git, &CodeGit::busyChanged);
    const QString id = git.checkpoint();
    QCOMPARE(git.busyAction(), u"checkpoint"_s);
    git.restore(id);
    git.createBranch(u"po"_s);
    QVERIFY(git.busy());
    QTRY_COMPARE_WITH_TIMEOUT(order.size(), 3, Wait);
    QCOMPARE(order, (QStringList{u"checkpoint"_s, u"restore"_s, u"createBranch"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(!git.busy(), Wait);
    QVERIFY(git.busyAction().isEmpty());
    QVERIFY(busy.count() >= 2);
}

void TestCodeGit::folderChangeDropsTheQueue()
{
    Repo a;
    a.write(u"a.txt"_s, "a\n");
    a.commitAll(u"a"_s);
    Repo b;
    b.write(u"b.txt"_s, "b\n");
    b.commitAll(u"b"_s);
    CodeGit git;
    git.setFolder(a.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.isRepo(), Wait);
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    QSignalSpy made(&git, &CodeGit::checkpointMade);
    git.createBranch(u"pierwsza"_s);          // runs
    const QString waiting = git.checkpoint(); // waits
    git.pull();                               // waits
    git.setFolder(b.root);
    QCOMPARE(made.count(), 1);
    QCOMPARE(made.at(0).at(0).toString(), waiting);
    QCOMPARE(made.at(0).at(1).toBool(), false);
    QVERIFY(!finished(actions, u"pull"_s).isEmpty());
    QCOMPARE(finished(actions, u"pull"_s).at(1).toBool(), false);
    // The one running ends where it ran.
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"createBranch"_s).isEmpty(), Wait);
    QTRY_VERIFY_WITH_TIMEOUT(!git.busy() && git.loaded(), Wait);
    QTRY_COMPARE_WITH_TIMEOUT(git.topLevel(), b.root, Wait);
    QCOMPARE(git.branch(), u"main"_s);
    QCOMPARE(b.text({u"for-each-ref"_s, u"refs/brulion/"_s}), QString());
}

void TestCodeGit::tools()
{
    CodeGit git;
    QSignalSpy tools(&git, &CodeGit::toolsChanged);
    QVERIFY(git.gitAvailable());
    git.setGhProgram(QString());
    QVERIFY(!git.ghAvailable());
    git.setGitProgram(QString());
    QVERIFY(!git.gitAvailable());
    QCOMPARE(tools.count(), 2);
    QCOMPARE(git.subjectOf(u"\n  Tytuł  \nreszta"_s), u"Tytuł"_s);
    QCOMPARE(CodeGit::maxRows(), CodeGit::MaxRows);
}

QTEST_GUILESS_MAIN(TestCodeGit)
#include "tst_codegit.moc"
