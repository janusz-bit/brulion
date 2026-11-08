// RepoSync against FakeGitHub: a notebook kept in a folder of a repository — pulled (downloads,
// removals, merges, conflict copies, files left out), committed (inline text, blobs, the
// executable bit, a branch that moved meanwhile, an empty repository) and proposed as a pull
// request (a branch of the repository or of the user's fork; updated while open), with the guards
// that keep notes from being lost.
#include "fakegithub.h"

#include "github/github.h"
#include "sync/conflicts.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace brulion;
using namespace brulion::github;
using namespace Qt::StringLiterals;
using githubtest::FakeGitHub;

namespace {

const QString kRepo = u"jan/notatki"_s;

bool waitFor(const std::function<bool()> &done, int timeoutMs = 20000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs * BRULION_TEST_SLOWDOWN)
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 20);
    return done();
}

QByteArray u8(const QString &text)
{
    return text.toUtf8();
}

/// A vault in a temporary folder linked to a folder of a FakeGitHub repository.
struct Notebook {
    QTemporaryDir dir;
    std::unique_ptr<Vault> vault;
    std::unique_ptr<GitHubClient> client;
    std::unique_ptr<RepoSync> sync;
    QDateTime clock{QDate(2026, 11, 8), QTime(10, 0, 0)};

    Notebook(FakeGitHub *github, const QString &folder, bool withToken = true, const QString &repo = kRepo)
    {
        vault = std::make_unique<Vault>(dir.path() + u"/notes"_s, dir.path() + u"/state"_s);
        vault->open();
        client = std::make_unique<GitHubClient>(github);
        if (withToken)
            client->setToken(github->token);
        sync = std::make_unique<RepoSync>(vault.get(), client.get(), vault->store());
        sync->setClock([this] { return clock; });
        sync->setForkWait(1, 50);
        RepoLink link;
        link.repo = RepoId{repo.section(u'/', 0, 0), repo.section(u'/', 1)};
        link.branch = u"main"_s;
        link.folder = folder;
        sync->setLink(link);
    }

    PullReport pull()
    {
        std::optional<PullReport> report;
        sync->pull([&report](const PullReport &r) { report = r; });
        if (!waitFor([&report] { return report.has_value(); }))
            qFatal("pull did not finish");
        return *report;
    }
    CommitReport commit(const QString &message, const QStringList &paths = {})
    {
        std::optional<CommitReport> report;
        sync->commit(message, paths, [&report](const CommitReport &r) { report = r; });
        if (!waitFor([&report] { return report.has_value(); }))
            qFatal("commit did not finish");
        return *report;
    }
    CommitReport propose(const QString &title, const QString &body = {}, const QStringList &paths = {})
    {
        std::optional<CommitReport> report;
        sync->propose(title, body, paths, [&report](const CommitReport &r) { report = r; });
        if (!waitFor([&report] { return report.has_value(); }))
            qFatal("propose did not finish");
        return *report;
    }
    QByteArray read(const QString &path) { return vault->readBinary(path); }
    bool has(const QString &path) const { return vault->exists(path); }
    void write(const QString &path, const QByteArray &bytes) { QVERIFY(vault->writeBinary(path, bytes)); }
    QStringList files() const
    {
        QStringList out;
        for (const FileStat &file : vault->files())
            out.append(file.path);
        std::sort(out.begin(), out.end());
        return out;
    }
    QStringList changes()
    {
        QStringList out;
        for (const LocalChange &change : sync->localChanges())
            out.append(changeKindName(change.kind) + u' ' + change.path);
        return out;
    }
};

QMap<QString, QByteArray> sampleRepo()
{
    return {
            {u"README.md"_s, "# Notatki\n"},
            {u"src/main.cpp"_s, "int main() {}\n"},
            {u"Notatki/Zażółć gęślą.md"_s, u8(u"# Zażółć gęślą jaźń\n\n- pierwsza\n- druga\n- trzecia\n"_s)},
            {u"Notatki/Dziennik/2026-11-07.md"_s, "- wczoraj\n"},
            {u"Notatki/obrazy/kot.png"_s, QByteArray("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16)},
            {u"Notatki/.gitignore"_s, "*.tmp\n"},
            {u"Notatki/.obsidian/app.json"_s, "{}\n"},
            {u"Notatki/.github/notes.yml"_s, "on: push\n"},
    };
}

} // namespace

class TestGitHubSync : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void firstPullDownloadsTheFolder();
    void unmovedBranchCostsOneRequest();
    void changesOnGitHubArrive();
    void renamedOnGitHub();
    void commitWritesTheBranch();
    void bothSidesMerged();
    void conflictKeepsBothVersions();
    void removedHereChangedThere();
    void changedHereRemovedThere();
    void withoutToken();
    void branchMovedMeanwhile();
    void proposeFromFork();
    void proposeInOwnRepository();
    void emptyRepository();
    void lfsPointersLeftOut();
    void remoteEmptyRefused();
    void corruptDownloadNotWritten();
    void executableBitKept();
    void wholeRepository();
    void guards();
};

