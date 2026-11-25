// `brulion-cli acp usage` as a process with a private home: Claude Code's and Codex's synthetic
// session logs (CLAUDE_CONFIG_DIR and CODEX_HOME, Polish folder names) read into totals per day,
// model and project — a streamed message once, Codex's cumulative totals as turns —, the period,
// one agent, this notebook only, the text for people, the app's cache written and used again, and
// the refusals (--days out of range, --notebook with --all, an agent that is none).
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int kTimeoutMs = 30000 * BRULION_TEST_SLOWDOWN;

struct Run {
    int code = -1;
    QJsonObject json;
    QByteArray out;
    QByteArray err;
    QJsonObject result() const { return json.value(u"result"_s).toObject(); }
    QString errorCode() const { return json.value(u"error"_s).toObject().value(u"code"_s).toString(); }
    QJsonObject agent(int i) const { return result().value(u"agents"_s).toArray().at(i).toObject(); }
};

void writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QByteArray line(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

QByteArray claudeLine(const QString &id, const QString &time, const QString &cwd, qint64 input, qint64 output)
{
    return line({{u"type"_s, u"assistant"_s},
                 {u"timestamp"_s, time},
                 {u"cwd"_s, cwd},
                 {u"requestId"_s, QString(u"req_"_s + id)},
                 {u"message"_s, QJsonObject{{u"id"_s, QString(u"msg_"_s + id)},
                                            {u"model"_s, u"claude-opus-5-5"_s},
                                            {u"usage"_s, QJsonObject{{u"input_tokens"_s, double(input)},
                                                                     {u"output_tokens"_s, double(output)},
                                                                     {u"cache_read_input_tokens"_s, 1000}}}}}});
}

QByteArray codexCount(const QString &time, qint64 input, qint64 cached, qint64 output)
{
    const QJsonObject usage{{u"input_tokens"_s, double(input)},
                            {u"cached_input_tokens"_s, double(cached)},
                            {u"output_tokens"_s, double(output)},
                            {u"reasoning_output_tokens"_s, 0},
                            {u"total_tokens"_s, double(input + output)}};
    return line({{u"timestamp"_s, time},
                 {u"type"_s, u"event_msg"_s},
                 {u"payload"_s, QJsonObject{{u"type"_s, u"token_count"_s},
                                            {u"info"_s, QJsonObject{{u"total_token_usage"_s, usage}}}}}});
}

} // namespace

