// Repository addresses, Git's object ids, which files of a folder are the notebook's, and how the
// client explains GitHub's answers.
#include "github/github.h"
#include "log/redact.h"

#include <QTest>

using namespace brulion;
using namespace brulion::github;
using namespace Qt::StringLiterals;

class TestGitHubAddress : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parse_data();
    void parse();
    void refused_data();
    void refused();
    void splitRefPath_data();
    void splitRefPath();
    void folders_data();
    void folders();
    void branchNames_data();
    void branchNames();
    void urls();
    void blobIds();
    void lfsPointers();
    void notebookPaths_data();
    void notebookPaths();
    void errors_data();
    void errors();
    void tokenIsMasked();
};

void TestGitHubAddress::parse_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("owner");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QStringList>("refPath");
    QTest::addColumn<bool>("file");

    const QStringList none;
    QTest::newRow("owner/name") << u"janusz-bit/brulion"_s << u"janusz-bit"_s << u"brulion"_s << none << false;
    QTest::newRow("https") << u"https://github.com/janusz-bit/brulion"_s << u"janusz-bit"_s << u"brulion"_s << none
                           << false;
    QTest::newRow(".git") << u"https://github.com/janusz-bit/brulion.git"_s << u"janusz-bit"_s << u"brulion"_s << none
                          << false;
    QTest::newRow("no scheme, slash") << u"github.com/janusz-bit/brulion/"_s << u"janusz-bit"_s << u"brulion"_s << none
                                      << false;
    QTest::newRow("www, query, fragment, spaces")
            << u"  https://www.github.com/janusz-bit/brulion?tab=readme-ov-file#brulion \n"_s << u"janusz-bit"_s
            << u"brulion"_s << none << false;
    QTest::newRow("tree with folder") << u"https://github.com/janusz-bit/brulion/tree/main/src/app/shell/guide/pl"_s
                                      << u"janusz-bit"_s << u"brulion"_s
                                      << QStringList{u"main"_s, u"src"_s, u"app"_s, u"shell"_s, u"guide"_s, u"pl"_s}
                                      << false;
    QTest::newRow("blob") << u"https://github.com/janusz-bit/brulion/blob/main/docs/UI.md"_s << u"janusz-bit"_s
                          << u"brulion"_s << QStringList{u"main"_s, u"docs"_s, u"UI.md"_s} << true;
    QTest::newRow("scp remote") << u"git@github.com:janusz-bit/brulion.git"_s << u"janusz-bit"_s << u"brulion"_s << none
                                << false;
    QTest::newRow("ssh remote") << u"ssh://git@github.com/janusz-bit/brulion.git"_s << u"janusz-bit"_s << u"brulion"_s
                                << none << false;
    QTest::newRow("raw file") << u"https://raw.githubusercontent.com/janusz-bit/brulion/main/README.md"_s
                              << u"janusz-bit"_s << u"brulion"_s << QStringList{u"main"_s, u"README.md"_s} << true;
    QTest::newRow("percent-encoded Polish folder")
            << u"https://github.com/jan/notatki/tree/main/Notatki%20%C5%BC%C3%B3%C5%82w"_s << u"jan"_s << u"notatki"_s
            << QStringList{u"main"_s, u"Notatki żółw"_s} << false;
    QTest::newRow("typed Polish folder") << u"github.com/jan/notatki/tree/main/Zażółć gęślą"_s << u"jan"_s
                                         << u"notatki"_s << QStringList{u"main"_s, u"Zażółć gęślą"_s} << false;
    QTest::newRow("branch with a slash") << u"github.com/jan/notatki/tree/feature/x/Dziennik"_s << u"jan"_s
                                         << u"notatki"_s << QStringList{u"feature"_s, u"x"_s, u"Dziennik"_s} << false;
    QTest::newRow("other pages of the repository")
            << u"https://github.com/jan/notatki/issues/12"_s << u"jan"_s << u"notatki"_s << none << false;
    QTest::newRow("dots and underscores")
            << u"jan_k/my.notes-2026"_s << u"jan_k"_s << u"my.notes-2026"_s << none << false;
}

