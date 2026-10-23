// Remote control of the running app (src/app/shell/remotecontrol.h), end to end: the real app
// started offscreen on a small notebook in a private home, driven through `brulion-cli app …`
// (QProcess) and the control socket itself. Off by default: without --remote-control the CLI is
// refused (exit 5, remote_control_off, with a hint) and there is no socket. With it: the socket is
// owner-only (0600); hello and protocol errors (no JSON, an unknown method, bad parameters); state
// (status, notebook, route, window, language; no secrets); navigate; open by name, by line ref,
// today, a missing page and a missing heading (its toast in lastToasts); find and click (by handle
// and by text) in the sidebar; a click into a block, typing Polish letters key by key and the page
// saved with them; Ctrl+K opening the command palette, Escape closing it; the palette's commands
// (run, unknown, disabled); quick capture with a date; waitFor (a timeout, idle); screenshots (a
// PNG of the window's size, where asked, relative paths, the runtime folder by default, a refused
// path); scroll, long press, double click and drag; a page with a conflict (in state, never
// idle until the user decides); the MCP server's app tools (--app-control), a screenshot as an
// image; quit, after which nothing listens any more. The settings switch turns it off, in a
// window where the page fits and in a low one (a finger scrolling over the switch never taps it). Without a private
// runtime folder the app still starts, with neither socket in the shared temp folder. The app's log must hold no QML
// TypeError, ReferenceError or binding loop.
#include "controlclient.h"
#include "controlprotocol.h"
#include "singleinstance.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <functional>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

constexpr int kSlowdown = BRULION_TEST_SLOWDOWN;
constexpr int kStartMs = 90000 * kSlowdown;
constexpr int kCallMs = 60000 * kSlowdown;

struct Run {
    int code = -1;
    QJsonObject json;
    QByteArray out;
    QByteArray err;
    QJsonObject result() const { return json.value(u"result"_s).toObject(); }
    QString errorCode() const { return json.value(u"error"_s).toObject().value(u"code"_s).toString(); }
    QString describe() const { return QString::fromUtf8(out + err).left(2000); }
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

/// Width and height of a PNG file (its IHDR); (-1, -1) when it is no PNG.
QSize pngSize(const QByteArray &bytes)
{
    if (!bytes.startsWith("\x89PNG\r\n\x1a\n") || bytes.size() < 24 || bytes.mid(12, 4) != "IHDR")
        return {-1, -1};
    return {int(qFromBigEndian<quint32>(bytes.constData() + 16)), int(qFromBigEndian<quint32>(bytes.constData() + 20))};
}

} // namespace

