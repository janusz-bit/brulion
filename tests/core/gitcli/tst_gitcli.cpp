// The parsers of gitcli.h, without git: `git status --porcelain=v2 --branch -z` (branch headers,
// ordinary / renamed / copied / unmerged / untracked / ignored entries, staged and unstaged
// letters, submodules), `git diff --numstat -z` and `--raw -z` (alone and together, renames,
// binary files), `git worktree list --porcelain` with and without -z, `git branch --format`, the
// URL `gh pr create` prints, and the shared rules (branch names, checkpoint ids, safe paths,
// binary data, a message's subject). Paths with spaces, tabs, newlines and Polish letters;
// garbage and truncated records never crash and are skipped.
#include "gitcli/gitcli.h"

#include <QTest>

#include <initializer_list>

using namespace Qt::StringLiterals;
using namespace brulion::gitcli;

namespace {

/// Every token followed by a NUL, as git's -z output.
QByteArray z(std::initializer_list<const char *> tokens)
{
    QByteArray out;
    for (const char *t : tokens) {
        out += QByteArray(t);
        out += '\0';
    }
    return out;
}

const char *const Oid1 = "61780798228d17af2d34fce4cfbdf35556832472";
const char *const Oid2 = "78981922613b2afb6025042ff6bd878ac1994e85";

QByteArray ordinary(const char *xy, const char *path, const char *sub = "N...")
{
    return QByteArray("1 ") + xy + ' ' + sub + " 100644 100644 100644 " + Oid1 + ' ' + Oid2 + ' ' + path;
}

} // namespace

class TestGitCli : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- status ----
    void statusBranch_data();
    void statusBranch();
    void statusEntries();
    void statusPathsAsWritten_data();
    void statusPathsAsWritten();
    void statusUnmergedAndSubmodule();
    void statusMalformed_data();
    void statusMalformed();
    void changeOfEntries_data();
    void changeOfEntries();
    void changeNamesAndLetters();

    // ---- diff ----
    void numstat_data();
    void numstat();
    void numstatMalformed_data();
    void numstatMalformed();
    void rawDiff();
    void rawAndNumstatTogether();
    void rawMalformed();

    // ---- worktrees ----
    void worktreesNul();
    void worktreesNewlines();
    void worktreesWindows();
    void worktreesMalformed();

    // ---- branches ----
    void branches();
    void branchesMalformed();

    // ---- gh ----
    void pullRequestUrl_data();
    void pullRequestUrl();

    // ---- rules ----
    void branchNames_data();
    void branchNames();
    void checkpointIds_data();
    void checkpointIds();
    void safePaths_data();
    void safePaths();
    void binary();
    void subject_data();
    void subject();
    void revisions_data();
    void revisions();

    // history, remotes, gh
    void log();
    void logMalformed();
    void stashes();
    void remotes();
    void remoteUrls_data();
    void remoteUrls();
    void forkTargets();
    void pullRequestView_data();
    void pullRequestView();
    void repositoryNames_data();
    void repositoryNames();

    // cloning
    void cloneSources_data();
    void cloneSources();
    void cloneSourcesRefused_data();
    void cloneSourcesRefused();
    void cloneProtocols_data();
    void cloneProtocols();
    void urlCredentials_data();
    void urlCredentials();
    void cloneProgress_data();
    void cloneProgress();
    void authenticationFailures_data();
    void authenticationFailures();
    void repositoryList();

    // patches
    void patchHunks();
    void patchTouching_data();
    void patchTouching();
    void patchOfSomeHunks();
    void patchOfSomeHunksReversed();
    void patchOfSomeLines();
    void patchNoNewlineAndCrlf();
    void patchMalformed();
    void patchBinary();
    void conflictMarkers_data();
    void conflictMarkers();
};

// ---- status ------------------------------------------------------------------------------------

void TestGitCli::statusBranch_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("oid");
    QTest::addColumn<QString>("head");
    QTest::addColumn<bool>("detached");
    QTest::addColumn<bool>("initial");
    QTest::addColumn<QString>("upstream");
    QTest::addColumn<int>("ahead");
    QTest::addColumn<int>("behind");
    QTest::addColumn<bool>("gone");

    QTest::newRow("tracking") << z({"# branch.oid 61780798228d17af2d34fce4cfbdf35556832472", "# branch.head main",
                                    "# branch.upstream origin/main", "# branch.ab +3 -12"})
                              << QString::fromLatin1(Oid1) << u"main"_s << false << false << u"origin/main"_s << 3 << 12
                              << false;
    QTest::newRow("no upstream") << z(
            {"# branch.oid 61780798228d17af2d34fce4cfbdf35556832472", "# branch.head funkcja/żółw"})
                                 << QString::fromLatin1(Oid1) << u"funkcja/żółw"_s << false << false << QString() << 0
                                 << 0 << false;
    QTest::newRow("initial") << z({"# branch.oid (initial)", "# branch.head main"}) << QString() << u"main"_s << false
                             << true << QString() << 0 << 0 << false;
    QTest::newRow("detached") << z(
            {"# branch.oid 61780798228d17af2d34fce4cfbdf35556832472", "# branch.head (detached)"})
                              << QString::fromLatin1(Oid1) << QString() << true << false << QString() << 0 << 0
                              << false;
    QTest::newRow("upstream gone") << z({"# branch.oid 61780798228d17af2d34fce4cfbdf35556832472", "# branch.head stara",
                                         "# branch.upstream origin/stara"})
                                   << QString::fromLatin1(Oid1) << u"stara"_s << false << false << u"origin/stara"_s
                                   << 0 << 0 << true;
    QTest::newRow("bad ab") << z({"# branch.head main", "# branch.upstream origin/main", "# branch.ab 3 12"})
                            << QString() << u"main"_s << false << false << u"origin/main"_s << 0 << 0 << true;
    QTest::newRow("negative ab") << z({"# branch.head main", "# branch.upstream o/m", "# branch.ab +-1 --2"})
                                 << QString() << u"main"_s << false << false << u"o/m"_s << 0 << 0 << true;
    QTest::newRow("empty") << QByteArray() << QString() << QString() << false << false << QString() << 0 << 0 << false;
}

void TestGitCli::statusBranch()
{
    QFETCH(QByteArray, data);
    const Status s = parseStatus(data);
    QTEST(s.branch.oid, "oid");
    QTEST(s.branch.head, "head");
    QTEST(s.branch.detached, "detached");
    QTEST(s.branch.initial, "initial");
    QTEST(s.branch.upstream, "upstream");
    QTEST(s.branch.ahead, "ahead");
    QTEST(s.branch.behind, "behind");
    QTEST(s.branch.upstreamGone(), "gone");
    QVERIFY(s.entries.isEmpty());
    QCOMPARE(s.skipped, 0);
}

void TestGitCli::statusEntries()
{
    QByteArray data = z({"# branch.oid 61780798228d17af2d34fce4cfbdf35556832472", "# branch.head main", "# stash 2"});
    data += ordinary(".M", "sub/a.txt") + '\0';
    data += ordinary("A.", "nowy plik.md") + '\0';
    data += ordinary("MM", "both.txt") + '\0';
    data += ordinary("D.", "usunięty.txt") + '\0';
    data += ordinary(".D", "gone.txt") + '\0';
    data += QByteArray("2 R. N... 100644 100644 100644 ") + Oid1 + ' ' + Oid1 + " R100 d e.txt" + '\0' + "b c.txt"
            + '\0';
    data += QByteArray("2 C. N... 100644 100644 100644 ") + Oid1 + ' ' + Oid1 + " C75 kopia.txt" + '\0' + "orig.txt"
            + '\0';
    data += z({"? zażółć gęślą.txt", "? new/deep/file", "! build/out.o"});

    const Status s = parseStatus(data);
    QCOMPARE(s.skipped, 0);
    QCOMPARE(s.stashCount, 2);
    QCOMPARE(s.entries.size(), 10);

    const StatusEntry &m = s.entries.at(0);
    QCOMPARE(m.kind, EntryKind::Ordinary);
    QCOMPARE(m.staged, '.');
    QCOMPARE(m.unstaged, 'M');
    QCOMPARE(m.path, u"sub/a.txt"_s);
    QCOMPARE(m.headMode, 0100644u);
    QCOMPARE(m.worktreeMode, 0100644u);
    QVERIFY(!m.submodule);

    QCOMPARE(s.entries.at(1).staged, 'A');
    QCOMPARE(s.entries.at(1).path, u"nowy plik.md"_s);
    QCOMPARE(s.entries.at(2).staged, 'M');
    QCOMPARE(s.entries.at(2).unstaged, 'M');
    QCOMPARE(s.entries.at(3).path, u"usunięty.txt"_s);

    const StatusEntry &r = s.entries.at(5);
    QCOMPARE(r.kind, EntryKind::Renamed);
    QCOMPARE(r.staged, 'R');
    QCOMPARE(r.path, u"d e.txt"_s);
    QCOMPARE(r.oldPath, u"b c.txt"_s);
    QCOMPARE(r.score, 100);

    const StatusEntry &c = s.entries.at(6);
    QCOMPARE(c.kind, EntryKind::Copied);
    QCOMPARE(c.path, u"kopia.txt"_s);
    QCOMPARE(c.oldPath, u"orig.txt"_s);
    QCOMPARE(c.score, 75);

    QCOMPARE(s.entries.at(7).kind, EntryKind::Untracked);
    QCOMPARE(s.entries.at(7).path, u"zażółć gęślą.txt"_s);
    QCOMPARE(s.entries.at(8).path, u"new/deep/file"_s);
    QCOMPARE(s.entries.at(9).kind, EntryKind::Ignored);
    QCOMPARE(s.entries.at(9).path, u"build/out.o"_s);
}

void TestGitCli::statusPathsAsWritten_data()
{
    QTest::addColumn<QByteArray>("path");
    QTest::newRow("spaces") << QByteArray("  spaces  around .txt");
    QTest::newRow("newline") << QByteArray("line\nbreak.txt");
    QTest::newRow("tab") << QByteArray("tab\there");
    QTest::newRow("polish") << QByteArray("Źdźbło/ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ.md");
    QTest::newRow("quotes") << QByteArray("\"quoted\" \\back.txt");
    QTest::newRow("emoji") << QByteArray("notatki/🦕.md");
    QTest::newRow("looks like a record") << QByteArray("1 .M N... x");
}

