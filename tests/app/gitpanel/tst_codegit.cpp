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
namespace gitcli = brulion::gitcli;

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
    void patchForTheAi();
    void questionsForTheAi();
    void pullRequestQuestionAgainstTheRemote();
    void pullRequestQuestionRefused();
    void commit();
    void commitAndPush();
    void commitPushAndPullRequest();
    void branches();
    void worktrees();
    void turnCheckpoint();
    void actionsInOrder();
    void folderChangeDropsTheQueue();
    void tools();

    void compareSources();
    void commitList();
    void filesStagedAndThrownAway();
    void hunksFromThePanel();
    void commitChosenFiles();
    void pullRequestBadgeAndFork();
    void quickAction();
    void initAndPublish();
    void mergeInProgress();
    void stashes();
    void revertAnOlderTurn();

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
    qputenv("XDG_DATA_HOME", m_home.filePath(u"xdg-data"_s).toUtf8());
    QDir().mkpath(m_home.filePath(u"xdg-data"_s));
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

    // The changes since a checkpoint there: answered, with nothing.
    QSignalSpy since(&git, &CodeGit::changesSinceReady);
    git.changesSince(u"0123456789abcdef0123456789abcdef01234567"_s);
    QTRY_COMPARE_WITH_TIMEOUT(since.count(), 1, Wait);
    QCOMPARE(since.at(0).at(0).toString(), u"0123456789abcdef0123456789abcdef01234567"_s);
    QVERIFY(since.at(0).at(1).toList().isEmpty());

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

    // A refresh with nothing new changes nothing. (One refresh first: git's first status after files
    // were written in the same second may rewrite the index's stat data — racy git — and Windows'
    // coarser timestamps make that likely.)
    git.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!git.refreshing(), Wait);
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

// The checked files' changes as text for the AI's commit message (requestPatch): the tracked ones'
// diff against HEAD, a new file's first lines, nothing of a file not asked for.
void TestCodeGit::patchForTheAi()
{
    Repo repo;
    repo.write(u"calc.py"_s, "def add(a, b):\n    return a - b\n");
    repo.write(u"inny.txt"_s, "bez zmian\n");
    repo.commitAll(u"a"_s);
    repo.write(u"calc.py"_s, "def add(a, b):\n    return a + b\n");
    repo.write(u"inny.txt"_s, "zmieniony, nie zaznaczony\n");
    repo.write(u"żółw.md"_s, "# Żółw\nnowy plik\n");
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.changeCount() == 3, Wait);
    QSignalSpy ready(&git, &CodeGit::patchReady);
    git.requestPatch({u"calc.py"_s, u"żółw.md"_s});
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, Wait);
    const QString text = ready.at(0).at(0).toString();
    QVERIFY2(text.contains(u"-    return a - b"_s) && text.contains(u"+    return a + b"_s), qPrintable(text));
    QVERIFY2(text.contains(u"New file żółw.md:\n# Żółw\nnowy plik\n"_s), qPrintable(text));
    QVERIFY(!text.contains(u"inny.txt"_s));
    git.requestPatch({});
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, Wait);
    QCOMPARE(ready.at(1).at(0).toString(), QString());
}

