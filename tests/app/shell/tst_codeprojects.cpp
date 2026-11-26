// CodeProjects beyond the list (docs/CODE.md): a project's t3.json (parsing malformed and hostile
// files, the watcher), scripts merged with this device's, pins, icons, order and defaults, "Open in…"
// (editors found on a PATH of fakes, the arguments that open a file at a line), pull request
// references, and — where git is — worktrees from a base branch, branches, a pull request checked
// out without gh and with a fake one, a worktree checked, removed and made again, a deleted thread's
// Undo.
#include "codeprojects.h"
#include "codescriptsmodel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::shell::CodeProjects;
using brulion::shell::CodeScriptsModel;

namespace {

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

/// git in `folder` (an identity of its own), its exit code.
int git(const QString &folder, const QStringList &args)
{
    QProcess p;
    p.setProgram(u"git"_s);
    p.setArguments(QStringList{u"-C"_s, folder, u"-c"_s, u"user.name=Ala"_s, u"-c"_s, u"user.email=ala@example.com"_s,
                               u"-c"_s, u"init.defaultBranch=main"_s, u"-c"_s, u"commit.gpgsign=false"_s}
                   + args);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start();
    p.waitForFinished(30000);
    if (p.exitCode() != 0)
        qWarning() << "git" << args << p.readAll();
    return p.exitCode();
}

QString gitOut(const QString &folder, const QStringList &args)
{
    QProcess p;
    p.start(u"git"_s, QStringList{u"-C"_s, folder} + args);
    p.waitForFinished(30000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QVariantMap byName(const QVariantList &rows, const QString &name)
{
    for (const QVariant &v : rows)
        if (v.toMap().value(u"name"_s).toString() == name)
            return v.toMap();
    return {};
}

} // namespace

class TestCodeProjects : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString fresh(const QString &name) { return m_tmp.filePath(u"%1-%2"_s.arg(++m_n).arg(name)); }
    bool m_git = false;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_git = !QStandardPaths::findExecutable(u"git"_s).isEmpty();
    }

    // ---- t3.json ---------------------------------------------------------------------------------

    void parseConfig_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("error");       // a word of the error ("" none)
        QTest::addColumn<QStringList>("names");   // the scripts read
        QTest::addColumn<QString>("mode");
        QTest::addColumn<int>("warnings");