void TestGitCli::statusPathsAsWritten()
{
    QFETCH(QByteArray, path);
    const QString expected = QString::fromUtf8(path);
    QByteArray data = ordinary(".M", path.constData()) + '\0';
    data += QByteArray("? ") + path + '\0';
    data += QByteArray("2 R. N... 100644 100644 100644 ") + Oid1 + ' ' + Oid1 + " R90 " + path + '\0' + path + ".old"
            + '\0';
    const Status s = parseStatus(data);
    QCOMPARE(s.skipped, 0);
    QCOMPARE(s.entries.size(), 3);
    QCOMPARE(s.entries.at(0).path, expected);
    QCOMPARE(s.entries.at(1).path, expected);
    QCOMPARE(s.entries.at(2).path, expected);
    QCOMPARE(s.entries.at(2).oldPath, expected + u".old"_s);
}

void TestGitCli::statusUnmergedAndSubmodule()
{
    QByteArray data = QByteArray("u UU N... 100644 100644 100644 100644 ") + Oid1 + ' ' + Oid2 + ' ' + Oid1
                      + " konflikt ł.txt" + '\0';
    data += QByteArray("u AA N... 000000 100644 100644 100644 ") + Oid1 + ' ' + Oid2 + ' ' + Oid1 + " both added"
            + '\0';
    data += ordinary(".M", "vendor/lib", "SC..") + '\0';
    const Status s = parseStatus(data);
    QCOMPARE(s.skipped, 0);
    QCOMPARE(s.entries.size(), 3);
    QCOMPARE(s.entries.at(0).kind, EntryKind::Unmerged);
    QCOMPARE(s.entries.at(0).path, u"konflikt ł.txt"_s);
    QCOMPARE(s.entries.at(0).staged, 'U');
    QCOMPARE(changeOf(s.entries.at(0)), Change::Conflicted);
    QCOMPARE(s.entries.at(1).path, u"both added"_s);
    QCOMPARE(changeOf(s.entries.at(1)), Change::Conflicted);
    QVERIFY(s.entries.at(2).submodule);
    QCOMPARE(changeOf(s.entries.at(2)), Change::Modified);
}

void TestGitCli::statusMalformed_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<int>("entries");

    QTest::newRow("garbage") << QByteArray("\x01\x02\xff\xfe garbage \0 more\0\0\0", 22) << 0;
    QTest::newRow("newline output") << QByteArray("1 .M N... 100644\n? x\n") << 0;
    QTest::newRow("unknown kind") << z({"x whatever", "? fine.txt"}) << 1;
    QTest::newRow("bad xy") << (ordinary("?M", "a") + '\0' + ordinary(".M", "b") + '\0') << 1;
    QTest::newRow("short ordinary") << z({"1 .M N... 100644 100644", "? ok"}) << 1;
    QTest::newRow("no path") << (QByteArray("1 .M N... 100644 100644 100644 ") + Oid1 + ' ' + Oid2 + ' ' + '\0') << 0;
    QTest::newRow("bad mode") << (QByteArray("1 .M N... 1o0644 100644 100644 ") + Oid1 + ' ' + Oid2 + " p" + '\0') << 0;
    QTest::newRow("bad hash") << (QByteArray("1 .M N... 100644 100644 100644 zz ") + Oid2 + " p" + '\0') << 0;
    QTest::newRow("rename truncated") << (QByteArray("2 R. N... 100644 100644 100644 ") + Oid1 + ' ' + Oid1
                                          + " R100 new name")
                                      << 0;
    QTest::newRow("rename bad score") << (QByteArray("2 R. N... 100644 100644 100644 ") + Oid1 + ' ' + Oid1 + " X100 n"
                                          + '\0' + "o" + '\0')
                                      << 0;
    QTest::newRow("last record cut") << (z({"? a"}) + QByteArray("1 .M N... 1006")) << 1;
    QTest::newRow("empty untracked") << z({"? ", "?"}) << 0;
    QTest::newRow("only NULs") << QByteArray(64, '\0') << 0;
}

void TestGitCli::statusMalformed()
{
    QFETCH(QByteArray, data);
    const Status s = parseStatus(data);
    QTEST(int(s.entries.size()), "entries");
}

void TestGitCli::changeOfEntries_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<char>("x");
    QTest::addColumn<char>("y");
    QTest::addColumn<int>("change");
    const auto row = [](const char *name, EntryKind k, char x, char y, Change c) {
        QTest::newRow(name) << int(k) << x << y << int(c);
    };
    row("worktree modified", EntryKind::Ordinary, '.', 'M', Change::Modified);
    row("staged modified", EntryKind::Ordinary, 'M', '.', Change::Modified);
    row("type changed", EntryKind::Ordinary, 'T', '.', Change::Modified);
    row("added", EntryKind::Ordinary, 'A', '.', Change::Added);
    row("added then modified", EntryKind::Ordinary, 'A', 'M', Change::Added);
    row("staged deletion", EntryKind::Ordinary, 'D', '.', Change::Deleted);
    row("worktree deletion", EntryKind::Ordinary, '.', 'D', Change::Deleted);
    row("modified then deleted", EntryKind::Ordinary, 'M', 'D', Change::Deleted);
    row("renamed", EntryKind::Renamed, 'R', '.', Change::Renamed);
    row("copied", EntryKind::Copied, 'C', '.', Change::Added);
    row("unmerged", EntryKind::Unmerged, 'U', 'U', Change::Conflicted);
    row("untracked", EntryKind::Untracked, '?', '?', Change::Untracked);
    row("ignored", EntryKind::Ignored, '!', '!', Change::Ignored);
}

void TestGitCli::changeOfEntries()
{
    QFETCH(int, kind);
    QFETCH(char, x);
    QFETCH(char, y);
    StatusEntry e;
    e.kind = EntryKind(kind);
    e.staged = x;
    e.unstaged = y;
    QTEST(int(changeOf(e)), "change");
}

void TestGitCli::changeNamesAndLetters()
{
    QCOMPARE(changeName(Change::Modified), u"modified"_s);
    QCOMPARE(changeName(Change::Added), u"added"_s);
    QCOMPARE(changeName(Change::Deleted), u"deleted"_s);
    QCOMPARE(changeName(Change::Renamed), u"renamed"_s);
    QCOMPARE(changeName(Change::Untracked), u"untracked"_s);
    QCOMPARE(changeName(Change::Conflicted), u"conflicted"_s);
    QCOMPARE(changeLetter(Change::Modified), u"M"_s);
    QCOMPARE(changeLetter(Change::Added), u"A"_s);
    QCOMPARE(changeLetter(Change::Deleted), u"D"_s);
    QCOMPARE(changeLetter(Change::Renamed), u"R"_s);
    QCOMPARE(changeLetter(Change::Untracked), u"U"_s);
    QCOMPARE(changeLetter(Change::Conflicted), u"C"_s);
    QCOMPARE(changeOfRawStatus('A'), Change::Added);
    QCOMPARE(changeOfRawStatus('C'), Change::Added);
    QCOMPARE(changeOfRawStatus('D'), Change::Deleted);
    QCOMPARE(changeOfRawStatus('M'), Change::Modified);
    QCOMPARE(changeOfRawStatus('T'), Change::Modified);
    QCOMPARE(changeOfRawStatus('R'), Change::Renamed);
    QCOMPARE(changeOfRawStatus('U'), Change::Conflicted);
}

// ---- diff --------------------------------------------------------------------------------------

void TestGitCli::numstat_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QStringList>("paths");
    QTest::addColumn<QStringList>("oldPaths");
    QTest::addColumn<QList<int>>("additions");
    QTest::addColumn<QList<int>>("deletions");
    QTest::addColumn<QList<bool>>("binary");

    QTest::newRow("plain") << z({"1\t0\tsub/a.txt", "12\t3\tzażółć gęślą.txt"})
                           << QStringList{u"sub/a.txt"_s, u"zażółć gęślą.txt"_s} << QStringList{QString(), QString()}
                           << QList<int>{1, 12} << QList<int>{0, 3} << QList<bool>{false, false};
    QTest::newRow("binary") << z({"-\t-\tobraz.png"}) << QStringList{u"obraz.png"_s} << QStringList{QString()}
                            << QList<int>{0} << QList<int>{0} << QList<bool>{true};
    QTest::newRow("rename") << z({"0\t0\t", "b c.txt", "d e.txt", "5\t1\tnext"}) << QStringList{u"d e.txt"_s, u"next"_s}
                            << QStringList{u"b c.txt"_s, QString()} << QList<int>{0, 5} << QList<int>{0, 1}
                            << QList<bool>{false, false};
    QTest::newRow("tab and newline in path")
            << z({"2\t2\ta\tb\nc"}) << QStringList{u"a\tb\nc"_s} << QStringList{QString()} << QList<int>{2}
            << QList<int>{2} << QList<bool>{false};
    QTest::newRow("empty") << QByteArray() << QStringList() << QStringList() << QList<int>() << QList<int>()
                           << QList<bool>();
}

void TestGitCli::numstat()
{
    QFETCH(QByteArray, data);
    QFETCH(QStringList, paths);
    QFETCH(QStringList, oldPaths);
    QFETCH(QList<int>, additions);
    QFETCH(QList<int>, deletions);
    QFETCH(QList<bool>, binary);
    const QList<NumstatEntry> entries = parseNumstat(data);
    QCOMPARE(entries.size(), paths.size());
    for (qsizetype i = 0; i < entries.size(); ++i) {
        QCOMPARE(entries.at(i).path, paths.at(i));
        QCOMPARE(entries.at(i).oldPath, oldPaths.at(i));
        QCOMPARE(entries.at(i).additions, additions.at(i));
        QCOMPARE(entries.at(i).deletions, deletions.at(i));
        QCOMPARE(entries.at(i).binary, binary.at(i));
    }
}

void TestGitCli::numstatMalformed_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<int>("entries");
    QTest::newRow("no tabs") << z({"garbage", "1\t2\tok"}) << 1;
    QTest::newRow("one tab") << z({"1\tx"}) << 0;
    QTest::newRow("words") << z({"one\ttwo\tpath"}) << 0;
    QTest::newRow("negative") << z({"-1\t2\tpath"}) << 0;
    QTest::newRow("half binary") << z({"-\t2\tpath"}) << 0;
    QTest::newRow("rename truncated") << z({"1\t1\t", "old only"}) << 0;
    QTest::newRow("no NUL at the end") << QByteArray("3\t4\tlast") << 1;
    QTest::newRow("bytes") << QByteArray("\xff\xfe\t\x01\t\x02", 6) << 0;
}