class TestRemoteControl : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    QString m_home;
    QString m_vault;
    QString m_log;
    QProcess m_app;

    QProcessEnvironment environment() const
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"HOME"_s, m_home);
        env.insert(u"XDG_CONFIG_HOME"_s, m_home + u"/config"_s);
        env.insert(u"XDG_DATA_HOME"_s, m_home + u"/data"_s);
        env.insert(u"XDG_STATE_HOME"_s, m_home + u"/state"_s);
        env.insert(u"XDG_CACHE_HOME"_s, m_home + u"/cache"_s);
        env.insert(u"XDG_RUNTIME_DIR"_s, m_home + u"/run"_s);
        env.insert(u"QT_QPA_PLATFORM"_s, u"offscreen"_s);
        env.insert(u"QT_QUICK_BACKEND"_s, u"software"_s);
        env.insert(u"QT_FORCE_STDERR_LOGGING"_s, u"1"_s);
        env.insert(u"QML_IMPORT_PATH"_s, QStringLiteral(BRULION_QML_ROOT));
        env.insert(u"LANG"_s, u"C.UTF-8"_s);
        env.remove(u"BRULION_VAULT"_s);
        return env;
    }

    QString singleInstanceKey() const
    {
        return brulion::ipc::SingleInstance::keyFor(m_home + u"/data/Brulion/Brulion"_s);
    }
    QString controlKey() const { return brulion::ipc::controlKeyFor(singleInstanceKey()); }

    Run cli(const QStringList &args, const QString &workingDir = QString())
    {
        QProcess p;
        p.setProcessEnvironment(environment());
        if (!workingDir.isEmpty())
            p.setWorkingDirectory(workingDir);
        p.start(QStringLiteral(BRULION_CLI), args);
        Run r;
        if (!p.waitForStarted(kCallMs))
            return r;
        p.closeWriteChannel();
        if (!p.waitForFinished(kCallMs)) {
            p.kill();
            return r;
        }
        r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -2;
        r.out = p.readAllStandardOutput();
        r.err = p.readAllStandardError();
        r.json = QJsonDocument::fromJson(r.out).object();
        return r;
    }
    /// `brulion-cli app <args…>`.
    Run app(QStringList args, const QString &workingDir = QString())
    {
        args.prepend(u"app"_s);
        return cli(args, workingDir);
    }
    /// Repeat `app <args>` until `done` holds (true) or the time is up.
    bool until(const QStringList &args, const std::function<bool(const Run &)> &done, int timeoutMs,
               Run *last = nullptr)
    {
        QElapsedTimer t;
        t.start();
        Run r;
        do {
            r = app(args);
            if (done(r)) {
                if (last)
                    *last = r;
                return true;
            }
            QTest::qWait(250);
        } while (t.elapsed() < timeoutMs);
        if (last)
            *last = r;
        return false;
    }

    void startApp(bool remoteControl, const QString &size = u"1280x800"_s)
    {
        QStringList args{u"--vault"_s,          m_vault,       u"--size"_s, size, u"--now"_s,
                         u"2026-10-14 10:00"_s, u"--locale"_s, u"pl"_s};
        if (remoteControl)
            args.append(u"--remote-control"_s);
        m_app.setProcessEnvironment(environment());
        // A file, not a pipe: nobody reads a pipe while the test waits for the command line.
        m_app.setStandardOutputFile(m_log, QIODevice::Append);
        m_app.setStandardErrorFile(m_log, QIODevice::Append);
        m_app.start(QStringLiteral(BRULION_APP), args);
        QVERIFY(m_app.waitForStarted(kStartMs));
    }
    void stopApp()
    {
        if (m_app.state() == QProcess::NotRunning)
            return;
        m_app.terminate();
        if (!m_app.waitForFinished(20000 * kSlowdown)) {
            m_app.kill();
            m_app.waitForFinished();
        }
    }
    QString vaultFile(const QString &path) const { return m_vault + u'/' + path; }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_home = m_tmp.filePath(u"dom"_s);
        QDir().mkpath(m_home + u"/run"_s);
        QFile::setPermissions(m_home + u"/run"_s,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        // The socket names are computed here as in the app: the same runtime folder.
        qputenv("XDG_RUNTIME_DIR", (m_home + u"/run"_s).toUtf8());
        m_vault = m_tmp.filePath(u"Notatnik żółwia"_s);
        writeFile(vaultFile(u"Test.md"_s), "- Pierwszy blok\n- Drugi blok\n");
        writeFile(vaultFile(u"Projekty/Remont mieszkania.md"_s), "# Remont\n- Kupić farbę\n");
        writeFile(vaultFile(u"Konflikt.md"_s), "- pierwsza linia\n- druga linia\n");
        m_log = m_tmp.filePath(u"app.log"_s);
        QVERIFY(QFileInfo::exists(QStringLiteral(BRULION_APP)));
        QVERIFY(QFileInfo::exists(QStringLiteral(BRULION_CLI)));
    }

    void cleanupTestCase()
    {
        stopApp();
        // QML problems anywhere in the run fail it (as in the other QML tests).
        const QString log = QString::fromUtf8(readFile(m_log));
        for (const QString &bad : {u"TypeError"_s, u"ReferenceError"_s, u"Binding loop detected"_s}) {
            const qsizetype at = log.indexOf(bad);
            QVERIFY2(at < 0, qPrintable(log.mid(qMax<qsizetype>(0, at - 300), 800)));
        }
    }

    void offByDefault()
    {
        Run r = app({u"state"_s});
        QCOMPARE(r.code, 5);
        QCOMPARE(r.errorCode(), u"app_not_running"_s);
        startApp(false);
        // Running (its single-instance socket answers), but without remote control.
        QVERIFY2(until(
                         {u"state"_s}, [](const Run &x) { return x.errorCode() == u"remote_control_off"; }, kStartMs,
                         &r),
                 qPrintable(r.describe()));
        QCOMPARE(r.code, 5);
        const QJsonObject details = r.json.value(u"error"_s).toObject().value(u"details"_s).toObject();
        QVERIFY(details.value(u"hint"_s).toString().contains(u"--remote-control"_s));
        QVERIFY(details.value(u"gui"_s).toObject().value(u"running"_s).toBool());
        QVERIFY(!QFileInfo::exists(controlKey()));
        QVERIFY(!brulion::ipc::ControlClient(controlKey()).connect(300));
        stopApp();
    }

    void startsWithRemoteControl()
    {
        startApp(true);
        Run r;
        QVERIFY2(until(
                         {u"state"_s},
                         [](const Run &x) { return x.code == 0 && x.result().value(u"ready"_s).toBool(); }, kStartMs,
                         &r),
                 qPrintable(r.describe()));
        // Owner only: read and write for the user, nothing for anybody else.
        struct stat st{};
        QCOMPARE(::stat(QFile::encodeName(controlKey()).constData(), &st), 0);
        QVERIFY(S_ISSOCK(st.st_mode));
        QCOMPARE(int(st.st_mode & 0777), 0600);
    }

    void protocol()
    {
        brulion::ipc::ControlClient client(controlKey());
        QVERIFY(client.connect());
        auto response = client.call(u"hello"_s);
        QVERIFY2(response.ok, qPrintable(response.message));
        const QJsonObject hello = response.result.toObject();
        QCOMPARE(hello.value(u"protocol"_s).toInt(), brulion::ipc::ControlProtocolVersion);
        QCOMPARE(hello.value(u"app"_s).toString(), u"Brulion"_s);
        QVERIFY(hello.value(u"methods"_s).toArray().contains(u"waitFor"_s));
        response = client.call(u"explode"_s);
        QCOMPARE(response.code, u"unknown_method"_s);
        response = client.call(u"click"_s, {{u"handle"_s, u"h1"_s}, {u"x"_s, 3}});
        QCOMPARE(response.code, u"bad_params"_s);
        response = client.call(u"click"_s, {{u"handle"_s, u"h999999"_s}});
        QCOMPARE(response.code, u"not_found"_s);
        QLocalSocket raw;
        raw.connectToServer(controlKey());
        QVERIFY(raw.waitForConnected(5000));
        raw.write("{nie json\n");
        QVERIFY(raw.waitForReadyRead(kCallMs));
        const QJsonObject error = QJsonDocument::fromJson(raw.readLine()).object();
        QCOMPARE(error.value(u"error"_s).toObject().value(u"code"_s).toString(), u"parse_error"_s);
    }

    void state()
    {
        const Run r = app({u"state"_s});
        QCOMPARE(r.code, 0);
        const QJsonObject s = r.result();
        QCOMPARE(s.value(u"status"_s).toString(), u"ready"_s);
        QCOMPARE(QDir(s.value(u"vault"_s).toObject().value(u"path"_s).toString()).canonicalPath(),
                 QDir(m_vault).canonicalPath());
        QCOMPARE(s.value(u"vault"_s).toObject().value(u"name"_s).toString(), u"Notatnik żółwia"_s);
        QCOMPARE(s.value(u"currentRoute"_s).toObject().value(u"view"_s).toString(), u"journals"_s);
        QCOMPARE(s.value(u"window"_s).toObject().value(u"width"_s).toInt(), 1280);
        QCOMPARE(s.value(u"window"_s).toObject().value(u"height"_s).toInt(), 800);
        QCOMPARE(s.value(u"language"_s).toString(), u"pl"_s);
        QVERIFY(s.value(u"theme"_s).toString() == u"light" || s.value(u"theme"_s).toString() == u"dark");
        QVERIFY(s.value(u"lastToasts"_s).isArray());
        QVERIFY(s.value(u"openPopups"_s).toArray().isEmpty());
        // Nothing secret in it.
        const QString text = QString::fromUtf8(QJsonDocument(s).toJson()).toLower();
        QVERIFY(!text.contains(u"password"_s));
        QVERIFY(!text.contains(u"token"_s));
    }

    void navigate()
    {
        Run r = app({u"navigate"_s, u"tasks"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"route"_s).toObject().value(u"view"_s).toString(), u"tasks"_s);
        r = app({u"wait"_s, u"--route"_s, u"tasks"_s});
        QCOMPARE(r.code, 0);
        r = app({u"navigate"_s, uR"({"view":"calendar","date":"2026-10-20"})"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"route"_s).toObject().value(u"date"_s).toString(), u"2026-10-20"_s);
        r = app({u"navigate"_s, u"nigdzie"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"bad_params"_s);
        r = app({u"navigate"_s, u"{nie json"_s});
        QCOMPARE(r.code, 2);
    }

    void open()
    {
        Run r = app({u"open"_s, u"Remont mieszkania"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"via"_s).toString(), u"control"_s);
        QCOMPARE(r.result().value(u"route"_s).toObject().value(u"path"_s).toString(),
                 u"Projekty/Remont mieszkania.md"_s);
        r = app({u"wait"_s, u"--route"_s, uR"({"view":"page","path":"Projekty/Remont mieszkania.md"})"_s});
        QCOMPARE(r.code, 0);
        r = app({u"open"_s, u"Test.md#L2"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"route"_s).toObject().value(u"line"_s).toInt(), 1);
        r = app({u"open"_s, u"today"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"route"_s).toObject().value(u"date"_s).toString(), u"2026-10-14"_s);
        r = app({u"open"_s, u"Nieistniejąca strona"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"not_found"_s);
        // A heading the page does not have: the page opens at its top with a toast.
        r = app({u"open"_s, u"Test"_s, u"--heading"_s, u"Brak takiego nagłówka"_s});
        QCOMPARE(r.code, 0);
        Run s;
        QVERIFY2(until(
                         {u"state"_s},
                         [](const Run &x) { return !x.result().value(u"lastToasts"_s).toArray().isEmpty(); },
                         10000 * kSlowdown, &s),
                 qPrintable(s.describe()));
        const QJsonObject toast = s.result().value(u"lastToasts"_s).toArray().last().toObject();
        QVERIFY(!toast.value(u"text"_s).toString().isEmpty());
        QVERIFY(!toast.value(u"at"_s).toString().isEmpty());
    }

    void findAndClick()
    {
        QCOMPARE(app({u"navigate"_s, u"journals"_s}).code, 0);
        QCOMPARE(app({u"wait"_s, u"--idle"_s, u"--timeout"_s, QString::number(20000 * kSlowdown)}).code, 0);
        Run r = app({u"find"_s, u"Zadania"_s, u"--exact"_s});
        QCOMPARE(r.code, 0);
        const QJsonArray items = r.result().value(u"items"_s).toArray();
        QVERIFY2(!items.isEmpty(), qPrintable(r.describe()));
        const QJsonObject item = items.first().toObject();
        QVERIFY(item.value(u"handle"_s).toString().startsWith(u'h'));
        QVERIFY(item.value(u"visible"_s).toBool());
        QVERIFY(item.value(u"geometry"_s).toObject().value(u"width"_s).toDouble() > 0);
        // The same item keeps its handle.
        const Run again = app({u"find"_s, u"Zadania"_s, u"--exact"_s});
        QCOMPARE(again.result().value(u"items"_s).toArray().first().toObject().value(u"handle"_s),
                 item.value(u"handle"_s));
        r = app({u"click"_s, item.value(u"handle"_s).toString()});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QCOMPARE(app({u"wait"_s, u"--route"_s, u"tasks"_s}).code, 0);
        r = app({u"click"_s, u"--text"_s, u"Kalendarz"_s, u"--exact"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(app({u"wait"_s, u"--route"_s, u"calendar"_s}).code, 0);
        r = app({u"click"_s, u"Nie ma takiego przycisku"_s});
        QCOMPARE(r.code, 3);
        r = app({u"click"_s});
        QCOMPARE(r.code, 2);
        r = app({u"find"_s, u"--type"_s, u"BIconButton"_s, u"--limit"_s, u"3"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"items"_s).toArray().size() <= 3);
        QVERIFY(r.result().value(u"total"_s).toInt() >= r.result().value(u"items"_s).toArray().size());
    }

    void typePolishTextIntoABlock()
    {
        QCOMPARE(app({u"open"_s, u"Test"_s}).code, 0);
        QCOMPARE(app({u"wait"_s, u"--route"_s, uR"({"view":"page","path":"Test.md"})"_s}).code, 0);
        QCOMPARE(app({u"wait"_s, u"--text"_s, u"Pierwszy blok"_s, u"--exact"_s}).code, 0);
        Run r = app({u"click"_s, u"--text"_s, u"Pierwszy blok"_s, u"--exact"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        Run s;
        QVERIFY2(until(
                         {u"state"_s},
                         [](const Run &x) {
                             return x.result().value(u"editing"_s).toObject().value(u"page"_s).toString() == u"Test.md";
                         },
                         10000 * kSlowdown, &s),
                 qPrintable(s.describe()));
        QCOMPARE(s.result().value(u"editing"_s).toObject().value(u"row"_s).toInt(), 0);
        QCOMPARE(app({u"key"_s, u"End"_s}).code, 0);
        const QString typed = u" — zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ"_s;
        r = app({u"type"_s, typed});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QCOMPARE(r.result().value(u"keys"_s).toInt(), typed.size());
        QCOMPARE(app({u"wait"_s, u"--idle"_s, u"--timeout"_s, QString::number(20000 * kSlowdown)}).code, 0);
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromUtf8(readFile(vaultFile(u"Test.md"_s))),
                                  u"- Pierwszy blok — zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ\n- Drugi blok\n"_s,
                                  10000 * kSlowdown);
        s = app({u"state"_s});
        QCOMPARE(s.result().value(u"editing"_s).toObject().value(u"text"_s).toString(),
                 u"Pierwszy blok — zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ"_s);
        QCOMPARE(app({u"key"_s, u"Escape"_s}).code, 0);
    }

    void ctrlKOpensThePalette()
    {
        QCOMPARE(app({u"key"_s, u"Escape, Escape"_s}).code, 0);
        Run r = app({u"key"_s, u"Ctrl+K"_s});
        QCOMPARE(r.code, 0);
        const auto paletteOpen = [](const Run &x) {
            for (const QJsonValue &p : x.result().value(u"openPopups"_s).toArray()) {
                if (p.toObject().value(u"objectName"_s).toString() == u"shellPalette")
                    return true;
            }
            return false;
        };
        Run s;
        QVERIFY2(until({u"state"_s}, paletteOpen, 10000 * kSlowdown, &s), qPrintable(s.describe()));
        // The palette's field has the keyboard: typing filters it (every letter: typing waits for
        // the popup to give its field the keyboard).
        r = app({u"type"_s, u"kalendarz"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"focusedItem"_s).toObject().value(u"text"_s).toString(), u"kalendarz"_s);
        // Escape clears the query, a second one closes the palette.
        r = app({u"key"_s, u"Escape"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"focusedItem"_s).toObject().value(u"text"_s).toString(), QString());
        QVERIFY(paletteOpen(app({u"state"_s})));
        QCOMPARE(app({u"key"_s, u"Escape"_s}).code, 0);
        QVERIFY(until({u"state"_s}, [&](const Run &x) { return x.code == 0 && !paletteOpen(x); }, 10000 * kSlowdown));
    }

    void commands()
    {
        Run r = app({u"commands"_s});
        QCOMPARE(r.code, 0);
        QStringList ids;
        for (const QJsonValue &c : r.result().value(u"commands"_s).toArray())
            ids.append(c.toObject().value(u"id"_s).toString());
        QVERIFY(ids.contains(u"go.calendar"_s));
        QVERIFY(ids.contains(u"nav.forward"_s));
        r = app({u"command"_s, u"go.tasks"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(app({u"wait"_s, u"--route"_s, u"tasks"_s}).code, 0);
        r = app({u"command"_s, u"nie.ma.takiego"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"not_found"_s);
        r = app({u"command"_s, u"nav.forward"_s}); // nothing to go forward to
        QCOMPARE(r.code, 7);
        QCOMPARE(r.errorCode(), u"refused"_s);
    }

    void capture()
    {
        const Run r = app({u"capture"_s, u"Kupić mleko jutro o 18"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QCOMPARE(r.result().value(u"kind"_s).toString(), u"reminder"_s);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"journals/2026-10-14.md"_s);
        QTRY_VERIFY_WITH_TIMEOUT(
                readFile(vaultFile(u"journals/2026-10-14.md"_s)).contains("- [ ] Kupić mleko ⏰ 2026-10-15 18:00"),
                10000 * kSlowdown);
    }

    void waitFor()
    {
        Run r = app({u"wait"_s, u"--text"_s, u"Tego tekstu nie ma w oknie"_s, u"--timeout"_s, u"400"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"timeout"_s);
        r = app({u"wait"_s, u"--idle"_s, u"--timeout"_s, QString::number(20000 * kSlowdown)});
        QCOMPARE(r.code, 0);
        r = app({u"wait"_s, u"--text"_s, u"Tego tekstu nie ma w oknie"_s, u"--gone"_s});
        QCOMPARE(r.code, 0);
        r = app({u"wait"_s});
        QCOMPARE(r.code, 2);
    }

    void screenshots()
    {
        const QString file = m_tmp.filePath(u"zrzut ekranu.png"_s);
        Run r = app({u"screenshot"_s, file});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QCOMPARE(r.result().value(u"path"_s).toString(), file);
        QCOMPARE(pngSize(readFile(file)), QSize(1280, 800));
        // A relative path is where the command runs.
        const QString cwd = m_tmp.filePath(u"tutaj"_s);
        QDir().mkpath(cwd);
        r = app({u"screenshot"_s, u"okno.png"_s}, cwd);
        QCOMPARE(r.code, 0);
        QVERIFY(QFileInfo::exists(cwd + u"/okno.png"_s));
        // By default in the user's runtime folder.
        r = app({u"screenshot"_s});
        QCOMPARE(r.code, 0);
        const QString runtimeShot = r.result().value(u"path"_s).toString();
        QVERIFY2(runtimeShot.startsWith(m_home + u"/run/brulion-screenshots/"_s), qPrintable(runtimeShot));
        QCOMPARE(pngSize(readFile(runtimeShot)).width(), 1280);
        // Only PNG files in folders that exist.
        r = app({u"screenshot"_s, m_tmp.filePath(u"zrzut.jpg"_s)});
        QCOMPARE(r.code, 2);
        r = app({u"screenshot"_s, m_tmp.filePath(u"brak/zrzut.png"_s)});
        QCOMPARE(r.code, 2);
        QVERIFY(!QFileInfo::exists(m_tmp.filePath(u"brak"_s)));
    }

    void otherGestures()
    {
        QCOMPARE(app({u"open"_s, u"Test"_s}).code, 0);
        QCOMPARE(app({u"wait"_s, u"--text"_s, u"Drugi blok"_s, u"--exact"_s}).code, 0);
        Run r = app({u"scroll"_s, u"--dy"_s, u"200"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        r = app({u"scroll"_s, u"--dy"_s, u"-200"_s, u"--device"_s, u"touch"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"device"_s).toString(), u"touch"_s);
        r = app({u"scroll"_s});
        QCOMPARE(r.code, 2);
        r = app({u"double-click"_s, u"--text"_s, u"Drugi blok"_s, u"--exact"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QCOMPARE(app({u"key"_s, u"Escape, Escape"_s}).code, 0);
        r = app({u"long-press"_s, u"--text"_s, u"Drugi blok"_s, u"--exact"_s, u"--ms"_s, u"600"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QCOMPARE(app({u"key"_s, u"Escape, Escape"_s}).code, 0);
        r = app({u"drag"_s, u"Pierwszy blok — zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ"_s, u"Drugi blok"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QCOMPARE(app({u"key"_s, u"Escape, Escape"_s}).code, 0);
        r = app({u"click"_s, u"5000,10"_s});
        QCOMPARE(r.code, 2); // outside the window
        r = app({u"key"_s, u"Hyper+K"_s});
        QCOMPARE(r.code, 2);
        // The notes are as they were (nothing typed by these).
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromUtf8(readFile(vaultFile(u"Test.md"_s))),
                                  u"- Pierwszy blok — zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ\n- Drugi blok\n"_s, 5000 * kSlowdown);
    }

    // Typing stops when the command line that asked for it is interrupted.
    void interruptedTypingStops()
    {
        QCOMPARE(app({u"open"_s, u"Test"_s}).code, 0);
        QCOMPARE(app({u"wait"_s, u"--text"_s, u"Drugi blok"_s, u"--exact"_s}).code, 0);
        QCOMPARE(app({u"click"_s, u"--text"_s, u"Drugi blok"_s, u"--exact"_s}).code, 0);
        QCOMPARE(app({u"key"_s, u"End"_s}).code, 0);
        const auto edited = [this] {
            return app({u"state"_s}).result().value(u"editing"_s).toObject().value(u"text"_s).toString();
        };
        QProcess typing;
        typing.setProcessEnvironment(environment());
        typing.start(QStringLiteral(BRULION_CLI), {u"app"_s, u"type"_s, QString(3000, u'ż')});
        QVERIFY(typing.waitForStarted(kCallMs));
        QTRY_VERIFY_WITH_TIMEOUT(edited().count(u'ż') >= 5, 20000 * kSlowdown);
        typing.kill();
        typing.waitForFinished();
        QTest::qWait(500);
        const qsizetype stopped = edited().count(u'ż');
        QTest::qWait(1000);
        QCOMPARE(edited().count(u'ż'), stopped);
        QVERIFY2(stopped < 3000, qPrintable(QString::number(stopped)));
        QCOMPARE(app({u"key"_s, u"Escape, Escape"_s}).code, 0);
        QCOMPARE(app({u"wait"_s, u"--idle"_s, u"--timeout"_s, QString::number(20000 * kSlowdown)}).code, 0);
    }

    // A page edited in the app while another program changes the same line: a conflict waiting
    // for the user. `state` lists it (and says so of the edited block), `wait --idle` never calls
    // that idle (the page is not saved) and says why; once the user keeps their version, it is.
    void conflictIsNotIdle()
    {
        QCOMPARE(app({u"open"_s, u"Konflikt.md"_s}).code, 0);
        QCOMPARE(app({u"wait"_s, u"--text"_s, u"druga linia"_s, u"--exact"_s}).code, 0);
        QCOMPARE(app({u"click"_s, u"--text"_s, u"druga linia"_s, u"--exact"_s}).code, 0);
        QCOMPARE(app({u"key"_s, u"End"_s}).code, 0);
        const auto state = [this] { return app({u"state"_s}).result(); };
        QTRY_COMPARE_WITH_TIMEOUT(state().value(u"editing"_s).toObject().value(u"page"_s).toString(), u"Konflikt.md"_s,
                                  10000 * kSlowdown);
        QVERIFY(state().value(u"conflicts"_s).toArray().isEmpty());
        QVERIFY(!state().value(u"editing"_s).toObject().value(u"conflict"_s).toBool());
        QCOMPARE(state().value(u"syncConflicts"_s).toInt(-1), 0);
        // Typing on (the page stays unsaved) while another program changes that line.
        QProcess typing;
        typing.setProcessEnvironment(environment());
        typing.start(QStringLiteral(BRULION_CLI), {u"app"_s, u"type"_s, QString(3000, u'ż')});
        QVERIFY(typing.waitForStarted(kCallMs));
        QTRY_VERIFY_WITH_TIMEOUT(state().value(u"editing"_s).toObject().value(u"text"_s).toString().count(u'ż') >= 5,
                                 20000 * kSlowdown);
        writeFile(vaultFile(u"Konflikt.md"_s), "- pierwsza linia\n- druga linia, zmieniona gdzie indziej\n");
        const auto conflicted = [&] {
            const QJsonArray list = state().value(u"conflicts"_s).toArray();
            return list.size() == 1 && list.first().toObject().value(u"page"_s).toString() == u"Konflikt.md";
        };
        QTRY_VERIFY_WITH_TIMEOUT(conflicted(), 20000 * kSlowdown);
        typing.kill();
        typing.waitForFinished();
        QVERIFY(state().value(u"editing"_s).toObject().value(u"conflict"_s).toBool());
        // Not idle while the user has not decided: a timeout naming the page.
        Run r = app({u"wait"_s, u"--idle"_s, u"--timeout"_s, u"1500"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"timeout"_s);
        const QJsonObject details = r.json.value(u"error"_s).toObject().value(u"details"_s).toObject();
        QVERIFY(!details.value(u"idle"_s).toBool());
        QCOMPARE(details.value(u"conflicts"_s).toArray().first().toObject().value(u"page"_s).toString(),
                 u"Konflikt.md"_s);
        // The other version is still in the file; the user keeps theirs (the banner's button).
        QCOMPARE(readFile(vaultFile(u"Konflikt.md"_s)),
                 QByteArray("- pierwsza linia\n- druga linia, zmieniona gdzie indziej\n"));
        r = app({u"click"_s, u"--text"_s, u"Zachowaj moją"_s, u"--exact"_s});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        r = app({u"wait"_s, u"--idle"_s, u"--timeout"_s, QString::number(20000 * kSlowdown)});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        QVERIFY(state().value(u"conflicts"_s).toArray().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(
                readFile(vaultFile(u"Konflikt.md"_s)).startsWith("- pierwsza linia\n- druga liniażżżżż"),
                10000 * kSlowdown);
        QCOMPARE(app({u"key"_s, u"Escape, Escape"_s}).code, 0);
    }

    // A password field's text never leaves the app: not in find, not in state, not matched.
    void secretFields()
    {
        QCOMPARE(app({u"navigate"_s, uR"({"view":"settings","section":"sync"})"_s}).code, 0);
        QCOMPARE(app({u"wait"_s, u"--type"_s, u"BTextField"_s}).code, 0);
        Run r = app({u"find"_s, u"--type"_s, u"BTextField"_s, u"--include-hidden"_s, u"--limit"_s, u"200"_s});
        QCOMPARE(r.code, 0);
        QString handle;
        for (const QJsonValue &item : r.result().value(u"items"_s).toArray()) {
            if (item.toObject().value(u"secret"_s).toBool())
                handle = item.toObject().value(u"handle"_s).toString();
        }
        QVERIFY2(!handle.isEmpty(), qPrintable(r.describe()));
        r = app({u"click"_s, handle});
        QVERIFY2(r.code == 0, qPrintable(r.describe()));
        const QString password = u"tajneHasłoŻółwia-123"_s;
        r = app({u"type"_s, password});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"focusedItem"_s).toObject().value(u"secret"_s).toBool());
        QCOMPARE(r.result().value(u"focusedItem"_s).toObject().value(u"text"_s).toString(), QString());
        QVERIFY(!r.out.contains("tajne"));
        r = app({u"state"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"focusedItem"_s).toObject().value(u"secret"_s).toBool());
        QVERIFY(!r.out.contains("tajne"));
        r = app({u"find"_s, u"tajneHasło"_s, u"--include-hidden"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"total"_s).toInt(), 0);
        r = app({u"find"_s, u"--type"_s, u"BTextField"_s, u"--include-hidden"_s, u"--limit"_s, u"200"_s});
        QVERIFY(!r.out.contains("tajne"));
        // Nothing of it in the app's log either.
        QVERIFY(!readFile(m_log).contains("tajne"));
        QCOMPARE(app({u"navigate"_s, u"journals"_s}).code, 0);
    }

    void mcpAppTools()
    {
        QProcess p;
        p.setProcessEnvironment(environment());
        p.start(QStringLiteral(BRULION_CLI), {u"--vault"_s, m_vault, u"mcp"_s, u"--app-control"_s});
        QVERIFY(p.waitForStarted(kCallMs));
        const auto request = [&](const QByteArray &line) -> QJsonObject {
            p.write(line + '\n');
            QElapsedTimer t;
            t.start();
            while (!p.canReadLine() && t.elapsed() < kCallMs)
                p.waitForReadyRead(100);
            return QJsonDocument::fromJson(p.readLine()).object();
        };
        QJsonObject reply =
                request(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})");
        QVERIFY(reply.value(u"result"_s).toObject().value(u"instructions"_s).toString().contains(u"brulion_app_"_s));
        reply = request(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
        QStringList names;
        for (const QJsonValue &t : reply.value(u"result"_s).toObject().value(u"tools"_s).toArray())
            names.append(t.toObject().value(u"name"_s).toString());
        for (const QString &tool : {u"brulion_app_state"_s, u"brulion_app_click"_s, u"brulion_app_long_press"_s,
                                    u"brulion_app_type"_s, u"brulion_app_screenshot"_s, u"brulion_app_wait"_s})
            QVERIFY2(names.contains(tool), qPrintable(tool));
        reply = request(
                R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"brulion_app_state","arguments":{}}})");
        QJsonObject result = reply.value(u"result"_s).toObject();
        QVERIFY(!result.value(u"isError"_s).toBool());
        QCOMPARE(result.value(u"structuredContent"_s)
                         .toObject()
                         .value(u"result"_s)
                         .toObject()
                         .value(u"status"_s)
                         .toString(),
                 u"ready"_s);
        reply = request(
                R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"brulion_app_screenshot","arguments":{}}})");
        const QJsonArray content = reply.value(u"result"_s).toObject().value(u"content"_s).toArray();
        QCOMPARE(content.size(), 2);
        QCOMPARE(content.at(1).toObject().value(u"type"_s).toString(), u"image"_s);
        QCOMPARE(content.at(1).toObject().value(u"mimeType"_s).toString(), u"image/png"_s);
        QCOMPARE(pngSize(QByteArray::fromBase64(content.at(1).toObject().value(u"data"_s).toString().toLatin1())),
                 QSize(1280, 800));
        p.closeWriteChannel();
        QVERIFY(p.waitForFinished(kCallMs));
    }

    void quit()
    {
        const Run r = app({u"quit"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(m_app.waitForFinished(30000 * kSlowdown));
        QCOMPARE(m_app.exitStatus(), QProcess::NormalExit);
        QCOMPARE(m_app.exitCode(), 0);
        QVERIFY(!QFileInfo::exists(controlKey()));
        const Run after = app({u"state"_s});
        QCOMPARE(after.code, 5);
        QCOMPARE(after.errorCode(), u"app_not_running"_s);
    }

    // The device setting alone turns it on (no --remote-control), the switch in Settings →
    // Application turns it off at once (the connections too), and a second launch with
    // --remote-control cannot turn it on in the running app. In a window the settings page fits
    // and in a low one (the switch scrolled into view at the page's end; fonts differ between
    // systems, so either can be the case anywhere).
    void settingTurnsItOnAndOff_data()
    {
        QTest::addColumn<QString>("size");
        QTest::newRow("1280x800") << u"1280x800"_s;
        QTest::newRow("1280x480") << u"1280x480"_s;
    }
    void settingTurnsItOnAndOff()
    {
        QFETCH(QString, size);
        {
            QSettings settings(m_home + u"/config/Brulion/Brulion.ini"_s, QSettings::IniFormat);
            settings.setValue(u"devtools/remoteControl"_s, true);
        }
        startApp(false, size);
        Run r;
        QVERIFY2(until(
                         {u"state"_s},
                         [](const Run &x) { return x.code == 0 && x.result().value(u"ready"_s).toBool(); }, kStartMs,
                         &r),
                 qPrintable(r.describe()));
        // A second launch only brings the running app to the front.
        QProcess second;
        second.setProcessEnvironment(environment());
        second.start(QStringLiteral(BRULION_APP), {u"--remote-control"_s});
        QVERIFY(second.waitForFinished(kCallMs));
        QCOMPARE(second.exitCode(), 0);
        QVERIFY2(second.readAllStandardError().contains("already running"), "no notice for --remote-control");

        QCOMPARE(app({u"navigate"_s, uR"({"view":"settings","section":"app"})"_s}).code, 0);
        QCOMPARE(app({u"wait"_s, u"--object-name"_s, u"settingsRemoteControl"_s}).code, 0);
        r = app({u"find"_s, u"--object-name"_s, u"settingsRemoteControl"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"items"_s).toArray().first().toObject().value(u"checked"_s).toBool());
        // A finger scrolling a little over it, down and up: where the page can move that way it
        // scrolls (the finger travels the drag threshold first), at its end or where it fits
        // the window nothing is touched (a real finger would toggle the switch) — never a tap.
        bool moved = false;
        for (const int dy : {1, -1}) {
            r = app({u"scroll"_s, u"--object-name"_s, u"settingsRemoteControl"_s, u"--dy"_s, QString::number(dy),
                     u"--device"_s, u"touch"_s});
            QVERIFY2(r.code == 0, qPrintable(r.describe()));
            const double scrolled = r.result().value(u"scrolled"_s).toObject().value(u"dy"_s).toDouble();
            if (r.result().value(u"touched"_s).toBool()) {
                QVERIFY2(scrolled * dy > 0, qPrintable(r.describe()));
                moved = true;
            } else {
                QCOMPARE(scrolled, 0.0);
            }
            r = app({u"find"_s, u"--object-name"_s, u"settingsRemoteControl"_s});
            QVERIFY2(r.result().value(u"items"_s).toArray().first().toObject().value(u"checked"_s).toBool(),
                     qPrintable(r.describe()));
        }
        // The page is longer than the low window: one of the two moved it.
        if (size == u"1280x480")
            QVERIFY(moved);
        // Switched off by its own click: the connection that asked is closed with the socket.
        r = app({u"click"_s, u"--object-name"_s, u"settingsRemoteControl"_s});
        QCOMPARE(r.code, 5);
        QVERIFY(until(
                {u"state"_s}, [](const Run &x) { return x.errorCode() == u"remote_control_off"; }, 10000 * kSlowdown,
                &r));
        QVERIFY(!QFileInfo::exists(controlKey()));
        // Saved (QSettings writes from the app's event loop).
        const auto stored = [this] {
            return QSettings(m_home + u"/config/Brulion/Brulion.ini"_s, QSettings::IniFormat)
                    .value(u"devtools/remoteControl"_s)
                    .toString();
        };
        QTRY_COMPARE_WITH_TIMEOUT(stored(), u"false"_s, 10000 * kSlowdown);
        stopApp();
    }

    // No private runtime folder ($XDG_RUNTIME_DIR unset, Qt's <temp>/runtime-<user> not to be
    // had): the single-instance and the control socket would be bare names in the shared temp
    // folder, where another user could block the start or read what is sent. The app starts
    // without both (the event log says why), creates nothing there, and the command line finds no
    // app to send to.
    void withoutARuntimeFolder()
    {
        const QString shared = m_tmp.filePath(u"wspolny-tmp"_s);
        QVERIFY(QDir().mkpath(shared));
        QCOMPARE(::chmod(QFile::encodeName(shared).constData(), 01777), 0);
        // Qt's fallback runtime folder cannot be made there: a file has its name.
        const passwd *pw = ::getpwuid(::geteuid());
        writeFile(shared + u"/runtime-"_s + (pw ? QString::fromLocal8Bit(pw->pw_name) : QString()), "zajęte");
        QProcessEnvironment env = environment();
        env.remove(u"XDG_RUNTIME_DIR"_s);
        env.insert(u"TMPDIR"_s, shared);
        {
            // The same environment here: no runtime folder indeed.
            const QByteArray runtimeDir = qgetenv("XDG_RUNTIME_DIR");
            const QByteArray tmpDir = qgetenv("TMPDIR");
            qunsetenv("XDG_RUNTIME_DIR");
            qputenv("TMPDIR", QFile::encodeName(shared));
            const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
            qputenv("XDG_RUNTIME_DIR", runtimeDir);
            if (tmpDir.isNull())
                qunsetenv("TMPDIR");
            else
                qputenv("TMPDIR", tmpDir);
            if (!runtime.isEmpty())
                QSKIP(qPrintable(u"Qt still finds a runtime folder: "_s + runtime));
        }
        const qint64 logStart = QFileInfo(m_log).size();
        m_app.setProcessEnvironment(env);
        m_app.setStandardOutputFile(m_log, QIODevice::Append);
        m_app.setStandardErrorFile(m_log, QIODevice::Append);
        m_app.start(QStringLiteral(BRULION_APP), {u"--vault"_s, m_vault, u"--size"_s, u"800x600"_s, u"--now"_s,
                                                  u"2026-10-14 10:00"_s, u"--remote-control"_s});
        QVERIFY(m_app.waitForStarted(kStartMs));
        const auto log = [this, logStart] { return QString::fromUtf8(readFile(m_log).mid(logStart)); };
        // Up to the UI: the remote control was asked for and says why it does not listen.
        QTRY_VERIFY2_WITH_TIMEOUT(log().contains(u"Remote control could not start"_s), qPrintable(log().right(3000)),
                                  kStartMs);
        QVERIFY2(log().contains(u"No single instance: There is no private runtime folder"_s),
                 qPrintable(log().right(3000)));
        QCOMPARE(m_app.state(), QProcess::Running);
        for (const QString &name : QDir(shared).entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot))
            QVERIFY2(!name.startsWith(u"brulion"_s), qPrintable(name));
        // brulion-cli with the same environment: no app to send to (and nothing sent anywhere).
        QProcess cli;
        cli.setProcessEnvironment(env);
        cli.start(QStringLiteral(BRULION_CLI), {u"app"_s, u"today"_s});
        QVERIFY(cli.waitForFinished(kCallMs));
        const QJsonObject json = QJsonDocument::fromJson(cli.readAllStandardOutput()).object();
        QCOMPARE(cli.exitCode(), 5);
        QCOMPARE(json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"app_not_running"_s);
        QCOMPARE(m_app.state(), QProcess::Running);
        stopApp();
    }
};

QTEST_GUILESS_MAIN(TestRemoteControl)
#include "tst_remote_control.moc"
