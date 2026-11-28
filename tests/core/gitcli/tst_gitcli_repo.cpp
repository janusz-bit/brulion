// gitcli::Client with real git in temporary repositories (skipped where git is not on PATH):
// a folder that is no repository, a deleted folder, a repository without commits, a detached
// HEAD; status with lines added / deleted (untracked files counted, the user's index never
// written); a file's two texts (modified, new, deleted, renamed, binary, too large); commit,
// push to and pull from a bare remote; branches; worktrees; pull requests through a fake `gh`
// (fake_gh.cpp: arguments never through a shell, Windows' command line included); checkpoints — the index, HEAD and stash untouched, the
// changes since one, the work tree put back (modified, added, deleted, renamed files, Polish
// letters and spaces in names, a nested new folder, a file where a folder was and the other way
// round, modes and symbolic links, ignored files left alone, files ignored again by the restored
// .gitignore kept), the refs dropped; time limits and no callback after the client is gone.
#include "gitcli/client.h"

#include "testtiming.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <optional>

using namespace Qt::StringLiterals;
using namespace brulion::gitcli;

namespace {

/// What `call` hands its callback, waited for (an empty T after kRoomyMs).
template<typename T, typename Call>
T await(Call &&call)
{
    std::optional<T> out;
    call([&out](const T &value) { out = value; });
    QDeadlineTimer deadline(brulion::test::kRoomyMs);
    while (!out && !deadline.hasExpired())
        QTest::qWait(2);
    if (!out) {
        qWarning("no answer in time");
        return T();
    }
    return *out;
}

QString gitProgram()
{
    return QStandardPaths::findExecutable(u"git"_s);
}

/// A folder (a repository unless told otherwise) with synchronous git and file helpers.
class Repo {
public:
    explicit Repo(bool init = true)
    {
        root = QFileInfo(dir.path()).canonicalFilePath();
        if (init) {
            run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
            run({u"config"_s, u"user.name"_s, u"Testowy Użytkownik"_s});
            run({u"config"_s, u"user.email"_s, u"test@example.com"_s});
        }
    }

    /// `git -C root <args>`'s standard output (`code`: its exit code).
    QByteArray run(const QStringList &args, const QByteArray &input = {}, int *code = nullptr) const
    {
        return runIn(root, args, input, code);
    }
    static QByteArray runIn(const QString &folder, const QStringList &args, const QByteArray &input = {},
                            int *code = nullptr)
    {
        QProcess p;
        p.setProgram(gitProgram());
        p.setArguments(QStringList{u"-C"_s, folder} + args);
        p.start();
        if (!input.isEmpty())
            p.write(input);
        p.closeWriteChannel();
        p.waitForFinished(brulion::test::kRoomyMs);
        if (code)
            *code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
        else if (p.exitCode() != 0)
            qWarning() << "git" << args << "failed:" << p.readAllStandardError();
        return p.readAllStandardOutput();
    }
    QString text(const QStringList &args) const { return QString::fromUtf8(run(args)).trimmed(); }

    QString path(const QString &rel) const { return root + u'/' + rel; }
    void write(const QString &rel, const QByteArray &content) const
    {
        const QString full = path(rel);
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile f(full);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            qWarning() << "cannot write" << full;
        f.write(content);
    }
    QByteArray read(const QString &rel) const
    {
        QFile f(path(rel));
        if (!f.open(QIODevice::ReadOnly))
            return "<missing>";
        return f.readAll();
    }
    bool exists(const QString &rel) const
    {
        return QFileInfo::exists(path(rel)) || QFileInfo(path(rel)).isSymbolicLink();
    }
    void remove(const QString &rel) const { QFile::remove(path(rel)); }
    void commitAll(const QString &message) const
    {
        run({u"add"_s, u"-A"_s});
        run({u"commit"_s, u"-q"_s, u"-m"_s, message});
    }
    QByteArray indexBytes() const
    {
        QFile f(root + u"/.git/index"_s);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<no index>");
    }

    QTemporaryDir dir;
    QString root;
};

const FileChange *find(const QList<FileChange> &changes, const QString &path)
{
    for (const FileChange &c : changes) {
        if (c.path == path)
            return &c;
    }
    return nullptr;
}

#ifdef Q_OS_UNIX
/// A fake gh in `dir`: writes its arguments NUL-separated to `<dir>/gh-args` and answers with
/// `script` (shell lines: what it prints and its exit code).
QString fakeGh(const QTemporaryDir &dir, const QByteArray &script)
{
    const QString argsFile = dir.filePath(u"gh-args"_s);
    const QString path = dir.filePath(u"gh"_s);
    QFile s(path);
    if (!s.open(QIODevice::WriteOnly | QIODevice::Truncate))
        qWarning() << "cannot write" << path;
    s.write("#!/bin/sh\nfor a in \"$@\"; do printf '%s\\0' \"$a\"; done > \"" + argsFile.toUtf8() + "\"\n" + script);
    s.close();
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}

QStringList ghArgs(const QTemporaryDir &dir)
{
    QFile a(dir.filePath(u"gh-args"_s));
    if (!a.open(QIODevice::ReadOnly))
        return {};
    QStringList out;
    for (const QByteArray &arg : a.readAll().split('\0'))
        out.append(QString::fromUtf8(arg));
    if (!out.isEmpty() && out.last().isEmpty())
        out.removeLast();
    return out;
}
#endif

/// A repository whose branch "inna" and main changed the same line of "wspólny.txt": `git merge
/// inna` stops on the conflict (also "usunięty.txt": changed on main, deleted on inna).
void makeConflict(Repo &repo)
{
    repo.write(u"wspólny.txt"_s, "jeden\ndwa\ntrzy\n");
    repo.write(u"usunięty.txt"_s, "stary\n");
    repo.commitAll(u"baza"_s);
    repo.run({u"switch"_s, u"-q"_s, u"-c"_s, u"inna"_s});
    repo.write(u"wspólny.txt"_s, "jeden\nDWA z innej\ntrzy\n");
    repo.run({u"rm"_s, u"-q"_s, u"usunięty.txt"_s});
    repo.commitAll(u"inna zmiana"_s);
    repo.run({u"switch"_s, u"-q"_s, u"main"_s});
    repo.write(u"wspólny.txt"_s, "jeden\nDWA z main\ntrzy\n");
    repo.write(u"usunięty.txt"_s, "zmieniony\n");
    repo.commitAll(u"main zmiana"_s);
}

} // namespace

class TestGitCliRepo : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void notARepository();
    void deletedFolder();
    void repositoryFolders();
    void emptyRepository();
    void statusOfChanges();
    void statusLeavesTheIndex();
    void detachedHead();
    void fileTexts();
    void commit();
    void pushAndPull();
    void branches();
    void worktrees();
    void pullRequestThroughGh();
    void pullRequestWithoutGh();

    void checkpointLeavesIndexHeadAndStash();
    void changesSinceCheckpoint();
    void restoreCheckpoint();
    void restoreKeepsFilesIgnoredAgain();
    void restoreAfterTheTurnCommitted();
    void restoreInEmptyRepository();
    void checkpointInWorktree();
    void dropCheckpoints();
    void badCheckpointIds();

    void noGit();
    void timeLimit();
    void noCallbackAfterDestruction();

    // Cloning
    void cloneFromThisComputer();
    void cloneIntoAnEmptyFolder();
    void cloneRefusesAFullFolder();
    void cloneFailureLeavesNothing();
    void cloneStopped();
    void cloneSaysWhySigningInFailed();
    void repositoryListThroughGh();

    // History, comparisons, remotes, gh
    void logOfCommits();
    void changesBetweenRevisions();
    void fileTextsBetweenRevisions();
    void mergeBaseAndDefaultBranch();
    void forkPullRequestTarget();
    void pullRequestView();
    void initAndPublish();
    void revertAnOlderTurn();

    // Files, hunks, commits
    void stageAndUnstage();
    void stageInEmptyRepository();
    void discardFiles();
    void discardRefused();
    void hunks();
    void singleLines();
    void hunkOfARangeReverted();
    void commitChosenFiles();
    void commitStagedOnly();
    void commitOnNewBranch();
    void commitRefusesConflictMarkers();

    // Merges, rebases, stashes
    void mergeConflictResolved();
    void mergeAborted();
    void takeTheirsOfADeletion();
    void rebaseContinued();
    void rebaseSkipped();
    void cherryPickAborted();
    void stashes();

private:
    QTemporaryDir m_home;
};

void TestGitCliRepo::initTestCase()
{
    if (gitProgram().isEmpty())
        QSKIP("git is not on PATH");
    QVERIFY(m_home.isValid());
    // The user's own git configuration (hooks, signing, templates) stays out of the tests.
    const QString global = m_home.filePath(u"gitconfig"_s);
    QFile config(global);
    QVERIFY(config.open(QIODevice::WriteOnly));
    config.write("[init]\n\tdefaultBranch = main\n[advice]\n\tdetachedHead = false\n");
    config.close();
    qputenv("GIT_CONFIG_GLOBAL", global.toUtf8());
    qputenv("GIT_CONFIG_NOSYSTEM", "1");
    qputenv("HOME", m_home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", m_home.filePath(u"xdg"_s).toUtf8());
    // (discard() moves files to the trash: this home's.)
    qputenv("XDG_DATA_HOME", m_home.filePath(u"xdg-data"_s).toUtf8());
    QDir().mkpath(m_home.filePath(u"xdg-data"_s)); // (Qt trashes only into a data folder that exists)
    // No repository above the temporary folders counts (a test's "no repository" folder).
    qputenv("GIT_CEILING_DIRECTORIES",
            (QDir::tempPath() + QDir::listSeparator() + QFileInfo(QDir::tempPath()).canonicalFilePath()).toUtf8());
}

void TestGitCliRepo::notARepository()
{
    Repo folder(false);
    folder.write(u"notatka.md"_s, "# Zażółć\n");
    Client client;
    const RepoInfo info = await<RepoInfo>([&](auto done) { client.repository(folder.root, done); });
    QVERIFY(!info.isRepo);
    QVERIFY2(info.error.contains(u"not a git repository"_s), qPrintable(info.error));
    const StatusReport status = await<StatusReport>([&](auto done) { client.status(folder.root, done); });
    QVERIFY(!status.ok);
    QVERIFY(!status.error.isEmpty());
    const Outcome cp = await<Outcome>([&](auto done) { client.checkpoint(folder.root, u"a1"_s, done); });
    QVERIFY(!cp.ok);
}

void TestGitCliRepo::deletedFolder()
{
    QString gone;
    {
        Repo repo;
        gone = repo.root;
    }
    QVERIFY(!QFileInfo::exists(gone));
    Client client;
    const RepoInfo info = await<RepoInfo>([&](auto done) { client.repository(gone, done); });
    QVERIFY(!info.isRepo);
    QVERIFY(!info.error.isEmpty());
    const StatusReport status = await<StatusReport>([&](auto done) { client.status(gone, done); });
    QVERIFY(!status.ok);
    const Outcome commit = await<Outcome>([&](auto done) { client.commitAll(gone, u"x"_s, done); });
    QVERIFY(!commit.ok);
}

void TestGitCliRepo::repositoryFolders()
{
    Repo repo;
    repo.write(u"katalog/plik.txt"_s, "a\n");
    Client client;
    const RepoInfo info = await<RepoInfo>([&](auto done) { client.repository(repo.path(u"katalog"_s), done); });
    QVERIFY2(info.isRepo, qPrintable(info.error));
    QCOMPARE(info.topLevel, repo.root);
    QCOMPARE(QFileInfo(info.gitDir).canonicalFilePath(), QFileInfo(repo.root + u"/.git"_s).canonicalFilePath());
    QCOMPARE(QFileInfo(info.commonDir).canonicalFilePath(), QFileInfo(repo.root + u"/.git"_s).canonicalFilePath());
    QVERIFY(!info.bare);

    QTemporaryDir bareDir;
    Repo::runIn(bareDir.path(), {u"init"_s, u"-q"_s, u"--bare"_s, u"goły.git"_s});
    const RepoInfo bare = await<RepoInfo>([&](auto done) { client.repository(bareDir.filePath(u"goły.git"_s), done); });
    QVERIFY(!bare.isRepo);
    QVERIFY(bare.bare);
}

void TestGitCliRepo::emptyRepository()
{
    Repo repo;
    repo.write(u"pierwszy plik.md"_s, "jeden\ndwa\ntrzy\n");
    repo.write(u"staged.txt"_s, "x\n");
    repo.run({u"add"_s, u"staged.txt"_s});
    Client client;
    const StatusReport r = await<StatusReport>([&](auto done) { client.status(repo.root, done); });
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(r.status.branch.initial);
    QCOMPARE(r.status.branch.head, u"main"_s);
    QVERIFY(r.status.branch.oid.isEmpty());
    QCOMPARE(r.changes.size(), 2);
    const FileChange *untracked = find(r.changes, u"pierwszy plik.md"_s);
    QVERIFY(untracked);
    QCOMPARE(untracked->change, Change::Untracked);
    QCOMPARE(untracked->additions, 3);
    QCOMPARE(untracked->deletions, 0);
    const FileChange *staged = find(r.changes, u"staged.txt"_s);
    QVERIFY(staged);
    QCOMPARE(staged->change, Change::Added);
    QVERIFY(staged->staged);
    QCOMPARE(staged->additions, 1);

    const FileTexts t = await<FileTexts>(
            [&](auto done) { client.fileTexts(repo.root, {u"pierwszy plik.md"_s, QString(), u"HEAD"_s, QString()}, done); });
    QVERIFY2(t.ok, qPrintable(t.error));
    QVERIFY(!t.oldExists);
    QVERIFY(t.oldText.isEmpty());
    QCOMPARE(t.newText, u"jeden\ndwa\ntrzy\n"_s);
}

void TestGitCliRepo::statusOfChanges()
{
    Repo repo;
    repo.write(u"zmieniony.txt"_s, "a\nb\nc\n");
    repo.write(u"usunięty.txt"_s, "1\n2\n");
    repo.write(u"stara nazwa.txt"_s, "ten sam tekst\nkilka linii\nżeby git rozpoznał zmianę nazwy\n");
    repo.write(u"obraz.bin"_s, QByteArray("\x89PNG\0\0\0", 7));
    repo.commitAll(u"Początek"_s);

    repo.write(u"zmieniony.txt"_s, "a\nB\nc\nd\n");
    repo.remove(u"usunięty.txt"_s);
    repo.run({u"mv"_s, u"stara nazwa.txt"_s, u"nowa nazwa ż.txt"_s});
    repo.write(u"nowy/folder/zażółć gęślą.txt"_s, "jaźń\n");
    repo.write(u"obraz.bin"_s, QByteArray("\x89PNG\0\1\0", 7));
#ifndef Q_OS_WIN
    repo.write(u"z\nnową linią.txt"_s, "x\n"); // (no newline in a Windows file name)
#endif

    Client client;
    const StatusReport r = await<StatusReport>([&](auto done) { client.status(repo.root, done); });
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.status.branch.head, u"main"_s);
    QVERIFY(!r.status.branch.initial);
    QCOMPARE(r.status.branch.oid, repo.text({u"rev-parse"_s, u"HEAD"_s}));

    const FileChange *m = find(r.changes, u"zmieniony.txt"_s);
    QVERIFY(m);
    QCOMPARE(m->change, Change::Modified);
    QVERIFY(m->unstaged);
    QVERIFY(!m->staged);
    QCOMPARE(m->additions, 2);
    QCOMPARE(m->deletions, 1);

    const FileChange *d = find(r.changes, u"usunięty.txt"_s);
    QVERIFY(d);
    QCOMPARE(d->change, Change::Deleted);
    QCOMPARE(d->deletions, 2);

    const FileChange *ren = find(r.changes, u"nowa nazwa ż.txt"_s);
    QVERIFY(ren);
    QCOMPARE(ren->change, Change::Renamed);
    QCOMPARE(ren->oldPath, u"stara nazwa.txt"_s);
    QVERIFY(ren->staged);
    QCOMPARE(ren->additions, 0);
    QCOMPARE(ren->deletions, 0);

    const FileChange *u = find(r.changes, u"nowy/folder/zażółć gęślą.txt"_s);
    QVERIFY(u);
    QCOMPARE(u->change, Change::Untracked);
    QCOMPARE(u->additions, 1);

#ifndef Q_OS_WIN
    const FileChange *nl = find(r.changes, u"z\nnową linią.txt"_s);
    QVERIFY(nl);
    QCOMPARE(nl->additions, 1);
    QCOMPARE(r.changes.size(), 6);
#else
    QCOMPARE(r.changes.size(), 5);
#endif

    const FileChange *bin = find(r.changes, u"obraz.bin"_s);
    QVERIFY(bin);
    QVERIFY(bin->binary);
    QCOMPARE(bin->additions, -1);
}