void TestGitHubSync::firstPullDownloadsTheFolder()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, sampleRepo());
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.sync->hasPulled());

    const PullReport report = nb.pull();
    QVERIFY2(!report.failed(), qPrintable(report.error.message + report.refused));
    QCOMPARE(report.downloaded, 3);
    QCOMPARE(report.commit, github.head(kRepo, u"main"_s));
    // Only the folder's notes: hidden files and .github, .obsidian are the repository's.
    QCOMPARE(nb.files(), (QStringList{u"Dziennik/2026-11-07.md"_s, u"Zażółć gęślą.md"_s, u"obrazy/kot.png"_s}));
    QCOMPARE(nb.read(u"Zażółć gęślą.md"_s), github.file(kRepo, u"main"_s, u"Notatki/Zażółć gęślą.md"_s).value());
    QCOMPARE(nb.read(u"obrazy/kot.png"_s), github.file(kRepo, u"main"_s, u"Notatki/obrazy/kot.png"_s).value());
    QVERIFY(nb.sync->hasPulled());
    QCOMPARE(nb.sync->headCommit(), report.commit);
    QVERIFY(nb.sync->lastPullMs() > 0);
    QVERIFY(nb.changes().isEmpty());
    // The branch, the parent folder's listing, the folder's tree, three blobs.
    QCOMPARE(github.count("GET", u"/branches/main"_s), 1);
    QCOMPARE(github.count("GET", u"/contents"_s), 1);
    QCOMPARE(github.count("GET", u"/git/trees/"_s), 1);
    QCOMPARE(github.count("GET", u"/git/blobs/"_s), 3);
    QCOMPARE(github.count({}, {}, u"raw.githubusercontent.com"_s), 0);
}

void TestGitHubSync::unmovedBranchCostsOneRequest()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, sampleRepo());
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.pull().failed());
    github.clearLog();
    const PullReport again = nb.pull();
    QVERIFY(!again.failed());
    QVERIFY(again.upToDate);
    QCOMPARE(again.downloaded, 0);
    QCOMPARE(github.requests.size(), 1); // If-None-Match: 304
    // A change elsewhere in the repository: the folder's tree is the same, not listed again.
    github.push(kRepo, u"main"_s, {{u"src/main.cpp"_s, QByteArray("int main() { return 0; }\n")}});
    github.clearLog();
    const PullReport elsewhere = nb.pull();
    QVERIFY(!elsewhere.failed());
    QVERIFY(!elsewhere.upToDate);
    QCOMPARE(elsewhere.downloaded, 0);
    QCOMPARE(github.count("GET", u"/git/trees/"_s), 0);
    QCOMPARE(github.requests.size(), 2); // the branch, the parent folder
}

void TestGitHubSync::changesOnGitHubArrive()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, sampleRepo());
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.pull().failed());
    github.push(kRepo, u"main"_s,
                {{u"Notatki/Zażółć gęślą.md"_s,
                  u8(u"# Zażółć gęślą jaźń\n\n- pierwsza\n- druga\n- trzecia\n- czwarta\n"_s)},
                 {u"Notatki/Projekty/Nowy dom.md"_s, u8(u"# Nowy dom\n"_s)},
                 {u"Notatki/Dziennik/2026-11-07.md"_s, std::nullopt}});
    const PullReport report = nb.pull();
    QVERIFY(!report.failed());
    QCOMPARE(report.downloaded, 2);
    QCOMPARE(report.deletedLocal, 1);
    QCOMPARE(nb.read(u"Projekty/Nowy dom.md"_s), u8(u"# Nowy dom\n"_s));
    QVERIFY(nb.read(u"Zażółć gęślą.md"_s).endsWith("- czwarta\n"));
    QVERIFY(!nb.has(u"Dziennik/2026-11-07.md"_s));
    // In the trash, and its emptied folder is gone (Git keeps no empty folders).
    bool trashed = false;
    for (const FileStat &file : nb.vault->trash())
        trashed = trashed || file.path.contains(u"2026-11-07"_s);
    QVERIFY(trashed);
    QVERIFY(!nb.vault->stat(u"Dziennik"_s));
    QVERIFY(nb.changes().isEmpty());
}