void TestGitHubAddress::parse()
{
    QFETCH(QString, text);
    QFETCH(QString, owner);
    QFETCH(QString, name);
    QFETCH(QStringList, refPath);
    QFETCH(bool, file);
    const std::optional<RepoAddress> address = parseRepoAddress(text);
    QVERIFY(address);
    QCOMPARE(address->owner, owner);
    QCOMPARE(address->name, name);
    QCOMPARE(address->refPath, refPath);
    QCOMPARE(address->file, file);
}

void TestGitHubAddress::refused_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("empty") << QString();
    QTest::newRow("spaces") << u"   "_s;
    QTest::newRow("owner only") << u"jan"_s;
    QTest::newRow("a user's page") << u"https://github.com/jan"_s;
    QTest::newRow("another host") << u"https://gitlab.com/jan/notatki"_s;
    QTest::newRow("codeberg remote") << u"git@codeberg.org:jan/notatki.git"_s;
    QTest::newRow("settings page") << u"https://github.com/settings/tokens"_s;
    QTest::newRow("orgs page") << u"https://github.com/orgs/brulion"_s;
    QTest::newRow("owner starts with a dash") << u"-jan/notatki"_s;
    QTest::newRow("name ..") << u"jan/.."_s;
    QTest::newRow("Polish owner") << u"żółw/notatki"_s;
    QTest::newRow("ftp") << u"ftp://github.com/jan/notatki"_s;
    QTest::newRow("too long owner") << QString(QString(40, u'a') + u"/x"_s);
}

void TestGitHubAddress::refused()
{
    QFETCH(QString, text);
    QVERIFY(!parseRepoAddress(text));
}

void TestGitHubAddress::splitRefPath_data()
{
    QTest::addColumn<QStringList>("refPath");
    QTest::addColumn<QStringList>("branches");
    QTest::addColumn<bool>("file");
    QTest::addColumn<QString>("branch");
    QTest::addColumn<QString>("folder");

    const QStringList path{u"feature"_s, u"x"_s, u"Dziennik"_s};
    QTest::newRow("longest branch wins") << path << QStringList{u"main"_s, u"feature"_s, u"feature/x"_s} << false
                                         << u"feature/x"_s << u"Dziennik"_s;
    QTest::newRow("unknown: the first segment")
            << path << QStringList{u"main"_s} << false << u"feature"_s << u"x/Dziennik"_s;
    QTest::newRow("a branch name is whole segments")
            << QStringList{u"feature-2"_s, u"a"_s} << QStringList{u"feature"_s} << false << u"feature-2"_s << u"a"_s;
    QTest::newRow("a file's folder") << QStringList{u"main"_s, u"docs"_s, u"UI.md"_s} << QStringList{u"main"_s} << true
                                     << u"main"_s << u"docs"_s;
    QTest::newRow("a file at the root") << QStringList{u"main"_s, u"README.md"_s} << QStringList{u"main"_s} << true
                                        << u"main"_s << QString();
    QTest::newRow("nothing") << QStringList() << QStringList{u"main"_s} << false << QString() << QString();
}

void TestGitHubAddress::splitRefPath()
{
    QFETCH(QStringList, refPath);
    QFETCH(QStringList, branches);
    QFETCH(bool, file);
    QFETCH(QString, branch);
    QFETCH(QString, folder);
    const auto [b, f] = github::splitRefPath(refPath, branches, file);
    QCOMPARE(b, branch);
    QCOMPARE(f, folder);
}

void TestGitHubAddress::folders_data()
{
    QTest::addColumn<QString>("folder");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<QString>("normalized");
    QTest::newRow("root") << QString() << true << QString();
    QTest::newRow("slashes") << u"/Notatki//Dziennik/"_s << true << u"Notatki/Dziennik"_s;
    QTest::newRow("spaces around") << u"  Notatki  "_s << true << u"Notatki"_s;
    QTest::newRow("decomposed Polish") << u"Żółw"_s << true << u"Żółw"_s;
    QTest::newRow("dot dot") << u"Notatki/../x"_s << false << QString();
    QTest::newRow("dot") << u"./Notatki"_s << false << QString();
    QTest::newRow("backslash") << u"Notatki\\Dziennik"_s << false << QString();
    QTest::newRow("control character") << u"Nota\ttki"_s << false << QString();
}