void TestGitCliRepo::statusLeavesTheIndex()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.write(u"a.txt"_s, "b\n");
    repo.write(u"nowy.txt"_s, "n\n");
    // The index's stat information is stale now: a status that refreshes it would write it.
    const QByteArray before = repo.indexBytes();
    Client client;
    for (int i = 0; i < 3; ++i) {
        const StatusReport r = await<StatusReport>([&](auto done) { client.status(repo.root, done); });
        QVERIFY(r.ok);
    }
    QCOMPARE(repo.indexBytes(), before);
    QVERIFY(!QFileInfo::exists(repo.root + u"/.git/index.lock"_s));
    // The intent-to-add entries went into a copy: nowy.txt is still untracked in the real index.
    QCOMPARE(repo.text({u"ls-files"_s}), u"a.txt"_s);
}

void TestGitCliRepo::detachedHead()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.run({u"checkout"_s, u"-q"_s, u"--detach"_s});
    Client client;
    const StatusReport r = await<StatusReport>([&](auto done) { client.status(repo.root, done); });
    QVERIFY(r.ok);
    QVERIFY(r.status.branch.detached);
    QVERIFY(r.status.branch.head.isEmpty());
    const Outcome push = await<Outcome>([&](auto done) { client.push(repo.root, done); });
    QVERIFY(!push.ok);
    QVERIFY2(push.message.contains(u"detached"_s), qPrintable(push.message));
}

void TestGitCliRepo::fileTexts()
{
    Repo repo;
    repo.write(u"tekst.md"_s, "Zażółć\ngęślą\n");
    repo.write(u"do usunięcia.txt"_s, "pa\n");
    repo.write(u"stara.txt"_s, "stara treść\n");
    repo.write(u"obraz.bin"_s, QByteArray("bin\0ary", 7));
    repo.commitAll(u"a"_s);
    repo.write(u"tekst.md"_s, "Zażółć\njaźń\n");
    repo.remove(u"do usunięcia.txt"_s);
    repo.run({u"mv"_s, u"stara.txt"_s, u"nowa.txt"_s});
    repo.write(u"nowa.txt"_s, "nowa treść\n");
    repo.write(u"duży.txt"_s, QByteArray(Client::MaxTextBytes + 10, 'a'));

    Client client;
    const auto texts = [&](const QString &path, const QString &oldPath) {
        return await<FileTexts>([&](auto done) { client.fileTexts(repo.root, {path, oldPath, u"HEAD"_s, QString()}, done); });
    };
    const FileTexts mod = texts(u"tekst.md"_s, QString());
    QVERIFY2(mod.ok, qPrintable(mod.error));
    QCOMPARE(mod.oldText, u"Zażółć\ngęślą\n"_s);
    QCOMPARE(mod.newText, u"Zażółć\njaźń\n"_s);
    QVERIFY(mod.oldExists && mod.newExists);
    QVERIFY(!mod.binary);

    const FileTexts del = texts(u"do usunięcia.txt"_s, QString());
    QVERIFY(del.ok);
    QCOMPARE(del.oldText, u"pa\n"_s);
    QVERIFY(del.newText.isEmpty());
    QVERIFY(!del.newExists);

    const FileTexts ren = texts(u"nowa.txt"_s, u"stara.txt"_s);
    QVERIFY(ren.ok);
    QCOMPARE(ren.oldText, u"stara treść\n"_s);
    QCOMPARE(ren.newText, u"nowa treść\n"_s);

    const FileTexts bin = texts(u"obraz.bin"_s, QString());
    QVERIFY(bin.ok);
    QVERIFY(bin.binary);
    QVERIFY(bin.oldText.isEmpty());

    const FileTexts big = texts(u"duży.txt"_s, QString());
    QVERIFY(big.ok);
    QVERIFY(big.tooLarge);
    QVERIFY(big.newText.isEmpty());

    // Paths that leave the work tree are refused before git runs.
    const FileTexts outside = texts(u"../poza.txt"_s, QString());
    QVERIFY(!outside.ok);
    const FileTexts dotgit = texts(u".git/config"_s, QString());
    QVERIFY(!dotgit.ok);

    // No base: everything is new.
    const FileTexts none = await<FileTexts>(
            [&](auto done) { client.fileTexts(repo.root, {u"tekst.md"_s, QString(), QString(), QString()}, done); });
    QVERIFY(none.ok);
    QVERIFY(none.oldText.isEmpty());
    QCOMPARE(none.newText, u"Zażółć\njaźń\n"_s);
}

void TestGitCliRepo::commit()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.write(u"a.txt"_s, "b\n");
    repo.write(u"nowy plik ą.txt"_s, "n\n");
    Client client;
    const QString message = u"Poprawka: zażółć gęślą jaźń\n\nDłuższy opis — \"cudzysłów\", $(echo nie) i `ls`.\n"_s;
    const Outcome o = await<Outcome>([&](auto done) { client.commitAll(repo.root, message, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(o.value, repo.text({u"rev-parse"_s, u"HEAD"_s}));
    QVERIFY(o.message.contains(o.value.left(7)));
    QCOMPARE(QString::fromUtf8(repo.run({u"log"_s, u"-1"_s, u"--format=%B"_s})).trimmed(), message.trimmed());
    QCOMPARE(repo.text({u"status"_s, u"--porcelain"_s}), QString());

    const Outcome nothing = await<Outcome>([&](auto done) { client.commitAll(repo.root, u"Znowu"_s, done); });
    QVERIFY(!nothing.ok);
    QCOMPARE(nothing.message, u"Nothing to commit"_s);

    repo.write(u"a.txt"_s, "c\n");
    const Outcome empty = await<Outcome>([&](auto done) { client.commitAll(repo.root, u"  \n "_s, done); });
    QVERIFY(!empty.ok);
    QCOMPARE(repo.text({u"rev-parse"_s, u"HEAD"_s}), o.value);
}

void TestGitCliRepo::pushAndPull()
{
    QTemporaryDir remoteDir;
    const QString remote = QFileInfo(remoteDir.path()).canonicalFilePath() + u"/zdalne repo.git"_s;
    Repo::runIn(remoteDir.path(), {u"init"_s, u"-q"_s, u"--bare"_s, u"zdalne repo.git"_s});

    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.run({u"remote"_s, u"add"_s, u"origin"_s, remote});

    Client client;
    // No upstream yet: pushed with -u.
    const Outcome first = await<Outcome>([&](auto done) { client.push(repo.root, done); });
    QVERIFY2(first.ok, qPrintable(first.message));
    StatusReport s = await<StatusReport>([&](auto done) { client.status(repo.root, done); });
    QCOMPARE(s.status.branch.upstream, u"origin/main"_s);
    QVERIFY(s.status.branch.aheadBehindKnown);
    QCOMPARE(s.status.branch.ahead, 0);

    repo.write(u"b.txt"_s, "b\n");
    repo.commitAll(u"b"_s);
    s = await<StatusReport>([&](auto done) { client.status(repo.root, done); });
    QCOMPARE(s.status.branch.ahead, 1);
    const Outcome second = await<Outcome>([&](auto done) { client.push(repo.root, done); });
    QVERIFY2(second.ok, qPrintable(second.message));
    s = await<StatusReport>([&](auto done) { client.status(repo.root, done); });
    QCOMPARE(s.status.branch.ahead, 0);

    // Someone else pushes; pull fast-forwards.
    QTemporaryDir otherDir;
    Repo::runIn(otherDir.path(), {u"clone"_s, u"-q"_s, remote, u"inny"_s});
    const QString other = otherDir.filePath(u"inny"_s);
    Repo::runIn(other, {u"config"_s, u"user.name"_s, u"Ktoś"_s});
    Repo::runIn(other, {u"config"_s, u"user.email"_s, u"ktos@example.com"_s});
    QFile f(other + u"/c.txt"_s);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("ć\n");
    f.close();
    Repo::runIn(other, {u"add"_s, u"-A"_s});
    Repo::runIn(other, {u"commit"_s, u"-q"_s, u"-m"_s, u"c"_s});
    Repo::runIn(other, {u"push"_s, u"-q"_s});

    const Outcome pulled = await<Outcome>([&](auto done) { client.pull(repo.root, done); });
    QVERIFY2(pulled.ok, qPrintable(pulled.message));
    QCOMPARE(repo.read(u"c.txt"_s), QByteArray("ć\n"));
    const Outcome again = await<Outcome>([&](auto done) { client.pull(repo.root, done); });
    QVERIFY(again.ok);
    QCOMPARE(again.message, u"Already up to date"_s);

    // Diverged: --ff-only refuses, nothing merged.
    repo.write(u"d.txt"_s, "d\n");
    repo.commitAll(u"d"_s);
    QFile g(other + u"/e.txt"_s);
    QVERIFY(g.open(QIODevice::WriteOnly));
    g.write("e\n");
    g.close();
    Repo::runIn(other, {u"pull"_s, u"-q"_s});
    Repo::runIn(other, {u"add"_s, u"-A"_s});
    Repo::runIn(other, {u"commit"_s, u"-q"_s, u"-m"_s, u"e"_s});
    Repo::runIn(other, {u"push"_s, u"-q"_s});
    const QString head = repo.text({u"rev-parse"_s, u"HEAD"_s});
    const Outcome refused = await<Outcome>([&](auto done) { client.pull(repo.root, done); });
    QVERIFY(!refused.ok);
    QCOMPARE(repo.text({u"rev-parse"_s, u"HEAD"_s}), head);

    // A repository without a remote.
    Repo lonely;
    lonely.write(u"a"_s, "a");
    lonely.commitAll(u"a"_s);
    const Outcome noRemote = await<Outcome>([&](auto done) { client.push(lonely.root, done); });
    QVERIFY(!noRemote.ok);
}

void TestGitCliRepo::branches()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    Client client;
    repo.write(u"zmiana.txt"_s, "w toku\n");
    const Outcome created = await<Outcome>([&](auto done) { client.createBranch(repo.root, u"funkcja/żółw"_s, done); });
    QVERIFY2(created.ok, qPrintable(created.message));
    QCOMPARE(repo.text({u"branch"_s, u"--show-current"_s}), u"funkcja/żółw"_s);
    QVERIFY(repo.exists(u"zmiana.txt"_s)); // the work tree's changes came along

    const BranchList list = await<BranchList>([&](auto done) { client.branches(repo.root, done); });
    QVERIFY2(list.ok, qPrintable(list.error));
    QCOMPARE(list.branches.size(), 2);
    bool sawMain = false;
    for (const Branch &b : list.branches) {
        if (b.name == u"main"_s)
            sawMain = !b.current;
        else
            QVERIFY(b.name == u"funkcja/żółw"_s && b.current);
    }
    QVERIFY(sawMain);

    const Outcome back = await<Outcome>([&](auto done) { client.checkout(repo.root, u"main"_s, done); });
    QVERIFY2(back.ok, qPrintable(back.message));
    QCOMPARE(repo.text({u"branch"_s, u"--show-current"_s}), u"main"_s);

    const Outcome bad = await<Outcome>([&](auto done) { client.createBranch(repo.root, u"zła nazwa"_s, done); });
    QVERIFY(!bad.ok);
    const Outcome dash = await<Outcome>([&](auto done) { client.checkout(repo.root, u"--orphan"_s, done); });
    QVERIFY(!dash.ok);
    const Outcome missing = await<Outcome>([&](auto done) { client.checkout(repo.root, u"nie-ma"_s, done); });
    QVERIFY(!missing.ok);
    QCOMPARE(repo.text({u"branch"_s, u"--show-current"_s}), u"main"_s);

    // A remote-tracking branch without a local one: switched to with --track.
    QTemporaryDir remoteDir;
    const QString remote = remoteDir.filePath(u"r.git"_s);
    Repo::runIn(remoteDir.path(), {u"init"_s, u"-q"_s, u"--bare"_s, u"r.git"_s});
    repo.run({u"remote"_s, u"add"_s, u"origin"_s, remote});
    repo.run({u"push"_s, u"-q"_s, u"origin"_s, u"main:zdalna"_s});
    repo.run({u"fetch"_s, u"-q"_s, u"origin"_s});
    const Outcome tracked = await<Outcome>([&](auto done) { client.checkout(repo.root, u"origin/zdalna"_s, done); });
    QVERIFY2(tracked.ok, qPrintable(tracked.message));
    QCOMPARE(repo.text({u"branch"_s, u"--show-current"_s}), u"zdalna"_s);
    const BranchList withRemote = await<BranchList>([&](auto done) { client.branches(repo.root, done); });
    bool sawRemote = false;
    for (const Branch &b : withRemote.branches)
        sawRemote = sawRemote || (b.remote && b.name == u"origin/zdalna"_s);
    QVERIFY(sawRemote);
}

void TestGitCliRepo::worktrees()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    QTemporaryDir place;
    const QString path = QFileInfo(place.path()).canonicalFilePath() + u"/drzewa/gałąź ćma"_s;
    Client client;
    const Outcome added =
            await<Outcome>([&](auto done) { client.addWorktree(repo.root, path, u"wt/ćma"_s, u"HEAD"_s, done); });
    QVERIFY2(added.ok, qPrintable(added.message));
    QCOMPARE(added.value, path);
    QVERIFY(QFileInfo::exists(path + u"/a.txt"_s));

    const WorktreeList list = await<WorktreeList>([&](auto done) { client.worktrees(repo.root, done); });
    QVERIFY2(list.ok, qPrintable(list.error));
    QCOMPARE(list.worktrees.size(), 2);
    QCOMPARE(list.worktrees.at(0).path, repo.root);
    QCOMPARE(list.worktrees.at(0).branch, u"main"_s);
    QCOMPARE(list.worktrees.at(1).path, path);
    QCOMPARE(list.worktrees.at(1).branch, u"wt/ćma"_s);

    // The linked worktree: its own git folder (.git is a file there), the shared one.
    QVERIFY(QFileInfo(path + u"/.git"_s).isFile());
    const RepoInfo info = await<RepoInfo>([&](auto done) { client.repository(path, done); });
    QVERIFY(info.isRepo);
    QCOMPARE(info.topLevel, path);
    QVERIFY2(info.gitDir.contains(u"/.git/worktrees/"_s), qPrintable(info.gitDir));
    QCOMPARE(QFileInfo(info.commonDir).canonicalFilePath(), QFileInfo(repo.root + u"/.git"_s).canonicalFilePath());

    QFile f(path + u"/a.txt"_s);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("changed\n");
    f.close();
    const StatusReport s = await<StatusReport>([&](auto done) { client.status(path, done); });
    QVERIFY(s.ok);
    QCOMPARE(s.status.branch.head, u"wt/ćma"_s);
    QCOMPARE(s.changes.size(), 1);

    // Changes in it: removed only with force.
    const Outcome refused = await<Outcome>([&](auto done) { client.removeWorktree(repo.root, path, false, done); });
    QVERIFY(!refused.ok);
    QVERIFY(QFileInfo::exists(path));
    const Outcome removed = await<Outcome>([&](auto done) { client.removeWorktree(repo.root, path, true, done); });
    QVERIFY2(removed.ok, qPrintable(removed.message));
    QVERIFY(!QFileInfo::exists(path));
    const WorktreeList after = await<WorktreeList>([&](auto done) { client.worktrees(repo.root, done); });
    QCOMPARE(after.worktrees.size(), 1);

    const Outcome badBranch =
            await<Outcome>([&](auto done) { client.addWorktree(repo.root, path, u"zła nazwa"_s, QString(), done); });
    QVERIFY(!badBranch.ok);
}