void TestGitHubSync::renamedOnGitHub()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s,
                      {{u"Notatki/Stara.md"_s, u8(u"treść\n"_s)},
                       {u"Notatki/Inna.md"_s, "inna\n"},
                       {u"Notatki/Kopia 1.md"_s, "to samo\n"},
                       {u"Notatki/Kopia 2.md"_s, "to samo\n"}});
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.pull().failed());
    // Moved on GitHub (another device, the web): moved here too — its history and favourite follow.
    github.push(kRepo, u"main"_s,
                {{u"Notatki/Stara.md"_s, std::nullopt},
                 {u"Notatki/Archiwum/Nowa.md"_s, u8(u"treść\n"_s)},
                 {u"Notatki/Kopia 1.md"_s, std::nullopt},
                 {u"Notatki/Kopia 3.md"_s, "to samo\n"}});
    github.clearLog();
    const PullReport report = nb.pull();
    QVERIFY(!report.failed());
    // One to one by content: a file whose content another file has too is moved as well (one
    // removed, one added), never two.
    QCOMPARE(report.renamed, 2);
    QCOMPARE(report.downloaded, 0);
    QCOMPARE(report.deletedLocal, 0);
    QCOMPARE(github.count("GET", u"/git/blobs/"_s), 0);
    QVERIFY(!nb.has(u"Stara.md"_s));
    QCOMPARE(nb.read(u"Archiwum/Nowa.md"_s), u8(u"treść\n"_s));
    QVERIFY(!nb.has(u"Kopia 1.md"_s));
    QCOMPARE(nb.read(u"Kopia 3.md"_s), QByteArray("to samo\n"));
    QCOMPARE(nb.read(u"Kopia 2.md"_s), QByteArray("to samo\n"));
    QVERIFY(nb.vault->trash().isEmpty());
    QVERIFY(nb.changes().isEmpty());

    // Two files of the same content gone and two arriving: no guess — downloaded and removed.
    github.push(kRepo, u"main"_s,
                {{u"Notatki/Kopia 2.md"_s, std::nullopt},
                 {u"Notatki/Kopia 3.md"_s, std::nullopt},
                 {u"Notatki/A.md"_s, "to samo\n"},
                 {u"Notatki/B.md"_s, "to samo\n"}});
    const PullReport ambiguous = nb.pull();
    QVERIFY(!ambiguous.failed());
    QCOMPARE(ambiguous.renamed, 0);
    QCOMPARE(ambiguous.downloaded, 2);
    QCOMPARE(ambiguous.deletedLocal, 2);
    QVERIFY(nb.changes().isEmpty());
}

void TestGitHubSync::commitWritesTheBranch()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, sampleRepo());
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.pull().failed());
    const QString before = github.head(kRepo, u"main"_s);

    nb.write(u"Zażółć gęślą.md"_s, u8(u"# Zażółć gęślą jaźń\n\n- pierwsza\n- druga (poprawiona)\n- trzecia\n"_s));
    nb.write(u"Pomysły.md"_s, u8(u"# Pomysły\r\n\r\n- ogród\r\n"_s)); // CRLF kept
    nb.write(u"obrazy/pies.png"_s, QByteArray("\x89PNG\r\n\x1a\n\x01\x02", 10));
    QVERIFY(nb.vault->remove(u"Dziennik/2026-11-07.md"_s));
    QCOMPARE(nb.changes(), (QStringList{u"deleted Dziennik/2026-11-07.md"_s, u"added Pomysły.md"_s,
                                        u"modified Zażółć gęślą.md"_s, u"added obrazy/pies.png"_s}));

    github.clearLog();
    const CommitReport report = nb.commit(u"Poprawki z Brulionu"_s);
    QVERIFY2(!report.failed(), qPrintable(report.error.message + report.refused));
    QCOMPARE(report.committed.size(), 4);
    const QString after = github.head(kRepo, u"main"_s);
    QCOMPARE(report.commit, after);
    QCOMPARE(github.messageOf(after), u"Poprawki z Brulionu"_s);
    QCOMPARE(github.parentsOf(after), QStringList{before});
    QCOMPARE(github.file(kRepo, u"main"_s, u"Notatki/Pomysły.md"_s).value(), u8(u"# Pomysły\r\n\r\n- ogród\r\n"_s));
    QVERIFY(github.file(kRepo, u"main"_s, u"Notatki/Zażółć gęślą.md"_s)->contains("poprawiona"));
    QCOMPARE(github.file(kRepo, u"main"_s, u"Notatki/obrazy/pies.png"_s).value(),
             QByteArray("\x89PNG\r\n\x1a\n\x01\x02", 10));
    QVERIFY(!github.file(kRepo, u"main"_s, u"Notatki/Dziennik/2026-11-07.md"_s));
    // Untouched: the rest of the repository and the folder's hidden files.
    QCOMPARE(github.file(kRepo, u"main"_s, u"Notatki/.gitignore"_s).value(), QByteArray("*.tmp\n"));
    QCOMPARE(github.file(kRepo, u"main"_s, u"src/main.cpp"_s).value(), QByteArray("int main() {}\n"));
    // Text travels inline in the tree; only the picture is uploaded as a blob.
    QCOMPARE(github.count("POST", u"/git/blobs"_s), 1);
    QCOMPARE(github.count("POST", u"/git/trees"_s), 1);
    QCOMPARE(github.count("POST", u"/git/commits"_s), 1);
    QCOMPARE(github.count("PATCH", u"/git/refs/heads/main"_s), 1);

    QVERIFY(nb.changes().isEmpty());
    QCOMPARE(nb.sync->headCommit(), after);
    const PullReport next = nb.pull();
    QVERIFY(!next.failed());
    QCOMPARE(next.downloaded, 0);
    QCOMPARE(next.deletedLocal, 0);
    QVERIFY(nb.changes().isEmpty());

    // Only the selected files.
    nb.write(u"Pomysły.md"_s, u8(u"# Pomysły\r\n\r\n- ogród\r\n- taras\r\n"_s));
    nb.write(u"Zażółć gęślą.md"_s, u8(u"# Zmienione\n"_s));
    const CommitReport partial = nb.commit(u"Tylko pomysły"_s, {u"Pomysły.md"_s});
    QVERIFY(!partial.failed());
    QCOMPARE(partial.committed, QList<VaultPath>{u"Pomysły.md"_s});
    QCOMPARE(nb.changes(), QStringList{u"modified Zażółć gęślą.md"_s});
    QVERIFY(github.file(kRepo, u"main"_s, u"Notatki/Pomysły.md"_s)->contains("taras"));
    QVERIFY(github.file(kRepo, u"main"_s, u"Notatki/Zażółć gęślą.md"_s)->contains("poprawiona"));

    // Nothing to commit.
    QVERIFY(nb.vault->writeBinary(u"Zażółć gęślą.md"_s,
                                  github.file(kRepo, u"main"_s, u"Notatki/Zażółć gęślą.md"_s).value()));
    const CommitReport nothing = nb.commit(u"Nic"_s);
    QVERIFY(!nothing.failed());
    QVERIFY(nothing.nothingToCommit);
    QVERIFY(nothing.commit.isEmpty());
}