void TestGitCli::numstatMalformed()
{
    QFETCH(QByteArray, data);
    QTEST(int(parseNumstat(data).size()), "entries");
}

void TestGitCli::rawDiff()
{
    const QByteArray data =
            z({":100644 100644 abaddc0b9edd523c69166a2c9f3a9e31a4c873e3 658dd9d519864a95c710a75d0febf5de2289663a M",
               "mod.txt",
               ":000000 100644 0000000000000000000000000000000000000000 8ba3a16384aacc37d01564b28401755ce8053f51 A",
               "new/deep/nowy plik ś.txt",
               ":100644 000000 9d80ddb4cc7365318accecc8f8084993ecf72e69 0000000000000000000000000000000000000000 D",
               "del.txt",
               ":100644 100644 61780798228d17af2d34fce4cfbdf35556832472 61780798228d17af2d34fce4cfbdf35556832472 R100",
               "b c.txt", "d e.txt",
               ":160000 160000 61780798228d17af2d34fce4cfbdf35556832472 78981922613b2afb6025042ff6bd878ac1994e85 M",
               "vendor/lib",
               ":100644 120000 61780798228d17af2d34fce4cfbdf35556832472 78981922613b2afb6025042ff6bd878ac1994e85 T",
               "link"});
    const DiffRecords d = parseDiff(data);
    QVERIFY(d.numstat.isEmpty());
    QCOMPARE(d.raw.size(), 6);
    QCOMPARE(d.raw.at(0).status, 'M');
    QCOMPARE(d.raw.at(0).path, u"mod.txt"_s);
    QCOMPARE(d.raw.at(0).oldMode, 0100644u);
    QCOMPARE(d.raw.at(0).oldOid, u"abaddc0b9edd523c69166a2c9f3a9e31a4c873e3"_s);
    QCOMPARE(d.raw.at(1).status, 'A');
    QCOMPARE(d.raw.at(1).oldMode, 0u);
    QCOMPARE(d.raw.at(1).path, u"new/deep/nowy plik ś.txt"_s);
    QCOMPARE(d.raw.at(2).status, 'D');
    QCOMPARE(d.raw.at(2).newMode, 0u);
    QCOMPARE(d.raw.at(3).status, 'R');
    QCOMPARE(d.raw.at(3).score, 100);
    QCOMPARE(d.raw.at(3).oldPath, u"b c.txt"_s);
    QCOMPARE(d.raw.at(3).path, u"d e.txt"_s);
    QCOMPARE(d.raw.at(4).newMode, GitlinkMode);
    QCOMPARE(d.raw.at(5).newMode, SymlinkMode);
    QCOMPARE(d.raw.at(5).status, 'T');
}

void TestGitCli::rawAndNumstatTogether()
{
    // `git diff-tree -r -z -M --raw --numstat` (as seen from git 2.56).
    const QByteArray data =
            z({":100644 100644 61780798228d17af2d34fce4cfbdf35556832472 61780798228d17af2d34fce4cfbdf35556832472 R100",
               "b c.txt", "d e.txt",
               ":000000 100644 0000000000000000000000000000000000000000 b68025345d5301abad4d9ec9166f455243a0d746 A",
               "zażółć gęślą.txt", "0\t0\t", "b c.txt", "d e.txt", "1\t0\tzażółć gęślą.txt"});
    const DiffRecords d = parseDiff(data);
    QCOMPARE(d.raw.size(), 2);
    QCOMPARE(d.numstat.size(), 2);
    QCOMPARE(d.raw.at(0).path, u"d e.txt"_s);
    QCOMPARE(d.raw.at(1).path, u"zażółć gęślą.txt"_s);
    QCOMPARE(d.numstat.at(0).path, u"d e.txt"_s);
    QCOMPARE(d.numstat.at(0).oldPath, u"b c.txt"_s);
    QCOMPARE(d.numstat.at(1).additions, 1);
}

void TestGitCli::rawMalformed()
{
    // A header without its path, a bad status, a short header: skipped or dropped, no crash.
    QCOMPARE(parseDiff(z({":100644 100644 abcd abcd M"})).raw.size(), 0);
    QCOMPARE(parseDiff(z({":100644 100644 abcd abcd Q", "p"})).raw.size(), 0);
    QCOMPARE(parseDiff(z({":100644 abcd", "p"})).raw.size(), 0);
    QCOMPARE(parseDiff(z({":100644 100644 abcd abcd R100", "only old"})).raw.size(), 0);
    QCOMPARE(parseDiff(z({":", ":::", "::: :"})).raw.size(), 0);
    const DiffRecords ok = parseDiff(z({":100644 100644 abcd abcd M", "p", ":junk", "1\t1\tp"}));
    QCOMPARE(ok.raw.size(), 1);
    QCOMPARE(ok.numstat.size(), 1);
}

// ---- worktrees ---------------------------------------------------------------------------------

void TestGitCli::worktreesNul()
{
    const QByteArray data =
            z({"worktree /home/ja/projekt", "HEAD e7d0f3081712448569561c1efb757927caf94140", "branch refs/heads/main",
               "", "worktree /home/ja/wt/gałąź z\nnową linią", "HEAD 61780798228d17af2d34fce4cfbdf35556832472",
               "branch refs/heads/funkcja/ćma", "locked na dysku USB", "", "worktree /tmp/odłączony",
               "HEAD 78981922613b2afb6025042ff6bd878ac1994e85", "detached",
               "prunable gitdir file points to non-existent location", "", "worktree /srv/goły.git", "bare", ""});
    const QList<Worktree> w = parseWorktrees(data);
    QCOMPARE(w.size(), 4);
    QCOMPARE(w.at(0).path, u"/home/ja/projekt"_s);
    QCOMPARE(w.at(0).branch, u"main"_s);
    QCOMPARE(w.at(0).head, u"e7d0f3081712448569561c1efb757927caf94140"_s);
    QVERIFY(!w.at(0).locked);
    QCOMPARE(w.at(1).path, u"/home/ja/wt/gałąź z\nnową linią"_s);
    QCOMPARE(w.at(1).branch, u"funkcja/ćma"_s);
    QVERIFY(w.at(1).locked);
    QCOMPARE(w.at(1).lockReason, u"na dysku USB"_s);
    QVERIFY(w.at(2).detached);
    QVERIFY(w.at(2).branch.isEmpty());
    QVERIFY(w.at(2).prunable);
    QCOMPARE(w.at(2).prunableReason, u"gitdir file points to non-existent location"_s);
    QVERIFY(w.at(3).bare);
    QVERIFY(w.at(3).head.isEmpty());
}

void TestGitCli::worktreesNewlines()
{
    const QByteArray data = "worktree /home/ja/projekt\r\nHEAD e7d0f30817\r\nbranch refs/heads/main\r\n\r\n"
                            "worktree /home/ja/inny\nHEAD 61780798\nlocked\n";
    const QList<Worktree> w = parseWorktrees(data);
    QCOMPARE(w.size(), 2);
    QCOMPARE(w.at(0).path, u"/home/ja/projekt"_s);
    QCOMPARE(w.at(0).branch, u"main"_s);
    QCOMPARE(w.at(1).path, u"/home/ja/inny"_s);
    QVERIFY(w.at(1).locked);
    QVERIFY(w.at(1).lockReason.isEmpty());
}

void TestGitCli::worktreesWindows()
{
    // git for Windows: drive letters and '/' (-z), CRLF where a tool in between made them.
    const QByteArray nul = z({"worktree C:/Users/Łucja/projekt", "HEAD e7d0f3081712448569561c1efb757927caf94140",
                              "branch refs/heads/main", "", "worktree C:/Users/Łucja/.brulion/worktrees/projekt/żółw",
                              "HEAD 6178079823bf7b4fc6ddbb1af1d5bf9a5e5e9e2c", "branch refs/heads/brulion/żółw", ""});
    const QList<Worktree> w = parseWorktrees(nul);
    QCOMPARE(w.size(), 2);
    QCOMPARE(w.at(0).path, u"C:/Users/Łucja/projekt"_s);
    QCOMPARE(w.at(1).path, u"C:/Users/Łucja/.brulion/worktrees/projekt/żółw"_s);
    QCOMPARE(w.at(1).branch, u"brulion/żółw"_s);
    const QList<Worktree> crlf =
            parseWorktrees("worktree D:/repo z spacją\r\nHEAD 61780798\r\ndetached\r\nlocked w użyciu\r\n\r\n");
    QCOMPARE(crlf.size(), 1);
    QCOMPARE(crlf.at(0).path, u"D:/repo z spacją"_s);
    QVERIFY(crlf.at(0).detached);
    QCOMPARE(crlf.at(0).lockReason, u"w użyciu"_s);
}

void TestGitCli::worktreesMalformed()
{
    QVERIFY(parseWorktrees(QByteArray()).isEmpty());
    QVERIFY(parseWorktrees(z({"HEAD abc", "branch refs/heads/x", ""})).isEmpty()); // no "worktree" line
    QVERIFY(parseWorktrees(z({"worktree ", "HEAD abc"})).isEmpty());               // no path
    QCOMPARE(parseWorktrees(z({"worktree /a", "unknown thing", "HEAD"})).size(), 1);
    QCOMPARE(parseWorktrees(QByteArray("\xff\xfe\x00worktree /b\x00", 15)).size(), 1);
}

// ---- branches ----------------------------------------------------------------------------------