void TestGitCliRepo::pullRequestThroughGh()
{
    // A fake gh (tests/core/gitcli/fake_gh.cpp): writes its arguments NUL-separated and answers
    // like gh — a program, so the arguments go through Windows' command line too.
    Repo repo;
    const QString argsFile = repo.dir.filePath(u"gh-args"_s);
    qputenv("FAKE_GH_ARGS", argsFile.toUtf8());
    qputenv("FAKE_GH_MODE", "ok");

    Client client;
    client.setGhProgram(QString::fromUtf8(FAKE_GH));
    PullRequest pr;
    pr.title = u"Tytuł z \"cudzysłowem\" i $(rm -rf ~) & del %PATH% \\\" ^|"_s;
    pr.body = u"Treść;\n`ls` && echo ąę\nC:\\ścieżka\\"_s;
    pr.draft = true;
    pr.head = u"funkcja/żółw"_s;
    const Outcome o = await<Outcome>([&](auto done) { client.createPullRequest(repo.root, pr, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(o.url, u"https://github.com/ja/repo/pull/17"_s);
    QFile a(argsFile);
    QVERIFY(a.open(QIODevice::ReadOnly));
    const QList<QByteArray> args = a.readAll().split('\0');
    a.close();
    QCOMPARE(QString::fromUtf8(args.at(0)), u"pr"_s);
    QCOMPARE(QString::fromUtf8(args.at(1)), u"create"_s);
    QCOMPARE(QString::fromUtf8(args.at(2)), u"--title"_s);
    QCOMPARE(QString::fromUtf8(args.at(3)), pr.title);
    QCOMPARE(QString::fromUtf8(args.at(4)), u"--body"_s);
    QCOMPARE(QString::fromUtf8(args.at(5)), pr.body);
    QCOMPARE(QString::fromUtf8(args.at(6)), u"--draft"_s);
    QCOMPARE(QString::fromUtf8(args.at(7)), u"--head"_s);
    QCOMPARE(QString::fromUtf8(args.at(8)), pr.head);
    QFile pwd(argsFile + u".pwd"_s);
    QVERIFY(pwd.open(QIODevice::ReadOnly));
    QCOMPARE(QFileInfo(QString::fromUtf8(pwd.readAll()).trimmed()).canonicalFilePath(), repo.root);
    pwd.close();

    // "Already exists" is a pull request too; not signed in says how to sign in.
    qputenv("FAKE_GH_MODE", "exists");
    const Outcome exists = await<Outcome>([&](auto done) { client.createPullRequest(repo.root, pr, done); });
    QVERIFY(exists.ok);
    QCOMPARE(exists.url, u"https://github.com/ja/repo/pull/9"_s);
    qputenv("FAKE_GH_MODE", "signed-out");
    const Outcome signedOut = await<Outcome>([&](auto done) { client.createPullRequest(repo.root, pr, done); });
    QVERIFY(!signedOut.ok);
    QVERIFY2(signedOut.message.contains(u"gh auth login"_s), qPrintable(signedOut.message));
    qunsetenv("FAKE_GH_MODE");
    qunsetenv("FAKE_GH_ARGS");

    PullRequest untitled;
    const Outcome noTitle = await<Outcome>([&](auto done) { client.createPullRequest(repo.root, untitled, done); });
    QVERIFY(!noTitle.ok);
}

void TestGitCliRepo::pullRequestWithoutGh()
{
    Repo repo;
    Client client;
    client.setGhProgram(QString());
    PullRequest pr;
    pr.title = u"Tytuł"_s;
    const Outcome o = await<Outcome>([&](auto done) { client.createPullRequest(repo.root, pr, done); });
    QVERIFY(!o.ok);
    QVERIFY2(o.message.contains(u"(gh)"_s), qPrintable(o.message));
}

// ---- Checkpoints -------------------------------------------------------------------------------

void TestGitCliRepo::checkpointLeavesIndexHeadAndStash()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.write(u"b.txt"_s, "b\n");
    repo.commitAll(u"a"_s);
    repo.write(u"a.txt"_s, "staged\n");
    repo.run({u"add"_s, u"a.txt"_s});
    repo.write(u"a.txt"_s, "staged then changed\n");
    repo.write(u"untracked ż.txt"_s, "u\n");
    const QString head = repo.text({u"rev-parse"_s, u"HEAD"_s});
    const QString cached = repo.text({u"diff"_s, u"--cached"_s});
    const QByteArray index = repo.indexBytes();

    Client client;
    const QString id = Client::newCheckpointId();
    QCOMPARE(id.size(), 32);
    QVERIFY(isValidCheckpointId(id));
    QVERIFY(Client::newCheckpointId() != id);
    const Outcome o = await<Outcome>([&](auto done) { client.checkpoint(repo.root, id, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.indexBytes(), index);
    QCOMPARE(repo.text({u"rev-parse"_s, u"HEAD"_s}), head);
    QCOMPARE(repo.text({u"rev-parse"_s, Client::checkpointRef(id)}), o.value);
    QCOMPARE(repo.text({u"rev-parse"_s, o.value + u"^"_s}), head);
    QCOMPARE(repo.text({u"stash"_s, u"list"_s}), QString());
    QCOMPARE(repo.text({u"diff"_s, u"--cached"_s}), cached);
    QCOMPARE(repo.text({u"branch"_s, u"--format=%(refname)"_s}), u"refs/heads/main"_s);
    // The snapshot holds the work tree as it is, untracked files included.
    QCOMPARE(repo.run({u"show"_s, o.value + u":a.txt"_s}), QByteArray("staged then changed\n"));
    QCOMPARE(repo.run({u"show"_s, o.value + u":untracked ż.txt"_s}), QByteArray("u\n"));
    const QStringList ids = await<QStringList>([&](auto done) {
        client.checkpoints(repo.root, [done](bool, const QStringList &list, const QString &) { done(list); });
    });
    QCOMPARE(ids, QStringList{id});
}

void TestGitCliRepo::changesSinceCheckpoint()
{
    Repo repo;
    repo.write(u"zmieniony.txt"_s, "a\nb\n");
    repo.write(u"usuwany.txt"_s, "x\n");
    repo.write(u"stara nazwa.txt"_s, "długi tekst\nktóry zostaje\nten sam po zmianie nazwy\n");
    repo.commitAll(u"a"_s);
    repo.write(u"przed turą.txt"_s, "był\n");

    Client client;
    const Outcome cp = await<Outcome>([&](auto done) { client.checkpoint(repo.root, u"tura-1"_s, done); });
    QVERIFY2(cp.ok, qPrintable(cp.message));
    ChangesReport none = await<ChangesReport>([&](auto done) { client.changesSince(repo.root, u"tura-1"_s, done); });
    QVERIFY2(none.ok, qPrintable(none.error));
    QVERIFY(none.changes.isEmpty());

    repo.write(u"zmieniony.txt"_s, "a\nB\nc\n");
    repo.remove(u"usuwany.txt"_s);
    QVERIFY(QFile::rename(repo.path(u"stara nazwa.txt"_s), repo.path(u"nowa nazwa ś.txt"_s)));
    repo.write(u"nowy/zagnieżdżony/plik.txt"_s, "1\n2\n3\n");
    repo.write(u"przed turą.txt"_s, "był\njest\n");

    const ChangesReport r = await<ChangesReport>([&](auto done) { client.changesSince(repo.root, u"tura-1"_s, done); });
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.changes.size(), 5);
    const FileChange *m = find(r.changes, u"zmieniony.txt"_s);
    QVERIFY(m);
    QCOMPARE(m->change, Change::Modified);
    QCOMPARE(m->additions, 2);
    QCOMPARE(m->deletions, 1);
    const FileChange *d = find(r.changes, u"usuwany.txt"_s);
    QVERIFY(d);
    QCOMPARE(d->change, Change::Deleted);
    const FileChange *ren = find(r.changes, u"nowa nazwa ś.txt"_s);
    QVERIFY(ren);
    QCOMPARE(ren->change, Change::Renamed);
    QCOMPARE(ren->oldPath, u"stara nazwa.txt"_s);
    const FileChange *a = find(r.changes, u"nowy/zagnieżdżony/plik.txt"_s);
    QVERIFY(a);
    QCOMPARE(a->change, Change::Added);
    QCOMPARE(a->additions, 3);
    const FileChange *u = find(r.changes, u"przed turą.txt"_s);
    QVERIFY(u);
    QCOMPARE(u->change, Change::Modified);
    QCOMPARE(u->additions, 1);

    // The old text from the checkpoint, the new one from disk.
    const FileTexts t = await<FileTexts>([&](auto done) {
        client.fileTexts(repo.root, {u"przed turą.txt"_s, QString(), Client::checkpointRef(u"tura-1"_s), QString()}, done);
    });
    QVERIFY(t.ok);
    QCOMPARE(t.oldText, u"był\n"_s);
    QCOMPARE(t.newText, u"był\njest\n"_s);
    const FileTexts rt = await<FileTexts>([&](auto done) {
        client.fileTexts(repo.root, {u"nowa nazwa ś.txt"_s, u"stara nazwa.txt"_s, Client::checkpointRef(u"tura-1"_s), QString()},
                         done);
    });
    QVERIFY(rt.ok);
    QCOMPARE(rt.oldText, rt.newText);
    QVERIFY(!rt.oldText.isEmpty());
}

void TestGitCliRepo::restoreCheckpoint()
{
    Repo repo;
    repo.write(u".gitignore"_s, "ignored/\n*.log\n");
    repo.write(u"keep.txt"_s, "keep\n");
    repo.write(u"mod.txt"_s, "mod\n");
    repo.write(u"del.txt"_s, "del\n");
    repo.write(u"ren.txt"_s, "ren\nmore\nlines\n");
    repo.write(u"zażółć gęślą jaźń.txt"_s, "przed\n");
    repo.write(u"y"_s, "y was a file\n");
    repo.write(u"ignored/x"_s, "ignored before\n");
    repo.write(u"a.log"_s, "log before\n");
#ifdef Q_OS_UNIX
    repo.write(u"run.sh"_s, "#!/bin/sh\n");
    QFile::setPermissions(repo.path(u"run.sh"_s), QFile::permissions(repo.path(u"run.sh"_s)) | QFileDevice::ExeOwner);
    QVERIFY(QFile::link(u"keep.txt"_s, repo.path(u"link"_s)));
#endif
    repo.commitAll(u"start"_s);
    repo.write(u"untracked before.txt"_s, "untracked before\n");
    repo.write(u"x/inner"_s, "x was a folder\n");
    repo.write(u"keep.txt"_s, "staged\n");
    repo.run({u"add"_s, u"keep.txt"_s});
    repo.write(u"keep.txt"_s, "keep, changed after staging\n");

    const QString head = repo.text({u"rev-parse"_s, u"HEAD"_s});
    const QByteArray index = repo.indexBytes();
    Client client;
    const Outcome cp = await<Outcome>([&](auto done) { client.checkpoint(repo.root, u"przed"_s, done); });
    QVERIFY2(cp.ok, qPrintable(cp.message));

    // The turn.
    repo.write(u"mod.txt"_s, "mod\nchanged\n");
    repo.remove(u"del.txt"_s);
    QVERIFY(QFile::rename(repo.path(u"ren.txt"_s), repo.path(u"renamed.txt"_s)));
    repo.write(u"zażółć gęślą jaźń.txt"_s, "po turze\n");
    repo.write(u"new/deep/nowy plik ś.txt"_s, "new\n");
    repo.write(u"untracked before.txt"_s, "changed by the turn\n");
    repo.write(u"keep.txt"_s, "the turn's\n");
    repo.write(u"b.log"_s, "ignored, new\n");
    repo.write(u"ignored/y"_s, "ignored, new\n");
    repo.write(u"a.log"_s, "ignored, changed\n");
    QVERIFY(QDir(repo.path(u"x"_s)).removeRecursively());
    repo.write(u"x"_s, "x is a file now\n");
    repo.remove(u"y"_s);
    repo.write(u"y/a"_s, "y is a folder now\n");
#ifdef Q_OS_UNIX
    QFile::setPermissions(repo.path(u"run.sh"_s), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    repo.remove(u"link"_s);
    repo.write(u"link"_s, "a file instead of a link\n");
#endif

    const RestoreReport r = await<RestoreReport>([&](auto done) { client.restore(repo.root, u"przed"_s, done); });
    QVERIFY2(r.ok, qPrintable(r.error));

    QCOMPARE(repo.read(u"keep.txt"_s), QByteArray("keep, changed after staging\n"));
    QCOMPARE(repo.read(u"mod.txt"_s), QByteArray("mod\n"));
    QCOMPARE(repo.read(u"del.txt"_s), QByteArray("del\n"));
    QCOMPARE(repo.read(u"ren.txt"_s), QByteArray("ren\nmore\nlines\n"));
    QVERIFY(!repo.exists(u"renamed.txt"_s));
    QCOMPARE(repo.read(u"zażółć gęślą jaźń.txt"_s), QByteArray("przed\n"));
    QVERIFY(!repo.exists(u"new/deep/nowy plik ś.txt"_s));
    QVERIFY(!repo.exists(u"new"_s)); // the folders it made, gone with it
    QCOMPARE(repo.read(u"untracked before.txt"_s), QByteArray("untracked before\n"));
    QCOMPARE(repo.read(u"x/inner"_s), QByteArray("x was a folder\n"));
    QCOMPARE(repo.read(u"y"_s), QByteArray("y was a file\n"));
    QVERIFY(QFileInfo(repo.path(u"y"_s)).isFile());
    // Ignored files are not the checkpoint's: left as they are.
    QCOMPARE(repo.read(u"b.log"_s), QByteArray("ignored, new\n"));
    QCOMPARE(repo.read(u"ignored/y"_s), QByteArray("ignored, new\n"));
    QCOMPARE(repo.read(u"ignored/x"_s), QByteArray("ignored before\n"));
    QCOMPARE(repo.read(u"a.log"_s), QByteArray("ignored, changed\n"));
#ifdef Q_OS_UNIX
    QVERIFY(QFile::permissions(repo.path(u"run.sh"_s)) & QFileDevice::ExeOwner);
    QVERIFY(QFileInfo(repo.path(u"link"_s)).isSymbolicLink());
    QCOMPARE(QFileInfo(repo.path(u"link"_s)).readSymLink(), u"keep.txt"_s);
#endif
    QVERIFY(r.removed.contains(u"renamed.txt"_s));
    QVERIFY(r.removed.contains(u"new/deep/nowy plik ś.txt"_s));
    QVERIFY(r.restored.contains(u"del.txt"_s));
    QVERIFY(r.restored.contains(u"zażółć gęślą jaźń.txt"_s));

    // Nothing differs from the checkpoint any more; the index and HEAD never moved.
    const ChangesReport after =
            await<ChangesReport>([&](auto done) { client.changesSince(repo.root, u"przed"_s, done); });
    QVERIFY(after.ok);
    QStringList left;
    for (const FileChange &c : after.changes)
        left.append(c.path);
    QVERIFY2(left.isEmpty(), qPrintable(left.join(u", "_s)));
    QCOMPARE(repo.indexBytes(), index);
    QCOMPARE(repo.text({u"rev-parse"_s, u"HEAD"_s}), head);
    QCOMPARE(repo.text({u"stash"_s, u"list"_s}), QString());
}

void TestGitCliRepo::restoreKeepsFilesIgnoredAgain()
{
    Repo repo;
    repo.write(u".gitignore"_s, "*.env\n");
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.write(u"sekret.env"_s, "HASŁO=tajne\n");
    Client client;
    const Outcome cp = await<Outcome>([&](auto done) { client.checkpoint(repo.root, u"c"_s, done); });
    QVERIFY(cp.ok);
    // The turn un-ignores the file: it is "added" since the checkpoint, though it was there.
    repo.write(u".gitignore"_s, "\n");
    repo.write(u"nowy.txt"_s, "n\n");
    const ChangesReport changes =
            await<ChangesReport>([&](auto done) { client.changesSince(repo.root, u"c"_s, done); });
    QVERIFY(find(changes.changes, u"sekret.env"_s));
    const RestoreReport r = await<RestoreReport>([&](auto done) { client.restore(repo.root, u"c"_s, done); });
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(repo.read(u".gitignore"_s), QByteArray("*.env\n"));
    QCOMPARE(repo.read(u"sekret.env"_s), QByteArray("HASŁO=tajne\n"));
    QVERIFY(r.kept.contains(u"sekret.env"_s));
    QVERIFY(!repo.exists(u"nowy.txt"_s));
}

void TestGitCliRepo::restoreAfterTheTurnCommitted()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    Client client;
    const Outcome cp = await<Outcome>([&](auto done) { client.checkpoint(repo.root, u"c"_s, done); });
    QVERIFY(cp.ok);
    repo.write(u"a.txt"_s, "the agent's\n");
    repo.write(u"b.txt"_s, "b\n");
    repo.commitAll(u"agent"_s);
    const QString agentHead = repo.text({u"rev-parse"_s, u"HEAD"_s});
    const RestoreReport r = await<RestoreReport>([&](auto done) { client.restore(repo.root, u"c"_s, done); });
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(repo.read(u"a.txt"_s), QByteArray("a\n"));
    QVERIFY(!repo.exists(u"b.txt"_s));
    // HEAD stays at the agent's commit: its changes now show as undone in the work tree.
    QCOMPARE(repo.text({u"rev-parse"_s, u"HEAD"_s}), agentHead);
    const StatusReport s = await<StatusReport>([&](auto done) { client.status(repo.root, done); });
    QCOMPARE(s.changes.size(), 2);
}

void TestGitCliRepo::restoreInEmptyRepository()
{
    Repo repo;
    repo.write(u"pierwszy.txt"_s, "1\n");
    Client client;
    const Outcome cp = await<Outcome>([&](auto done) { client.checkpoint(repo.root, u"zero"_s, done); });
    QVERIFY2(cp.ok, qPrintable(cp.message));
    QCOMPARE(repo.text({u"rev-list"_s, u"--count"_s, Client::checkpointRef(u"zero"_s)}), u"1"_s); // no parent
    int code = 0;
    repo.run({u"rev-parse"_s, u"-q"_s, u"--verify"_s, u"HEAD"_s}, {}, &code);
    QVERIFY(code != 0); // still no commit of the user's
    repo.write(u"pierwszy.txt"_s, "zmieniony\n");
    repo.write(u"drugi.txt"_s, "2\n");
    const RestoreReport r = await<RestoreReport>([&](auto done) { client.restore(repo.root, u"zero"_s, done); });
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(repo.read(u"pierwszy.txt"_s), QByteArray("1\n"));
    QVERIFY(!repo.exists(u"drugi.txt"_s));
}

void TestGitCliRepo::checkpointInWorktree()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    QTemporaryDir place;
    const QString wt = QFileInfo(place.path()).canonicalFilePath() + u"/wt"_s;
    repo.run({u"worktree"_s, u"add"_s, u"-q"_s, u"-b"_s, u"wt"_s, wt});
    Client client;
    const Outcome cp = await<Outcome>([&](auto done) { client.checkpoint(wt, u"w1"_s, done); });
    QVERIFY2(cp.ok, qPrintable(cp.message));
    QFile f(wt + u"/a.txt"_s);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("w\n");
    f.close();
    const ChangesReport c = await<ChangesReport>([&](auto done) { client.changesSince(wt, u"w1"_s, done); });
    QCOMPARE(c.changes.size(), 1);
    // The main work tree is not the worktree's: no changes since there.
    const RestoreReport r = await<RestoreReport>([&](auto done) { client.restore(wt, u"w1"_s, done); });
    QVERIFY(r.ok);
    QFile g(wt + u"/a.txt"_s);
    QVERIFY(g.open(QIODevice::ReadOnly));
    QCOMPARE(g.readAll(), QByteArray("a\n"));
    QCOMPARE(repo.read(u"a.txt"_s), QByteArray("a\n"));
    // The refs are the repository's, shared by its worktrees.
    QCOMPARE(repo.text({u"rev-parse"_s, Client::checkpointRef(u"w1"_s)}), cp.value);
}

void TestGitCliRepo::dropCheckpoints()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    Client client;
    for (const QString &id : {u"t/1"_s, u"t/2"_s, u"t/3"_s}) {
        const Outcome o = await<Outcome>([&](auto done) { client.checkpoint(repo.root, id, done); });
        QVERIFY(o.ok);
    }
    const Outcome dropped = await<Outcome>([&](auto done) {
        client.dropCheckpoints(repo.root, {u"t/1"_s, u"t/3"_s, u"nie-było"_s, u"../zły"_s}, done);
    });
    QVERIFY2(dropped.ok, qPrintable(dropped.message));
    const QStringList ids = await<QStringList>([&](auto done) {
        client.checkpoints(repo.root, [done](bool, const QStringList &list, const QString &) { done(list); });
    });
    QCOMPARE(ids, QStringList{u"t/2"_s});
    const ChangesReport gone = await<ChangesReport>([&](auto done) { client.changesSince(repo.root, u"t/1"_s, done); });
    QVERIFY(!gone.ok);
    const RestoreReport noRestore = await<RestoreReport>([&](auto done) { client.restore(repo.root, u"t/3"_s, done); });
    QVERIFY(!noRestore.ok);
    QCOMPARE(repo.read(u"a.txt"_s), QByteArray("a\n"));
}

void TestGitCliRepo::badCheckpointIds()
{
    Repo repo;
    Client client;
    for (const QString &id : {QString(), u"../x"_s, u"a b"_s, u"a..b"_s, u".x"_s}) {
        const Outcome o = await<Outcome>([&](auto done) { client.checkpoint(repo.root, id, done); });
        QVERIFY(!o.ok);
        const RestoreReport r = await<RestoreReport>([&](auto done) { client.restore(repo.root, id, done); });
        QVERIFY(!r.ok);
    }
    QCOMPARE(repo.text({u"for-each-ref"_s}), QString());
}

// ---- Runs --------------------------------------------------------------------------------------

void TestGitCliRepo::noGit()
{
    Repo repo;
    Client client;
    client.setGitProgram(QString());
    const StatusReport s = await<StatusReport>([&](auto done) { client.status(repo.root, done); });
    QVERIFY(!s.ok);
    QVERIFY2(s.error.contains(u"not installed"_s), qPrintable(s.error));
    client.setGitProgram(repo.dir.filePath(u"no-such-git"_s));
    const StatusReport t = await<StatusReport>([&](auto done) { client.status(repo.root, done); });
    QVERIFY(!t.ok);
    QVERIFY(!t.error.isEmpty());
    QCOMPARE(client.running(), 0);
}

void TestGitCliRepo::timeLimit()
{
    // A git that never finishes: the fake gh asleep.
    Repo repo;
    qputenv("FAKE_GH_MODE", "slow");
    Client client;
    client.setGitProgram(QString::fromUtf8(FAKE_GH));
    client.setLocalTimeoutMs(200);
    const RunResult r = await<RunResult>([&](auto done) { client.git(repo.root, {u"status"_s}, {}, done); });
    qunsetenv("FAKE_GH_MODE");
    QVERIFY(!r.ok());
    QVERIFY2(r.failure.contains(u"did not finish"_s), qPrintable(r.failure));
    QCOMPARE(client.running(), 0);
}

void TestGitCliRepo::noCallbackAfterDestruction()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    bool called = false;
    {
        Client client;
        client.status(repo.root, [&called](const StatusReport &) { called = true; });
        client.checkpoint(repo.root, u"x"_s, [&called](const Outcome &) { called = true; });
        client.commitAll(QString(), QString(), [&called](const Outcome &) { called = true; }); // fails "later"
    }
    QTest::qWait(300);
    QVERIFY(!called);
}

