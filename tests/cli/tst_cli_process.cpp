// brulion-cli as a process (QProcess on the built binary, a private HOME / XDG folders — on Windows,
// where these do not move QStandardPaths, a separate profile, BRULION_PROFILE): the JSON
// envelope and exit code of the commands on temporary notebooks with Polish text, the files they
// write byte for byte (line endings, BOM), refusals (read-only, destructive without
// --allow-destructive, paths outside the notebook, a missing notebook — never created), the app's
// settings file left byte-identical and no reminder state written, Polish argv under LANG=C and
// text on stdin, two processes appending to one journal (nothing lost), dry runs showing what the
// real run writes (a daily note's template, the notes a rename rewrites, what a delete trashes),
// pages named like dates resolving as [[links]] do, the notebook and language taken as the app
// takes them, messages in the system's language (Polish or English; the machine's fields and MCP
// English), `app open` against a stand-in app, and the MCP server (initialize, tools/list,
// tools/call, errors, a request line over the 16 MiB limit, the warm index seeing a file written
// meanwhile). The tools of an agent on another computer than the app (`acp serve`'s tunnel,
// BRULION_APP_SOCKET): every request to the tunnel's socket with its grant, never to the app of this
// desktop (today and open through the tunnel, no notebook check against a copy elsewhere, capture
// without text refused), the tunnel gone in words, and the MCP server's instructions naming the device.
#include "appmessage.h"
#include "attachments/manifest.h"
#include "attachments/record.h"
#include "automation/policy.h"
#include "controlprotocol.h"
#include "controlserver.h"
#include "notebook/journals.h"
#include "notebook/session.h"
#include "semantic/models.h"
#include "semantic/vectorindex.h"
#include "singleinstance.h"
#include "vault/vault.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace {

constexpr int kTimeoutMs = 30000 * BRULION_TEST_SLOWDOWN;
const QString kNow = u"2026-10-14 10:30"_s; // a Wednesday

struct Run {
    int code = -1;
    QJsonObject json;
    QByteArray out;
    QByteArray err;
    QJsonObject result() const { return json.value(u"result"_s).toObject(); }
    QString errorCode() const { return json.value(u"error"_s).toObject().value(u"code"_s).toString(); }
    /// What it said, for a failure's message.
    QString describe() const
    {
        return u"exit %1: %2 %3"_s.arg(QString::number(code), QString::fromUtf8(out), QString::fromUtf8(err));
    }
    QStringList warnings() const
    {
        QStringList codes;
        for (const QJsonValue &w : json.value(u"warnings"_s).toArray())
            codes.append(w.toObject().value(u"code"_s).toString());
        return codes;
    }
};

void writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    QVERIFY(f.commit());
}

QByteArray readFile(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestCliProcess : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    QString m_home;
    int m_n = 0;

    /// {{weekday}} in a journal template (1 = Monday): brulion-cli's language when none is set is
    /// the app's default, English, whatever the system's (notebook::appLanguage()).
    static QString weekday(int day)
    {
        if (brulion::notebook::appLanguage({}) == u"en")
            return QLocale(QLocale::English).dayName(day);
        static const QStringList polish{u"poniedziałek"_s, u"wtorek"_s, u"środa"_s,    u"czwartek"_s,
                                        u"piątek"_s,       u"sobota"_s, u"niedziela"_s};
        return polish.at(day - 1);
    }
    static QString wednesday() { return weekday(3); }

    QProcessEnvironment environment() const
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"HOME"_s, m_home);
        env.insert(u"XDG_CONFIG_HOME"_s, m_home + u"/config"_s);
        env.insert(u"XDG_DATA_HOME"_s, m_home + u"/data"_s);
        env.insert(u"XDG_STATE_HOME"_s, m_home + u"/state"_s);
        env.insert(u"XDG_CACHE_HOME"_s, m_home + u"/cache"_s);
        env.insert(u"XDG_RUNTIME_DIR"_s, m_home + u"/run"_s);
        env.remove(u"BRULION_VAULT"_s);
        // (A grant of an app this test does not run: never the developer's agent's.)
        env.remove(u"BRULION_APP_TOKEN"_s);
        env.remove(u"BRULION_APP_CHAT_TOKEN"_s);
        env.remove(u"BRULION_APP_CLIENT"_s);
        env.remove(u"BRULION_APP_SOCKET"_s);
        env.remove(u"BRULION_APP_DEVICE"_s);
        env.insert(u"LANG"_s, u"C.UTF-8"_s);
        // Messages for people follow the system's language (docs/CLI.md "Language"): English here,
        // whatever the developer's session says.
        for (const QString &v : {u"LC_ALL"_s, u"LC_MESSAGES"_s, u"LANGUAGE"_s})
            env.remove(v);
#ifdef Q_OS_WIN
        env.insert(u"BRULION_PROFILE"_s, profile());
#endif
        return env;
    }

    // Where brulion-cli (and the app) keep their settings, app data, cache and log for this test: in the
    // private home's XDG folders, on Windows in the profile (src/ipc/profile.h), whose paths this
    // process gets from Qt under the profile's names (initTestCase()).
#ifdef Q_OS_WIN
    static QString profile() { return u"test-cli-%1"_s.arg(QCoreApplication::applicationPid()); }
    QString settingsFile() const
    {
        return QSettings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(),
                         QCoreApplication::applicationName())
                .fileName();
    }
    QString appDataDir() const { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }
    QString logFile() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::StateLocation) + u"/logs/brulion.log"_s;
    }
    QString cacheDir() const { return QStandardPaths::writableLocation(QStandardPaths::CacheLocation); }
    /// The profile's folders (removed before and after the test).
    QStringList profileDirs() const
    {
        return {QFileInfo(settingsFile()).absolutePath(),
                QFileInfo(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).absolutePath()};
    }
#else
    QString settingsFile() const { return m_home + u"/config/Brulion/Brulion.ini"_s; }
    QString appDataDir() const { return m_home + u"/data/Brulion/Brulion"_s; }
    QString logFile() const { return m_home + u"/state/Brulion/Brulion/logs/brulion.log"_s; }
    QString cacheDir() const { return m_home + u"/cache/Brulion/Brulion"_s; }