void TestGitCli::branches()
{
    // Fields: refname, objectname, upstream:short, upstream:track,nobracket, HEAD, worktreepath,
    // symref — each followed by a NUL, the record by a newline.
    QByteArray data;
    const auto record = [&data](std::initializer_list<const char *> fields) {
        for (const char *f : fields) {
            data += QByteArray(f);
            data += '\0';
        }
        data += '\n';
    };
    record({"refs/heads/main", Oid1, "origin/main", "ahead 2, behind 5", "*", "/home/ja/projekt", ""});
    record({"refs/heads/funkcja/żółw", Oid2, "", "", " ", "/home/ja/wt/żółw", ""});
    record({"refs/heads/stara", Oid2, "origin/stara", "gone", " ", "", ""});
    record({"refs/heads/tylko-przed", Oid2, "origin/x", "ahead 7", " ", "", ""});
    record({"(HEAD detached at 6178079)", Oid1, "", "", " ", "", ""});
    record({"refs/remotes/origin/HEAD", Oid1, "", "", " ", "", "refs/remotes/origin/main"});
    record({"refs/remotes/origin/main", Oid1, "", "", " ", "", ""});
    record({"refs/tags/v1", Oid1, "", "", " ", "", ""});

    const QList<Branch> b = parseBranches(data);
    QCOMPARE(b.size(), 5);
    QCOMPARE(b.at(0).name, u"main"_s);
    QCOMPARE(b.at(0).refName, u"refs/heads/main"_s);
    QCOMPARE(b.at(0).oid, QString::fromLatin1(Oid1));
    QCOMPARE(b.at(0).upstream, u"origin/main"_s);
    QCOMPARE(b.at(0).ahead, 2);
    QCOMPARE(b.at(0).behind, 5);
    QVERIFY(b.at(0).current);
    QVERIFY(!b.at(0).remote);
    QCOMPARE(b.at(0).worktreePath, u"/home/ja/projekt"_s);
    QCOMPARE(b.at(1).name, u"funkcja/żółw"_s);
    QVERIFY(!b.at(1).current);
    QCOMPARE(b.at(1).worktreePath, u"/home/ja/wt/żółw"_s);
    QVERIFY(b.at(2).upstreamGone);
    QCOMPARE(b.at(3).ahead, 7);
    QCOMPARE(b.at(3).behind, 0);
    QCOMPARE(b.at(4).name, u"origin/main"_s);
    QVERIFY(b.at(4).remote);
    QVERIFY(branchFormat().contains(u"%(refname)"_s));
}

void TestGitCli::branchesMalformed()
{
    QVERIFY(parseBranches(QByteArray()).isEmpty());
    QVERIFY(parseBranches("main\nfeature\n").isEmpty());
    QVERIFY(parseBranches(z({"refs/heads/cut", "abc"})).isEmpty()); // truncated record
    QVERIFY(parseBranches(z({"refs/heads/", "a", "", "", "", "", ""})).isEmpty());
    QCOMPARE(parseBranches(z({"refs/heads/a", "a", "", "ahead x, behind", "*", "", ""})).size(), 1);
    QCOMPARE(parseBranches(QByteArray("\xff\x00\x00\x00\x00\x00\x00\x00", 8)).size(), 0);
}

// ---- gh ----------------------------------------------------------------------------------------

void TestGitCli::pullRequestUrl_data()
{
    QTest::addColumn<QString>("output");
    QTest::addColumn<QString>("url");
    QTest::newRow("plain") << u"https://github.com/ja/projekt/pull/42\n"_s
                           << u"https://github.com/ja/projekt/pull/42"_s;
    QTest::newRow("with progress")
            << u"\nCreating pull request for funkcja into main in ja/projekt\n\nhttps://github.com/ja/projekt/pull/7\n"_s
            << u"https://github.com/ja/projekt/pull/7"_s;
    QTest::newRow("already exists")
            << u"a pull request for branch \"x\" into branch \"main\" already exists:\nhttps://github.com/o/r/pull/5"_s
            << u"https://github.com/o/r/pull/5"_s;
    QTest::newRow("enterprise") << u"https://git.firma.pl/zespół/repo/pull/1234"_s
                                << u"https://git.firma.pl/zespół/repo/pull/1234"_s;
    QTest::newRow("last of two") << u"https://github.com/a/b/pull/1 then https://github.com/a/b/pull/2"_s
                                 << u"https://github.com/a/b/pull/2"_s;
    QTest::newRow("issue, not a pull") << u"https://github.com/a/b/issues/3"_s << QString();
    QTest::newRow("CRLF (Windows)") << u"Creating pull request\r\n\r\nhttps://github.com/ja/projekt/pull/8\r\n"_s
                                    << u"https://github.com/ja/projekt/pull/8"_s;
    QTest::newRow("none") << u"GraphQL: Head sha can't be blank"_s << QString();
    QTest::newRow("empty") << QString() << QString();
}

void TestGitCli::pullRequestUrl()
{
    QFETCH(QString, output);
    QTEST(parsePullRequestUrl(output), "url");
}

// ---- rules -------------------------------------------------------------------------------------

void TestGitCli::branchNames_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("valid");
    QTest::newRow("simple") << u"main"_s << true;
    QTest::newRow("slashes") << u"funkcja/nowa-rzecz"_s << true;
    QTest::newRow("polish") << u"poprawka/zażółć-gęślą"_s << true;
    QTest::newRow("remote style") << u"origin/main"_s << true;
    QTest::newRow("dots inside") << u"v1.2.3"_s << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("leading dash") << u"-f"_s << false;
    QTest::newRow("space") << u"moja gałąź"_s << false;
    QTest::newRow("double dot") << u"a..b"_s << false;
    QTest::newRow("at brace") << u"a@{1}"_s << false;
    QTest::newRow("at alone") << u"@"_s << false;
    QTest::newRow("HEAD") << u"HEAD"_s << false;
    QTest::newRow("tilde") << u"a~1"_s << false;
    QTest::newRow("caret") << u"a^"_s << false;
    QTest::newRow("colon") << u"a:b"_s << false;
    QTest::newRow("question") << u"a?"_s << false;
    QTest::newRow("star") << u"a*"_s << false;
    QTest::newRow("bracket") << u"a[b"_s << false;
    QTest::newRow("backslash") << u"a\\b"_s << false;
    QTest::newRow("control") << u"a\tb"_s << false;
    QTest::newRow("newline") << u"a\nb"_s << false;
    QTest::newRow("trailing slash") << u"a/"_s << false;
    QTest::newRow("leading slash") << u"/a"_s << false;
    QTest::newRow("double slash") << u"a//b"_s << false;
    QTest::newRow("trailing dot") << u"a."_s << false;
    QTest::newRow("dot component") << u"a/.b"_s << false;
    QTest::newRow("lock") << u"a/b.lock"_s << false;
}

void TestGitCli::branchNames()
{
    QFETCH(QString, name);
    QTEST(isValidBranchName(name), "valid");
}

void TestGitCli::checkpointIds_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<bool>("valid");
    QTest::newRow("hex") << u"0123456789abcdef0123456789abcdef"_s << true;
    QTest::newRow("thread and turn") << u"thread-7/turn_12"_s << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("dot") << u"a.b"_s << false;
    QTest::newRow("dotdot") << u".."_s << false;
    QTest::newRow("space") << u"a b"_s << false;
    QTest::newRow("polish") << u"żółw"_s << false;
    QTest::newRow("slash edges") << u"/a/"_s << false;
    QTest::newRow("double slash") << u"a//b"_s << false;
    QTest::newRow("too long") << QString(201, u'a') << false;
    QTest::newRow("ref chars") << u"a~1"_s << false;
}

void TestGitCli::checkpointIds()
{
    QFETCH(QString, id);
    QTEST(isValidCheckpointId(id), "valid");
}

void TestGitCli::safePaths_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("safe");
    QTest::newRow("file") << u"a.txt"_s << true;
    QTest::newRow("nested") << u"new/deep/nowy plik ś.txt"_s << true;
    QTest::newRow("dot file") << u".gitignore"_s << true;
    QTest::newRow("dotted name") << u"a/..b/c"_s << true;
    QTest::newRow("newline") << u"line\nbreak"_s << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("absolute") << u"/etc/passwd"_s << false;
    QTest::newRow("parent") << u"../outside"_s << false;
    QTest::newRow("parent inside") << u"a/../../b"_s << false;
    QTest::newRow("dot") << u"./a"_s << false;
    QTest::newRow("git folder") << u".git/config"_s << false;
    QTest::newRow("git folder any case") << u"sub/.GIT/hooks/x"_s << false;
    QTest::newRow("backslash") << u"a\\b"_s << false;
    QTest::newRow("drive") << u"C:/Windows"_s << false;
    QTest::newRow("drive, backslash") << u"C:\\Windows"_s << false;
    QTest::newRow("drive-relative") << u"c:plik"_s << false;
    QTest::newRow("UNC") << u"//serwer/udział/plik"_s << false;
    QTest::newRow("git folder with a dot (NTFS)") << u".git./config"_s << false;
    QTest::newRow("git folder with a space (NTFS)") << u"a/.GIT /hooks/x"_s << false;
    QTest::newRow("git folder's short name") << u"GIT~1/config"_s << false;
    QTest::newRow("short name of another folder") << u"git~2/plik"_s << true;
    QTest::newRow("gitignore") << u"a/.gitignore"_s << true;
    QTest::newRow("dots in a name") << u"wersja 1.2./plik"_s << true;
    QTest::newRow("double slash") << u"a//b"_s << false;
    QTest::newRow("trailing slash") << u"a/"_s << false;
    QTest::newRow("NUL") << QStringView(u"a\0b", 3).toString() << false;
}

void TestGitCli::safePaths()
{
    QFETCH(QString, path);
    QTEST(isSafeRelativePath(path), "safe");
}

void TestGitCli::binary()
{
    QVERIFY(!looksBinary(QByteArray()));
    QVERIFY(!looksBinary("zażółć gęślą jaźń\n"));
    QVERIFY(looksBinary(QByteArray("PNG\0\0data", 9)));
    // git looks at the first 8000 bytes only.
    QByteArray late(9000, 'a');
    late[8500] = '\0';
    QVERIFY(!looksBinary(late));
    late[7999] = '\0';
    QVERIFY(looksBinary(late));
}

void TestGitCli::subject_data()
{
    QTest::addColumn<QString>("message");
    QTest::addColumn<QString>("subject");
    QTest::newRow("one line") << u"Poprawka: łączenie bloków"_s << u"Poprawka: łączenie bloków"_s;
    QTest::newRow("body") << u"Tytuł\n\nTreść zmiany."_s << u"Tytuł"_s;
    QTest::newRow("blank first") << u"\n  \n  Właściwy tytuł  \nreszta"_s << u"Właściwy tytuł"_s;
    QTest::newRow("crlf") << u"Tytuł\r\nTreść"_s << u"Tytuł"_s;
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("blank") << u" \n\t\n"_s << QString();
}

void TestGitCli::subject()
{
    QFETCH(QString, message);
    QTEST(subjectOf(message), "subject");
}