// ---- Cloning -------------------------------------------------------------------------------------

void TestGitCliRepo::cloneFromThisComputer()
{
    // A repository with a few commits, cloned by its path and by a file: URL into a folder whose
    // parents are not there yet; the progress comes as git says it.
    Repo source;
    source.write(u"README.md"_s, "# Zażółć gęślą jaźń\n");
    source.write(u"src/main.cpp"_s, "int main() { return 0; }\n");
    source.commitAll(u"Pierwszy"_s);
    source.write(u"notatki/żółw.md"_s, "żółw\n");
    source.commitAll(u"Drugi"_s);

    QTemporaryDir target;
    const QString dest = target.filePath(u"projekty/nowy folder/repo"_s);
    Client client;
    QList<CloneProgress> steps;
    const Outcome o = await<Outcome>([&](auto done) {
        client.clone(source.root, dest, u"t1"_s, [&steps](const CloneProgress &p) { steps << p; }, done);
    });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(o.value, QDir::cleanPath(dest));
    QVERIFY(QFileInfo::exists(dest + u"/README.md"_s));
    QVERIFY(QFileInfo::exists(dest + u"/notatki/żółw.md"_s));
    QCOMPARE(Repo::runIn(dest, {u"rev-list"_s, u"--count"_s, u"HEAD"_s}).trimmed(), QByteArray("2"));
    // (A local clone says little: the checkout at least, when git reports progress at all.)
    for (const CloneProgress &p : std::as_const(steps))
        QVERIFY(p.percent >= -1 && p.percent <= 100);
    QCOMPARE(client.running(), 0);

    const QString viaUrl = target.filePath(u"z adresu"_s);
    const Outcome u = await<Outcome>([&](auto done) {
        client.clone(QUrl::fromLocalFile(source.root).toString(), viaUrl, QString(), nullptr, done);
    });
    QVERIFY2(u.ok, qPrintable(u.message));
    QVERIFY(QFileInfo::exists(viaUrl + u"/src/main.cpp"_s));
}

