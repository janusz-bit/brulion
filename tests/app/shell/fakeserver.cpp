// A server for ServerProcess's test (tests/app/shell/tst_serverprocess.cpp), as brulion-cli's
// servers behave: one JSON line when it serves, log lines with a time on standard error, and the
// end of its standard input as the sign to stop (`--stop-on-eof`). Its options:
//
//   --announce URL    {"ok": true, "result": {"url": URL}} and serve
//   --refuse CODE     {"ok": false, "error": {"code": "bad_listen", "message": …}}, exit CODE
//   --crash-after MS  serve, then abort() after MS
//   --ignore-eof      keep serving when standard input ends (it has to be terminated)
//   --mark FILE       write "stopped" into FILE when it stops by itself (its input ended)
#include <QCoreApplication>
#include <QFile>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const auto value = [&args](const char *name) {
        const qsizetype i = args.indexOf(QString::fromLatin1(name));
        return i >= 0 && i + 1 < args.size() ? args.at(i + 1) : QString();
    };
    std::fputs("2026-11-22 10:30:00 starting the fake server\n", stderr);
    std::fflush(stderr);
    if (const QString code = value("--refuse"); !code.isEmpty()) {
        std::fputs(R"({"ok": false, "error": {"code": "bad_listen", "message": "Nie można słuchać na tym adresie"}})"
                   "\n",
                   stdout);
        std::fflush(stdout);
        return code.toInt();
    }
    const QString url = value("--announce");
    const QByteArray line = R"({"ok": true, "command": "fake.serve", "result": {"url": ")" + url.toUtf8() + "\"}}\n";
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fflush(stdout);
    std::fputs("2026-11-22 10:30:01 serving at the fake address\n", stderr);
    std::fflush(stderr);
    if (const QString ms = value("--crash-after"); !ms.isEmpty())
        QTimer::singleShot(ms.toInt(), [] { std::abort(); });
    const QString mark = value("--mark");
    if (!args.contains(QStringLiteral("--ignore-eof"))) {
        std::thread([&app] {
            char buffer[64];
            while (std::fread(buffer, 1, sizeof buffer, stdin) > 0) { }
            QMetaObject::invokeMethod(&app, [] { QCoreApplication::quit(); }, Qt::QueuedConnection);
        }).detach();
    }
    const int code = QCoreApplication::exec();
    if (!mark.isEmpty()) {
        QFile f(mark);
        if (f.open(QIODevice::WriteOnly))
            f.write("stopped");
    }
    return code;
}