void TestGitHubSync::bothSidesMerged()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, {{u"Notatki/Lista.md"_s, "- a\n- b\n- c\n- d\n- e\n"}});
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.pull().failed());
    github.push(kRepo, u"main"_s, {{u"Notatki/Lista.md"_s, QByteArray("- A (GitHub)\n- b\n- c\n- d\n- e\n")}});
    nb.write(u"Lista.md"_s, "- a\n- b\n- c\n- d\n- E (tutaj)\n");
    const PullReport report = nb.pull();
    QVERIFY(!report.failed());
    QCOMPARE(report.merged, 1);
    QVERIFY(report.conflicts.isEmpty());
    QCOMPARE(nb.read(u"Lista.md"_s), QByteArray("- A (GitHub)\n- b\n- c\n- d\n- E (tutaj)\n"));
    QCOMPARE(nb.changes(), QStringList{u"modified Lista.md"_s});
    QVERIFY(!nb.commit(u"Scalone"_s).failed());
    QCOMPARE(github.file(kRepo, u"main"_s, u"Notatki/Lista.md"_s).value(),
             QByteArray("- A (GitHub)\n- b\n- c\n- d\n- E (tutaj)\n"));
}

void TestGitHubSync::conflictKeepsBothVersions()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, {{u"Notatki/Plan.md"_s, "Spotkanie o 9\n"}});
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.pull().failed());
    github.push(kRepo, u"main"_s, {{u"Notatki/Plan.md"_s, QByteArray("Spotkanie o 10\n")}});
    nb.write(u"Plan.md"_s, "Spotkanie o 11\n");

    const PullReport report = nb.pull();
    QVERIFY(!report.failed());
    QCOMPARE(report.conflicts, QList<VaultPath>{u"Plan.md"_s});
    QCOMPARE(report.conflictCopies.size(), 1);
    const VaultPath copy = report.conflictCopies.first();
    QVERIFY2(copy.startsWith(u"Plan.conflict-GitHub-20261108-100000"_s), qPrintable(copy));
    QCOMPARE(nb.read(copy), QByteArray("Spotkanie o 10\n"));
    QCOMPARE(nb.read(u"Plan.md"_s), QByteArray("Spotkanie o 11\n")); // this device's version kept
    const std::optional<sync::BrulionConflict> parsed = sync::parseBrulionConflict(copy);
    QVERIFY(parsed);
    QCOMPARE(parsed->device, u"GitHub"_s);
    // The copy stays here: never a change to commit.
    QCOMPARE(nb.changes(), QStringList{u"modified Plan.md"_s});
    // A pull again makes no second copy.
    QCOMPARE(nb.pull().conflictCopies.size(), 0);

    const CommitReport refused = nb.commit(u"Za wcześnie"_s);
    QCOMPARE(refused.refused, u"conflicts"_s);
    QCOMPARE(refused.conflictsPending, QList<VaultPath>{u"Plan.md"_s});
    QCOMPARE(github.file(kRepo, u"main"_s, u"Notatki/Plan.md"_s).value(), QByteArray("Spotkanie o 10\n"));

    // Merged by the user (the sync center: written into the original, the copy to the trash).
    nb.write(u"Plan.md"_s, "Spotkanie o 10, potem o 11\n");
    QVERIFY(nb.vault->remove(copy));
    const CommitReport resolved = nb.commit(u"Scalony plan"_s);
    QVERIFY2(!resolved.failed(), qPrintable(resolved.refused + resolved.error.message));
    QCOMPARE(github.file(kRepo, u"main"_s, u"Notatki/Plan.md"_s).value(), QByteArray("Spotkanie o 10, potem o 11\n"));
}