void TestGitCliRepo::cloneIntoAnEmptyFolder()
{
    Repo source;
    source.write(u"a.txt"_s, "a\n");
    source.commitAll(u"a"_s);
    QTemporaryDir target;
    const QString dest = target.filePath(u"pusty"_s);
    QVERIFY(QDir().mkpath(dest));
    Client client;
    const Outcome o = await<Outcome>([&](auto done) { client.clone(source.root, dest, QString(), nullptr, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QVERIFY(QFileInfo::exists(dest + u"/a.txt"_s));
}

void TestGitCliRepo::cloneRefusesAFullFolder()
{
    Repo source;
    source.write(u"a.txt"_s, "a\n");
    source.commitAll(u"a"_s);
    QTemporaryDir target;
    const QString dest = target.filePath(u"zajęty"_s);
    QVERIFY(QDir().mkpath(dest));
    QFile mine(dest + u"/moje.txt"_s);
    QVERIFY(mine.open(QIODevice::WriteOnly));
    mine.write("moje\n");
    mine.close();
    Client client;
    const Outcome o = await<Outcome>([&](auto done) { client.clone(source.root, dest, QString(), nullptr, done); });
    QVERIFY(!o.ok);
    QVERIFY2(o.message.contains(u"not an empty folder"_s), qPrintable(o.message));
    QVERIFY(QFileInfo::exists(dest + u"/moje.txt"_s)); // (left as it was)
    QVERIFY(!QFileInfo::exists(dest + u"/a.txt"_s));
    // A file where the folder would be.
    const Outcome f = await<Outcome>(
            [&](auto done) { client.clone(source.root, dest + u"/moje.txt"_s, QString(), nullptr, done); });
    QVERIFY(!f.ok);
    // Nothing to clone, or an option.
    QVERIFY(!await<Outcome>([&](auto done) { client.clone(QString(), dest, QString(), nullptr, done); }).ok);
    QVERIFY(!await<Outcome>([&](auto done) { client.clone(u"--upload-pack=x"_s, dest, QString(), nullptr, done); }).ok);
}

void TestGitCliRepo::cloneFailureLeavesNothing()
{
    QTemporaryDir target;
    const QString dest = target.filePath(u"nic"_s);
    Client client;
    const Outcome o = await<Outcome>([&](auto done) {
        client.clone(target.filePath(u"nie ma takiego repozytorium"_s), dest, QString(), nullptr, done);
    });
    QVERIFY(!o.ok);
    QVERIFY(!o.message.isEmpty());
    QVERIFY(!QFileInfo::exists(dest));
    // An empty folder that was there stays, empty.
    QVERIFY(QDir().mkpath(dest));
    const Outcome e = await<Outcome>([&](auto done) {
        client.clone(target.filePath(u"nie ma takiego repozytorium"_s), dest, QString(), nullptr, done);
    });
    QVERIFY(!e.ok);
    QVERIFY(QFileInfo(dest).isDir());
    QVERIFY(QDir(dest).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden));
}

void TestGitCliRepo::cloneStopped()
{
    // A clone that never finishes (the fake gh asleep as git), stopped by its tag.
    qputenv("FAKE_GH_MODE", "slow");
    QTemporaryDir target;
    const QString dest = target.filePath(u"przerwany"_s);
    Client client;
    client.setGitProgram(QString::fromUtf8(FAKE_GH));
    std::optional<Outcome> outcome;
    client.clone(u"https://example.com/a/b.git"_s, dest, u"klon"_s, nullptr,
                 [&outcome](const Outcome &o) { outcome = o; });
    QTest::qWait(100);
    QVERIFY(!client.stop(u"inny"_s));
    QVERIFY(client.stop(u"klon"_s));
    QTRY_VERIFY_WITH_TIMEOUT(outcome.has_value(), brulion::test::kRoomyMs);
    qunsetenv("FAKE_GH_MODE");
    QVERIFY(!outcome->ok);
    QCOMPARE(outcome->message, u"Stopped"_s);
    QVERIFY(!QFileInfo::exists(dest));
    QCOMPARE(client.running(), 0);
    QVERIFY(!client.stop(u"klon"_s));
}

void TestGitCliRepo::cloneSaysWhySigningInFailed()
{
#ifdef Q_OS_UNIX
    // ssh's reason comes before git's "fatal:" line: the outcome says both.
    QTemporaryDir dir;
    const QString script = dir.filePath(u"git"_s);
    QFile s(script);
    QVERIFY(s.open(QIODevice::WriteOnly));
    s.write("#!/bin/sh\necho \"Cloning into 'x'...\" >&2\necho 'git@github.com: Permission denied (publickey).' >&2\n"
            "echo 'fatal: Could not read from remote repository.' >&2\necho >&2\n"
            "echo 'Please make sure you have the correct access rights' >&2\nexit 128\n");
    s.close();
    QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    Client client;
    client.setGitProgram(script);
    const Outcome o = await<Outcome>([&](auto done) {
        client.clone(u"git@github.com:ja/repo.git"_s, dir.filePath(u"repo"_s), QString(), nullptr, done);
    });
    QVERIFY(!o.ok);
    QVERIFY2(o.message.contains(u"Permission denied (publickey)"_s), qPrintable(o.message));
    QVERIFY2(o.message.contains(u"Could not read from remote repository"_s), qPrintable(o.message));
    QVERIFY(isAuthenticationFailure(o.message));
#else
    QSKIP("the fake git is a shell script");
#endif
}

void TestGitCliRepo::repositoryListThroughGh()
{
    QTemporaryDir argsDir;
    const QString argsFile = argsDir.filePath(u"args"_s);
    qputenv("FAKE_GH_ARGS", argsFile.toUtf8());
    qputenv("FAKE_GH_JSON", R"([{"nameWithOwner":"ja/notatki","description":"Zażółć","isPrivate":true}])");
    Client client;
    client.setGhProgram(QString::fromUtf8(FAKE_GH));
    std::optional<QList<HostedRepository>> list;
    QString error;
    client.repositoryList(50, [&](const QList<HostedRepository> &r, const QString &e) {
        list = r;
        error = e;
    });
    QTRY_VERIFY_WITH_TIMEOUT(list.has_value(), brulion::test::kRoomyMs);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(list->size(), 1);
    QCOMPARE(list->at(0).nameWithOwner, u"ja/notatki"_s);
    QCOMPARE(list->at(0).description, u"Zażółć"_s);
    QFile a(argsFile);
    QVERIFY(a.open(QIODevice::ReadOnly));
    const QList<QByteArray> args = a.readAll().split('\0');
    QVERIFY(args.size() >= 6);
    QCOMPARE(args.at(0), QByteArray("repo"));
    QCOMPARE(args.at(1), QByteArray("list"));
    QCOMPARE(args.at(3), QByteArray("50"));
    QCOMPARE(args.at(5), repositoryListFields().toUtf8());

    qputenv("FAKE_GH_MODE", "signed-out");
    list.reset();
    client.repositoryList(50, [&](const QList<HostedRepository> &r, const QString &e) {
        list = r;
        error = e;
    });
    QTRY_VERIFY_WITH_TIMEOUT(list.has_value(), brulion::test::kRoomyMs);
    QVERIFY(list->isEmpty());
    QVERIFY2(error.contains(u"gh auth login"_s), qPrintable(error));
    qunsetenv("FAKE_GH_MODE");
    qunsetenv("FAKE_GH_ARGS");
    qunsetenv("FAKE_GH_JSON");

    client.setGhProgram(QString());
    list.reset();
    client.repositoryList(50, [&](const QList<HostedRepository> &r, const QString &e) {
        list = r;
        error = e;
    });
    QTRY_VERIFY_WITH_TIMEOUT(list.has_value(), brulion::test::kRoomyMs);
    QVERIFY2(error.contains(u"not installed"_s), qPrintable(error));
}

// ---- History, comparisons, remotes, gh ---------------------------------------------------------

void TestGitCliRepo::logOfCommits()
{
    Client client;
    Repo empty;
    const CommitList none = await<CommitList>([&](auto done) { client.log(empty.root, QString(), 100, done); });
    QVERIFY2(none.ok, qPrintable(none.error));
    QVERIFY(none.commits.isEmpty());

    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"Pierwszy: zażółć"_s);
    repo.write(u"a.txt"_s, "b\n");
    repo.commitAll(u"Drugi"_s);
    repo.write(u"a.txt"_s, "c\n");
    repo.commitAll(u"Trzeci"_s);
    const CommitList all = await<CommitList>([&](auto done) { client.log(repo.root, QString(), 100, done); });
    QVERIFY(all.ok);
    QCOMPARE(all.commits.size(), 3);
    QCOMPARE(all.commits.at(0).subject, u"Trzeci"_s);
    QCOMPARE(all.commits.at(0).oid, repo.text({u"rev-parse"_s, u"HEAD"_s}));
    QCOMPARE(all.commits.at(0).parents, QStringList{all.commits.at(1).oid});
    QCOMPARE(all.commits.at(2).subject, u"Pierwszy: zażółć"_s);
    QVERIFY(all.commits.at(2).parents.isEmpty());
    QCOMPARE(all.commits.at(0).authorName, u"Testowy Użytkownik"_s);
    QVERIFY(all.commits.at(0).authorTime > 1600000000);
    const CommitList two = await<CommitList>([&](auto done) { client.log(repo.root, u"HEAD~1"_s, 1, done); });
    QCOMPARE(two.commits.size(), 1);
    QCOMPARE(two.commits.at(0).subject, u"Drugi"_s);
    const CommitList bad = await<CommitList>([&](auto done) { client.log(repo.root, u"--all"_s, 5, done); });
    QVERIFY(!bad.ok);
}

void TestGitCliRepo::changesBetweenRevisions()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\nb\n");
    repo.write(u"stary.txt"_s, "s\n");
    repo.commitAll(u"jeden"_s);
    const QString first = repo.text({u"rev-parse"_s, u"HEAD"_s});
    repo.write(u"a.txt"_s, "a\nB\nc\n");
    repo.remove(u"stary.txt"_s);
    repo.write(u"folder ż/nowy.txt"_s, "n\n");
    repo.commitAll(u"dwa"_s);
    const QString second = repo.text({u"rev-parse"_s, u"HEAD"_s});
    Client client;
    ChangesReport r = await<ChangesReport>([&](auto done) { client.changesBetween(repo.root, first, second, done); });
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.changes.size(), 3);
    const FileChange *a = find(r.changes, u"a.txt"_s);
    QVERIFY(a);
    QCOMPARE(a->change, Change::Modified);
    QCOMPARE(a->additions, 2);
    QCOMPARE(a->deletions, 1);
    QCOMPARE(find(r.changes, u"stary.txt"_s)->change, Change::Deleted);
    QCOMPARE(find(r.changes, u"folder ż/nowy.txt"_s)->change, Change::Added);

    // The root commit's own changes: against the empty tree.
    r = await<ChangesReport>([&](auto done) { client.changesBetween(repo.root, QString(), first, done); });
    QVERIFY(r.ok);
    QCOMPARE(r.changes.size(), 2);
    QCOMPARE(find(r.changes, u"a.txt"_s)->change, Change::Added);

    // A commit against the work tree: untracked files too, the index untouched.
    repo.write(u"nieśledzony.txt"_s, "u\n");
    const QByteArray index = repo.indexBytes();
    r = await<ChangesReport>([&](auto done) { client.changesBetween(repo.root, second, QString(), done); });
    QVERIFY(r.ok);
    QCOMPARE(r.changes.size(), 1);
    QCOMPARE(r.changes.at(0).path, u"nieśledzony.txt"_s);
    QCOMPARE(repo.indexBytes(), index);

    // Revisions that are no revisions.
    r = await<ChangesReport>([&](auto done) { client.changesBetween(repo.root, u"nie-ma-takiej"_s, second, done); });
    QVERIFY(!r.ok);
    QVERIFY2(r.error.contains(u"nie-ma-takiej"_s), qPrintable(r.error));
    r = await<ChangesReport>([&](auto done) { client.changesBetween(repo.root, u"--output=x"_s, second, done); });
    QVERIFY(!r.ok);
    QVERIFY(!QFileInfo::exists(repo.path(u"x"_s)));
    r = await<ChangesReport>(
            [&](auto done) { client.changesBetween(repo.root, Client::checkpointRef(u"brak"_s), QString(), done); });
    QVERIFY(!r.ok);
    QCOMPARE(r.error, u"The checkpoint does not exist"_s);
}

void TestGitCliRepo::fileTextsBetweenRevisions()
{
    Repo repo;
    repo.write(u"a.txt"_s, "stary ą\n");
    repo.commitAll(u"jeden"_s);
    const QString first = repo.text({u"rev-parse"_s, u"HEAD"_s});
    repo.write(u"a.txt"_s, "nowy ę\n");
    repo.write(u"b.txt"_s, "b\n");
    repo.commitAll(u"dwa"_s);
    repo.write(u"a.txt"_s, "na dysku\n");
    Client client;
    FileTextsRequest req;
    req.path = u"a.txt"_s;
    req.base = first;
    req.target = u"HEAD"_s;
    FileTexts t = await<FileTexts>([&](auto done) { client.fileTexts(repo.root, req, done); });
    QVERIFY2(t.ok, qPrintable(t.error));
    QCOMPARE(t.oldText, u"stary ą\n"_s);
    QCOMPARE(t.newText, u"nowy ę\n"_s);
    QVERIFY(t.oldExists && t.newExists);
    // A file added by the commit: no old side; deleted: no new side.
    req.path = u"b.txt"_s;
    t = await<FileTexts>([&](auto done) { client.fileTexts(repo.root, req, done); });
    QVERIFY(t.ok);
    QVERIFY(!t.oldExists);
    QCOMPARE(t.newText, u"b\n"_s);
    req.base = u"HEAD"_s;
    req.target = first;
    t = await<FileTexts>([&](auto done) { client.fileTexts(repo.root, req, done); });
    QVERIFY(t.ok);
    QVERIFY(t.oldExists);
    QVERIFY(!t.newExists);
    QCOMPARE(t.newText, QString());
    // A root commit against the empty side.
    req.path = u"a.txt"_s;
    req.base.clear();
    req.target = first;
    t = await<FileTexts>([&](auto done) { client.fileTexts(repo.root, req, done); });
    QVERIFY(t.ok);
    QCOMPARE(t.newText, u"stary ą\n"_s);
    // An option as a revision.
    req.target = u"--output=x"_s;
    t = await<FileTexts>([&](auto done) { client.fileTexts(repo.root, req, done); });
    QVERIFY(!t.ok);
}

void TestGitCliRepo::mergeBaseAndDefaultBranch()
{
    Client client;
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    const QString base = repo.text({u"rev-parse"_s, u"HEAD"_s});
    DefaultBranch d = await<DefaultBranch>([&](auto done) { client.defaultBranch(repo.root, done); });
    QVERIFY(d.ok);
    QCOMPARE(d.name, u"main"_s);
    QCOMPARE(d.ref, u"main"_s);
    repo.run({u"switch"_s, u"-q"_s, u"-c"_s, u"funkcja/żółw"_s});
    repo.write(u"b.txt"_s, "b\n");
    repo.commitAll(u"b"_s);
    repo.run({u"switch"_s, u"-q"_s, u"main"_s});
    repo.write(u"c.txt"_s, "c\n");
    repo.commitAll(u"c"_s);
    Outcome m = await<Outcome>([&](auto done) { client.mergeBase(repo.root, u"main"_s, u"funkcja/żółw"_s, done); });
    QVERIFY2(m.ok, qPrintable(m.message));
    QCOMPARE(m.value, base);
    m = await<Outcome>([&](auto done) { client.mergeBase(repo.root, u"main"_s, u"-x"_s, done); });
    QVERIFY(!m.ok);
    // An unrelated history: nothing in common.
    repo.run({u"switch"_s, u"-q"_s, u"--orphan"_s, u"sierota"_s});
    repo.write(u"d.txt"_s, "d\n");
    repo.commitAll(u"d"_s);
    m = await<Outcome>([&](auto done) { client.mergeBase(repo.root, u"main"_s, u"sierota"_s, done); });
    QVERIFY(!m.ok);

    // master; and origin/HEAD of a clone wins.
    Repo old;
    old.run({u"switch"_s, u"-q"_s, u"--orphan"_s, u"master"_s});
    old.write(u"a"_s, "a");
    old.commitAll(u"a"_s);
    d = await<DefaultBranch>([&](auto done) { client.defaultBranch(old.root, done); });
    QCOMPARE(d.name, u"master"_s);
    QTemporaryDir cloneDir;
    Repo::runIn(cloneDir.path(), {u"clone"_s, u"-q"_s, repo.root, u"klon"_s});
    const QString clone = cloneDir.filePath(u"klon"_s);
    Repo::runIn(clone, {u"remote"_s, u"set-head"_s, u"origin"_s, u"funkcja/żółw"_s});
    d = await<DefaultBranch>([&](auto done) { client.defaultBranch(clone, done); });
    QVERIFY(d.ok);
    QCOMPARE(d.name, u"funkcja/żółw"_s);
    QCOMPARE(d.ref, u"origin/funkcja/żółw"_s);
    // None at all.
    Repo bare;
    d = await<DefaultBranch>([&](auto done) { client.defaultBranch(bare.root, done); });
    QVERIFY(!d.ok);
}