// The questions for the AI's texts (codeai): a pull request's — the branch's commits (their bodies
// too), stat and diff against the base (the default branch, or one chosen), never what the base has
// alone; a base that is none or looks like an option, and a branch with nothing of its own, say why
// —, its answer read into title and body; the commit message's — the branch and the changes.
void TestCodeGit::questionsForTheAi()
{
    Repo repo;
    repo.write(u"koszyk.py"_s, "RABAT = 0\n");
    repo.commitAll(u"Początek"_s);
    repo.git({u"switch"_s, u"-q"_s, u"-c"_s, u"rabat"_s});
    repo.write(u"koszyk.py"_s, "RABAT = 0.05  # zniżka\n");
    repo.git({u"add"_s, u"-A"_s});
    repo.git({u"commit"_s, u"-q"_s, u"-m"_s, u"Dodaj rabat dla stałych klientów"_s, u"-m"_s,
              u"Rabat 5 % od trzeciego zamówienia."_s});
    repo.git({u"switch"_s, u"-q"_s, u"main"_s});
    repo.write(u"inne.txt"_s, "tylko na main\n");
    repo.commitAll(u"Tylko na main"_s);
    repo.git({u"switch"_s, u"-q"_s, u"rabat"_s});

    CodeGit git;
    git.setFolder(repo.root);
    QTRY_COMPARE_WITH_TIMEOUT(git.defaultBranch(), u"main"_s, Wait);
    QTRY_COMPARE_WITH_TIMEOUT(git.branch(), u"rabat"_s, Wait);
    QSignalSpy ready(&git, &CodeGit::pullRequestQuestionReady);
    git.requestPullRequestQuestion(QString());
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, Wait);
    QCOMPARE(ready.at(0).at(2).toString(), QString());
    QVERIFY(ready.at(0).at(0).toString().contains(u"pull request"_s));
    const QString prompt = ready.at(0).at(1).toString();
    QVERIFY2(prompt.contains(u"Base branch: main\nHead branch: rabat\n"_s), qPrintable(prompt));
    QVERIFY(prompt.contains(u"Dodaj rabat dla stałych klientów"_s));
    QVERIFY(prompt.contains(u"Rabat 5 % od trzeciego zamówienia."_s));
    QVERIFY(prompt.contains(u"koszyk.py | "_s));
    QVERIFY(prompt.contains(u"+RABAT = 0.05  # zniżka"_s));
    QVERIFY(!prompt.contains(u"Tylko na main"_s) && !prompt.contains(u"inne.txt"_s));
    // A base chosen by its name.
    git.requestPullRequestQuestion(u"main"_s);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, Wait);
    QVERIFY(ready.at(1).at(1).toString().contains(u"Base branch: main\n"_s));
    // A base that is none, one that looks like an option: said, nothing asked.
    git.requestPullRequestQuestion(u"nie-ma-takiej"_s);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, Wait);
    QCOMPARE(ready.at(2).at(1).toString(), QString());
    QVERIFY(ready.at(2).at(2).toString().contains(u"nie-ma-takiej"_s));
    git.requestPullRequestQuestion(u"--output=/tmp/wynik"_s);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 4, Wait);
    QCOMPARE(ready.at(3).at(1).toString(), QString());
    QCOMPARE(ready.at(3).at(2).toString(), u"“--output=/tmp/wynik” cannot be the name of a branch."_s);
    // The default branch itself has nothing of its own: said.
    repo.git({u"switch"_s, u"-q"_s, u"main"_s});
    git.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(git.branch(), u"main"_s, Wait);
    git.requestPullRequestQuestion(QString());
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 5, Wait);
    QCOMPARE(ready.at(4).at(1).toString(), QString());
    QVERIFY(ready.at(4).at(2).toString().contains(u"main"_s));

    // The answer read.
    const QVariantMap text = git.pullRequestText(u"```\nDodaj rabat.\n\n## Summary\n- 5 %\n```"_s);
    QCOMPARE(text.value(u"title"_s).toString(), u"Dodaj rabat"_s);
    QCOMPARE(text.value(u"body"_s).toString(), u"## Summary\n- 5 %"_s);
    QCOMPARE(git.pullRequestText(QString()).value(u"title"_s).toString(), QString());
    // The commit message's question.
    const QVariantMap commit = git.commitQuestion(u"diff --git a/koszyk.py b/koszyk.py\n"_s);
    QVERIFY(commit.value(u"system"_s).toString().contains(u"commit message"_s));
    QVERIFY(commit.value(u"prompt"_s).toString().startsWith(u"Branch: main\n"_s));
    QVERIFY(commit.value(u"prompt"_s).toString().endsWith(u"diff --git a/koszyk.py b/koszyk.py\n"_s));
}

// The base as the remote has it: the default branch's remote first for a name chosen (what the pull
// request goes into), a branch only the remote has; answers for a folder left meanwhile are dropped.
void TestCodeGit::pullRequestQuestionAgainstTheRemote()
{
    QTemporaryDir remoteDir;
    const QString remote = QFileInfo(remoteDir.path()).canonicalFilePath() + u"/r.git"_s;
    gitIn(remoteDir.path(), {u"init"_s, u"-q"_s, u"--bare"_s, u"r.git"_s});
    Repo repo;
    repo.write(u"koszyk.py"_s, "RABAT = 0\n");
    repo.commitAll(u"Początek"_s);
    repo.git({u"remote"_s, u"add"_s, u"origin"_s, remote});
    repo.git({u"push"_s, u"-q"_s, u"-u"_s, u"origin"_s, u"main"_s, u"main:wydanie"_s});
    repo.git({u"remote"_s, u"set-head"_s, u"origin"_s, u"main"_s});
    repo.git({u"switch"_s, u"-q"_s, u"-c"_s, u"rabat"_s});
    repo.write(u"koszyk.py"_s, "RABAT = 0.05\n");
    repo.commitAll(u"Dodaj rabat"_s);
    // (The local main falls behind the remote's: what the pull request goes into is the remote's.)
    repo.git({u"branch"_s, u"-f"_s, u"main"_s, u"HEAD"_s});
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_COMPARE_WITH_TIMEOUT(git.defaultBranchRef(), u"origin/main"_s, Wait);
    QTRY_COMPARE_WITH_TIMEOUT(git.branch(), u"rabat"_s, Wait);
    QSignalSpy ready(&git, &CodeGit::pullRequestQuestionReady);
    const auto ask = [&](const QString &base) {
        const qsizetype before = ready.count();
        git.requestPullRequestQuestion(base);
        return QTest::qWaitFor([&] { return ready.count() > before; }, Wait) ? ready.last() : QList<QVariant>();
    };
    for (const QString &base : {QString(), u"main"_s, u"origin/main"_s}) {
        const QList<QVariant> answer = ask(base);
        QVERIFY2(!answer.isEmpty() && answer.at(1).toString().contains(u"Base branch: origin/main\n"_s), qPrintable(base));
        QVERIFY(answer.at(1).toString().contains(u"Dodaj rabat"_s));
    }
    const QList<QVariant> remoteOnly = ask(u"wydanie"_s);
    QVERIFY(!remoteOnly.isEmpty() && remoteOnly.at(1).toString().contains(u"Base branch: origin/wydanie\n"_s));

    // The folder left while git answers (the repository known, then a new one still being read):
    // nothing comes for the one left.
    Repo other;
    other.write(u"a.txt"_s, "a\n");
    other.commitAll(u"a"_s);
    const qsizetype before = ready.count();
    git.requestPullRequestQuestion(QString());
    git.setFolder(other.root);
    git.requestPullRequestQuestion(QString());
    git.setFolder(repo.root);
    QTest::qWait(1500);
    QCOMPARE(ready.count(), before);
}