void TestGitHubSync::removedHereChangedThere()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, {{u"Notatki/A.md"_s, "a\n"}, {u"Notatki/B.md"_s, "b\n"}});
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.pull().failed());
    QVERIFY(nb.vault->remove(u"A.md"_s));
    github.push(kRepo, u"main"_s, {{u"Notatki/A.md"_s, QByteArray("a, zmienione na GitHubie\n")}});
    const PullReport report = nb.pull();
    QVERIFY(!report.failed());
    QCOMPARE(report.downloaded, 1); // the change wins over the removal: restored
    QCOMPARE(nb.read(u"A.md"_s), QByteArray("a, zmienione na GitHubie\n"));
    // Removed here only: a removal to commit.
    QVERIFY(nb.vault->remove(u"B.md"_s));
    QVERIFY(!nb.pull().failed());
    QVERIFY(!nb.has(u"B.md"_s));
    QCOMPARE(nb.changes(), QStringList{u"deleted B.md"_s});
    QVERIFY(!nb.commit(u"Bez B"_s).failed());
    QVERIFY(!github.file(kRepo, u"main"_s, u"Notatki/B.md"_s));
}

void TestGitHubSync::changedHereRemovedThere()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, {{u"Notatki/A.md"_s, "a\n"}, {u"Notatki/B.md"_s, "b\n"}});
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.pull().failed());
    nb.write(u"A.md"_s, "a, zmienione tutaj\n");
    github.push(kRepo, u"main"_s, {{u"Notatki/A.md"_s, std::nullopt}});
    const PullReport report = nb.pull();
    QVERIFY(!report.failed());
    QCOMPARE(report.kept, 1);
    QCOMPARE(report.deletedLocal, 0);
    QCOMPARE(nb.read(u"A.md"_s), QByteArray("a, zmienione tutaj\n"));
    QCOMPARE(nb.changes(), QStringList{u"added A.md"_s});
    QVERIFY(!nb.commit(u"Z powrotem"_s).failed());
    QCOMPARE(github.file(kRepo, u"main"_s, u"Notatki/A.md"_s).value(), QByteArray("a, zmienione tutaj\n"));
}

void TestGitHubSync::withoutToken()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, sampleRepo());
    github.anonymousLimit = 3;
    Notebook nb(&github, u"Notatki"_s, false);
    const PullReport report = nb.pull();
    QVERIFY2(!report.failed(), qPrintable(report.error.message));
    QCOMPARE(report.downloaded, 3);
    // Files from raw.githubusercontent.com by the commit; the API only for the branch and trees.
    QCOMPARE(github.count("GET", {}, u"raw.githubusercontent.com"_s), 3);
    QCOMPARE(github.count("GET", u"/git/blobs/"_s), 0);
    for (const FakeGitHub::LoggedRequest &request : std::as_const(github.requests)) {
        QVERIFY(!request.authorized);
        if (request.host == u"raw.githubusercontent.com"_s)
            QVERIFY2(request.path.startsWith(u"/jan/notatki/"_s + report.commit + u"/Notatki/"_s),
                     qPrintable(request.path));
    }
    nb.write(u"Nowa.md"_s, "x\n");
    const CommitReport commit = nb.commit(u"Bez tokenu"_s);
    QCOMPARE(commit.refused, u"no-token"_s);
    QCOMPARE(nb.propose(u"Bez tokenu"_s).refused, u"no-token"_s);

    // The limit used up: the pull fails and says so.
    github.push(kRepo, u"main"_s, {{u"Notatki/Nowa2.md"_s, QByteArray("y\n")}});
    github.anonymousLimit = 0;
    const PullReport limited = nb.pull();
    QCOMPARE(limited.error.code, GitHubError::Code::RateLimited);
    QCOMPARE(limited.error.resetAt, 1793955600000LL);
    QVERIFY(!nb.has(u"Nowa2.md"_s));
}

void TestGitHubSync::branchMovedMeanwhile()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, {{u"Notatki/Lista.md"_s, "- a\n- b\n- c\n"}});
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.pull().failed());
    nb.write(u"Lista.md"_s, "- a\n- b\n- c\n- d (tutaj)\n");
    // Someone pushes right before Brulion moves the branch.
    bool pushed = false;
    github.hook = [&github, &pushed](const sync::HttpRequest &request,
                                     const FakeGitHub::LoggedRequest &) -> std::optional<sync::HttpResponse> {
        if (request.method == "PATCH" && !pushed) {
            pushed = true;
            github.push(kRepo, u"main"_s,
                        {{u"Notatki/Lista.md"_s, QByteArray("- A (inny)\n- b\n- c\n")},
                         {u"Notatki/Inny.md"_s, QByteArray("inny\n")}});
        }
        return std::nullopt;
    };
    const CommitReport report = nb.commit(u"Po kolei"_s);
    QVERIFY2(!report.failed(), qPrintable(report.error.message + report.refused));
    QCOMPARE(github.count("PATCH", u"/git/refs/heads/main"_s), 2);
    QCOMPARE(github.file(kRepo, u"main"_s, u"Notatki/Lista.md"_s).value(),
             QByteArray("- A (inny)\n- b\n- c\n- d (tutaj)\n"));
    QCOMPARE(nb.read(u"Inny.md"_s), QByteArray("inny\n"));
    QCOMPARE(report.pull.merged, 1);
    QVERIFY(nb.changes().isEmpty());
}

