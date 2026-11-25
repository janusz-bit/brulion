// gitcli::Client with real git in temporary repositories (skipped where git is not on PATH):
// a folder that is no repository, a deleted folder, a repository without commits, a detached
// HEAD; status with lines added / deleted (untracked files counted, the user's index never
// written); a file's two texts (modified, new, deleted, renamed, binary, too large); commit,
// push to and pull from a bare remote; branches; worktrees; pull requests through a fake `gh`
// (arguments never through a shell); checkpoints — the index, HEAD and stash untouched, the
// changes since one, the work tree put back (modified, added, deleted, renamed files, Polish
// letters and spaces in names, a nested new folder, a file where a folder was and the other way
// round, modes and symbolic links, ignored files left alone, files ignored again by the restored
// .gitignore kept), the refs dropped; time limits and no callback after the client is gone.
#include "gitcli/client.h"

#include "testtiming.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

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
            [&](auto done) { client.fileTexts(repo.root, {u"pierwszy plik.md"_s, QString(), u"HEAD"_s}, done); });
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
        return await<FileTexts>([&](auto done) { client.fileTexts(repo.root, {path, oldPath, u"HEAD"_s}, done); });
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
            [&](auto done) { client.fileTexts(repo.root, {u"tekst.md"_s, QString(), QString()}, done); });
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
#ifdef Q_OS_UNIX
    Repo repo;
    // A fake gh: writes its arguments one per line (NUL-separated) and answers like gh.
    const QString argsFile = repo.dir.filePath(u"gh-args"_s);
    const QString script = repo.dir.filePath(u"gh"_s);
    QFile s(script);
    QVERIFY(s.open(QIODevice::WriteOnly));
    s.write("#!/bin/sh\nfor a in \"$@\"; do printf '%s\\0' \"$a\"; done > \"" + argsFile.toUtf8()
            + "\"\n"
              "pwd > \""
            + argsFile.toUtf8()
            + ".pwd\"\n"
              "if [ \"$GH_PROMPT_DISABLED\" != 1 ]; then echo prompts >&2; exit 3; fi\n"
              "echo 'Creating pull request for x into main in ja/repo' >&2\n"
              "echo https://github.com/ja/repo/pull/17\n");
    s.close();
    QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    Client client;
    client.setGhProgram(script);
    PullRequest pr;
    pr.title = u"Tytuł z \"cudzysłowem\" i $(rm -rf ~)"_s;
    pr.body = u"Treść;\n`ls` && echo ąę"_s;
    pr.draft = true;
    pr.head = u"funkcja/żółw"_s;
    const Outcome o = await<Outcome>([&](auto done) { client.createPullRequest(repo.root, pr, done); });
    QVERIFY2(o.ok, qPrintable(o.message));
    QCOMPARE(o.url, u"https://github.com/ja/repo/pull/17"_s);
    QFile a(argsFile);
    QVERIFY(a.open(QIODevice::ReadOnly));
    const QList<QByteArray> args = a.readAll().split('\0');
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

    // "Already exists" is a pull request too; not signed in says how to sign in.
    QFile e(script);
    QVERIFY(e.open(QIODevice::WriteOnly | QIODevice::Truncate));
    e.write("#!/bin/sh\necho 'a pull request for branch \"x\" into branch \"main\" already exists:' >&2\n"
            "echo https://github.com/ja/repo/pull/9 >&2\nexit 1\n");
    e.close();
    const Outcome exists = await<Outcome>([&](auto done) { client.createPullRequest(repo.root, pr, done); });
    QVERIFY(exists.ok);
    QCOMPARE(exists.url, u"https://github.com/ja/repo/pull/9"_s);
    QVERIFY(e.open(QIODevice::WriteOnly | QIODevice::Truncate));
    e.write("#!/bin/sh\necho 'To get started with GitHub CLI, please run:  gh auth login' >&2\nexit 4\n");
    e.close();
    const Outcome signedOut = await<Outcome>([&](auto done) { client.createPullRequest(repo.root, pr, done); });
    QVERIFY(!signedOut.ok);
    QVERIFY2(signedOut.message.contains(u"gh auth login"_s), qPrintable(signedOut.message));

    PullRequest untitled;
    const Outcome noTitle = await<Outcome>([&](auto done) { client.createPullRequest(repo.root, untitled, done); });
    QVERIFY(!noTitle.ok);
#else
    QSKIP("a fake gh as a shell script");
#endif
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
        client.fileTexts(repo.root, {u"przed turą.txt"_s, QString(), Client::checkpointRef(u"tura-1"_s)}, done);
    });
    QVERIFY(t.ok);
    QCOMPARE(t.oldText, u"był\n"_s);
    QCOMPARE(t.newText, u"był\njest\n"_s);
    const FileTexts rt = await<FileTexts>([&](auto done) {
        client.fileTexts(repo.root, {u"nowa nazwa ś.txt"_s, u"stara nazwa.txt"_s, Client::checkpointRef(u"tura-1"_s)},
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
#ifdef Q_OS_UNIX
    Repo repo;
    const QString script = repo.dir.filePath(u"slow-git"_s);
    QFile s(script);
    QVERIFY(s.open(QIODevice::WriteOnly));
    s.write("#!/bin/sh\nexec sleep 30\n");
    s.close();
    QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    Client client;
    client.setGitProgram(script);
    client.setLocalTimeoutMs(200);
    const RunResult r = await<RunResult>([&](auto done) { client.git(repo.root, {u"status"_s}, {}, done); });
    QVERIFY(!r.ok());
    QVERIFY2(r.failure.contains(u"did not finish"_s), qPrintable(r.failure));
    QCOMPARE(client.running(), 0);
#else
    QSKIP("a slow fake git as a shell script");
#endif
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

QTEST_GUILESS_MAIN(TestGitCliRepo)
#include "tst_gitcli_repo.moc"