void TestGitHubAddress::folders()
{
    QFETCH(QString, folder);
    QFETCH(bool, valid);
    QFETCH(QString, normalized);
    const std::optional<QString> result = normalizeFolder(folder);
    QCOMPARE(result.has_value(), valid);
    if (valid)
        QCOMPARE(*result, normalized);
}

void TestGitHubAddress::branchNames_data()
{
    QTest::addColumn<QString>("branch");
    QTest::addColumn<bool>("valid");
    QTest::newRow("main") << u"main"_s << true;
    QTest::newRow("with a slash") << u"feature/notatki"_s << true;
    QTest::newRow("Polish") << u"poprawki-żółw"_s << true;
    QTest::newRow("Brulion's proposals") << u"brulion/20261108-100000"_s << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("dot dot") << u"a..b"_s << false;
    QTest::newRow("space") << u"a b"_s << false;
    QTest::newRow("hidden segment") << u"x/.hidden"_s << false;
    QTest::newRow(".lock") << u"x.lock"_s << false;
    QTest::newRow("trailing slash") << u"x/"_s << false;
    QTest::newRow("trailing dot") << u"x."_s << false;
    QTest::newRow("at") << u"@"_s << false;
    QTest::newRow("reflog syntax") << u"x@{1}"_s << false;
    QTest::newRow("colon") << u"a:b"_s << false;
    QTest::newRow("leading dash") << u"-x"_s << false;
}

void TestGitHubAddress::branchNames()
{
    QFETCH(QString, branch);
    QFETCH(bool, valid);
    QCOMPARE(isValidBranchName(branch), valid);
}

void TestGitHubAddress::urls()
{
    QCOMPARE(webUrl(u"janusz-bit"_s, u"brulion"_s).toString(), u"https://github.com/janusz-bit/brulion"_s);
    QCOMPARE(webUrl(u"janusz-bit"_s, u"brulion"_s, u"main"_s, u"src/app/shell/guide/pl"_s).toString(),
             u"https://github.com/janusz-bit/brulion/tree/main/src/app/shell/guide/pl"_s);
    QCOMPARE(QString::fromLatin1(webUrl(u"jan"_s, u"notatki"_s, u"main"_s, u"Notatki żółw"_s).toEncoded()),
             u"https://github.com/jan/notatki/tree/main/Notatki%20%C5%BC%C3%B3%C5%82w"_s);
    QCOMPARE(commitUrl(u"jan"_s, u"notatki"_s, u"ce013625030ba8dba906f756967f9e9ca394464a"_s).toString(),
             u"https://github.com/jan/notatki/commit/ce013625030ba8dba906f756967f9e9ca394464a"_s);
    QCOMPARE(fileUrl(u"jan"_s, u"notatki"_s, u"main"_s, u"Dziennik/2026-11-08.md"_s).toString(),
             u"https://github.com/jan/notatki/blob/main/Dziennik/2026-11-08.md"_s);
    const QUrl token = newTokenUrl();
    QCOMPARE(token.host(), u"github.com"_s);
    QVERIFY(token.query().contains(u"scopes=repo"_s));
    QVERIFY(newTokenUrl(true).query().contains(u"scopes=public_repo"_s));
    // Parsed back: the same repository, branch and folder.
    const std::optional<RepoAddress> again =
            parseRepoAddress(webUrl(u"jan"_s, u"notatki"_s, u"main"_s, u"Notatki żółw/Dziennik"_s).toString());
    QVERIFY(again);
    QCOMPARE(again->refPath, (QStringList{u"main"_s, u"Notatki żółw"_s, u"Dziennik"_s}));
    QCOMPARE(fullName(u"jan"_s, u"notatki"_s), u"jan/notatki"_s);
    QCOMPARE(encodePath(u"a b/ż#?.md"_s), u"a%20b/%C5%BC%23%3F.md"_s);
}