// What is refused, each saying why: no base branch known, a detached HEAD, git's own failure.
void TestCodeGit::pullRequestQuestionRefused()
{
    Repo repo;
    repo.git({u"switch"_s, u"-q"_s, u"-c"_s, u"funkcja"_s}); // (no main, master or trunk: no default)
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.write(u"a.txt"_s, "b\n");
    repo.commitAll(u"b"_s);
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_COMPARE_WITH_TIMEOUT(git.branch(), u"funkcja"_s, Wait);
    QSignalSpy ready(&git, &CodeGit::pullRequestQuestionReady);
    git.requestPullRequestQuestion(QString());
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, Wait);
    QCOMPARE(ready.at(0).at(2).toString(), u"There is no base branch to compare with: choose one."_s);
    repo.git({u"switch"_s, u"-q"_s, u"--detach"_s, u"HEAD~1"_s});
    git.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(git.detached(), Wait);
    git.requestPullRequestQuestion(u"funkcja"_s);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, Wait);
    QCOMPARE(ready.at(1).at(2).toString(), u"A pull request's text is written for a branch with commits."_s);
#ifdef Q_OS_UNIX
    // git failing to list the branch's commits: its message.
    repo.git({u"switch"_s, u"-q"_s, u"funkcja"_s});
    QTemporaryDir bin;
    const QString fake = bin.filePath(u"git"_s);
    writeFile(fake, "#!/bin/sh\nfor a in \"$@\"; do [ \"$a\" = log ] && { echo 'fatal: zepsuty log' >&2; exit 128; }; done\nexec '"
                            + gitProgram().toUtf8() + "' \"$@\"\n");
    QFile::setPermissions(fake, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    git.setGitProgram(fake);
    git.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(git.branch(), u"funkcja"_s, Wait);
    git.requestPullRequestQuestion(u"HEAD~1"_s);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, Wait);
    QVERIFY2(ready.at(2).at(2).toString().contains(u"zepsuty log"_s), qPrintable(ready.at(2).at(2).toString()));
#endif
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

    // A refresh (the watcher's, refresh()'s) keeps the turn's rows current and answers no request:
    // changesSinceReady is changesSince()'s answer alone (on Windows the watcher's late refreshes
    // answered a request two more times).
    QSignalSpy since(&git, &CodeGit::changesSinceReady);
    git.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(git.refreshing(), Wait);
    QTRY_VERIFY_WITH_TIMEOUT(!git.refreshing(), Wait);
    QCOMPARE(since.count(), 0);
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

namespace {

#ifdef Q_OS_UNIX
/// A fake gh in `dir` running `body` (shell lines); its arguments of every run appended to
/// `<dir>/gh-log`, one run a line, NUL-separated.
QString fakeGh(const QTemporaryDir &dir, const QByteArray &body)
{
    const QString script = dir.filePath(u"gh"_s);
    writeFile(script, "#!/bin/sh\nfor a in \"$@\"; do printf '%s\\0' \"$a\"; done >> \"" + dir.filePath(u"gh-log"_s).toUtf8()
                              + "\"\necho >> \"" + dir.filePath(u"gh-log"_s).toUtf8() + "\"\n" + body);
    QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return script;
}

/// The runs of the fake gh whose arguments begin with `a b` (each a list of arguments).
QList<QStringList> ghRuns(const QTemporaryDir &dir, const QString &a, const QString &b)
{
    QList<QStringList> out;
    for (const QByteArray &line : readFile(dir.filePath(u"gh-log"_s)).split('\n')) {
        QStringList args;
        for (const QByteArray &arg : line.split('\0'))
            args.append(QString::fromUtf8(arg));
        if (!args.isEmpty() && args.last().isEmpty())
            args.removeLast();
        if (args.size() >= 2 && args.at(0) == a && args.at(1) == b)
            out.append(args);
    }
    return out;
}
#endif

/// The files of a row list, by path.
QStringList pathsOf(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &v : rows)
        out.append(v.toMap().value(u"path"_s).toString());
    return out;
}

} // namespace