class TestCliAcpUsage : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_home;
    QString m_notebook;

    Run run(const QStringList &args, const QString &language = u"en"_s)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"HOME"_s, m_home);
        env.insert(u"XDG_CONFIG_HOME"_s, m_home + u"/config"_s);
        env.insert(u"XDG_DATA_HOME"_s, m_home + u"/data"_s);
        env.insert(u"XDG_STATE_HOME"_s, m_home + u"/state"_s);
        env.insert(u"XDG_CACHE_HOME"_s, m_home + u"/cache"_s);
        env.insert(u"XDG_RUNTIME_DIR"_s, m_home + u"/run"_s);
        env.insert(u"CLAUDE_CONFIG_DIR"_s, m_home + u"/konfiguracja claude"_s);
        env.insert(u"CODEX_HOME"_s, m_home + u"/codex"_s);
        env.insert(u"LANG"_s, u"C.UTF-8"_s);
        env.insert(u"LANGUAGE"_s, language);
        env.insert(u"TZ"_s, u"Europe/Warsaw"_s);
        QProcess p;
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(BRULION_CLI), QStringList{u"--now"_s, u"2026-11-24 12:00"_s} + args);
        Run r;
        if (!p.waitForStarted(kTimeoutMs) || !p.waitForFinished(kTimeoutMs))
            return r;
        r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -2;
        r.out = p.readAllStandardOutput();
        r.err = p.readAllStandardError();
        r.json = QJsonDocument::fromJson(r.out).object();
        return r;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_home = m_tmp.filePath(u"dom"_s);
        m_notebook = m_tmp.filePath(u"Notatki Żółwia"_s);
        QVERIFY(QDir().mkpath(m_notebook));
        QVERIFY(QDir().mkpath(m_home));
        const QString claude = m_home + u"/konfiguracja claude/projects/-x-Notatki"_s;
        // Today twice in the notebook (one message logged twice while it streamed), yesterday
        // elsewhere, ten days ago (outside a week).
        writeFile(claude + u"/a.jsonl"_s,
                  claudeLine(u"1"_s, u"2026-11-24T10:00:00Z"_s, m_notebook, 100, 50)
                          + claudeLine(u"1"_s, u"2026-11-24T10:00:00Z"_s, m_notebook, 100, 50)
                          + claudeLine(u"2"_s, u"2026-11-24T10:05:00Z"_s, m_notebook + u"/podfolder"_s, 200, 10)
                          + claudeLine(u"3"_s, u"2026-11-23T10:00:00Z"_s, u"/home/ola/Inne"_s, 1000, 0)
                          + claudeLine(u"4"_s, u"2026-11-14T10:00:00Z"_s, m_notebook, 5000, 0)
                          + "{zepsuta linia \"usage\"\n");
        QByteArray rollout = line({{u"timestamp"_s, u"2026-11-24T09:00:00Z"_s},
                                   {u"type"_s, u"session_meta"_s},
                                   {u"payload"_s, QJsonObject{{u"cwd"_s, m_notebook}}}})
                             + line({{u"timestamp"_s, u"2026-11-24T09:00:01Z"_s},
                                     {u"type"_s, u"turn_context"_s},
                                     {u"payload"_s, QJsonObject{{u"model"_s, u"gpt-5.3-codex"_s}}}});
        rollout += codexCount(u"2026-11-24T09:01:00Z"_s, 1000, 800, 100);
        rollout += codexCount(u"2026-11-24T09:01:01Z"_s, 1000, 800, 100); // (a repeat)
        rollout += codexCount(u"2026-11-24T09:02:00Z"_s, 3000, 2500, 300);
        writeFile(m_home + u"/codex/sessions/2026/11/24/rollout-2026-11-24T10-00-00-a.jsonl"_s, rollout);
    }

    void bothAgents()
    {
        const Run r = run({u"acp"_s, u"usage"_s, u"--days"_s, u"7"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"from"_s).toString(), u"2026-11-18"_s);
        QCOMPARE(r.result().value(u"to"_s).toString(), u"2026-11-24"_s);
        QVERIFY(r.result().value(u"folder"_s).isNull());
        QCOMPARE(r.result().value(u"pricesAsOf"_s).toString().size(), 10);
        QVERIFY(r.result().value(u"note"_s).toString().contains(u"subscription pays nothing more"_s));
        const QJsonObject claude = r.agent(0);
        QCOMPARE(claude.value(u"agent"_s).toString(), u"claude"_s);
        QVERIFY(claude.value(u"found"_s).toBool());
        QCOMPARE(claude.value(u"files"_s).toInt(), 1);
        QCOMPARE(claude.value(u"skippedLines"_s).toInt(), 1);
        const QJsonObject total = claude.value(u"total"_s).toObject();
        QCOMPARE(total.value(u"requests"_s).toInt(), 3);
        QCOMPARE(total.value(u"input"_s).toInt(), 1300);
        QCOMPARE(total.value(u"cacheRead"_s).toInt(), 3000);
        QVERIFY(total.value(u"costKnown"_s).toBool());
        QVERIFY(total.value(u"costUsd"_s).toDouble() > 0);
        QCOMPARE(claude.value(u"days"_s).toArray().size(), 7);
        QCOMPARE(claude.value(u"days"_s).toArray().last().toObject().value(u"requests"_s).toInt(), 2);
        QCOMPARE(claude.value(u"models"_s).toArray().first().toObject().value(u"model"_s).toString(),
                 u"claude-opus-5-5"_s);
        QCOMPARE(claude.value(u"projects"_s).toArray().size(), 3);
        const QJsonObject codex = r.agent(1);
        QCOMPARE(codex.value(u"agent"_s).toString(), u"codex"_s);
        const QJsonObject codexTotal = codex.value(u"total"_s).toObject();
        QCOMPARE(codexTotal.value(u"requests"_s).toInt(), 2);
        QCOMPARE(codexTotal.value(u"input"_s).toInt() + codexTotal.value(u"cacheRead"_s).toInt(), 3000);
        QCOMPARE(codexTotal.value(u"output"_s).toInt(), 300);
        // The app's cache: written, and used by the next run.
        bool cached = false;
        for (QDirIterator it(m_home + u"/cache"_s, {u"*.json"_s}, QDir::Files, QDirIterator::Subdirectories);
             it.hasNext();)
            cached = it.next().contains(u"/agent-usage/"_s) || cached; // (next() first: the loop moves on)
        QVERIFY(cached);
        const Run again = run({u"acp"_s, u"usage"_s, u"--days"_s, u"7"_s});
        QVERIFY(again.agent(0).value(u"total"_s).toObject() == total);
    }

    void oneAgentOneNotebook()
    {
        Run r = run({u"acp"_s, u"usage"_s, u"--agent"_s, u"claude"_s, u"--days"_s, u"30"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"agents"_s).toArray().size(), 1);
        QCOMPARE(r.agent(0).value(u"total"_s).toObject().value(u"requests"_s).toInt(), 4);
        r = run({u"--vault"_s, m_notebook, u"acp"_s, u"usage"_s, u"--agent"_s, u"claude"_s, u"--notebook"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"folder"_s).toString(), QDir(m_notebook).absolutePath());
        QCOMPARE(r.agent(0).value(u"total"_s).toObject().value(u"requests"_s).toInt(), 3);
        r = run({u"acp"_s, u"usage"_s, u"--agent"_s, u"codex"_s, u"--all"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.agent(0).value(u"agent"_s).toString(), u"codex"_s);
    }

    void text()
    {
        const Run r = run({u"--format"_s, u"text"_s, u"acp"_s, u"usage"_s, u"--days"_s, u"7"_s});
        QCOMPARE(r.code, 0);
        const QString out = QString::fromUtf8(r.out);
        QVERIFY2(out.contains(u"Claude Code  "_s) && out.contains(u" requests  "_s), qPrintable(out));
        QVERIFY(out.contains(u"claude-opus-5-5"_s));
        QVERIFY(out.contains(u"gpt-5.3-codex"_s));
        QVERIFY(out.contains(u"subscription pays nothing more"_s));
    }

    void refusals()
    {
        Run r = run({u"acp"_s, u"usage"_s, u"--days"_s, u"0"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"bad_days"_s);
        r = run({u"acp"_s, u"usage"_s, u"--days"_s, u"400"_s});
        QCOMPARE(r.code, 2);
        r = run({u"--vault"_s, m_notebook, u"acp"_s, u"usage"_s, u"--notebook"_s, u"--all"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"bad_scope"_s);
        r = run({u"acp"_s, u"usage"_s, u"--agent"_s, u"gemini"_s});
        QCOMPARE(r.code, 2);
    }

    void noLogs()
    {
        QTemporaryDir empty;
        const QString saved = m_home;
        m_home = empty.path();
        const Run r = run({u"acp"_s, u"usage"_s, u"--agent"_s, u"codex"_s});
        m_home = saved;
        QCOMPARE(r.code, 0);
        QVERIFY(!r.agent(0).value(u"found"_s).toBool());
        QCOMPARE(r.agent(0).value(u"total"_s).toObject().value(u"requests"_s).toInt(), 0);
    }
};

QTEST_GUILESS_MAIN(TestCliAcpUsage)
#include "tst_cli_acp_usage.moc"
