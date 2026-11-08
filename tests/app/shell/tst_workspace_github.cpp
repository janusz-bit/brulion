// Workspace.github against the core's in-memory GitHub (FakeGitHub): the token checked, kept in
// an owner-only file and forgotten; a notebook linked from a pasted address (a folder of a branch
// whose name has a "/"), pulled, its changes listed, committed (also with notes left out) and
// proposed; a repository opened as a new notebook in the app's storage; the link per notebook in
// the settings, unlinking; refusals and errors explained in the UI language; the status the sync
// footer shows.
#include "workspace.h"
#include "workspacegithub.h"

#include "fakegithub.h"

#include "github/github.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using brulion::shell::Workspace;
using brulion::shell::WorkspaceGitHub;
using githubtest::FakeGitHub;

namespace {

bool waitFor(const std::function<bool()> &done, int timeoutMs = 20000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs * BRULION_TEST_SLOWDOWN)
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 20);
    return done();
}

QString pathOf(const QVariant &row)
{
    return row.toMap().value(u"path"_s).toString();
}

} // namespace

class TestWorkspaceGitHub : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QVariantMap m_settings; ///< what AppSettings would keep
    QDateTime m_now{QDate(2026, 11, 8), QTime(10, 0)};

    std::unique_ptr<Workspace> open(FakeGitHub *github, const QString &root = {}, const QString &language = u"en"_s)
    {
        const QString folder = root.isEmpty() ? m_tmp.filePath(u"notebook-%1"_s.arg(++m_n)) : root;
        QDir().mkpath(folder);
        auto ws = std::make_unique<Workspace>();
        ws->setClock([this] { return m_now; });
        ws->setUiLanguage(language);
        WorkspaceGitHub *gh = ws->github();
        gh->setAutomatic(false);
        gh->setTransport(github);
        gh->setNotebooksRoot(m_tmp.filePath(u"appdata/GitHub"_s));
        gh->setSettingsAccess(
                [this](const QString &key, const QVariant &fallback) { return m_settings.value(key, fallback); },
                [this](const QString &key, const QVariant &value) { m_settings.insert(key, value); });
        gh->setTokenFile(m_tmp.filePath(u"appdata/secrets/github-token"_s));
        if (!ws->open(folder, folder + u"-state"_s))
            qWarning() << "cannot open" << folder;
        ws->finishIndexing();
        return ws;
    }

    static bool saveToken(WorkspaceGitHub *gh, const QString &token, QString *message = nullptr)
    {
        QSignalSpy checked(gh, &WorkspaceGitHub::tokenChecked);
        gh->saveToken(token);
        if (!waitFor([&] { return checked.size() == 1; }))
            return false;
        // A linked notebook gets its newest version with the new token: waited for.
        if (!waitFor([gh] { return !gh->busy(); }))
            return false;
        if (message)
            *message = checked.first().at(1).toString();
        return checked.first().at(0).toBool();
    }

    static bool link(WorkspaceGitHub *gh, const QString &address, QString *message = nullptr,
                     const QString &branch = {}, const QString &folder = {})
    {
        QSignalSpy finished(gh, &WorkspaceGitHub::linkFinished);
        gh->linkNotebook(address, branch, folder);
        if (!waitFor([&] { return finished.size() == 1; }))
            return false;
        if (message)
            *message = finished.first().at(1).toString();
        return finished.first().at(0).toBool();
    }

    /// Run `start` and wait for the operation's end: (ok, message, tone).
    static QVariantList operation(WorkspaceGitHub *gh, const std::function<void()> &start)
    {
        QSignalSpy finished(gh, &WorkspaceGitHub::operationFinished);
        start();
        if (!waitFor([&] { return !finished.isEmpty() && !gh->busy(); }))
            return {};
        return {finished.first().at(1), finished.first().at(2), finished.first().at(3)};
    }

    static QMap<QString, QByteArray> repoFiles()
    {
        return {{u"README.md"_s, "# Repo\n"},
                {u"Notatki/Plan.md"_s, "- plan\n"},
                {u"Notatki/Zażółć.md"_s, u"# Zażółć gęślą jaźń\n"_s.toUtf8()}};
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }
    void init()
    {
        m_settings.clear();
        QFile::remove(m_tmp.filePath(u"appdata/secrets/github-token"_s));
    }

    void token()
    {
        FakeGitHub github;
        auto ws = open(&github);
        WorkspaceGitHub *gh = ws->github();
        QVERIFY(!gh->signedIn());
        QSignalSpy account(gh, &WorkspaceGitHub::accountChanged);

        QString message;
        QVERIFY(!saveToken(gh, u"ghp_wrong"_s, &message));
        QCOMPARE(message, u"GitHub did not accept this token."_s);
        QVERIFY(!gh->signedIn());
        QVERIFY(!QFile::exists(m_tmp.filePath(u"appdata/secrets/github-token"_s)));
        QVERIFY(!saveToken(gh, u"two words"_s, &message));

        QVERIFY(saveToken(gh, u"  "_s + github.token + u"\n"_s, &message));
        QCOMPARE(message, u"Signed in to GitHub as @jan."_s);
        QVERIFY(gh->signedIn());
        QCOMPARE(gh->login(), u"jan"_s);
        QVERIFY(!account.isEmpty());
        const QString file = m_tmp.filePath(u"appdata/secrets/github-token"_s);
        QFile in(file);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(in.readAll()), github.token);