void TestGitCli::revisions_data()
{
    QTest::addColumn<QString>("revision");
    QTest::addColumn<bool>("ok");
    QTest::newRow("HEAD") << u"HEAD"_s << true;
    QTest::newRow("oid") << u"1a2b3c4d5e6f"_s << true;
    QTest::newRow("remote branch") << u"origin/main"_s << true;
    QTest::newRow("parent") << u"HEAD~1"_s << true;
    QTest::newRow("checkpoint") << u"refs/brulion/checkpoints/abc"_s << true;
    QTest::newRow("Polish branch") << u"gałąź/żółw"_s << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("option") << u"--output=/tmp/x"_s << false;
    QTest::newRow("space") << u"a b"_s << false;
    QTest::newRow("colon") << u"HEAD:secret"_s << false;
    QTest::newRow("newline") << u"a\nb"_s << false;
    QTest::newRow("backslash") << u"a\\b"_s << false;
    QTest::newRow("too long") << QString(251, u'a') << false;
}

void TestGitCli::revisions()
{
    QFETCH(QString, revision);
    QTEST(isSafeRevision(revision), "ok");
}

// ---- history, remotes, gh ----------------------------------------------------------------------

void TestGitCli::log()
{
    QVERIFY(logFormat().startsWith(u"tformat:"_s));
    const QByteArray data = z({"1111111111111111111111111111111111111111", "2222222222222222222222222222222222222222",
                               "Zofia Żółć", "zofia@example.com", "1700000000", "Poprawka: łączenie gałęzi",
                               "\n2222222222222222222222222222222222222222", "", "Jan", "jan@example.com",
                               "1600000000", "Root",
                               "\n3333333333333333333333333333333333333333",
                               "1111111111111111111111111111111111111111 2222222222222222222222222222222222222222",
                               "Ann", "ann@example.com", "1800000000", "Merge branch 'x'"})
                            + '\n';
    const QList<Commit> commits = parseLog(data);
    QCOMPARE(commits.size(), 3);
    QCOMPARE(commits.at(0).oid, u"1111111111111111111111111111111111111111"_s);
    QCOMPARE(commits.at(0).parents, QStringList{u"2222222222222222222222222222222222222222"_s});
    QCOMPARE(commits.at(0).authorName, u"Zofia Żółć"_s);
    QCOMPARE(commits.at(0).authorEmail, u"zofia@example.com"_s);
    QCOMPARE(commits.at(0).authorTime, 1700000000);
    QCOMPARE(commits.at(0).subject, u"Poprawka: łączenie gałęzi"_s);
    QVERIFY(commits.at(1).parents.isEmpty());
    QCOMPARE(commits.at(2).parents.size(), 2);
    QCOMPARE(commits.at(2).subject, u"Merge branch 'x'"_s);
}

void TestGitCli::logMalformed()
{
    QVERIFY(parseLog("").isEmpty());
    QVERIFY(parseLog("garbage without nuls").isEmpty());
    // Not an id; a truncated record at the end.
    QVERIFY(parseLog(z({"zzzz", "", "a", "b", "1", "s", "\n4444444444444444444444444444444444444444", "", "a"})).isEmpty());
    // A time that does not read is 0, the rest stays.
    const QList<Commit> c = parseLog(z({"5555555555555555555555555555555555555555", "", "a", "b", "x", "s"}));
    QCOMPARE(c.size(), 1);
    QCOMPARE(c.at(0).authorTime, 0);
}

void TestGitCli::stashes()
{
    QVERIFY(stashFormat().startsWith(u"tformat:"_s));
    const QByteArray data = z({"stash@{0}", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "1700000000", "On main: przed zmianą",
                               "\nstash@{1}", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "1600000000",
                               "WIP on funkcja/żółw: 1a2b3c4 Coś",
                               "\nstash@{x}", "cccccccccccccccccccccccccccccccccccccccc", "1", "broken",
                               "\nstash@{2}", "dddddddddddddddddddddddddddddddddddddddd", "5", "autostash"})
                            + '\n';
    const QList<Stash> s = parseStashes(data);
    QCOMPARE(s.size(), 3);
    QCOMPARE(s.at(0).index, 0);
    QCOMPARE(s.at(0).oid, u"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_s);
    QCOMPARE(s.at(0).time, 1700000000);
    QCOMPARE(s.at(0).message, u"On main: przed zmianą"_s);
    QCOMPARE(s.at(0).branch, u"main"_s);
    QCOMPARE(s.at(1).index, 1);
    QCOMPARE(s.at(1).branch, u"funkcja/żółw"_s);
    QCOMPARE(s.at(2).index, 2);
    QCOMPARE(s.at(2).branch, QString());
    QVERIFY(parseStashes("").isEmpty());
}

void TestGitCli::remotes()
{
    const QList<Remote> r = parseRemotes("origin\tgit@github.com:ja/repo.git (fetch)\n"
                                         "origin\tgit@github.com:ja/repo.git (push)\n"
                                         "upstream\thttps://github.com/org/repo.git (fetch)\n"
                                         "upstream\tno-push (push)\r\n"
                                         "local\t/home/ja/kopia zapasowa (fetch)\n"
                                         "broken line\n"
                                         "\tno name (fetch)\n");
    QCOMPARE(r.size(), 3);
    QCOMPARE(r.at(0).name, u"origin"_s);
    QCOMPARE(r.at(0).fetchUrl, u"git@github.com:ja/repo.git"_s);
    QCOMPARE(r.at(0).pushUrl, u"git@github.com:ja/repo.git"_s);
    QCOMPARE(r.at(1).pushUrl, u"no-push"_s);
    QCOMPARE(r.at(2).fetchUrl, u"/home/ja/kopia zapasowa"_s);
    QCOMPARE(r.at(2).pushUrl, r.at(2).fetchUrl);
}

void TestGitCli::remoteUrls_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("host");
    QTest::addColumn<QString>("owner");
    QTest::addColumn<QString>("name");
    QTest::newRow("scp") << u"git@github.com:ja/repo.git"_s << u"github.com"_s << u"ja"_s << u"repo"_s;
    QTest::newRow("https") << u"https://github.com/Org/My.Repo"_s << u"github.com"_s << u"Org"_s << u"My.Repo"_s;
    QTest::newRow("https .git /") << u"https://github.com/org/repo.git/"_s << u"github.com"_s << u"org"_s << u"repo"_s;
    QTest::newRow("token") << u"https://x:ghp_secret@GitHub.com/org/repo"_s << u"github.com"_s << u"org"_s << u"repo"_s;
    QTest::newRow("ssh port") << u"ssh://git@example.org:2222/grupa/podgrupa/repo.git"_s << u"example.org"_s
                              << u"grupa/podgrupa"_s << u"repo"_s;
    QTest::newRow("git://") << u"git://github.com/a/b"_s << u"github.com"_s << u"a"_s << u"b"_s;
    QTest::newRow("local path") << u"/home/ja/repo"_s << QString() << QString() << QString();
    QTest::newRow("relative") << u"../repo"_s << QString() << QString() << QString();
    QTest::newRow("windows") << u"C:\\repos\\x"_s << QString() << QString() << QString();
    QTest::newRow("one part") << u"https://github.com/repo"_s << QString() << QString() << QString();
    QTest::newRow("empty") << QString() << QString() << QString() << QString();
}

void TestGitCli::remoteUrls()
{
    QFETCH(QString, url);
    const RepoSlug slug = parseRemoteUrl(url);
    QTEST(slug.host, "host");
    QTEST(slug.owner, "owner");
    QTEST(slug.name, "name");
}

void TestGitCli::forkTargets()
{
    const QList<Remote> fork{{u"origin"_s, u"git@github.com:ja/repo.git"_s, u"git@github.com:ja/repo.git"_s},
                             {u"upstream"_s, u"https://github.com/org/repo.git"_s, u"https://github.com/org/repo.git"_s}};
    ForkTarget t = forkTarget(fork, QString(), u"funkcja/żółw"_s);
    QCOMPARE(t.repo, u"org/repo"_s);
    QCOMPARE(t.head, u"ja:funkcja/żółw"_s);
    // Pushed to upstream itself: no fork.
    t = forkTarget(fork, u"upstream"_s, u"x"_s);
    QVERIFY(t.repo.isEmpty() && t.head.isEmpty());
    // No upstream remote, or the same repository under two names, or another host.
    QVERIFY(forkTarget({fork.at(0)}, QString(), u"x"_s).repo.isEmpty());
    const QList<Remote> same{fork.at(0), {u"upstream"_s, u"https://github.com/JA/repo"_s, u"https://github.com/JA/repo"_s}};
    QVERIFY(forkTarget(same, QString(), u"x"_s).repo.isEmpty());
    const QList<Remote> otherHost{fork.at(0), {u"upstream"_s, u"https://gitlab.com/org/repo"_s, QString()}};
    QVERIFY(forkTarget(otherHost, QString(), u"x"_s).repo.isEmpty());
    // GitHub Enterprise: the host before the repository.
    const QList<Remote> enterprise{{u"origin"_s, u"git@ghe.example.com:ja/repo.git"_s, u"git@ghe.example.com:ja/repo.git"_s},
                                   {u"upstream"_s, u"https://ghe.example.com/org/repo"_s, QString()}};
    t = forkTarget(enterprise, u"origin"_s, u"x"_s);
    QCOMPARE(t.repo, u"ghe.example.com/org/repo"_s);
    QCOMPARE(t.head, u"ja:x"_s);
    QVERIFY(forkTarget(fork, QString(), QString()).repo.isEmpty());
}