        QTest::newRow("T3 Code's example")
                // (Not a raw string: moc reads one with "//" in it as a comment.)
                << QByteArray("{\"$schema\":\"https:\\/\\/t3.codes\\/schema\\/t3.json\",\"iconPath\":\"logo.png\","
                              "\"defaultThreadEnvMode\":\"worktree\",\"worktreeSubmodules\":\"top-level\","
                              "\"scripts\":[{\"name\":\"Setup\",\"command\":\"pnpm install\",\"icon\":\"configure\","
                              "\"runOnWorktreeCreate\":true,\"async\":false},"
                              "{\"name\":\"Dev\",\"command\":\"pnpm dev\",\"icon\":\"play\","
                              "\"previewUrl\":\"http:\\/\\/localhost:3000\"},"
                              "{\"name\":\"Testy — kalkulator\",\"command\":\"ctest -R kalk\",\"icon\":\"test\"}]}")
                << QString() << QStringList{u"Setup"_s, u"Dev"_s, u"Testy — kalkulator"_s} << u"worktree"_s
                << 1; // (its logo.png is not there)
        QTest::newRow("empty object") << QByteArray("{}") << QString() << QStringList() << QString() << 0;
        QTest::newRow("not JSON") << QByteArray("{scripts: [}") << u"not valid JSON"_s << QStringList() << QString()
                                  << 0;
        QTest::newRow("an array") << QByteArray("[1,2]") << u"not a JSON object"_s << QStringList() << QString() << 0;
        QTest::newRow("empty file") << QByteArray() << u"not valid JSON"_s << QStringList() << QString() << 0;
        QTest::newRow("too large") << QByteArray(CodeProjects::MaxConfigBytes + 1, ' ') << u"larger than"_s
                                   << QStringList() << QString() << 0;
        QTest::newRow("wrong types")
                << QByteArray(R"({"defaultThreadEnvMode":"cloud","iconPath":7,"scripts":[
                    7, "x", {"name":1,"command":"a"}, {"name":"b"}, {"name":"  ","command":"c"},
                    {"name":"Ok","command":"make","icon":"rocket","runOnWorktreeCreate":"yes","async":"false"}]})")
                << QString() << QStringList{u"Ok"_s} << QString() << 7;
        QTest::newRow("scripts not a list") << QByteArray(R"({"scripts":{"name":"a","command":"b"}})") << QString()
                                            << QStringList() << QString() << 1;
        QTest::newRow("same name twice, letter case aside")
                << QByteArray(R"({"scripts":[{"name":"Test","command":"a"},{"name":"test","command":"b"}]})")
                << QString() << QStringList{u"Test"_s} << QString() << 1;
        QTest::newRow("control characters in a name")
                << QByteArray("{\"scripts\":[{\"name\":\"Zbuduj\\n\\u001b[31m\\u200e ąę\",\"command\":\"make\"}]}")
                << QString() << QStringList{u"Zbuduj [31m ąę"_s} << QString() << 0;
        QTest::newRow("local mode") << QByteArray(R"({"defaultThreadEnvMode":"local"})") << QString()
                                    << QStringList() << u"local"_s << 0;
    }
    void parseConfig()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, error);
        QFETCH(QStringList, names);
        QFETCH(QString, mode);
        QFETCH(int, warnings);
        const QString folder = fresh(u"projekt"_s);
        QVERIFY(QDir().mkpath(folder));
        const CodeProjects::Config c = CodeProjects::parseConfig(json, folder);
        QVERIFY(c.found);
        if (error.isEmpty())
            QVERIFY2(c.error.isEmpty(), qPrintable(c.error));
        else
            QVERIFY2(c.error.contains(error), qPrintable(c.error));
        QStringList read;
        for (const CodeProjects::Script &s : c.scripts)
            read << s.name;
        QCOMPARE(read, names);
        QCOMPARE(c.defaultThreadEnvMode, mode);
        QVERIFY2(c.warnings.size() == warnings, qPrintable(c.warnings.join(u" | "_s)));
    }

    void parseConfigFields()
    {
        const QString folder = fresh(u"pola"_s);
        QVERIFY(writeFile(folder + u"/assets/logo.png"_s, "png"));
        const CodeProjects::Config c = CodeProjects::parseConfig(
                R"({"iconPath":"assets/../assets/logo.png","scripts":[
                    {"name":"Setup","command":"pnpm install\n  && pnpm build","icon":"configure","runOnWorktreeCreate":true,"async":false},
                    {"name":"Watch","command":"pnpm watch","runOnWorktreeCreate":true},
                    {"name":"Lint","command":"eslint .","icon":"lint","async":false}]})",
                folder);
        QCOMPARE(c.iconPath, QFileInfo(folder + u"/assets/logo.png"_s).canonicalFilePath());
        QCOMPARE(c.scripts.size(), 3);
        QCOMPARE(c.scripts.at(0).command, u"pnpm install\n  && pnpm build"_s);
        QCOMPARE(c.scripts.at(0).icon, u"configure"_s);
        QVERIFY(c.scripts.at(0).onWorktree && c.scripts.at(0).wait);
        // (async true by default: it runs without being waited for.)
        QVERIFY(c.scripts.at(1).onWorktree && !c.scripts.at(1).wait);
        QCOMPARE(c.scripts.at(1).icon, u"play"_s);
        // (async alone, without runOnWorktreeCreate: nothing to wait for.)
        QVERIFY(!c.scripts.at(2).onWorktree && !c.scripts.at(2).wait);
        // Too many: 50 kept.
        QJsonArray many;
        for (int i = 0; i < 60; ++i)
            many << QJsonObject{{u"name"_s, u"Skrypt %1"_s.arg(i)}, {u"command"_s, u"echo %1"_s.arg(i)}};
        const CodeProjects::Config big =
                CodeProjects::parseConfig(QJsonDocument(QJsonObject{{u"scripts"_s, many}}).toJson(), folder);
        QCOMPARE(big.scripts.size(), CodeProjects::MaxScripts);
        QCOMPARE(big.warnings.size(), 1);
        // A command too long is left out.
        const QByteArray longOne = "{\"scripts\":[{\"name\":\"a\",\"command\":\"" + QByteArray(4001, 'x') + "\"}]}";
        QVERIFY(CodeProjects::parseConfig(longOne, folder).scripts.isEmpty());
    }

    void iconPath_data()
    {
        QTest::addColumn<QString>("icon");
        QTest::addColumn<bool>("kept");
        QTest::newRow("inside") << u"ikona.svg"_s << true;
        QTest::newRow("nested") << u"docs/logo żółw.png"_s << true;
        QTest::newRow("absolute") << u"/etc/passwd.png"_s << false;
        QTest::newRow("leaves the project") << u"../poza.png"_s << false;
        QTest::newRow("missing") << u"brak.png"_s << false;
        QTest::newRow("no picture") << u"README.md"_s << false;
        QTest::newRow("a folder") << u"docs.png"_s << false;
        QTest::newRow("a link out of it") << u"link.png"_s << false;
    }
    void iconPath()
    {
        QFETCH(QString, icon);
        QFETCH(bool, kept);
        const QString folder = fresh(u"ikony"_s);
        QVERIFY(writeFile(folder + u"/ikona.svg"_s, "<svg/>"));
        QVERIFY(writeFile(folder + u"/docs/logo żółw.png"_s, "png"));
        QVERIFY(writeFile(folder + u"/README.md"_s, "# x"));
        QVERIFY(QDir().mkpath(folder + u"/docs.png"_s));
        QVERIFY(writeFile(m_tmp.filePath(u"poza.png"_s), "png"));
        QFile::link(m_tmp.filePath(u"poza.png"_s), folder + u"/link.png"_s);
        const QJsonObject o{{u"iconPath"_s, icon}};
        const CodeProjects::Config c = CodeProjects::parseConfig(QJsonDocument(o).toJson(), folder);
        QCOMPARE(!c.iconPath.isEmpty(), kept);
        QCOMPARE(c.warnings.size(), kept ? 0 : 1);
        if (kept)
            QVERIFY(c.iconPath.startsWith(QFileInfo(folder).canonicalFilePath() + u'/'));
    }

    // A project's t3.json read when it is added, merged with this device's scripts, read again when it
    // changes or goes; never written.
    void configInProject()
    {
        const QString folder = fresh(u"Kalkulator"_s);
        QVERIFY(writeFile(folder + u"/t3.json"_s,
                          R"({"defaultThreadEnvMode":"worktree","scripts":[
                              {"name":"Setup","command":"npm ci","icon":"configure","runOnWorktreeCreate":true,"async":false},
                              {"name":"Testy","command":"npm test","icon":"test"}]})"));
        QByteArray kept;
        CodeProjects projects;
        projects.setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
        const QString id = projects.add(folder);
        QVERIFY(!id.isEmpty());
        QVariantList scripts = projects.scripts(id);
        QCOMPARE(scripts.size(), 2);
        QCOMPARE(scripts.at(0).toMap().value(u"source"_s).toString(), u"t3json"_s);
        QCOMPARE(scripts.at(0).toMap().value(u"iconName"_s).toString(), u"sliders-horizontal"_s);
        QCOMPARE(projects.setupScripts(id).size(), 1);
        QVERIFY(projects.project(id).value(u"config"_s).toBool());
        QCOMPARE(projects.newThreadEnv(id), u"worktree"_s);
        // A script of the device's with a name of the file's overrides it; its own come first.
        projects.setScripts(id, {QVariantMap{{u"name"_s, u"testy"_s}, {u"command"_s, u"npm test -- --watch=false"_s}},
                                 QVariantMap{{u"name"_s, u"Dev"_s}, {u"command"_s, u"npm run dev"_s}},
                                 QVariantMap{{u"name"_s, u"Dev"_s}, {u"command"_s, u"drugi"_s}},
                                 QVariantMap{{u"command"_s, u"   "_s}}});
        scripts = projects.scripts(id);
        QCOMPARE(scripts.size(), 3);
        QCOMPARE(scripts.at(0).toMap().value(u"command"_s).toString(), u"npm test -- --watch=false"_s);
        QVERIFY(scripts.at(0).toMap().value(u"overrides"_s).toBool());
        QCOMPARE(scripts.at(0).toMap().value(u"source"_s).toString(), u"device"_s);
        QCOMPARE(scripts.at(1).toMap().value(u"name"_s).toString(), u"Dev"_s);
        QCOMPARE(scripts.at(2).toMap().value(u"name"_s).toString(), u"Setup"_s);
        // (Only the device's are kept: never the file's.)
        QVERIFY(kept.contains("npm run dev") && !kept.contains("npm ci"));
        // The file changes: read again by itself.
        QSignalSpy changed(&projects, &CodeProjects::changed);
        QVERIFY(writeFile(folder + u"/t3.json"_s, R"({"scripts":[{"name":"Lint","command":"eslint .","icon":"lint"}]})"));
        QTRY_VERIFY_WITH_TIMEOUT(byName(projects.scripts(id), u"Lint"_s).value(u"iconName"_s) == u"list-checks"_s, 5000);
        QVERIFY(byName(projects.scripts(id), u"Setup"_s).isEmpty());
        QCOMPARE(projects.newThreadEnv(id), u"local"_s);
        QVERIFY(!changed.isEmpty());
        // Broken: said, and nothing of it used.
        QVERIFY(writeFile(folder + u"/t3.json"_s, "{ zepsuty"));
        QTRY_VERIFY_WITH_TIMEOUT(!projects.project(id).value(u"configError"_s).toString().isEmpty(), 5000);
        QVERIFY(byName(projects.scripts(id), u"Lint"_s).isEmpty());
        // Gone.
        QVERIFY(QFile::remove(folder + u"/t3.json"_s));
        QTRY_VERIFY_WITH_TIMEOUT(projects.project(id).value(u"configError"_s).toString().isEmpty(), 5000);
        QVERIFY(!projects.project(id).value(u"config"_s).toBool());
        // Made again.
        QVERIFY(writeFile(folder + u"/t3.json"_s, R"({"scripts":[{"name":"Nowy","command":"echo"}]})"));
        QTRY_VERIFY_WITH_TIMEOUT(!byName(projects.scripts(id), u"Nowy"_s).isEmpty(), 5000);
        // Never written by the app.
        QFile f(folder + u"/t3.json"_s);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray(R"({"scripts":[{"name":"Nowy","command":"echo"}]})"));
    }

    void mergeIds()
    {
        const QList<CodeProjects::Script> device{{u"Testy"_s, u"a"_s, u"test"_s, {}, false, false}};
        const QList<CodeProjects::Script> file{{u"TESTY"_s, u"b"_s, u"play"_s, {}, false, false},
                                               {u"Build"_s, u"c"_s, u"build"_s, {}, true, true}};
        const auto merged = CodeProjects::mergeScripts(device, file);
        QCOMPARE(merged.size(), 2);
        QCOMPARE(merged.at(0).second, u"override"_s);
        QCOMPARE(merged.at(1).first.name, u"Build"_s);
        QCOMPARE(merged.at(1).second, u"t3json"_s);
        // Ids: slugs, unique.
        CodeProjects projects;
        const QString folder = fresh(u"ids"_s);
        QVERIFY(QDir().mkpath(folder));
        const QString id = projects.add(folder);
        projects.setScripts(id, {QVariantMap{{u"name"_s, u"Zbuduj żółwia"_s}, {u"command"_s, u"a"_s}},
                                 QVariantMap{{u"name"_s, u"zbuduj Żółwia!"_s}, {u"command"_s, u"b"_s}}});
        const QVariantList scripts = projects.scripts(id);
        QCOMPARE(scripts.at(0).toMap().value(u"id"_s).toString(), u"zbuduj-zolwia"_s);
        QCOMPARE(scripts.at(1).toMap().value(u"id"_s).toString(), u"zbuduj-zolwia-2"_s);
        // Fields kept and cleaned: an unknown icon is "play", waiting needs running on a worktree.
        projects.setScripts(id, {QVariantMap{{u"name"_s, u"x"_s}, {u"command"_s, u"y"_s}, {u"icon"_s, u"rakieta"_s},
                                             {u"wait"_s, true}, {u"shortcut"_s, u"Ctrl+Alt+Y"_s}}});
        const QVariantMap s = projects.scripts(id).first().toMap();
        QCOMPARE(s.value(u"icon"_s).toString(), u"play"_s);
        QVERIFY(!s.value(u"wait"_s).toBool());
        QCOMPARE(s.value(u"shortcut"_s).toString(), u"Ctrl+Alt+Y"_s);
        // At most MaxScripts.
        QVariantList many;
        for (int i = 0; i < 70; ++i)
            many << QVariantMap{{u"name"_s, u"s%1"_s.arg(i)}, {u"command"_s, u"echo"_s}};
        projects.setScripts(id, many);
        QCOMPARE(projects.scripts(id).size(), CodeProjects::MaxScripts);
    }

    // The project dialog's scripts editor: the device's rows, then the file's; a file's row changed is
    // the device's override, deleted brings the file's back; saved, only the device's are kept.
    void scriptsModel()
    {
        const QString folder = fresh(u"edytor"_s);
        QVERIFY(writeFile(folder + u"/t3.json"_s, R"({"scripts":[
            {"name":"Setup","command":"npm ci","icon":"configure","runOnWorktreeCreate":true,"async":false},
            {"name":"Lint","command":"eslint .","icon":"lint"}]})"));
        CodeProjects projects;
        const QString id = projects.add(folder);
        projects.setScripts(id, {QVariantMap{{u"name"_s, u"Dev"_s}, {u"command"_s, u"npm run dev"_s}}});
        CodeScriptsModel m;
        m.setProjects(&projects);
        m.setProject(id);
        const auto text = [&m](int row, int role) { return m.data(m.index(row), role).toString(); };
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(text(0, CodeScriptsModel::NameRole), u"Dev"_s);
        QCOMPARE(text(1, CodeScriptsModel::SourceRole), u"t3json"_s);
        QCOMPARE(text(1, CodeScriptsModel::IconNameRole), u"sliders-horizontal"_s);
        QVERIFY(m.data(m.index(1), CodeScriptsModel::WaitRole).toBool());
        QVERIFY(!m.dirty());
        QVERIFY(!m.remove(1)); // (in t3.json)
        QVERIFY(!m.canMove(1, -1));
        // A file's row changed: the device's own, after the device's rows, overriding the file's.
        m.setText(2, u"command"_s, u"eslint --fix ."_s);
        QVERIFY(m.dirty());
        QCOMPARE(text(1, CodeScriptsModel::NameRole), u"Lint"_s);
        QCOMPARE(text(1, CodeScriptsModel::SourceRole), u"device"_s);
        QVERIFY(m.data(m.index(1), CodeScriptsModel::OverridesRole).toBool());
        QCOMPARE(text(2, CodeScriptsModel::NameRole), u"Setup"_s);
        m.setFlag(2, u"wait"_s, false);
        QCOMPARE(text(2, CodeScriptsModel::SourceRole), u"device"_s);
        QVERIFY(!m.data(m.index(2), CodeScriptsModel::WaitRole).toBool());
        QVERIFY(m.data(m.index(2), CodeScriptsModel::OnWorktreeRole).toBool());
        // A new row, moved to the top; two of a name are said.
        QCOMPARE(m.add(), 3);
        m.setText(3, u"name"_s, u"  Zbuduj   żółwia "_s);
        m.setText(3, u"command"_s, u"make żółw"_s);
        m.setText(3, u"icon"_s, u"build"_s);
        m.setText(3, u"shortcut"_s, u"Ctrl+Alt+Ż"_s);
        QCOMPARE(text(3, CodeScriptsModel::NameRole), u"Zbuduj żółwia"_s);
        QVERIFY(m.moveRow(3, -3));
        QCOMPARE(text(0, CodeScriptsModel::NameRole), u"Zbuduj żółwia"_s);
        QVERIFY(!m.canMove(0, -1) && m.canMove(0, 1));
        m.setText(1, u"name"_s, u"zbuduj ŻÓŁWIA"_s);
        QVERIFY(m.nameTaken(1) && m.nameTaken(0));
        m.setText(1, u"name"_s, u"Dev"_s);
        QVERIFY(!m.nameTaken(1));
        // (Waiting needs running on a worktree.)
        m.setFlag(0, u"wait"_s, true);
        QVERIFY(!m.data(m.index(0), CodeScriptsModel::WaitRole).toBool());
        m.save();
        QVERIFY(!m.dirty());
        QVariantList scripts = projects.scripts(id);
        QCOMPARE(scripts.size(), 4);
        QCOMPARE(scripts.at(0).toMap().value(u"name"_s).toString(), u"Zbuduj żółwia"_s);
        QCOMPARE(scripts.at(0).toMap().value(u"icon"_s).toString(), u"build"_s);
        QCOMPARE(scripts.at(0).toMap().value(u"shortcut"_s).toString(), u"Ctrl+Alt+Ż"_s);
        QCOMPARE(scripts.at(2).toMap().value(u"command"_s).toString(), u"eslint --fix ."_s);
        QVERIFY(scripts.at(3).toMap().value(u"overrides"_s).toBool());
        // The override of Lint deleted: the file's Lint again.
        QVERIFY(m.remove(2));
        QCOMPARE(m.rowCount(), 4);
        QCOMPARE(text(3, CodeScriptsModel::NameRole), u"Lint"_s);
        QCOMPARE(text(3, CodeScriptsModel::SourceRole), u"t3json"_s);
        m.save();
        scripts = projects.scripts(id);
        QCOMPARE(byName(scripts, u"Lint"_s).value(u"command"_s).toString(), u"eslint ."_s);
        QCOMPARE(byName(scripts, u"Lint"_s).value(u"source"_s).toString(), u"t3json"_s);
        // Never written to the file.
        QFile f(folder + u"/t3.json"_s);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains("\"command\":\"npm ci\""));
    }

    // ---- the projects: pins, order, icons, defaults ---------------------------------------------------

    void projectsList()
    {
        QByteArray kept;
        {
            CodeProjects projects;
            projects.setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
            QStringList ids;
            for (const QString &name : {u"Ala"_s, u"Bolek"_s, u"Celina"_s, u"Darek"_s}) {
                const QString folder = fresh(name);
                QVERIFY(QDir().mkpath(folder));
                ids << projects.add(folder);
            }
            const auto order = [&projects] {
                QStringList out;
                for (const QVariant &v : projects.list())
                    out << v.toMap().value(u"name"_s).toString().section(u'-', 1);
                return out;
            };
            QCOMPARE(order(), (QStringList{u"Ala"_s, u"Bolek"_s, u"Celina"_s, u"Darek"_s}));
            // Pinned ones first.
            projects.setPinned(ids.at(2), true);
            QCOMPARE(order(), (QStringList{u"Celina"_s, u"Ala"_s, u"Bolek"_s, u"Darek"_s}));
            // A drag: within its part of the list.
            projects.move(ids.at(3), 0);
            QCOMPARE(order(), (QStringList{u"Celina"_s, u"Darek"_s, u"Ala"_s, u"Bolek"_s}));
            projects.move(ids.at(2), 3);
            QCOMPARE(order(), (QStringList{u"Celina"_s, u"Darek"_s, u"Ala"_s, u"Bolek"_s}));
            projects.move(ids.at(0), 3);
            QCOMPARE(order(), (QStringList{u"Celina"_s, u"Darek"_s, u"Bolek"_s, u"Ala"_s}));
            projects.setPinned(ids.at(2), false);
            QCOMPARE(order(), (QStringList{u"Celina"_s, u"Darek"_s, u"Bolek"_s, u"Ala"_s}));
            // Icons: one grapheme.
            projects.setIcon(ids.at(0), u" 🦊 lis"_s);
            QCOMPARE(projects.project(ids.at(0)).value(u"icon"_s).toString(), u"🦊"_s);
            projects.setIcon(ids.at(1), u"👩‍💻x"_s);
            QCOMPARE(projects.project(ids.at(1)).value(u"icon"_s).toString(), u"👩‍💻"_s);
            projects.setIcon(ids.at(2), u"\u001bż"_s);
            QCOMPARE(projects.project(ids.at(2)).value(u"icon"_s).toString(), QString());
            projects.setIcon(ids.at(3), u"Ż"_s);
            QCOMPARE(projects.project(ids.at(3)).value(u"icon"_s).toString(), u"Ż"_s);
            // Defaults for new threads.
            projects.setDefaults(ids.at(0), u"acp:codex"_s, u"worktree"_s);
            projects.setDefaults(ids.at(1), QString(), u"chmura"_s);
            QCOMPARE(projects.project(ids.at(0)).value(u"agent"_s).toString(), u"acp:codex"_s);
            QCOMPARE(projects.newThreadEnv(ids.at(0)), u"worktree"_s);
            QCOMPARE(projects.newThreadEnv(ids.at(1)), u"local"_s);
            projects.setNewThreadEnvDefault(u"worktree"_s);
            QCOMPARE(projects.newThreadEnv(ids.at(1)), u"worktree"_s);
            projects.setDefaults(ids.at(1), QString(), u"local"_s);
            QCOMPARE(projects.newThreadEnv(ids.at(1)), u"local"_s);
            // The device's settings.
            projects.setBranchPrefix(u"ala/"_s);
            projects.setBranchPrefix(u"zła nazwa"_s); // (no space in a branch: the one before stays)
            QCOMPARE(projects.branchPrefix(), u"ala/"_s);
            projects.setDeletedWorktrees(u"delete"_s);
            projects.setThreadSort(u"created"_s);
            projects.setDeletedWorktrees(u"cokolwiek"_s);
            QCOMPARE(projects.deletedWorktrees(), u"ask"_s);
            projects.setDeletedWorktrees(u"keep"_s);
        }
        CodeProjects again;
        again.setStorage([&kept] { return kept; }, [](const QByteArray &) {});
        QCOMPARE(again.list().first().toMap().value(u"name"_s).toString().section(u'-', 1), u"Celina"_s);
        QCOMPARE(again.list().at(3).toMap().value(u"icon"_s).toString(), u"🦊"_s);
        QCOMPARE(again.branchPrefix(), u"ala/"_s);
        QCOMPARE(again.deletedWorktrees(), u"keep"_s);
        QCOMPARE(again.threadSort(), u"created"_s);
        QCOMPARE(again.newThreadEnvDefault(), u"worktree"_s);
        const QString id = again.list().at(3).toMap().value(u"id"_s).toString();
        QCOMPARE(again.project(id).value(u"agent"_s).toString(), u"acp:codex"_s);
        QVERIFY(again.worktreeFor(id, u"Napraw błąd"_s).value(u"branch"_s).toString().startsWith(u"ala/napraw-blad"_s));
        // A worktree's folder belongs to its project.
        QCOMPARE(again.projectOfFolder(again.worktreeFor(id, u"x"_s).value(u"folder"_s).toString()), id);
        QCOMPARE(again.projectOfFolder(again.folderOf(id) + u"/"_s), id);
        QVERIFY(again.projectOfFolder(m_tmp.path()).isEmpty());
        // Broken stored values: defaults.
        again.setStorage([] { return QByteArray(R"({"projects":[{"id":"a","path":"/a","pinned":"tak","icon":7,
            "env":"chmura","scripts":[{"name":"x"},{"command":"y","icon":"zły"}]}],"branchPrefix":"a b",
            "deletedWorktrees":3,"threadSort":"losowo"})"); },
                         [](const QByteArray &) {});
        QCOMPARE(again.count(), 1);
        QCOMPARE(again.branchPrefix(), u"brulion/"_s);
        QCOMPARE(again.deletedWorktrees(), u"ask"_s);
        QCOMPARE(again.threadSort(), u"activity"_s);
        QVERIFY(!again.project(u"a"_s).value(u"pinned"_s).toBool());
        QCOMPARE(again.scripts(u"a"_s).size(), 1);
        QCOMPARE(again.scripts(u"a"_s).first().toMap().value(u"icon"_s).toString(), u"play"_s);
    }

    // ---- "Open in…" ------------------------------------------------------------------------------------

    void editorArguments_data()
    {
        QTest::addColumn<QString>("editor");
        QTest::addColumn<int>("line");
        QTest::addColumn<QStringList>("args"); // "%f" the file
        QTest::newRow("code") << u"code"_s << 12 << QStringList{u"--goto"_s, u"%f:12"_s};
        QTest::newRow("cursor") << u"cursor"_s << 3 << QStringList{u"--goto"_s, u"%f:3"_s};
        QTest::newRow("codium") << u"codium"_s << 3 << QStringList{u"--goto"_s, u"%f:3"_s};
        QTest::newRow("zed") << u"zed"_s << 7 << QStringList{u"%f:7"_s};
        QTest::newRow("sublime") << u"subl"_s << 7 << QStringList{u"%f:7"_s};
        QTest::newRow("idea") << u"idea"_s << 40 << QStringList{u"--line"_s, u"40"_s, u"%f"_s};
        QTest::newRow("rustrover") << u"rustrover"_s << 1 << QStringList{u"--line"_s, u"1"_s, u"%f"_s};
        QTest::newRow("kate") << u"kate"_s << 5 << QStringList{u"--line"_s, u"5"_s, u"%f"_s};
        QTest::newRow("gedit") << u"gedit"_s << 5 << QStringList{u"+5"_s, u"%f"_s};
        QTest::newRow("no line") << u"code"_s << 0 << QStringList{u"%f"_s};
        QTest::newRow("gnome: no lines") << u"gnome-text-editor"_s << 9 << QStringList{u"%f"_s};
    }
    void editorArguments()
    {
        QFETCH(QString, editor);
        QFETCH(int, line);
        QFETCH(QStringList, args);
        const QString file = m_tmp.filePath(u"plik żółwia.cpp"_s);
        QVERIFY(writeFile(file, "int main() {}\n"));
        QStringList expected;
        for (const QString &a : args)
            expected << QString(a).replace(u"%f"_s, file);
        QCOMPARE(CodeProjects::editorArguments(editor, file, line), expected);
        // A folder never gets a line.
        QCOMPARE(CodeProjects::editorArguments(editor, m_tmp.path(), line), QStringList{m_tmp.path()});
    }

    void editorsFound()
    {
#ifdef Q_OS_WIN
        QSKIP("fake programs are shell scripts");
#endif
        const QString bin = fresh(u"bin"_s);
        const QString log = m_tmp.filePath(u"editor-%1.log"_s.arg(m_n));
        // (Each fake writes its arguments to the log.)
        for (const QString &name : {u"zeditor"_s, u"idea"_s, u"code"_s}) {
            QVERIFY(writeFile(bin + u'/' + name, "#!/bin/sh\nprintf '%s\\n' \"" + name.toUtf8() + "\" \"$@\" > '"
                                                         + log.toUtf8() + "'\n"));
            QFile(bin + u'/' + name).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                     | QFileDevice::ExeOwner);
        }
        CodeProjects projects;
        QSignalSpy changed(&projects, &CodeProjects::editorsChanged);
        projects.setEditorSearchPaths({bin});
        QStringList ids;
        for (const QVariant &v : projects.editors())
            ids << v.toMap().value(u"id"_s).toString();
        // In Brulion's order, then the file manager.
        QCOMPARE(ids, (QStringList{u"code"_s, u"zed"_s, u"idea"_s, u"files"_s}));
        QCOMPARE(projects.effectiveEditor(), u"code"_s);
        projects.setPreferredEditor(u"zed"_s);
        QCOMPARE(projects.effectiveEditor(), u"zed"_s);
        projects.setPreferredEditor(u"sublime"_s); // (not here: the first one found)
        QCOMPARE(projects.effectiveEditor(), u"code"_s);
        // A custom command: offered, and the one used when none is chosen.
        projects.setPreferredEditor(QString());
        projects.setEditorCommand(u"code --new-window"_s);
        QCOMPARE(projects.effectiveEditor(), u"custom"_s);
        QVERIFY(projects.editors().at(3).toMap().value(u"name"_s).toString() == u"code --new-window"_s);
        QVERIFY(changed.size() >= 4);
        // Opening a file at a line: the editor gets its arguments.
        const QString file = m_tmp.filePath(u"main %1.cpp"_s.arg(m_n));
        QVERIFY(writeFile(file, "x\n"));
        QVERIFY(projects.openWith(u"zed"_s, file, 3));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(log).size() > 0, 5000);
        QFile f(log);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), u"zeditor\n%1:3\n"_s.arg(file));
        QVERIFY(QFile::remove(log));
        QVERIFY(projects.openWith(u"custom"_s, m_tmp.path(), -1));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(log).size() > 0, 5000);
        QFile g(log);
        QVERIFY(g.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(g.readAll()), u"code\n--new-window\n%1\n"_s.arg(QDir::cleanPath(m_tmp.path())));
        // Nothing to open, or an editor that is not here.
        QVERIFY(!projects.openWith(u"code"_s, m_tmp.filePath(u"nie ma"_s), 1));
        QVERIFY(!projects.openWith(u"rider"_s, file, 1));
    }

    void pullRequestNumber_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("number");
        QTest::newRow("hash") << u"#123"_s << 123;
        QTest::newRow("bare") << u" 42 "_s << 42;
        QTest::newRow("url") << u"https://github.com/pingdotgg/t3code/pull/987"_s << 987;
        QTest::newRow("url, files") << u"https://github.com/a/b/pull/5/files#diff-1"_s << 5;
        QTest::newRow("url, query") << u"https://github.example.com/org/repo/pull/77?w=1"_s << 77;
        QTest::newRow("zero") << u"#0"_s << 0;
        QTest::newRow("a branch") << u"feature/123"_s << 0;
        QTest::newRow("words") << u"napraw #12 teraz"_s << 0;
        QTest::newRow("issue url") << u"https://github.com/a/b/issues/5"_s << 0;
        QTest::newRow("too long") << u"12345678901"_s << 0;
        QTest::newRow("empty") << QString() << 0;
    }
    void pullRequestNumber()
    {
        QFETCH(QString, text);
        QFETCH(int, number);
        QCOMPARE(CodeProjects::parsePullRequest(text), number);
    }

    // ---- git: worktrees, branches, pull requests --------------------------------------------------------

    void worktreesAndBranches()
    {
        if (!m_git)
            QSKIP("no git");
        // A "remote" with a branch of its own and a pull request's ref, and a clone of it.
        const QString origin = fresh(u"origin"_s);
        QVERIFY(QDir().mkpath(origin));
        QCOMPARE(git(origin, {u"init"_s, u"-q"_s}), 0);
        QVERIFY(writeFile(origin + u"/README.md"_s, "# Kalkulator\n"));
        QCOMPARE(git(origin, {u"add"_s, u"-A"_s}), 0);
        QCOMPARE(git(origin, {u"commit"_s, u"-qm"_s, u"start"_s}), 0);
        QCOMPARE(git(origin, {u"switch"_s, u"-qc"_s, u"feature"_s}), 0);
        QVERIFY(writeFile(origin + u"/feature.txt"_s, "f\n"));
        QCOMPARE(git(origin, {u"add"_s, u"-A"_s}), 0);
        QCOMPARE(git(origin, {u"commit"_s, u"-qm"_s, u"feature"_s}), 0);
        QCOMPARE(git(origin, {u"switch"_s, u"-qc"_s, u"pr7"_s, u"main"_s}), 0);
        QVERIFY(writeFile(origin + u"/pr.txt"_s, "z pull requesta\n"));
        QCOMPARE(git(origin, {u"add"_s, u"-A"_s}), 0);
        QCOMPARE(git(origin, {u"commit"_s, u"-qm"_s, u"pr 7"_s}), 0);
        QCOMPARE(git(origin, {u"update-ref"_s, u"refs/pull/7/head"_s, u"pr7"_s}), 0);
        QCOMPARE(git(origin, {u"switch"_s, u"-q"_s, u"main"_s}), 0);
        const QString repo = fresh(u"Kalkulator"_s);
        QCOMPARE(git(m_tmp.path(), {u"clone"_s, u"-q"_s, origin, repo}), 0);

        CodeProjects projects;
        projects.setPrograms(QStandardPaths::findExecutable(u"git"_s), QString()); // (no gh)
        projects.setWorktreesRoot(fresh(u"drzewa"_s));
        const QString id = projects.add(repo);
        QVERIFY(!projects.ghAvailable() && projects.gitAvailable());

        // A worktree from the project's HEAD; then one whose name is taken by a branch already.
        QSignalSpy created(&projects, &CodeProjects::worktreeCreated);
        QCOMPARE(git(repo, {u"branch"_s, u"brulion/dodaj-rabat"_s}), 0);
        QVERIFY(projects.createWorktree(id, u"Dodaj rabat"_s));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 20000);
        QCOMPARE(created.at(0).at(3).toString(), QString());
        const QString first = created.at(0).at(1).toString();
        QCOMPARE(created.at(0).at(2).toString(), u"brulion/dodaj-rabat-2"_s);
        QVERIFY(QFileInfo(first + u"/README.md"_s).isFile());
        QCOMPARE(gitOut(first, {u"branch"_s, u"--show-current"_s}), u"brulion/dodaj-rabat-2"_s);
        // From a remote branch (fetched first), with another prefix.
        projects.setBranchPrefix(u"ala/"_s);
        QVERIFY(projects.createWorktree(id, u"Na bazie feature"_s, u"origin/feature"_s));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 2, 20000);
        QCOMPARE(created.at(1).at(3).toString(), QString());
        const QString second = created.at(1).at(1).toString();
        QCOMPARE(created.at(1).at(2).toString(), u"ala/na-bazie-feature"_s);
        QVERIFY(QFileInfo(second + u"/feature.txt"_s).isFile());
        // A base that is no branch fails, said.
        QVERIFY(projects.createWorktree(id, u"Zła baza"_s, u"nie-ma-takiej"_s));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 3, 20000);
        QVERIFY(!created.at(2).at(3).toString().isEmpty());
        QVERIFY(projects.createWorktree(id, u"Opcja"_s, u"--orphan"_s));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 4, 20000);
        QVERIFY(!created.at(3).at(3).toString().isEmpty());

        // The worktrees of the project.
        QSignalSpy listed(&projects, &CodeProjects::worktreesListed);
        QVERIFY(projects.listWorktrees(id));
        QTRY_COMPARE_WITH_TIMEOUT(listed.size(), 1, 20000);
        const QVariantList trees = listed.at(0).at(1).toList();
        QCOMPARE(trees.size(), 2);
        QCOMPARE(trees.at(0).toMap().value(u"branch"_s).toString(), u"brulion/dodaj-rabat-2"_s);

        // The branches: badges.
        QSignalSpy branches(&projects, &CodeProjects::branchesListed);
        QVERIFY(projects.listBranches(repo));
        QTRY_COMPARE_WITH_TIMEOUT(branches.size(), 1, 20000);
        const QVariantList rows = branches.at(0).at(1).toList();
        const QVariantMap main = byName(rows, u"main"_s);
        QVERIFY(main.value(u"current"_s).toBool() && main.value(u"isDefault"_s).toBool());
        QVERIFY(!main.value(u"remote"_s).toBool());
        QCOMPARE(byName(rows, u"brulion/dodaj-rabat-2"_s).value(u"worktree"_s).toString(), QDir::cleanPath(first));
        QVERIFY(byName(rows, u"origin/main"_s).isEmpty()); // (main tracks it)
        QVERIFY(byName(rows, u"origin/pr7"_s).value(u"remote"_s).toBool());
        // Local ones before remote ones.
        bool remoteSeen = false;
        for (const QVariant &v : rows) {
            if (v.toMap().value(u"remote"_s).toBool())
                remoteSeen = true;
            else
                QVERIFY(!remoteSeen);
        }

        // Switch and create.
        QSignalSpy switched(&projects, &CodeProjects::branchSwitched);
        QVERIFY(projects.createBranch(repo, u"gałąź-ćwiczebna"_s));
        QTRY_COMPARE_WITH_TIMEOUT(switched.size(), 1, 20000);
        QVERIFY2(switched.at(0).at(2).toBool(), qPrintable(switched.at(0).at(3).toString()));
        QCOMPARE(gitOut(repo, {u"branch"_s, u"--show-current"_s}), u"gałąź-ćwiczebna"_s);
        QVERIFY(projects.switchBranch(repo, u"origin/feature"_s));
        QTRY_COMPARE_WITH_TIMEOUT(switched.size(), 2, 20000);
        QVERIFY2(switched.at(1).at(2).toBool(), qPrintable(switched.at(1).at(3).toString()));
        QCOMPARE(switched.at(1).at(1).toString(), u"feature"_s);
        QVERIFY(projects.createBranch(repo, u"zła nazwa"_s));
        QTRY_COMPARE_WITH_TIMEOUT(switched.size(), 3, 20000);
        QVERIFY(!switched.at(2).at(2).toBool());
        QVERIFY(!projects.validBranchName(u"zła nazwa"_s) && projects.validBranchName(u"gałąź/ćwiczebna"_s));
        QCOMPARE(git(repo, {u"switch"_s, u"-q"_s, u"main"_s}), 0);

        // A pull request without gh, in the project's folder: refused over uncommitted changes.
        QSignalSpy pr(&projects, &CodeProjects::pullRequestCheckedOut);
        QVERIFY(writeFile(repo + u"/README.md"_s, "# Zmieniony\n"));
        QVERIFY(projects.checkoutPullRequest(id, 7, false));
        QTRY_COMPARE_WITH_TIMEOUT(pr.size(), 1, 20000);
        QVERIFY(pr.at(0).at(5).toString().contains(u"uncommitted"_s));
        QCOMPARE(git(repo, {u"checkout"_s, u"--"_s, u"README.md"_s}), 0);
        // (An untracked file is no reason to refuse.)
        QVERIFY(writeFile(repo + u"/notatka.txt"_s, "moja\n"));
        QVERIFY(projects.checkoutPullRequest(id, 7, false));
        QTRY_COMPARE_WITH_TIMEOUT(pr.size(), 2, 20000);
        QVERIFY2(pr.at(1).at(5).toString().isEmpty(), qPrintable(pr.at(1).at(5).toString()));
        QCOMPARE(pr.at(1).at(2).toString(), QString());
        QCOMPARE(pr.at(1).at(3).toString(), u"ala/pr-7"_s);
        QVERIFY(QFileInfo(repo + u"/pr.txt"_s).isFile());
        QCOMPARE(git(repo, {u"switch"_s, u"-q"_s, u"main"_s}), 0);
        // In a new worktree (its branch taken locally already: another name).
        QVERIFY(projects.checkoutPullRequest(id, 7, true));
        QTRY_COMPARE_WITH_TIMEOUT(pr.size(), 3, 20000);
        QVERIFY2(pr.at(2).at(5).toString().isEmpty(), qPrintable(pr.at(2).at(5).toString()));
        const QString prTree = pr.at(2).at(2).toString();
        QCOMPARE(pr.at(2).at(3).toString(), u"ala/pr-7-2"_s);
        QVERIFY(QFileInfo(prTree + u"/pr.txt"_s).isFile());
        // A pull request that is not there.
        QVERIFY(projects.checkoutPullRequest(id, 99, true));
        QTRY_COMPARE_WITH_TIMEOUT(pr.size(), 4, 30000);
        QVERIFY(!pr.at(3).at(5).toString().isEmpty());

        // Checked, removed (forced over changes), made again from its branch.
        QSignalSpy checked(&projects, &CodeProjects::worktreeChecked);
        QVERIFY(writeFile(second + u"/nowy.txt"_s, "nowy\n"));
        QVERIFY(writeFile(second + u"/feature.txt"_s, "zmieniony\n"));
        QVERIFY(projects.checkWorktree(second));
        QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 20000);
        QCOMPARE(checked.at(0).at(1).toInt(), 2);
        QCOMPARE(checked.at(0).at(2).toString(), u"ala/na-bazie-feature"_s);
        QSignalSpy removed(&projects, &CodeProjects::worktreeRemoved);
        QVERIFY(projects.removeWorktree(id, second, false));
        QTRY_COMPARE_WITH_TIMEOUT(removed.size(), 1, 20000);
        QVERIFY(!removed.at(0).at(1).toBool()); // (git refuses without force)
        QVERIFY(projects.removeWorktree(id, second, true));
        QTRY_COMPARE_WITH_TIMEOUT(removed.size(), 2, 20000);
        QVERIFY2(removed.at(1).at(1).toBool(), qPrintable(removed.at(1).at(2).toString()));
        QVERIFY(!QFileInfo::exists(second));
        QCOMPARE(gitOut(repo, {u"branch"_s, u"--list"_s, u"ala/na-bazie-feature"_s}), u"ala/na-bazie-feature"_s);
        QVERIFY(!projects.removeWorktree(id, repo, true)); // (never the project's own folder)
        QVERIFY(projects.checkWorktree(second));
        QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 2, 20000);
        QCOMPARE(checked.at(1).at(1).toInt(), 0);
        // A deleted thread's Undo: the pages back, the worktree made again.
        QStringList restoredPages;
        projects.setRestorer([&restoredPages](const QStringList &paths) { restoredPages = paths; });
        QSignalSpy restored(&projects, &CodeProjects::worktreeRestored);
        projects.keepUndo(5, {u".trash/AI/wątek.md"_s}, id, second, u"ala/na-bazie-feature"_s, true);
        projects.keepUndo(6, {u".trash/AI/inny.md"_s}, id, second, u"x"_s, false);
        projects.forgetUndo(6);
        projects.undo(6);
        QVERIFY(restoredPages.isEmpty());
        projects.undo(5);
        QCOMPARE(restoredPages, QStringList{u".trash/AI/wątek.md"_s});
        QTRY_COMPARE_WITH_TIMEOUT(restored.size(), 1, 20000);
        QVERIFY2(restored.at(0).at(1).toBool(), qPrintable(restored.at(0).at(2).toString()));
        QVERIFY(QFileInfo(second + u"/feature.txt"_s).isFile());
        projects.undo(5); // (once)
        QCOMPARE(restored.size(), 1);
    }

    // With gh: `gh pr checkout` in a new detached worktree, its title from `gh pr view`.
    void pullRequestWithGh()
    {
#ifdef Q_OS_WIN
        QSKIP("the fake gh is a shell script");
#endif
        if (!m_git)
            QSKIP("no git");
        const QString origin = fresh(u"origin"_s);
        QVERIFY(QDir().mkpath(origin));
        QCOMPARE(git(origin, {u"init"_s, u"-q"_s}), 0);
        QVERIFY(writeFile(origin + u"/a.txt"_s, "a\n"));
        QCOMPARE(git(origin, {u"add"_s, u"-A"_s}), 0);
        QCOMPARE(git(origin, {u"commit"_s, u"-qm"_s, u"a"_s}), 0);
        QCOMPARE(git(origin, {u"switch"_s, u"-qc"_s, u"popraw-kalkulator"_s}), 0);
        QVERIFY(writeFile(origin + u"/b.txt"_s, "b\n"));
        QCOMPARE(git(origin, {u"add"_s, u"-A"_s}), 0);
        QCOMPARE(git(origin, {u"commit"_s, u"-qm"_s, u"b"_s}), 0);
        QCOMPARE(git(origin, {u"switch"_s, u"-q"_s, u"main"_s}), 0);
        const QString repo = fresh(u"repo"_s);
        QCOMPARE(git(m_tmp.path(), {u"clone"_s, u"-q"_s, origin, repo}), 0);
        // (gh as a script: "pr checkout 12" checks the branch out, "pr view 12 --json title" tells it.)
        const QString gh = fresh(u"gh"_s);
        QVERIFY(writeFile(gh, "#!/bin/sh\n"
                              "if [ \"$1 $2\" = \"pr checkout\" ]; then git fetch -q origin popraw-kalkulator && "
                              "git switch -q -c popraw-kalkulator --track origin/popraw-kalkulator; exit $?; fi\n"
                              "if [ \"$1 $2\" = \"pr view\" ]; then echo '{\"title\":\"Popraw kalkulator — ułamki\"}'; "
                              "exit 0; fi\nexit 1\n"));
        QFile(gh).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        CodeProjects projects;
        projects.setPrograms(QStandardPaths::findExecutable(u"git"_s), gh);
        projects.setWorktreesRoot(fresh(u"drzewa"_s));
        const QString id = projects.add(repo);
        QSignalSpy pr(&projects, &CodeProjects::pullRequestCheckedOut);
        QVERIFY(projects.checkoutPullRequest(id, 12, true));
        QTRY_COMPARE_WITH_TIMEOUT(pr.size(), 1, 30000);
        QVERIFY2(pr.at(0).at(5).toString().isEmpty(), qPrintable(pr.at(0).at(5).toString()));
        const QString folder = pr.at(0).at(2).toString();
        QVERIFY(folder.endsWith(u"/pr-12"_s));
        QCOMPARE(pr.at(0).at(3).toString(), u"popraw-kalkulator"_s);
        QCOMPARE(pr.at(0).at(4).toString(), u"Popraw kalkulator — ułamki"_s);
        QVERIFY(QFileInfo(folder + u"/b.txt"_s).isFile());
        // gh failing: the worktree made for it goes again.
        QVERIFY(writeFile(gh, "#!/bin/sh\necho 'no pull requests found' >&2\nexit 1\n"));
        QVERIFY(projects.checkoutPullRequest(id, 13, true));
        QTRY_COMPARE_WITH_TIMEOUT(pr.size(), 2, 30000);
        QVERIFY(pr.at(1).at(5).toString().contains(u"no pull requests found"_s));
        QVERIFY(!QFileInfo::exists(QDir(projects.worktreesRoot()).filePath(id + u"/pr-13"_s)));
    }
};

QTEST_GUILESS_MAIN(TestCodeProjects)
#include "tst_codeprojects.moc"