void TestGitCliRepo::forkPullRequestTarget()
{
    Client client;
    Repo repo;
    repo.write(u"a"_s, "a");
    repo.commitAll(u"a"_s);
    repo.run({u"remote"_s, u"add"_s, u"origin"_s, u"git@github.com:ja/repo.git"_s});
    ForkTarget t = await<ForkTarget>([&](auto done) { client.pullRequestTarget(repo.root, u"main"_s, done); });
    QVERIFY(t.repo.isEmpty() && t.head.isEmpty());
    repo.run({u"remote"_s, u"add"_s, u"upstream"_s, u"https://github.com/org/repo.git"_s});
    t = await<ForkTarget>([&](auto done) { client.pullRequestTarget(repo.root, u"main"_s, done); });
    QCOMPARE(t.repo, u"org/repo"_s);
    QCOMPARE(t.head, u"ja:main"_s);
    // Pushed to upstream (branch.<name>.pushRemote): no fork.
    repo.run({u"config"_s, u"branch.main.pushRemote"_s, u"upstream"_s});
    t = await<ForkTarget>([&](auto done) { client.pullRequestTarget(repo.root, u"main"_s, done); });
    QVERIFY(t.repo.isEmpty());
}

void TestGitCliRepo::pullRequestView()
{
#ifdef Q_OS_UNIX
    Repo repo;
    QTemporaryDir ghDir;
    const QString gh = fakeGh(ghDir, "echo '{\"number\":42,\"url\":\"https://github.com/ja/repo/pull/42\",\"state\":\"OPEN\","
                                     "\"isDraft\":true,\"title\":\"Żółw\",\"baseRefName\":\"main\"}'\n");
    Client client;
    client.setGhProgram(gh);
    PullRequestStatus s = await<PullRequestStatus>([&](auto done) { client.pullRequestFor(repo.root, QString(), QString(), done); });
    QVERIFY2(s.ok, qPrintable(s.error));
    QVERIFY(s.found);
    QCOMPARE(s.pr.number, 42);
    QCOMPARE(s.pr.state, u"draft"_s);
    QCOMPARE(s.pr.title, u"Żółw"_s);
    QCOMPARE(ghArgs(ghDir),
             (QStringList{u"pr"_s, u"view"_s, u"--json"_s, u"number,url,state,isDraft,title,baseRefName"_s}));
    s = await<PullRequestStatus>([&](auto done) { client.pullRequestFor(repo.root, u"ja:x"_s, u"org/repo"_s, done); });
    QCOMPARE(ghArgs(ghDir), (QStringList{u"pr"_s, u"view"_s, u"ja:x"_s, u"--repo"_s, u"org/repo"_s, u"--json"_s,
                                         u"number,url,state,isDraft,title,baseRefName"_s}));
    // None for the branch: an answer.
    fakeGh(ghDir, "echo 'no pull requests found for branch \"x\"' >&2\nexit 1\n");
    s = await<PullRequestStatus>([&](auto done) { client.pullRequestFor(repo.root, QString(), QString(), done); });
    QVERIFY(s.ok);
    QVERIFY(!s.found);
    fakeGh(ghDir, "echo 'To get started with GitHub CLI, please run:  gh auth login' >&2\nexit 4\n");
    s = await<PullRequestStatus>([&](auto done) { client.pullRequestFor(repo.root, QString(), QString(), done); });
    QVERIFY(!s.ok);
    QVERIFY2(s.error.contains(u"gh auth login"_s), qPrintable(s.error));
    client.setGhProgram(QString());
    s = await<PullRequestStatus>([&](auto done) { client.pullRequestFor(repo.root, QString(), QString(), done); });
    QVERIFY(!s.ok);

    // A pull request into another base, from a fork.
    fakeGh(ghDir, "echo https://github.com/org/repo/pull/7\n");
    client.setGhProgram(gh);
    PullRequest pr;
    pr.title = u"Tytuł"_s;
    pr.base = u"rozwój"_s;
    pr.head = u"ja:funkcja"_s;
    pr.repo = u"org/repo"_s;
    const Outcome o = await<Outcome>([&](auto done) { client.createPullRequest(repo.root, pr, done); });
    QVERIFY(o.ok);
    QCOMPARE(o.url, u"https://github.com/org/repo/pull/7"_s);
    const QStringList args = ghArgs(ghDir);
    QVERIFY(args.contains(u"--base"_s));
    QCOMPARE(args.at(args.indexOf(u"--base"_s) + 1), u"rozwój"_s);
    QCOMPARE(args.at(args.indexOf(u"--head"_s) + 1), u"ja:funkcja"_s);
    QCOMPARE(args.at(args.indexOf(u"--repo"_s) + 1), u"org/repo"_s);
#else
    QSKIP("a fake gh as a shell script");
#endif
}