void TestGitHubSync::proposeFromFork()
{
    FakeGitHub github;
    github.createRepo(u"janusz-bit"_s, u"brulion"_s,
                      {{u"src/app/shell/guide/pl/Witaj.md"_s, u8(u"# Witaj\n\nTo jest dokumentacja.\n"_s)},
                       {u"src/app/shell/guide/pl/Synchronizacja.md"_s, u8(u"# Synchronizacja\n"_s)},
                       {u"src/main.cpp"_s, "int main() {}\n"}});
    github.forkDelay = 2;
    Notebook nb(&github, u"src/app/shell/guide/pl"_s, true, u"janusz-bit/brulion"_s);
    QVERIFY(!nb.pull().failed());
    QCOMPARE(nb.files(), (QStringList{u"Synchronizacja.md"_s, u"Witaj.md"_s}));
    nb.write(u"Witaj.md"_s, u8(u"# Witaj\n\nTo jest dokumentacja Brulionu.\n"_s));

    // The token may not push there: a commit is refused, a proposal goes through a fork.
    QCOMPARE(nb.commit(u"Nie wolno"_s).refused, u"read-only"_s);
    QVERIFY(nb.sync->repository());
    QVERIFY(!nb.sync->repository()->canPush);
    const CommitReport report = nb.propose(u"Popraw zdanie powitalne"_s, u"Dopisane słowo „Brulionu”."_s);
    QVERIFY2(!report.failed(), qPrintable(report.error.message + report.refused));
    QVERIFY(report.forked);
    QVERIFY(report.proposal);
    QCOMPARE(report.proposal->number, 1);
    QCOMPARE(report.proposal->state, u"open"_s);
    QCOMPARE(report.proposal->head.fullName(), u"jan/brulion"_s);
    QCOMPARE(report.proposal->branch, u"brulion/20261108-100000"_s);
    QCOMPARE(report.proposal->url.toString(), u"https://github.com/janusz-bit/brulion/pull/1"_s);
    QCOMPARE(github.pullRequests.size(), 1);
    QCOMPARE(github.pullRequests.first().head, u"jan:brulion/20261108-100000"_s);
    QCOMPARE(github.pullRequests.first().base, u"main"_s);
    QCOMPARE(github.pullRequests.first().title, u"Popraw zdanie powitalne"_s);
    QCOMPARE(github.pullRequests.first().body, u"Dopisane słowo „Brulionu”."_s);
    QVERIFY(github.file(u"jan/brulion"_s, u"brulion/20261108-100000"_s, u"src/app/shell/guide/pl/Witaj.md"_s)
                    ->contains("Brulionu"));
    // The repository itself is untouched; the change stays a change here, marked proposed.
    QVERIFY(!github.file(u"janusz-bit/brulion"_s, u"main"_s, u"src/app/shell/guide/pl/Witaj.md"_s)
                     ->contains("Brulionu"));
    const QList<LocalChange> changes = nb.sync->localChanges();
    QCOMPARE(changes.size(), 1);
    QVERIFY(changes.first().proposed);

    // Proposing again updates the same pull request: one more commit on its branch.
    const QString first = report.proposal->commit;
    nb.write(u"Synchronizacja.md"_s, u8(u"# Synchronizacja\n\nSyncthing albo Nextcloud.\n"_s));
    nb.clock = nb.clock.addSecs(60);
    const CommitReport update = nb.propose(u"Dopisz sposoby synchronizacji"_s);
    QVERIFY2(!update.failed(), qPrintable(update.error.message + update.refused));
    QCOMPARE(github.pullRequests.size(), 1);
    QCOMPARE(update.proposal->number, 1);
    QCOMPARE(github.head(u"jan/brulion"_s, u"brulion/20261108-100000"_s), update.commit);
    QCOMPARE(github.parentsOf(update.commit), QStringList{first});
    QCOMPARE(update.proposal->files.size(), 2);
    QVERIFY(github.file(u"jan/brulion"_s, u"brulion/20261108-100000"_s, u"src/app/shell/guide/pl/Witaj.md"_s)
                    ->contains("Brulionu"));
    for (const LocalChange &change : nb.sync->localChanges())
        QVERIFY(change.proposed);

    // Merged on GitHub: the next pull brings the same text, the proposal is told merged.
    github.merge(1);
    const PullReport merged = nb.pull();
    QVERIFY(!merged.failed());
    QVERIFY(merged.proposal);
    QCOMPARE(merged.proposal->state, u"merged"_s);
    QCOMPARE(nb.sync->proposal()->state, u"merged"_s);
    QVERIFY(nb.changes().isEmpty());

    // A new change after a merged proposal: a new pull request.
    nb.write(u"Witaj.md"_s, u8(u"# Witaj!\n"_s));
    nb.clock = nb.clock.addSecs(60);
    const CommitReport second = nb.propose(u"Wykrzyknik"_s);
    QVERIFY2(!second.failed(), qPrintable(second.error.message + second.refused));
    QCOMPARE(second.proposal->number, 2);
    QCOMPARE(second.proposal->branch, u"brulion/20261108-100200"_s);
    QCOMPARE(github.pullRequests.size(), 2);
}