void TestGitCli::pullRequestView_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<int>("number");
    QTest::addColumn<QString>("state");
    QTest::newRow("open") << QByteArray(R"({"number":17,"url":"https://github.com/ja/repo/pull/17","state":"OPEN",)"
                                        R"("isDraft":false,"title":"Żółw","baseRefName":"main"})")
                          << 17 << u"open"_s;
    QTest::newRow("draft") << QByteArray(R"({"number":3,"url":"https://github.com/a/b/pull/3","state":"OPEN","isDraft":true})")
                           << 3 << u"draft"_s;
    QTest::newRow("merged") << QByteArray(R"({"number":4,"url":"https://github.com/a/b/pull/4","state":"MERGED","isDraft":true})")
                            << 4 << u"merged"_s;
    QTest::newRow("closed") << QByteArray(R"({"number":5,"url":"https://github.com/a/b/pull/5","state":"CLOSED"})")
                            << 5 << u"closed"_s;
    QTest::newRow("no url") << QByteArray(R"({"number":5,"state":"OPEN"})") << 0 << QString();
    QTest::newRow("not http") << QByteArray(R"j({"number":5,"url":"javascript:alert(1)","state":"OPEN"})j") << 0 << QString();
    QTest::newRow("garbage") << QByteArray("no pull requests found") << 0 << QString();
    QTest::newRow("array") << QByteArray("[1,2]") << 0 << QString();
}

void TestGitCli::pullRequestView()
{
    QFETCH(QByteArray, json);
    const PullRequestInfo pr = parsePullRequestView(json);
    QTEST(pr.number, "number");
    QTEST(pr.state, "state");
    if (pr.number == 17) {
        QCOMPARE(pr.title, u"Żółw"_s);
        QCOMPARE(pr.baseRefName, u"main"_s);
        QCOMPARE(pr.url, u"https://github.com/ja/repo/pull/17"_s);
    }
}

void TestGitCli::repositoryNames_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("ok");
    QTest::newRow("plain") << u"notatki"_s << true;
    QTest::newRow("owner") << u"ja/my-repo.v2_x"_s << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("dot") << u"."_s << false;
    QTest::newRow("dots") << u"ja/.."_s << false;
    QTest::newRow("option") << u"--public"_s << false;
    QTest::newRow("space") << u"my repo"_s << false;
    QTest::newRow("Polish") << u"notatki-żółw"_s << false;
    QTest::newRow("three parts") << u"a/b/c"_s << false;
    QTest::newRow("empty part") << u"a/"_s << false;
}

void TestGitCli::repositoryNames()
{
    QFETCH(QString, name);
    QTEST(isValidRepositoryName(name), "ok");
}

// ---- cloning -----------------------------------------------------------------------------------

void TestGitCli::cloneSources_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("host");
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("shorthand");

    QTest::newRow("owner/name") << u"janusz-bit/brulion"_s << u"https://github.com/janusz-bit/brulion.git"_s
                                << u"github.com"_s << u"janusz-bit/brulion"_s << u"brulion"_s << true;
    QTest::newRow("owner/name.git, spaces") << u"  Org/My.Repo.git "_s << u"https://github.com/Org/My.Repo.git"_s
                                            << u"github.com"_s << u"Org/My.Repo"_s << u"My.Repo"_s << true;
    QTest::newRow("no scheme") << u"github.com/ja/notatki"_s << u"https://github.com/ja/notatki.git"_s
                               << u"github.com"_s << u"ja/notatki"_s << u"notatki"_s << false;
    QTest::newRow("https") << u"https://github.com/ja/repo"_s << u"https://github.com/ja/repo.git"_s << u"github.com"_s
                           << u"ja/repo"_s << u"repo"_s << false;
    QTest::newRow("https .git") << u"https://github.com/ja/repo.git"_s << u"https://github.com/ja/repo.git"_s
                                << u"github.com"_s << u"ja/repo"_s << u"repo"_s << false;
    QTest::newRow("www") << u"https://www.github.com/ja/repo"_s << u"https://github.com/ja/repo.git"_s
                         << u"github.com"_s << u"ja/repo"_s << u"repo"_s << false;
    QTest::newRow("a page: tree") << u"https://github.com/ja/repo/tree/main/src/core"_s
                                  << u"https://github.com/ja/repo.git"_s << u"github.com"_s << u"ja/repo"_s << u"repo"_s
                                  << false;
    QTest::newRow("a page: pull") << u"https://github.com/ja/repo/pull/12/files?diff=split#x"_s
                                  << u"https://github.com/ja/repo.git"_s << u"github.com"_s << u"ja/repo"_s << u"repo"_s
                                  << false;
    QTest::newRow("a page: query") << u"https://github.com/ja/repo?tab=readme"_s << u"https://github.com/ja/repo.git"_s
                                   << u"github.com"_s << u"ja/repo"_s << u"repo"_s << false;
    QTest::newRow("gitlab subgroups") << u"https://gitlab.com/grupa/podgrupa/projekt"_s
                                      << u"https://gitlab.com/grupa/podgrupa/projekt"_s << u"gitlab.com"_s
                                      << u"grupa/podgrupa/projekt"_s << u"projekt"_s << false;
    QTest::newRow("gitlab page") << u"https://gitlab.com/grupa/projekt/-/tree/main"_s
                                 << u"https://gitlab.com/grupa/projekt"_s << u"gitlab.com"_s << u"grupa/projekt"_s
                                 << u"projekt"_s << false;
    QTest::newRow("codeberg page") << u"https://codeberg.org/ja/repo/src/branch/main/README.md"_s
                                   << u"https://codeberg.org/ja/repo"_s << u"codeberg.org"_s << u"ja/repo"_s
                                   << u"repo"_s << false;
    QTest::newRow("https with a token") << u"https://ja:ghp_x@github.com/ja/repo.git"_s
                                        << u"https://ja:ghp_x@github.com/ja/repo.git"_s << u"github.com"_s
                                        << u"ja/repo"_s << u"repo"_s << false;
    QTest::newRow("https port") << u"https://git.example.com:8443/zespół/repo.git"_s
                                << u"https://git.example.com:8443/zesp%C3%B3%C5%82/repo.git"_s << u"git.example.com"_s
                                << u"zespół/repo"_s << u"repo"_s << false;
    QTest::newRow("scp") << u"git@github.com:ja/repo.git"_s << u"git@github.com:ja/repo.git"_s << u"github.com"_s
                         << u"ja/repo"_s << u"repo"_s << false;
    QTest::newRow("scp without user") << u"example.org:team/tool"_s << u"example.org:team/tool"_s << u"example.org"_s
                                      << u"team/tool"_s << u"tool"_s << false;
    QTest::newRow("ssh url") << u"ssh://git@github.com/ja/repo.git"_s << u"ssh://git@github.com/ja/repo.git"_s
                             << u"github.com"_s << u"ja/repo"_s << u"repo"_s << false;
    QTest::newRow("ssh port") << u"ssh://git@host.example:2222/a/b.git"_s << u"ssh://git@host.example:2222/a/b.git"_s
                              << u"host.example"_s << u"a/b"_s << u"b"_s << false;
    QTest::newRow("Polish name") << u"https://gitlab.com/ja/żółć"_s
                                 << u"https://gitlab.com/ja/%C5%BC%C3%B3%C5%82%C4%87"_s << u"gitlab.com"_s
                                 << u"ja/żółć"_s << u"żółć"_s << false;
    QTest::newRow("percent-encoded") << u"https://gitlab.com/ja/%C5%BC%C3%B3%C5%82%C4%87"_s
                                     << u"https://gitlab.com/ja/%C5%BC%C3%B3%C5%82%C4%87"_s << u"gitlab.com"_s
                                     << u"ja/żółć"_s << u"żółć"_s << false;
}

void TestGitCli::cloneSources()
{
    QFETCH(QString, text);
    const CloneSource s = parseCloneSource(text);
    QVERIFY2(s.isValid(), qPrintable(s.error));
    QTEST(s.url, "url");
    QTEST(s.host, "host");
    QTEST(s.path, "path");
    QTEST(s.name, "name");
    QTEST(s.shorthand, "shorthand");
    QVERIFY(!s.local);
    QVERIFY(s.error.isEmpty());
    QCOMPARE(s.displayName(), s.host == u"github.com" ? s.path : s.host + u'/' + s.path);
}

void TestGitCli::cloneSourcesRefused_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("an option") << u"--upload-pack=touch /tmp/x"_s;
    QTest::newRow("an option after spaces") << u"  -u x"_s;
    QTest::newRow("ext transport") << u"ext::sh -c touch% /tmp/pwned"_s;
    QTest::newRow("fd transport") << u"fd::17"_s;
    QTest::newRow("host:: helper") << u"github.com::ja/repo"_s;
    QTest::newRow("ftp") << u"ftp://example.com/a/b"_s;
    QTest::newRow("owner only") << u"janusz-bit"_s;
    QTest::newRow("owner/") << u"janusz-bit/"_s;
    QTest::newRow("github owner page") << u"https://github.com/janusz-bit"_s;
    QTest::newRow("no path") << u"https://github.com"_s;
    QTest::newRow("dot dot") << u"https://example.com/a/../b"_s;
    QTest::newRow("scp dot dot") << u"git@example.com:../../etc"_s;
    QTest::newRow("a space in the path") << u"https://example.com/a b/c"_s;
    QTest::newRow("a newline") << u"https://github.com/ja/repo\n--upload-pack=x"_s;
    QTest::newRow("relative path") << u"../repo"_s;
    QTest::newRow("words") << u"zażółć gęślą jaźń"_s;
    QTest::newRow("bad port") << u"https://example.com:x/a/b"_s;
    QTest::newRow("bad host") << u"https://exa_mple!/a/b"_s;
}

void TestGitCli::cloneSourcesRefused()
{
    QFETCH(QString, text);
    const CloneSource s = parseCloneSource(text);
    QVERIFY2(!s.isValid(), qPrintable(s.url));
    QVERIFY(!s.error.isEmpty());
    QVERIFY(!parseCloneSource(u""_s).isValid());
    QVERIFY(parseCloneSource(u"   "_s).error.isEmpty()); // (nothing typed: nothing to say)
}

void TestGitCli::cloneProtocols_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("ssh");
    QTest::addColumn<QString>("expected");
    QTest::newRow("https → ssh") << u"https://github.com/ja/repo.git"_s << true << u"git@github.com:ja/repo.git"_s;
    QTest::newRow("shorthand → ssh") << u"ja/repo"_s << true << u"git@github.com:ja/repo.git"_s;
    QTest::newRow("shorthand stays https") << u"ja/repo"_s << false << u"https://github.com/ja/repo.git"_s;
    QTest::newRow("scp → https") << u"git@github.com:ja/repo.git"_s << false << u"https://github.com/ja/repo.git"_s;
    QTest::newRow("ssh url → https") << u"ssh://git@gitlab.com/g/s/p.git"_s << false
                                     << u"https://gitlab.com/g/s/p.git"_s;
    QTest::newRow("Polish → https") << u"git@gitlab.com:ja/żółć.git"_s << false
                                    << u"https://gitlab.com/ja/%C5%BC%C3%B3%C5%82%C4%87.git"_s;
    QTest::newRow("same protocol") << u"git@github.com:ja/repo.git"_s << true << u"git@github.com:ja/repo.git"_s;
    QTest::newRow("custom port") << u"ssh://git@host.example:2222/a/b.git"_s << false << QString();
    QTest::newRow("https port") << u"https://host.example:8443/a/b.git"_s << true << QString();
    QTest::newRow("local") << u"/srv/git/repo.git"_s << true << QString();
    QTest::newRow("git protocol") << u"git://example.com/a/b.git"_s << true << QString();
    QTest::newRow("garbage") << u"--x"_s << true << QString();
}