#endif

    Run cli(const QStringList &args, const QByteArray &input = {}, const QProcessEnvironment &env = {})
    {
        QProcess p;
        p.setProcessEnvironment(env.isEmpty() ? environment() : env);
        p.start(QStringLiteral(BRULION_CLI), args);
        Run r;
        if (!p.waitForStarted(kTimeoutMs))
            return r;
        if (!input.isEmpty())
            p.write(input);
        p.closeWriteChannel();
        if (!p.waitForFinished(kTimeoutMs)) {
            p.kill();
            return r;
        }
        r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -2;
        r.out = p.readAllStandardOutput();
        r.err = p.readAllStandardError();
        r.json = QJsonDocument::fromJson(r.out).object();
        return r;
    }

    /// cli() while this process's event loop runs (for stand-ins of the app living here).
    Run cliLive(const QStringList &args, const QByteArray &input = {}, const QProcessEnvironment &env = {})
    {
        QProcess p;
        p.setProcessEnvironment(env.isEmpty() ? environment() : env);
        p.start(QStringLiteral(BRULION_CLI), args);
        Run r;
        if (!p.waitForStarted(kTimeoutMs))
            return r;
        if (!input.isEmpty())
            p.write(input);
        p.closeWriteChannel();
        QElapsedTimer t;
        t.start();
        while (p.state() != QProcess::NotRunning && t.elapsed() < kTimeoutMs)
            QTest::qWait(20);
        if (p.state() != QProcess::NotRunning) {
            p.kill();
            p.waitForFinished();
            return r;
        }
        r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -2;
        r.out = p.readAllStandardOutput();
        r.err = p.readAllStandardError();
        r.json = QJsonDocument::fromJson(r.out).object();
        return r;
    }

    /// cli() with --vault and the fixed clock.
    Run run(const QString &vault, QStringList args, const QByteArray &input = {})
    {
        args.prepend(kNow);
        args.prepend(u"--now"_s);
        args.prepend(vault);
        args.prepend(u"--vault"_s);
        return cli(args, input);
    }

    QString vault(const QHash<QString, QByteArray> &files = {})
    {
        const QString dir = m_tmp.filePath(u"Notatnik %1"_s.arg(++m_n));
        QDir().mkpath(dir);
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeFile(dir + u'/' + it.key(), it.value());
        return dir;
    }

    /// The device state the app (and brulion-cli) keeps for a notebook folder.
    QString stateDirOf(const QString &vaultDir) const
    {
        return brulion::notebook::deviceStatePath(appDataDir(), vaultDir);
    }

    /// A notebook whose device has the calendars of calendarChanges(): CalDAV "Praca" (a weekly
    /// stand-up with its 28 October moved to 09:00, a meeting), "Tylko zadania" (events not
    /// shown), "Wspólny" (read-only on its server) and the subscription "Święta". Floating times:
    /// the same wall clock in every zone.
    QString calendarNotebook()
    {
        const QString dir = vault({{u"Notatka.md"_s, "- x\n"}});
        const QString folder = stateDirOf(dir) + u"/calendars"_s;
        const QJsonObject account{
                {u"id"_s, u"a-1"_s}, {u"server"_s, u"https://dav.example.pl"_s}, {u"user"_s, u"jan"_s}};
        const auto calendar = [](const QString &id, const QString &name, const QString &path) {
            return QJsonObject{{u"id"_s, id},
                               {u"account"_s, u"a-1"_s},
                               {u"url"_s, QString(u"https://dav.example.pl/dav/"_s + path + u'/')},
                               {u"name"_s, name},
                               {u"showEvents"_s, true}};
        };
        QJsonObject zadania = calendar(u"c-2"_s, u"Tylko zadania"_s, u"zadania"_s);
        zadania.insert(u"showEvents"_s, false);
        QJsonObject wspolny = calendar(u"c-3"_s, u"Wspólny"_s, u"wspolny"_s);
        wspolny.insert(u"readOnly"_s, true);
        const QJsonObject feed{
                {u"id"_s, u"s-1"_s}, {u"url"_s, u"https://example.pl/swieta.ics"_s}, {u"name"_s, u"Święta"_s}};
        writeFile(folder + u"/calendars.json"_s,
                  QJsonDocument(QJsonObject{{u"version"_s, 1},
                                            {u"accounts"_s, QJsonArray{account}},
                                            {u"calendars"_s,
                                             QJsonArray{calendar(u"c-1"_s, u"Praca"_s, u"praca"_s), zadania, wspolny}},
                                            {u"subscriptions"_s, QJsonArray{feed}}})
                          .toJson(QJsonDocument::Compact));
        writeFile(folder + u"/s-1.ics"_s,
                  "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:s1\r\nDTSTART;VALUE=DATE:20261111\r\n"
                  "SUMMARY:Święto Niepodległości\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
        QJsonObject members;
        const auto resource = [&members](const QString &name, const QString &uid, const QString &data) {
            members.insert(
                    u"/dav/praca/"_s + name,
                    QJsonObject{{u"etag"_s, u"\"1\""_s}, {u"uid"_s, uid}, {u"kind"_s, u"VEVENT"_s}, {u"data"_s, data}});
        };
        resource(u"stand.ics"_s, u"stand"_s,
                 u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:stand\r\nDTSTART:20261014T080000\r\n"
                 u"DTEND:20261014T081500\r\nRRULE:FREQ=WEEKLY;COUNT=4\r\nSUMMARY:Zażółć gęślą jaźń\r\nEND:VEVENT\r\n"
                 u"BEGIN:VEVENT\r\nUID:stand\r\nRECURRENCE-ID:20261028T080000\r\nDTSTART:20261028T090000\r\n"
                 u"DTEND:20261028T091500\r\nSUMMARY:Zażółć (później)\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"_s);
        resource(
                u"rada.ics"_s, u"rada"_s,
                u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:rada\r\nDTSTART:20261016T120000\r\n"
                u"DTEND:20261016T133000\r\nSUMMARY:Rada pedagogiczna\r\nLOCATION:Pokój 12\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"_s);
        writeFile(folder + u"/c-1.state.json"_s,
                  QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"resources"_s, members}})
                          .toJson(QJsonDocument::Compact));
        return dir;
    }

    /// The changes brulion-cli left for the app (<calendars>/inbox), oldest first.
    QList<QJsonObject> inboxOf(const QString &dir) const
    {
        QList<QJsonObject> out;
        const QDir inbox(stateDirOf(dir) + u"/calendars/inbox"_s);
        for (const QString &name : inbox.entryList({u"*.json"_s}, QDir::Files, QDir::Name))
            out.append(QJsonDocument::fromJson(readFile(inbox.filePath(name))).object());
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_home = m_tmp.filePath(u"home"_s);
        QDir().mkpath(m_home + u"/run"_s);
        QFile::setPermissions(m_home + u"/run"_s,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        // The stand-in app below computes the socket key in this process: the same runtime folder.
        qputenv("XDG_RUNTIME_DIR", (m_home + u"/run"_s).toUtf8());
        QVERIFY(QFileInfo::exists(QStringLiteral(BRULION_CLI)));
#ifdef Q_OS_WIN
        // The profile's names, as brulion-cli sets them for BRULION_PROFILE.
        QCoreApplication::setOrganizationName(u"Brulion-"_s + profile());
        QCoreApplication::setApplicationName(u"Brulion"_s);
        for (const QString &dir : profileDirs())
            QDir(dir).removeRecursively();
#endif
    }

#ifdef Q_OS_WIN
    void cleanupTestCase()
    {
        for (const QString &dir : profileDirs())
            QDir(dir).removeRecursively();
    }
#endif

    void versionAndHelp()
    {
        Run r = cli({u"version"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.json.value(u"ok"_s).toBool());
        QVERIFY(!r.result().value(u"version"_s).toString().isEmpty());
        QCOMPARE(r.result().value(u"mcpProtocol"_s).toString(), u"2026-07-28"_s);
        QVERIFY(r.result().value(u"mcpProtocols"_s).toArray().contains(u"2025-06-18"_s));
        // The plugins over llama.cpp this build has (found where the app looks; nothing loaded) …
        const QStringList plugins{u"model"_s, u"semantic"_s, u"speech"_s};
        for (const QString &name : plugins) {
            const QJsonObject plugin = r.result().value(u"plugins"_s).toObject().value(name).toObject();
            QVERIFY2(plugin.value(u"found"_s).isBool(), qPrintable(name));
            QVERIFY2(!plugin.contains(u"loaded"_s), qPrintable(name));
        }
        // … and with --plugins each one found loaded without a model (a Windows build tree has them
        // next to brulion-cli.exe, a debug build where they were built; a build without has none).
        r = cli({u"version"_s, u"--plugins"_s});
        QVERIFY2(r.code == 0, r.out.constData());
        for (const QString &name : plugins) {
            const QJsonObject plugin = r.result().value(u"plugins"_s).toObject().value(name).toObject();
            QCOMPARE(plugin.value(u"loaded"_s).toBool(), plugin.value(u"found"_s).toBool());
            QVERIFY2(!plugin.contains(u"error"_s), qPrintable(plugin.value(u"error"_s).toString()));
        }
        r = cli({u"--format"_s, u"text"_s, u"version"_s, u"--plugins"_s});
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.startsWith("brulion-cli ") && r.out.contains("search by meaning: "), r.out.constData());
        r = cli({u"--help"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("page read"));
        QVERIFY(r.err.isEmpty());
        r = cli({u"help"_s, u"--json"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"commands"_s).toArray().size() > 40);
        r = cli({u"schema"_s, u"task"_s, u"add"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"properties"_s).toObject().contains(u"remind"_s));
        r = cli({u"completion"_s, u"bash"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("complete -F"));
        r = cli({u"bogus"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"unknown_command"_s);
        QVERIFY(!r.json.value(u"ok"_s).toBool());
        // Text output and one-line JSON.
        r = cli({u"--format"_s, u"text"_s, u"date"_s, u"parse"_s, u"jutro o 9"_s, u"--now"_s, kNow});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.out, "2026-10-15 09:00\n"_ba);
        r = cli({u"--compact"_s, u"version"_s});
        QCOMPARE(r.out.count('\n'), 1);
    }

    void missingNotebook()
    {
        // No notebook anywhere.
        Run r = cli({u"info"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"no_vault"_s);
        // A mistyped folder is never created.
        const QString missing = m_tmp.filePath(u"brak/Notatki"_s);
        r = cli({u"--vault"_s, missing, u"capture"_s, u"x"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"vault_missing"_s);
        QVERIFY(!QFileInfo::exists(missing));
        QVERIFY(!QFileInfo::exists(m_tmp.filePath(u"brak"_s)));
        writeFile(m_tmp.filePath(u"plik.txt"_s), "x");
        r = cli({u"--vault"_s, m_tmp.filePath(u"plik.txt"_s), u"info"_s});
        QCOMPARE(r.errorCode(), u"vault_not_a_folder"_s);
        // init creates one.
        r = cli({u"init"_s, missing});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"created"_s).toBool());
        QVERIFY(QFileInfo(missing).isDir());
        r = cli({u"--vault"_s, missing, u"info"_s});
        QCOMPARE(r.code, 0);
    }

    // The index cache in the device state folder (shared with the app): the first `info` parses
    // and leaves the cache, the second reads every note from it; BRULION_NO_INDEX_CACHE turns it off.
    void indexCacheAcrossCalls()
    {
        QHash<QString, QByteArray> files;
        for (int i = 0; i < 210; ++i)
            files.insert(u"Strony/Strona %1.md"_s.arg(i), "- [[Strona 0]] żółw #t\n");
        const QString dir = vault(files);
        Run r = run(dir, {u"info"_s});
        QCOMPARE(r.code, 0);
        QJsonObject index = r.result().value(u"index"_s).toObject();
        QCOMPARE(index.value(u"notes"_s).toInt(), 210);
        QCOMPARE(index.value(u"parsed"_s).toInt(), 210);
        QCOMPARE(index.value(u"cache"_s).toString(), u"absent"_s);
        QVERIFY(index.value(u"workers"_s).toInt() >= 1);
        QVERIFY(QFileInfo::exists(stateDirOf(dir) + u"/index-cache.bin"_s));
        r = run(dir, {u"info"_s});
        index = r.result().value(u"index"_s).toObject();
        QCOMPARE(index.value(u"cache"_s).toString(), u"used"_s);
        QCOMPARE(index.value(u"fromCache"_s).toInt(), 210);
        QCOMPARE(index.value(u"parsed"_s).toInt(), 0);
        QProcessEnvironment env = environment();
        env.insert(u"BRULION_NO_INDEX_CACHE"_s, u"1"_s);
        env.insert(u"BRULION_INDEX_THREADS"_s, u"0"_s);
        r = cli({u"--vault"_s, dir, u"info"_s}, {}, env);
        index = r.result().value(u"index"_s).toObject();
        QCOMPARE(index.value(u"cache"_s).toString(), u"disabled"_s);
        QCOMPARE(index.value(u"workers"_s).toInt(), 0);
        QCOMPARE(index.value(u"parsed"_s).toInt(), 210);
    }

    void notebookFromTheEnvironmentAndTheAppSettings()
    {
        const QString dir = vault({{u"Dom.md"_s, "- dom\n"}});
        QProcessEnvironment env = environment();
        env.insert(u"BRULION_VAULT"_s, dir);
        Run r = cli({u"info"_s}, {}, env);
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"vault"_s).toObject().value(u"source"_s).toString(), u"environment"_s);
        // The app's current notebook, read from its settings — which stay byte for byte as they were.
        const QString ini = settingsFile();
        const QByteArray settings = "[ui]\nlanguage=en\n\n[vaults]\ncurrent=" + dir.toUtf8() + "\n";
        writeFile(ini, settings);
        const QDateTime mtime = QFileInfo(ini).lastModified();
        r = cli({u"info"_s, u"--now"_s, kNow});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"vault"_s).toObject().value(u"source"_s).toString(), u"settings"_s);
        QCOMPARE(r.json.value(u"vault"_s).toString(), dir);
        QCOMPARE(r.result().value(u"language"_s).toString(), u"en"_s); // the app's language
        QCOMPARE(r.result().value(u"counts"_s).toObject().value(u"pages"_s).toInt(), 1);
        r = cli({u"capture"_s, u"Call mom tomorrow at 6pm"_s, u"--now"_s, kNow});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/journals/2026-10-14.md"_s), "- [ ] Call mom ⏰ 2026-10-15 18:00\n"_ba);
        QCOMPARE(readFile(ini), settings);
        QCOMPARE(QFileInfo(ini).lastModified(), mtime);
        QFile::remove(ini);
    }

    void weeklyAndMonthlyNotes()
    {
        // kNow is Wednesday 2026-10-14: ISO week 42.
        const QString dir = vault({{u"templates/weekly.md"_s, "# Tydzień {{week}}: {{date:d.MM}}–{{end:d.MM}}\n- \n"},
                                   {u"journals/2026-10.md"_s, "- październik\n"}});
        Run r = run(dir, {u"week"_s, u"path"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-W42.md"_s);
        QCOMPARE(r.result().value(u"start"_s).toString(), u"2026-10-12"_s);
        QCOMPARE(r.result().value(u"end"_s).toString(), u"2026-10-18"_s);
        QCOMPARE(r.result().value(u"week"_s).toInt(), 42);
        QCOMPARE(r.result().value(u"exists"_s).toBool(), false);
        // Week 53 belongs to its week-year.
        r = run(dir, {u"week"_s, u"path"_s, u"2027-01-01"_s});
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-W53.md"_s);
        QCOMPARE(r.result().value(u"weekYear"_s).toInt(), 2026);
        r = run(dir, {u"week"_s, u"path"_s, u"next"_s, u"week"_s});
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-W43.md"_s);
        // Appending creates it from the weekly template.
        r = run(dir, {u"week"_s, u"append"_s, u"Przegląd celów — żółw"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/journals/2026-W42.md"_s),
                 QString(u"# Tydzień 42: 12.10–18.10\n- Przegląd celów — żółw\n"_s).toUtf8());
        QCOMPARE(r.result().value(u"line"_s).toInt(), 1);
        // The month's note exists: read it; another month by name.
        r = run(dir, {u"month"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"content"_s).toString(), u"- październik\n"_s);
        QCOMPARE(r.result().value(u"exists"_s).toBool(), true);
        r = run(dir, {u"month"_s, u"path"_s, u"listopad"_s});
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-11.md"_s);
        r = run(dir, {u"month"_s, u"append"_s, u"Plan"_s, u"--when"_s, u"następny miesiąc"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/journals/2026-11.md"_s), "- Plan\n"_ba);
        // Not a week: an error with examples.
        r = run(dir, {u"week"_s, u"path"_s, u"2027-W53"_s});
        QVERIFY(r.code != 0);
        QCOMPARE(r.errorCode(), u"week_not_understood"_s);
        r = run(dir, {u"month"_s, u"żółw"_s});
        QCOMPARE(r.errorCode(), u"month_not_understood"_s);
    }

    void calendars()
    {
        // The calendars the app set up on this device (<state>/calendars/): a subscription and a
        // CalDAV calendar with a weekly series, an exception and a Polish title. Read only: nothing
        // is fetched, no address is shown.
        const QString dir = vault({{u"Notatka.md"_s, "- x\n"}});
        const QString folder = stateDirOf(dir) + u"/calendars"_s;
        const QJsonObject account{{u"id"_s, u"a-1"_s}, {u"server"_s, u"https://tajny.example.pl"_s}, {u"user"_s, u"jan"_s},
                                  {u"name"_s, u"jan @ chmura"_s}};
        const QJsonObject praca{{u"id"_s, u"c-1"_s},
                                {u"account"_s, u"a-1"_s},
                                {u"url"_s, u"https://tajny.example.pl/dav/praca/"_s},
                                {u"name"_s, u"Praca"_s},
                                {u"showEvents"_s, true},
                                {u"syncTasks"_s, true},
                                {u"alarms"_s, false}};
        const QJsonObject zadania{{u"id"_s, u"c-2"_s},
                                  {u"account"_s, u"a-1"_s},
                                  {u"url"_s, u"https://tajny.example.pl/dav/zadania/"_s},
                                  {u"name"_s, u"Tylko zadania"_s},
                                  {u"showEvents"_s, false}};
        const QJsonObject feed{{u"id"_s, u"s-1"_s},
                               {u"url"_s, u"https://tajny.example.pl/swieta.ics?token=sekret"_s},
                               {u"name"_s, u"Święta"_s},
                               {u"visible"_s, true},
                               {u"refresh"_s, 360}};
        writeFile(folder + u"/calendars.json"_s,
                  QJsonDocument(QJsonObject{{u"version"_s, 1},
                                            {u"accounts"_s, QJsonArray{account}},
                                            {u"calendars"_s, QJsonArray{praca, zadania}},
                                            {u"subscriptions"_s, QJsonArray{feed}}})
                          .toJson(QJsonDocument::Compact));
        writeFile(folder + u"/s-1.ics"_s,
                  "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:s1\r\nDTSTART;VALUE=DATE:20261111\r\n"
                  "SUMMARY:Święto Niepodległości\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
        QJsonObject members;
        const auto resource = [&members](const QString &name, const QString &data) {
            members.insert(name, QJsonObject{{u"etag"_s, u"\"1\""_s}, {u"uid"_s, name}, {u"kind"_s, u"VEVENT"_s}, {u"data"_s, data}});
        };
        resource(u"/dav/praca/stand.ics"_s,
                 u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:stand\r\n"
                 u"DTSTART:20261014T080000Z\r\nDTEND:20261014T081500Z\r\nRRULE:FREQ=WEEKLY;COUNT=3\r\n"
                 u"SUMMARY:Zażółć gęślą jaźń\r\nLOCATION:Sala B\r\nEND:VEVENT\r\n"
                 u"BEGIN:VEVENT\r\nUID:stand\r\nRECURRENCE-ID:20261021T080000Z\r\nDTSTART:20261021T090000Z\r\n"
                 u"SUMMARY:Zażółć gęślą jaźń (później)\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"_s);
        writeFile(folder + u"/c-1.state.json"_s,
                  QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"resources"_s, members},
                                            {u"syncedAt"_s, u"2026-10-14T09:00:00"_s}})
                          .toJson(QJsonDocument::Compact));
        Run r = run(dir, {u"calendar"_s, u"list"_s});
        QCOMPARE(r.code, 0);
        QJsonArray list = r.result().value(u"calendars"_s).toArray();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).toObject().value(u"kind"_s).toString(), u"subscription"_s);
        QCOMPARE(list.at(0).toObject().value(u"name"_s).toString(), u"Święta"_s);
        QCOMPARE(list.at(1).toObject().value(u"account"_s).toString(), u"jan @ chmura"_s);
        QCOMPARE(list.at(1).toObject().value(u"alarms"_s).toBool(true), false);
        QCOMPARE(list.at(1).toObject().value(u"tasks"_s).toBool(), true);
        QVERIFY(!r.out.contains("sekret")); // an address may hold a secret token
        QVERIFY(!r.out.contains("tajny.example.pl"));
        r = run(dir, {u"calendar"_s, u"events"_s, u"--from"_s, u"2026-10-14"_s, u"--to"_s, u"2026-11-30"_s});
        QCOMPARE(r.code, 0);
        QJsonArray events = r.result().value(u"events"_s).toArray();
        QStringList titles;
        for (const QJsonValue &e : events)
            titles.append(e.toObject().value(u"start"_s).toString().left(10) + u' ' + e.toObject().value(u"title"_s).toString());
        // Three Wednesdays of the series (the second moved by an exception), then the holiday.
        QCOMPARE(titles.size(), 4);
        QVERIFY(titles.at(0).endsWith(u"Zażółć gęślą jaźń"_s));
        QVERIFY(titles.at(1).endsWith(u"Zażółć gęślą jaźń (później)"_s));
        QVERIFY(titles.at(3).endsWith(u"Święto Niepodległości"_s));
        QCOMPARE(events.at(3).toObject().value(u"allDay"_s).toBool(), true);
        QCOMPARE(events.at(3).toObject().value(u"end"_s).toString(), u"2026-11-11"_s);
        QCOMPARE(events.at(0).toObject().value(u"location"_s).toString(), u"Sala B"_s);
        QCOMPARE(events.at(0).toObject().value(u"recurring"_s).toBool(), true);
        // One calendar, by name (any letter case) or id; a limit; a bad range.
        r = run(dir, {u"calendar"_s, u"events"_s, u"--from"_s, u"2026-10-14"_s, u"--to"_s, u"2026-11-30"_s, u"--calendar"_s, u"święta"_s});
        QCOMPARE(r.result().value(u"events"_s).toArray().size(), 1);
        r = run(dir, {u"calendar"_s, u"events"_s, u"--from"_s, u"2026-10-14"_s, u"--to"_s, u"2026-11-30"_s, u"--limit"_s, u"2"_s});
        QCOMPARE(r.result().value(u"events"_s).toArray().size(), 2);
        QCOMPARE(r.result().value(u"truncated"_s).toBool(), true);
        r = run(dir, {u"calendar"_s, u"events"_s, u"--from"_s, u"2026-11-30"_s, u"--to"_s, u"2026-10-14"_s});
        QCOMPARE(r.errorCode(), u"bad_range"_s);
        // Text form; a notebook without calendars says so and fails nothing.
        r = run(dir, {u"--format"_s, u"text"_s, u"calendar"_s, u"events"_s, u"--from"_s, u"2026-10-14"_s, u"--to"_s, u"2026-10-14"_s});
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains("Zażółć gęślą jaźń"), r.out.constData());
        const QString bare = vault({{u"Notatka.md"_s, "- x\n"}});
        r = run(bare, {u"calendar"_s, u"list"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"calendars"_s).toArray().isEmpty());
        r = run(bare, {u"calendar"_s, u"events"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"events"_s).toArray().isEmpty());
    }

    void calendarChanges()
    {
        // CalDAV events created, changed and deleted: left for the app in the inbox (it is the one
        // writer of the calendars' state), checked first against the calendar as it will be.
        const QString dir = calendarNotebook();
        const QString state = stateDirOf(dir) + u"/calendars/c-1.state.json"_s;
        const QByteArray stateBefore = readFile(state);
        Run r = run(dir, {u"calendar"_s, u"add"_s, u"Obiad z Zosią"_s, u"--start"_s, u"2026-10-20 13:00"_s, u"--end"_s,
                          u"14:30"_s, u"--location"_s, u"Kraków, Rynek"_s, u"--reminder"_s, u"15m"_s, u"--every"_s,
                          u"co tydzień"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QCOMPARE(r.result().value(u"queued"_s).toBool(), true);
        QCOMPARE(r.result().value(u"kind"_s).toString(), u"create"_s);
        QCOMPARE(r.result().value(u"calendar"_s).toString(), u"Praca"_s); // the only one taking events
        const QString uid = r.result().value(u"uid"_s).toString();
        QVERIFY(uid.startsWith(u"brulion-event-"_s));
        QJsonObject event = r.result().value(u"event"_s).toObject();
        QCOMPARE(event.value(u"start"_s).toString(), u"2026-10-20T13:00"_s);
        QCOMPARE(event.value(u"end"_s).toString(), u"2026-10-20T14:30"_s);
        QCOMPARE(event.value(u"rule"_s).toString(), u"every week"_s);
        QCOMPARE(event.value(u"reminderMinutes"_s).toInt(), 15);
        QCOMPARE(event.value(u"location"_s).toString(), u"Kraków, Rynek"_s);
        QCOMPARE(r.result().value(u"app"_s).toObject().value(u"running"_s).toBool(true), false);
        QList<QJsonObject> inbox = inboxOf(dir);
        QCOMPARE(inbox.size(), 1);
        QCOMPARE(inbox.first().value(u"kind"_s).toString(), u"create"_s);
        QCOMPARE(inbox.first().value(u"uid"_s).toString(), uid);
        QCOMPARE(inbox.first().value(u"title"_s).toString(), u"Obiad z Zosią"_s);
        QCOMPARE(inbox.first().value(u"start"_s).toString(), u"2026-10-20T13:00"_s); // the wall clock
        QCOMPARE(inbox.first().value(u"end"_s).toString(), u"14:30"_s);
        QCOMPARE(readFile(state), stateBefore); // never the app's state
        // Shown (marked pending) until the app takes it; changed again before that.
        r = run(dir, {u"calendar"_s, u"events"_s, u"--from"_s, u"2026-10-20"_s, u"--to"_s, u"2026-10-20"_s});
        QJsonArray events = r.result().value(u"events"_s).toArray();
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.at(0).toObject().value(u"title"_s).toString(), u"Obiad z Zosią"_s);
        QCOMPARE(events.at(0).toObject().value(u"pending"_s).toBool(), true);
        r = run(dir, {u"calendar"_s, u"edit"_s, uid, u"--title"_s, u"Obiad z Zosią i Kubą"_s, u"--start"_s,
                      u"2026-10-21"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        event = r.result().value(u"event"_s).toObject();
        QCOMPARE(event.value(u"title"_s).toString(), u"Obiad z Zosią i Kubą"_s);
        QCOMPARE(event.value(u"start"_s).toString(), u"2026-10-21T13:00"_s); // its time and length kept
        QCOMPARE(event.value(u"end"_s).toString(), u"2026-10-21T14:30"_s);
        // All-day, its day alone.
        r = run(dir,
                {u"calendar"_s, u"add"_s, u"Wigilia"_s, u"--start"_s, u"2026-12-24"_s, u"--calendar"_s, u"praca"_s});
        QCOMPARE(r.result().value(u"event"_s).toObject().value(u"allDay"_s).toBool(), true);
        QCOMPARE(r.result().value(u"event"_s).toObject().value(u"end"_s).toString(), u"2026-12-24"_s);
        // One occurrence of the series, named by its day; its instance in calendar events.
        r = run(dir, {u"calendar"_s, u"events"_s, u"--from"_s, u"2026-10-28"_s, u"--to"_s, u"2026-10-28"_s});
        events = r.result().value(u"events"_s).toArray();
        QCOMPARE(events.size(), 2); // the moved stand-up, the lunch (Wednesdays now)
        QCOMPARE(events.at(0).toObject().value(u"title"_s).toString(), u"Zażółć (później)"_s);
        QCOMPARE(events.at(0).toObject().value(u"instance"_s).toString(), u"20261028T080000"_s);
        QVERIFY(!events.at(0).toObject().contains(u"pending"_s));
        r = run(dir,
                {u"calendar"_s, u"edit"_s, u"stand"_s, u"--occurrence"_s, u"2026-10-21"_s, u"--start"_s, u"10:00"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QCOMPARE(r.result().value(u"instance"_s).toString(), u"20261021T080000"_s);
        QCOMPARE(r.result().value(u"event"_s).toObject().value(u"start"_s).toString(), u"2026-10-21T10:00"_s);
        QCOMPARE(r.result().value(u"event"_s).toObject().value(u"end"_s).toString(), u"2026-10-21T10:15"_s);
        // Deleting is destructive: one occurrence, then the whole meeting.
        r = run(dir, {u"calendar"_s, u"delete"_s, u"stand"_s, u"--occurrence"_s, u"20261104T080000"_s});
        QCOMPARE(r.code, 7);
        r = run(dir, {u"--allow-destructive"_s, u"calendar"_s, u"delete"_s, u"stand"_s, u"--occurrence"_s,
                      u"20261104T080000"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QCOMPARE(r.result().value(u"kind"_s).toString(), u"delete"_s);
        r = run(dir, {u"--allow-destructive"_s, u"calendar"_s, u"delete"_s, u"rada"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        inbox = inboxOf(dir);
        QCOMPARE(inbox.size(), 6);
        QCOMPARE(inbox.last().value(u"kind"_s).toString(), u"delete"_s);
        QVERIFY(!inbox.last().contains(u"instance"_s));
        r = run(dir, {u"calendar"_s, u"events"_s, u"--from"_s, u"2026-10-14"_s, u"--to"_s, u"2026-11-30"_s});
        QStringList titles;
        for (const QJsonValue &e : r.result().value(u"events"_s).toArray())
            titles.append(e.toObject().value(u"start"_s).toString() + u' ' + e.toObject().value(u"title"_s).toString());
        QCOMPARE(titles,
                 (QStringList{u"2026-10-14T08:00 Zażółć gęślą jaźń"_s, u"2026-10-21T10:00 Zażółć gęślą jaźń"_s,
                              u"2026-10-21T13:00 Obiad z Zosią i Kubą"_s, u"2026-10-28T09:00 Zażółć (później)"_s,
                              u"2026-10-28T13:00 Obiad z Zosią i Kubą"_s, u"2026-11-04T13:00 Obiad z Zosią i Kubą"_s,
                              u"2026-11-11 Święto Niepodległości"_s, u"2026-11-11T13:00 Obiad z Zosią i Kubą"_s,
                              u"2026-11-18T13:00 Obiad z Zosią i Kubą"_s, u"2026-11-25T13:00 Obiad z Zosią i Kubą"_s}));
        // A dry run leaves nothing; --read-only refuses.
        r = run(dir, {u"--dry-run"_s, u"calendar"_s, u"add"_s, u"Próba"_s, u"--start"_s, u"jutro 9:00"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QCOMPARE(r.result().value(u"queued"_s).toBool(true), false);
        QCOMPARE(r.result().value(u"event"_s).toObject().value(u"start"_s).toString(), u"2026-10-15T09:00"_s);
        QCOMPARE(inboxOf(dir).size(), 6);
        r = run(dir, {u"--read-only"_s, u"calendar"_s, u"add"_s, u"Próba"_s, u"--start"_s, u"jutro"_s});
        QCOMPARE(r.code, 7);
        // Text for people.
        r = run(dir, {u"--format"_s, u"text"_s, u"--dry-run"_s, u"calendar"_s, u"add"_s, u"Zebranie"_s, u"--start"_s,
                      u"2026-10-22 10:00"_s});
        QVERIFY2(r.out.contains("Praca: “Zebranie” 2026-10-22 10:00–11:00"), r.out.constData());
        QCOMPARE(inboxOf(dir).size(), 6);
    }

    void calendarChangesRefused_data()
    {
        QTest::addColumn<QStringList>("args");
        QTest::addColumn<int>("exit");
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("reason"); // details.reason, "" none
        const QStringList add{u"calendar"_s, u"add"_s, u"Spotkanie"_s, u"--start"_s, u"2026-10-20 13:00"_s};
        QTest::newRow("a subscription") << (add + QStringList{u"--calendar"_s, u"Święta"_s}) << 7
                                        << "calendar_read_only"
                                        << "subscription";
        QTest::newRow("a subscription's event") << QStringList{u"calendar"_s, u"edit"_s, u"s1"_s, u"--title"_s, u"x"_s}
                                                << 7 << "calendar_read_only" << "subscription";
        QTest::newRow("read-only on its server")
                << (add + QStringList{u"--calendar"_s, u"Wspólny"_s}) << 7 << "calendar_read_only" << "read_only";
        QTest::newRow("events not shown") << (add + QStringList{u"--calendar"_s, u"tylko zadania"_s}) << 7
                                          << "calendar_read_only" << "events_hidden";
        QTest::newRow("no such calendar")
                << (add + QStringList{u"--calendar"_s, u"Żaden"_s}) << 3 << "calendar_not_found" << "";
        QTest::newRow("no such event") << QStringList{u"calendar"_s, u"edit"_s, u"brak"_s, u"--title"_s, u"x"_s} << 3
                                       << "event_not_found" << "";
        QTest::newRow("no such occurrence") << QStringList{u"calendar"_s,   u"edit"_s,    u"stand"_s, u"--occurrence"_s,
                                                           u"2026-10-15"_s, u"--title"_s, u"x"_s}
                                            << 3 << "occurrence_not_found" << "";
        QTest::newRow("an occurrence's own rule")
                << QStringList{u"calendar"_s,   u"edit"_s,    u"stand"_s,   u"--occurrence"_s,
                               u"2026-10-21"_s, u"--every"_s, u"co dzień"_s}
                << 2 << "occurrence_rule" << "occurrence_rule";
        QTest::newRow("its end before its start")
                << (add + QStringList{u"--end"_s, u"2026-10-19"_s}) << 2 << "end_before_start" << "end_before_start";
        QTest::newRow("all-day at a time") << (add + QStringList{u"--all-day"_s}) << 2 << "all_day_with_time"
                                           << "all_day_with_time";
        QTest::newRow("all-day and timed")
                << (add + QStringList{u"--all-day"_s, u"--timed"_s}) << 2 << "bad_arguments" << "";
        QTest::newRow("a day that is none")
                << QStringList{u"calendar"_s, u"add"_s, u"Spotkanie"_s, u"--start"_s, u"żółw"_s} << 6
                << "date_not_understood" << "";
        QTest::newRow("a reminder that is none")
                << (add + QStringList{u"--reminder"_s, u"kwadrans"_s}) << 6 << "reminder_not_understood" << "";
        QTest::newRow("a recurrence that is none")
                << (add + QStringList{u"--every"_s, u"raz na ruski rok"_s}) << 6 << "recurrence_not_understood" << "";
        QTest::newRow("a title of spaces") << QStringList{u"calendar"_s, u"edit"_s, u"rada"_s, u"--title"_s, u"   "_s}
                                           << 2 << "no_title" << "no_title";
    }

    void calendarChangesRefused()
    {
        QFETCH(QStringList, args);
        QFETCH(int, exit);
        QFETCH(QString, code);
        QFETCH(QString, reason);
        const QString dir = calendarNotebook();
        const Run r = run(dir, args);
        QCOMPARE(r.code, exit);
        QCOMPARE(r.errorCode(), code);
        const QJsonObject error = r.json.value(u"error"_s).toObject();
        if (!reason.isEmpty())
            QCOMPARE(error.value(u"details"_s).toObject().value(u"reason"_s).toString(), reason);
        if (reason == u"subscription")
            QVERIFY2(error.value(u"message"_s).toString().contains(u"ICS subscription"_s), qPrintable(r.describe()));
        QVERIFY(inboxOf(dir).isEmpty());
        // No calendars set up on the device: said, nothing left.
        if (code == u"calendar_not_found") {
            const QString bare = vault({{u"Notatka.md"_s, "- x\n"}});
            const Run none = run(bare, {u"calendar"_s, u"add"_s, u"Spotkanie"_s, u"--start"_s, u"jutro"_s});
            QCOMPARE(none.code, 5);
            QCOMPARE(none.errorCode(), u"no_calendars"_s);
        }
    }

    // card notes / card review-note (docs/SYNTAX.md §15 "Notes to review"): the notes tagged #review
    // as a whole, their state in the frontmatter, the plugin's scheduling (kNow is 2026-10-14).
    void cardNotes()
    {
        const QString dir = vault({{u"A.md"_s, "---\ntags: [review]\nsr-due: 2026-10-10\nsr-interval: 3\nsr-ease: 250\n---\nTreść\n"},
                                   {u"Nauka/Nowa.md"_s, "#review\nżółw\n"},
                                   {u"Inna.md"_s, "Bez tagu\n"}});
        Run r = run(dir, {u"card"_s, u"notes"_s});
        QCOMPARE(r.code, 0);
        const QJsonArray notes = r.result().value(u"notes"_s).toArray();
        QCOMPARE(notes.size(), 2);
        QCOMPARE(notes[0].toObject().value(u"path"_s).toString(), u"A.md"_s);
        QCOMPARE(notes[0].toObject().value(u"status"_s).toString(), u"due"_s);
        QCOMPARE(notes[0].toObject().value(u"next"_s).toObject().value(u"good"_s).toObject().value(u"due"_s).toString(),
                 u"2026-10-27"_s);
        QCOMPARE(notes[1].toObject().value(u"status"_s).toString(), u"new"_s);
        QCOMPARE(r.result().value(u"counts"_s).toObject().value(u"due"_s).toInt(), 1);
        QCOMPARE(r.result().value(u"counts"_s).toObject().value(u"new"_s).toInt(), 1);
        // Another tag of the notebook's own.
        r = run(dir, {u"card"_s, u"notes"_s, u"--review-tags"_s, u"nic"_s});
        QCOMPARE(r.result().value(u"notes"_s).toArray().size(), 0);

        // Nothing written by a dry run; then the frontmatter of the note, the rest as it was.
        const QByteArray before = readFile(dir + u"/A.md"_s);
        r = run(dir, {u"--dry-run"_s, u"card"_s, u"review-note"_s, u"A"_s, u"good"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/A.md"_s), before);
        r = run(dir, {u"card"_s, u"review-note"_s, u"A"_s, u"good"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"after"_s).toObject().value(u"due"_s).toString(), u"2026-10-27"_s);
        QCOMPARE(readFile(dir + u"/A.md"_s),
                 QByteArray("---\ntags: [review]\nsr-due: 2026-10-27\nsr-interval: 13\nsr-ease: 250\n---\nTreść\n"));
        // A new note gets a frontmatter; by its path, the Polish grade.
        r = run(dir, {u"card"_s, u"review-note"_s, u"Nauka/Nowa.md"_s, u"łatwe"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/Nauka/Nowa.md"_s),
                 QByteArray("---\nsr-due: 2026-10-18\nsr-interval: 4\nsr-ease: 270\n---\n#review\nżółw\n"));
        r = run(dir, {u"card"_s, u"notes"_s, u"--due"_s});
        QCOMPARE(r.result().value(u"notes"_s).toArray().size(), 0);
        // Mistakes: a grade that is none, a note without the tag, a note that does not exist.
        r = run(dir, {u"card"_s, u"review-note"_s, u"A"_s, u"again"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"bad_argument"_s);
        r = run(dir, {u"card"_s, u"review-note"_s, u"Inna"_s, u"good"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"not_review_note"_s);
        QCOMPARE(readFile(dir + u"/Inna.md"_s), QByteArray("Bez tagu\n"));
        r = run(dir, {u"card"_s, u"review-note"_s, u"Brak"_s, u"good"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"note_not_found"_s);
    }

    void habits()
    {
        // kNow is Wednesday 2026-10-14.
        const QString dir =
                vault({{u"Nawyki.md"_s, "- Medytacja\n  habit:: codziennie\n- Czytanie książek\n  habit:: 3 razy w "
                                        "tygodniu\n- Coś\n  habit:: kiedy mi się zachce\n"},
                       {u"templates/journal.md"_s, "# Dzień\n- \n"},
                       {u"journals/2026-10-12.md"_s, "- [x] Medytacja #nawyk\r\n- [x] Spacer #nawyk\r\n"},
                       {u"journals/2026-10-13.md"_s, "- [x] Medytacja #nawyk\n- [x] Czytanie książek #habit\n"}});
        Run r = run(dir, {u"habit"_s, u"list"_s, u"--days"_s, u"3"_s});
        QCOMPARE(r.code, 0);
        const QJsonArray list = r.result().value(u"habits"_s).toArray();
        QCOMPARE(list.size(), 3);
        const QJsonObject med = list[0].toObject();
        QCOMPARE(med.value(u"name"_s).toString(), u"Medytacja"_s);
        QCOMPARE(med.value(u"streak"_s).toObject().value(u"current"_s).toInt(), 2);
        QCOMPARE(med.value(u"today"_s).toObject().value(u"due"_s).toBool(), true);
        QCOMPARE(med.value(u"today"_s).toObject().value(u"mark"_s).toString(), QString());
        QCOMPARE(med.value(u"days"_s).toArray().size(), 3);
        QCOMPARE(med.value(u"days"_s).toArray()[2].toObject().value(u"state"_s).toString(), u"due"_s);
        QCOMPARE(med.value(u"rate"_s).toDouble(), 1.0);
        const QJsonObject read = list[1].toObject();
        QCOMPARE(read.value(u"period"_s).toObject().value(u"done"_s).toInt(), 1);
        QCOMPARE(read.value(u"period"_s).toObject().value(u"target"_s).toInt(), 3);
        QCOMPARE(list[2].toObject().value(u"problem"_s).toString(), u"schedule_not_understood"_s);
        QCOMPARE(r.result().value(u"undefined"_s).toArray(), QJsonArray{u"spacer"_s});

        // Tick today: the note is created from the template; the rest of the notes stay.
        r = run(dir, {u"--lang"_s, u"pl"_s, u"habit"_s, u"tick"_s, u"medytacja"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"created"_s).toBool(), true);
        QCOMPARE(r.result().value(u"mark"_s).toString(), u"done"_s);
        QCOMPARE(readFile(dir + u"/journals/2026-10-14.md"_s),
                 QString(u"# Dzień\n- [x] Medytacja #nawyk ✅ 2026-10-14\n"_s).toUtf8());
        r = run(dir, {u"habit"_s, u"list"_s});
        QCOMPARE(r.result()
                         .value(u"habits"_s)
                         .toArray()[0]
                         .toObject()
                         .value(u"streak"_s)
                         .toObject()
                         .value(u"current"_s)
                         .toInt(),
                 3);
        // Ticking again changes nothing; a past day next to its checks, CRLF kept; a multi-word name.
        r = run(dir, {u"habit"_s, u"tick"_s, u"Medytacja"_s});
        QCOMPARE(r.result().value(u"changed"_s).toBool(), false);
        r = run(dir, {u"--lang"_s, u"pl"_s, u"habit"_s, u"set"_s, u"Czytanie książek"_s, u"pominięte"_s, u"--date"_s,
                      u"przedwczoraj"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(
                readFile(dir + u"/journals/2026-10-12.md"_s),
                QString(u"- [x] Medytacja #nawyk\r\n- [x] Spacer #nawyk\r\n- [-] Czytanie książek #nawyk ❌ 2026-10-14\r\n"_s)
                        .toUtf8());
        QCOMPARE(r.result().value(u"line"_s).toInt(), 2);
        r = run(dir, {u"habit"_s, u"set"_s, u"Czytanie książek"_s, u"open"_s, u"--date"_s, u"2026-10-12"_s});
        QCOMPARE(r.result().value(u"before"_s).toString(), u"skipped"_s);
        QVERIFY(readFile(dir + u"/journals/2026-10-12.md"_s)
                        .endsWith(QString(u"- [ ] Czytanie książek #nawyk\r\n"_s).toUtf8()));
        // Open on a day without a note writes nothing; a dry run writes nothing.
        r = run(dir, {u"habit"_s, u"set"_s, u"Medytacja"_s, u"open"_s, u"--date"_s, u"2026-10-01"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(!QFileInfo::exists(dir + u"/journals/2026-10-01.md"_s));
        r = run(dir, {u"--dry-run"_s, u"habit"_s, u"skip"_s, u"Medytacja"_s, u"--date"_s, u"2026-10-02"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(!QFileInfo::exists(dir + u"/journals/2026-10-02.md"_s));
        // Errors.
        r = run(dir, {u"habit"_s, u"tick"_s, u"Pływanie"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"habit_not_found"_s);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"details"_s).toObject().value(u"habits"_s).toArray().size(),
                 3);
        r = run(dir, {u"habit"_s, u"set"_s, u"Medytacja"_s, u"może"_s});
        QCOMPARE(r.code, 2);
        r = run(dir, {u"--read-only"_s, u"habit"_s, u"tick"_s, u"Medytacja"_s});
        QCOMPARE(r.code, 7);
        r = run(dir, {u"--format"_s, u"text"_s, u"habit"_s, u"list"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(QString::fromUtf8(r.out).contains(u"[x] Medytacja"_s));
    }

    void gym()
    {
        // kNow is Wednesday 2026-10-14: the weeks of 2026-10-05 and 2026-10-12.
        const QString dir =
                vault({{u"journals/2026-10-05.md"_s,
                        "- Trening A #trening\n  - [x] [[Przysiad]] 100 kg 5×5\n  - [x] [[Podciąganie]] MC × 10\n"
                        "- 80 kg #waga\n"},
                       {u"journals/2026-10-13.md"_s, "- Trening B #trening\r\n  - [x] [[Martwy ciąg]] 140 kg 1×5\r\n"
                                                     "- 81 kg 20 % #waga\r\n"}});
        Run r = run(dir, {u"--lang"_s, u"pl"_s, u"gym"_s, u"volume"_s, u"--weeks"_s, u"2"_s});
        QCOMPARE(r.code, 0);
        QJsonObject o = r.result();
        QCOMPARE(o.value(u"weeks"_s).toArray(), (QJsonArray{u"2026-10-05"_s, u"2026-10-12"_s}));
        QCOMPARE(o.value(u"totals"_s).toArray()[0].toDouble(), 3300.0); // 2500 + 80 kg × 10
        QCOMPARE(o.value(u"totals"_s).toArray()[1].toDouble(), 700.0);
        QCOMPARE(o.value(u"muscles"_s).toArray()[0].toObject().value(u"id"_s).toString(), u"quadriceps"_s);
        QCOMPARE(o.value(u"muscles"_s).toArray()[0].toObject().value(u"name"_s).toString(), u"czworogłowe uda"_s);
        // in pounds
        r = run(dir, {u"gym"_s, u"volume"_s, u"--weeks"_s, u"2"_s, u"--unit"_s, u"lb"_s});
        QCOMPARE(r.result().value(u"unit"_s).toString(), u"lb"_s);
        QCOMPARE(r.result().value(u"totals"_s).toArray()[1].toDouble(), 1543.2); // 700 kg

        // The log, a weigh-in written at the end of today's note (made: there is none), CRLF kept
        // in another day's.
        r = run(dir, {u"gym"_s, u"weights"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"entries"_s).toArray().size(), 2);
        QCOMPARE(r.result().value(u"fat"_s).toDouble(), 20.0);
        r = run(dir, {u"--lang"_s, u"pl"_s, u"gym"_s, u"weigh"_s, u"82,5"_s, u"--fat"_s, u"18"_s, u"--note"_s,
                      u"rano, na czczo"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"line"_s).toString(), u"82,5 kg 18 % rano, na czczo #waga"_s);
        QCOMPARE(readFile(dir + u"/journals/2026-10-14.md"_s),
                 QString(u"- 82,5 kg 18 % rano, na czczo #waga\n"_s).toUtf8());
        r = run(dir, {u"--lang"_s, u"pl"_s, u"gym"_s, u"weigh"_s, u"181 lb"_s, u"--date"_s, u"wczoraj"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(
                readFile(dir + u"/journals/2026-10-13.md"_s),
                QString(u"- Trening B #trening\r\n  - [x] [[Martwy ciąg]] 140 kg 1×5\r\n- 81 kg 20 % #waga\r\n- 181 lb #waga\r\n"_s)
                        .toUtf8());
        r = run(dir,
                {u"--lang"_s, u"en"_s, u"--dry-run"_s, u"gym"_s, u"weigh"_s, u"80.5"_s, u"--date"_s, u"2026-10-01"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(!QFileInfo::exists(dir + u"/journals/2026-10-01.md"_s));
        r = run(dir, {u"gym"_s, u"weights"_s, u"--from"_s, u"2026-10-13"_s});
        QCOMPARE(r.result().value(u"entries"_s).toArray().size(), 3);
        QCOMPARE(r.result().value(u"latest"_s).toDouble(), 82.5);

        // The catalog: names in either language, filters, one exercise with its instructions.
        r = run(dir, {u"--lang"_s, u"pl"_s, u"gym"_s, u"exercises"_s, u"martwy"_s, u"ciąg"_s, u"--limit"_s, u"3"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"exercises"_s).toArray().size(), 3);
        QCOMPARE(r.result().value(u"exercises"_s).toArray()[0].toObject().value(u"id"_s).toString(), u"deadlift"_s);
        r = run(dir, {u"gym"_s, u"exercises"_s, u"--muscle"_s, u"biceps"_s, u"--equipment"_s, u"cable"_s});
        QVERIFY(r.result().value(u"total"_s).toInt() >= 5);
        r = run(dir, {u"gym"_s, u"exercise"_s, u"face-pull"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(!r.result().value(u"instructions"_s).toArray().isEmpty());

        // Errors.
        r = run(dir, {u"gym"_s, u"exercise"_s, u"nie-ma"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"exercise_not_found"_s);
        r = run(dir, {u"gym"_s, u"exercises"_s, u"--muscle"_s, u"skrzydła"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"bad_argument"_s);
        r = run(dir, {u"gym"_s, u"weigh"_s, u"dużo"_s});
        QCOMPARE(r.code, 2);
        r = run(dir, {u"--read-only"_s, u"gym"_s, u"weigh"_s, u"80"_s});
        QCOMPARE(r.code, 7);
        r = run(dir, {u"--format"_s, u"text"_s, u"--lang"_s, u"pl"_s, u"gym"_s, u"volume"_s});
        QCOMPARE(r.code, 0);
        QVERIFY2(QString::fromUtf8(r.out).contains(u"czworogłowe uda"_s), r.out.constData());
    }

    void obsidianVaultSettings()
    {
        // An Obsidian vault without .brulion/config.json: its daily notes settings are used.
        const QString dir = vault({{u".obsidian/daily-notes.json"_s, R"({"folder":"Dziennik","format":"DD.MM.YYYY"})"},
                                   {u"Dziennik/13.10.2026.md"_s, "- wtorek\n"}});
        Run r = run(dir, {u"info"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"config"_s).toObject().value(u"journalsFolder"_s).toString(), u"Dziennik"_s);
        QCOMPARE(r.result().value(u"configFromObsidian"_s).toArray().first().toString(), u"journalsFolder"_s);
        r = run(dir, {u"journal"_s, u"path"_s, u"2026-10-20"_s});
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Dziennik/20.10.2026.md"_s);
        r = run(dir, {u"journal"_s, u"append"_s, u"Kupić żurek"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/Dziennik/14.10.2026.md"_s), "- Kupić żurek\n"_ba);
        QVERIFY(!QFileInfo::exists(dir + u"/.brulion/config.json"_s));
    }

    void captureAndJournals()
    {
        const QString dir = vault({{u"templates/journal.md"_s, "## {{weekday}}\n- \n"}});
        Run r = run(dir, {u"capture"_s, u"Zadzwonić do Łukasza jutro o 9"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"kind"_s).toString(), u"reminder"_s);
        QCOMPARE(r.result().value(u"when"_s).toObject().value(u"date"_s).toString(), u"2026-10-15"_s);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-10-14.md"_s);
        QCOMPARE(readFile(dir + u"/journals/2026-10-14.md"_s),
                 (u"## "_s + wednesday() + u"\n- [ ] Zadzwonić do Łukasza ⏰ 2026-10-15 09:00\n"_s).toUtf8());
        QVERIFY(r.result().value(u"ref"_s).toString().startsWith(u"journals/2026-10-14.md#L2@"_s));
        r = run(dir, {u"capture"_s, u"Oddać książkę w piątek"_s});
        QCOMPARE(r.result().value(u"kind"_s).toString(), u"due"_s);
        r = run(dir, {u"capture"_s, u"wczoraj byłem u lekarza"_s});
        QCOMPARE(r.result().value(u"kind"_s).toString(), u"note"_s);
        r = run(dir, {u"capture"_s, u"   "_s});
        QCOMPARE(r.code, 2);
        // journal append on another day, journal read, journal path.
        r = run(dir, {u"journal"_s, u"append"_s, u"Notatka z wczoraj"_s, u"--date"_s, u"wczoraj"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"date"_s).toString(), u"2026-10-13"_s);
        QCOMPARE(readFile(dir + u"/journals/2026-10-13.md"_s),
                 (u"## "_s + weekday(2) + u"\n- Notatka z wczoraj\n"_s).toUtf8());
        r = run(dir, {u"journal"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"content"_s).toString().contains(u"Łukasza"_s));
        r = run(dir, {u"journal"_s, u"path"_s, u"2026-10-20"_s});
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-10-20.md"_s);
        QVERIFY(!r.result().value(u"exists"_s).toBool());
        r = run(dir, {u"journal"_s, u"read"_s, u"32.13"_s});
        QCOMPARE(r.code, 6);
        QCOMPARE(r.errorCode(), u"date_not_understood"_s);
        // CRLF and a BOM stay.
        writeFile(dir + u"/journals/2026-10-12.md"_s, "\xEF\xBB\xBF- a\r\n- b\r\n");
        r = run(dir, {u"journal"_s, u"append"_s, u"--date"_s, u"2026-10-12"_s, u"ż"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/journals/2026-10-12.md"_s), "\xEF\xBB\xBF- a\r\n- b\r\n- ż\r\n"_ba);
    }

    void tasks()
    {
        const QString dir =
                vault({{u"Dom.md"_s, "# Dom\n- [ ] Pomalować płot 📅 2026-10-20\n- [ ] Podlać kwiaty 🔁 every week 📅 "
                                     "2026-10-14\n- [x] Zrobione ✅ 2026-10-01\n"}});
        Run r = run(dir, {u"task"_s, u"add"_s, u"Kupić żółte jabłka"_s, u"--remind"_s, u"jutro 9:00"_s, u"--every"_s,
                          u"co tydzień"_s, u"--priority"_s, u"wysoki"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"written"_s).toString(),
                 u"- [ ] Kupić żółte jabłka ⏫ ⏰ 2026-10-15 09:00 🔁 every week"_s);
        r = run(dir, {u"task"_s, u"add"_s, u"Stare"_s, u"--due"_s, u"wczoraj"_s, u"--page"_s, u"Dom"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.warnings().contains(u"date_in_past"_s));
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Dom.md"_s);
        r = run(dir, {u"task"_s, u"add"_s, u"x"_s, u"--due"_s, u"blablabla"_s});
        QCOMPARE(r.code, 6);
        r = run(dir, {u"task"_s, u"add"_s, u"x"_s, u"--every"_s, u"czasami"_s});
        QCOMPARE(r.code, 6);
        QCOMPARE(r.errorCode(), u"recurrence_not_understood"_s);
        // List open tasks with their refs; complete the recurring one.
        r = run(dir, {u"task"_s, u"list"_s, u"--page"_s, u"Dom.md"_s});
        QCOMPARE(r.code, 0);
        const QJsonArray items = r.result().value(u"items"_s).toArray();
        QCOMPARE(items.size(), 3);
        QString weekly;
        for (const QJsonValue &v : items) {
            if (v.toObject().value(u"text"_s).toString().contains(u"Podlać"_s))
                weekly = v.toObject().value(u"ref"_s).toString();
        }
        QVERIFY(weekly.startsWith(u"Dom.md#L3@"_s));
        r = run(dir, {u"task"_s, u"done"_s, weekly});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"status"_s).toString(), u"done"_s);
        const QString dom = QString::fromUtf8(readFile(dir + u"/Dom.md"_s));
        QVERIFY2(dom.contains(u"- [ ] Podlać kwiaty 🔁 every week 📅 2026-10-21"_s), qPrintable(dom));
        QVERIFY2(dom.contains(u"- [x] Podlać kwiaty 🔁 every week 📅 2026-10-14 ✅ 2026-10-14\n"_s), qPrintable(dom));
        QCOMPARE(r.result().value(u"line"_s).toInt(), 3); // the completed line, below its next occurrence
        QVERIFY(r.result().contains(u"next"_s));
        // The same ref again: that line now holds other text → stale.
        r = run(dir, {u"task"_s, u"set"_s, weekly, u"zrobione"_s});
        QCOMPARE(r.code, 4);
        QCOMPARE(r.errorCode(), u"stale_ref"_s);
        // A Polish status word, a line ref without a hash, a bad status, a page without a line.
        r = run(dir, {u"task"_s, u"set"_s, u"Dom.md#L2"_s, u"w toku"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(readFile(dir + u"/Dom.md"_s).contains("- [/] Pomalować płot"));
        r = run(dir, {u"task"_s, u"set"_s, u"Dom.md#L2"_s, u"gotowe"_s});
        QCOMPARE(r.code, 2);
        r = run(dir, {u"task"_s, u"done"_s, u"Dom.md"_s});
        QCOMPARE(r.errorCode(), u"item_ref_required"_s);
        r = run(dir, {u"task"_s, u"done"_s, u"Dom.md#L99@0000aaaa"_s});
        QVERIFY(r.code == 4 || r.code == 3);
        // Queries in Polish; a broken one.
        r = run(dir, {u"query"_s, u"typ:zadania status:otwarte termin:<=+7d"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"type"_s).toString(), u"tasks"_s);
        QVERIFY(r.result().value(u"total"_s).toInt() >= 2);
        r = run(dir, {u"query"_s, u"status:jutrzejsze"_s});
        QCOMPARE(r.code, 6);
        QCOMPARE(r.errorCode(), u"query_error"_s);
    }

    // Database views (docs/SYNTAX.md §10 "Views", "Obsidian Bases"): a query with view: prints its
    // table, `base` runs a .base file and a page's ```base block.
    void viewsAndBases()
    {
        const QString dir = vault(
                {{u"Projekty/Dom.md"_s, "---\nstatus: w toku\nkoszt: 1200\ntags: [projekt]\n---\nDom.\n"},
                 {u"Projekty/Ogród.md"_s, "---\nstatus: planowany\nkoszt: 300\ntags: [projekt]\n---\nOgród.\n"},
                 {u"Projekty.base"_s, "filters: file.hasTag(\"projekt\")\nviews:\n  - type: table\n    name: Drogie\n"
                                      "    filters: 'koszt > 500'\n    order: [file.name, koszt]\n  - type: kanban\n"
                                      "    name: Stan\n    groupBy: {property: status, direction: ASC}\n"},
                 {u"Strona z bazą.md"_s, "Tekst.\n\n```base\nviews:\n  - type: list\n```\n"}});
        Run r = run(dir, {u"query"_s, u"type:pages tag:projekt widok:tabela kolumny:tytuł,status,koszt "
                                      u"sortuj:-koszt podsumowanie:koszt=suma"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"kind"_s).toString(), u"table"_s);
        const QJsonArray rows = r.result().value(u"rows"_s).toArray();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toObject().value(u"path"_s).toString(), u"Projekty/Dom.md"_s);
        QCOMPARE(r.result().value(u"columns"_s).toArray().at(2).toObject().value(u"summaryText"_s).toString(),
                 u"1500"_s);
        r = run(dir, {u"--format"_s, u"text"_s, u"query"_s, u"type:pages tag:projekt view:board group:status"_s});
        QCOMPARE(r.code, 0);
        const QString text = QString::fromUtf8(r.out);
        QVERIFY2(text.contains(u"## planowany (1)"_s) && text.contains(u"## w toku (1)"_s), qPrintable(text));
        // A base file: its first view, a view by name and by number; a page's block.
        r = run(dir, {u"base"_s, u"Projekty.base"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"rows"_s).toArray().size(), 1);
        QCOMPARE(r.result().value(u"views"_s).toArray().size(), 2);
        r = run(dir, {u"base"_s, u"Projekty.base"_s, u"--view"_s, u"stan"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"kind"_s).toString(), u"board"_s);
        QCOMPARE(r.result().value(u"groups"_s).toArray().size(), 2);
        r = run(dir, {u"base"_s, u"Projekty.base"_s, u"--view"_s, u"2"_s});
        QCOMPARE(r.result().value(u"view"_s).toInt(), 1);
        r = run(dir, {u"base"_s, u"Strona z bazą"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"kind"_s).toString(), u"list"_s);
        r = run(dir, {u"base"_s, u"Brak.base"_s});
        QCOMPARE(r.code, 3);
        r = run(dir, {u"query"_s, u"view:table columns:\"Zła = (\""_s});
        QCOMPARE(r.code, 6);
    }

    // Dependencies (⛔ / 🆔), the :LOGBOOK: clock (one at a time, local time) and time summaries.
    void taskDependenciesAndClock()
    {
        const QString dir = vault({{u"Budowa.md"_s, "- [ ] Wylać fundament\n- [ ] Postawić ściany 📅 2026-10-20\n"}});
        const auto at = [&](const QString &now, QStringList args) {
            return cli(QStringList{u"--vault"_s, dir, u"--now"_s, now} + args);
        };
        Run r = run(dir, {u"task"_s, u"depend"_s, u"Budowa.md#L2"_s, u"--on"_s, u"Budowa.md#L1"_s});
        QCOMPARE(r.code, 0);
        const QString id = r.result().value(u"id"_s).toString();
        QCOMPARE(id.size(), 6);
        QString budowa = QString::fromUtf8(readFile(dir + u"/Budowa.md"_s));
        QCOMPARE(budowa,
                 u"- [ ] Wylać fundament 🆔 "_s + id + u"\n- [ ] Postawić ściany ⛔ "_s + id + u" 📅 2026-10-20\n"_s);
        // Blocked: the flag, the JSON and the text form.
        r = run(dir, {u"task"_s, u"list"_s, u"--blocked"_s});
        QCOMPARE(r.code, 0);
        QJsonArray items = r.result().value(u"items"_s).toArray();
        QCOMPARE(items.size(), 1);
        QVERIFY(items[0].toObject().value(u"blocked"_s).toBool());
        QCOMPARE(items[0].toObject().value(u"blockedBy"_s).toArray().first().toObject().value(u"text"_s).toString(),
                 u"Wylać fundament"_s);
        r = run(dir, {u"--format"_s, u"text"_s, u"task"_s, u"list"_s});
        QVERIFY2(r.out.contains("[todo] \xE2\x9B\x94 Postawi"), r.out.constData());
        // Removing it again.
        r = run(dir, {u"task"_s, u"depend"_s, u"Budowa.md#L2"_s, u"--on"_s, u"Budowa.md#L1"_s, u"--remove"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(!readFile(dir + u"/Budowa.md"_s).contains("\xE2\x9B\x94"));

        // The clock: start one, start another (the first stops), stop.
        r = at(u"2026-10-14 10:00"_s, {u"task"_s, u"start"_s, u"Budowa.md#L1"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(readFile(dir + u"/Budowa.md"_s).contains("  :LOGBOOK:\n  CLOCK: [2026-10-14 Wed 10:00:00]\n  :END:\n"));
        r = at(u"2026-10-14 10:05"_s, {u"task"_s, u"start"_s, u"Budowa.md#L1"_s});
        QVERIFY(r.warnings().contains(u"already_running"_s));
        r = run(dir, {u"task"_s, u"list"_s, u"--query"_s, u"is:running"_s});
        QCOMPARE(r.result().value(u"total"_s).toInt(), 1);
        r = at(u"2026-10-14 10:25"_s, {u"task"_s, u"start"_s, u"Budowa.md#L5"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"stopped"_s).toArray().size(), 1);
        QCOMPARE(r.result().value(u"stopped"_s).toArray().first().toObject().value(u"seconds"_s).toInt(), 25 * 60);
        budowa = QString::fromUtf8(readFile(dir + u"/Budowa.md"_s));
        QVERIFY2(budowa.contains(u"CLOCK: [2026-10-14 Wed 10:00:00]--[2026-10-14 Wed 10:25:00] =>  00:25:00"_s),
                 qPrintable(budowa));
        QVERIFY2(budowa.contains(
                         u"- [ ] Postawić ściany 📅 2026-10-20\n  :LOGBOOK:\n  CLOCK: [2026-10-14 Wed 10:25:00]\n"_s),
                 qPrintable(budowa));
        r = at(u"2026-10-14 11:00"_s, {u"task"_s, u"stop"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"stopped"_s).toArray().first().toObject().value(u"seconds"_s).toInt(), 35 * 60);
        r = at(u"2026-10-14 11:05"_s, {u"task"_s, u"stop"_s, u"Budowa.md#L1"_s});
        QVERIFY(r.warnings().contains(u"not_running"_s));
        r = run(dir, {u"task"_s, u"start"_s, u"Budowa.md"_s});
        QCOMPARE(r.errorCode(), u"item_ref_required"_s);

        // Summaries.
        r = run(dir, {u"task"_s, u"time"_s, u"--by"_s, u"task"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"total"_s).toInt(), 60 * 60);
        const QJsonArray rows = r.result().value(u"rows"_s).toArray();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].toObject().value(u"label"_s).toString(), u"Postawić ściany"_s);
        QCOMPARE(rows[0].toObject().value(u"duration"_s).toString(), u"00:35:00"_s);
        r = run(dir, {u"task"_s, u"time"_s, u"--from"_s, u"jutro"_s});
        QCOMPARE(r.result().value(u"total"_s).toInt(), 0);
        r = run(dir, {u"task"_s, u"time"_s, u"--by"_s, u"miesiąc"_s});
        QCOMPARE(r.errorCode(), u"bad_argument"_s);

        // The status history: doing starts the clock, done stops it.
        r = at(u"2026-10-14 12:00"_s, {u"task"_s, u"set"_s, u"Budowa.md#L1"_s, u"doing"_s, u"--history"_s});
        QCOMPARE(r.code, 0);
        r = at(u"2026-10-14 12:30"_s, {u"task"_s, u"set"_s, u"Budowa.md#L1"_s, u"done"_s, u"--history"_s});
        QCOMPARE(r.code, 0);
        budowa = QString::fromUtf8(readFile(dir + u"/Budowa.md"_s));
        QVERIFY2(budowa.contains(u"  CLOCK: [2026-10-14 Wed 12:00:00]--[2026-10-14 Wed 12:30:00] =>  00:30:00\n"
                                 "  State \"DOING\"      from \"TODO\"       [2026-10-14 Wed 12:00]\n"
                                 "  State \"DONE\"       from \"DOING\"      [2026-10-14 Wed 12:30]\n  :END:\n"_s),
                 qPrintable(budowa));
    }

    void reminders()
    {
        const QString dir = vault();
        Run r = run(dir, {u"remind"_s, u"add"_s, u"Leki"_s, u"--at"_s, u"dziś o 21"_s, u"--every"_s, u"codziennie"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"written"_s).toString(), u"- Leki ⏰ 2026-10-14 21:00 🔁 every day"_s);
        r = run(dir, {u"remind"_s, u"add"_s, u"Pobudka"_s, u"--at"_s, u"jutro 6:30"_s, u"--alarm"_s, u"--task"_s});
        QCOMPARE(r.result().value(u"written"_s).toString(), u"- [ ] Pobudka ⏰! 2026-10-15 06:30"_s);
        const QString pobudka = r.result().value(u"ref"_s).toString();
        r = run(dir, {u"remind"_s, u"list"_s, u"--upcoming"_s, u"2"_s});
        QCOMPARE(r.code, 0);
        const QJsonArray items = r.result().value(u"items"_s).toArray();
        QVERIFY(items.size() >= 3); // today 21:00, tomorrow 06:30, tomorrow 21:00
        QCOMPARE(items.first().toObject().value(u"at"_s).toString(), u"2026-10-14 21:00"_s);
        r = run(dir, {u"remind"_s, u"snooze"_s, pobudka, u"--minutes"_s, u"30"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"at"_s).toString(), u"2026-10-14 11:00"_s);
        QVERIFY(readFile(dir + u"/journals/2026-10-14.md"_s).contains("- [ ] Pobudka ⏰! 2026-10-14 11:00"));
        // remind list keeps no state: neither in the notebook's device state nor anywhere.
        const QString store = stateDirOf(dir) + u"/store.json"_s;
        QVERIFY(!readFile(store).contains("reminders/"));
    }

    void pagesAndSafety()
    {
        const QString dir = vault({{u"Projekty/Dom.md"_s, "# Dom\n## Zakupy\n- farba\n## Plan\n- [ ] malowanie\n"},
                                   {u"Notatka.md"_s, "- [[Dom]] i #dom\n"},
                                   {u".brulion/config.json"_s, "{}"}});
        Run r = run(dir, {u"page"_s, u"read"_s, u"Dom"_s, u"--blocks"_s, u"--backlinks"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Projekty/Dom.md"_s);
        const QString hash = r.result().value(u"hash"_s).toString();
        QCOMPARE(hash.size(), 16);
        QCOMPARE(r.result().value(u"backlinks"_s).toArray().size(), 2); // the link and the tag
        QVERIFY(r.result().value(u"blocks"_s).toArray().size() >= 4);
        // Append under a heading; the dry run first changes nothing.
        const QByteArray before = readFile(dir + u"/Projekty/Dom.md"_s);
        r = run(dir, {u"--dry-run"_s, u"page"_s, u"append"_s, u"Dom"_s, u"pędzel"_s, u"--under"_s, u"## Zakupy"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"dryRun"_s).toBool());
        QVERIFY(r.result().value(u"diff"_s).toString().contains(u"+- pędzel"_s));
        QCOMPARE(readFile(dir + u"/Projekty/Dom.md"_s), before);
        r = run(dir, {u"page"_s, u"append"_s, u"Dom"_s, u"pędzel"_s, u"--under"_s, u"## Zakupy"_s});
        QCOMPARE(r.code, 0);
        // (BlockDocument separates the list from the heading after it, as the editor does)
        QCOMPARE(readFile(dir + u"/Projekty/Dom.md"_s),
                 "# Dom\n## Zakupy\n- farba\n- pędzel\n\n## Plan\n- [ ] malowanie\n"_ba);
        r = run(dir, {u"page"_s, u"append"_s, u"Dom"_s, u"x"_s, u"--under"_s, u"## Brak"_s});
        QCOMPARE(r.code, 3);
        // Destructive commands need --allow-destructive; page write needs the current hash.
        r = run(dir, {u"page"_s, u"write"_s, u"Dom"_s, u"--stdin"_s, u"--expect-hash"_s, hash}, "nowe\n");
        QCOMPARE(r.code, 7);
        QCOMPARE(r.errorCode(), u"destructive_refused"_s);
        r = run(dir,
                {u"--allow-destructive"_s, u"page"_s, u"write"_s, u"Dom"_s, u"--stdin"_s, u"--expect-hash"_s, hash},
                "nowe\n");
        QCOMPARE(r.code, 4); // the page changed since (the append)
        QCOMPARE(r.errorCode(), u"hash_mismatch"_s);
        r = run(dir, {u"--allow-destructive"_s, u"page"_s, u"write"_s, u"Dom"_s, u"--stdin"_s}, "nowe\n");
        QCOMPARE(r.code, 2); // --expect-hash is required
        const QString current = run(dir, {u"page"_s, u"read"_s, u"Dom"_s}).result().value(u"hash"_s).toString();
        r = run(dir,
                {u"--allow-destructive"_s, u"page"_s, u"write"_s, u"Dom"_s, u"--stdin"_s, u"--expect-hash"_s, current},
                "# Dom\r\n- nowe\r\n");
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/Projekty/Dom.md"_s), "# Dom\r\n- nowe\r\n"_ba);
        // Rename and move keep links and tags.
        r = run(dir, {u"--allow-destructive"_s, u"page"_s, u"rename"_s, u"Dom"_s, u"Mieszkanie"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Projekty/Mieszkanie.md"_s);
        QCOMPARE(readFile(dir + u"/Notatka.md"_s), "- [[Mieszkanie]] i #Mieszkanie\n"_ba);
        QCOMPARE(r.result().value(u"queries"_s).toInt(), 0);
        // A query naming the page follows it too (counted as links and tags).
        writeFile(dir + u"/Zapytanie.md"_s, "```query\ntag:mieszkanie ref:Mieszkanie path:Projekty/\n```\n");
        r = run(dir, {u"--allow-destructive"_s, u"page"_s, u"rename"_s, u"Mieszkanie"_s, u"Mieszkanie w bloku"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"queries"_s).toInt(), 2);
        QCOMPARE(readFile(dir + u"/Zapytanie.md"_s),
                 "```query\ntag:[[Mieszkanie w bloku]] ref:[[Mieszkanie w bloku]] path:Projekty/\n```\n"_ba);
        r = run(dir, {u"--allow-destructive"_s, u"page"_s, u"rename"_s, u"Mieszkanie w bloku"_s, u"Mieszkanie"_s});
        QCOMPARE(r.code, 0);
        r = run(dir, {u"--allow-destructive"_s, u"page"_s, u"move"_s, u"Mieszkanie"_s, u"Archiwum"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(QFile::exists(dir + u"/Archiwum/Mieszkanie.md"_s));
        r = run(dir, {u"--allow-destructive"_s, u"page"_s, u"move"_s, u"Notatka"_s, u"Archiwum/Mieszkanie.md"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"not_a_folder"_s);
        // Outside the notebook, its settings and its trash are off limits.
        r = run(dir, {u"page"_s, u"read"_s, u"../poza.md"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"outside_vault"_s);
        r = run(dir, {u"page"_s, u"read"_s, u".brulion/config.json"_s});
        QCOMPARE(r.code, 2);
        r = run(dir, {u"page"_s, u"append"_s, u"/etc/passwd"_s, u"x"_s});
        QCOMPARE(r.code, 2);
        // ...under every name storage has for the hidden folders (paths::isIgnored).
        for (const QString &path : {u".Brulion/x.md"_s, u".GIT/hooks/x.md"_s, u".git./x.md"_s, u"GIT~1/x.md"_s,
                                    u".Obsidian/plugins/x.md"_s, u"OBSIDI~1/x.md"_s}) {
            r = run(dir, {u"page"_s, u"read"_s, path});
            QVERIFY2(r.code == 2, qPrintable(path + u": "_s + r.errorCode()));
        }
        // A page is not created where the notebook would not show it (a hidden name typed loses
        // its dot, as in the app: ".git/x" is the page "git/x").
        r = run(dir, {u"page"_s, u"create"_s, u"GIT~1/x"_s, u"--content"_s, u"x"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"hidden_name"_s);
        QVERIFY(!QFile::exists(dir + u"/GIT~1"_s));
        // Create (a case twin is that page), delete to the trash.
        r = run(dir, {u"page"_s, u"create"_s, u"Lista zakupów"_s, u"--content"_s, u"- chleb\n"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/Lista zakupów.md"_s), "- chleb\n"_ba);
        r = run(dir, {u"page"_s, u"create"_s, u"lista ZAKUPÓW"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"existing"_s).toBool());
        r = run(dir, {u"page"_s, u"delete"_s, u"Lista zakupów"_s});
        QCOMPARE(r.code, 7);
        r = run(dir, {u"--allow-destructive"_s, u"page"_s, u"delete"_s, u"Lista zakupów"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"trashPath"_s).toString().startsWith(u".trash/"_s));
        QVERIFY(!QFile::exists(dir + u"/Lista zakupów.md"_s));
        // Read-only refuses every write.
        r = run(dir, {u"--read-only"_s, u"capture"_s, u"x"_s});
        QCOMPARE(r.code, 7);
        QCOMPARE(r.errorCode(), u"read_only"_s);
        // Ambiguous names.
        writeFile(dir + u"/A/Plan.md"_s, "a");
        writeFile(dir + u"/B/Plan.md"_s, "b");
        r = run(dir, {u"page"_s, u"read"_s, u"Plan"_s});
        QCOMPARE(r.code, 4);
        QCOMPARE(r.errorCode(), u"ambiguous_ref"_s);
        QCOMPARE(r.json.value(u"error"_s)
                         .toObject()
                         .value(u"details"_s)
                         .toObject()
                         .value(u"candidates"_s)
                         .toArray()
                         .size(),
                 2);
        r = run(dir, {u"page"_s, u"read"_s, u"Nie ma takiej"_s});
        QCOMPARE(r.code, 3);
    }

    void foldersAndSettings()
    {
        const QString dir = vault({{u"journals/2026-10-13.md"_s, "- [[Dom]]\n"}, {u"Dom.md"_s, "x"}});
        Run r = run(dir, {u"folder"_s, u"create"_s, u"Archiwum/2026"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(QFileInfo(dir + u"/Archiwum/2026"_s).isDir());
        r = run(dir, {u"folder"_s, u"create"_s, u".ukryty"_s});
        QCOMPARE(r.code, 2);
        r = run(dir, {u"--allow-destructive"_s, u"folder"_s, u"rename"_s, u"journals"_s, u"Dziennik"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"rebased"_s).toArray().first().toObject().value(u"value"_s).toString(),
                 u"Dziennik"_s);
        QVERIFY(readFile(dir + u"/.brulion/config.json"_s).contains("\"journalsFolder\": \"Dziennik\""));
        r = run(dir, {u"journal"_s, u"path"_s, u"wczoraj"_s});
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Dziennik/2026-10-13.md"_s);
        r = run(dir, {u"folder"_s, u"list"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"folders"_s).toArray().size() >= 3);
        r = run(dir, {u"--allow-destructive"_s, u"folder"_s, u"delete"_s, u"Archiwum"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(!QFileInfo::exists(dir + u"/Archiwum"_s));
    }

    // search --attachments (docs/CLI.md): the text of PDFs and pictures the app read on this device
    // — its cache's manifest of the notebook and the records —, searched read-only: a PDF's page with
    // an excerpt, the filters (in:pdf alone, in:notes without them), a file changed since it was read
    // left out.
    void searchAttachments()
    {
        using namespace brulion::attachments;
        const QString dir = vault({{u"Notatka.md"_s, "- żółw w notatce\n"},
                                   {u"assets/Wykład 3.pdf"_s, "%PDF-1.4 (bytes the app read)"},
                                   {u"assets/Stary.pdf"_s, "%PDF-1.4 (changed since)"}});
        const QString cache = cacheDir() + u"/attachment-text"_s;
        QVERIFY(QDir().mkpath(cache + u"/notebooks"_s));
        Record lecture;
        lecture.pageCount = 3;
        lecture.pages = {Page{PageSource::TextLayer, u"Wstęp do kursu"_s, {}, {}},
                         Page{PageSource::NeedsOcr, {}, {}, {}},
                         Page{PageSource::TextLayer, u"Strona trzecia: żółw idzie powoli"_s, {}, {}}};
        Manifest manifest;
        const auto stamp = [&dir](const QString &path) {
            const QFileInfo fi(dir + u'/' + path);
            return std::pair(
                    fi.size(),
                    fi.fileTime(QFileDevice::FileModificationTime, QTimeZone(QTimeZone::UTC)).toMSecsSinceEpoch());
        };
        for (const QString &path : {u"assets/Wykład 3.pdf"_s, u"assets/Stary.pdf"_s}) {
            const QByteArray hash = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256);
            const auto [size, mtime] = stamp(path);
            // (Stary.pdf: read when it was one byte shorter.)
            manifest.insert(path, ManifestEntry{path.contains(u"Stary"_s) ? size - 1 : size, mtime, hash});
            QFile record(cache + u'/' + recordFileName(hash));
            QVERIFY(record.open(QIODevice::WriteOnly));
            record.write(encodeRecord(lecture));
        }
        QFile manifestFile(cache + u'/' + manifestFileName(brulion::Vault::idForPath(dir)));
        QVERIFY(manifestFile.open(QIODevice::WriteOnly));
        manifestFile.write(encodeManifest(manifest));
        manifestFile.close();

        Run r = run(dir, {u"search"_s, u"zolw"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"hits"_s).toArray().size(), 1);
        QVERIFY(!r.result().contains(u"attachments"_s));
        r = run(dir, {u"search"_s, u"--attachments"_s, u"zolw"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"hits"_s).toArray().size(), 1);
        const QJsonArray found = r.result().value(u"attachments"_s).toArray();
        QCOMPARE(found.size(), 1); // (not Stary.pdf: changed since)
        const QJsonObject hit = found.first().toObject();
        QCOMPARE(hit.value(u"path"_s).toString(), u"assets/Wykład 3.pdf"_s);
        QCOMPARE(hit.value(u"kind"_s).toString(), u"pdf"_s);
        QCOMPARE(hit.value(u"page"_s).toInt(), 3);
        QCOMPARE(hit.value(u"pageCount"_s).toInt(), 3);
        QVERIFY(hit.value(u"excerpt"_s).toString().contains(u"żółw idzie"_s));
        QCOMPARE(hit.value(u"find"_s).toString(), u"żółw"_s);
        // Filters: in:pdf searches the attachments alone; in:notes never them.
        r = run(dir, {u"search"_s, u"in:pdf"_s, u"zolw"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"hits"_s).toArray().isEmpty());
        QCOMPARE(r.result().value(u"attachments"_s).toArray().size(), 1);
        r = run(dir, {u"search"_s, u"--attachments"_s, u"w:notatki zolw"_s});
        QCOMPARE(r.result().value(u"hits"_s).toArray().size(), 1);
        QVERIFY(!r.result().contains(u"attachments"_s));
        r = run(dir, {u"search"_s, u"in:pdf"_s});
        QVERIFY(r.code != 0);
    }

    // search --semantic (docs/CLI.md): without what it needs — the plugin, the model, the app's
    // vectors of the notebook, a model that opens — the words' hits as they are, with a warning
    // saying which; never a crash. (A real model: tst_semantic_plugin; the fused results:
    // tst_content_semantic and tst_semantic.)
    void searchSemantic()
    {
        const QString dir = vault({{u"Szarlotka babci.md"_s, "- jabłka i kruszonka\n"},
                                   {u"Notatka.md"_s, "- żółw w notatce\n- żółw je sałatę\n"}});
        const Run plain = run(dir, {u"search"_s, u"zolw"_s});
        QCOMPARE(plain.code, 0);
        QString reasonGiven;
        const auto expect = [&](const QString &reason) {
            const Run r = run(dir, {u"search"_s, u"--semantic"_s, u"zolw"_s});
            QCOMPARE(r.code, 0);
            QCOMPARE(r.result().value(u"hits"_s).toArray().size(), plain.result().value(u"hits"_s).toArray().size());
            QCOMPARE(r.result().value(u"hits"_s).toArray().first().toObject().value(u"path"_s),
                     plain.result().value(u"hits"_s).toArray().first().toObject().value(u"path"_s));
            const QJsonObject semantic = r.result().value(u"semantic"_s).toObject();
            QCOMPARE(semantic.value(u"used"_s).toBool(true), false);
            if (!reason.isEmpty())
                QCOMPARE(semantic.value(u"reason"_s).toString(), reason);
            QVERIFY(r.warnings().contains(semantic.value(u"reason"_s).toString()));
            reasonGiven = semantic.value(u"reason"_s).toString();
        };
        expect(QString());
        const QString first = reasonGiven;
        QVERIFY2(first == u"semantic_unavailable"_s || first == u"semantic_no_model"_s, qPrintable(first));
        if (first == u"semantic_unavailable"_s)
            return; // (a build without the plugin: nothing more to find)
        // The model's file (in size and marked downloaded; its bytes matter only once it is opened),
        // the app's vectors of the notebook missing.
        const brulion::ai::LocalModel *model = brulion::semantic::semanticModel(QString());
        const QString models = appDataDir() + u"/models/semantic"_s;
        QVERIFY(QDir().mkpath(models));
        QFile file(models + u'/' + model->file.name);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(model->file.bytes)); // (sparse)
        file.close();
        QFile marker(models + u'/' + model->id + u".downloaded"_s);
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.close();
        expect(u"semantic_not_indexed"_s);
        // Vectors as the app keeps them, but a model file that is no model: it fails to open.
        brulion::semantic::VectorIndex vectors;
        brulion::semantic::Vector v(384, 0.0f);
        v[0] = 1;
        vectors.set(u"Notatka.md"_s, {brulion::semantic::Chunk{u"Notatka\nżółw"_s, 0}}, {v}, 1);
        const QString kept = cacheDir() + u"/semantic/"_s
                             + brulion::semantic::indexFileName(brulion::Vault::idForPath(dir), model->id);
        QVERIFY(QDir().mkpath(QFileInfo(kept).absolutePath()));
        QFile f(kept);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(vectors.save(model->id));
        f.close();
        expect(u"semantic_failed"_s);
        QFile::remove(file.fileName());
        QFile::remove(kept);
    }

    void indexCommands()
    {
        const QString dir = vault({{u"Zwierzęta/Żółw.md"_s, "- żółw je sałatę #zwierzęta\n- Kot też\n"},
                                   {u"Notatka.md"_s, "- [[Żółw]] i kot\n- zadanie ⏰ 2026-10-15 09:00\n"},
                                   {u"Budżet.md"_s, "```calc\ncena = 12,5 zł\ncena * 2\n```\n"}});
        Run r = run(dir, {u"search"_s, u"zolw"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"hits"_s).toArray().first().toObject().value(u"path"_s).toString(),
                 u"Zwierzęta/Żółw.md"_s);
        r = run(dir, {u"tags"_s});
        QCOMPARE(r.result().value(u"tags"_s).toArray().first().toObject().value(u"tag"_s).toString(), u"zwierzęta"_s);
        r = run(dir, {u"tag"_s, u"#zwierzęta"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"total"_s).toInt(), 1);
        r = run(dir, {u"backlinks"_s, u"Żółw"_s});
        QCOMPARE(r.result().value(u"items"_s).toArray().size(), 1);
        r = run(dir, {u"links"_s, u"Notatka"_s});
        QCOMPARE(r.result().value(u"links"_s).toArray().first().toObject().value(u"resolved"_s).toString(),
                 u"Zwierzęta/Żółw.md"_s);
        r = run(dir, {u"unlinked"_s, u"Notatka"_s});
        QCOMPARE(r.code, 0);
        r = run(dir, {u"calendar"_s, u"--from"_s, u"dziś"_s, u"--to"_s, u"jutro"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"items"_s).toArray().first().toObject().value(u"kind"_s).toString(), u"reminder"_s);
        r = run(dir, {u"calc"_s, u"page"_s, u"Budżet"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result()
                         .value(u"blocks"_s)
                         .toArray()
                         .first()
                         .toObject()
                         .value(u"lines"_s)
                         .toArray()
                         .last()
                         .toObject()
                         .value(u"value"_s)
                         .toString(),
                 u"25 zł"_s);
        r = run(dir, {u"calc"_s, u"2 + 2 * 3"_s});
        QCOMPARE(r.result().value(u"value"_s).toString(), u"8"_s);
        r = run(dir, {u"calc"_s, u"2 +"_s});
        QCOMPARE(r.code, 6);
        r = run(dir, {u"date"_s, u"parse"_s, u"w piątek o 18"_s});
        QCOMPARE(r.result().value(u"spec"_s).toString(), u"2026-10-16 18:00"_s);
        r = run(dir, {u"date"_s, u"parse"_s, u"nic"_s});
        QCOMPARE(r.code, 6);
        r = run(dir, {u"info"_s});
        QCOMPARE(r.result().value(u"counts"_s).toObject().value(u"tags"_s).toInt(), 1);
        QVERIFY(!r.result().value(u"gui"_s).toObject().value(u"running"_s).toBool());
    }

    void blockLinkAndHistory()
    {
        const QString dir = vault({{u"Dom.md"_s, "# Dom\n- farba\n"}});
        Run r = run(dir, {u"page"_s, u"read"_s, u"Dom"_s, u"--blocks"_s});
        QString farba;
        for (const QJsonValue &b : r.result().value(u"blocks"_s).toArray()) {
            if (b.toObject().value(u"text"_s).toString() == u"farba")
                farba = b.toObject().value(u"ref"_s).toString();
        }
        QVERIFY(!farba.isEmpty());
        r = run(dir, {u"block"_s, u"link"_s, farba});
        QCOMPARE(r.code, 0);
        const QString id = r.result().value(u"id"_s).toString();
        QCOMPARE(id.size(), 6);
        QCOMPARE(r.result().value(u"ref"_s).toString(), u"((%1))"_s.arg(id));
        QCOMPARE(r.result().value(u"wikiLink"_s).toString(), u"[[Dom#^%1]]"_s.arg(id));
        QCOMPARE(readFile(dir + u"/Dom.md"_s), "# Dom\n- farba ^" + id.toUtf8() + "\n");
        // The same block again: the same id, nothing written.
        r = run(dir, {u"block"_s, u"link"_s, u"Dom.md#^"_s + id});
        QCOMPARE(r.result().value(u"id"_s).toString(), id);
        QVERIFY(!r.result().value(u"changed"_s).toBool());
        // A heading: also its link.
        r = run(dir, {u"block"_s, u"link"_s, u"Dom.md#L1"_s});
        QCOMPARE(r.result().value(u"headingLink"_s).toString(), u"[[Dom#Dom]]"_s);
        // History: every write of the command line is a version with the reason "cli".
        r = run(dir, {u"history"_s, u"list"_s, u"Dom"_s});
        QCOMPARE(r.code, 0);
        const QJsonArray versions = r.result().value(u"versions"_s).toArray();
        QVERIFY(versions.size() >= 1);
        QCOMPARE(versions.first().toObject().value(u"reason"_s).toString(), u"cli"_s);
        const QString oldest = versions.last().toObject().value(u"id"_s).toString();
        r = run(dir, {u"history"_s, u"show"_s, oldest});
        QCOMPARE(r.result().value(u"content"_s).toString(), u"# Dom\n- farba\n"_s);
        r = run(dir, {u"history"_s, u"restore"_s, u"Dom"_s, oldest});
        QCOMPARE(r.code, 7);
        r = run(dir, {u"--allow-destructive"_s, u"history"_s, u"restore"_s, u"Dom"_s, oldest});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/Dom.md"_s), "# Dom\n- farba\n"_ba);
        r = run(dir, {u"history"_s, u"show"_s, u"nie-ma"_s});
        QCOMPARE(r.code, 3);
    }

    // A page whose title another page has too: the link names it by the shortest folder path that
    // tells them apart (notebook::pageLinkTarget, the app's "Copy link" and page menu alike).
    void blockLinkSameNames()
    {
        const QString dir = vault({{u"Dom.md"_s, "- główny\n"},
                                   {u"Archiwum/Projekty/Dom.md"_s, "## Zażółć gęślą jaźń\n- farba\n"},
                                   {u"journals/2026-10-14.md"_s, "- dziś\n"},
                                   {u"Stare/2026-10-14.md"_s, "- kopia\n"}});
        Run r = run(dir, {u"block"_s, u"link"_s, u"Archiwum/Projekty/Dom.md#L2"_s}); // "- farba"
        QCOMPARE(r.code, 0);
        const QString id = r.result().value(u"id"_s).toString();
        QCOMPARE(r.result().value(u"wikiLink"_s).toString(), u"[[Projekty/Dom#^%1]]"_s.arg(id));
        r = run(dir, {u"block"_s, u"link"_s, u"Archiwum/Projekty/Dom.md#L1"_s}); // the heading
        QCOMPARE(r.result().value(u"headingLink"_s).toString(), u"[[Projekty/Dom#Zażółć gęślą jaźń]]"_s);
        QCOMPARE(r.result().value(u"embed"_s).toString(), u"![[Projekty/Dom#Zażółć gęślą jaźń]]"_s);
        // A daily note keeps its file name (it reads as the date).
        r = run(dir, {u"block"_s, u"link"_s, u"journals/2026-10-14.md#L1"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"wikiLink"_s).toString(),
                 u"[[2026-10-14#^%1]]"_s.arg(r.result().value(u"id"_s).toString()));
    }

    // The other commands' JSON shapes and exit codes.
    void moreCommands()
    {
        const QString dir = vault({{u"Projekty/Dom.md"_s, "- [ ] Malowanie #dom\n"},
                                   {u"Projekty/Ogród.md"_s, "- trawa\n"},
                                   {u"journals/2026-10-13.md"_s, "- wczoraj [[Dom]]\n"},
                                   {u"Archiwum/.keep"_s, ""}});
        Run r = run(dir, {u"page"_s, u"list"_s, u"--folder"_s, u"Projekty"_s, u"--sort"_s, u"title"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"total"_s).toInt(), 2);
        QCOMPARE(r.result().value(u"pages"_s).toArray().first().toObject().value(u"path"_s).toString(),
                 u"Projekty/Dom.md"_s);
        r = run(dir, {u"page"_s, u"list"_s});
        QCOMPARE(r.result().value(u"pages"_s).toArray().size(), 3);
        r = run(dir, {u"page"_s, u"list"_s, u"--folder"_s, u"Brak"_s});
        QCOMPARE(r.code, 3);
        r = run(dir, {u"journal"_s, u"read"_s, u"wczoraj"_s, u"--blocks"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"blocks"_s).toArray().first().toObject().value(u"text"_s).toString(),
                 u"wczoraj [[Dom]]"_s);
        r = run(dir, {u"remind"_s, u"add"_s, u"Spotkanie"_s, u"--at"_s, u"9:00"_s, u"--date"_s, u"jutro"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-10-15.md"_s);
        r = run(dir, {u"--allow-destructive"_s, u"folder"_s, u"move"_s, u"Projekty"_s, u"Archiwum"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Archiwum/Projekty"_s);
        QCOMPARE(readFile(dir + u"/journals/2026-10-13.md"_s), "- wczoraj [[Dom]]\n"_ba); // still unique
        r = run(dir, {u"--allow-destructive"_s, u"folder"_s, u"move"_s, u"Archiwum"_s, u"Archiwum/Projekty"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"into_itself"_s);
        r = run(dir, {u"--allow-destructive"_s, u"folder"_s, u"move"_s, u"Nie ma"_s, u"."_s});
        QCOMPARE(r.code, 3);
        r = run(dir, {u"calc"_s, u"--stdin"_s}, "a = 2\nb = 3\na * b\n");
        QCOMPARE(r.result().value(u"value"_s).toString(), u"6"_s);
        r = run(dir, {u"--diff"_s, u"journal"_s, u"append"_s, u"z diffem"_s});
        QVERIFY(r.result().value(u"diff"_s).toString().contains(u"+- z diffem"_s));
        r = run(dir, {u"--format"_s, u"text"_s, u"task"_s, u"list"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.startsWith("[todo] Malowanie #dom  Archiwum/Projekty/Dom.md#L1@"));
        r = run(dir, {u"search"_s, u"trawa"_s, u"--limit"_s, u"1"_s});
        QCOMPARE(r.result().value(u"hits"_s).toArray().size(), 1);
        r = run(dir, {u"search"_s, u"x"_s, u"--limit"_s, u"dużo"_s});
        QCOMPARE(r.code, 2);
        // Under a block: its last child, indented like its children.
        writeFile(dir + u"/Lista.md"_s, "- rodzic ^p1\n  - dziecko\n- inny\n");
        r = run(dir, {u"page"_s, u"append"_s, u"Lista"_s, u"nowe"_s, u"--under"_s, u"^p1"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/Lista.md"_s), "- rodzic ^p1\n  - dziecko\n  - nowe\n- inny\n"_ba);
        QVERIFY(r.result().value(u"ref"_s).toString().startsWith(u"Lista.md#L3@"_s));
    }

    // A page whose name reads as a date ("Poniedziałek", "Weekend", "Jutro") is that page, as
    // [[Poniedziałek]] opens it in the app; `today` and ISO dates stay the daily notes, and a date
    // phrase no page is called is the daily note too.
    void pageNamesWinOverDatePhrases()
    {
        const QString dir = vault({{u"Plan/Poniedziałek.md"_s, "- bieganie\n"},
                                   {u"Projekty/Weekend.md"_s, "- działka\n"},
                                   {u"Notatka.md"_s, "- [[Poniedziałek]] i [[Weekend]]\n"}});
        Run r = run(dir, {u"page"_s, u"read"_s, u"Poniedziałek"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Plan/Poniedziałek.md"_s);
        QVERIFY(r.warnings().contains(u"ref_also_a_date"_s));
        r = run(dir, {u"page"_s, u"append"_s, u"Poniedziałek"_s, u"rozciąganie"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Plan/Poniedziałek.md"_s);
        QCOMPARE(readFile(dir + u"/Plan/Poniedziałek.md"_s), "- bieganie\n- rozciąganie\n"_ba);
        r = run(dir, {u"task"_s, u"add"_s, u"Pompki"_s, u"--page"_s, u"poniedziałek"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Plan/Poniedziałek.md"_s);
        r = run(dir, {u"backlinks"_s, u"Weekend"_s});
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Projekty/Weekend.md"_s);
        QCOMPARE(r.result().value(u"items"_s).toArray().size(), 1);
        QVERIFY(!QFileInfo::exists(dir + u"/journals"_s)); // no daily note written by mistake
        // The daily notes: `today`, a date, a phrase no page has.
        r = run(dir, {u"page"_s, u"append"_s, u"today"_s, u"dziś"_s});
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-10-14.md"_s);
        r = run(dir, {u"page"_s, u"read"_s, u"2026-10-19"_s});
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-10-19.md"_s);
        r = run(dir, {u"page"_s, u"read"_s, u"wtorek"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-10-20.md"_s);
        QVERIFY(!r.warnings().contains(u"ref_also_a_date"_s));
        // Two pages of that name: ambiguous, never the daily note.
        writeFile(dir + u"/Archiwum/Weekend.md"_s, "- stary\n");
        r = run(dir, {u"page"_s, u"read"_s, u"Weekend"_s});
        QCOMPARE(r.code, 4);
        QCOMPARE(r.errorCode(), u"ambiguous_ref"_s);
    }

    // Without --vault: the app's notebook, also when it was quit in its documentation, and the
    // app's language also when it was never chosen.
    void notebookAndLanguageAsTheApp()
    {
        const QString dir = vault({{u"Dom.md"_s, "- dom\n"}});
        const QString ini = settingsFile();
        const QString docs = appDataDir() + u"/Dokumentacja Brulionu"_s;
        QDir().mkpath(docs);
        {
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue(u"vaults/current"_s, docs);
            settings.setValue(u"vaults/recent"_s,
                              QVariantList{QVariantMap{{u"path"_s, docs}}, QVariantMap{{u"path"_s, dir}}});
        }
        Run r = cli({u"info"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.json.value(u"vault"_s).toString(), dir);
        r = cli({u"capture"_s, u"Leki jutro o 9"_s, u"--now"_s, kNow});
        QCOMPARE(r.code, 0);
        QVERIFY(QFileInfo::exists(dir + u"/journals/2026-10-14.md"_s));
        QVERIFY(!QFileInfo::exists(docs + u"/journals"_s));
        // No notebook of the user's known: refused, the documentation is never written.
        {
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue(u"vaults/recent"_s, QVariantList{QVariantMap{{u"path"_s, docs}}});
        }
        r = cli({u"capture"_s, u"x"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"no_vault"_s);
        QVERIFY(!QFileInfo::exists(docs + u"/journals"_s));
        // ui/language never chosen: English, as the app — also on a Polish system.
        {
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue(u"vaults/current"_s, dir);
        }
        QProcessEnvironment env = environment();
        for (const QString &v : {u"LC_ALL"_s, u"LC_NUMERIC"_s, u"LC_MESSAGES"_s, u"LANGUAGE"_s})
            env.remove(v);
        env.insert(u"LANG"_s, u"en_US.UTF-8"_s);
        r = cli({u"info"_s}, {}, env);
        QCOMPARE(r.result().value(u"language"_s).toString(), u"en"_s);
        r = cli({u"date"_s, u"parse"_s, u"jutro o 9"_s, u"--now"_s, kNow}, {}, env);
        QCOMPARE(r.result().value(u"human"_s).toString(), u"tomorrow 09:00"_s);
        env.insert(u"LANG"_s, u"pl_PL.UTF-8"_s);
        r = cli({u"info"_s}, {}, env);
        QCOMPARE(r.result().value(u"language"_s).toString(), u"en"_s);
        // Chosen in the app: its language.
        {
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue(u"ui/language"_s, u"pl"_s);
        }
        r = cli({u"info"_s}, {}, env);
        QCOMPARE(r.result().value(u"language"_s).toString(), u"pl"_s);
        QFile::remove(ini);
        QDir(docs).removeRecursively();
    }

    // Folder links to outside the notebook: followed as the app on this device follows them
    // (AppSettings' followsOutsideLinks, read only), so a command sees and writes the same files.
    void outsideFolderLinksAsTheApp()
    {
#ifndef Q_OS_UNIX
        QSKIP("symbolic links");
#else
        const QString dir = vault({{u"Dom.md"_s, "- dom\n"}});
        const QString outside = m_tmp.filePath(u"poza notatnikiem"_s);
        writeFile(outside + u"/Plan.md"_s, "- plan zażółć\n");
        QVERIFY(QFile::link(outside, dir + u"/Dokumenty"_s));
        Run r = run(dir, {u"page"_s, u"list"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"total"_s).toInt(), 1); // the default: not followed
        r = run(dir, {u"page"_s, u"append"_s, u"Dokumenty/Plan.md"_s, u"- dopisane"_s});
        QVERIFY(r.code != 0);
        QCOMPARE(readFile(outside + u"/Plan.md"_s), "- plan zażółć\n"_ba);
        {
            QSettings settings(settingsFile(), QSettings::IniFormat);
            settings.setValue(u"vaults/followOutsideLinks"_s, true);
        }
        r = run(dir, {u"page"_s, u"list"_s});
        QCOMPARE(r.result().value(u"total"_s).toInt(), 2);
        QFile::remove(settingsFile());
#endif
    }

    // The documentation of either language (an English one, and a Polish one left from before
    // the UI language changed) is never the user's notebook.
    void documentationOfEitherLanguageIsNoNotebook()
    {
        const QString dir = vault({{u"Dom.md"_s, "- dom\n"}});
        const QString ini = settingsFile();
        const QString english = appDataDir() + u"/Brulion Documentation"_s;
        const QString polish = appDataDir() + u"/Dokumentacja Brulionu"_s;
        QDir().mkpath(english);
        QDir().mkpath(polish);
        {
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue(u"vaults/current"_s, english);
            settings.setValue(u"vaults/recent"_s,
                              QVariantList{QVariantMap{{u"path"_s, polish}}, QVariantMap{{u"path"_s, english}},
                                           QVariantMap{{u"path"_s, dir}}});
        }
        Run r = cli({u"info"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.json.value(u"vault"_s).toString(), dir);
        {
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue(u"vaults/current"_s, QString(polish + u'/'));
            settings.setValue(u"vaults/recent"_s, QVariantList{QVariantMap{{u"path"_s, english}}});
        }
        r = cli({u"capture"_s, u"x"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"no_vault"_s);
        QVERIFY(!QFileInfo::exists(english + u"/journals"_s));
        QVERIFY(!QFileInfo::exists(polish + u"/journals"_s));
        QFile::remove(ini);
        QDir(english).removeRecursively();
        QDir(polish).removeRecursively();
    }

    // A dry run shows what the real run writes: a daily note that does not exist yet comes from
    // the journal template; page create, rename, move and delete show their changes.
    void dryRunsShowTheRealChange()
    {
        const QString dir = vault({{u"templates/journal.md"_s, "## {{weekday}}\n### Plan\n- \n"},
                                   {u"Projekty/Dom.md"_s, "- farba\n"},
                                   {u"Notatka.md"_s, "- [[Dom]] i #Dom\n"}});
        Run dry = run(dir, {u"--dry-run"_s, u"capture"_s, u"Kupić mleko jutro o 18"_s});
        QCOMPARE(dry.code, 0);
        QVERIFY(dry.result().value(u"dryRun"_s).toBool());
        QVERIFY(dry.result().value(u"created"_s).toBool());
        QCOMPARE(dry.result().value(u"diff"_s).toString(),
                 u"--- a/journals/2026-10-14.md\n+++ b/journals/2026-10-14.md\n@@ -0,0 +1,3 @@\n+## "_s + wednesday()
                         + u"\n+### Plan\n+- [ ] Kupić mleko ⏰ 2026-10-15 18:00\n"_s);
        QVERIFY(!QFileInfo::exists(dir + u"/journals"_s));
        Run real = run(dir, {u"capture"_s, u"Kupić mleko jutro o 18"_s});
        QCOMPARE(real.code, 0);
        QVERIFY(real.result().value(u"created"_s).toBool());
        QCOMPARE(real.result().value(u"ref"_s).toString(), dry.result().value(u"ref"_s).toString());
        QCOMPARE(real.result().value(u"line"_s).toInt(), 2);
        QCOMPARE(readFile(dir + u"/journals/2026-10-14.md"_s),
                 (u"## "_s + wednesday() + u"\n### Plan\n- [ ] Kupić mleko ⏰ 2026-10-15 18:00\n"_s).toUtf8());
        // The same for task add and page append into a day without a note.
        dry = run(dir, {u"--dry-run"_s, u"task"_s, u"add"_s, u"Pompki"_s, u"--date"_s, u"jutro"_s});
        QVERIFY(dry.result().value(u"diff"_s).toString().contains(u"+## "_s + weekday(4)
                                                                  + u"\n+### Plan\n+- [ ] Pompki\n"_s));
        dry = run(dir, {u"--dry-run"_s, u"page"_s, u"append"_s, u"2026-10-16"_s, u"x"_s, u"--under"_s, u"### Plan"_s});
        QCOMPARE(dry.code, 0);
        QVERIFY(dry.result().value(u"diff"_s).toString().contains(u"+## "_s + weekday(5) + u"\n"_s));
        QVERIFY(!QFileInfo::exists(dir + u"/journals/2026-10-15.md"_s));
        QVERIFY(!QFileInfo::exists(dir + u"/journals/2026-10-16.md"_s));

        // page create: the new page's text.
        dry = run(dir, {u"--dry-run"_s, u"page"_s, u"create"_s, u"Lista"_s, u"--content"_s, u"- chleb\n"_s});
        QCOMPARE(dry.result().value(u"diff"_s).toString(),
                 u"--- a/Lista.md\n+++ b/Lista.md\n@@ -0,0 +1,1 @@\n+- chleb\n"_s);
        QVERIFY(!QFileInfo::exists(dir + u"/Lista.md"_s));
        // page rename / move: every note whose links or tags follow.
        dry = run(dir, {u"--dry-run"_s, u"--allow-destructive"_s, u"page"_s, u"rename"_s, u"Dom"_s, u"Mieszkanie"_s});
        QCOMPARE(dry.code, 0);
        const QJsonArray changes = dry.result().value(u"changes"_s).toArray();
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.first().toObject().value(u"path"_s).toString(), u"Notatka.md"_s);
        QCOMPARE(changes.first().toObject().value(u"diff"_s).toString(),
                 u"--- a/Notatka.md\n+++ b/Notatka.md\n@@ -1,1 +1,1 @@\n-- [[Dom]] i #Dom\n+- [[Mieszkanie]] i "
                 u"#Mieszkanie\n"_s);
        QCOMPARE(dry.result().value(u"diff"_s).toString(), changes.first().toObject().value(u"diff"_s).toString());
        QCOMPARE(readFile(dir + u"/Notatka.md"_s), "- [[Dom]] i #Dom\n"_ba);
        QVERIFY(QFileInfo::exists(dir + u"/Projekty/Dom.md"_s));
        // page / folder delete: what would go to the trash.
        dry = run(dir, {u"--dry-run"_s, u"--allow-destructive"_s, u"page"_s, u"delete"_s, u"Dom"_s});
        QCOMPARE(dry.result().value(u"files"_s).toArray(), QJsonArray{u"Projekty/Dom.md"_s});
        QCOMPARE(dry.result().value(u"diff"_s).toString(),
                 u"--- a/Projekty/Dom.md\n+++ /dev/null\n@@ -1,1 +0,0 @@\n-- farba\n"_s);
        dry = run(dir, {u"--dry-run"_s, u"--allow-destructive"_s, u"folder"_s, u"delete"_s, u"Projekty"_s});
        QCOMPARE(dry.result().value(u"files"_s).toArray(), QJsonArray{u"Projekty/Dom.md"_s});
        QVERIFY(dry.result().value(u"folder"_s).toBool());
        QVERIFY(QFileInfo::exists(dir + u"/Projekty/Dom.md"_s));
    }

    void fileDelete()
    {
        const QString dir = vault({{u"Dom.md"_s, "- ![[plan żółwia.pdf]]\n"},
                                   {u"plan żółwia.pdf"_s, "%PDF-1.4"},
                                   {u"Załączniki/zdjęcie.png"_s, "png"}});
        // `page delete` keeps refusing an attachment.
        Run r = run(dir, {u"--allow-destructive"_s, u"page"_s, u"delete"_s, u"plan żółwia.pdf"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"not_a_page"_s);
        // Destructive: not without the flag.
        r = run(dir, {u"file"_s, u"delete"_s, u"plan żółwia.pdf"_s});
        QVERIFY(r.code != 0);
        QVERIFY(QFileInfo::exists(dir + u"/plan żółwia.pdf"_s));
        // A dry run names the file and leaves it.
        r = run(dir, {u"--dry-run"_s, u"--allow-destructive"_s, u"file"_s, u"delete"_s, u"plan żółwia.pdf"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"files"_s).toArray(), QJsonArray{u"plan żółwia.pdf"_s});
        QVERIFY(QFileInfo::exists(dir + u"/plan żółwia.pdf"_s));
        // The real run: to the trash, bytes kept.
        r = run(dir, {u"--allow-destructive"_s, u"file"_s, u"delete"_s, u"plan żółwia.pdf"_s});
        QCOMPARE(r.code, 0);
        const QString trashPath = r.result().value(u"trashPath"_s).toString();
        QVERIFY(trashPath.startsWith(u".trash/"_s));
        QVERIFY(!QFileInfo::exists(dir + u"/plan żółwia.pdf"_s));
        QCOMPARE(readFile(dir + u'/' + trashPath), "%PDF-1.4"_ba);
        // A page is a file too.
        QCOMPARE(run(dir, {u"--allow-destructive"_s, u"file"_s, u"delete"_s, u"Dom.md"_s}).code, 0);
        QVERIFY(!QFileInfo::exists(dir + u"/Dom.md"_s));
        // Neither a missing file, nor a folder, nor anything inside the trash (deleted for good there).
        r = run(dir, {u"--allow-destructive"_s, u"file"_s, u"delete"_s, u"nie ma.pdf"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"file_not_found"_s);
        r = run(dir, {u"--allow-destructive"_s, u"file"_s, u"delete"_s, u"Załączniki"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"not_a_file"_s);
        QVERIFY(QFileInfo::exists(dir + u"/Załączniki/zdjęcie.png"_s));
        r = run(dir, {u"--allow-destructive"_s, u"file"_s, u"delete"_s, trashPath});
        QVERIFY(r.code != 0);
        QVERIFY(QFileInfo::exists(dir + u'/' + trashPath));
    }

    void extensionsForAgents()
    {
        // What an agent needs to write an extension: the reference, the extensions there are, the
        // code checked and tried in a sandbox (writes computed, never made), the page written —
        // and never approved: the app's approval stays the user's.
        Run r = cli({u"extension"_s, u"docs"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"text"_s).toString().contains(u"## The API (`b`)"_s));
        QVERIFY(r.result().value(u"sections"_s).toArray().contains(u"Hooks"_s));
        QVERIFY(r.result().value(u"sections"_s).toArray().contains(u"Troubleshooting"_s)); // after "```` ```luau ````"
        r = cli({u"extension"_s, u"docs"_s, u"hooks"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"text"_s).toString().startsWith(u"## Hooks\n"_s));
        QVERIFY(!r.result().value(u"text"_s).toString().contains(u"## Questions"_s));
        r = cli({u"extension"_s, u"docs"_s, u"żółw"_s});
        QCOMPARE(r.code, 3);

        const QByteArray counter = "---\ntags: [rozszerzenie]\nopis: Liczy zakupy.\nuprawnienia: [zapis]\n---\n"
                                   "Liczy otwarte zakupy.\n\n```luau\n"
                                   "b.command(\"policz\", function(ctx)\n"
                                   "    local n = b.query(\"type:tasks status:open\").total\n"
                                   "    b.appendToJournal(b.today(), `- Zakupy: {n}`)\n"
                                   "    print(\"policzone\")\n"
                                   "    return `{ctx.page}: {n}`\n"
                                   "end, { label = \"Policz zakupy\" })\n"
                                   "b.command(\"pytaj\", function()\n"
                                   "    return \"Odpowiedź: \" .. b.ask(\"Co?\")\n"
                                   "end)\n"
                                   "b.view(\"lista\", function(p)\n"
                                   "    return b.ui.column({ b.ui.heading(p.tytul or \"Lista\") })\n"
                                   "end)\n"
                                   "b.on(\"save\", function(e) return \"zapisano \" .. e.page end)\n"
                                   "```\n";
        const QString dir = vault({{u"Rozszerzenia/Licznik.md"_s, counter},
                                   {u"Zakupy.md"_s, "- [ ] mleko\n- [ ] chleb\n"}});
        r = run(dir, {u"extension"_s, u"list"_s});
        QCOMPARE(r.code, 0);
        QJsonArray pages = r.result().value(u"pages"_s).toArray();
        QCOMPARE(pages.size(), 1);
        QJsonObject page = pages.first().toObject();
        QCOMPARE(page.value(u"id"_s).toString(), u"licznik"_s);
        QCOMPARE(page.value(u"state"_s).toString(), u"approval"_s); // never approved on this device
        QCOMPARE(page.value(u"permissions"_s).toArray(), QJsonArray{u"write"_s});
        // The gym, the AI assistant, the habits, the flashcards.
        QCOMPARE(r.result().value(u"builtins"_s).toArray().size(), 4);
        // Approved in the app (its device state): approved here too.
        writeFile(stateDirOf(dir) + u"/store.json"_s,
                  QJsonDocument(QJsonObject{{u"extensions/approved"_s,
                                             QJsonArray{page.value(u"codeHash"_s).toString()}}})
                          .toJson());
        r = run(dir, {u"extension"_s, u"list"_s});
        QCOMPARE(r.result().value(u"pages"_s).toArray().first().toObject().value(u"state"_s).toString(),
                 u"approved"_s);

        r = run(dir, {u"extension"_s, u"check"_s, u"Licznik"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"loaded"_s).toBool());
        QCOMPARE(r.result().value(u"commands"_s).toArray().size(), 2);
        QCOMPARE(r.result().value(u"views"_s).toArray().size(), 1);
        QCOMPARE(r.result().value(u"hooks"_s).toArray(), QJsonArray{u"save"_s});
        // A text not saved yet; an error names the page's line.
        r = run(dir, {u"extension"_s, u"check"_s, u"Zepsute"_s, u"--stdin"_s},
                "---\ntags: [extension]\n---\nText.\n\n```luau\nlocal x = = 1\n```\n");
        QCOMPARE(r.code, 6);
        QCOMPARE(r.errorCode(), u"extension_error"_s);
        QJsonObject details = r.json.value(u"error"_s).toObject().value(u"details"_s).toObject();
        QCOMPARE(details.value(u"error"_s).toObject().value(u"line"_s).toInt(), 7);
        QVERIFY(!QFile::exists(dir + u"/Rozszerzenia/Zepsute.md"_s));
        r = run(dir, {u"extension"_s, u"check"_s, u"Zakupy"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"not_an_extension"_s);

        // Running: the command's message, the journal line it would append, what it printed.
        r = run(dir, {u"extension"_s, u"run"_s, u"licznik"_s, u"policz"_s, u"--page"_s, u"Zakupy"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"value"_s).toString(), u"Zakupy: 2"_s);
        QCOMPARE(r.result().value(u"printed"_s).toArray(), QJsonArray{u"policzone"_s});
        const QJsonArray writes = r.result().value(u"writes"_s).toArray();
        QCOMPARE(writes.size(), 1);
        QVERIFY(writes.first().toObject().value(u"created"_s).toBool());
        QVERIFY(writes.first().toObject().value(u"diff"_s).toString().contains(u"+- Zakupy: 2"_s));
        QVERIFY(QDir(dir).entryList({u"journals"_s}, QDir::Dirs).isEmpty()); // nothing written
        // Questions: answered in order, or the run stops at one without an answer.
        r = run(dir, {u"extension"_s, u"run"_s, u"Licznik"_s, u"pytaj"_s, u"--answers"_s, uR"(["Żółw"])"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"value"_s).toString(), u"Odpowiedź: Żółw"_s);
        r = run(dir, {u"extension"_s, u"run"_s, u"Licznik"_s, u"pytaj"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"status"_s).toString(), u"waiting"_s);
        QVERIFY(r.warnings().contains(u"question_unanswered"_s));
        r = run(dir, {u"extension"_s, u"run"_s, u"Licznik"_s, u"pytaj"_s, u"--answers"_s, u"nie json"_s});
        QCOMPARE(r.code, 2);
        // A view's node tree, its params; a hook.
        r = run(dir, {u"extension"_s, u"run"_s, u"Licznik"_s, u"--view"_s, u"lista"_s, u"--args"_s,
                      uR"({"tytul": "Łąka"})"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(QJsonDocument(r.result().value(u"value"_s).toObject()).toJson().contains("\xc5\x81\xc4\x85ka"));
        r = run(dir, {u"extension"_s, u"run"_s, u"Licznik"_s, u"--hook"_s, u"save"_s, u"--page"_s, u"Zakupy"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"values"_s).toArray(), QJsonArray{u"zapisano Zakupy"_s});
        // A command's arguments (ctx.args): the built-in habits' schedule reader.
        r = run(dir, {u"extension"_s, u"run"_s, u"nawyki"_s, u"schedule"_s, u"--args"_s, uR"({"text": "3 razy w tygodniu"})"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"value"_s).toObject().value(u"times"_s).toInt(), 3);
        r = run(dir, {u"extension"_s, u"run"_s, u"Licznik"_s, u"brak"_s});
        QCOMPARE(r.code, 6);
        r = run(dir, {u"extension"_s, u"run"_s, u"Licznik"_s});
        QCOMPARE(r.code, 2);
        // The console.
        r = run(dir, {u"extension"_s, u"eval"_s, u"b.today()"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"value"_s).toString(), u"2026-10-14"_s);
        r = run(dir, {u"extension"_s, u"eval"_s, u"--stdin"_s}, "b.write(\"Zakupy\", \"- [x] mleko\\n\")\n");
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"writes"_s).toArray().size(), 1);
        QCOMPARE(readFile(dir + u"/Zakupy.md"_s), "- [ ] mleko\n- [ ] chleb\n"_ba);
        r = run(dir, {u"extension"_s, u"eval"_s, u"error(\"ups\")"_s});
        QCOMPARE(r.code, 6);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"details"_s).toObject().value(u"error"_s).toObject().value(
                         u"line"_s).toInt(),
                 1);
        // A built-in's source and the page that would replace it.
        r = run(dir, {u"extension"_s, u"show"_s, u"silownia"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"code"_s).toString().contains(u"b.command"_s));
        QVERIFY(r.result().value(u"overridePage"_s).toString().contains(u"silownia"_s));

        // Writing: a new page around the code, in the notebook language's folder; checked first.
        const QString code = u"b.command(\"hej\", function() return \"Cześć\" end, { label = \"Hej\" })"_s;
        r = run(dir, {u"--lang"_s, u"pl"_s, u"extension"_s, u"write"_s, u"Powitanie"_s, u"--code"_s, code,
                      u"--description"_s, u"Mówi: cześć"_s, u"--permissions"_s, u"zapis"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Rozszerzenia/Powitanie.md"_s);
        QVERIFY(!r.result().value(u"approved"_s).toBool());
        QVERIFY(r.warnings().contains(u"needs_approval"_s));
        QCOMPARE(r.result().value(u"check"_s).toObject().value(u"commands"_s).toArray().size(), 1);
        const QByteArray written = readFile(dir + u"/Rozszerzenia/Powitanie.md"_s);
        QVERIFY2(written.contains("opis: 'Mówi: cześć'") || written.contains("opis: \"Mówi: cześć\""), written.constData());
        QVERIFY(written.contains(code.toUtf8()));
        r = run(dir, {u"extension"_s, u"show"_s, u"Powitanie"_s});
        QCOMPARE(r.result().value(u"description"_s).toString(), u"Mówi: cześć"_s);
        QCOMPARE(r.result().value(u"permissions"_s).toArray(), QJsonArray{u"write"_s});
        const QString hash = r.result().value(u"hash"_s).toString();
        // Replacing needs its hash; broken code is not written unless forced.
        r = run(dir, {u"extension"_s, u"write"_s, u"Powitanie"_s, u"--code"_s, u"return 1"_s});
        QCOMPARE(r.code, 4);
        QCOMPARE(r.errorCode(), u"hash_required"_s);
        r = run(dir, {u"extension"_s, u"write"_s, u"Powitanie"_s, u"--code"_s, u"b.command(("_s, u"--expect-hash"_s,
                      hash});
        QCOMPARE(r.code, 6);
        QCOMPARE(readFile(dir + u"/Rozszerzenia/Powitanie.md"_s), written);
        r = run(dir, {u"--lang"_s, u"pl"_s, u"extension"_s, u"write"_s, u"powitanie"_s, u"--code"_s,
                      u"b.command(\"hej\", function() return \"Hej!\" end)"_s, u"--expect-hash"_s, hash});
        QCOMPARE(r.code, 0);
        QVERIFY(r.warnings().contains(u"page_replaced"_s));
        QVERIFY(readFile(dir + u"/Rozszerzenia/Powitanie.md"_s).contains("opis: ")); // kept from the page
        QVERIFY(readFile(dir + u"/Rozszerzenia/Powitanie.md"_s).contains("Hej!"));
        r = run(dir, {u"extension"_s, u"write"_s, u"Zakupy"_s, u"--code"_s, u"return 1"_s});
        QCOMPARE(r.code, 4);
        QCOMPARE(r.errorCode(), u"not_an_extension"_s);
        r = run(dir, {u"extension"_s, u"write"_s, u"Bez tagu"_s, u"--content"_s, u"```luau\nreturn 1\n```\n"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"not_an_extension_page"_s);
        r = run(dir, {u"--read-only"_s, u"extension"_s, u"write"_s, u"Inne"_s, u"--code"_s, u"return 1"_s});
        QCOMPARE(r.code, 7);
        r = run(dir, {u"--read-only"_s, u"extension"_s, u"check"_s, u"Powitanie"_s});
        QCOMPARE(r.code, 0);
    }

    void extensionMacros()
    {
        // Macros and template variables tried in the sandbox (docs/EXTENSIONS.md "Macros"): the
        // check names them, a run gives the value and what a note would show.
        const QByteArray page =
                "---\ntags: [rozszerzenie]\n---\n```luau\n"
                "b.macro(\"otwarte\", function(ctx)\n"
                "    local n = b.query(\"type:tasks status:open\").total\n"
                "    return { b.ui.heading(tostring(n)), ` na {ctx.page} ({table.concat(ctx.args, \"/\")})` }\n"
                "end, { description = \"Otwarte zadania\", args = { \"x\" }, example = \"a\" })\n"
                "b.variable(\"plan\", function(ctx) return `- plan {ctx.date or \"-\"}` end)\n"
                "```\n";
        const QString dir = vault({{u"Rozszerzenia/Makra.md"_s, page},
                                   {u"Zakupy.md"_s, "- [ ] mleko\n- [ ] chleb\n"},
                                   {u"journals/2026-10-14.md"_s, "- dzień\n"}});
        Run r = run(dir, {u"extension"_s, u"check"_s, u"Makra"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"macros"_s).toArray().size(), 1);
        QCOMPARE(r.result().value(u"macros"_s).toArray().first().toObject().value(u"example"_s).toString(), u"a"_s);
        QCOMPARE(r.result().value(u"variables"_s).toArray().first().toObject().value(u"name"_s).toString(), u"plan"_s);
        r = run(dir, {u"extension"_s, u"run"_s, u"Makra"_s, u"--macro"_s, u"otwarte"_s, u"--text"_s,
                      u"żółw, \"a, b\""_s, u"--page"_s, u"Zakupy"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"shows"_s).toString(), u"2 na Zakupy (żółw/a, b)"_s);
        r = run(dir, {u"extension"_s, u"run"_s, u"Makra"_s, u"--variable"_s, u"plan"_s, u"--page"_s, u"2026-10-14"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"value"_s).toString(), u"- plan 2026-10-14"_s);
        r = run(dir, {u"extension"_s, u"run"_s, u"Makra"_s, u"--macro"_s, u"brak"_s});
        QCOMPARE(r.code, 6);
        r = run(dir, {u"extension"_s, u"run"_s, u"Makra"_s, u"--macro"_s, u"otwarte"_s, u"--variable"_s, u"plan"_s});
        QCOMPARE(r.code, 2);
    }

    void extensionsApprovedSettingsAndCopies()
    {
        // What changed since the approval on this device, an extension's settings on it, and a
        // copy into another notebook with the page it requires — the command line approves nothing.
        const QByteArray shared =
                "---\ntags: [rozszerzenie]\n---\n```luau\nreturn { dwa = function(s) return s .. s end }\n```\n";
        const QByteArray counter = "---\ntags: [rozszerzenie]\n---\n```luau\n"
                                   "local w = b.require(\"Wspólne\")\n"
                                   "b.settings({ { key = \"ile\", label = \"Ile\", value = 3, step = 1 }, "
                                   "{ key = \"tryb\", label = \"Tryb\", options = { \"krótko\", \"długo\" } } })\n"
                                   "b.command(\"x\", function() return w.dwa(\"ż\") end)\n"
                                   "b.on(\"task\", function(e) return e.task .. e.previous end)\n"
                                   "```\n";
        const QString dir = vault({{u"Rozszerzenia/Licznik.md"_s, counter},
                                   {u"Rozszerzenia/Wspólne.md"_s, shared},
                                   {u"Zakupy.md"_s, "- [ ] mleko\n"}});
        // Never approved here: all of its code is new.
        Run r = run(dir, {u"extension"_s, u"diff"_s, u"Licznik"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(!r.result().value(u"approvedBefore"_s).toBool());
        QCOMPARE(r.result().value(u"added"_s).toInt(), 4);
        QVERIFY(r.warnings().contains(u"never_approved"_s));
        // Approved in the app with its code kept; a line changed since: one added, one removed.
        const QString hash =
                run(dir, {u"extension"_s, u"show"_s, u"Licznik"_s}).result().value(u"codeHash"_s).toString();
        const QString oldCode =
                run(dir, {u"extension"_s, u"show"_s, u"Licznik"_s}).result().value(u"code"_s).toString();
        writeFile(stateDirOf(dir) + u"/store.json"_s,
                  QJsonDocument(QJsonObject{{u"extensions/approved"_s, QJsonArray{hash}},
                                            {u"extensions/approvedCode/licznik"_s,
                                             QJsonObject{{u"hash"_s, hash},
                                                         {u"code"_s, oldCode},
                                                         {u"permissions"_s, QJsonArray{}}}}})
                          .toJson());
        QByteArray changed = counter;
        changed.replace("w.dwa(\"ż\")", "w.dwa(\"ł\")");
        writeFile(dir + u"/Rozszerzenia/Licznik.md"_s, changed);
        r = run(dir, {u"extension"_s, u"diff"_s, u"Licznik"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"approvedBefore"_s).toBool());
        QVERIFY(!r.result().value(u"approved"_s).toBool());
        QCOMPARE(r.result().value(u"added"_s).toInt(), 1);
        QCOMPARE(r.result().value(u"removed"_s).toInt(), 1);
        bool addedLine = false;
        for (const QJsonValue &l : r.result().value(u"lines"_s).toArray())
            addedLine = addedLine
                        || (l.toObject().value(u"kind"_s) == u"added"_s
                            && l.toObject().value(u"text"_s).toString().contains(u"w.dwa(\"ł\")"_s));
        QVERIFY(addedLine);
        // Its settings: declared, set, refused, back to the defaults.
        r = run(dir, {u"extension"_s, u"settings"_s, u"Licznik"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"settings"_s).toArray().size(), 2);
        QCOMPARE(r.result().value(u"settings"_s).toArray().first().toObject().value(u"current"_s).toInt(), 3);
        r = run(dir, {u"extension"_s, u"settings"_s, u"Licznik"_s, u"--set"_s, u"ile=7,5"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"settings"_s).toArray().first().toObject().value(u"current"_s).toDouble(), 7.5);
        r = run(dir, {u"extension"_s, u"settings"_s, u"Licznik"_s, u"--set"_s, u"tryb=średnio"_s});
        QCOMPARE(r.code, 2);
        r = run(dir, {u"extension"_s, u"settings"_s, u"Licznik"_s, u"--set"_s, u"nie-ma=1"_s});
        QCOMPARE(r.errorCode(), u"unknown_setting"_s);
        r = run(dir, {u"extension"_s, u"settings"_s, u"Licznik"_s, u"--set"_s, u"tryb=długo"_s});
        QCOMPARE(r.result().value(u"settings"_s).toArray().at(1).toObject().value(u"current"_s).toString(), u"długo"_s);
        r = run(dir, {u"extension"_s, u"settings"_s, u"Licznik"_s, u"--reset"_s});
        QCOMPARE(r.result().value(u"settings"_s).toArray().first().toObject().value(u"current"_s).toInt(), 3);
        // A hook of the new events, with the event the app would give.
        r = run(dir, {u"extension"_s, u"run"_s, u"Licznik"_s, u"--hook"_s, u"task"_s, u"--page"_s, u"Zakupy"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"values"_s).toArray(), QJsonArray{u"x "_s});
        // A copy: the page and the one it requires, at the same paths; a conflict, then replaced.
        const QString other = vault({{u"Inne.md"_s, "- coś\n"}});
        r = run(dir, {u"extension"_s, u"copy"_s, u"Licznik"_s, u"--to"_s, other});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(other + u"/Rozszerzenia/Licznik.md"_s), changed);
        QCOMPARE(readFile(other + u"/Rozszerzenia/Wspólne.md"_s), shared);
        QVERIFY(r.warnings().contains(u"needs_approval"_s));
        r = run(other, {u"extension"_s, u"list"_s});
        QCOMPARE(r.result().value(u"pages"_s).toArray().first().toObject().value(u"state"_s).toString(), u"approval"_s);
        writeFile(other + u"/Rozszerzenia/Wspólne.md"_s, "zmienione tam\n");
        r = run(dir, {u"extension"_s, u"copy"_s, u"Licznik"_s, u"--to"_s, other});
        QCOMPARE(r.code, 4);
        QCOMPARE(r.errorCode(), u"conflict"_s);
        r = run(dir, {u"extension"_s, u"copy"_s, u"Licznik"_s, u"--to"_s, other, u"--replace"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(other + u"/Rozszerzenia/Wspólne.md"_s), shared);
        r = run(dir, {u"extension"_s, u"copy"_s, u"Licznik"_s, u"--to"_s, dir});
        QCOMPARE(r.errorCode(), u"same_notebook"_s);
        r = run(dir, {u"extension"_s, u"copy"_s, u"Licznik"_s, u"--to"_s, dir + u"/nie-ma"_s});
        QCOMPARE(r.errorCode(), u"no_notebook"_s);
    }

    void pageWriteCanEmptyAPage()
    {
        const QString dir = vault({{u"Dom.md"_s, "- farba\n"}});
        const QString hash = run(dir, {u"page"_s, u"read"_s, u"Dom"_s}).result().value(u"hash"_s).toString();
        Run r = run(dir, {u"--allow-destructive"_s, u"page"_s, u"write"_s, u"Dom"_s, u"--content"_s, QString(),
                          u"--expect-hash"_s, hash});
        QCOMPARE(r.code, 0);
        QVERIFY(QFileInfo::exists(dir + u"/Dom.md"_s));
        QCOMPARE(readFile(dir + u"/Dom.md"_s), QByteArray());
        r = run(dir, {u"--allow-destructive"_s, u"page"_s, u"write"_s, u"Dom"_s, u"--expect-hash"_s, hash});
        QCOMPARE(r.code, 2); // no text at all
        QCOMPARE(r.errorCode(), u"text_required"_s);
    }

    /// environment() with a locale: "NAME=value" pairs (LANG, LC_ALL, LC_MESSAGES, LANGUAGE).
    QProcessEnvironment localeEnvironment(const QStringList &variables) const
    {
        QProcessEnvironment env = environment();
        for (const QString &v : variables)
            env.insert(v.section(u'=', 0, 0), v.section(u'=', 1));
        return env;
    }

    // Messages for people in Polish when the system's language for messages is Polish (as
    // gettext programs pick it: LANGUAGE, LC_ALL, LC_MESSAGES, LANG), else English; "C" is English.
    void messagesInTheSystemLanguage_data()
    {
        QTest::addColumn<QStringList>("variables");
        QTest::addColumn<bool>("polish");
        QTest::newRow("LANG pl_PL.UTF-8") << QStringList{u"LANG=pl_PL.UTF-8"_s} << true;
        QTest::newRow("LANG pl_PL") << QStringList{u"LANG=pl_PL"_s} << true;
        QTest::newRow("LC_ALL over LANG") << QStringList{u"LANG=en_US.UTF-8"_s, u"LC_ALL=pl_PL.UTF-8"_s} << true;
        QTest::newRow("LC_MESSAGES over LANG") << QStringList{u"LANG=C.UTF-8"_s, u"LC_MESSAGES=pl_PL.UTF-8"_s} << true;
        QTest::newRow("LANGUAGE list") << QStringList{u"LANG=de_DE.UTF-8"_s, u"LANGUAGE=pl:en"_s} << true;
        QTest::newRow("LANGUAGE en") << QStringList{u"LANG=pl_PL.UTF-8"_s, u"LANGUAGE=en"_s} << false;
        QTest::newRow("LC_ALL C") << QStringList{u"LANG=pl_PL.UTF-8"_s, u"LC_ALL=C"_s} << false;
        QTest::newRow("C.UTF-8") << QStringList{u"LANG=C.UTF-8"_s} << false;
        QTest::newRow("English") << QStringList{u"LANG=en_GB.UTF-8"_s} << false;
        QTest::newRow("German") << QStringList{u"LANG=de_DE.UTF-8"_s} << false;
    }

    void messagesInTheSystemLanguage()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows takes the language from the user's settings, not from LANG");
#endif
        QFETCH(QStringList, variables);
        QFETCH(bool, polish);
        const QProcessEnvironment env = localeEnvironment(variables);
        Run r = cli({u"zółw"_s}, {}, env);
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"unknown_command"_s);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"message"_s).toString(),
                 polish ? u"Nieznane polecenie: zółw (spróbuj: help)"_s : u"Unknown command: zółw (try: help)"_s);
        r = cli({u"help"_s}, {}, env);
        QCOMPARE(r.code, 0);
        QCOMPARE(QString::fromUtf8(r.out).contains(u"Opcje globalne:"_s), polish);
        QCOMPARE(QString::fromUtf8(r.out).contains(u"Global options:"_s), !polish);
    }

    // In Polish: the help, errors, warnings and --format text are Polish, also the core's messages
    // the CLI passes on (a query's); command and option names, JSON keys, error and warning codes,
    // `help --json`, `schema` and the MCP server stay English.
    void polishMessagesKeepTheMachineFieldsEnglish()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows takes the language from the user's settings, not from LANG");
#endif
        const QString dir = vault({{u"Zakupy.md"_s, "- mleko\n"}});
        const QProcessEnvironment env = localeEnvironment({u"LANG=pl_PL.UTF-8"_s});
        const auto runPl = [&](QStringList args, const QByteArray &input = {}) {
            args = QStringList{u"--vault"_s, dir, u"--now"_s, kNow} + args;
            return cli(args, input, env);
        };
        // An error: the envelope's keys and the code in English, the message in Polish.
        Run r = runPl({u"page"_s, u"read"_s, u"Brak"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.json.keys(), (QStringList{u"command"_s, u"error"_s, u"ok"_s, u"vault"_s, u"warnings"_s}));
        QCOMPARE(r.json.value(u"command"_s).toString(), u"page.read"_s);
        const QJsonObject error = r.json.value(u"error"_s).toObject();
        QCOMPARE(error.value(u"code"_s).toString(), u"page_not_found"_s);
        QVERIFY2(error.value(u"message"_s).toString().startsWith(u"Nie ma strony "_s), r.out.constData());
        // A bad argument type: a whole Polish sentence.
        r = runPl({u"task"_s, u"list"_s, u"--limit"_s, u"dużo"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"bad_argument"_s);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"message"_s).toString(), u"limit musi być liczbą całkowitą"_s);
        // A warning: its code in English, its message in Polish; the result's keys in English.
        r = runPl({u"task"_s, u"add"_s, u"Zapłacić rachunek"_s, u"--due"_s, u"wczoraj"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.warnings(), QStringList{u"date_in_past"_s});
        QCOMPARE(r.json.value(u"warnings"_s).toArray().first().toObject().value(u"message"_s).toString(),
                 u"Data w --due jest w przeszłości: 2026-10-13"_s);
        QVERIFY(r.result().contains(u"path"_s));
        QVERIFY(r.result().contains(u"written"_s));
        // A core message (the query parser's) from the same catalogue.
        r = runPl({u"query"_s, u"\"mleko"_s});
        QCOMPARE(r.code, 6);
        QCOMPARE(r.errorCode(), u"query_error"_s);
        QVERIFY2(r.json.value(u"error"_s).toObject().value(u"message"_s).toString().contains(
                         u"Brak zamykającego cudzysłowu"_s),
                 r.out.constData());
        // --format text.
        r = runPl({u"--format"_s, u"text"_s, u"page"_s, u"read"_s, u"Brak"_s});
        QCOMPARE(r.code, 3);
        QVERIFY2(r.out.startsWith("błąd: Nie ma strony "), r.out.constData());
        QVERIFY2(r.out.split('\n').first().endsWith(" (page_not_found)"), r.out.constData());
        // The help: Polish text around English command and option names.
        r = cli({u"help"_s, u"task"_s, u"add"_s}, {}, env);
        QCOMPARE(r.code, 0);
        const QString help = QString::fromUtf8(r.out);
        QVERIFY2(help.contains(u"brulion-cli task add <text>"_s), r.out.constData());
        QVERIFY(help.contains(u"Dodaj zadanie z kanonicznymi znacznikami (📅 ⏳ 🛫 ⏰ 🔁, priorytet)"_s));
        QVERIFY(help.contains(u"Argumenty:"_s));
        QVERIFY(help.contains(u"--due"_s));
        QVERIFY(help.contains(u"Termin 📅"_s));
        QVERIFY(help.contains(u"Zapisuje notatki (odmowa przy --read-only; --dry-run pokazuje zmianę)."_s));
        // For programs: `help --json` and `schema` keep the English texts.
        r = cli({u"help"_s, u"--json"_s}, {}, env);
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("\"Add a task with canonical tokens"));
        QVERIFY(!r.out.contains("Dodaj zadanie"));
        r = cli({u"schema"_s, u"task.add"_s}, {}, env);
        QCOMPARE(r.code, 0);
        QVERIFY(r.out.contains("Add a task with canonical tokens"));
        QVERIFY(!r.out.contains("Dodaj zadanie"));
        // The MCP server speaks English: tool descriptions, tool errors and JSON-RPC errors.
        QProcess p;
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(BRULION_CLI), {u"--vault"_s, dir, u"--now"_s, kNow, u"mcp"_s});
        QVERIFY(p.waitForStarted(kTimeoutMs));
        p.write(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"
                "\n"
                R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"
                "\n"
                R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"brulion_page_read","arguments":{"ref":"Brak"}}})"
                "\n"
                R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"brulion_zolw","arguments":{}}})"
                "\n");
        p.closeWriteChannel();
        QVERIFY(p.waitForFinished(kTimeoutMs));
        const QList<QByteArray> lines = p.readAllStandardOutput().trimmed().split('\n');
        QCOMPARE(lines.size(), 4);
        QVERIFY(lines.at(1).contains("Add a task with canonical tokens"));
        QVERIFY(!lines.at(1).contains("Dodaj zadanie"));
        const QJsonObject called = QJsonDocument::fromJson(lines.at(2)).object().value(u"result"_s).toObject();
        QVERIFY(called.value(u"isError"_s).toBool());
        const QJsonObject mcpError = called.value(u"structuredContent"_s).toObject().value(u"error"_s).toObject();
        QCOMPARE(mcpError.value(u"code"_s).toString(), u"page_not_found"_s);
        QVERIFY2(mcpError.value(u"message"_s).toString().startsWith(u"No page "_s), lines.at(2).constData());
        QCOMPARE(QJsonDocument::fromJson(lines.at(3)).object().value(u"error"_s).toObject().value(u"message"_s).toString(),
                 u"Unknown tool: brulion_zolw"_s);
#ifdef BRULION_APP
        // `brulion cli …` in the app's binary: the same catalogue (the app's own).
        QProcessEnvironment appEnv = env;
        appEnv.remove(u"QT_QPA_PLATFORM"_s);
        appEnv.remove(u"DISPLAY"_s);
        appEnv.remove(u"WAYLAND_DISPLAY"_s);
        p.setProcessEnvironment(appEnv);
        p.start(QStringLiteral(BRULION_APP), {u"cli"_s, u"--vault"_s, dir, u"page"_s, u"read"_s, u"Brak"_s});
        QVERIFY(p.waitForFinished(kTimeoutMs));
        QCOMPARE(p.exitCode(), 3);
        const QJsonObject appJson = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
        QCOMPARE(appJson.value(u"error"_s).toObject().value(u"code"_s).toString(), u"page_not_found"_s);
        QVERIFY(appJson.value(u"error"_s).toObject().value(u"message"_s).toString().startsWith(u"Nie ma strony "_s));
#endif
    }

    void polishArgvAndStdin()
    {
        const QString dir = vault();
        // LANG=C: argv is still UTF-8.
        QProcessEnvironment env = environment();
        env.insert(u"LANG"_s, u"C"_s);
        env.insert(u"LC_ALL"_s, u"C"_s);
        Run r = cli({u"--vault"_s, dir, u"--now"_s, kNow, u"journal"_s, u"append"_s, u"ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ"_s}, {},
                    env);
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/journals/2026-10-14.md"_s), "- ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ\n"_ba);
        // Standard input, several lines.
        r = run(dir, {u"journal"_s, u"append"_s, u"--stdin"_s}, "Zakupy:\nmleko\nżółty ser\n");
        QCOMPARE(r.code, 0);
        QVERIFY(readFile(dir + u"/journals/2026-10-14.md"_s).endsWith("- Zakupy:\n  mleko\n  żółty ser\n"));
        // Invalid UTF-8 is replaced and said.
        r = run(dir, {u"journal"_s, u"append"_s, u"--stdin"_s}, "zły \xff bajt\n");
        QCOMPARE(r.code, 0);
        QVERIFY(r.warnings().contains(u"stdin_invalid_utf8"_s));
        // A large text on stdin (5 MB).
        const QByteArray big = QByteArray(5 * 1024 * 1024, 'a') + "\n";
        r = run(dir, {u"page"_s, u"create"_s, u"Duża"_s, u"--stdin"_s}, big);
        QCOMPARE(r.code, 0);
        QCOMPARE(QFileInfo(dir + u"/Duża.md"_s).size(), big.size());
    }

    void twoProcessesAppendingLoseNothing()
    {
        // Two writers at once, each running brulion-cli 25 times in a row (the next run when the
        // previous one ended), as two shell loops would.
        const QString dir = vault();
        struct Writer {
            QString who;
            int runs = 0;
            int failures = 0;
            bool done = false;
            QProcess process;
        };
        Writer a;
        Writer b;
        a.who = u"A"_s;
        b.who = u"B"_s;
        const auto start = [this, &dir](Writer &w) {
            w.process.setProcessEnvironment(environment());
            w.process.setStandardOutputFile(QProcess::nullDevice());
            w.process.start(QStringLiteral(BRULION_CLI), {u"--vault"_s, dir, u"--now"_s, kNow, u"journal"_s,
                                                          u"append"_s, u"%1 %2"_s.arg(w.who).arg(w.runs)});
        };
        for (Writer *w : {&a, &b}) {
            connect(&w->process, &QProcess::finished, this, [w, &start](int code, QProcess::ExitStatus status) {
                if (code != 0 || status != QProcess::NormalExit)
                    ++w->failures;
                if (++w->runs < 25)
                    QTimer::singleShot(0, &w->process, [w, &start] { start(*w); }); // not inside its signal
                else
                    w->done = true;
            });
            start(*w);
        }
        QTRY_VERIFY_WITH_TIMEOUT(a.done && b.done, kTimeoutMs * 4);
        QCOMPARE(a.failures, 0);
        QCOMPARE(b.failures, 0);
        const QString journal = QString::fromUtf8(readFile(dir + u"/journals/2026-10-14.md"_s));
        for (int i = 0; i < 25; ++i) {
            QVERIFY2(journal.contains(u"- A %1\n"_s.arg(i)), qPrintable(u"A %1"_s.arg(i)));
            QVERIFY2(journal.contains(u"- B %1\n"_s.arg(i)), qPrintable(u"B %1"_s.arg(i)));
        }
        QCOMPARE(journal.count(u'\n'), 50);
    }

    void appOpen()
    {
        const QString dir = vault({{u"Dom.md"_s, "- a\n- b\n"}});
        // No app: exit 5, gui.running false.
        Run r = run(dir, {u"app"_s, u"open"_s, u"Dom"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"app_not_running"_s);
        QVERIFY(!r.json.value(u"error"_s)
                         .toObject()
                         .value(u"details"_s)
                         .toObject()
                         .value(u"gui"_s)
                         .toObject()
                         .value(u"running"_s)
                         .toBool(true));
        // A stand-in app on the socket the real one uses for this home.
        brulion::ipc::SingleInstance app(brulion::ipc::SingleInstance::keyFor(appDataDir()));
        QVERIFY(app.listen());
        QSignalSpy messages(&app, &brulion::ipc::SingleInstance::messageReceived);
        QProcess p;
        p.setProcessEnvironment(environment());
        p.start(QStringLiteral(BRULION_CLI),
                               {u"--vault"_s, dir, u"--now"_s, kNow, u"app"_s, u"open"_s, u"Dom.md#L2"_s});
        QTRY_VERIFY_WITH_TIMEOUT(p.state() == QProcess::NotRunning, kTimeoutMs);
        QCOMPARE(p.exitCode(), 0);
        QTRY_COMPARE(messages.size(), 1);
        const auto message = brulion::ipc::parseMessage(messages.first().first().toByteArray());
        QVERIFY(message);
        QCOMPARE(message->verb, QByteArray("open"));
        QCOMPARE(message->payload.value(u"route"_s).toObject(),
                 (QJsonObject{{u"view"_s, u"page"_s}, {u"path"_s, u"Dom.md"_s}, {u"line"_s, 1}}));
        QCOMPARE(message->payload.value(u"vault"_s).toString(), dir);
        p.start(QStringLiteral(BRULION_CLI), {u"app"_s, u"capture"_s});
        QTRY_VERIFY_WITH_TIMEOUT(p.state() == QProcess::NotRunning, kTimeoutMs);
        QTRY_COMPARE(messages.size(), 2);
        QCOMPARE(messages.last().first().toByteArray(), QByteArray("capture"));
    }

    // brulion:// addresses (docs/CLI.md "brulion:// addresses"): built (with the notebook's key from
    // the app's key file), read, carried out headless — Polish text, a heading, a reminder from a
    // phrase, a dry run with its diff, --read-only, an address of another notebook, errors with
    // their exit codes — and handed to the running app as a `uri` message.
    void uriCommands()
    {
        const QString dir = vault({{u"Zakupy.md"_s, "# Zakupy\n\n## Dziś\n- chleb\n"_ba},
                                   {u"templates/journal.md"_s, "## Plan\n- \n"_ba}});
        Run r = run(dir, {u"uri"_s, u"build"_s, u"append"_s, u"--page"_s, u"Zakupy"_s, u"--heading"_s, u"Dziś"_s,
                          u"--text"_s, u"Mleko 2% i żółty ser"_s});
        QCOMPARE(r.code, 0);
        const QString built = r.result().value(u"uri"_s).toString();
        QCOMPARE(built,
                 u"brulion://append?page=Zakupy&heading=Dzi%C5%9B&text=Mleko%202%25%20i%20%C5%BC%C3%B3%C5%82ty%20ser"_s);
        // No key yet: --with-key says so.
        r = run(dir, {u"uri"_s, u"build"_s, u"capture"_s, u"--text"_s, u"x"_s, u"--with-key"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"no_key"_s);
        brulion::automation::KeyStore keys(brulion::automation::KeyStore::fileIn(appDataDir()));
        const QString key = keys.ensureKey(brulion::Vault::idForPath(dir));
        QVERIFY(!key.isEmpty());
        r = run(dir, {u"uri"_s, u"build"_s, u"capture"_s, u"--text"_s, u"x"_s, u"--with-key"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"uri"_s).toString().endsWith(u"&key="_s + key));
        QVERIFY(r.result().value(u"uri"_s).toString().contains(u"vault="_s));
        r = run(dir, {u"uri"_s, u"build"_s, u"open"_s, u"--text"_s, u"x"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"unknown_parameter"_s);
        // A prompt link: the text is its prompt, what nobody sees taken out (and counted).
        r = run(dir, {u"uri"_s, u"build"_s, u"chat"_s, u"--text"_s, u"Streść\u200B „Zakupy”\U000E0068"_s,
                      u"--partner"_s, u"acp:claude"_s, u"--page"_s, u"Zakupy"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"uri"_s).toString(),
                 u"brulion://chat?page=Zakupy&prompt=Stre%C5%9B%C4%87%20%E2%80%9EZakupy%E2%80%9D&partner=acp%3Aclaude"_s);
        QCOMPARE(r.result().value(u"hidden"_s).toInt(), 2);
        r = run(dir, {u"uri"_s, u"run"_s, r.result().value(u"uri"_s).toString()});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"route"_s).toObject().value(u"draft"_s).toString(), u"Streść „Zakupy”"_s);
        QCOMPARE(r.result().value(u"route"_s).toObject().value(u"page"_s).toString(), u"Zakupy"_s);
        // parse: never shows the key.
        r = cli({u"uri"_s, u"parse"_s, built + u"&key=tajny"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"action"_s).toString(), u"append"_s);
        QCOMPARE(r.result().value(u"access"_s).toString(), u"write"_s);
        QCOMPARE(r.result().value(u"params"_s).toObject().value(u"text"_s).toString(), u"Mleko 2% i żółty ser"_s);
        QVERIFY(r.result().value(u"hasKey"_s).toBool());
        QVERIFY(!r.out.contains("tajny"));
        r = cli({u"uri"_s, u"parse"_s, u"brulion://append?text=%C5"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"bad_uri"_s);
        // run: a dry run first, then for real.
        r = run(dir, {u"--dry-run"_s, u"uri"_s, u"run"_s, built});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"diff"_s).toString().contains(u"+- Mleko 2% i żółty ser"_s));
        QCOMPARE(readFile(dir + u"/Zakupy.md"_s), "# Zakupy\n\n## Dziś\n- chleb\n"_ba);
        r = run(dir, {u"uri"_s, u"run"_s, built});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/Zakupy.md"_s), "# Zakupy\n\n## Dziś\n- chleb\n- Mleko 2% i żółty ser\n"_ba);
        QCOMPARE(r.result().value(u"line"_s).toInt(), 5);
        r = run(dir, {u"uri"_s, u"run"_s, u"brulion://task?text=Zadzwoni%C4%87&remind=jutro+o+18"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(readFile(dir + u"/journals/2026-10-14.md"_s), "## Plan\n- [ ] Zadzwonić ⏰ 2026-10-15 18:00\n"_ba);
        QCOMPARE(r.result().value(u"reminder"_s).toString(), u"2026-10-15 18:00"_s);
        r = run(dir, {u"uri"_s, u"run"_s, u"brulion://open?page=zakupy"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"route"_s).toObject().value(u"path"_s).toString(), u"Zakupy.md"_s);
        r = run(dir, {u"--read-only"_s, u"uri"_s, u"run"_s, built});
        QCOMPARE(r.code, 7);
        r = run(dir, {u"uri"_s, u"run"_s, u"brulion://append?page=Brak&text=x"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"page_not_found"_s);
        r = run(dir, {u"uri"_s, u"run"_s, u"brulion://new?title=zakupy"_s});
        QCOMPARE(r.code, 4);
        QCOMPARE(r.errorCode(), u"exists"_s);
        r = run(dir, {u"uri"_s, u"run"_s, u"brulion://append?vault=Inny&text=x"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"unknown_notebook"_s);
        r = run(dir, {u"uri"_s, u"run"_s, u"brulion://task?text=x&due=kiedy%C5%9B"_s});
        QCOMPARE(r.code, 6);
        r = run(dir, {u"uri"_s, u"run"_s, u"brulion://run?extension=a&command=b"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"needs_app"_s);
        // open: to the running app (a stand-in on its socket) as a `uri` message.
        r = run(dir, {u"uri"_s, u"open"_s, built});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"app_not_running"_s);
        brulion::ipc::SingleInstance app(brulion::ipc::SingleInstance::keyFor(appDataDir()));
        QVERIFY(app.listen());
        QSignalSpy messages(&app, &brulion::ipc::SingleInstance::messageReceived);
        QProcess p;
        p.setProcessEnvironment(environment());
        p.start(QStringLiteral(BRULION_CLI), {u"uri"_s, u"open"_s, built});
        QTRY_VERIFY_WITH_TIMEOUT(p.state() == QProcess::NotRunning, kTimeoutMs);
        QCOMPARE(p.exitCode(), 0);
        QTRY_COMPARE(messages.size(), 1);
        const auto message = brulion::ipc::parseMessage(messages.first().first().toByteArray());
        QVERIFY(message);
        QCOMPARE(message->verb, QByteArray("uri"));
        QCOMPARE(message->payload.value(u"uri"_s).toString(), built);
    }

    // The running app's remote control (docs/CLI.md "Remote control") as the command line sees it:
    // refusals before the app is asked, no app, an app without remote control (the hint), and a
    // stand-in control socket: the requests sent, the answers and the exit codes of its errors.
    void appRemoteControl()
    {
        Run r = cli({u"app"_s, u"state"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"app_not_running"_s);
        QVERIFY(r.json.value(u"error"_s)
                        .toObject()
                        .value(u"details"_s)
                        .toObject()
                        .value(u"hint"_s)
                        .toString()
                        .contains(u"--remote-control"_s));
        // Usage errors are found before asking the app.
        r = cli({u"app"_s, u"click"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"bad_target"_s);
        r = cli({u"app"_s, u"click"_s, u"h1"_s, u"--x"_s, u"5"_s, u"--y"_s, u"6"_s});
        QCOMPARE(r.code, 2);
        r = cli({u"app"_s, u"navigate"_s, u"{zły"_s});
        QCOMPARE(r.code, 2);
        r = cli({u"app"_s, u"find"_s});
        QCOMPARE(r.code, 2);
        r = cli({u"app"_s, u"scroll"_s});
        QCOMPARE(r.code, 2);
        // --dry-run says what it would ask; --read-only refuses input.
        r = cli({u"--dry-run"_s, u"app"_s, u"click"_s, u"h3"_s, u"--button"_s, u"right"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"method"_s).toString(), u"click"_s);
        QCOMPARE(r.result().value(u"params"_s).toObject(),
                 (QJsonObject{{u"handle"_s, u"h3"_s}, {u"button"_s, u"right"_s}}));
        r = cli({u"--read-only"_s, u"app"_s, u"type"_s, u"żółw"_s});
        QCOMPARE(r.code, 7);
        // A screenshot path could replace any .png (a picture in the notebook too): not when read-only.
        r = cli({u"--read-only"_s, u"app"_s, u"screenshot"_s, m_tmp.filePath(u"zdjęcie.png"_s)});
        QCOMPARE(r.code, 7);
        QCOMPARE(r.errorCode(), u"read_only"_s);

        // An app without remote control: refused, with the way to allow it.
        const QString key = brulion::ipc::SingleInstance::keyFor(appDataDir());
        brulion::ipc::SingleInstance app(key);
        QVERIFY(app.listen());
        r = cliLive({u"app"_s, u"state"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"remote_control_off"_s);
        QVERIFY(r.json.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"--remote-control"_s));

        // A stand-in remote control: echoes what it was asked, or fails with the code asked for.
        brulion::ipc::ControlServer control;
        QList<brulion::ipc::ControlRequest> requests;
        control.setHandler([&requests](const brulion::ipc::ControlRequest &request, brulion::ipc::ControlReply reply) {
            requests.append(request);
            const QString fail = request.params.value(u"text"_s).toString();
            // Luau that fails: `ok` false in the answer.
            const QString luau = request.params.value(request.method == u"load" ? u"source"_s : u"code"_s).toString();
            if ((request.method == u"eval" || request.method == u"load") && luau.contains(u"błąd"_s))
                reply.ok(QJsonObject{{u"ok"_s, false}, {u"error"_s, u"live:1: błąd składni"_s}, {u"line"_s, 1}});
            else if (request.method == u"eval" || request.method == u"load")
                reply.ok(QJsonObject{{u"ok"_s, true}, {u"method"_s, request.method}, {u"params"_s, request.params}});
            else if (request.method == u"find" && fail.startsWith(u"fail:"_s))
                reply.fail(fail.mid(5), u"Nie wyszło"_s);
            else if (request.method == u"open" && request.params.value(u"ref"_s).toString() == u"today")
                reply.fail(u"not_ready"_s, u"No notebook is open (status loading)"_s); // still opening
            else if (request.method == u"screenshot" && request.params.contains(u"path"_s))
                reply.ok(QJsonObject{{u"path"_s, request.params.value(u"path"_s)}}); // "saved" there
            else
                reply.ok(QJsonObject{{u"method"_s, request.method}, {u"params"_s, request.params}});
        });
        QVERIFY(control.listen(brulion::ipc::controlKeyFor(key)));
        r = cliLive({u"app"_s, u"key"_s, u"Ctrl+K"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"method"_s).toString(), u"key"_s);
        QCOMPARE(r.result().value(u"params"_s).toObject().value(u"sequence"_s).toString(), u"Ctrl+K"_s);
        r = cliLive({u"app"_s, u"type"_s, u"Zażółć"_s, u"gęślą"_s, u"jaźń"_s});
        QCOMPARE(requests.last().params.value(u"text"_s).toString(), u"Zażółć gęślą jaźń"_s);
        r = cliLive({u"app"_s, u"click"_s, u"120,340"_s, u"--modifiers"_s, u"ctrl"_s});
        QCOMPARE(requests.last().params, (QJsonObject{{u"x"_s, 120}, {u"y"_s, 340}, {u"modifiers"_s, u"ctrl"_s}}));
        r = cliLive({u"app"_s, u"drag"_s, u"h1"_s, u"Kosz"_s, u"--hold-ms"_s, u"500"_s});
        QCOMPARE(requests.last().params, (QJsonObject{{u"from"_s, QJsonObject{{u"handle"_s, u"h1"_s}}},
                                                      {u"to"_s, QJsonObject{{u"text"_s, u"Kosz"_s}}},
                                                      {u"holdMs"_s, 500}}));
        r = cliLive({u"app"_s, u"wait"_s, u"--text"_s, u"Zapisano"_s, u"--gone"_s, u"--timeout"_s, u"300"_s});
        QCOMPARE(requests.last().method, u"waitFor"_s);
        QCOMPARE(requests.last().params, (QJsonObject{{u"text"_s, u"Zapisano"_s}, {u"gone"_s, true}, {u"ms"_s, 300}}));
        QDir().mkpath(m_tmp.filePath(u"tu"_s));
        QProcess p;
        p.setProcessEnvironment(environment());
        p.setWorkingDirectory(m_tmp.filePath(u"tu"_s));
        p.start(QStringLiteral(BRULION_CLI), {u"app"_s, u"screenshot"_s, u"okno.png"_s});
        QTRY_VERIFY_WITH_TIMEOUT(p.state() == QProcess::NotRunning, kTimeoutMs);
        QCOMPARE(requests.last().params.value(u"path"_s).toString(), m_tmp.filePath(u"tu/okno.png"_s));
        // MCP: the screenshot comes back as a picture only when the file the answer names is a PNG
        // (a socket answering in the app's place must not make it hand over another file).
        writeFile(m_tmp.filePath(u"tu/okno.png"_s), QByteArray("\x89PNG\r\n\x1a\n", 8) + "obraz");
        writeFile(m_tmp.filePath(u"tu/sekret.png"_s), "SEKRET: hasło do banku\n");
        for (const QString &name : {u"okno.png"_s, u"sekret.png"_s}) {
            QProcess mcp;
            mcp.setProcessEnvironment(environment());
            mcp.start(QStringLiteral(BRULION_CLI), {u"mcp"_s, u"--app-control"_s});
            QVERIFY(mcp.waitForStarted(kTimeoutMs));
            const QJsonObject call{
                    {u"jsonrpc"_s, u"2.0"_s},
                    {u"id"_s, 2},
                    {u"method"_s, u"tools/call"_s},
                    {u"params"_s,
                     QJsonObject{{u"name"_s, u"brulion_app_screenshot"_s},
                                 {u"arguments"_s, QJsonObject{{u"path"_s, m_tmp.filePath(u"tu/"_s + name)}}}}}};
            mcp.write(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"
                      "\n"
                      + QJsonDocument(call).toJson(QJsonDocument::Compact) + '\n');
            mcp.closeWriteChannel();
            QElapsedTimer t;
            t.start();
            while (mcp.state() != QProcess::NotRunning && t.elapsed() < kTimeoutMs)
                QTest::qWait(20);
            QCOMPARE(mcp.state(), QProcess::NotRunning);
            const QList<QByteArray> lines = mcp.readAllStandardOutput().trimmed().split('\n');
            QCOMPARE(lines.size(), 2);
            const QJsonObject result = QJsonDocument::fromJson(lines.last()).object().value(u"result"_s).toObject();
            QVERIFY(!result.value(u"isError"_s).toBool());
            const QJsonArray content = result.value(u"content"_s).toArray();
            QCOMPARE(content.size(), name == u"okno.png" ? 2 : 1);
            if (content.size() == 2) {
                QCOMPARE(content.at(1).toObject().value(u"type"_s).toString(), u"image"_s);
                QCOMPARE(QByteArray::fromBase64(content.at(1).toObject().value(u"data"_s).toString().toLatin1()),
                         QByteArray("\x89PNG\r\n\x1a\n", 8) + "obraz");
            }
        }
        // `app open` asks the app (it resolves the ref in the notebook it shows).
        r = cliLive({u"app"_s, u"open"_s, u"Dom"_s, u"--line"_s, u"3"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"via"_s).toString(), u"control"_s);
        QCOMPARE(requests.last().params, (QJsonObject{{u"ref"_s, u"Dom"_s}, {u"line"_s, 3}}));
        // A notebook still opening: the single-instance message, kept by the app until it is ready.
        QSignalSpy openMessages(&app, &brulion::ipc::SingleInstance::messageReceived);
        r = cliLive({u"app"_s, u"open"_s, u"today"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"sent"_s).toString(), u"today"_s);
        QCOMPARE(requests.last().method, u"open"_s);
        QTRY_COMPARE(openMessages.size(), 1);
        QCOMPARE(openMessages.first().first().toByteArray(), QByteArray("today"));
        // `app capture` with a text saves it through the app; without one it opens the dialog.
        r = cliLive({u"app"_s, u"capture"_s, u"Kupić mleko jutro o 18"_s});
        QCOMPARE(requests.last().method, u"capture"_s);
        // The app's errors as exit codes.
        const QList<std::pair<QString, int>> codes{
                {u"bad_params"_s, 2},   {u"not_found"_s, 3},     {u"timeout"_s, 3},
                {u"stale_handle"_s, 4}, {u"ambiguous_ref"_s, 4}, {u"not_ready"_s, 5},
                {u"refused"_s, 7},      {u"failed"_s, 1},        {u"off_screen"_s, 3}};
        for (const auto &[code, exit] : codes) {
            r = cliLive({u"app"_s, u"find"_s, u"fail:"_s + code});
            QCOMPARE(r.code, exit);
            QCOMPARE(r.errorCode(), code);
            QCOMPARE(r.json.value(u"error"_s).toObject().value(u"message"_s).toString(), u"Nie wyszło"_s);
        }
        r = cliLive({u"app"_s, u"find"_s, u"fail:unauthorized"_s});
        QCOMPARE(r.code, 5); // (with a token of its own it would be told so: below)

        // Live changes (docs/CLI.md "Live changes"): what each command asks the app.
        const auto asked = [&requests](const QString &method, const QJsonObject &params) {
            if (requests.isEmpty() || requests.last().method != method || requests.last().params != params) {
                qWarning() << "asked:" << (requests.isEmpty() ? QString() : requests.last().method)
                           << QJsonDocument(requests.isEmpty() ? QJsonObject() : requests.last().params).toJson();
                return false;
            }
            return true;
        };
        r = cliLive({u"app"_s, u"eval"_s, u"return #b.pages()"_s, u"--extension"_s, u"nawyki"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(asked(u"eval"_s, {{u"code"_s, u"return #b.pages()"_s}, {u"extension"_s, u"nawyki"_s}}));
        r = cliLive({u"--stdin"_s, u"app"_s, u"eval"_s}, "return \"zażółć\"\n");
        QCOMPARE(r.code, 0);
        QVERIFY(asked(u"eval"_s, {{u"code"_s, u"return \"zażółć\"\n"_s}}));
        r = cliLive({u"app"_s, u"eval"_s, u"błąd("_s});
        QCOMPARE(r.code, 6);
        QCOMPARE(r.errorCode(), u"extension_error"_s);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"details"_s).toObject().value(u"line"_s).toInt(), 1);
        const qsizetype before = requests.size();
        QCOMPARE(cliLive({u"app"_s, u"eval"_s, u"  "_s}).code, 2);
        writeFile(m_tmp.filePath(u"żywy.luau"_s), "b.command(\"hej\", function() return \"Cześć\" end)\n");
        r = cliLive({u"app"_s, u"load"_s, u"zywy"_s, u"--file"_s, m_tmp.filePath(u"żywy.luau"_s), u"--title"_s,
                     u"Żywy"_s, u"--permissions"_s, u"write, app"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(asked(u"load"_s, {{u"id"_s, u"zywy"_s},
                                  {u"source"_s, u"b.command(\"hej\", function() return \"Cześć\" end)\n"_s},
                                  {u"title"_s, u"Żywy"_s},
                                  {u"permissions"_s, u"write, app"_s}}));
        r = cliLive({u"app"_s, u"load"_s, u"zywy"_s, u"--code"_s, u"x = 1"_s});
        QVERIFY(asked(u"load"_s, {{u"id"_s, u"zywy"_s}, {u"source"_s, u"x = 1"_s}}));
        r = cliLive({u"app"_s, u"load"_s, u"zywy"_s, u"--code"_s, u"błąd"_s});
        QCOMPARE(r.code, 6);
        const qsizetype loads = requests.size();
        QCOMPARE(cliLive({u"app"_s, u"load"_s, u"zywy"_s}).code, 2); // no source
        QCOMPARE(cliLive({u"app"_s, u"load"_s, u"zywy"_s, u"--code"_s, u"x"_s, u"--file"_s, u"a.luau"_s}).code, 2);
        QCOMPARE(cliLive({u"app"_s, u"load"_s, u"zywy"_s, u"--file"_s, m_tmp.filePath(u"brak.luau"_s)}).code, 3);
        QCOMPARE(requests.size(), loads);
        QVERIFY(before < loads);
        cliLive({u"app"_s, u"unload"_s, u"zywy"_s});
        QVERIFY(asked(u"unload"_s, {{u"id"_s, u"zywy"_s}}));
        for (const QString &word : {u"extensions"_s, u"reload"_s, u"settings"_s, u"changes"_s}) {
            r = cliLive({u"app"_s, word});
            QCOMPARE(r.code, 0);
            QVERIFY(asked(word, {}));
        }
        cliLive({u"app"_s, u"console"_s, u"--since"_s, u"12"_s, u"--limit"_s, u"5"_s});
        QVERIFY(asked(u"console"_s, {{u"since"_s, 12}, {u"limit"_s, 5}}));
        // A setting's value: JSON, else the words as a string.
        const QList<std::pair<QStringList, QJsonValue>> values{
                {{u"compact"_s}, u"compact"_s},
                {{u"18"_s}, 18},
                {{u"true"_s}, true},
                {{uR"(["journals","tasks"])"_s}, QJsonArray{u"journals"_s, u"tasks"_s}},
                {{u"Inter"_s, u"Display"_s}, u"Inter Display"_s},
                {{u"\"Żółw\""_s}, u"Żółw"_s}};
        for (const auto &[words, value] : values) {
            r = cliLive(QStringList{u"app"_s, u"set"_s, u"klucz"_s} + words);
            QCOMPARE(r.code, 0);
            QVERIFY(asked(u"set"_s, {{u"key"_s, u"klucz"_s}, {u"value"_s, value}}));
        }
        // Theme tokens: checked here as in the app, before it is asked.
        r = cliLive({u"app"_s, u"theme"_s, u"--tokens"_s, u"{\"accent\": \"#3a7\", \"dark\": {\"bg\": \"#0b0d10\"}}"_s,
                     u"--catalog"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(asked(u"theme"_s, {{u"tokens"_s, QJsonObject{{u"accent"_s, u"#3a7"_s},
                                                             {u"dark"_s, QJsonObject{{u"bg"_s, u"#0b0d10"_s}}}}},
                                   {u"catalog"_s, true}}));
        cliLive({u"app"_s, u"theme"_s, u"--clear"_s});
        QVERIFY(asked(u"theme"_s, {{u"clear"_s, true}}));
        const qsizetype themes = requests.size();
        r = cliLive({u"app"_s, u"theme"_s, u"--tokens"_s, u"{\"bgg\": \"#000\"}"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"bad_tokens"_s);
        QVERIFY(r.json.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"bg"_s));
        QCOMPARE(cliLive({u"app"_s, u"theme"_s, u"--tokens"_s, u"[1]"_s}).code, 2);
        QCOMPARE(cliLive({u"app"_s, u"theme"_s, u"--tokens"_s, u"{\"radius\": 4}"_s, u"--clear"_s}).code, 2);
        QCOMPARE(cliLive({u"app"_s, u"contributions"_s, u"--slot"_s, u"nigdzie"_s}).errorCode(), u"bad_slot"_s);
        QCOMPARE(requests.size(), themes);
        cliLive({u"app"_s, u"contributions"_s, u"--slot"_s, u"toolbar"_s});
        QVERIFY(asked(u"contributions"_s, {{u"slot"_s, u"toolbar"_s}}));
        cliLive({u"app"_s, u"inspect"_s, u"h5"_s, u"--depth"_s, u"2"_s, u"--properties"_s, u"--limit"_s, u"10"_s});
        QVERIFY(asked(u"inspect"_s,
                      {{u"handle"_s, u"h5"_s}, {u"depth"_s, 2}, {u"properties"_s, true}, {u"limit"_s, 10}}));
        cliLive({u"app"_s, u"set-property"_s, u"h5"_s, u"opacity"_s, u"0.5"_s});
        QVERIFY(asked(u"setProperty"_s, {{u"handle"_s, u"h5"_s}, {u"name"_s, u"opacity"_s}, {u"value"_s, 0.5}}));
        cliLive({u"app"_s, u"set-property"_s, u"h5"_s, u"color"_s, u"\"#ff0000\""_s});
        QVERIFY(asked(u"setProperty"_s, {{u"handle"_s, u"h5"_s}, {u"name"_s, u"color"_s}, {u"value"_s, u"#ff0000"_s}}));
        cliLive({u"app"_s, u"events"_s, u"--since"_s, u"7"_s, u"--wait"_s, u"100"_s});
        QVERIFY(asked(u"events"_s, {{u"since"_s, 7}, {u"wait"_s, 100}}));
        cliLive({u"app"_s, u"revert"_s, u"--who"_s, u"Claude Code"_s});
        QVERIFY(asked(u"revert"_s, {{u"who"_s, u"Claude Code"_s}}));
        // What changes the app is refused read-only; what only looks is not.
        const qsizetype looks = requests.size();
        for (const QStringList &args :
             {QStringList{u"eval"_s, u"return 1"_s}, QStringList{u"load"_s, u"a"_s, u"--code"_s, u"x"_s},
              QStringList{u"unload"_s, u"a"_s}, QStringList{u"reload"_s},
              QStringList{u"set"_s, u"density"_s, u"compact"_s}, QStringList{u"theme"_s, u"--clear"_s},
              QStringList{u"set-property"_s, u"h1"_s, u"opacity"_s, u"1"_s}, QStringList{u"revert"_s}}) {
            r = cliLive(QStringList{u"--read-only"_s, u"app"_s} + args);
            QVERIFY2(r.code == 7, qPrintable(args.join(u' ')));
        }
        QCOMPARE(requests.size(), looks);
        for (const QString &word : {u"inspect"_s, u"events"_s, u"console"_s, u"changes"_s, u"extensions"_s,
                                    u"settings"_s, u"contributions"_s}) {
            r = cliLive({u"--read-only"_s, u"app"_s, word});
            QVERIFY2(r.code == 0, qPrintable(word));
        }
        const qsizetype dry = requests.size();
        r = cliLive({u"--dry-run"_s, u"app"_s, u"set"_s, u"density"_s, u"compact"_s});
        QCOMPARE(r.result().value(u"method"_s).toString(), u"set"_s);
        QCOMPARE(requests.size(), dry); // (nothing asked)

        // Over MCP: the code itself, never a file (its lines would come back in a syntax error).
        {
            QProcess mcp;
            mcp.setProcessEnvironment(environment());
            mcp.start(QStringLiteral(BRULION_CLI), {u"mcp"_s, u"--app-control"_s});
            QVERIFY(mcp.waitForStarted(kTimeoutMs));
            const auto call = [](int id, const QString &tool, const QJsonObject &arguments) {
                return QJsonDocument(
                               QJsonObject{{u"jsonrpc"_s, u"2.0"_s},
                                           {u"id"_s, id},
                                           {u"method"_s, u"tools/call"_s},
                                           {u"params"_s, QJsonObject{{u"name"_s, tool}, {u"arguments"_s, arguments}}}})
                               .toJson(QJsonDocument::Compact)
                       + '\n';
            };
            mcp.write(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"
                      "\n"
                      + call(2, u"brulion_app_load"_s,
                             {{u"id"_s, u"zywy"_s}, {u"file"_s, m_tmp.filePath(u"żywy.luau"_s)}})
                      + call(3, u"brulion_app_eval"_s, {{u"code"_s, u"return 2"_s}}));
            mcp.closeWriteChannel();
            QElapsedTimer t;
            t.start();
            while (mcp.state() != QProcess::NotRunning && t.elapsed() < kTimeoutMs)
                QTest::qWait(20);
            const QList<QByteArray> lines = mcp.readAllStandardOutput().trimmed().split('\n');
            QCOMPARE(lines.size(), 3);
            const QJsonObject load = QJsonDocument::fromJson(lines.at(1)).object().value(u"result"_s).toObject();
            QVERIFY(load.value(u"isError"_s).toBool());
            QCOMPARE(load.value(u"structuredContent"_s)
                             .toObject()
                             .value(u"error"_s)
                             .toObject()
                             .value(u"code"_s)
                             .toString(),
                     u"bad_arguments"_s);
            const QJsonObject eval = QJsonDocument::fromJson(lines.at(2)).object().value(u"result"_s).toObject();
            QVERIFY(!eval.value(u"isError"_s).toBool());
            QVERIFY(asked(u"eval"_s, {{u"code"_s, u"return 2"_s}}));
        }

        // Only grants (the remote control's setting off, an agent the app started): without a token
        // as if it were off (the hint), `app open` by message; a grant's token in the environment is
        // shown in a hello first and makes the request its client's; a wrong one is refused.
        control.setOpen(false);
        const QString token = control.addGrant(u"Claude Code"_s);
        r = cliLive({u"app"_s, u"state"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"remote_control_off"_s);
        QVERIFY(r.json.value(u"error"_s)
                        .toObject()
                        .value(u"details"_s)
                        .toObject()
                        .value(u"hint"_s)
                        .toString()
                        .contains(u"--remote-control"_s));
        r = cliLive({u"app"_s, u"open"_s, u"today"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"via"_s).toString(), u"message"_s);
        QProcessEnvironment agentEnv = environment();
        agentEnv.insert(u"BRULION_APP_TOKEN"_s, token);
        agentEnv.insert(u"BRULION_APP_CLIENT"_s, u"inna nazwa"_s);
        r = cliLive({u"app"_s, u"eval"_s, u"return 1"_s}, {}, agentEnv);
        QCOMPARE(r.code, 0);
        QCOMPARE(requests.last().method, u"eval"_s);
        QCOMPARE(requests.last().client, u"Claude Code"_s);
        QVERIFY(requests.last().granted);
        QCOMPARE(requests.at(requests.size() - 2).method, u"hello"_s);
        agentEnv.insert(u"BRULION_APP_TOKEN"_s, QString(64, u'0'));
        r = cliLive({u"app"_s, u"state"_s}, {}, agentEnv);
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"unauthorized"_s);
        QVERIFY(control.revokeGrant(token));
        control.setOpen(true);
    }

    void appThroughATunnel()
    {
        const QString dir = vault({{u"Dom.md"_s, "- a\n"}});
        // An app on this desktop too: nothing of the tunnel's goes to it.
        const QString key = brulion::ipc::SingleInstance::keyFor(appDataDir());
        brulion::ipc::SingleInstance desktop(key);
        QVERIFY(desktop.listen());
        QSignalSpy messages(&desktop, &brulion::ipc::SingleInstance::messageReceived);
        brulion::ipc::ControlServer desktopControl;
        int desktopRequests = 0;
        desktopControl.setHandler(
                [&desktopRequests](const brulion::ipc::ControlRequest &, brulion::ipc::ControlReply reply) {
                    ++desktopRequests;
                    reply.ok(QJsonObject());
                });
        QVERIFY(desktopControl.listen(brulion::ipc::controlKeyFor(key)));
        // The tunnel of acp serve, a stand-in: only its grants.
        brulion::ipc::ControlServer tunnel;
        tunnel.setOpen(false);
        QList<brulion::ipc::ControlRequest> requests;
        tunnel.setHandler([&requests](const brulion::ipc::ControlRequest &request, brulion::ipc::ControlReply reply) {
            requests.append(request);
            if (request.method == u"hello")
                return reply.ok(QJsonObject{{u"granted"_s, true}, {u"device"_s, u"Pixel 9"_s}});
            if (request.method == u"state")
                return reply.ok(
                        QJsonObject{{u"ready"_s, true}, {u"vault"_s, QJsonObject{{u"path"_s, u"/data/Notatki"_s}}}});
            reply.ok(QJsonObject{{u"method"_s, request.method}, {u"params"_s, request.params}});
        });
        const QString tunnelKey = m_home + u"/run/brulion-acp-test"_s;
        QVERIFY(tunnel.listen(tunnelKey));
        const QString token = tunnel.addGrant(u"Claude Code"_s);
        QProcessEnvironment env = environment();
        env.insert(u"BRULION_APP_SOCKET"_s, tunnelKey);
        env.insert(u"BRULION_APP_TOKEN"_s, token);
        env.insert(u"BRULION_APP_CLIENT"_s, u"Claude Code"_s);
        env.insert(u"BRULION_APP_DEVICE"_s, u"Pixel 9"_s);

        Run r = cliLive({u"app"_s, u"state"_s}, {}, env);
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"vault"_s).toObject().value(u"path"_s).toString(), u"/data/Notatki"_s);
        QVERIFY(requests.at(requests.size() - 2).method == u"hello");
        QCOMPARE(requests.last().method, u"state"_s);
        QVERIFY(requests.last().granted);
        QCOMPARE(requests.last().client, u"Claude Code"_s);
        // Today and a page: opened by the app on the device; no notebook check against its copy.
        r = cliLive({u"app"_s, u"today"_s}, {}, env);
        QCOMPARE(r.code, 0);
        QCOMPARE(requests.last().method, u"open"_s);
        QCOMPARE(requests.last().params.value(u"ref"_s).toString(), u"today"_s);
        const qsizetype before = requests.size();
        r = cliLive({u"--vault"_s, dir, u"app"_s, u"open"_s, u"Dom"_s}, {}, env);
        QCOMPARE(r.code, 0);
        QStringList asked;
        for (qsizetype i = before; i < requests.size(); ++i)
            asked << requests.at(i).method;
        QCOMPARE(asked, (QStringList{u"hello"_s, u"open"_s})); // (no state: its notebook is a copy elsewhere)
        QCOMPARE(requests.last().params.value(u"ref"_s).toString(), u"Dom"_s);
        // The quick capture's window is for a person: on another device, the text is needed.
        r = cliLive({u"app"_s, u"capture"_s}, {}, env);
        QCOMPARE(r.code, 2);
        QVERIFY2(r.json.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"Pixel 9"_s),
                 qPrintable(r.describe()));
        r = cliLive({u"app"_s, u"capture"_s, u"Kupić mleko jutro o 9"_s}, {}, env);
        QCOMPARE(r.code, 0);
        QCOMPARE(requests.last().method, u"capture"_s);
        // A secret asked for on the device: only its value can come back (a file would be written
        // there); a file asked for is refused before anything is sent.
        r = cliLive({u"app"_s, u"chat"_s, u"request-secret"_s, u"Token GitHuba"_s}, {}, env);
        QCOMPARE(r.code, 0);
        QCOMPARE(requests.last().method, u"chatRequestSecret"_s);
        QCOMPARE(requests.last().params.value(u"delivery"_s).toString(), u"value"_s);
        const qsizetype beforeFile = requests.size();
        r = cliLive({u"app"_s, u"chat"_s, u"request-secret"_s, u"PIN"_s, u"--delivery"_s, u"file"_s}, {}, env);
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"no_file_here"_s);
        QVERIFY(r.json.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"Pixel 9"_s));
        QCOMPARE(requests.size(), beforeFile);
        // Nothing reached the app of this desktop.
        QTest::qWait(100);
        QCOMPARE(messages.size(), 0);
        QCOMPARE(desktopRequests, 0);
        // The MCP server's instructions name the device.
        {
            QProcess p;
            p.setProcessEnvironment(env);
            p.start(QStringLiteral(BRULION_CLI), {u"--vault"_s, dir, u"mcp"_s, u"--app-control"_s});
            QVERIFY(p.waitForStarted(kTimeoutMs));
            p.write(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})"
                    "\n");
            QElapsedTimer t;
            t.start();
            while (!p.canReadLine() && t.elapsed() < kTimeoutMs)
                p.waitForReadyRead(100);
            const QString instructions = QJsonDocument::fromJson(p.readLine())
                                                 .object()
                                                 .value(u"result"_s)
                                                 .toObject()
                                                 .value(u"instructions"_s)
                                                 .toString();
            QVERIFY2(instructions.contains(u"device \"Pixel 9\""_s), qPrintable(instructions));
            QVERIFY(instructions.contains(u"acp serve"_s));
            QVERIFY(!instructions.contains(u"running on this desktop"_s));
            p.closeWriteChannel();
            QVERIFY(p.waitForFinished(kTimeoutMs));
        }
        // The tunnel gone (acp serve stopped): said so, nothing sent to this desktop's app.
        tunnel.close();
        r = cliLive({u"app"_s, u"state"_s}, {}, env);
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"app_not_reached"_s);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"details"_s).toObject().value(u"reason"_s).toString(),
                 u"no_tunnel"_s);
        QVERIFY(r.json.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"Pixel 9"_s));
        QCOMPARE(desktopRequests, 0);
    }

    // `app chat …` against a stand-in remote control: what each command asks (start with --wait on one
    // connection, the schedules' options), the exit codes of a wait (an answer 0, a failure 1, a
    // question for the user 5, still running 3, busy 4), --read-only and --dry-run, an agent's chat
    // grant (BRULION_APP_CHAT_TOKEN) shown for the chat before its full grant, and the MCP tools of
    // `mcp --app-chat` (the chat's alone; read-only: those that only look).
    void appChat()
    {
        const QString key = brulion::ipc::SingleInstance::keyFor(appDataDir());
        brulion::ipc::SingleInstance app(key);
        QVERIFY(app.listen());
        brulion::ipc::ControlServer control;
        QList<brulion::ipc::ControlRequest> requests;
        control.setHandler([&requests](const brulion::ipc::ControlRequest &request, brulion::ipc::ControlReply reply) {
            requests.append(request);
            const QString path = request.params.value(u"path"_s).toString();
            if (request.method == u"chatStart") {
                reply.ok(QJsonObject{{u"path"_s, u"AI/2026-11-22 Zapytaj Codexa.md"_s}, {u"state"_s, u"running"_s}});
            } else if (request.method == u"chatWait" && path.contains(u"błąd"_s)) {
                reply.ok(QJsonObject{{u"path"_s, path}, {u"state"_s, u"failed"_s}, {u"failure"_s, u"Agent padł"_s}});
            } else if (request.method == u"chatWait" && path.contains(u"pytanie"_s)) {
                reply.ok(QJsonObject{{u"path"_s, path},
                                     {u"state"_s, u"asking"_s},
                                     {u"question"_s, QJsonObject{{u"text"_s, u"Kodeks asks: Write Zgoda.md"_s},
                                                                {u"options"_s, QJsonArray{u"Allow"_s, u"Reject"_s}}}}});
            } else if (request.method == u"chatWait" && path.contains(u"długo"_s)) {
                reply.fail(u"timeout"_s, u"Its turn still runs"_s, {{u"path"_s, path}, {u"state"_s, u"running"_s}});
            } else if (request.method == u"chatWait") {
                reply.ok(QJsonObject{{u"path"_s, path},
                                     {u"state"_s, u"idle"_s},
                                     {u"answer"_s, QJsonObject{{u"position"_s, 1}, {u"text"_s, u"Plan jest dobry."_s}}}});
            } else if (request.method == u"chatSend" && path.contains(u"zajęta"_s)) {
                reply.fail(u"busy"_s, u"Its turn runs"_s, {{u"state"_s, u"running"_s}});
            } else if (request.method == u"chatRequestSecret"
                       && request.params.value(u"label"_s).toString().contains(u"odmowa"_s)) {
                reply.fail(u"refused"_s, u"The user did not give it"_s, {{u"reason"_s, u"cancelled"_s}});
            } else if (request.method == u"chatRequestSecret") {
                reply.ok(QJsonObject{{u"delivery"_s, u"file"_s},
                                     {u"file"_s, u"/run/user/1000/brulion-x-secrets/run-1/secret-2"_s},
                                     {u"params"_s, request.params}});
            } else {
                reply.ok(QJsonObject{{u"method"_s, request.method},
                                     {u"params"_s, request.params},
                                     {u"granted"_s, request.granted},
                                     {u"chatOnly"_s, request.chatOnly}});
            }
        });
        QVERIFY(control.listen(brulion::ipc::controlKeyFor(key)));

        // Start and wait on one connection.
        Run r = cliLive({u"app"_s, u"chat"_s, u"start"_s, u"Druga"_s, u"opinia,"_s, u"proszę"_s, u"--partner"_s,
                         u"acp:codex"_s, u"--page"_s, u"Dom"_s, u"--wait"_s, u"--timeout"_s, u"5000"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"state"_s).toString(), u"idle"_s);
        QCOMPARE(r.result().value(u"answer"_s).toObject().value(u"text"_s).toString(), u"Plan jest dobry."_s);
        QCOMPARE(requests.size(), 2);
        QCOMPARE(requests.at(0).method, u"chatStart"_s);
        QCOMPARE(requests.at(0).params, (QJsonObject{{u"text"_s, u"Druga opinia, proszę"_s},
                                                     {u"partner"_s, u"acp:codex"_s},
                                                     {u"page"_s, u"Dom"_s}}));
        QCOMPARE(requests.at(1).method, u"chatWait"_s);
        QCOMPARE(requests.at(1).params,
                 (QJsonObject{{u"path"_s, u"AI/2026-11-22 Zapytaj Codexa.md"_s}, {u"ms"_s, 5000}}));
        // A wait's ends as exit codes.
        r = cliLive({u"app"_s, u"chat"_s, u"wait"_s, u"AI/błąd"_s});
        QCOMPARE(r.code, 1);
        QCOMPARE(r.errorCode(), u"turn_failed"_s);
        QVERIFY(r.json.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"Agent padł"_s));
        QCOMPARE(requests.last().params.value(u"ms"_s).toInt(), 60000);
        r = cliLive({u"app"_s, u"chat"_s, u"wait"_s, u"pytanie"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"waiting_for_user"_s);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"details"_s).toObject().value(u"question"_s).toObject().value(
                         u"options"_s),
                 QJsonValue(QJsonArray{u"Allow"_s, u"Reject"_s}));
        r = cliLive({u"app"_s, u"chat"_s, u"wait"_s, u"długo"_s, u"--timeout"_s, u"100"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"timeout"_s);
        r = cliLive({u"app"_s, u"chat"_s, u"send"_s, u"zajęta"_s, u"I"_s, u"jeszcze"_s});
        QCOMPARE(r.code, 4);
        QCOMPARE(r.errorCode(), u"busy"_s);
        // --wake: no wait, the end of its turn wakes the parent conversation.
        r = cliLive({u"app"_s, u"chat"_s, u"start"_s, u"Podsumuj"_s, u"źródła"_s, u"--parent"_s, u"AI/Plan"_s,
                     u"--wake"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(requests.last().method, u"chatStart"_s);
        QCOMPARE(requests.last().params,
                 (QJsonObject{{u"text"_s, u"Podsumuj źródła"_s}, {u"parent"_s, u"AI/Plan"_s}, {u"wake"_s, true}}));
        r = cliLive({u"app"_s, u"chat"_s, u"send"_s, u"AI/Plan"_s, u"I"_s, u"żółwie"_s, u"--queue"_s, u"--wake"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(requests.last().method, u"chatSend"_s);
        QCOMPARE(
                requests.last().params,
                (QJsonObject{
                        {u"path"_s, u"AI/Plan"_s}, {u"text"_s, u"I żółwie"_s}, {u"queue"_s, true}, {u"wake"_s, true}}));
        // What the others ask.
        r = cliLive({u"app"_s, u"chat"_s, u"list"_s, u"--parent"_s, u"AI/Plan"_s, u"--limit"_s, u"7"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(requests.last().method, u"chats"_s);
        QCOMPARE(requests.last().params, (QJsonObject{{u"parent"_s, u"AI/Plan"_s}, {u"limit"_s, 7}}));
        r = cliLive({u"app"_s, u"chat"_s, u"read"_s, u"Plan wyjazdu"_s, u"--from"_s, u"2"_s, u"--offset"_s, u"300"_s});
        QCOMPARE(requests.last().params,
                 (QJsonObject{{u"path"_s, u"Plan wyjazdu"_s}, {u"from"_s, 2}, {u"offset"_s, 300}}));
        r = cliLive({u"app"_s, u"chat"_s, u"stop"_s, u"AI/Plan"_s});
        QCOMPARE(requests.last().method, u"chatStop"_s);
        r = cliLive({u"app"_s, u"chat"_s, u"partners"_s});
        QCOMPARE(requests.last().method, u"chatPartners"_s);
        r = cliLive({u"app"_s, u"chat"_s, u"schedule"_s, u"add"_s, u"Co"_s, u"mam"_s, u"dziś?"_s, u"--when"_s,
                     u"codziennie o 7:30"_s, u"--title"_s, u"Plan dnia"_s, u"--continue"_s, u"--partner"_s, u"acp:claude"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(requests.last().method, u"chatScheduleAdd"_s);
        QCOMPARE(requests.last().params, (QJsonObject{{u"prompt"_s, u"Co mam dziś?"_s},
                                                      {u"when"_s, u"codziennie o 7:30"_s},
                                                      {u"title"_s, u"Plan dnia"_s},
                                                      {u"partner"_s, u"acp:claude"_s},
                                                      {u"destination"_s, u"continue"_s}}));
        r = cliLive({u"app"_s, u"chat"_s, u"schedule"_s, u"add"_s, u"Bez reguły"_s});
        QCOMPARE(r.code, 2); // (--when is needed)
        r = cliLive({u"app"_s, u"chat"_s, u"schedule"_s, u"set"_s, u"s1"_s, u"--disable"_s, u"--destination"_s, u"new"_s});
        QCOMPARE(requests.last().params,
                 (QJsonObject{{u"id"_s, u"s1"_s}, {u"destination"_s, u"new"_s}, {u"enabled"_s, false}}));
        for (const QString &verb : {u"remove"_s, u"run"_s}) {
            r = cliLive({u"app"_s, u"chat"_s, u"schedule"_s, verb, u"s1"_s});
            QCOMPARE(r.code, 0);
            QCOMPARE(requests.last().params, (QJsonObject{{u"id"_s, u"s1"_s}}));
        }
        r = cliLive({u"app"_s, u"chat"_s, u"schedule"_s, u"list"_s});
        QCOMPARE(requests.last().method, u"chatSchedules"_s);
        // A notification through the app: its text, title, the page a click opens, a script's conversation.
        r = cliLive({u"app"_s, u"chat"_s, u"notify"_s, u"Kopia"_s, u"gotowa:"_s, u"żółw"_s, u"--title"_s, u"Kopie"_s,
                     u"--page"_s, u"Dom"_s, u"--conversation"_s, u"AI/Plan"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(requests.last().method, u"chatNotify"_s);
        QCOMPARE(requests.last().params, (QJsonObject{{u"text"_s, u"Kopia gotowa: żółw"_s},
                                                      {u"title"_s, u"Kopie"_s},
                                                      {u"page"_s, u"Dom"_s},
                                                      {u"path"_s, u"AI/Plan"_s}}));
        // A secret asked for in the app's private card: what is asked, the file that comes back, the
        // user's refusal (exit 7); the call waits as long as asked and a margin.
        r = cliLive({u"app"_s, u"chat"_s, u"request-secret"_s, u"Token GitHuba"_s, u"--reason"_s,
                     u"Żeby wypchnąć gałąź"_s, u"--placeholder"_s, u"ghp_…"_s, u"--url"_s,
                     u"https://github.com/settings/tokens"_s, u"--timeout"_s, u"20000"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"delivery"_s).toString(), u"file"_s);
        QCOMPARE(requests.last().method, u"chatRequestSecret"_s);
        QCOMPARE(requests.last().params, (QJsonObject{{u"label"_s, u"Token GitHuba"_s},
                                                      {u"ms"_s, 20000},
                                                      {u"reason"_s, u"Żeby wypchnąć gałąź"_s},
                                                      {u"placeholder"_s, u"ghp_…"_s},
                                                      {u"url"_s, u"https://github.com/settings/tokens"_s}}));
        r = cliLive({u"app"_s, u"chat"_s, u"request-secret"_s, u"PIN"_s, u"--delivery"_s, u"value"_s,
                     u"--conversation"_s, u"AI/Plan"_s});
        QCOMPARE(requests.last().params, (QJsonObject{{u"label"_s, u"PIN"_s},
                                                      {u"ms"_s, 300000},
                                                      {u"delivery"_s, u"value"_s},
                                                      {u"path"_s, u"AI/Plan"_s}}));
        r = cliLive({u"app"_s, u"chat"_s, u"request-secret"_s, u"odmowa"_s});
        QCOMPARE(r.code, 7);
        QCOMPARE(r.errorCode(), u"refused"_s);
        r = cliLive({u"app"_s, u"chat"_s, u"request-secret"_s, u"x"_s, u"--delivery"_s, u"mail"_s});
        QCOMPARE(r.code, 2);
        // --read-only refuses what writes a conversation; --dry-run says what it would ask.
        const qsizetype asked = requests.size();
        r = cliLive({u"--read-only"_s, u"app"_s, u"chat"_s, u"start"_s, u"Ą"_s});
        QCOMPARE(r.code, 7);
        r = cliLive({u"--read-only"_s, u"app"_s, u"chat"_s, u"schedule"_s, u"run"_s, u"s1"_s});
        QCOMPARE(r.code, 7);
        r = cliLive({u"--dry-run"_s, u"app"_s, u"chat"_s, u"start"_s, u"Ą"_s, u"--wait"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"method"_s).toString(), u"chatStart"_s);
        QVERIFY(r.result().value(u"wait"_s).toBool());
        QCOMPARE(requests.size(), asked);
        r = cliLive({u"--read-only"_s, u"app"_s, u"chat"_s, u"list"_s});
        QCOMPARE(r.code, 0);

        // An agent's chat grant goes first for the chat, its full one for the rest.
        control.setOpen(false);
        const QString chatToken = control.addGrant(u"Claude Code"_s, brulion::ipc::GrantScope::Chat);
        const QString fullToken = control.addGrant(u"Claude Code"_s);
        QProcessEnvironment agentEnv = environment();
        agentEnv.insert(u"BRULION_APP_CHAT_TOKEN"_s, chatToken);
        r = cliLive({u"app"_s, u"chat"_s, u"partners"_s}, {}, agentEnv);
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"granted"_s).toBool());
        QVERIFY(r.result().value(u"chatOnly"_s).toBool());
        r = cliLive({u"app"_s, u"state"_s}, {}, agentEnv); // (a chat grant is no grant for the rest)
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"remote_control_off"_s);
        agentEnv.insert(u"BRULION_APP_TOKEN"_s, fullToken);
        r = cliLive({u"app"_s, u"chat"_s, u"list"_s}, {}, agentEnv);
        QVERIFY(r.result().value(u"chatOnly"_s).toBool());
        r = cliLive({u"app"_s, u"state"_s}, {}, agentEnv);
        QCOMPARE(r.code, 0);
        QVERIFY(!r.result().value(u"chatOnly"_s).toBool());
        QVERIFY(control.revokeGrant(chatToken));
        QVERIFY(control.revokeGrant(fullToken));
        control.setOpen(true);

        // MCP: --app-chat offers the chat's tools alone.
        const QString dir = vault({{u"Dom.md"_s, "- żółw\n"}});
        for (const bool readOnly : {false, true}) {
            QProcess mcp;
            mcp.setProcessEnvironment(environment());
            QStringList args{u"--vault"_s, dir, u"mcp"_s, u"--app-chat"_s};
            if (readOnly)
                args.append(u"--read-only"_s);
            mcp.start(QStringLiteral(BRULION_CLI), args);
            QVERIFY(mcp.waitForStarted(kTimeoutMs));
            mcp.write(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"
                      "\n"
                      R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"
                      "\n");
            mcp.closeWriteChannel();
            QVERIFY(mcp.waitForFinished(kTimeoutMs));
            const QList<QByteArray> lines = mcp.readAllStandardOutput().trimmed().split('\n');
            QCOMPARE(lines.size(), 2);
            QVERIFY(QJsonDocument::fromJson(lines.first())
                            .object()
                            .value(u"result"_s)
                            .toObject()
                            .value(u"instructions"_s)
                            .toString()
                            .contains(u"brulion_app_chat_start"_s));
            QVERIFY(QJsonDocument::fromJson(lines.at(0))
                            .object()
                            .value(u"result"_s)
                            .toObject()
                            .value(u"instructions"_s)
                            .toString()
                            .contains(u"brulion_app_chat_notify"_s));
            QStringList tools;
            QJsonObject startProperties;
            for (const QJsonValue &t : QJsonDocument::fromJson(lines.last())
                                               .object()
                                               .value(u"result"_s)
                                               .toObject()
                                               .value(u"tools"_s)
                                               .toArray()) {
                tools.append(t.toObject().value(u"name"_s).toString());
                if (tools.last() == u"brulion_app_chat_start")
                    startProperties = t.toObject().value(u"inputSchema"_s).toObject().value(u"properties"_s).toObject();
            }
            // (Its wake: the parent woken instead of a wait.)
            QCOMPARE(startProperties.contains(u"wake"_s), !readOnly);
            for (const char *tool : {"brulion_app_chat_list", "brulion_app_chat_read", "brulion_app_chat_wait",
                                     "brulion_app_chat_partners", "brulion_app_chat_schedule_list",
                                     "brulion_app_chat_request_secret", "brulion_app_chat_notify"})
                QVERIFY2(tools.contains(QLatin1String(tool)), tool);
            // (The webhooks' server is no tool.)
            QVERIFY(!tools.contains(u"brulion_app_hooks_serve"_s));
            for (const char *tool : {"brulion_app_chat_start", "brulion_app_chat_send", "brulion_app_chat_stop",
                                     "brulion_app_chat_schedule_add", "brulion_app_chat_schedule_run"})
                QVERIFY2(tools.contains(QLatin1String(tool)) == !readOnly, tool);
            for (const char *tool : {"brulion_app_state", "brulion_app_click", "brulion_app_eval"})
                QVERIFY2(!tools.contains(QLatin1String(tool)), tool);
            QVERIFY(tools.contains(u"brulion_search"_s));
        }
    }

    // `app hooks serve` (docs/CLI.md "Webhooks") as the real program against a stand-in remote control
    // that answers chatHook: refused without the app's grant and with a token that is none; its first
    // line; a hook's POST passed on with its token and its body as text (JSON indented, a form decoded)
    // — 202 with the conversation's name —, an unknown token 404, one too soon 429 with Retry-After, GET
    // 405, a body not UTF-8 or of another charset 400, over 1 MiB 413, other paths 404, the root 200;
    // only hello and chatHook asked; no token in its log; it ends when its standard input does.
    void appHooks()
    {
        Run r = cli({u"app"_s, u"hooks"_s, u"serve"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"no_grant"_s);
        const QString key = brulion::ipc::SingleInstance::keyFor(appDataDir());
        brulion::ipc::SingleInstance app(key);
        QVERIFY(app.listen());
        brulion::ipc::ControlServer control;
        const QString known = u"Kk0_-Kk0_-Kk0_-Kk0_-Kk0_-Kk0_-Kk0_-Kk0_-Kk0"_s; // (43 characters: a token)
        const QString soon = QString(43, u'S');
        QList<brulion::ipc::ControlRequest> requests;
        control.setHandler([&](const brulion::ipc::ControlRequest &request, brulion::ipc::ControlReply reply) {
            requests.append(request);
            if (request.method == u"hello")
                return reply.ok(QJsonObject{{u"granted"_s, request.granted}});
            const QString token = request.params.value(u"token"_s).toString();
            if (token == known)
                reply.ok(QJsonObject{{u"status"_s, u"started"_s}, {u"conversation"_s, u"Dzwonek do drzwi"_s}});
            else if (token == soon)
                reply.ok(QJsonObject{{u"status"_s, u"busy"_s}, {u"retryAfterMs"_s, 42000}});
            else
                reply.ok(QJsonObject{{u"status"_s, u"unknown"_s}});
        });
        QVERIFY(control.listen(brulion::ipc::controlKeyFor(key)));
        control.setOpen(false);
        const QString grant = control.addGrant(u"webhooks"_s, brulion::ipc::GrantScope::Hook);
        QProcessEnvironment env = environment();
        env.insert(u"BRULION_APP_HOOK_TOKEN"_s, u"nie-grant"_s);
        r = cliLive({u"app"_s, u"hooks"_s, u"serve"_s, u"--port"_s, u"0"_s}, {}, env);
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"app_not_reached"_s);
        r = cliLive({u"app"_s, u"hooks"_s, u"serve"_s, u"--listen"_s, u"nie-adres"_s}, {}, env);
        QCOMPARE(r.code, 2);

        env.insert(u"BRULION_APP_HOOK_TOKEN"_s, grant);
        QProcess server;
        server.setProcessEnvironment(env);
        server.start(QStringLiteral(BRULION_CLI),
                                    {u"app"_s, u"hooks"_s, u"serve"_s, u"--port"_s, u"0"_s, u"--stop-on-eof"_s});
        QVERIFY(server.waitForStarted(kTimeoutMs));
        QVERIFY(QTest::qWaitFor([&server] { return server.canReadLine() || server.state() == QProcess::NotRunning; },
                                kTimeoutMs));
        const QJsonObject started = QJsonDocument::fromJson(server.readLine()).object();
        QVERIFY2(started.value(u"ok"_s).toBool(), server.readAllStandardError().constData());
        const QString base = started.value(u"result"_s).toObject().value(u"url"_s).toString();
        QVERIFY2(base.startsWith(u"http://127.0.0.1:"_s), qPrintable(base));

        QNetworkAccessManager nam;
        nam.setProxy(QNetworkProxy::NoProxy);
        struct Answer {
            int status = 0;
            QJsonObject json;
            QByteArray retryAfter;
            QByteArray allow;
        };
        const auto send = [&](const QByteArray &method, const QString &path, const QByteArray &body,
                              const QByteArray &type) {
            QNetworkRequest request(QUrl(base + path));
            if (!type.isEmpty())
                request.setRawHeader("Content-Type"_ba, type);
            QNetworkReply *reply = nam.sendCustomRequest(request, method, body);
            Answer a;
            if (QTest::qWaitFor([reply] { return reply->isFinished(); }, kTimeoutMs)) {
                a.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                a.json = QJsonDocument::fromJson(reply->readAll()).object();
                a.retryAfter = reply->rawHeader("Retry-After"_ba);
                a.allow = reply->rawHeader("Allow"_ba);
            }
            delete reply;
            return a;
        };
        Answer a = send("POST"_ba, u"/hooks/"_s + known, R"({"kto":"listonosz","pokój":"sień"})", "application/json");
        QCOMPARE(a.status, 202);
        QCOMPARE(a.json, (QJsonObject{{u"conversation"_s, u"Dzwonek do drzwi"_s}}));
        QCOMPARE(requests.last().method, u"chatHook"_s);
        QVERIFY(requests.last().hookOnly);
        QCOMPARE(requests.last().params.value(u"token"_s).toString(), known);
        QCOMPARE(requests.last().params.value(u"kind"_s).toString(), u"json"_s);
        QVERIFY(requests.last().params.value(u"text"_s).toString().contains(u"\"pokój\": \"sień\""_s));
        // Behind a proxy's prefix, a form decoded.
        a = send("POST"_ba, u"/brulion/hooks/"_s + known, "temat=Przesy%C5%82ka&od=DHL",
                 "application/x-www-form-urlencoded");
        QCOMPARE(a.status, 202);
        QCOMPARE(requests.last().params.value(u"text"_s).toString(), u"temat: Przesyłka\nod: DHL"_s);
        QCOMPARE(requests.last().params.value(u"kind"_s).toString(), u"form"_s);
        // Empty: still a run.
        a = send("POST"_ba, u"/hooks/"_s + known, {}, {});
        QCOMPARE(a.status, 202);
        QCOMPARE(requests.last().params.value(u"kind"_s).toString(), u"empty"_s);
        // (Each request reaches the app over a connection of its own: hello, then chatHook.)
        const auto hooks = [&requests] {
            return std::count_if(requests.cbegin(), requests.cend(),
                                 [](const brulion::ipc::ControlRequest &r) { return r.method == u"chatHook"; });
        };
        const qsizetype asked = hooks();
        a = send("POST"_ba, u"/hooks/"_s + QString(43, u'x'), "{}", "application/json");
        QCOMPARE(a.status, 404);
        QCOMPARE(a.json.value(u"error"_s).toString(), u"not_found"_s);
        a = send("POST"_ba, u"/hooks/"_s + soon, "ok", "text/plain");
        QCOMPARE(a.status, 429);
        QCOMPARE(a.retryAfter, "42"_ba);
        QCOMPARE(hooks(), asked + 2);
        // Refused before the app is asked.
        a = send("GET"_ba, u"/hooks/"_s + known, {}, {});
        QCOMPARE(a.status, 405);
        QCOMPARE(a.allow, "POST"_ba);
        a = send("POST"_ba, u"/hooks/"_s + known, QByteArray("Za\xbf\xf3\xb3\xe6"), "text/plain");
        QCOMPARE(a.status, 400);
        QCOMPARE(a.json.value(u"error"_s).toString(), u"not_utf8"_s);
        a = send("POST"_ba, u"/hooks/"_s + known, "ok", "text/plain; charset=iso-8859-2");
        QCOMPARE(a.status, 400);
        {
            // (Over 1 MiB: refused after the header fields, the body never read.)
            QTcpSocket raw;
            raw.connectToHost(QHostAddress::LocalHost, quint16(QUrl(base).port()));
            QVERIFY(raw.waitForConnected(kTimeoutMs));
            raw.write("POST /hooks/" + known.toLatin1()
                      + " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: text/plain\r\n"
                        "Content-Length: 2000000\r\n\r\n");
            QVERIFY(QTest::qWaitFor(
                    [&raw] { return raw.bytesAvailable() > 12 || raw.state() != QAbstractSocket::ConnectedState; },
                    kTimeoutMs));
            const QByteArray answer = raw.readAll();
            QVERIFY2(answer.startsWith("HTTP/1.1 413 "), answer.constData());
        }
        a = send("POST"_ba, u"/hooks/krótki"_s, "{}", "application/json");
        QCOMPARE(a.status, 404);
        a = send("POST"_ba, u"/mcp"_s, "{}", "application/json");
        QCOMPARE(a.status, 404);
        QCOMPARE(hooks(), asked + 2);
        a = send("GET"_ba, u"/"_s, {}, {});
        QCOMPARE(a.status, 200);
        for (const brulion::ipc::ControlRequest &request : std::as_const(requests))
            QVERIFY2(request.method == u"hello" || request.method == u"chatHook", qPrintable(request.method));
        // It ends with its standard input; its log names every request, never a token.
        server.closeWriteChannel();
        QVERIFY(QTest::qWaitFor([&server] { return server.state() == QProcess::NotRunning; }, kTimeoutMs));
        const QByteArray log = server.readAllStandardError();
        QVERIFY2(log.contains("/hooks/Kk0_…"), log.constData());
        QVERIFY(!log.contains(known.toLatin1()));
        QVERIFY(!log.contains(soon.toLatin1()));
        QVERIFY(!log.contains(grant.toLatin1()));
        QVERIFY(control.revokeGrant(grant));
    }

    // `mcp serve` as the real program (the in-process flow: tst_cli_remote_mcp): the password
    // commands, the server's first line, its metadata, a whole sign-in with the password of the
    // state file, a tool call over HTTP, `mcp clients` and `mcp revoke` taking effect in the
    // running server, the sign-in page in Polish, and the refusals at start.
    void remoteMcpServe()
    {
        const QString dir = vault({{u"Ogród.md"_s, "- żółw błotny\n"}});
        const QString password = u"Hasło do notatek 1"_s;
        Run r = run(dir, {u"--stdin"_s, u"mcp"_s, u"password"_s}, "krótkie\n");
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"password_too_short"_s);
        r = run(dir, {u"mcp"_s, u"password"_s}); // standard input is no terminal here
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"password_needs_stdin"_s);
        r = run(dir, {u"--stdin"_s, u"mcp"_s, u"password"_s, u"--print-hash"_s}, password.toUtf8() + '\n');
        QCOMPARE(r.code, 0);
        const QString printedHash = r.result().value(u"hash"_s).toString();
        QVERIFY(printedHash.startsWith(u"pbkdf2-sha256$600000$"_s));
        // No password yet: the server does not start.
        r = run(dir, {u"mcp"_s, u"serve"_s, u"--port"_s, u"0"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"cannot_serve"_s);
        r = run(dir, {u"--stdin"_s, u"mcp"_s, u"password"_s}, password.toUtf8() + '\n');
        QCOMPARE(r.code, 0);
        const QString authFile = r.result().value(u"authFile"_s).toString();
        QVERIFY(authFile.startsWith(stateDirOf(dir)));
        QFile state(authFile);
        QVERIFY(state.open(QIODevice::ReadOnly));
        QVERIFY(!state.readAll().contains(password.toUtf8()));
        state.close();
#ifndef Q_OS_WIN
        QCOMPARE(QFile::permissions(authFile) & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());
#endif
        r = run(dir, {u"mcp"_s, u"clients"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"passwordSet"_s).toBool());
        QVERIFY(r.result().value(u"connections"_s).toArray().isEmpty());
        r = run(dir, {u"mcp"_s, u"serve"_s, u"--public-url"_s, u"http://notes.example.com"_s});
        QCOMPARE(r.code, 5);
        r = run(dir, {u"mcp"_s, u"serve"_s, u"--listen"_s, u"nie-adres"_s});
        QCOMPARE(r.code, 2);
        r = run(dir, {u"mcp"_s, u"serve"_s, u"--port"_s, u"70000"_s});
        QCOMPARE(r.code, 2);

        QProcess server;
        const auto stopServer = qScopeGuard([&server] {
            server.kill();
            server.waitForFinished();
        });
        server.setProcessEnvironment(environment());
        server.start(QStringLiteral(BRULION_CLI), {u"--vault"_s, dir, u"--now"_s, kNow, u"mcp"_s, u"serve"_s,
                                                   u"--port"_s, u"0"_s});
        QVERIFY(server.waitForStarted(kTimeoutMs));
        QVERIFY(QTest::qWaitFor([&] { return server.canReadLine() || server.state() != QProcess::Running; },
                                kTimeoutMs));
        const QJsonObject started = QJsonDocument::fromJson(server.readLine()).object();
        QByteArray log = server.readAllStandardError(); // the server's log
        QVERIFY2(started.value(u"ok"_s).toBool(), log.constData());
        const QString endpoint = started.value(u"result"_s).toObject().value(u"endpoint"_s).toString();
        const QString base = endpoint.chopped(4);
        QVERIFY(endpoint.startsWith(u"http://127.0.0.1:"_s));
        QCOMPARE(started.value(u"result"_s).toObject().value(u"authFile"_s).toString(), authFile);

        QNetworkAccessManager nam;
        struct Answer {
            int status = 0;
            QByteArray body;
            QByteArray location;
            QJsonObject json;
        };
        const auto send = [&](const QByteArray &method, const QString &path, const QByteArray &body,
                              const QList<std::pair<QByteArray, QByteArray>> &headers) {
            QNetworkRequest request(QUrl(base + path));
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            for (const auto &[name, value] : headers)
                request.setRawHeader(name, value);
            QNetworkReply *reply = nam.sendCustomRequest(request, method, body);
            Answer a;
            if (QTest::qWaitFor([reply] { return reply->isFinished(); }, kTimeoutMs)) {
                a.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                a.body = reply->readAll();
                a.location = reply->rawHeader("Location"_ba);
                a.json = QJsonDocument::fromJson(a.body).object();
            }
            delete reply;
            return a;
        };
        const auto form = [](const QList<std::pair<QString, QString>> &fields) {
            QByteArray out;
            for (const auto &[name, value] : fields)
                out += (out.isEmpty() ? ""_ba : "&"_ba) + QUrl::toPercentEncoding(name) + '=' + QUrl::toPercentEncoding(value);
            return out;
        };
        const QList<std::pair<QByteArray, QByteArray>> formType{{"Content-Type"_ba, "application/x-www-form-urlencoded"_ba}};
        const auto field = [](const QByteArray &html, const QString &name) {
            return QRegularExpression(u"name=\"%1\" value=\"([^\"]*)\""_s.arg(name)).match(QString::fromUtf8(html)).captured(1);
        };
        Answer a = send("GET"_ba, u"/.well-known/oauth-protected-resource"_s, {}, {});
        QCOMPARE(a.status, 200);
        QCOMPARE(a.json.value(u"resource"_s).toString(), endpoint);
        a = send("POST"_ba, u"/mcp"_s, "{}"_ba, {{"Content-Type"_ba, "application/json"_ba}});
        QCOMPARE(a.status, 401);
        a = send("POST"_ba, u"/register"_s,
                 R"({"redirect_uris":["https://claude.ai/api/mcp/auth_callback"],"client_name":"Claude"})"_ba,
                 {{"Content-Type"_ba, "application/json"_ba}});
        QCOMPARE(a.status, 201);
        const QString client = a.json.value(u"client_id"_s).toString();
        const QString verifier = QString(50, u'v');
        const QString challenge = QString::fromLatin1(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
                                                              .toBase64(QByteArray::Base64UrlEncoding
                                                                        | QByteArray::OmitTrailingEquals));
        const QString query = QString::fromLatin1(form({{u"response_type"_s, u"code"_s},
                                                        {u"client_id"_s, client},
                                                        {u"redirect_uri"_s, u"https://claude.ai/api/mcp/auth_callback"_s},
                                                        {u"code_challenge"_s, challenge},
                                                        {u"code_challenge_method"_s, u"S256"_s},
                                                        {u"resource"_s, endpoint}}));
        // The pages speak the browser's language.
        a = send("GET"_ba, u"/authorize?"_s + query, {}, {{"Accept-Language"_ba, "pl-PL,pl;q=0.9,en;q=0.5"_ba}});
        QCOMPARE(a.status, 200);
        QVERIFY2(a.body.contains("Hasło"), a.body.constData());
        a = send("GET"_ba, u"/authorize?"_s + query, {}, {{"Accept-Language"_ba, "en-GB"_ba}});
        QVERIFY(a.body.contains("Password"));
        const QString request = field(a.body, u"request"_s);
        a = send("POST"_ba, u"/authorize"_s,
                 form({{u"request"_s, request}, {u"csrf"_s, field(a.body, u"csrf"_s)}, {u"action"_s, u"signin"_s},
                       {u"password"_s, password}}),
                 formType);
        QVERIFY2(a.body.contains("value=\"allow\""), a.body.constData());
        a = send("POST"_ba, u"/authorize"_s,
                 form({{u"request"_s, request}, {u"csrf"_s, field(a.body, u"csrf"_s)}, {u"action"_s, u"allow"_s},
                       {u"write"_s, u"on"_s}}),
                 formType);
        QCOMPARE(a.status, 303);
        const QString code = QUrlQuery(QUrl(QString::fromUtf8(a.location))).queryItemValue(u"code"_s);
        a = send("POST"_ba, u"/token"_s,
                 form({{u"grant_type"_s, u"authorization_code"_s}, {u"code"_s, code}, {u"client_id"_s, client},
                       {u"code_verifier"_s, verifier}, {u"redirect_uri"_s, u"https://claude.ai/api/mcp/auth_callback"_s}}),
                 formType);
        QCOMPARE(a.status, 200);
        const QByteArray bearer = "Bearer "_ba + a.json.value(u"access_token"_s).toString().toLatin1();
        const QList<std::pair<QByteArray, QByteArray>> modern{
                {"Authorization"_ba, bearer},       {"Content-Type"_ba, "application/json"_ba},
                {"MCP-Protocol-Version"_ba, "2026-07-28"_ba}, {"Mcp-Method"_ba, "tools/call"_ba},
                {"Mcp-Name"_ba, "brulion_search"_ba}};
        a = send("POST"_ba, u"/mcp"_s,
                 R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"brulion_search","arguments":{"text":"zolw"},"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}})"_ba,
                 modern);
        QCOMPARE(a.status, 200);
        QCOMPARE(a.json.value(u"result"_s)
                         .toObject()
                         .value(u"structuredContent"_s)
                         .toObject()
                         .value(u"result"_s)
                         .toObject()
                         .value(u"hits"_s)
                         .toArray()
                         .size(),
                 1);
        // The connection as `mcp clients` lists it, and revoked while the server runs.
        r = run(dir, {u"mcp"_s, u"clients"_s});
        const QJsonArray connections = r.result().value(u"connections"_s).toArray();
        QCOMPARE(connections.size(), 1);
        QCOMPARE(connections.first().toObject().value(u"client"_s).toString(), u"Claude"_s);
        QVERIFY(connections.first().toObject().value(u"write"_s).toBool());
        r = run(dir, {u"--format"_s, u"text"_s, u"mcp"_s, u"clients"_s});
        QVERIFY(r.out.contains("Claude"));
        r = run(dir, {u"mcp"_s, u"revoke"_s, u"bgr_nie-ma"_s});
        QCOMPARE(r.code, 3);
        r = run(dir, {u"mcp"_s, u"revoke"_s});
        QCOMPARE(r.code, 2);
        r = run(dir, {u"mcp"_s, u"revoke"_s, connections.first().toObject().value(u"id"_s).toString()});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"revoked"_s).toArray().size(), 1);
        a = send("POST"_ba, u"/mcp"_s,
                 R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}})"_ba,
                 modern);
        QCOMPARE(a.status, 401);
        server.kill();
        QVERIFY(server.waitForFinished(kTimeoutMs));
        log += server.readAllStandardError();
        QVERIFY(log.contains("serving"));
        QVERIFY(log.contains("POST /token 200"));
        QVERIFY(!log.contains(password.toUtf8()));
        QVERIFY(!log.contains(bearer.mid(7)));

        // --password-file with the hash: the state file's password is not needed. On a port given
        // (one free a moment ago).
        quint16 freePort = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            freePort = probe.serverPort();
        }
        const QString hashFile = m_tmp.filePath(u"mcp-hash.txt"_s);
        writeFile(hashFile, printedHash.toUtf8() + '\n');
        QProcess second;
        const auto stopSecond = qScopeGuard([&second] {
            second.kill();
            second.waitForFinished();
        });
        second.setProcessEnvironment(environment());
        second.start(QStringLiteral(BRULION_CLI),
                     {u"--vault"_s, dir, u"mcp"_s, u"serve"_s, u"--port"_s, QString::number(freePort), u"--read-only"_s,
                      u"--auth-file"_s, m_tmp.filePath(u"inny/stan.json"_s), u"--password-file"_s, hashFile});
        QVERIFY(second.waitForStarted(kTimeoutMs));
        QVERIFY(QTest::qWaitFor([&] { return second.canReadLine() || second.state() != QProcess::Running; },
                                kTimeoutMs));
        const QJsonObject secondStarted = QJsonDocument::fromJson(second.readLine()).object();
        QVERIFY2(secondStarted.value(u"ok"_s).toBool(), second.readAllStandardError().constData());
        QVERIFY(secondStarted.value(u"result"_s).toObject().value(u"readOnly"_s).toBool());
        QCOMPARE(secondStarted.value(u"result"_s).toObject().value(u"port"_s).toInt(), freePort);
        second.kill();
        QVERIFY(second.waitForFinished(kTimeoutMs));
    }

    void mcp()
    {
        const QString dir = vault({{u"Zwierzęta/Żółw.md"_s, "- żółw\n"}});
        QProcess p;
        p.setProcessEnvironment(environment());
        p.start(QStringLiteral(BRULION_CLI), {u"--vault"_s, dir, u"--now"_s, kNow, u"mcp"_s});
        QVERIFY(p.waitForStarted(kTimeoutMs));
        const auto request = [&](const QByteArray &line) -> QJsonObject {
            p.write(line + '\n');
            QElapsedTimer t;
            t.start();
            while (!p.canReadLine() && t.elapsed() < kTimeoutMs)
                p.waitForReadyRead(100);
            return QJsonDocument::fromJson(p.readLine()).object();
        };
        QJsonObject reply = request(
                R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
        QCOMPARE(reply.value(u"id"_s).toInt(), 1);
        QCOMPARE(reply.value(u"result"_s).toObject().value(u"protocolVersion"_s).toString(), u"2025-06-18"_s);
        QVERIFY(reply.value(u"result"_s).toObject().value(u"instructions"_s).toString().contains(u"untrusted"_s));
        p.write(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"
                "\n");
        reply = request(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
        QStringList names;
        for (const QJsonValue &t : reply.value(u"result"_s).toObject().value(u"tools"_s).toArray()) {
            const QJsonObject tool = t.toObject();
            names.append(tool.value(u"name"_s).toString());
            // The text has no --stdin in MCP: the schema says it is needed.
            if (names.last() == u"brulion_capture" || names.last() == u"brulion_journal_append")
                QVERIFY(tool.value(u"inputSchema"_s).toObject().value(u"required"_s).toArray().contains(u"text"_s));
        }
        QVERIFY(names.contains(u"brulion_capture"_s));
        QVERIFY(names.contains(u"brulion_search"_s));
        QVERIFY(!names.contains(u"brulion_page_delete"_s)); // destructive: not without --allow-destructive
        QVERIFY(!names.contains(u"brulion_task_done"_s));   // an alias of brulion_task_set
        QVERIFY(names.contains(u"brulion_app_open"_s));
        QVERIFY(!names.contains(u"brulion_app_state"_s)); // the app's remote control: only with --app-control
        QVERIFY(!names.contains(u"brulion_app_click"_s));
        reply = request(
                R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"brulion_capture","arguments":{"text":"Kupić mleko jutro o 18"}}})");
        QJsonObject result = reply.value(u"result"_s).toObject();
        QVERIFY(!result.value(u"isError"_s).toBool());
        QCOMPARE(result.value(u"structuredContent"_s)
                         .toObject()
                         .value(u"result"_s)
                         .toObject()
                         .value(u"kind"_s)
                         .toString(),
                 u"reminder"_s);
        QVERIFY(result.value(u"content"_s)
                        .toArray()
                        .first()
                        .toObject()
                        .value(u"text"_s)
                        .toString()
                        .contains(u"\"ok\":true"_s));
        QCOMPARE(readFile(dir + u"/journals/2026-10-14.md"_s), "- [ ] Kupić mleko ⏰ 2026-10-15 18:00\n"_ba);
        // A dry run through MCP.
        reply = request(
                R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"brulion_journal_append","arguments":{"text":"x","dryRun":true}}})");
        QVERIFY(reply.value(u"result"_s)
                        .toObject()
                        .value(u"structuredContent"_s)
                        .toObject()
                        .value(u"result"_s)
                        .toObject()
                        .value(u"dryRun"_s)
                        .toBool());
        // A file written by another program meanwhile is found by the warm index.
        writeFile(dir + u"/Nowy.md"_s, "- jeżozwierz\n");
        reply = request(
                R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"brulion_search","arguments":{"text":"jezozwierz"}}})");
        QCOMPARE(reply.value(u"result"_s)
                         .toObject()
                         .value(u"structuredContent"_s)
                         .toObject()
                         .value(u"result"_s)
                         .toObject()
                         .value(u"hits"_s)
                         .toArray()
                         .size(),
                 1);
        // Errors: malformed JSON, an unknown method, an unknown tool, a wrong argument type.
        reply = request("{nie json");
        QCOMPARE(reply.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32700);
        reply = request(R"({"jsonrpc":"2.0","id":6,"method":"resources/list"})");
        QCOMPARE(reply.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32601);
        reply = request(
                R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"brulion_page_delete","arguments":{"ref":"Nowy"}}})");
        QCOMPARE(reply.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32602);
        reply = request(
                R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"brulion_search","arguments":{"text":42}}})");
        QVERIFY(reply.value(u"result"_s).toObject().value(u"isError"_s).toBool());
        reply = request(R"({"jsonrpc":"2.0","id":9,"method":"ping"})");
        QCOMPARE(reply.value(u"id"_s).toInt(), 9);
        // A request line of up to 16 MiB is read; a longer one is refused (Invalid Request, no id:
        // it was not read) and skipped to its end, and the server goes on.
        constexpr qsizetype kMaxLine = 16 * 1024 * 1024; // mcp.cpp, kMaxLineBytes
        // (No raw strings: moc loses its way in one with an odd number of quotes.)
        const QByteArray head = "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"ping\",\"params\":{\"zażółć\":\"";
        const QByteArray tail = "\"}}";
        QByteArray longest = head + QByteArray(kMaxLine - head.size() - tail.size(), 'x') + tail;
        QCOMPARE(longest.size(), kMaxLine);
        reply = request(longest);
        QCOMPARE(reply.value(u"id"_s).toInt(), 10);
        reply = request(longest.insert(head.size(), 'x'));
        QCOMPARE(reply.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32600);
        QVERIFY(reply.contains(u"id"_s) && reply.value(u"id"_s).isNull());
        QVERIFY(reply.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"16777216"_s));
        reply = request(R"({"jsonrpc":"2.0","id":11,"method":"ping"})");
        QCOMPARE(reply.value(u"id"_s).toInt(), 11);
        // The per-request protocol (2026-07-28) on the same connection: no handshake, the version
        // in each request's _meta.
        reply = request(
                R"({"jsonrpc":"2.0","id":12,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}})");
        QCOMPARE(reply.value(u"result"_s).toObject().value(u"resultType"_s).toString(), u"complete"_s);
        QVERIFY(reply.value(u"result"_s).toObject().value(u"supportedVersions"_s).toArray().contains(u"2026-07-28"_s));
        reply = request(
                R"({"jsonrpc":"2.0","id":13,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"1900-01-01","io.modelcontextprotocol/clientCapabilities":{}}}})");
        QCOMPARE(reply.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32022);
        reply = request(R"({"jsonrpc":"2.0","id":14,"method":"initialize","params":{"protocolVersion":"2099-01-01"}})");
        QCOMPARE(reply.value(u"result"_s).toObject().value(u"protocolVersion"_s).toString(), u"2025-11-25"_s);
        p.closeWriteChannel();
        QVERIFY(p.waitForFinished(kTimeoutMs));
        QCOMPARE(p.exitCode(), 0);
        QVERIFY(QFile::exists(dir + u"/Nowy.md"_s));

        // --read-only: only the tools that read.
        QProcess ro;
        ro.setProcessEnvironment(environment());
        ro.start(QStringLiteral(BRULION_CLI), {u"--vault"_s, dir, u"mcp"_s, u"--read-only"_s});
        QVERIFY(ro.waitForStarted(kTimeoutMs));
        ro.write(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"
                 "\n");
        ro.closeWriteChannel();
        QVERIFY(ro.waitForFinished(kTimeoutMs));
        const QJsonArray tools = QJsonDocument::fromJson(ro.readAllStandardOutput().trimmed())
                                         .object()
                                         .value(u"result"_s)
                                         .toObject()
                                         .value(u"tools"_s)
                                         .toArray();
        QVERIFY(!tools.isEmpty());
        for (const QJsonValue &t : tools)
            QVERIFY2(t.toObject().value(u"annotations"_s).toObject().value(u"readOnlyHint"_s).toBool(),
                     qPrintable(t.toObject().value(u"name"_s).toString()));

        // --app-control: the tools that drive the running app too (read-only: those that only look).
        for (const bool readOnly : {false, true}) {
            QProcess ac;
            ac.setProcessEnvironment(environment());
            QStringList args{u"--vault"_s, dir, u"mcp"_s, u"--app-control"_s};
            if (readOnly)
                args.append(u"--read-only"_s);
            ac.start(QStringLiteral(BRULION_CLI), args);
            QVERIFY(ac.waitForStarted(kTimeoutMs));
            ac.write(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"
                     "\n"
                     R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"
                     "\n");
            ac.closeWriteChannel();
            QVERIFY(ac.waitForFinished(kTimeoutMs));
            const QList<QByteArray> lines = ac.readAllStandardOutput().trimmed().split('\n');
            QCOMPARE(lines.size(), 2);
            QVERIFY(QJsonDocument::fromJson(lines.first())
                            .object()
                            .value(u"result"_s)
                            .toObject()
                            .value(u"instructions"_s)
                            .toString()
                            .contains(u"brulion_app_state"_s));
            QStringList appTools;
            for (const QJsonValue &t : QJsonDocument::fromJson(lines.last())
                                               .object()
                                               .value(u"result"_s)
                                               .toObject()
                                               .value(u"tools"_s)
                                               .toArray())
                appTools.append(t.toObject().value(u"name"_s).toString());
            QVERIFY(appTools.contains(u"brulion_app_state"_s));
            QVERIFY(appTools.contains(u"brulion_app_screenshot"_s));
            QVERIFY(appTools.contains(u"brulion_app_wait"_s));
            QCOMPARE(appTools.contains(u"brulion_app_click"_s), !readOnly);
            QCOMPARE(appTools.contains(u"brulion_app_long_press"_s), !readOnly);
            QCOMPARE(appTools.contains(u"brulion_app_type"_s), !readOnly);
            // The live changes: those that change the app only when not read-only.
            for (const char *tool :
                 {"brulion_app_inspect", "brulion_app_events", "brulion_app_console", "brulion_app_changes",
                  "brulion_app_extensions", "brulion_app_settings", "brulion_app_contributions"})
                QVERIFY2(appTools.contains(QLatin1String(tool)), tool);
            for (const char *tool :
                 {"brulion_app_eval", "brulion_app_load", "brulion_app_unload", "brulion_app_reload", "brulion_app_set",
                  "brulion_app_theme", "brulion_app_set_property", "brulion_app_revert"})
                QVERIFY2(appTools.contains(QLatin1String(tool)) == !readOnly, tool);
            const QString instructions = QJsonDocument::fromJson(lines.first())
                                                 .object()
                                                 .value(u"result"_s)
                                                 .toObject()
                                                 .value(u"instructions"_s)
                                                 .toString();
            QVERIFY(instructions.contains(u"brulion_app_load"_s) && instructions.contains(u"brulion_app_revert"_s));
            if (!readOnly) {
                // A tool's description tells a model what it does and that the user can undo it.
                for (const QJsonValue &t : QJsonDocument::fromJson(lines.last())
                                                   .object()
                                                   .value(u"result"_s)
                                                   .toObject()
                                                   .value(u"tools"_s)
                                                   .toArray()) {
                    const QJsonObject tool = t.toObject();
                    if (tool.value(u"name"_s).toString() != u"brulion_app_load")
                        continue;
                    const QString description = tool.value(u"description"_s).toString();
                    QVERIFY2(description.contains(u"replaces"_s) && description.contains(u"undo"_s),
                             qPrintable(description));
                    const QJsonObject properties =
                            tool.value(u"inputSchema"_s).toObject().value(u"properties"_s).toObject();
                    QVERIFY(properties.contains(u"code"_s) && properties.contains(u"file"_s));
                    QVERIFY(!tool.value(u"annotations"_s).toObject().value(u"readOnlyHint"_s).toBool());
                }
            }
        }

        // --allow-destructive: page write may empty a page (content "" is a text).
        QProcess rw;
        rw.setProcessEnvironment(environment());
        rw.start(QStringLiteral(BRULION_CLI), {u"--vault"_s, dir, u"mcp"_s, u"--allow-destructive"_s});
        QVERIFY(rw.waitForStarted(kTimeoutMs));
        const QString hash = brulion::notebook::contentHash(readFile(dir + u"/Nowy.md"_s));
        rw.write(QJsonDocument(QJsonObject{{u"jsonrpc"_s, u"2.0"_s},
                                           {u"id"_s, 1},
                                           {u"method"_s, u"tools/call"_s},
                                           {u"params"_s,
                                            QJsonObject{{u"name"_s, u"brulion_page_write"_s},
                                                        {u"arguments"_s, QJsonObject{{u"ref"_s, u"Nowy"_s},
                                                                                     {u"content"_s, QString()},
                                                                                     {u"expectHash"_s, hash}}}}}})
                         .toJson(QJsonDocument::Compact)
                 + '\n');
        rw.closeWriteChannel();
        QVERIFY(rw.waitForFinished(kTimeoutMs));
        const QJsonObject written = QJsonDocument::fromJson(rw.readAllStandardOutput().trimmed()).object();
        QVERIFY2(!written.value(u"result"_s).toObject().value(u"isError"_s).toBool(true),
                 QJsonDocument(written).toJson().constData());
        QCOMPARE(readFile(dir + u"/Nowy.md"_s), QByteArray());
    }

    // The app's binary hands `brulion cli …` over (the AppImage's one entry point): no window, no log
    // file, no single-instance claim.
    void appBinaryHandsOver()
    {
#ifdef BRULION_APP
        const QString dir = vault();
        QProcess p;
        QProcessEnvironment env = environment();
        env.remove(u"QT_QPA_PLATFORM"_s);
        env.remove(u"DISPLAY"_s);
        env.remove(u"WAYLAND_DISPLAY"_s);
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(BRULION_APP),
                               {u"cli"_s, u"--vault"_s, dir, u"--now"_s, kNow, u"capture"_s, u"Test jutro o 9"_s});
        QVERIFY(p.waitForFinished(kTimeoutMs));
        QCOMPARE(p.exitCode(), 0);
        const QJsonObject json = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
        QVERIFY(json.value(u"ok"_s).toBool());
        QCOMPARE(readFile(dir + u"/journals/2026-10-14.md"_s), "- [ ] Test ⏰ 2026-10-15 09:00\n"_ba);
        // No single-instance claim: nothing in the runtime folder (on Windows no lock file of this
        // profile's key in the temp folder).
#ifdef Q_OS_WIN
        QVERIFY(!QFileInfo::exists(brulion::ipc::SingleInstance::keyFor(appDataDir()) + u".lock"_s));
#else
        QVERIFY(QDir(m_home + u"/run"_s).entryList({u"brulion-*"_s}, QDir::AllEntries | QDir::System).isEmpty());
#endif
        QVERIFY(!QFileInfo::exists(logFile()));
        p.start(QStringLiteral(BRULION_APP), {u"cli"_s, u"--help"_s});
        QVERIFY(p.waitForFinished(kTimeoutMs));
        QVERIFY(p.readAllStandardOutput().contains("brulion cli"));
#else
        QSKIP("built without the app (BRULION_BUILD_APP=OFF)");
#endif
    }

    void noReminderStateAndSettingsUntouched()
    {
        // After writes with reminders, the device state holds history and the trash's notes only.
        const QString dir = vault();
        QVERIFY(run(dir, {u"capture"_s, u"Leki dziś o 21"_s}).code == 0);
        QVERIFY(run(dir, {u"remind"_s, u"list"_s}).code == 0);
        const QString store = QString::fromUtf8(readFile(stateDirOf(dir) + u"/store.json"_s));
        QVERIFY(!store.contains(u"reminders/"_s));
        QVERIFY(!QFileInfo::exists(settingsFile())); // never created
    }

    void publishing()
    {
        // A folder target (no network): add, include, the dry run, a publish, an unchanged one, a
        // changed page, refusals, secrets kept out of the store, unpublish (destructive).
        const QString dir = vault({{u"Blog/Pierwszy wpis.md"_s, "# Wpis\nZażółć gęślą jaźń, [[Szkic]] ![[kot.png]]\n"},
                                   {u"Blog/Szkic.md"_s, "---\npublish: false\n---\nTajne.\n"},
                                   {u"assets/kot.png"_s, "\x89PNG kot"},
                                   {u"Prywatne.md"_s, "Hasło.\n"}});
        const QString www = m_tmp.filePath(u"www łąka"_s);
        Run r = run(dir, {u"publish"_s, u"targets"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"targets"_s).toArray().isEmpty());
        r = run(dir, {u"publish"_s, u"add"_s, u"folder"_s, u"Strona łąkowa"_s, u"--path"_s, www});
        QCOMPARE(r.code, 0);
        const QString id = r.result().value(u"id"_s).toString();
        QVERIFY(id.startsWith(u't'));
        QCOMPARE(run(dir, {u"publish"_s, u"add"_s, u"folder"_s, u"strona ŁĄKOWA"_s, u"--path"_s, www}).errorCode(),
                 u"target_exists"_s);
        QCOMPARE(run(dir, {u"publish"_s, u"add"_s, u"github"_s, u"Zła"_s, u"--repository"_s, u"jan"_s}).errorCode(),
                 u"bad_target"_s);
        // Nothing chosen yet.
        r = run(dir, {u"publish"_s, u"run"_s, u"Strona łąkowa"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"nothing_to_publish"_s);
        QCOMPARE(run(dir, {u"publish"_s, u"include"_s, u"Strona łąkowa"_s, u"--folder"_s, u"Blog"_s}).code, 0);
        // The dry run: what goes up and what is left out, nothing written.
        r = run(dir, {u"publish"_s, u"preview"_s, u"Strona łąkowa"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"counts"_s).toObject().value(u"upload"_s).toInt(), 6);
        const QJsonArray leftOut = r.result().value(u"leftOut"_s).toArray();
        QCOMPARE(leftOut.size(), 1);
        QCOMPARE(leftOut.first().toObject().value(u"reason"_s).toString(), u"private"_s);
        QVERIFY(!QFileInfo::exists(www));
        QCOMPARE(run(dir, {u"--dry-run"_s, u"publish"_s, u"run"_s, id}).result().value(u"operation"_s).toString(),
                 u"preview"_s);
        QVERIFY(!QFileInfo::exists(www));
        // Read-only refuses a publish.
        QCOMPARE(run(dir, {u"--read-only"_s, u"publish"_s, u"run"_s, id}).code, 7);
        // Publish.
        r = run(dir, {u"publish"_s, u"run"_s, u"Strona łąkowa"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"done"_s).toObject().value(u"uploaded"_s).toInt(), 6);
        QVERIFY(readFile(www + u"/Blog/Pierwszy wpis.html"_s).contains("Zażółć gęślą jaźń"));
        QVERIFY(!QFileInfo::exists(www + u"/Blog/Szkic.html"_s));
        QVERIFY(!QFileInfo::exists(www + u"/Prywatne.html"_s));
        QVERIFY(r.result().value(u"url"_s).toString().startsWith(u"file://"_s));
        // Again: nothing to send.
        r = run(dir, {u"publish"_s, u"run"_s, id});
        QCOMPARE(r.result().value(u"counts"_s).toObject().value(u"unchanged"_s).toInt(), 6);
        QCOMPARE(r.result().value(u"done"_s).toObject().value(u"uploaded"_s).toInt(), 0);
        // The last status, the manifest in the device state.
        r = run(dir, {u"publish"_s, u"targets"_s});
        const QJsonObject target = r.result().value(u"targets"_s).toArray().first().toObject();
        QVERIFY(target.value(u"last"_s).toObject().value(u"ok"_s).toBool());
        QVERIFY(QFileInfo::exists(stateDirOf(dir) + u"/publish/"_s + id + u".json"_s));
        // A file changed in the folder: a conflict (exit 4) until --force (destructive).
        writeFile(www + u"/style.css"_s, "body{}");
        writeFile(dir + u"/Blog/Pierwszy wpis.md"_s, "Nowy tekst.\n");
        r = run(dir, {u"publish"_s, u"run"_s, id});
        QCOMPARE(r.code, 4);
        QCOMPARE(r.errorCode(), u"conflicts"_s);
        QCOMPARE(run(dir, {u"publish"_s, u"run"_s, id, u"--force"_s}).code, 7);
        QCOMPARE(run(dir, {u"--allow-destructive"_s, u"publish"_s, u"run"_s, id, u"--force"_s}).code, 0);
        QVERIFY(readFile(www + u"/Blog/Pierwszy wpis.html"_s).contains("Nowy tekst"));
        // A secret: from standard input, into an owner-only file, never into the store.
        QCOMPARE(run(dir,
                     {u"publish"_s, u"add"_s, u"webdav"_s, u"Serwer"_s, u"--url"_s, u"https://dav.example.pl/strona"_s})
                         .code,
                 0);
        QCOMPARE(run(dir, {u"publish"_s, u"secret"_s, u"Serwer"_s, u"--stdin"_s}, "hasło-źdźbło\n").code, 0);
        QVERIFY(!readFile(stateDirOf(dir) + u"/publish/targets.json"_s).contains("hasło"));
        r = run(dir, {u"publish"_s, u"targets"_s});
        QVERIFY(r.result().value(u"targets"_s).toArray().at(1).toObject().value(u"hasPassword"_s).toBool());
        // Unpublishing needs --allow-destructive; the folder's own files stay.
        writeFile(www + u"/.htaccess"_s, "Options -Indexes\n");
        QCOMPARE(run(dir, {u"publish"_s, u"unpublish"_s, id}).code, 7);
        r = run(dir, {u"--allow-destructive"_s, u"publish"_s, u"unpublish"_s, id});
        QCOMPARE(r.code, 0);
        QVERIFY(!QFileInfo::exists(www + u"/index.html"_s));
        QVERIFY(QFileInfo::exists(www + u"/.htaccess"_s));
        // Configuration is the user's: no MCP tool adds targets or changes what they publish.
        r = cli({u"schema"_s});
        QVERIFY(r.code == 0);
        QCOMPARE(run(dir, {u"--allow-destructive"_s, u"publish"_s, u"remove"_s, u"Serwer"_s}).code, 0);
        QCOMPARE(run(dir, {u"publish"_s, u"targets"_s}).result().value(u"targets"_s).toArray().size(), 1);
    }

    void protectedPublishing()
    {
        // Password-protected pages: --protect on, the pages' password from standard input, only
        // ciphertext in the folder, the password never in the store.
        const QString dir = vault({{u"Blog/Pierwszy wpis.md"_s, "# Wpis\nZażółć gęślą jaźń, ![[kot.png]]\n"},
                                   {u"assets/kot.png"_s, "\x89PNG kot"}});
        const QString www = m_tmp.filePath(u"www chroniona"_s);
        Run r = run(dir,
                    {u"publish"_s, u"add"_s, u"folder"_s, u"Rodzina"_s, u"--path"_s, www, u"--protect"_s, u"on"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"protect"_s).toBool());
        QVERIFY(!r.result().value(u"hasPagesPassword"_s).toBool());
        QCOMPARE(run(dir, {u"publish"_s, u"include"_s, u"Rodzina"_s, u"--folder"_s, u"Blog"_s}).code, 0);
        r = run(dir, {u"publish"_s, u"run"_s, u"Rodzina"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"no_page_password"_s);
        QVERIFY(!QFileInfo::exists(www));
        QCOMPARE(run(dir, {u"publish"_s, u"secret"_s, u"Rodzina"_s, u"--share"_s, u"--pages"_s, u"--stdin"_s}, "x\n")
                         .code,
                 2);
        QCOMPARE(run(dir, {u"publish"_s, u"secret"_s, u"Rodzina"_s, u"--pages"_s, u"--stdin"_s}, "Źdźbło łąki\n")
                         .result()
                         .value(u"which"_s)
                         .toString(),
                 u"pages"_s);
        r = run(dir, {u"publish"_s, u"run"_s, u"Rodzina"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"protected"_s).toBool());
        const QByteArray index = readFile(www + u"/index.html"_s);
        QVERIFY(index.contains("brulion-sealed"));
        for (const QString &secret : {u"Zażółć"_s, u"Pierwszy wpis"_s, u"kot.png"_s, u"Źdźbło łąki"_s})
            QVERIFY2(!index.contains(secret.toUtf8()), qPrintable(secret));
        QVERIFY(!QFileInfo::exists(www + u"/Blog"_s));
        QVERIFY(!QFileInfo::exists(www + u"/search-index.js"_s));
        QCOMPARE(QDir(www + u"/p"_s).entryList(QDir::Files).size(), 1);
        QCOMPARE(QDir(www + u"/f"_s).entryList(QDir::Files).size(), 1);
        const QByteArray targets = readFile(stateDirOf(dir) + u"/publish/targets.json"_s);
        QVERIFY(targets.contains("protectSalt"));
        QVERIFY(!targets.contains("Źdźbło łąki"));
        r = run(dir, {u"publish"_s, u"targets"_s});
        QVERIFY(r.result().value(u"targets"_s).toArray().first().toObject().value(u"hasPagesPassword"_s).toBool());
        // Unchanged: nothing sent again.
        r = run(dir, {u"publish"_s, u"run"_s, u"Rodzina"_s});
        QCOMPARE(r.result().value(u"done"_s).toObject().value(u"uploaded"_s).toInt(), 0);
    }
};

QTEST_GUILESS_MAIN(TestCliProcess)
#include "tst_cli_process.moc"