void TestGitHubSync::proposeInOwnRepository()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, {{u"Plan.md"_s, "plan\n"}});
    Notebook nb(&github, QString());
    QVERIFY(!nb.pull().failed());
    nb.write(u"Plan.md"_s, "plan B\n");
    const CommitReport report = nb.propose(u"Plan B"_s);
    QVERIFY2(!report.failed(), qPrintable(report.error.message + report.refused));
    QVERIFY(!report.forked);
    QCOMPARE(report.proposal->head.fullName(), kRepo);
    QCOMPARE(github.pullRequests.first().head, u"brulion/20261108-100000"_s);
    QCOMPARE(github.file(kRepo, u"brulion/20261108-100000"_s, u"Plan.md"_s).value(), QByteArray("plan B\n"));
    QCOMPARE(github.file(kRepo, u"main"_s, u"Plan.md"_s).value(), QByteArray("plan\n"));
    QCOMPARE(github.count("POST", u"/forks"_s), 0);
}

void TestGitHubSync::emptyRepository()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s); // created on GitHub without a README
    Notebook nb(&github, QString());
    nb.write(u"Dziennik/2026-11-08.md"_s, "- dziś\n");
    nb.write(u"Witaj.md"_s, "# Witaj\n");
    nb.write(u"obrazek.png"_s, QByteArray("\x89PNG\r\n\x1a\n", 8));
    const PullReport pull = nb.pull();
    QVERIFY2(!pull.failed(), qPrintable(pull.error.message + pull.refused));
    QVERIFY(pull.emptyRepository);
    QCOMPARE(nb.changes().size(), 3);
    const CommitReport report = nb.commit(u"Pierwsze notatki"_s);
    QVERIFY2(!report.failed(), qPrintable(report.error.message + report.refused));
    QCOMPARE(github.count("PUT", u"/contents/"_s), 1);
    QCOMPARE(github.files(kRepo, u"main"_s).keys(),
             (QStringList{u"Dziennik/2026-11-08.md"_s, u"Witaj.md"_s, u"obrazek.png"_s}));
    QCOMPARE(report.committed.size(), 3);
    QVERIFY(nb.changes().isEmpty());
    QVERIFY(nb.pull().upToDate || nb.changes().isEmpty());
}

void TestGitHubSync::lfsPointersLeftOut()
{
    const QByteArray pointer("version https://git-lfs.github.com/spec/v1\n"
                             "oid sha256:4d7a214614ab2935c943f9e0ff69d22eadbb8f32b1258daaa5e2ca24d17e2393\n"
                             "size 104857600\n");
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, {{u"Notatki/film.mp4"_s, pointer}, {u"Notatki/Opis.md"_s, "film\n"}});
    Notebook nb(&github, u"Notatki"_s);
    const PullReport report = nb.pull();
    QVERIFY(!report.failed());
    QCOMPARE(report.downloaded, 1);
    QVERIFY(!nb.has(u"film.mp4"_s));
    QCOMPARE(report.skipped.size(), 1);
    QCOMPARE(report.skipped.first().code, u"lfs"_s);
    QVERIFY(nb.changes().isEmpty()); // and never removed by a commit
    github.push(kRepo, u"main"_s, {{u"Notatki/Opis.md"_s, QByteArray("film, 100 MB\n")}});
    github.clearLog();
    const PullReport again = nb.pull();
    QVERIFY(!again.failed());
    QCOMPARE(github.count("GET", u"/git/blobs/"_s), 1); // the pointer is not fetched again
    nb.write(u"Opis.md"_s, "film, 100 MB, w LFS\n");
    QVERIFY(!nb.commit(u"Opis"_s).failed());
    QCOMPARE(github.file(kRepo, u"main"_s, u"Notatki/film.mp4"_s).value(), pointer);
}

void TestGitHubSync::remoteEmptyRefused()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s,
                      {{u"Notatki/A.md"_s, "a\n"}, {u"Notatki/B.md"_s, "b\n"}, {u"inne.md"_s, "x\n"}});
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.pull().failed());
    github.push(kRepo, u"main"_s, {{u"Notatki/A.md"_s, std::nullopt}, {u"Notatki/B.md"_s, std::nullopt}});
    const PullReport report = nb.pull();
    QCOMPARE(report.refused, u"remote-empty"_s);
    QVERIFY(nb.has(u"A.md"_s));
    QVERIFY(nb.has(u"B.md"_s));
}