void TestGitCli::cloneProtocols()
{
    QFETCH(QString, url);
    QFETCH(bool, ssh);
    QTEST(cloneUrlForProtocol(url, ssh), "expected");
}

void TestGitCli::urlCredentials_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("expected");
    QTest::newRow("token") << u"https://ja:ghp_tajne@github.com/ja/repo.git"_s << u"https://github.com/ja/repo.git"_s;
    QTest::newRow("user only") << u"http://ja@example.com/a/b"_s << u"http://example.com/a/b"_s;
    QTest::newRow("query") << u"https://example.com/a/b.git?token=x#top"_s << u"https://example.com/a/b.git#top"_s;
    QTest::newRow("plain") << u"https://github.com/ja/repo"_s << u"https://github.com/ja/repo"_s;
    QTest::newRow("ssh keeps its user") << u"git@github.com:ja/repo.git"_s << u"git@github.com:ja/repo.git"_s;
    QTest::newRow("ssh url") << u"ssh://git@github.com/ja/repo.git"_s << u"ssh://git@github.com/ja/repo.git"_s;
}

void TestGitCli::urlCredentials()
{
    QFETCH(QString, url);
    QTEST(withoutUrlCredentials(url), "expected");
}

void TestGitCli::cloneProgress_data()
{
    QTest::addColumn<QString>("err");
    QTest::addColumn<int>("stage");
    QTest::addColumn<int>("percent");
    QTest::addColumn<QString>("detail");
    const auto stage = [](CloneStage s) { return int(s); };
    QTest::newRow("nothing") << QString() << stage(CloneStage::Connecting) << -1 << QString();
    QTest::newRow("cloning into") << u"Cloning into 'repo'...\n"_s << stage(CloneStage::Connecting) << -1 << QString();
    QTest::newRow("enumerating") << u"Cloning into 'repo'...\nremote: Enumerating objects: 1234, done.\n"_s
                                 << stage(CloneStage::Counting) << -1 << QString();
    QTest::newRow("counting") << u"remote: Counting objects:  45% (5/11)\r"_s << stage(CloneStage::Counting) << 45
                              << QString();
    QTest::newRow("receiving") << u"remote: Total 1000\nReceiving objects:  12% (120/1000)\rReceiving objects:  "
                                  "45% (450/1000), 12.40 MiB | 3.10 MiB/s\r"_s
                               << stage(CloneStage::Receiving) << 45 << u"12.40 MiB | 3.10 MiB/s"_s;
    QTest::newRow("received") << u"Receiving objects: 100% (1000/1000), 30.00 MiB | 4.00 MiB/s, done.\n"_s
                              << stage(CloneStage::Receiving) << 100 << u"30.00 MiB | 4.00 MiB/s"_s;
    QTest::newRow("resolving") << u"Receiving objects: 100% (10/10), done.\nResolving deltas:  30% (3/10)\r"_s
                               << stage(CloneStage::Resolving) << 30 << QString();
    QTest::newRow("resolved") << u"Resolving deltas: 100% (10/10), done.\n"_s << stage(CloneStage::Resolving) << 100
                              << QString();
    QTest::newRow("checkout") << u"Resolving deltas: 100% (10/10), done.\nUpdating files:  71% (500/700)\r"_s
                              << stage(CloneStage::Checkout) << 71 << QString();
    QTest::newRow("a broken line") << u"Receiving objects:  4"_s << stage(CloneStage::Receiving) << -1 << QString();
    QTest::newRow("garbage") << u"\r\r\n%%%: 999%\n"_s << stage(CloneStage::Connecting) << -1 << QString();
}

void TestGitCli::cloneProgress()
{
    QFETCH(QString, err);
    const CloneProgress p = parseCloneProgress(err);
    QTEST(int(p.stage), "stage");
    QTEST(p.percent, "percent");
    QTEST(p.detail, "detail");
    QVERIFY(!cloneStageName(p.stage).isEmpty());
}

void TestGitCli::authenticationFailures_data()
{
    QTest::addColumn<QString>("error");
    QTest::addColumn<bool>("auth");
    QTest::newRow("publickey")
            << u"git@github.com: Permission denied (publickey).\nfatal: Could not read from remote repository."_s
            << true;
    QTest::newRow("host key") << u"Host key verification failed.\nfatal: Could not read from remote repository."_s
                              << true;
    QTest::newRow("no prompt")
            << u"fatal: could not read Username for 'https://github.com': terminal prompts disabled"_s << true;
    QTest::newRow("bad token")
            << u"remote: Invalid username or password.\nfatal: Authentication failed for 'https://github.com/ja/r.git/'"_s
            << true;
    QTest::newRow("403") << u"fatal: unable to access 'https://x/': The requested URL returned error: 403"_s << true;
    QTest::newRow("not found")
            << u"remote: Repository not found.\nfatal: repository 'https://github.com/ja/nie.git/' not found"_s
            << false;
    QTest::newRow("no host") << u"fatal: unable to access 'https://nie.example/': Could not resolve host: nie.example"_s
                             << false;
    QTest::newRow("empty") << QString() << false;
}

void TestGitCli::authenticationFailures()
{
    QFETCH(QString, error);
    QTEST(isAuthenticationFailure(error), "auth");
}

void TestGitCli::repositoryList()
{
    const QByteArray json = R"([
        {"nameWithOwner":"janusz-bit/brulion","description":"Notatki  w\nMarkdown","url":"https://github.com/janusz-bit/brulion",
         "sshUrl":"git@github.com:janusz-bit/brulion.git","isPrivate":false,"isFork":false,"isArchived":false,
         "updatedAt":"2026-11-26T12:00:00Z"},
        {"nameWithOwner":"janusz-bit/prywatne","description":"","isPrivate":true,"isFork":true,"isArchived":true},
        {"nameWithOwner":"bez-nazwy"},
        {"nameWithOwner":"../../etc/passwd"},
        {"description":"no name"},
        7
    ])";
    const QList<HostedRepository> list = parseRepositoryList(json);
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).nameWithOwner, u"janusz-bit/brulion"_s);
    QCOMPARE(list.at(0).description, u"Notatki w Markdown"_s);
    QCOMPARE(list.at(0).sshUrl, u"git@github.com:janusz-bit/brulion.git"_s);
    QCOMPARE(list.at(0).updatedAt, u"2026-11-26T12:00:00Z"_s);
    QVERIFY(!list.at(0).isPrivate);
    QVERIFY(list.at(1).isPrivate);
    QVERIFY(list.at(1).isFork);
    QVERIFY(list.at(1).isArchived);
    QVERIFY(parseRepositoryList("{}").isEmpty());
    QVERIFY(parseRepositoryList("[").isEmpty());
    QVERIFY(parseRepositoryList("").isEmpty());
    QVERIFY(repositoryListFields().contains(u"nameWithOwner"_s));
}

// ---- patches -----------------------------------------------------------------------------------

namespace {

/// A file of 20 lines "1" … "20" with lines 3 and 15 changed and a line added after 9.
const QByteArray kPatch = "diff --git a/plik ż.txt b/plik ż.txt\n"
                          "index 1111111..2222222 100644\n"
                          "--- a/plik ż.txt\n"
                          "+++ b/plik ż.txt\n"
                          "@@ -1,6 +1,6 @@ sekcja\n"
                          " 1\n"
                          " 2\n"
                          "-3\n"
                          "+trzy\n"
                          " 4\n"
                          " 5\n"
                          " 6\n"
                          "@@ -7,5 +7,6 @@\n"
                          " 7\n"
                          " 8\n"
                          " 9\n"
                          "+9½\n"
                          " 10\n"
                          " 11\n"
                          "@@ -12,7 +13,6 @@\n"
                          " 12\n"
                          " 13\n"
                          " 14\n"
                          "-15\n"
                          " 16\n"
                          " 17\n"
                          " 18\n";

} // namespace

void TestGitCli::patchHunks()
{
    const FilePatch p = parsePatch(kPatch);
    QVERIFY(!p.binary);
    QVERIFY(p.header.startsWith("diff --git a/plik ż.txt"));
    QVERIFY(p.header.endsWith("+++ b/plik ż.txt\n"));
    QCOMPARE(p.hunks.size(), 3);
    const Hunk &a = p.hunks.at(0);
    QCOMPARE(a.oldStart, 1);
    QCOMPARE(a.oldLines, 6);
    QCOMPARE(a.newStart, 1);
    QCOMPARE(a.newLines, 6);
    QCOMPARE(a.section, QByteArray("sekcja"));
    QCOMPARE(a.lines.size(), 7);
    QCOMPARE(a.oldFirst, 3);
    QCOMPARE(a.oldLast, 3);
    QCOMPARE(a.newFirst, 3);
    QCOMPARE(a.newLast, 3);
    // An insertion after line 9: new line 10, before old line 10.
    const Hunk &b = p.hunks.at(1);
    QCOMPARE(b.newFirst, 10);
    QCOMPARE(b.newLast, 10);
    QCOMPARE(b.oldFirst, 10);
    QCOMPARE(b.oldLast, 10);
    // A deletion of old line 15: before new line 16 (15 + the line added above).
    const Hunk &c = p.hunks.at(2);
    QCOMPARE(c.oldFirst, 15);
    QCOMPARE(c.oldLast, 15);
    QCOMPARE(c.newFirst, 16);
    QCOMPARE(c.newLast, 16);
}

void TestGitCli::patchTouching_data()
{
    QTest::addColumn<int>("first");
    QTest::addColumn<int>("last");
    QTest::addColumn<bool>("newSide");
    QTest::addColumn<QList<int>>("hunks");
    QTest::newRow("first hunk") << 3 << 3 << true << QList<int>{0};
    QTest::newRow("context only") << 5 << 6 << true << QList<int>{};
    QTest::newRow("insertion") << 10 << 10 << true << QList<int>{1};
    QTest::newRow("all") << 1 << 30 << true << QList<int>{0, 1, 2};
    QTest::newRow("reversed range") << 16 << 3 << true << QList<int>{0, 1, 2};
    QTest::newRow("old side deletion") << 15 << 15 << false << QList<int>{2};
    QTest::newRow("old side insertion point") << 10 << 11 << false << QList<int>{1};
    QTest::newRow("beyond") << 40 << 50 << true << QList<int>{};
}

void TestGitCli::patchTouching()
{
    QFETCH(int, first);
    QFETCH(int, last);
    QFETCH(bool, newSide);
    QTEST(hunksTouching(parsePatch(kPatch), first, last, newSide), "hunks");
}