void TestGitHubAddress::blobIds()
{
    // git hash-object --stdin
    QCOMPARE(blobSha(QByteArray()), u"e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"_s);
    QCOMPARE(blobSha("hello\n"), u"ce013625030ba8dba906f756967f9e9ca394464a"_s);
    QCOMPARE(blobSha("test content\n"), u"d670460b4b4aece5915caf5c68d12f560a9fe3e4"_s);
    QCOMPARE(blobSha(u"Zażółć gęślą jaźń\n"_s.toUtf8()), u"57215003682e7c7c58c1564c394431554a5c14df"_s);
    QVERIFY(isSha(u"57215003682e7c7c58c1564c394431554a5c14df"_s));
    QVERIFY(isSha(u"57215003682E7C7C58C1564C394431554A5C14DF"_s));
    QVERIFY(!isSha(u"57215003682e7c7c58c1564c394431554a5c14d"_s));
    QVERIFY(!isSha(u"57215003682e7c7c58c1564c394431554a5c14dg"_s));
    QCOMPARE(shortSha(u"57215003682E7C7C58C1564C394431554A5C14DF"_s), u"5721500"_s);
}

void TestGitHubAddress::lfsPointers()
{
    QVERIFY(isLfsPointer("version https://git-lfs.github.com/spec/v1\n"
                         "oid sha256:4d7a214614ab2935c943f9e0ff69d22eadbb8f32b1258daaa5e2ca24d17e2393\n"
                         "size 12345\n"));
    QVERIFY(isLfsPointer("version https://hawser.github.com/spec/v1\noid sha256:4d7a\nsize 1\n"));
    QVERIFY(!isLfsPointer("# version https://git-lfs.github.com/spec/v1\n"));
    const QByteArray big = QByteArray("version https://git-lfs.github.com/spec/v1\n") + QByteArray(2000, 'x');
    QVERIFY(!isLfsPointer(big));
    QVERIFY(!isLfsPointer(u"Notatka o LFS: version https://git-lfs.github.com/spec/v1\n"_s.toUtf8()));
}

void TestGitHubAddress::notebookPaths_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("notebook");
    QTest::newRow("page") << u"Zażółć gęślą.md"_s << true;
    QTest::newRow("journal") << u"Dziennik/2026-11-08.md"_s << true;
    QTest::newRow("picture") << u"assets/kot.png"_s << true;
    QTest::newRow("config") << u".brulion/config.json"_s << true;
    QTest::newRow("hidden file") << u".gitignore"_s << false;
    QTest::newRow("hidden file in a folder") << u"docs/.editorconfig"_s << false;
    QTest::newRow(".github") << u".github/workflows/ci.yml"_s << false;
    QTest::newRow(".obsidian") << u".obsidian/app.json"_s << false;
    QTest::newRow("other .brulion files") << u".brulion/other.json"_s << false;
    QTest::newRow("trash") << u".trash/old.md"_s << false;
    QTest::newRow("temp file") << u"notes.md.part"_s << false;
    QTest::newRow("node_modules") << u"node_modules/x/README.md"_s << false;
    QTest::newRow("empty") << QString() << false;
}

void TestGitHubAddress::notebookPaths()
{
    QFETCH(QString, path);
    QFETCH(bool, notebook);
    QCOMPARE(isNotebookPath(path), notebook);
}