void TestCodeGit::compareSources()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"baza"_s);
    const QString base = repo.text({u"rev-parse"_s, u"HEAD"_s});
    repo.git({u"switch"_s, u"-q"_s, u"-c"_s, u"funkcja"_s});
    repo.write(u"b.txt"_s, "b\n");
    repo.commitAll(u"b"_s);
    const QString second = repo.text({u"rev-parse"_s, u"HEAD"_s});
    repo.git({u"switch"_s, u"-q"_s, u"main"_s});
    repo.write(u"m.txt"_s, "m\n");
    repo.commitAll(u"m na main"_s);
    repo.git({u"switch"_s, u"-q"_s, u"funkcja"_s});
    repo.write(u"c ż.txt"_s, "c\n");

    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded() && git.isRepo(), Wait);
    QTRY_COMPARE_WITH_TIMEOUT(git.defaultBranch(), u"main"_s, Wait);
    QCOMPARE(git.defaultBranchRef(), u"main"_s);
    QVERIFY(!git.onDefaultBranch());
    // The branch against main: the commit they share → the work tree (main's own commit not in it).
    git.compare(true, u"main"_s, QString(), true);
    QTRY_VERIFY_WITH_TIMEOUT(git.compareLoaded(), Wait);
    QCOMPARE(git.compareError(), QString());
    QCOMPARE(git.comparisonBase(), base);
    QStringList paths = pathsOf(git.compareChanges());
    paths.sort();
    QCOMPARE(paths, (QStringList{u"b.txt"_s, u"c ż.txt"_s}));
    // A commit's own changes; the root commit's against the empty tree.
    git.compare(true, base, second, false);
    QVERIFY(!git.compareLoaded());
    QTRY_VERIFY_WITH_TIMEOUT(git.compareLoaded(), Wait);
    QCOMPARE(pathsOf(git.compareChanges()), QStringList{u"b.txt"_s});
    git.compare(true, QString(), base, false);
    QTRY_VERIFY_WITH_TIMEOUT(git.compareLoaded(), Wait);
    QCOMPARE(pathsOf(git.compareChanges()), QStringList{u"a.txt"_s});
    QCOMPARE(git.compareChanges().at(0).toMap().value(u"status"_s).toString(), u"added"_s);
    // A file's texts between two revisions, and against the work tree.
    QSignalSpy diffs(&git, &CodeGit::diffReady);
    git.requestDiffBetween(u"b.txt"_s, QString(), base, second);
    QTRY_COMPARE_WITH_TIMEOUT(diffs.count(), 1, Wait);
    QCOMPARE(diffs.at(0).at(1).toString(), QString());
    QCOMPARE(diffs.at(0).at(2).toString(), u"b\n"_s);
    git.requestDiffBetween(u"c ż.txt"_s, QString(), second, QString());
    QTRY_COMPARE_WITH_TIMEOUT(diffs.count(), 2, Wait);
    QCOMPARE(diffs.at(1).at(2).toString(), u"c\n"_s);
    // The work tree changes: the comparison follows with the refresh.
    repo.write(u"d.txt"_s, "d\n");
    git.compare(true, u"main"_s, QString(), true);
    git.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(pathsOf(git.compareChanges()).contains(u"d.txt"_s), Wait);
    // A base that is no branch says so; off: nothing.
    git.compare(true, u"nie-ma"_s, QString(), true);
    QTRY_VERIFY_WITH_TIMEOUT(git.compareLoaded(), Wait);
    QVERIFY(!git.compareError().isEmpty());
    git.compare(false, QString(), QString(), false);
    QVERIFY(!git.compareActive());
    QVERIFY(git.compareChanges().isEmpty());
}

void TestCodeGit::commitList()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"Pierwszy"_s);
    repo.write(u"a.txt"_s, "b\n");
    repo.commitAll(u"Drugi: żółw"_s);
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded(), Wait);
    git.commits();
    QTRY_COMPARE_WITH_TIMEOUT(git.commitList().size(), 2, Wait);
    const QVariantMap first = git.commitList().at(0).toMap();
    QCOMPARE(first.value(u"subject"_s).toString(), u"Drugi: żółw"_s);
    QCOMPARE(first.value(u"author"_s).toString(), u"Testowy Użytkownik"_s);
    QCOMPARE(first.value(u"shortOid"_s).toString().size(), 7);
    QCOMPARE(first.value(u"parentOid"_s).toString(), git.commitList().at(1).toMap().value(u"oid"_s).toString());
    QCOMPARE(git.commitList().at(1).toMap().value(u"parentOid"_s).toString(), QString());
    QVERIFY(!first.value(u"when"_s).toString().isEmpty());
}

void TestCodeGit::filesStagedAndThrownAway()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.write(u"a.txt"_s, "zmiana\n");
    repo.write(u"nowy.txt"_s, "n\n");
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 2, Wait);
    QVERIFY(git.stagedPaths().isEmpty());
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.stage({u"a.txt"_s});
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"stage"_s).isEmpty(), Wait);
    QVERIFY(finished(actions, u"stage"_s).at(1).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(git.stagedPaths(), QStringList{u"a.txt"_s}, Wait);
    QVERIFY(rowOf(git.changes(), u"a.txt"_s).value(u"staged"_s).toBool());
    git.unstage({u"a.txt"_s});
    QTRY_VERIFY_WITH_TIMEOUT(git.stagedPaths().isEmpty(), Wait);
    git.discard(u"a.txt"_s, QString());
    git.discard(u"nowy.txt"_s, QString());
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 0, Wait);
    QCOMPARE(repo.read(u"a.txt"_s), QByteArray("a\n"));
    QVERIFY(!QFileInfo::exists(repo.path(u"nowy.txt"_s)));
    // The rows carry the tree's keys too.
    repo.write(u"x/y.txt"_s, "y\n");
    git.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 1, Wait);
    const QVariantMap row = git.changes().at(0).toMap();
    for (const QString &k : {u"kind"_s, u"depth"_s, u"collapsed"_s, u"fileCount"_s})
        QVERIFY2(row.contains(k), qPrintable(k));
    const QVariantList tree = git.treeRows(git.changes(), {});
    QCOMPARE(tree.size(), 2);
    QCOMPARE(tree.at(0).toMap().value(u"kind"_s).toString(), u"folder"_s);
}