#ifndef Q_OS_WIN
        QCOMPARE(QFileInfo(file).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());
#endif
        // Read again by a new workspace (the next start), never shown to QML.
        auto again = open(&github);
        QVERIFY(again->github()->signedIn());
        QCOMPARE(again->github()->login(), u"jan"_s);

        // A token without a write scope: saved, but told.
        github.scopes = u"read:user"_s;
        QVERIFY(saveToken(gh, github.token, &message));
        QVERIFY2(message.contains(u"can only read"_s), qPrintable(message));

        gh->forgetToken();
        QVERIFY(!gh->signedIn());
        QVERIFY(gh->login().isEmpty());
        QVERIFY(!QFile::exists(file));
    }

    void linkPullCommit()
    {
        FakeGitHub github;
        github.createRepo(u"jan"_s, u"notatki"_s, repoFiles());
        github.push(u"jan/notatki"_s, u"feature/x"_s, {{u"Notatki/Plan.md"_s, QByteArray("- plan (gałąź)\n")}});
        auto ws = open(&github);
        WorkspaceGitHub *gh = ws->github();
        QVERIFY(saveToken(gh, github.token));
        QVERIFY(!gh->linked());
        QCOMPARE(gh->state(), u"disabled"_s);
        QVERIFY(gh->appStatus().isEmpty());

        QString message;
        QVERIFY2(link(gh, u"https://github.com/jan/notatki/tree/feature/x/Notatki"_s, &message), qPrintable(message));
        QVERIFY(gh->linked());
        QCOMPARE(gh->repository(), u"jan/notatki"_s);
        QCOMPARE(gh->branch(), u"feature/x"_s);
        QCOMPARE(gh->folder(), u"Notatki"_s);
        QCOMPARE(gh->webUrl(), u"https://github.com/jan/notatki/tree/feature/x/Notatki"_s);
        QCOMPARE(gh->linkText(), u"jan/notatki · feature/x · Notatki"_s);
        const QString root = ws->vault()->rootPath();
        QCOMPARE(m_settings.value(WorkspaceGitHub::notebookKey(root, u"repository"_s)).toString(), u"jan/notatki"_s);
        QCOMPARE(m_settings.value(WorkspaceGitHub::notebookKey(root, u"branch"_s)).toString(), u"feature/x"_s);
        QCOMPARE(m_settings.value(WorkspaceGitHub::notebookKey(root, u"folder"_s)).toString(), u"Notatki"_s);

        QVariantList result = operation(gh, [gh] { gh->pull(); });
        QVERIFY(!result.isEmpty());
        QVERIFY(result.at(0).toBool());
        QCOMPARE(result.at(1).toString(), u"From GitHub: 2 file(s) downloaded"_s);
        QCOMPARE(ws->vault()->readBinary(u"Plan.md"_s), QByteArray("- plan (gałąź)\n"));
        QVERIFY(ws->vault()->exists(u"Zażółć.md"_s));
        QCOMPARE(gh->state(), u"idle"_s);
        QCOMPARE(gh->changeCount(), 0);
        QCOMPARE(gh->statusText(), u"The same as on GitHub · checked today 10:00"_s);
        QCOMPARE(gh->headText().size(), 7);
        QVERIFY(gh->canPush());
        const QVariantMap footer = gh->appStatus();
        QCOMPARE(footer.value(u"state"_s).toString(), u"idle"_s);
        QCOMPARE(footer.value(u"icon"_s).toString(), u"git-branch"_s);

        // Changes: listed with their titles and kinds.
        QVERIFY(ws->vault()->writeText(u"Plan.md"_s, u"- plan (gałąź)\n- więcej\n"_s));
        QVERIFY(ws->vault()->writeText(u"Nowa.md"_s, u"# Nowa\n"_s));
        gh->refreshChanges();
        QCOMPARE(gh->changeCount(), 2);
        const QVariantMap first = gh->changes().first().toMap();
        QCOMPARE(first.value(u"path"_s).toString(), u"Nowa.md"_s);
        QCOMPARE(first.value(u"title"_s).toString(), u"Nowa"_s);
        QCOMPARE(first.value(u"kind"_s).toString(), u"added"_s);
        QCOMPARE(first.value(u"kindText"_s).toString(), u"New"_s);
        QCOMPARE(gh->statusText(), u"2 change(s) not on GitHub yet · checked today 10:00"_s);
        QCOMPARE(gh->defaultCommitMessage(2), u"Update 2 note(s) from Brulion"_s);

        // Everything left out: nothing is sent.
        result = operation(gh, [gh] { gh->commit(u"Nic"_s, {u"Nowa.md"_s, u"Plan.md"_s}); });
        QCOMPARE(result.at(0).toBool(), false);
        QCOMPARE(result.at(1).toString(), u"Choose at least one note."_s);

        // One left out: the other committed.
        result = operation(gh, [gh] { gh->commit(u"Więcej planu"_s, {u"Nowa.md"_s}); });
        QVERIFY2(result.at(0).toBool(), qPrintable(result.at(1).toString()));
        QCOMPARE(result.at(1).toString(), u"1 file(s) committed to “feature/x”."_s);
        QCOMPARE(github.file(u"jan/notatki"_s, u"feature/x"_s, u"Notatki/Plan.md"_s).value(),
                 QByteArray("- plan (gałąź)\n- więcej\n"));
        QVERIFY(!github.file(u"jan/notatki"_s, u"feature/x"_s, u"Notatki/Nowa.md"_s));
        QCOMPARE(github.messageOf(github.head(u"jan/notatki"_s, u"feature/x"_s)), u"Więcej planu"_s);
        QCOMPARE(gh->changeCount(), 1);
        QCOMPARE(pathOf(gh->changes().first()), u"Nowa.md"_s);
        QVERIFY(gh->lastReport()
                        .value(u"commitUrl"_s)
                        .toString()
                        .startsWith(u"https://github.com/jan/notatki/commit/"_s));

        // A proposal of the rest: a branch of the repository itself (the token may push).
        result = operation(gh, [gh] { gh->propose(u"Nowa strona"_s, u"Opis"_s); });
        QVERIFY2(result.at(0).toBool(), qPrintable(result.at(1).toString()));
        QCOMPARE(result.at(1).toString(), u"Proposed as pull request #1."_s);
        QCOMPARE(gh->proposal().value(u"number"_s).toInt(), 1);
        QCOMPARE(gh->proposal().value(u"stateText"_s).toString(), u"Open"_s);
        QVERIFY(gh->proposal().value(u"open"_s).toBool());
        QVERIFY(gh->changes().first().toMap().value(u"proposed"_s).toBool());
        QCOMPARE(github.pullRequests.first().base, u"feature/x"_s);

        // Merged on GitHub: the next pull tells.
        github.merge(1);
        result = operation(gh, [gh] { gh->pull(); });
        QVERIFY(result.at(0).toBool());
        QCOMPARE(gh->proposal().value(u"stateText"_s).toString(), u"Merged"_s);
        QCOMPARE(gh->changeCount(), 0);
        gh->forgetProposal();
        QVERIFY(gh->proposal().isEmpty());

        // Unlinked: the notes stay, nothing more goes to GitHub.
        gh->unlink();
        QVERIFY(!gh->linked());
        QVERIFY(ws->vault()->exists(u"Plan.md"_s));
        QVERIFY(m_settings.value(WorkspaceGitHub::notebookKey(root, u"repository"_s)).toString().isEmpty());
    }

    void conflictsAndRefusals()
    {
        FakeGitHub github;
        github.createRepo(u"ktos"_s, u"cudze"_s, {{u"Plan.md"_s, "Spotkanie o 9\n"}});
        auto ws = open(&github);
        WorkspaceGitHub *gh = ws->github();
        QString message;
        QVERIFY2(link(gh, u"ktos/cudze"_s, &message), qPrintable(message));
        QCOMPARE(message, u"Linked. Brulion now gets the newest version."_s);
        QCOMPARE(gh->branch(), u"main"_s); // the default
        QVERIFY(operation(gh, [gh] { gh->pull(); }).at(0).toBool());
        QVERIFY(!gh->signedIn());
        QVERIFY(!gh->canPush());

        // Without a token: told what to do.
        QVERIFY(ws->vault()->writeText(u"Plan.md"_s, u"Spotkanie o 11\n"_s));
        gh->refreshChanges();
        QVariantList result = operation(gh, [gh] { gh->commit(u"x"_s); });
        QCOMPARE(result.at(0).toBool(), false);
        QCOMPARE(result.at(1).toString(),
                 u"Committing and proposing changes need a token: save one in the GitHub settings below."_s);
        QCOMPARE(result.at(2).toString(), u"warning"_s);
        QCOMPARE(gh->state(), u"idle"_s); // a refusal is not the notebook's state

        // With a token that may not push there.
        QVERIFY(saveToken(gh, github.token));
        result = operation(gh, [gh] { gh->commit(u"x"_s); });
        QCOMPARE(result.at(1).toString(),
                 u"Your token may not write to this repository: propose the changes instead."_s);
        QVERIFY(gh->pushKnown());
        QVERIFY(!gh->canPush());

        // Changed on GitHub too: both versions kept; a proposal waits until they are merged.
        github.push(u"ktos/cudze"_s, u"main"_s, {{u"Plan.md"_s, QByteArray("Spotkanie o 10\n")}});
        result = operation(gh, [gh] { gh->pull(); });
        QCOMPARE(result.at(2).toString(), u"warning"_s);
        QCOMPARE(
                result.at(1).toString(),
                u"1 note(s) changed both here and on GitHub. Both versions are kept: merge them in the sync center."_s);
        QCOMPARE(gh->lastReport().value(u"conflicts"_s).toInt(), 1);
        result = operation(gh, [gh] { gh->propose(u"Plan"_s, {}); });
        QCOMPARE(result.at(1).toString(),
                 u"Merge the conflicting notes first (the sync center's Conflicts tab), then commit."_s);

        // Polish texts.
        ws->setUiLanguage(u"pl"_s);
        QVERIFY(!gh->errorText(u"no-token"_s).isEmpty());
        QVERIFY(!gh->problemText(u"lfs"_s).isEmpty());
    }

    void notFoundAndEmpty()
    {
        FakeGitHub github;
        github.createRepo(u"jan"_s, u"pusty"_s);
        github.createRepo(u"jan"_s, u"tajne"_s, {{u"a.md"_s, "a\n"}}, u"main"_s, true);
        auto ws = open(&github);
        WorkspaceGitHub *gh = ws->github();
        QString message;
        QVERIFY(!link(gh, u"jan/brak"_s, &message));
        QCOMPARE(message,
                 u"The repository, branch or folder was not found on GitHub. A private repository needs a token."_s);
        QVERIFY(!link(gh, u"https://gitlab.com/jan/notatki"_s, &message));
        QCOMPARE(message, u"This is not the address of a GitHub repository."_s);
        QVERIFY(!link(gh, u"jan/tajne"_s, &message)); // private: not without a token
        QVERIFY(!link(gh, u"jan/pusty"_s, &message, u"a b"_s));
        QCOMPARE(message, u"“a b” cannot be the name of a branch."_s);
        QVERIFY(!link(gh, u"jan/pusty"_s, &message, {}, u"../x"_s));
        QVERIFY(saveToken(gh, github.token));
        QVERIFY(link(gh, u"jan/tajne"_s, &message));
        QVERIFY(!link(gh, u"jan/tajne"_s, &message, u"inna"_s));
        QCOMPARE(message, u"The repository has no branch “inna”."_s);
        QVERIFY(link(gh, u"jan/pusty"_s, &message));
        QCOMPARE(message, u"Linked. The repository is empty: your first commit fills it."_s);
        QVERIFY(gh->linked());
        QCOMPARE(gh->repository(), u"jan/pusty"_s);
    }

    void openRepositoryAsNotebook()
    {
        FakeGitHub github;
        github.createRepo(u"jan"_s, u"notatki"_s, repoFiles());
        auto ws = open(&github);
        WorkspaceGitHub *gh = ws->github();
        QCOMPARE(gh->suggestedFolder(u"jan/notatki"_s, u"Notatki"_s),
                 QDir::cleanPath(m_tmp.filePath(u"appdata/GitHub/jan/notatki – Notatki"_s)));
        QCOMPARE(gh->suggestedFolder(u"https://github.com/jan/notatki"_s),
                 QDir::cleanPath(m_tmp.filePath(u"appdata/GitHub/jan/notatki"_s)));
        QVERIFY(gh->suggestedFolder(u"nie adres"_s).isEmpty());
        const QVariantMap parsed = gh->parseAddress(u"https://github.com/jan/notatki/tree/main/Notatki"_s);
        QVERIFY(parsed.value(u"valid"_s).toBool());
        QCOMPARE(parsed.value(u"fullName"_s).toString(), u"jan/notatki"_s);
        QCOMPARE(parsed.value(u"branch"_s).toString(), u"main"_s);
        QCOMPARE(parsed.value(u"folder"_s).toString(), u"Notatki"_s);
        QVERIFY(!gh->parseAddress(u"x"_s).value(u"valid"_s).toBool());

        QSignalSpy prepared(gh, &WorkspaceGitHub::notebookPrepared);
        gh->openRepository(u"https://github.com/jan/notatki/tree/main/Notatki"_s, {}, {});
        QVERIFY(waitFor([&] { return prepared.size() == 1; }));
        const QString path = prepared.first().at(0).toString();
        QCOMPARE(path, QDir::cleanPath(m_tmp.filePath(u"appdata/GitHub/jan/notatki – Notatki"_s)));
        QCOMPARE(m_settings.value(WorkspaceGitHub::notebookKey(path, u"folder"_s)).toString(), u"Notatki"_s);

        // The shell opens it (AppController.openVaultAt): linked, its notes arrive.
        ws->close();
        auto notebook = open(&github, path);
        WorkspaceGitHub *opened = notebook->github();
        QVERIFY(opened->linked());
        QCOMPARE(opened->linkText(), u"jan/notatki · main · Notatki"_s);
        QVERIFY(operation(opened, [opened] { opened->pull(); }).at(0).toBool());
        QCOMPARE(notebook->vault()->readBinary(u"Plan.md"_s), QByteArray("- plan\n"));

        // The same repository again: the same folder; another link there: the next free name.
        prepared.clear();
        gh->openRepository(u"jan/notatki"_s, {}, u"Notatki"_s);
        QVERIFY(waitFor([&] { return prepared.size() == 1; }));
        QCOMPARE(prepared.first().at(0).toString(), path);
        prepared.clear();
        gh->openRepository(u"jan/notatki"_s, {}, {}, m_tmp.filePath(u"elsewhere"_s));
        QVERIFY(waitFor([&] { return prepared.size() == 1; }));
        QCOMPARE(prepared.first().at(0).toString(), QDir::cleanPath(m_tmp.filePath(u"elsewhere"_s)));
    }

    void offlineIsNotAnError()
    {
        FakeGitHub github;
        github.createRepo(u"jan"_s, u"notatki"_s, repoFiles());
        auto ws = open(&github);
        WorkspaceGitHub *gh = ws->github();
        QVERIFY(saveToken(gh, github.token));
        QVERIFY(link(gh, u"jan/notatki"_s));
        github.hook = [](const brulion::sync::HttpRequest &,
                         const FakeGitHub::LoggedRequest &) -> std::optional<brulion::sync::HttpResponse> {
            brulion::sync::HttpResponse none;
            none.networkError = u"Network unreachable"_s;
            return none;
        };
        const QVariantList result = operation(gh, [gh] { gh->pull(); });
        QCOMPARE(result.at(0).toBool(), false);
        QCOMPARE(gh->state(), u"offline"_s);
        QCOMPARE(gh->statusText(), u"Offline — Brulion checks GitHub again later"_s);
        QCOMPARE(gh->appStatus().value(u"state"_s).toString(), u"offline"_s);
        QVERIFY(gh->lastError().isEmpty());
    }
};

QTEST_MAIN(TestWorkspaceGitHub)
#include "tst_workspace_github.moc"