void TestGitHubAddress::errors_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QByteArray>("headerName");
    QTest::addColumn<QByteArray>("headerValue");
    QTest::addColumn<bool>("timedOut");
    QTest::addColumn<QString>("code");

    const QByteArray none;
    QTest::newRow("bad credentials") << 401 << QByteArray(R"({"message":"Bad credentials"})") << none << none << false
                                     << u"auth"_s;
    QTest::newRow("limit used up") << 403 << QByteArray(R"({"message":"API rate limit exceeded for 192.0.2.1."})")
                                   << QByteArray("x-ratelimit-remaining") << QByteArray("0") << false
                                   << u"rate-limited"_s;
    QTest::newRow("secondary limit") << 403 << QByteArray(R"({"message":"You have exceeded a secondary rate limit."})")
                                     << QByteArray("retry-after") << QByteArray("60") << false << u"rate-limited"_s;
    QTest::newRow("429") << 429 << QByteArray("{}") << none << none << false << u"rate-limited"_s;
    QTest::newRow("no permission") << 403
                                   << QByteArray(R"({"message":"Resource not accessible by personal access token"})")
                                   << QByteArray("x-ratelimit-remaining") << QByteArray("4990") << false
                                   << u"forbidden"_s;
    QTest::newRow("not found") << 404 << QByteArray(R"({"message":"Not Found"})") << none << none << false
                               << u"not-found"_s;
    QTest::newRow("empty repository") << 409 << QByteArray(R"({"message":"Git Repository is empty."})") << none << none
                                      << false << u"empty-repository"_s;
    QTest::newRow("other 409") << 409 << QByteArray(R"({"message":"is at abc but expected def"})") << none << none
                               << false << u"conflict"_s;
    QTest::newRow("not a fast-forward") << 422 << QByteArray(R"({"message":"Update is not a fast forward"})") << none
                                        << none << false << u"not-fast-forward"_s;
    QTest::newRow("validation")
            << 422
            << QByteArray(
                       R"({"message":"Validation Failed","errors":[{"message":"A pull request already exists for jan:x."}]})")
            << none << none << false << u"validation"_s;
    QTest::newRow("server") << 502 << QByteArray("<html>Bad gateway</html>") << none << none << false << u"server"_s;
    QTest::newRow("timeout") << 0 << none << none << none << true << u"timeout"_s;
    QTest::newRow("network") << 0 << none << none << none << false << u"network"_s;
}

void TestGitHubAddress::errors()
{
    QFETCH(int, status);
    QFETCH(QByteArray, body);
    QFETCH(QByteArray, headerName);
    QFETCH(QByteArray, headerValue);
    QFETCH(bool, timedOut);
    QFETCH(QString, code);
    sync::HttpResponse response;
    response.status = status;
    response.body = body;
    response.timedOut = timedOut;
    if (status == 0)
        response.networkError = u"Host api.github.com not found"_s;
    if (!headerName.isEmpty())
        response.headers.insert(headerName, headerValue);
    if (headerName == "x-ratelimit-remaining" && headerValue == "0")
        response.headers.insert("x-ratelimit-reset", "1793955600");
    const GitHubError error = GitHubClient::errorOf(response);
    QCOMPARE(errorCodeName(error.code), code);
    QCOMPARE(error.status, status);
    if (code == u"rate-limited"_s && headerName == "x-ratelimit-remaining")
        QCOMPARE(error.resetAt, 1793955600000LL);
    if (headerName == "retry-after")
        QCOMPARE(error.retryAfterSeconds, 60);
    if (body.contains("already exists"))
        QVERIFY(error.message.contains(u"A pull request already exists"_s));
    QCOMPARE(error.isFatal(),
             code == u"auth"_s || code == u"rate-limited"_s || code == u"timeout"_s || code == u"network"_s);
}

void TestGitHubAddress::tokenIsMasked()
{
    const QString token = u"ghp_AbCdEfGhIjKlMnOpQrStUvWxYz0123456789"_s;
    {
        GitHubClient client(nullptr);
        client.setToken(u"  "_s + token + u"\n"_s);
        QVERIFY(client.hasToken());
        QCOMPARE(logging::redact(u"token: "_s + token), u"token: ***"_s);
    }
    // Forgotten with the client.
    QCOMPARE(logging::redact(u"x "_s + token), u"x "_s + token);
}

QTEST_GUILESS_MAIN(TestGitHubAddress)
#include "tst_github_address.moc"