void TestCodeGit::hunksFromThePanel()
{
    Repo repo;
    QByteArray lines;
    for (int i = 1; i <= 20; ++i)
        lines += QByteArray::number(i) + '\n';
    repo.write(u"plik.txt"_s, lines);
    repo.commitAll(u"a"_s);
    QByteArray changed = lines;
    changed.replace("\n2\n", "\nDWA\n");
    changed.replace("\n18\n", "\nOSIEMNAŚCIE\n");
    repo.write(u"plik.txt"_s, changed);
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 1, Wait);
    // The panel's rows of the diff: two hunks, each knowing the lines it changes.
    const QVariantList rows = git.diffRows(QString::fromUtf8(lines), QString::fromUtf8(changed), false, false, 3);
    QVariantList hunks;
    for (const QVariant &v : rows) {
        if (v.toMap().value(u"kind"_s).toString() == u"hunk"_s)
            hunks.append(v);
    }
    QCOMPARE(hunks.size(), 2);
    const QVariantMap second = hunks.at(1).toMap();
    QCOMPARE(second.value(u"newFirst"_s).toInt(), 18);
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.stageHunk(u"plik.txt"_s, second.value(u"newFirst"_s).toInt(), second.value(u"newLast"_s).toInt());
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"stageHunk"_s).isEmpty(), Wait);
    QVERIFY2(finished(actions, u"stageHunk"_s).at(1).toBool(), qPrintable(finished(actions, u"stageHunk"_s).at(2).toString()));
    QVERIFY(repo.git({u"show"_s, u":plik.txt"_s}).contains("OSIEMNAŚCIE"));
    QVERIFY(!repo.git({u"show"_s, u":plik.txt"_s}).contains("DWA"));
    git.unstageHunk(u"plik.txt"_s, second.value(u"oldFirst"_s).toInt(), second.value(u"oldLast"_s).toInt());
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"unstageHunk"_s).isEmpty(), Wait);
    QVERIFY(finished(actions, u"unstageHunk"_s).at(1).toBool());
    QVERIFY(!repo.git({u"show"_s, u":plik.txt"_s}).contains("OSIEMNAŚCIE"));
    const QVariantMap first = hunks.at(0).toMap();
    git.discardHunk(u"plik.txt"_s, first.value(u"newFirst"_s).toInt(), first.value(u"newLast"_s).toInt());
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"discardHunk"_s).isEmpty(), Wait);
    QVERIFY(finished(actions, u"discardHunk"_s).at(1).toBool());
    QVERIFY(!repo.read(u"plik.txt"_s).contains("DWA"));
    QVERIFY(repo.read(u"plik.txt"_s).contains("OSIEMNAŚCIE"));
}

void TestCodeGit::commitChosenFiles()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.write(u"b.txt"_s, "b\n");
    repo.commitAll(u"start"_s);
    repo.write(u"a.txt"_s, "A\n");
    repo.write(u"b.txt"_s, "B\n");
    repo.write(u"nowy.txt"_s, "n\n");
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 3, Wait);
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    // Two of three, one of them new: only they go in.
    git.commitFiles(u"Wybrane"_s, {u"a.txt"_s, u"nowy.txt"_s}, QString(), false, false, QString(), QString(), false, QString());
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"commit"_s).isEmpty(), Wait);
    QVERIFY2(finished(actions, u"commit"_s).at(1).toBool(), qPrintable(finished(actions, u"commit"_s).at(2).toString()));
    QCOMPARE(repo.text({u"show"_s, u"--name-only"_s, u"--format="_s, u"HEAD"_s}), u"a.txt\nnowy.txt"_s);
    QTRY_COMPARE_WITH_TIMEOUT(git.changePaths(), QStringList{u"b.txt"_s}, Wait);
    // Staged ones only: the index as it is; on a new branch.
    repo.write(u"b.txt"_s, "B staged\n");
    repo.git({u"add"_s, u"b.txt"_s});
    repo.write(u"b.txt"_s, "B staged + more\n");
    repo.write(u"inny.txt"_s, "i\n");
    git.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(git.stagedPaths(), QStringList{u"b.txt"_s}, Wait);
    actions.clear();
    const QString branch = git.branchNameFor(u"Napraw synchronizację: żółw!\n\nszczegóły"_s);
    QCOMPARE(branch, u"napraw-synchronizację-żółw"_s);
    git.commitFiles(u"Tylko staged"_s, git.stagedPaths(), branch, false, false, QString(), QString(), false, QString());
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"commit"_s).isEmpty(), Wait);
    QVERIFY2(finished(actions, u"commit"_s).at(1).toBool(), qPrintable(finished(actions, u"commit"_s).at(2).toString()));
    QCOMPARE(repo.text({u"show"_s, u"HEAD:b.txt"_s}), u"B staged"_s);
    QCOMPARE(repo.read(u"b.txt"_s), QByteArray("B staged + more\n"));
    QTRY_COMPARE_WITH_TIMEOUT(git.branch(), branch, Wait);
    // Conflict markers refuse the commit.
    repo.write(u"inny.txt"_s, "<<<<<<< HEAD\nx\n=======\ny\n>>>>>>> b\n");
    git.refresh();
    actions.clear();
    git.commitFiles(u"Z konfliktem"_s, {u"inny.txt"_s}, QString(), false, false, QString(), QString(), false, QString());
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"commit"_s).isEmpty(), Wait);
    QVERIFY(!finished(actions, u"commit"_s).at(1).toBool());
    QVERIFY(finished(actions, u"commit"_s).at(2).toString().contains(u"inny.txt"_s));
    actions.clear();
    git.commitFiles(u"Nic"_s, {}, QString(), false, false, QString(), QString(), false, QString());
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"commit"_s).isEmpty(), Wait);
    QVERIFY(!finished(actions, u"commit"_s).at(1).toBool());
}