void TestGitHubSync::corruptDownloadNotWritten()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, {{u"Notatki/A.md"_s, "a\n"}, {u"Notatki/B.md"_s, "b\n"}});
    const QString sha = blobSha("b\n");
    github.corrupt.insert(sha, "<html>proxy error</html>");
    Notebook nb(&github, u"Notatki"_s);
    const PullReport report = nb.pull();
    QVERIFY(!report.failed());
    QCOMPARE(report.downloaded, 1);
    QCOMPARE(report.errors.size(), 1);
    QCOMPARE(report.errors.first().code, u"integrity"_s);
    QVERIFY(!nb.has(u"B.md"_s));
    // Not complete: the next pull compares again though the branch did not move.
    github.corrupt.clear();
    const PullReport again = nb.pull();
    QVERIFY(!again.failed());
    QVERIFY(!again.upToDate);
    QCOMPARE(again.downloaded, 1);
    QCOMPARE(nb.read(u"B.md"_s), QByteArray("b\n"));
    // A commit is refused while a pull could not bring every file up to date.
    github.corrupt.insert(blobSha("c\n"), "x");
    github.push(kRepo, u"main"_s, {{u"Notatki/C.md"_s, QByteArray("c\n")}});
    nb.write(u"A.md"_s, "a2\n");
    QCOMPARE(nb.commit(u"A2"_s).refused, u"pull-incomplete"_s);
}

void TestGitHubSync::executableBitKept()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, {{u"skrypt.sh"_s, "echo 1\n"}});
    github.push(kRepo, u"main"_s, {}, u"chmod"_s, {{u"skrypt.sh"_s, u"100755"_s}});
    Notebook nb(&github, QString());
    QVERIFY(!nb.pull().failed());
    nb.write(u"skrypt.sh"_s, "echo 2\n");
    QVERIFY(!nb.commit(u"Dwa"_s).failed());
    QCOMPARE(github.modeOf(kRepo, u"main"_s, u"skrypt.sh"_s), u"100755"_s);
    QCOMPARE(github.file(kRepo, u"main"_s, u"skrypt.sh"_s).value(), QByteArray("echo 2\n"));
}

void TestGitHubSync::wholeRepository()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, sampleRepo());
    Notebook nb(&github, QString());
    const PullReport report = nb.pull();
    QVERIFY(!report.failed());
    QCOMPARE(nb.files(), (QStringList{u"Notatki/Dziennik/2026-11-07.md"_s, u"Notatki/Zażółć gęślą.md"_s,
                                      u"Notatki/obrazy/kot.png"_s, u"README.md"_s, u"src/main.cpp"_s}));
    QCOMPARE(github.count("GET", u"/contents"_s), 0); // the root tree comes with the branch
    // Another link: the bookkeeping starts again (nothing removed).
    RepoLink other = nb.sync->link();
    other.folder = u"Notatki"_s;
    nb.sync->setLink(other);
    QVERIFY(!nb.sync->hasPulled());
    QVERIFY(nb.has(u"README.md"_s));
}

void TestGitHubSync::guards()
{
    FakeGitHub github;
    github.createRepo(u"jan"_s, u"notatki"_s, {{u"Notatki/A.md"_s, "a\n"}, {u"Notatki/B.md"_s, "b\n"}});
    Notebook nb(&github, u"Notatki"_s);
    QVERIFY(!nb.pull().failed());

    // One operation at a time.
    std::optional<PullReport> first;
    std::optional<PullReport> second;
    nb.sync->pull([&first](const PullReport &r) { first = r; });
    nb.sync->pull([&second](const PullReport &r) { second = r; });
    QVERIFY(waitFor([&] { return first && second; }));
    QCOMPARE(second->refused, u"busy"_s);
    QVERIFY(!first->failed());

    // Every file pulled before gone here (a lost storage): nothing is committed.
    QVERIFY(nb.vault->remove(u"A.md"_s));
    QVERIFY(nb.vault->remove(u"B.md"_s));
    QCOMPARE(nb.commit(u"Pusto"_s).refused, u"local-empty"_s);
    QVERIFY(github.file(kRepo, u"main"_s, u"Notatki/A.md"_s));

    // A token that may not push: refused before anything is written.
    nb.write(u"A.md"_s, "a\n");
    nb.write(u"B.md"_s, "b\n");
    github.repo(kRepo)->owner = u"ktos"_s;
    nb.write(u"C.md"_s, "c\n");
    QCOMPARE(nb.commit(u"Nie moje"_s).refused, u"read-only"_s);

    // A wrong token: the pull says so.
    nb.client->setToken(u"ghp_wrong"_s);
    QCOMPARE(nb.pull().error.code, GitHubError::Code::Auth);

    // Cancelled while GitHub does not answer.
    nb.client->setToken(github.token);
    github.clearLog();
    github.stallNext = true;
    std::optional<PullReport> cancelled;
    nb.sync->pull([&cancelled](const PullReport &r) { cancelled = r; });
    QVERIFY(waitFor([&] { return !github.requests.isEmpty(); }));
    QVERIFY(!cancelled);
    nb.sync->cancel();
    QVERIFY(waitFor([&] { return cancelled.has_value(); }));
    QCOMPARE(cancelled->error.code, GitHubError::Code::Cancelled);
    QVERIFY(!nb.sync->busy());
}

QTEST_GUILESS_MAIN(TestGitHubSync)
#include "tst_github_sync.moc"
