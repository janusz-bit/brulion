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
// tools/call, errors, the warm index seeing a file written meanwhile).
#include "appmessage.h"
#include "controlprotocol.h"
#include "controlserver.h"
#include "notebook/journals.h"
#include "notebook/session.h"
#include "singleinstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

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
    /// the system's — Polish for LANG=C.UTF-8 (environment()); Windows ignores LANG, there it is
    /// the user's language (English on the CI runners).
    static QString weekday(int day)
    {
#ifdef Q_OS_WIN
        if (brulion::notebook::appLanguage({}) == u"en")
            return QLocale(QLocale::English).dayName(day);
#endif
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

    // Where brulion-cli (and the app) keep their settings, app data and log for this test: in the
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
    Run cliLive(const QStringList &args)
    {
        QProcess p;
        p.setProcessEnvironment(environment());
        p.start(QStringLiteral(BRULION_CLI), args);
        Run r;
        if (!p.waitForStarted(kTimeoutMs))
            return r;
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
        QCOMPARE(r.result().value(u"mcpProtocol"_s).toString(), u"2025-06-18"_s);
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
    // tells them apart (notebook::pageLinkTarget, the app's "Kopiuj odnośnik" and page menu alike).
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
        // ui/language never chosen: the system's, as the app (an English one: English notes).
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
        QCOMPARE(r.result().value(u"language"_s).toString(), u"pl"_s);
        QFile::remove(ini);
        QDir(docs).removeRecursively();
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
            if (request.method == u"find" && fail.startsWith(u"fail:"_s))
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
};

QTEST_GUILESS_MAIN(TestCliProcess)
#include "tst_cli_process.moc"