void TestCodeGit::pullRequestBadgeAndFork()
{
#ifdef Q_OS_UNIX
    QTemporaryDir remoteDir;
    const QString remote = QFileInfo(remoteDir.path()).canonicalFilePath() + u"/r.git"_s;
    gitIn(remoteDir.path(), {u"init"_s, u"-q"_s, u"--bare"_s, u"r.git"_s});
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    // A fork: origin is ja's copy (pushed to the bare one), upstream the organisation's.
    repo.git({u"remote"_s, u"add"_s, u"origin"_s, remote});
    repo.git({u"config"_s, u"remote.origin.pushurl"_s, remote});
    repo.git({u"remote"_s, u"set-url"_s, u"origin"_s, u"git@github.com:ja/repo.git"_s});
    repo.git({u"config"_s, u"remote.origin.pushurl"_s, remote});
    repo.git({u"remote"_s, u"add"_s, u"upstream"_s, u"https://github.com/org/repo.git"_s});
    repo.git({u"switch"_s, u"-q"_s, u"-c"_s, u"funkcja"_s});
    repo.write(u"b.txt"_s, "b\n");
    repo.commitAll(u"b"_s);
    QTemporaryDir bin;
    const QString gh = fakeGh(bin, "if [ \"$2\" = view ]; then\n"
                                   "  echo '{\"number\":12,\"url\":\"https://github.com/org/repo/pull/12\",\"state\":\"OPEN\","
                                   "\"isDraft\":false,\"title\":\"Funkcja\",\"baseRefName\":\"main\"}'\n"
                                   "  exit 0\nfi\necho https://github.com/org/repo/pull/12\n");
    CodeGit git;
    git.setGhProgram(gh);
    QSignalSpy prChanged(&git, &CodeGit::pullRequestChanged);
    git.setFolder(repo.root);
    QTRY_COMPARE_WITH_TIMEOUT(git.pullRequest().value(u"number"_s).toInt(), 12, Wait);
    QCOMPARE(git.pullRequest().value(u"state"_s).toString(), u"open"_s);
    QCOMPARE(git.pullRequest().value(u"url"_s).toString(), u"https://github.com/org/repo/pull/12"_s);
    // Asked for the fork's branch in the organisation's repository.
    const QList<QStringList> views = ghRuns(bin, u"pr"_s, u"view"_s);
    QVERIFY(!views.isEmpty());
    QVERIFY2(views.last().contains(u"ja:funkcja"_s), qPrintable(views.last().join(u' ')));
    QCOMPARE(views.last().at(views.last().indexOf(u"--repo"_s) + 1), u"org/repo"_s);
    QTRY_VERIFY_WITH_TIMEOUT(git.hasRemote(), Wait);
    QCOMPARE(git.primaryAction(), u"push"_s); // (not pushed yet)
    QVERIFY(!git.unavailableReason(u"createPr"_s).isEmpty()); // (it has one)
    // A pull request into another base, from the fork: pushed first, then --repo and --head.
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.createPullRequestTo(u"Tytuł"_s, u"Opis"_s, false, u"rozwój"_s);
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"pullRequest"_s).isEmpty(), Wait);
    QVERIFY2(finished(actions, u"pullRequest"_s).at(1).toBool(), qPrintable(finished(actions, u"pullRequest"_s).at(2).toString()));
    const QList<QStringList> creates = ghRuns(bin, u"pr"_s, u"create"_s);
    QCOMPARE(creates.size(), 1);
    const QStringList args = creates.at(0);
    QCOMPARE(args.at(args.indexOf(u"--base"_s) + 1), u"rozwój"_s);
    QCOMPARE(args.at(args.indexOf(u"--head"_s) + 1), u"ja:funkcja"_s);
    QCOMPARE(args.at(args.indexOf(u"--repo"_s) + 1), u"org/repo"_s);
    QCOMPARE(gitIn(remote, {u"log"_s, u"-1"_s, u"--format=%s"_s, u"funkcja"_s}).trimmed(), QByteArray("b"));
    QTRY_COMPARE_WITH_TIMEOUT(git.primaryAction(), u"openPr"_s, Wait);
    // gh failing (offline): what was known stays.
    fakeGh(bin, "echo 'error connecting to api.github.com' >&2\nexit 1\n");
    git.refreshPullRequest();
    QTest::qWait(300);
    QCOMPARE(git.pullRequest().value(u"number"_s).toInt(), 12);
#else
    QSKIP("a fake gh as a shell script");
#endif
}