void TestGitCliRepo::initAndPublish()
{
    Client client;
    QTemporaryDir plain;
    const QString folder = QFileInfo(plain.path()).canonicalFilePath();
    Outcome o = await<Outcome>([&](auto done) { client.initRepository(folder, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    const RepoInfo info = await<RepoInfo>([&](auto done) { client.repository(folder, done); });
    QVERIFY(info.isRepo);
    o = await<Outcome>([&](auto done) { client.initRepository(folder + u"/brak"_s, done); });
    QVERIFY(!o.ok);
#ifdef Q_OS_UNIX
    QTemporaryDir ghDir;
    client.setGhProgram(fakeGh(ghDir, "echo '✓ Created repository ja/notatki on GitHub' >&2\n"
                                      "echo '  https://github.com/ja/notatki'\n"));
    o = await<Outcome>([&](auto done) { client.publish(folder, u"notatki"_s, false, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(o.url, u"https://github.com/ja/notatki"_s);
    QCOMPARE(ghArgs(ghDir), (QStringList{u"repo"_s, u"create"_s, u"notatki"_s, u"--private"_s, u"--source"_s, folder,
                                         u"--remote"_s, u"origin"_s, u"--push"_s}));
    o = await<Outcome>([&](auto done) { client.publish(folder, u"ja/publiczne"_s, true, done); });
    QVERIFY(ghArgs(ghDir).contains(u"--public"_s));
    // Names that are no names never reach gh.
    QFile::remove(ghDir.filePath(u"gh-args"_s));
    o = await<Outcome>([&](auto done) { client.publish(folder, u"--help"_s, true, done); });
    QVERIFY(!o.ok);
    o = await<Outcome>([&](auto done) { client.publish(folder, u"żółw"_s, true, done); });
    QVERIFY(!o.ok);
    QVERIFY(!QFileInfo::exists(ghDir.filePath(u"gh-args"_s)));
#endif
}

void TestGitCliRepo::revertAnOlderTurn()
{
    Repo repo;
    repo.write(u"a.txt"_s, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");
    repo.commitAll(u"a"_s);
    Client client;
    // Turn 1 changes line 2 and adds a file; turn 2 changes line 9; then turn 1 alone is undone.
    QVERIFY(await<Outcome>([&](auto done) { client.checkpoint(repo.root, u"t1"_s, done); }).ok);
    repo.write(u"a.txt"_s, "1\nDWA\n3\n4\n5\n6\n7\n8\n9\n10\n");
    repo.write(u"nowy ż.txt"_s, "n\n");
    QVERIFY(await<Outcome>([&](auto done) { client.checkpoint(repo.root, u"t2"_s, done); }).ok);
    repo.write(u"a.txt"_s, "1\nDWA\n3\n4\n5\n6\n7\n8\nDZIEWIĘĆ\n10\n");
    const QByteArray index = repo.indexBytes();
    Outcome o = await<Outcome>([&](auto done) {
        client.revertBetween(repo.root, Client::checkpointRef(u"t1"_s), Client::checkpointRef(u"t2"_s), done);
    });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.read(u"a.txt"_s), QByteArray("1\n2\n3\n4\n5\n6\n7\n8\nDZIEWIĘĆ\n10\n"));
    QVERIFY(!repo.exists(u"nowy ż.txt"_s));
    QCOMPARE(repo.indexBytes(), index);
    // Changed since in the same lines: refused, nothing written.
    QVERIFY(await<Outcome>([&](auto done) { client.checkpoint(repo.root, u"t3"_s, done); }).ok);
    repo.write(u"a.txt"_s, "1\n2\n3\n4\n5\n6\n7\n8\nINNE\n10\n");
    o = await<Outcome>([&](auto done) {
        client.revertBetween(repo.root, Client::checkpointRef(u"t2"_s), Client::checkpointRef(u"t3"_s), done);
    });
    QVERIFY(!o.ok);
    QCOMPARE(repo.read(u"a.txt"_s), QByteArray("1\n2\n3\n4\n5\n6\n7\n8\nINNE\n10\n"));
    // Nothing between them.
    o = await<Outcome>([&](auto done) {
        client.revertBetween(repo.root, Client::checkpointRef(u"t3"_s), Client::checkpointRef(u"t3"_s), done);
    });
    QVERIFY(o.ok);
    o = await<Outcome>([&](auto done) { client.revertBetween(repo.root, u"-x"_s, u"HEAD"_s, done); });
    QVERIFY(!o.ok);
}

// ---- Files, hunks, commits ---------------------------------------------------------------------

void TestGitCliRepo::stageAndUnstage()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.write(u"usuń.txt"_s, "x\n");
    repo.commitAll(u"a"_s);
    repo.write(u"a.txt"_s, "b\n");
    repo.remove(u"usuń.txt"_s);
    // A name git would read as a pathspec's magic; Windows allows neither ':' nor '*' in names.
#ifdef Q_OS_WIN
    const bool magicName = false;
#else
    const bool magicName = true;
    repo.write(u":(glob)*"_s, "dziwna nazwa\n");
#endif
    repo.write(u"-n"_s, "opcja?\n");
    Client client;
    QStringList toStage{u"a.txt"_s, u"usuń.txt"_s, u"-n"_s};
    if (magicName)
        toStage.append(u":(glob)*"_s);
    Outcome o = await<Outcome>([&](auto done) { client.stage(repo.root, toStage, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.text({u"diff"_s, u"--cached"_s, u"--name-status"_s, u"-z"_s})
                     .split(QChar(0), Qt::SkipEmptyParts)
                     .size(),
             magicName ? 8 : 6);
    QCOMPARE(repo.text({u"diff"_s, u"--name-only"_s}), QString());
    QStringList toUnstage{u"a.txt"_s};
    if (magicName)
        toUnstage.append(u":(glob)*"_s);
    o = await<Outcome>([&](auto done) { client.unstage(repo.root, toUnstage, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    const QString staged = repo.text({u"diff"_s, u"--cached"_s, u"--name-only"_s});
    QVERIFY(!staged.contains(u"a.txt"_s));
    QVERIFY(staged.contains(u"usuń.txt"_s) || staged.contains(u"\\305"_s));
    QCOMPARE(repo.read(u"a.txt"_s), QByteArray("b\n"));
    if (magicName)
        QVERIFY(repo.text({u"status"_s, u"--porcelain"_s}).contains(u"?? \":(glob)*\""_s)
                || repo.text({u"status"_s, u"--porcelain"_s}).contains(u"?? :(glob)*"_s));
    // Paths that leave the work tree never reach git.
    o = await<Outcome>([&](auto done) { client.stage(repo.root, {u"../poza"_s}, done); });
    QVERIFY(!o.ok);
    o = await<Outcome>([&](auto done) { client.stage(repo.root, {}, done); });
    QVERIFY(!o.ok);
}

void TestGitCliRepo::stageInEmptyRepository()
{
    Repo repo;
    repo.write(u"pierwszy.txt"_s, "1\n");
    Client client;
    QVERIFY(await<Outcome>([&](auto done) { client.stage(repo.root, {u"pierwszy.txt"_s}, done); }).ok);
    QCOMPARE(repo.text({u"ls-files"_s}), u"pierwszy.txt"_s);
    const Outcome o = await<Outcome>([&](auto done) { client.unstage(repo.root, {u"pierwszy.txt"_s}, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.text({u"ls-files"_s}), QString());
    QVERIFY(repo.exists(u"pierwszy.txt"_s));
}

void TestGitCliRepo::discardFiles()
{
    Repo repo;
    repo.write(u"zmieniony.txt"_s, "stary\n");
    repo.write(u"usunięty.txt"_s, "był\n");
    repo.write(u"przeniesiony.txt"_s, "przenoszę\nwiele\nlinii\n");
    repo.commitAll(u"a"_s);
    repo.write(u"zmieniony.txt"_s, "nowy\n");
    repo.run({u"add"_s, u"zmieniony.txt"_s});
    repo.write(u"zmieniony.txt"_s, "jeszcze nowszy\n");
    repo.remove(u"usunięty.txt"_s);
    repo.write(u"dodany.txt"_s, "dodany\n");
    repo.run({u"add"_s, u"dodany.txt"_s});
    repo.write(u"folder/nieśledzony.txt"_s, "u\n");
    repo.run({u"mv"_s, u"przeniesiony.txt"_s, u"nowa nazwa.txt"_s});
    Client client;
    for (const auto &[path, old] : QList<QPair<QString, QString>>{{u"zmieniony.txt"_s, QString()},
                                                                  {u"usunięty.txt"_s, QString()},
                                                                  {u"dodany.txt"_s, QString()},
                                                                  {u"folder/nieśledzony.txt"_s, QString()},
                                                                  {u"nowa nazwa.txt"_s, u"przeniesiony.txt"_s}}) {
        const Outcome o = await<Outcome>([&](auto done) { client.discard(repo.root, path, old, done); });
        QVERIFY2(o.ok, qPrintable(path + u": "_s + o.message));
    }
    QCOMPARE(repo.text({u"status"_s, u"--porcelain"_s, u"--untracked-files=all"_s}), QString());
    QCOMPARE(repo.read(u"zmieniony.txt"_s), QByteArray("stary\n"));
    QCOMPARE(repo.read(u"usunięty.txt"_s), QByteArray("był\n"));
    QCOMPARE(repo.read(u"przeniesiony.txt"_s), QByteArray("przenoszę\nwiele\nlinii\n"));
    QVERIFY(!repo.exists(u"dodany.txt"_s));
    QVERIFY(!repo.exists(u"nowa nazwa.txt"_s));
    QVERIFY(!repo.exists(u"folder/nieśledzony.txt"_s));
#ifdef Q_OS_LINUX
    // The new files went to the trash (this test's home), not gone for good. (The XDG trash: Windows'
    // Recycle Bin and the macOS Trash are the system's, outside the test's home.)
    QDirIterator it(m_home.filePath(u"xdg-data/Trash/files"_s), QDir::Files);
    QStringList trashed;
    while (it.hasNext())
        trashed << QFileInfo(it.next()).fileName();
    QVERIFY2(trashed.contains(u"dodany.txt"_s), qPrintable(trashed.join(u", "_s)));
    QVERIFY(trashed.contains(u"nieśledzony.txt"_s));
#endif
}

void TestGitCliRepo::discardRefused()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    repo.write(u"podrepo/plik"_s, "x");
    QDir().mkpath(repo.path(u"podrepo/.git"_s));
    Client client;
    Outcome o = await<Outcome>([&](auto done) { client.discard(repo.root, u"podrepo"_s, QString(), done); });
    QVERIFY(!o.ok);
    QVERIFY(repo.exists(u"podrepo/plik"_s));
    o = await<Outcome>([&](auto done) { client.discard(repo.root, u"../a.txt"_s, QString(), done); });
    QVERIFY(!o.ok);
    o = await<Outcome>([&](auto done) { client.discard(repo.root, u".git/config"_s, QString(), done); });
    QVERIFY(!o.ok);
#ifdef Q_OS_UNIX
    // Through a link to a folder outside: never.
    QTemporaryDir outside;
    QFile victim(outside.filePath(u"ofiara.txt"_s));
    QVERIFY(victim.open(QIODevice::WriteOnly));
    victim.write("cenne\n");
    victim.close();
    QVERIFY(QFile::link(outside.path(), repo.path(u"link"_s)));
    o = await<Outcome>([&](auto done) { client.discard(repo.root, u"link/ofiara.txt"_s, QString(), done); });
    QVERIFY(!o.ok);
    QVERIFY(QFileInfo::exists(outside.filePath(u"ofiara.txt"_s)));
#endif
    // A conflicted file is settled, not thrown away.
    Repo conflicted;
    makeConflict(conflicted);
    conflicted.run({u"merge"_s, u"-q"_s, u"inna"_s}, {}, nullptr);
    o = await<Outcome>([&](auto done) { client.discard(conflicted.root, u"wspólny.txt"_s, QString(), done); });
    QVERIFY(!o.ok);
    QVERIFY(conflicted.read(u"wspólny.txt"_s).contains("<<<<<<<"));
}

// Single lines of one hunk: one change staged, the other left; then one thrown away, the other kept.
void TestGitCliRepo::singleLines()
{
    Repo repo;
    repo.write(u"wiersze.txt"_s, "a\nb\nc\nd\n");
    repo.run({u"add"_s, u"-A"_s});
    repo.run({u"commit"_s, u"-qm"_s, u"a"_s});
    repo.write(u"wiersze.txt"_s, "a\nB żółw\nC\nd\n");
    Client client;
    // Line 2's change only: b → "B żółw".
    Outcome o = await<Outcome>([&](auto done) {
        client.applyLines(repo.root, u"wiersze.txt"_s, HunkAction::Stage, 2, 2, 2, 2, done);
    });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(o.message, u"Staged the lines"_s);
    QCOMPARE(repo.text({u"show"_s, u":wiersze.txt"_s}), u"a\nB żółw\nc\nd"_s);
    // Line 3's change thrown away from the work tree: C back to c, line 2 kept.
    o = await<Outcome>([&](auto done) {
        client.applyLines(repo.root, u"wiersze.txt"_s, HunkAction::Discard, 3, 3, 3, 3, done);
    });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(o.message, u"Threw the lines away"_s);
    QCOMPARE(repo.read(u"wiersze.txt"_s), QByteArray("a\nB żółw\nc\nd\n"));
    // The staged line unstaged.
    o = await<Outcome>([&](auto done) {
        client.applyLines(repo.root, u"wiersze.txt"_s, HunkAction::Unstage, 2, 2, 2, 2, done);
    });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(o.message, u"Unstaged the lines"_s);
    QCOMPARE(repo.text({u"diff"_s, u"--cached"_s, u"--name-only"_s}), QString());
    // Lines without a change: nothing to do, said.
    QVERIFY(!await<Outcome>([&](auto done) {
        client.applyLines(repo.root, u"wiersze.txt"_s, HunkAction::Stage, 4, 4, 4, 4, done);
    }).ok);
}

// A hunk of another source than the work tree's changes — a commit's, a turn's, a branch's
// (`base` → `target`) — taken out of the work tree alone; the other hunk stays. Lines the user has
// changed since: refused, the file as it was. A target-less range is the work tree itself.
void TestGitCliRepo::hunkOfARangeReverted()
{
    Repo repo;
    QByteArray lines;
    for (int i = 1; i <= 30; ++i)
        lines += "linia " + QByteArray::number(i) + '\n';
    repo.write(u"plik ż.txt"_s, lines);
    repo.commitAll(u"a"_s);
    QByteArray changed = lines;
    changed.replace("linia 3\n", "LINIA 3\n");
    changed.replace("linia 25\n", "LINIA 25\nnowa po 25\n");
    repo.write(u"plik ż.txt"_s, changed);
    repo.commitAll(u"b"_s);
    Client client;
    // The commit's second hunk (its lines 25–26) undone in the work tree.
    Outcome o = await<Outcome>([&](auto done) {
        client.revertHunk(repo.root, u"plik ż.txt"_s, u"HEAD~1"_s, u"HEAD"_s, 25, 26, done);
    });
    QVERIFY2(o.ok, qPrintable(o.message));
    QByteArray expected = lines;
    expected.replace("linia 3\n", "LINIA 3\n");
    QCOMPARE(repo.read(u"plik ż.txt"_s), expected);
    QCOMPARE(repo.text({u"diff"_s, u"--cached"_s, u"--name-only"_s}), QString()); // (the index untouched)
    // The first hunk's lines changed by the user since: refused, nothing written.
    QByteArray edited = expected;
    edited.replace("LINIA 3\n", "linia trzecia, moja\n");
    repo.write(u"plik ż.txt"_s, edited);
    o = await<Outcome>([&](auto done) {
        client.revertHunk(repo.root, u"plik ż.txt"_s, u"HEAD~1"_s, u"HEAD"_s, 3, 3, done);
    });
    QVERIFY(!o.ok);
    QCOMPARE(repo.read(u"plik ż.txt"_s), edited);
    // No hunk there, a base that is an option, an unsafe path: refused.
    o = await<Outcome>([&](auto done) {
        client.revertHunk(repo.root, u"plik ż.txt"_s, u"HEAD~1"_s, u"HEAD"_s, 12, 12, done);
    });
    QVERIFY(!o.ok);
    QVERIFY2(o.message.contains(u"revert"_s), qPrintable(o.message));
    QVERIFY(!await<Outcome>([&](auto done) {
        client.revertHunk(repo.root, u"plik ż.txt"_s, u"--output=x"_s, u"HEAD"_s, 3, 3, done);
    }).ok);
    QVERIFY(!QFileInfo::exists(repo.path(u"x"_s)));
    QVERIFY(!await<Outcome>([&](auto done) {
        client.revertHunk(repo.root, u"../poza.txt"_s, u"HEAD~1"_s, u"HEAD"_s, 3, 3, done);
    }).ok);
    QVERIFY(!await<Outcome>([&](auto done) {
        client.revertHunk(repo.root, u"plik ż.txt"_s, u"HEAD~1"_s, u"HEAD"_s, 0, 3, done);
    }).ok);
    QVERIFY(!await<Outcome>([&](auto done) {
        client.revertHunk(repo.root, u"plik ż.txt"_s, u"HEAD~1"_s, u"HEAD"_s, 3, 0, done);
    }).ok);
    QCOMPARE(repo.read(u"plik ż.txt"_s), edited);
    // Against the work tree (no target): the user's own line thrown away.
    o = await<Outcome>([&](auto done) {
        client.revertHunk(repo.root, u"plik ż.txt"_s, u"HEAD"_s, QString(), 3, 3, done);
    });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.read(u"plik ż.txt"_s), expected);
    // The lines are the target's: after lines added above, a hunk is found by its new numbers.
    QByteArray shifted = "nowa 1\nnowa 2\nnowa 3\n" + expected;
    shifted.replace("linia 12\n", "LINIA 12\n");
    repo.write(u"plik ż.txt"_s, shifted);
    repo.commitAll(u"c"_s);
    o = await<Outcome>([&](auto done) {
        client.revertHunk(repo.root, u"plik ż.txt"_s, u"HEAD~1"_s, u"HEAD"_s, 15, 15, done);
    });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.read(u"plik ż.txt"_s), "nowa 1\nnowa 2\nnowa 3\n" + expected);
}

void TestGitCliRepo::hunks()
{
    Repo repo;
    QByteArray lines;
    for (int i = 1; i <= 30; ++i)
        lines += "linia " + QByteArray::number(i) + '\n';
    repo.write(u"plik ż.txt"_s, lines);
    repo.commitAll(u"a"_s);
    QByteArray changed = lines;
    changed.replace("linia 3\n", "LINIA 3\n");
    changed.replace("linia 25\n", "LINIA 25\nnowa po 25\n");
    repo.write(u"plik ż.txt"_s, changed);
    Client client;
    // The second change only (the work tree's lines 25–26).
    Outcome o = await<Outcome>([&](auto done) { client.applyHunk(repo.root, u"plik ż.txt"_s, HunkAction::Stage, 25, 26, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QVERIFY2(o.message.startsWith(u"Staged 1 hunk"_s), qPrintable(o.message));
    QByteArray staged = repo.run({u"show"_s, u":plik ż.txt"_s});
    QVERIFY(staged.contains("LINIA 25\nnowa po 25\n"));
    QVERIFY(staged.contains("linia 3\n"));
    QCOMPARE(repo.read(u"plik ż.txt"_s), changed);
    // Again: nothing of those lines is unstaged any more.
    o = await<Outcome>([&](auto done) { client.applyHunk(repo.root, u"plik ż.txt"_s, HunkAction::Stage, 25, 26, done); });
    QVERIFY(!o.ok);
    // The first one staged too, then taken out of the index again by HEAD's line 3.
    QVERIFY(await<Outcome>([&](auto done) { client.applyHunk(repo.root, u"plik ż.txt"_s, HunkAction::Stage, 3, 3, done); }).ok);
    QCOMPARE(repo.run({u"show"_s, u":plik ż.txt"_s}), changed);
    o = await<Outcome>([&](auto done) { client.applyHunk(repo.root, u"plik ż.txt"_s, HunkAction::Unstage, 3, 3, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QVERIFY2(o.message.startsWith(u"Unstaged 1 hunk"_s), qPrintable(o.message));
    staged = repo.run({u"show"_s, u":plik ż.txt"_s});
    QVERIFY(staged.contains("linia 3\n"));
    QVERIFY(staged.contains("LINIA 25\n"));
    // Thrown away from the work tree: line 3 back to the index's text, line 25 stays.
    // (Line 0, or a range ending there: no line of the file.)
    QVERIFY(!await<Outcome>([&](auto done) { client.applyHunk(repo.root, u"plik ż.txt"_s, HunkAction::Discard, 0, 3, done); }).ok);
    QVERIFY(!await<Outcome>([&](auto done) { client.applyHunk(repo.root, u"plik ż.txt"_s, HunkAction::Discard, 3, 0, done); }).ok);
    QVERIFY(repo.read(u"plik ż.txt"_s).contains("LINIA 3\n"));
    o = await<Outcome>([&](auto done) { client.applyHunk(repo.root, u"plik ż.txt"_s, HunkAction::Discard, 3, 3, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QVERIFY2(o.message.startsWith(u"Threw away 1 hunk"_s), qPrintable(o.message));
    const QByteArray now = repo.read(u"plik ż.txt"_s);
    QVERIFY(now.contains("linia 3\n"));
    QVERIFY(now.contains("LINIA 25\nnowa po 25\n"));
    QCOMPARE(repo.text({u"diff"_s, u"--name-only"_s}), QString());
    // A binary file has no hunks.
    repo.write(u"obraz.bin"_s, QByteArray("\0\1\2", 3));
    repo.commitAll(u"bin"_s);
    repo.write(u"obraz.bin"_s, QByteArray("\0\1\3", 3));
    o = await<Outcome>([&](auto done) { client.applyHunk(repo.root, u"obraz.bin"_s, HunkAction::Stage, 1, 1, done); });
    QVERIFY(!o.ok);
    o = await<Outcome>([&](auto done) { client.applyHunk(repo.root, u"../x"_s, HunkAction::Stage, 1, 1, done); });
    QVERIFY(!o.ok);
}

void TestGitCliRepo::commitChosenFiles()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.write(u"b.txt"_s, "b\n");
    repo.write(u"usuń.txt"_s, "u\n");
    repo.commitAll(u"start"_s);
    repo.write(u"a.txt"_s, "A\n");
    repo.write(u"b.txt"_s, "B staged\n");
    repo.run({u"add"_s, u"b.txt"_s});
    repo.write(u"b.txt"_s, "B staged and more\n");
    repo.remove(u"usuń.txt"_s);
    repo.write(u"nowy ą.txt"_s, "n\n");
    repo.write(u"zostaje.txt"_s, "z\n");
    Client client;
    CommitRequest req;
    req.message = u"Tylko wybrane: a, usunięcie i nowy"_s;
    req.mode = u"paths"_s;
    req.paths = {u"a.txt"_s, u"usuń.txt"_s, u"nowy ą.txt"_s};
    req.untracked = {u"nowy ą.txt"_s};
    const Outcome o = await<Outcome>([&](auto done) { client.commit(repo.root, req, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    const QString committed = repo.text({u"-c"_s, u"core.quotepath=off"_s, u"show"_s, u"--name-status"_s,
                                         u"--format="_s, u"HEAD"_s});
    QVERIFY2(committed.contains(u"M\ta.txt"_s), qPrintable(committed));
    QVERIFY(committed.contains(u"D\tusuń.txt"_s));
    QVERIFY(committed.contains(u"A\tnowy ą.txt"_s));
    QVERIFY(!committed.contains(u"b.txt"_s));
    QVERIFY(!committed.contains(u"zostaje"_s));
    // b.txt is still staged as it was, with its unstaged rest; zostaje.txt still untracked.
    QCOMPARE(repo.run({u"show"_s, u":b.txt"_s}), QByteArray("B staged\n"));
    QCOMPARE(repo.read(u"b.txt"_s), QByteArray("B staged and more\n"));
    QVERIFY(repo.text({u"status"_s, u"--porcelain"_s}).contains(u"?? zostaje.txt"_s));

    // A failing commit (a hook refuses) takes the new files out of the index again.
    repo.write(u".git/hooks/pre-commit"_s, "#!/bin/sh\nexit 1\n");
    QFile::setPermissions(repo.path(u".git/hooks/pre-commit"_s), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    CommitRequest refused;
    refused.message = u"Odrzucony"_s;
    refused.mode = u"paths"_s;
    refused.paths = {u"zostaje.txt"_s};
    refused.untracked = {u"zostaje.txt"_s};
    const Outcome r = await<Outcome>([&](auto done) { client.commit(repo.root, refused, done); });
    QVERIFY(!r.ok);
    QVERIFY(repo.text({u"status"_s, u"--porcelain"_s}).contains(u"?? zostaje.txt"_s));
    CommitRequest none;
    none.message = u"x"_s;
    none.mode = u"paths"_s;
    QVERIFY(!await<Outcome>([&](auto done) { client.commit(repo.root, none, done); }).ok);
}

void TestGitCliRepo::commitStagedOnly()
{
    Repo repo;
    repo.write(u"a.txt"_s, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");
    repo.commitAll(u"start"_s);
    repo.write(u"a.txt"_s, "1\nDWA\n3\n4\n5\n6\n7\n8\n9\nDZIESIĘĆ\n");
    Client client;
    QVERIFY(await<Outcome>([&](auto done) { client.applyHunk(repo.root, u"a.txt"_s, HunkAction::Stage, 2, 2, done); }).ok);
    CommitRequest req;
    req.message = u"Tylko część pliku"_s;
    req.mode = u"staged"_s;
    req.paths = {u"a.txt"_s};
    const Outcome o = await<Outcome>([&](auto done) { client.commit(repo.root, req, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.run({u"show"_s, u"HEAD:a.txt"_s}), QByteArray("1\nDWA\n3\n4\n5\n6\n7\n8\n9\n10\n"));
    QCOMPARE(repo.read(u"a.txt"_s), QByteArray("1\nDWA\n3\n4\n5\n6\n7\n8\n9\nDZIESIĘĆ\n"));
    const Outcome nothing = await<Outcome>([&](auto done) { client.commit(repo.root, req, done); });
    QVERIFY(!nothing.ok);
}

void TestGitCliRepo::commitOnNewBranch()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"start"_s);
    repo.write(u"a.txt"_s, "b\n");
    Client client;
    CommitRequest req;
    req.message = u"Na nowej gałęzi"_s;
    req.newBranch = u"funkcja/nowa-gałąź"_s;
    const Outcome o = await<Outcome>([&](auto done) { client.commit(repo.root, req, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.text({u"symbolic-ref"_s, u"--short"_s, u"HEAD"_s}), u"funkcja/nowa-gałąź"_s);
    QCOMPARE(repo.text({u"log"_s, u"-1"_s, u"--format=%s"_s, u"main"_s}), u"start"_s);
    req.newBranch = u"zła nazwa"_s;
    QVERIFY(!await<Outcome>([&](auto done) { client.commit(repo.root, req, done); }).ok);
}

void TestGitCliRepo::commitRefusesConflictMarkers()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"start"_s);
    repo.write(u"a.txt"_s, "a\n<<<<<<< HEAD\nx\n=======\ny\n>>>>>>> inna\n");
    repo.write(u"b.txt"_s, "czysty\n");
    Client client;
    const QStringList found = await<QStringList>([&](auto done) {
        client.conflictMarkers(repo.root, {u"a.txt"_s, u"b.txt"_s, u"brak.txt"_s, u"../x"_s}, done);
    });
    QCOMPARE(found, QStringList{u"a.txt"_s});
    CommitRequest req;
    req.message = u"Z konfliktem"_s;
    req.paths = {u"a.txt"_s, u"b.txt"_s};
    const Outcome o = await<Outcome>([&](auto done) { client.commit(repo.root, req, done); });
    QVERIFY(!o.ok);
    QVERIFY2(o.message.contains(u"a.txt"_s), qPrintable(o.message));
    QVERIFY(!o.message.contains(u"b.txt"_s));
    QCOMPARE(repo.text({u"log"_s, u"-1"_s, u"--format=%s"_s}), u"start"_s);
    // Many files: found across the slices.
    QStringList many;
    for (int i = 0; i < 40; ++i) {
        const QString name = u"plik%1.txt"_s.arg(i);
        repo.write(name, i == 37 ? QByteArray(">>>>>>> koniec\n") : QByteArray("ok\n"));
        many << name;
    }
    QCOMPARE(await<QStringList>([&](auto done) { client.conflictMarkers(repo.root, many, done); }),
             QStringList{u"plik37.txt"_s});
}

// ---- Merges, rebases, stashes ------------------------------------------------------------------

void TestGitCliRepo::mergeConflictResolved()
{
    Repo repo;
    makeConflict(repo);
    int code = 0;
    repo.run({u"merge"_s, u"-q"_s, u"inna"_s}, {}, &code);
    QVERIFY(code != 0);
    Client client;
    const RepoInfo info = await<RepoInfo>([&](auto done) { client.repository(repo.root, done); });
    QCOMPARE(Client::operationIn(info.gitDir), Operation::Merge);
    QCOMPARE(Client::operationName(Operation::Merge), u"merge"_s);
    QCOMPARE(Client::operationFromName(u"cherryPick"_s), Operation::CherryPick);
    QCOMPARE(Client::operationFromName(u"x"_s), Operation::None);
    // Not yet: the files are conflicted.
    Outcome o = await<Outcome>([&](auto done) { client.continueOperation(repo.root, Operation::Merge, done); });
    QVERIFY(!o.ok);
    // Marked resolved with its markers still in: refused.
    o = await<Outcome>([&](auto done) { client.resolve(repo.root, u"wspólny.txt"_s, Resolution::Resolved, done); });
    QVERIFY(!o.ok);
    repo.write(u"wspólny.txt"_s, "jeden\nDWA z obu\ntrzy\n");
    o = await<Outcome>([&](auto done) { client.resolve(repo.root, u"wspólny.txt"_s, Resolution::Resolved, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    // Ours of a file they deleted: kept as ours.
    o = await<Outcome>([&](auto done) { client.resolve(repo.root, u"usunięty.txt"_s, Resolution::Ours, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.read(u"usunięty.txt"_s), QByteArray("zmieniony\n"));
    QCOMPARE(repo.text({u"ls-files"_s, u"-u"_s}), QString());
    o = await<Outcome>([&](auto done) { client.continueOperation(repo.root, Operation::Merge, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(Client::operationIn(info.gitDir), Operation::None);
    QCOMPARE(repo.text({u"log"_s, u"-1"_s, u"--format=%s"_s}), u"Merge branch 'inna'"_s);
    QCOMPARE(repo.text({u"rev-list"_s, u"--count"_s, u"--merges"_s, u"HEAD"_s}), u"1"_s);
    o = await<Outcome>([&](auto done) { client.resolve(repo.root, u"wspólny.txt"_s, Resolution::Theirs, done); });
    QVERIFY(!o.ok); // (no conflict any more)
}

void TestGitCliRepo::mergeAborted()
{
    Repo repo;
    makeConflict(repo);
    const QString head = repo.text({u"rev-parse"_s, u"HEAD"_s});
    repo.run({u"merge"_s, u"-q"_s, u"inna"_s}, {}, nullptr);
    Client client;
    const Outcome o = await<Outcome>([&](auto done) { client.abortOperation(repo.root, Operation::Merge, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.text({u"rev-parse"_s, u"HEAD"_s}), head);
    QCOMPARE(repo.read(u"wspólny.txt"_s), QByteArray("jeden\nDWA z main\ntrzy\n"));
    QCOMPARE(repo.text({u"status"_s, u"--porcelain"_s}), QString());
    QVERIFY(!await<Outcome>([&](auto done) { client.abortOperation(repo.root, Operation::None, done); }).ok);
}

void TestGitCliRepo::takeTheirsOfADeletion()
{
    Repo repo;
    makeConflict(repo);
    repo.run({u"merge"_s, u"-q"_s, u"inna"_s}, {}, nullptr);
    Client client;
    Outcome o = await<Outcome>([&](auto done) { client.resolve(repo.root, u"usunięty.txt"_s, Resolution::Theirs, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QVERIFY(!repo.exists(u"usunięty.txt"_s));
    o = await<Outcome>([&](auto done) { client.resolve(repo.root, u"wspólny.txt"_s, Resolution::Theirs, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.read(u"wspólny.txt"_s), QByteArray("jeden\nDWA z innej\ntrzy\n"));
    QCOMPARE(repo.text({u"ls-files"_s, u"-u"_s}), QString());
}

void TestGitCliRepo::rebaseContinued()
{
    Repo repo;
    makeConflict(repo);
    repo.run({u"switch"_s, u"-q"_s, u"inna"_s});
    int code = 0;
    repo.run({u"rebase"_s, u"main"_s}, {}, &code);
    QVERIFY(code != 0);
    Client client;
    const RepoInfo info = await<RepoInfo>([&](auto done) { client.repository(repo.root, done); });
    QCOMPARE(Client::operationIn(info.gitDir), Operation::Rebase);
    QVERIFY(await<Outcome>([&](auto done) { client.resolve(repo.root, u"wspólny.txt"_s, Resolution::Theirs, done); }).ok);
    QVERIFY(await<Outcome>([&](auto done) { client.resolve(repo.root, u"usunięty.txt"_s, Resolution::Theirs, done); }).ok);
    const Outcome o = await<Outcome>([&](auto done) { client.continueOperation(repo.root, Operation::Rebase, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(Client::operationIn(info.gitDir), Operation::None);
    QCOMPARE(repo.text({u"log"_s, u"-1"_s, u"--format=%s"_s}), u"inna zmiana"_s);
    QCOMPARE(repo.text({u"rev-parse"_s, u"HEAD~1"_s}), repo.text({u"rev-parse"_s, u"main"_s}));
}

// Skipping the commit that stopped a rebase (T3 Code's --skip): the rebase ends without it; a
// cherry-pick skips too; a merge has nothing to skip.
void TestGitCliRepo::rebaseSkipped()
{
    Repo repo;
    makeConflict(repo);
    repo.run({u"switch"_s, u"-q"_s, u"inna"_s});
    int code = 0;
    repo.run({u"rebase"_s, u"main"_s}, {}, &code);
    QVERIFY(code != 0);
    Client client;
    const RepoInfo info = await<RepoInfo>([&](auto done) { client.repository(repo.root, done); });
    QCOMPARE(Client::operationIn(info.gitDir), Operation::Rebase);
    Outcome o = await<Outcome>([&](auto done) { client.skipOperation(repo.root, Operation::Rebase, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(Client::operationIn(info.gitDir), Operation::None);
    // (Its one commit skipped: the branch is main now.)
    QCOMPARE(repo.text({u"rev-parse"_s, u"HEAD"_s}), repo.text({u"rev-parse"_s, u"main"_s}));
    QCOMPARE(repo.text({u"status"_s, u"--porcelain"_s}), QString());
    // A cherry-pick stopped by a conflict, skipped.
    repo.run({u"switch"_s, u"-q"_s, u"main"_s});
    const QString head = repo.text({u"rev-parse"_s, u"HEAD"_s});
    repo.run({u"cherry-pick"_s, u"ORIG_HEAD"_s}, {}, nullptr);
    if (Client::operationIn(info.gitDir) == Operation::CherryPick) {
        o = await<Outcome>([&](auto done) { client.skipOperation(repo.root, Operation::CherryPick, done); });
        QVERIFY2(o.ok, qPrintable(o.message));
        QCOMPARE(Client::operationIn(info.gitDir), Operation::None);
        QCOMPARE(repo.text({u"rev-parse"_s, u"HEAD"_s}), head);
    }
    // Nothing to skip in a merge, or with nothing in progress.
    QVERIFY(!await<Outcome>([&](auto done) { client.skipOperation(repo.root, Operation::Merge, done); }).ok);
    QVERIFY(!await<Outcome>([&](auto done) { client.skipOperation(repo.root, Operation::None, done); }).ok);
}

void TestGitCliRepo::cherryPickAborted()
{
    Repo repo;
    makeConflict(repo);
    const QString head = repo.text({u"rev-parse"_s, u"HEAD"_s});
    repo.run({u"cherry-pick"_s, u"inna"_s}, {}, nullptr);
    Client client;
    const RepoInfo info = await<RepoInfo>([&](auto done) { client.repository(repo.root, done); });
    QCOMPARE(Client::operationIn(info.gitDir), Operation::CherryPick);
    const Outcome o = await<Outcome>([&](auto done) { client.abortOperation(repo.root, Operation::CherryPick, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(Client::operationIn(info.gitDir), Operation::None);
    QCOMPARE(repo.text({u"rev-parse"_s, u"HEAD"_s}), head);
}

void TestGitCliRepo::stashes()
{
    Repo repo;
    repo.write(u"a.txt"_s, "a\n");
    repo.commitAll(u"a"_s);
    Client client;
    StashList list = await<StashList>([&](auto done) { client.stashes(repo.root, done); });
    QVERIFY(list.ok);
    QVERIFY(list.stashes.isEmpty());
    Outcome o = await<Outcome>([&](auto done) { client.stashPush(repo.root, u"nic"_s, done); });
    QVERIFY(!o.ok);
    repo.write(u"a.txt"_s, "zmiana\n");
    repo.write(u"nowy ż.txt"_s, "nowy\n");
    o = await<Outcome>([&](auto done) { client.stashPush(repo.root, u"Przed zmianą — ąę"_s, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.text({u"status"_s, u"--porcelain"_s}), QString());
    repo.write(u"b.txt"_s, "b\n");
    QVERIFY(await<Outcome>([&](auto done) { client.stashPush(repo.root, QString(), done); }).ok);
    list = await<StashList>([&](auto done) { client.stashes(repo.root, done); });
    QCOMPARE(list.stashes.size(), 2);
    QCOMPARE(list.stashes.at(1).message, u"On main: Przed zmianą — ąę"_s);
    QCOMPARE(list.stashes.at(1).branch, u"main"_s);
    const QString older = list.stashes.at(1).oid;
    const QString newer = list.stashes.at(0).oid;
    // Apply keeps it; pop of the older one (its index found again) drops only it.
    o = await<Outcome>([&](auto done) { client.stashApply(repo.root, newer, false, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QVERIFY(repo.exists(u"b.txt"_s));
    repo.remove(u"b.txt"_s);
    o = await<Outcome>([&](auto done) { client.stashApply(repo.root, older, true, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(repo.read(u"a.txt"_s), QByteArray("zmiana\n"));
    QCOMPARE(repo.read(u"nowy ż.txt"_s), QByteArray("nowy\n"));
    list = await<StashList>([&](auto done) { client.stashes(repo.root, done); });
    QCOMPARE(list.stashes.size(), 1);
    QCOMPARE(list.stashes.at(0).oid, newer);
    o = await<Outcome>([&](auto done) { client.stashDrop(repo.root, newer, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    list = await<StashList>([&](auto done) { client.stashes(repo.root, done); });
    QVERIFY(list.stashes.isEmpty());
    QVERIFY(!await<Outcome>([&](auto done) { client.stashDrop(repo.root, newer, done); }).ok);
    QVERIFY(!await<Outcome>([&](auto done) { client.stashApply(repo.root, u"--index"_s, false, done); }).ok);
}

QTEST_GUILESS_MAIN(TestGitCliRepo)
#include "tst_gitcli_repo.moc"
