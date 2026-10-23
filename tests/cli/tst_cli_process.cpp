// brulion-cli as a process (QProcess on the built binary, a private HOME / XDG folders): the JSON
// envelope and exit code of the commands on temporary notebooks with Polish text, the files they
// write byte for byte (line endings, BOM), refusals (read-only, destructive without
// --allow-destructive, paths outside the notebook, a missing notebook — never created), the app's
// settings file left byte-identical and no reminder state written, Polish argv under LANG=C and
// text on stdin, two processes appending to one journal (nothing lost), dry runs, `app open`
// against a stand-in app, and the MCP server (initialize, tools/list, tools/call, errors, the warm
// index seeing a file written meanwhile).
#include "appmessage.h"
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
#include <QProcess>
#include <QSaveFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

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
        return env;
    }

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
        return brulion::notebook::deviceStatePath(m_home + u"/data/Brulion/Brulion"_s, vaultDir);
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
    }

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
        const QString ini = m_home + u"/config/Brulion/Brulion.ini"_s;
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

    void captureAndJournals()
    {
        const QString dir = vault({{u"templates/journal.md"_s, "## {{weekday}}\n- \n"}});
        Run r = run(dir, {u"capture"_s, u"Zadzwonić do Łukasza jutro o 9"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"kind"_s).toString(), u"reminder"_s);
        QCOMPARE(r.result().value(u"when"_s).toObject().value(u"date"_s).toString(), u"2026-10-15"_s);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-10-14.md"_s);
        QCOMPARE(readFile(dir + u"/journals/2026-10-14.md"_s),
                 "## środa\n- [ ] Zadzwonić do Łukasza ⏰ 2026-10-15 09:00\n"_ba);
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
        QCOMPARE(readFile(dir + u"/journals/2026-10-13.md"_s), "## wtorek\n- Notatka z wczoraj\n"_ba);
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
        const QString dir = vault();
        const auto loop = [&](const QString &who) {
            auto *p = new QProcess(this);
            p->setProcessEnvironment(environment());
            QStringList script;
            for (int i = 0; i < 25; ++i)
                script.append(
                        u"\"$1\" --vault \"$2\" --now \"$3\" journal append \"%1 %2\" >/dev/null || exit 1"_s.arg(who)
                                .arg(i));
            p->start(u"sh"_s, {u"-c"_s, script.join(u'\n'), u"sh"_s, QStringLiteral(BRULION_CLI), dir, kNow});
            return p;
        };
        QProcess *a = loop(u"A"_s);
        QProcess *b = loop(u"B"_s);
        QVERIFY(a->waitForFinished(kTimeoutMs * 4));
        QVERIFY(b->waitForFinished(kTimeoutMs * 4));
        QCOMPARE(a->exitCode(), 0);
        QCOMPARE(b->exitCode(), 0);
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
        brulion::ipc::SingleInstance app(brulion::ipc::SingleInstance::keyFor(m_home + u"/data/Brulion/Brulion"_s));
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
        for (const QJsonValue &t : reply.value(u"result"_s).toObject().value(u"tools"_s).toArray())
            names.append(t.toObject().value(u"name"_s).toString());
        QVERIFY(names.contains(u"brulion_capture"_s));
        QVERIFY(names.contains(u"brulion_search"_s));
        QVERIFY(!names.contains(u"brulion_page_delete"_s)); // destructive: not without --allow-destructive
        QVERIFY(!names.contains(u"brulion_task_done"_s));   // an alias of brulion_task_set
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
        QVERIFY(QDir(m_home + u"/run"_s).entryList({u"brulion-*"_s}, QDir::AllEntries | QDir::System).isEmpty());
        QVERIFY(!QFileInfo::exists(m_home + u"/state/Brulion/Brulion/logs/brulion.log"_s));
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
        QVERIFY(!QFileInfo::exists(m_home + u"/config/Brulion/Brulion.ini"_s)); // never created
    }
};

QTEST_GUILESS_MAIN(TestCliProcess)
#include "tst_cli_process.moc"
