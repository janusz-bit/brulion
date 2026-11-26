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

QTEST_GUILESS_MAIN(TestGitCli)
#include "tst_gitcli.moc"