void TestCodeGit::quickAction()
{
    QTemporaryDir remoteDir;
    const QString remote = QFileInfo(remoteDir.path()).canonicalFilePath() + u"/r.git"_s;
    gitIn(remoteDir.path(), {u"init"_s, u"-q"_s, u"--bare"_s, u"r.git"_s});
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    CodeGit git;
    git.setGhProgram(QString());
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded(), Wait);
    // No remote and no gh: nothing to do; publishing needs gh.
    QCOMPARE(git.primaryAction(), QString());
    QVERIFY(!git.unavailableReason(u"publish"_s).isEmpty());
    QVERIFY(!git.unavailableReason(u"push"_s).isEmpty());
    QVERIFY(!git.unavailableReason(u"commit"_s).isEmpty());
    repo.write(u"a.txt"_s, "b\n");
    git.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(git.primaryAction(), u"commit"_s, Wait);
    QCOMPARE(git.unavailableReason(u"commit"_s), QString());
    repo.commitAll(u"b"_s);
    repo.git({u"remote"_s, u"add"_s, u"origin"_s, remote});
    git.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(git.primaryAction(), u"push"_s, Wait); // (no upstream)
    QVERIFY(git.onDefaultBranch());
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.push();
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"push"_s).isEmpty(), Wait);
    QTRY_COMPARE_WITH_TIMEOUT(git.primaryAction(), QString(), Wait); // (pushed; no gh)
    QVERIFY(!git.unavailableReason(u"push"_s).isEmpty()); // everything is pushed
    QCOMPARE(git.unavailableReason(u"pull"_s), QString());
    QVERIFY(!git.unavailableReason(u"createPr"_s).isEmpty());
    // Someone else pushed: pull.
    QTemporaryDir otherDir;
    gitIn(otherDir.path(), {u"clone"_s, u"-q"_s, remote, u"inny"_s});
    const QString other = otherDir.filePath(u"inny"_s);
    gitIn(other, {u"config"_s, u"user.name"_s, u"Ktoś"_s});
    gitIn(other, {u"config"_s, u"user.email"_s, u"k@example.com"_s});
    writeFile(other + u"/c.txt"_s, "c\n");
    gitIn(other, {u"add"_s, u"-A"_s});
    gitIn(other, {u"commit"_s, u"-q"_s, u"-m"_s, u"c"_s});
    gitIn(other, {u"push"_s, u"-q"_s});
    repo.git({u"fetch"_s, u"-q"_s});
    git.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(git.primaryAction(), u"pull"_s, Wait);
    QVERIFY(git.unavailableReason(u"push"_s).contains(u"pull"_s));
}

void TestCodeGit::initAndPublish()
{
    QTemporaryDir plain;
    const QString folder = QFileInfo(plain.path()).canonicalFilePath();
    writeFile(folder + u"/notatka.md"_s, "# Notatka\n");
    CodeGit git;
    git.setFolder(folder);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded(), Wait);
    QVERIFY(!git.isRepo());
    QCOMPARE(git.primaryAction(), u"init"_s);
    QCOMPARE(git.unavailableReason(u"init"_s), QString());
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.initRepository();
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"init"_s).isEmpty(), Wait);
    QVERIFY2(finished(actions, u"init"_s).at(1).toBool(), qPrintable(finished(actions, u"init"_s).at(2).toString()));
    QTRY_VERIFY_WITH_TIMEOUT(git.isRepo(), Wait);
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 1, Wait);
    QVERIFY(!git.unavailableReason(u"init"_s).isEmpty());
#ifdef Q_OS_UNIX
    gitIn(folder, {u"config"_s, u"user.name"_s, u"T"_s});
    gitIn(folder, {u"config"_s, u"user.email"_s, u"t@example.com"_s});
    gitIn(folder, {u"add"_s, u"-A"_s});
    gitIn(folder, {u"commit"_s, u"-q"_s, u"-m"_s, u"pierwszy"_s});
    QTemporaryDir bin;
    git.setGhProgram(fakeGh(bin, "echo https://github.com/ja/notatki\n"));
    git.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(git.primaryAction(), u"publish"_s, Wait);
    QVERIFY(git.isValidRepositoryName(u"notatki"_s));
    QVERIFY(!git.isValidRepositoryName(u"moje notatki"_s));
    git.publish(u"notatki"_s, false);
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"publish"_s).isEmpty(), Wait);
    const QList<QVariant> done = finished(actions, u"publish"_s);
    QVERIFY2(done.at(1).toBool(), qPrintable(done.at(2).toString()));
    QCOMPARE(done.at(3).toString(), u"https://github.com/ja/notatki"_s);
    const QList<QStringList> runs = ghRuns(bin, u"repo"_s, u"create"_s);
    QCOMPARE(runs.size(), 1);
    QVERIFY(runs.at(0).contains(u"--private"_s));
    QCOMPARE(runs.at(0).at(runs.at(0).indexOf(u"--source"_s) + 1), folder);
#endif
}

