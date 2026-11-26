// fake_gh — the GitHub CLI as tst_gitcli_repo needs it, on every platform (a program, not a shell
// script: Windows has no /bin/sh). It writes its arguments NUL-separated (UTF-8) to $FAKE_GH_ARGS and
// its working folder to $FAKE_GH_ARGS.pwd, refuses to run unless GH_PROMPT_DISABLED=1 (as the client
// sets it), and answers as $FAKE_GH_MODE says:
//
//   ok (default)  "Creating pull request …" on stderr, the pull request's URL on stdout
//   exists        gh's "already exists" error with the URL, exit 1
//   signed-out    gh's "gh auth login" advice, exit 4
//   slow          sleeps 30 s (a git or gh that never finishes)
//
// `gh repo list` prints $FAKE_GH_JSON ("[]" unset) on stdout (signed-out: the advice, exit 4).
//
// `gh pr view` (the panel asks for the branch's pull request after a push) finds none — "no pull
// requests found", exit 1 — and records nothing (the arguments of `pr create` stay readable).
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QThread>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // (arguments(): the command line as Unicode on Windows too; argv is the ANSI code page's.)
    const QStringList arguments = QCoreApplication::arguments().mid(1);
    const QString mode = qEnvironmentVariable("FAKE_GH_MODE", QStringLiteral("ok"));
    if (mode == QLatin1String("slow")) {
        QThread::sleep(30);
        return 0;
    }
    if (arguments.size() >= 2 && arguments.at(0) == QLatin1String("pr") && arguments.at(1) == QLatin1String("view")) {
        std::fputs("no pull requests found for branch\n", stderr);
        return 1;
    }
    const QString record = qEnvironmentVariable("FAKE_GH_ARGS");
    if (!record.isEmpty()) {
        QFile args(record);
        if (args.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            for (const QString &argument : arguments) {
                args.write(argument.toUtf8());
                args.write("", 1);
            }
        }
        QFile pwd(record + QStringLiteral(".pwd"));
        if (pwd.open(QIODevice::WriteOnly | QIODevice::Truncate))
            pwd.write(QDir::currentPath().toUtf8() + '\n');
    }
    if (qEnvironmentVariable("GH_PROMPT_DISABLED") != QLatin1String("1")) {
        std::fputs("prompts\n", stderr);
        return 3;
    }
    if (mode == QLatin1String("exists")) {
        std::fputs("a pull request for branch \"x\" into branch \"main\" already exists:\n"
                   "https://github.com/ja/repo/pull/9\n",
                   stderr);
        return 1;
    }
    if (mode == QLatin1String("signed-out")) {
        std::fputs("To get started with GitHub CLI, please run:  gh auth login\n", stderr);
        return 4;
    }
    if (arguments.size() >= 2 && arguments.at(0) == QLatin1String("repo") && arguments.at(1) == QLatin1String("list")) {
        const QByteArray json = qgetenv("FAKE_GH_JSON");
        std::fputs(json.isEmpty() ? "[]\n" : json.constData(), stdout);
        return 0;
    }
    std::fputs("Creating pull request for x into main in ja/repo\n", stderr);
    std::fputs("https://github.com/ja/repo/pull/17\n", stdout);
    return 0;
}