void TestGitCli::patchOfSomeHunks()
{
    const FilePatch p = parsePatch(kPatch);
    // Only the last hunk: its new start as if the insertion before it were not there.
    const QByteArray last = patchOf(p, {2});
    QVERIFY(last.startsWith(p.header));
    QVERIFY2(last.contains("\n@@ -12,7 +12,6 @@\n 12\n"), last.constData());
    QVERIFY(!last.contains("trzy"));
    // The first and the last: the first moves nothing (it adds as many lines as it deletes).
    const QByteArray two = patchOf(p, {2, 0, 2});
    QVERIFY(two.contains("@@ -1,6 +1,6 @@ sekcja\n"));
    QVERIFY(two.contains("@@ -12,7 +12,6 @@\n"));
    QVERIFY(!two.contains("9½"));
    // The insertion and the deletion: the deletion's new start moves by the inserted line.
    const QByteArray insertion = patchOf(p, {1, 2});
    QVERIFY(insertion.contains("@@ -7,5 +7,6 @@\n"));
    QVERIFY(insertion.contains("@@ -12,7 +13,6 @@\n"));
    // Everything: the patch as git wrote it.
    QCOMPARE(patchOf(p, {0, 1, 2}), kPatch);
    QVERIFY(patchOf(p, {}).isEmpty());
    QVERIFY(patchOf(p, {7}).isEmpty());
}

void TestGitCli::patchOfSomeHunksReversed()
{
    const FilePatch p = parsePatch(kPatch);
    // Reversed onto the new file (all hunks in it): the new starts stay, the old ones move as if
    // only the chosen hunks were taken out.
    const QByteArray last = patchOf(p, {2}, true);
    QVERIFY2(last.contains("\n@@ -13,7 +13,6 @@\n"), last.constData());
    const QByteArray insertion = patchOf(p, {1}, true);
    QVERIFY(insertion.contains("@@ -7,5 +7,6 @@\n"));
    QCOMPARE(patchOf(p, {0, 1, 2}, true), kPatch);
}

// Single lines of a hunk staged (forward) or thrown away (reverse): the changes outside the chosen
// lines left as they are — forward, a deletion not chosen is context and an insertion not chosen
// goes; reversed, the other way round —, the counts made again, a hunk left without a change gone.
void TestGitCli::patchOfSomeLines()
{
    const QByteArray data = "diff --git a/x b/x\n--- a/x\n+++ b/x\n"
                            "@@ -1,4 +1,4 @@\n a\n-b\n+B\n-c\n+C\n d\n"
                            "@@ -10,2 +10,3 @@\n j\n+żółw\n k\n";
    const FilePatch p = parsePatch(data);
    // The second line's change alone, forward.
    FilePatch two = linesOnly(p, 2, 2, 2, 2, false);
    QCOMPARE(two.hunks.size(), 1);
    QCOMPARE(patchOf(two, {0}), QByteArray("diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1,4 +1,4 @@\n a\n-b\n+B\n c\n d\n"));
    // Reversed: the other change stays in the file (context), its old line not brought back.
    two = linesOnly(p, 2, 2, 2, 2, true);
    QCOMPARE(patchOf(two, {0}, true), QByteArray("diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1,4 +1,4 @@\n a\n-b\n+B\n C\n d\n"));
    // Only an insertion chosen: its hunk alone; a deletion alone.
    const FilePatch inserted = linesOnly(p, 0, 0, 11, 11, false);
    QCOMPARE(inserted.hunks.size(), 1);
    QCOMPARE(inserted.hunks.at(0).lines, (QList<QByteArray>{" j", "+żółw", " k"}));
    const FilePatch deleted = linesOnly(p, 3, 3, 0, 0, false);
    QCOMPARE(deleted.hunks.at(0).lines, (QList<QByteArray>{" a", " b", "-c", " d"}));
    QCOMPARE(deleted.hunks.at(0).oldLines, 4);
    QCOMPARE(deleted.hunks.at(0).newLines, 3);
    // git's grouping of one block (-b -c +B +C): each deletion beside its insertion, so a line not
    // chosen keeps its place — forward and reversed, either line chosen.
    const FilePatch grouped = parsePatch("diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1,4 +1,4 @@\n a\n-b\n-c\n+B\n+C\n d\n");
    QCOMPARE(linesOnly(grouped, 2, 2, 2, 2, false).hunks.at(0).lines, (QList<QByteArray>{" a", "-b", "+B", " c", " d"}));
    QCOMPARE(linesOnly(grouped, 3, 3, 3, 3, false).hunks.at(0).lines, (QList<QByteArray>{" a", " b", "-c", "+C", " d"}));
    QCOMPARE(linesOnly(grouped, 3, 3, 3, 3, true).hunks.at(0).lines, (QList<QByteArray>{" a", " B", "-c", "+C", " d"}));
    QCOMPARE(linesOnly(grouped, 2, 2, 2, 2, true).hunks.at(0).lines, (QList<QByteArray>{" a", "-b", "+B", " C", " d"}));
    // Nothing chosen: nothing.
    QVERIFY(linesOnly(p, 5, 6, 5, 6, false).hunks.isEmpty());
    QVERIFY(linesOnly(p, 0, 0, 0, 0, true).hunks.isEmpty());
    // "\ No newline" stays with its line, goes with it.
    const FilePatch eof = parsePatch("diff --git a/y b/y\n--- a/y\n+++ b/y\n@@ -1,2 +1,2 @@\n a\n-b\n"
                                     "\\ No newline at end of file\n+B\n\\ No newline at end of file\n");
    QCOMPARE(linesOnly(eof, 0, 0, 2, 2, false).hunks.at(0).lines,
             (QList<QByteArray>{" a", " b", "\\ No newline at end of file", "+B", "\\ No newline at end of file"}));
}

void TestGitCli::patchNoNewlineAndCrlf()
{
    const QByteArray data = "diff --git a/x b/x\n--- a/x\n+++ b/x\n"
                            "@@ -1,2 +1,2 @@\n"
                            " jeden\r\n"
                            "-dwa\r\n"
                            "\\ No newline at end of file\n"
                            "+dwa!\r\n"
                            "\\ No newline at end of file\n";
    const FilePatch p = parsePatch(data);
    QCOMPARE(p.hunks.size(), 1);
    QCOMPARE(p.hunks.at(0).lines.size(), 5);
    QCOMPARE(p.hunks.at(0).lines.at(0), QByteArray(" jeden\r"));
    QCOMPARE(patchOf(p, {0}), data);
    // A pure insertion into an empty file and a deletion of everything.
    const FilePatch added = parsePatch("diff --git a/n b/n\nnew file mode 100644\n--- /dev/null\n+++ b/n\n"
                                       "@@ -0,0 +1,2 @@\n+a\n+b\n");
    QCOMPARE(added.hunks.size(), 1);
    QCOMPARE(added.hunks.at(0).newFirst, 1);
    QCOMPARE(added.hunks.at(0).newLast, 2);
    QCOMPARE(added.hunks.at(0).oldFirst, 1);
    const FilePatch deleted = parsePatch("diff --git a/d b/d\ndeleted file mode 100644\n--- a/d\n+++ /dev/null\n"
                                         "@@ -1,2 +0,0 @@\n-a\n-b\n");
    QCOMPARE(deleted.hunks.size(), 1);
    QCOMPARE(deleted.hunks.at(0).oldFirst, 1);
    QCOMPARE(deleted.hunks.at(0).oldLast, 2);
    QCOMPARE(deleted.hunks.at(0).newFirst, 1);
}

void TestGitCli::patchMalformed()
{
    QVERIFY(parsePatch("").hunks.isEmpty());
    QVERIFY(parsePatch("garbage\n@@ nonsense @@\n+x\n").hunks.isEmpty());
    // Counts that do not add up (truncated), then a good hunk.
    const FilePatch p = parsePatch("diff --git a/x b/x\n--- a/x\n+++ b/x\n"
                                   "@@ -1,3 +1,3 @@\n a\n-b\n"
                                   "@@ -10,1 +10,1 @@\n-x\n+y\n");
    QCOMPARE(p.hunks.size(), 1);
    QCOMPARE(p.hunks.at(0).oldStart, 10);
    // A second file is not the first one's.
    const FilePatch two = parsePatch("diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1 +1 @@\n-a\n+b\n"
                                     "diff --git a/y b/y\n--- a/y\n+++ b/y\n@@ -1 +1 @@\n-c\n+d\n");
    QCOMPARE(two.hunks.size(), 1);
    QCOMPARE(two.hunks.at(0).lines.at(1), QByteArray("+b"));
    QVERIFY(!two.header.contains("y"));
    // A context line that lost its space (an editor stripped it) still counts.
    const FilePatch stripped = parsePatch("diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1,3 +1,3 @@\n a\n\n-c\n+d\n");
    QCOMPARE(stripped.hunks.size(), 1);
    QCOMPARE(stripped.hunks.at(0).lines.at(1), QByteArray(" "));
}

void TestGitCli::patchBinary()
{
    const FilePatch p = parsePatch("diff --git a/obraz.png b/obraz.png\nindex 1..2 100644\n"
                                   "Binary files a/obraz.png and b/obraz.png differ\n");
    QVERIFY(p.binary);
    QVERIFY(p.hunks.isEmpty());
}

void TestGitCli::conflictMarkers_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<bool>("markers");
    QTest::newRow("conflict") << QByteArray("a\n<<<<<<< HEAD\nb\n=======\nc\n>>>>>>> gałąź\n") << true;
    QTest::newRow("only the end") << QByteArray("x\n>>>>>>> theirs") << true;
    QTest::newRow("bare marker") << QByteArray("<<<<<<<\n") << true;
    QTest::newRow("crlf") << QByteArray("a\r\n<<<<<<<\r\nb\r\n") << true;
    QTest::newRow("first line") << QByteArray("<<<<<<< ours\n") << true;
    QTest::newRow("indented") << QByteArray("  <<<<<<< HEAD\n") << false;
    QTest::newRow("longer marker") << QByteArray("<<<<<<<< eight\n") << false;
    QTest::newRow("in text") << QByteArray("a <<<<<<< b\n") << false;
    QTest::newRow("separator alone") << QByteArray("=======\n") << false;
    QTest::newRow("empty") << QByteArray() << false;
}

void TestGitCli::conflictMarkers()
{
    QFETCH(QByteArray, text);
    QTEST(hasConflictMarkers(text), "markers");
}

QTEST_GUILESS_MAIN(TestGitCli)
#include "tst_gitcli.moc"