void TestCodeGit::mergeInProgress()
{
    Repo repo;
    repo.write(u"wspólny.txt"_s, "jeden\ndwa\n");
    repo.commitAll(u"baza"_s);
    repo.git({u"switch"_s, u"-q"_s, u"-c"_s, u"inna"_s});
    repo.write(u"wspólny.txt"_s, "jeden\nDWA z innej\n");
    repo.commitAll(u"inna"_s);
    repo.git({u"switch"_s, u"-q"_s, u"main"_s});
    repo.write(u"wspólny.txt"_s, "jeden\nDWA z main\n");
    repo.commitAll(u"main"_s);
    QProcess merge;
    merge.start(gitProgram(), {u"-C"_s, repo.root, u"merge"_s, u"-q"_s, u"inna"_s});
    merge.waitForFinished(Wait);
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_COMPARE_WITH_TIMEOUT(git.operation(), u"merge"_s, Wait);
    QCOMPARE(git.conflictCount(), 1);
    QCOMPARE(rowOf(git.changes(), u"wspólny.txt"_s).value(u"status"_s).toString(), u"conflicted"_s);
    QCOMPARE(git.primaryAction(), QString());
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.resolve(u"wspólny.txt"_s, u"theirs"_s);
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"resolve"_s).isEmpty(), Wait);
    QVERIFY(finished(actions, u"resolve"_s).at(1).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(git.conflictCount(), 0, Wait);
    git.continueOperation();
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"continue"_s).isEmpty(), Wait);
    QVERIFY2(finished(actions, u"continue"_s).at(1).toBool(), qPrintable(finished(actions, u"continue"_s).at(2).toString()));
    QTRY_COMPARE_WITH_TIMEOUT(git.operation(), QString(), Wait);
    QCOMPARE(repo.read(u"wspólny.txt"_s), QByteArray("jeden\nDWA z innej\n"));
    // Another one, aborted.
    repo.git({u"switch"_s, u"-q"_s, u"-c"_s, u"trzecia"_s, u"HEAD~1"_s});
    repo.write(u"wspólny.txt"_s, "jeden\nTRZY\n");
    repo.commitAll(u"trzecia"_s);
    QProcess again;
    again.start(gitProgram(), {u"-C"_s, repo.root, u"merge"_s, u"-q"_s, u"main"_s});
    again.waitForFinished(Wait);
    git.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(git.operation(), u"merge"_s, Wait);
    git.abortOperation();
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"abort"_s).isEmpty(), Wait);
    QTRY_COMPARE_WITH_TIMEOUT(git.operation(), QString(), Wait);
    QCOMPARE(repo.read(u"wspólny.txt"_s), QByteArray("jeden\nTRZY\n"));
}

void TestCodeGit::stashes()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.write(u"a.txt"_s, "zmiana\n");
    repo.write(u"nowy.txt"_s, "n\n");
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 2, Wait);
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.stashPush(u"Na później — ąę"_s);
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"stash"_s).isEmpty(), Wait);
    QVERIFY(finished(actions, u"stash"_s).at(1).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(git.stashList().size(), 1, Wait);
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 0, Wait);
    const QVariantMap stash = git.stashList().at(0).toMap();
    QCOMPARE(stash.value(u"message"_s).toString(), u"On main: Na później — ąę"_s);
    QCOMPARE(stash.value(u"branch"_s).toString(), u"main"_s);
    git.stashApply(stash.value(u"oid"_s).toString());
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 2, Wait);
    QCOMPARE(git.stashList().size(), 1);
    git.discard(u"a.txt"_s, QString());
    git.discard(u"nowy.txt"_s, QString());
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 0, Wait);
    git.stashPop(stash.value(u"oid"_s).toString());
    QTRY_COMPARE_WITH_TIMEOUT(git.changeCount(), 2, Wait);
    QTRY_VERIFY_WITH_TIMEOUT(git.stashList().isEmpty(), Wait);
    git.stashPush(QString());
    QTRY_COMPARE_WITH_TIMEOUT(git.stashList().size(), 1, Wait);
    git.stashDrop(git.stashList().at(0).toMap().value(u"oid"_s).toString());
    QTRY_VERIFY_WITH_TIMEOUT(git.stashList().isEmpty(), Wait);
    QCOMPARE(git.changeCount(), 0);
}

void TestCodeGit::revertAnOlderTurn()
{
    Repo repo;
    repo.write(u"a.txt"_s, "1\n2\n3\n4\n5\n6\n7\n8\n9\n");
    repo.commitAll(u"a"_s);
    CodeGit git;
    git.setFolder(repo.root);
    QTRY_VERIFY_WITH_TIMEOUT(git.loaded(), Wait);
    QSignalSpy made(&git, &CodeGit::checkpointMade);
    const QString one = git.checkpoint();
    QTRY_COMPARE_WITH_TIMEOUT(made.count(), 1, Wait);
    repo.write(u"a.txt"_s, "1\nDWA\n3\n4\n5\n6\n7\n8\n9\n");
    const QString two = git.checkpoint();
    QTRY_COMPARE_WITH_TIMEOUT(made.count(), 2, Wait);
    repo.write(u"a.txt"_s, "1\nDWA\n3\n4\n5\n6\n7\n8\nDZIEWIĘĆ\n");
    // Turn 1's own changes: checkpoint 1 → checkpoint 2.
    git.compare(true, gitcli::Client::checkpointRef(one), gitcli::Client::checkpointRef(two), false);
    QTRY_VERIFY_WITH_TIMEOUT(git.compareLoaded(), Wait);
    QCOMPARE(pathsOf(git.compareChanges()), QStringList{u"a.txt"_s});
    QCOMPARE(git.compareChanges().at(0).toMap().value(u"additions"_s).toInt(), 1);
    QSignalSpy actions(&git, &CodeGit::actionFinished);
    git.revertBetween(gitcli::Client::checkpointRef(one), gitcli::Client::checkpointRef(two));
    QTRY_VERIFY_WITH_TIMEOUT(!finished(actions, u"revertTurn"_s).isEmpty(), Wait);
    QVERIFY2(finished(actions, u"revertTurn"_s).at(1).toBool(), qPrintable(finished(actions, u"revertTurn"_s).at(2).toString()));
    QCOMPARE(repo.read(u"a.txt"_s), QByteArray("1\n2\n3\n4\n5\n6\n7\n8\nDZIEWIĘĆ\n"));
}

QTEST_GUILESS_MAIN(TestCodeGit)
#include "tst_codegit.moc"
