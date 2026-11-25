// What the agents used as Settings and the chat show it (src/app/shell/agentusage.h,
// Workspace.chats.usage): which agents of this device have it (Claude Code's and Codex's presets, a
// command its program names, never one on another computer) and where their logs are, the texts —
// "Today 1.2 M tokens · ≈ 4.10 USD · 30 days ≈ 61 USD", the numbers, the costs with a part not
// known —, the dialog's bars, models and notebook filter, and reading the logs when shown (once in
// five minutes) or refreshed, off the GUI thread, from synthetic session logs.
#include "agentusage.h"
#include "workspace.h"
#include "workspaceagents.h"
#include "workspacechats.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::shell;
using namespace brulion::agentusage;

namespace {

const QDate kToday(2026, 11, 24);

DayUse use(const QDate &date, const QString &model, const QString &project, qint64 input, qint64 output,
           qint64 requests = 1)
{
    DayUse u;
    u.date = date;
    u.model = model;
    u.project = project;
    u.tokens.input = input;
    u.tokens.output = output;
    u.tokens.requests = requests;
    return u;
}

agentusage::History history(const QList<DayUse> &days)
{
    agentusage::History h;
    h.provider = Provider::Claude;
    h.found = true;
    h.readAt = QDateTime(kToday, QTime(14, 30));
    h.days = days;
    return h;
}

QByteArray claudeLine(const QString &id, const QString &cwd, qint64 input, qint64 output)
{
    const QJsonObject o{{u"type"_s, u"assistant"_s},
                        {u"timestamp"_s, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                        {u"cwd"_s, cwd},
                        {u"requestId"_s, QString(u"req_"_s + id)},
                        {u"message"_s, QJsonObject{{u"id"_s, QString(u"msg_"_s + id)},
                                                   {u"model"_s, u"claude-opus-5-5"_s},
                                                   {u"usage"_s, QJsonObject{{u"input_tokens"_s, double(input)},
                                                                            {u"output_tokens"_s, double(output)}}}}}};
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

} // namespace

class TestAgentUsageShell : public QObject {
    Q_OBJECT

    QTranslator m_translator;
    bool m_english = false;

    /// The app's translation `language` ("en", "pl") installed instead of the one before (the
    /// plural forms come from it).
    bool speak(const QString &language)
    {
#ifdef BRULION_QM_DIR
        QCoreApplication::removeTranslator(&m_translator);
        if (!m_translator.load(QStringLiteral(BRULION_QM_DIR "/brulion_") + language + u".qm"_s))
            return false;
        return QCoreApplication::installTranslator(&m_translator);
#else
        Q_UNUSED(language);
        return false;
#endif
    }

private Q_SLOTS:
    void initTestCase()
    {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
        m_english = speak(u"en"_s);
    }

    void sources_data()
    {
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("programName");
        QTest::addColumn<bool>("remote");
        QTest::addColumn<QStringList>("folders");
        QTest::newRow("Claude Code") << u"npx -y @agentclientprotocol/claude-agent-acp"_s << QString() << false
                                     << QStringList{u"/home/łucja/.claude/projects"_s,
                                                    u"/home/łucja/.config/claude/projects"_s};
        QTest::newRow("Codex") << u"npx -y @agentclientprotocol/codex-acp"_s << QString() << false
                               << QStringList{u"/home/łucja/.codex/sessions"_s,
                                              u"/home/łucja/.codex/archived_sessions"_s};
        QTest::newRow("its own config folder")
                << u"env CLAUDE_CONFIG_DIR=\"/home/łucja/praca claude\" npx claude-agent-acp"_s << QString() << false
                << QStringList{u"/home/łucja/praca claude/projects"_s};
        QTest::newRow("Gemini CLI") << u"npx -y @google/gemini-cli --acp"_s << QString() << false << QStringList();
        QTest::newRow("own command, Codex inside")
                << u"/opt/agent/run.sh"_s << u"codex-acp"_s << false
                << QStringList{u"/home/łucja/.codex/sessions"_s, u"/home/łucja/.codex/archived_sessions"_s};
        QTest::newRow("on another computer")
                << u"npx -y @agentclientprotocol/claude-agent-acp"_s << QString() << true << QStringList();
    }

    void sources()
    {
        QFETCH(QString, command);
        QFETCH(QString, programName);
        QFETCH(bool, remote);
        QFETCH(QStringList, folders);
        AgentConfig agent{u"a"_s, u"Agent"_s, command};
        if (remote)
            agent.url = u"https://nixos.example.ts.net"_s;
        QCOMPARE(AgentUsage::sourceOf(agent, programName, {}, u"/home/łucja"_s).folders, folders);
    }

    void numbers_data()
    {
        QTest::addColumn<qint64>("tokens");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("words");
        QTest::newRow("none") << qint64(0) << u"0"_s << u"0 tokens"_s;
        QTest::newRow("one") << qint64(1) << u"1"_s << u"1 token"_s;
        QTest::newRow("hundreds") << qint64(950) << u"950"_s << u"950 tokens"_s;
        QTest::newRow("thousand") << qint64(1000) << u"1 K"_s << u"1 K tokens"_s;
        QTest::newRow("1.2 K") << qint64(1234) << u"1.2 K"_s << u"1.2 K tokens"_s;
        QTest::newRow("12 K") << qint64(12345) << u"12 K"_s << u"12 K tokens"_s;
        QTest::newRow("almost a million") << qint64(999'700) << u"1 M"_s << u"1 M tokens"_s;
        QTest::newRow("1.2 M") << qint64(1'234'567) << u"1.2 M"_s << u"1.2 M tokens"_s;
        QTest::newRow("123 M") << qint64(123'456'789) << u"123 M"_s << u"123 M tokens"_s;
        QTest::newRow("3.4 B") << qint64(3'400'000'000) << u"3.4 B"_s << u"3.4 B tokens"_s;
    }

    void numbers()
    {
        QFETCH(qint64, tokens);
        QFETCH(QString, text);
        QFETCH(QString, words);
        QCOMPARE(AgentUsage::tokensText(tokens), text);
        if (tokens == 1 && !m_english)
            QSKIP("the singular comes from the app's English translation, not built here");
        QCOMPARE(AgentUsage::tokensWords(tokens), words);
    }

    void costs()
    {
        Totals t;
        QCOMPARE(AgentUsage::costText(t), u"0 USD"_s);
        t.tokens.requests = 1;
        t.cost = 0.001;
        QCOMPARE(AgentUsage::costText(t), u"< 0.01 USD"_s);
        t.cost = 4.1;
        QCOMPARE(AgentUsage::costText(t), u"≈ 4.10 USD"_s);
        t.cost = 61.4;
        QCOMPARE(AgentUsage::costText(t), u"≈ 61 USD"_s);
        t.unpricedRequests = 2;
        QCOMPARE(AgentUsage::costText(t), u"≥ 61 USD"_s);
        t.cost = 0;
        QCOMPARE(AgentUsage::costText(t), u"cost not known"_s);
    }

    void texts()
    {
        // Today: 1 M in + 10 K out of Opus 5.5 = 4.20 USD; 20 days ago another 14 M in = 56 USD.
        const agentusage::History h =
                history({use(kToday, u"claude-opus-5-5"_s, u"/home/ola/Notatki"_s, 1'000'000, 10'000, 3),
                         use(kToday.addDays(-20), u"claude-opus-5-5"_s, u"/home/ola/Inne"_s, 14'000'000, 0, 40),
                         use(kToday.addDays(-40), u"claude-opus-5-5"_s, u"/home/ola/Inne"_s, 9'000'000, 0)});
        QVariantMap d = AgentUsage::describe(h, kToday);
        QCOMPARE(d.value(u"provider"_s).toString(), u"Claude Code"_s);
        QVERIFY(d.value(u"read"_s).toBool());
        QCOMPARE(d.value(u"summary"_s).toString(), u"Today 1 M tokens · ≈ 4.20 USD · 30 days ≈ 60 USD"_s);
        QCOMPARE(d.value(u"today"_s).toString(), u"Used today: 1 M tokens · ≈ 4.20 USD at API prices"_s);
        QCOMPARE(d.value(u"checked"_s).toString(), u"Read at 14:30"_s);
        QCOMPARE(d.value(u"failure"_s).toString(), QString());
        // Nothing today; a model without a price only.
        d = AgentUsage::describe(history({use(kToday.addDays(-1), u"nowy-model"_s, {}, 1500, 0)}), kToday);
        QCOMPARE(d.value(u"summary"_s).toString(), u"Today nothing · 30 days 1.5 K tokens"_s);
        QCOMPARE(d.value(u"today"_s).toString(), u"Used today: nothing"_s);
        // Never read; no logs at all.
        QVERIFY(!AgentUsage::describe(agentusage::History{}, kToday).value(u"read"_s).toBool());
        agentusage::History missing = history({});
        missing.found = false;
        d = AgentUsage::describe(missing, kToday);
        QVERIFY(d.value(u"failure"_s).toString().contains(u"No session logs of Claude Code"_s));
        QCOMPARE(d.value(u"summary"_s).toString(), u"No usage yet"_s);
    }

    void details()
    {
        agentusage::History h =
                history({use(kToday, u"claude-opus-5-5"_s, u"/home/ola/Zażółć"_s, 1'000'000, 10'000, 3),
                         use(kToday.addDays(-2), u"claude-haiku-5-5"_s, u"/home/ola/Zażółć/podfolder"_s, 2'000'000, 0),
                         use(kToday.addDays(-3), u"tajny-model"_s, u"/home/ola/Inne"_s, 700, 0)});
        h.days[1].tokens.cacheRead = 5000;
        h.days[1].tokens.reasoning = 0;
        QVariantMap d = AgentUsage::describeDetails(h, kToday, 7, {});
        const QVariantList bars = d.value(u"bars"_s).toList();
        QCOMPARE(bars.size(), 7);
        const QVariantMap last = bars.last().toMap();
        QCOMPARE(last.value(u"day"_s).toString(), u"2026-11-24"_s);
        QVERIFY(last.value(u"today"_s).toBool());
        QCOMPARE(last.value(u"mark"_s).toString(), u"Tue"_s);
        QCOMPARE(last.value(u"tokens"_s).toDouble(), 1'010'000.0);
        QCOMPARE(last.value(u"tip"_s).toString(), u"Tue 24 Nov: 1 M tokens · ≈ 4.20 USD"_s);
        QCOMPARE(bars.at(5).toMap().value(u"tip"_s).toString(), u"Mon 23 Nov: nothing"_s);
        QCOMPARE(d.value(u"maxTokens"_s).toDouble(), 2'005'000.0);
        QCOMPARE(d.value(u"total"_s).toString(), u"3 M tokens · ≥ 4.40 USD · 5 requests"_s);
        const QVariantList models = d.value(u"models"_s).toList();
        QCOMPARE(models.size(), 3);
        const QVariantMap opus = models.first().toMap();
        QCOMPARE(opus.value(u"name"_s).toString(), u"claude-opus-5-5"_s);
        QCOMPARE(opus.value(u"cost"_s).toString(), u"≈ 4.20 USD"_s);
        QCOMPARE(opus.value(u"kinds"_s).toString(), u"input 1 M · output 10 K"_s);
        QCOMPARE(opus.value(u"requests"_s).toString(), u"3 requests"_s);
        QCOMPARE(models.at(1).toMap().value(u"kinds"_s).toString(), u"input 2 M · output 0 · cache read 5 K"_s);
        QCOMPARE(models.at(2).toMap().value(u"cost"_s).toString(), u"cost not known"_s);
        if (m_english)
            QCOMPARE(models.at(2).toMap().value(u"requests"_s).toString(), u"1 request"_s);
        QCOMPARE(d.value(u"unpriced"_s).toString(),
                 u"Models without a known price: their 700 tokens are not in the cost."_s);
        QVERIFY(d.value(u"note"_s).toString().contains(u"a subscription pays nothing more"_s));
        QVERIFY(!d.value(u"empty"_s).toBool());
        // This notebook only: the folder and below.
        d = AgentUsage::describeDetails(h, kToday, 30, u"/home/ola/Zażółć"_s);
        QCOMPARE(d.value(u"bars"_s).toList().size(), 30);
        QCOMPARE(d.value(u"total"_s).toString(), u"3 M tokens · ≈ 4.40 USD · 4 requests"_s);
        QCOMPARE(d.value(u"unpriced"_s).toString(), QString());
        QCOMPARE(d.value(u"folder"_s).toString(), QDir::toNativeSeparators(u"/home/ola/Zażółć"_s));
        // Every 7th day's date counted back from today.
        const QVariantList month = d.value(u"bars"_s).toList();
        QCOMPARE(month.last().toMap().value(u"mark"_s).toString(), u"24 Nov"_s);
        QCOMPARE(month.at(month.size() - 8).toMap().value(u"mark"_s).toString(), u"17 Nov"_s);
        QCOMPARE(month.at(month.size() - 2).toMap().value(u"mark"_s).toString(), QString());
        d = AgentUsage::describeDetails(h, kToday, 30, u"/home/ola/Pusty"_s);
        QVERIFY(d.value(u"empty"_s).toBool());
        QCOMPARE(d.value(u"emptyText"_s).toString(), u"Nothing used in this notebook in these days."_s);
        QCOMPARE(AgentUsage::describeDetails(h, kToday, 90, {}).value(u"bars"_s).toList().size(), 90);
        QCOMPARE(AgentUsage::describeDetails(h, kToday, 0, {}).value(u"bars"_s).toList().size(), 1);
    }

    void polish()
    {
        if (!speak(u"pl"_s))
            QSKIP("the app's translations are not built here");
        QLocale::setDefault(QLocale(QLocale::Polish, QLocale::Poland));
        QCOMPARE(AgentUsage::tokensText(1'234'567), u"1,2 mln"_s);
        QCOMPARE(AgentUsage::tokensText(12'345), u"12 tys."_s);
        QCOMPARE(AgentUsage::tokensWords(1), u"1 token"_s);
        QCOMPARE(AgentUsage::tokensWords(3), u"3 tokeny"_s);
        QCOMPARE(AgentUsage::tokensWords(25), u"25 tokenów"_s);
        QCOMPARE(AgentUsage::tokensWords(2'500'000), u"2,5 mln tokenów"_s);
        const agentusage::History h =
                history({use(kToday, u"claude-opus-5-5"_s, u"/home/ola/Zażółć"_s, 1'000'000, 10'000, 3)});
        const QVariantMap d = AgentUsage::describe(h, kToday);
        QCOMPARE(d.value(u"summary"_s).toString(), u"Dziś 1 mln tokenów · ≈ 4,20 USD · 30 dni ≈ 4,20 USD"_s);
        QCOMPARE(d.value(u"today"_s).toString(), u"Dziś zużyto: 1 mln tokenów · ≈ 4,20 USD w cenach API"_s);
        const QVariantMap details = AgentUsage::describeDetails(h, kToday, 7, {});
        QCOMPARE(details.value(u"total"_s).toString(), u"1 mln tokenów · ≈ 4,20 USD · 3 żądania"_s);
        QVERIFY(details.value(u"note"_s).toString().contains(u"abonamentu"_s));
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
        m_english = speak(u"en"_s);
    }

    void readsWhenShown()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString config = dir.filePath(u"claude config"_s);
        const QString log = config + u"/projects/-home-ola-Notatki/s1.jsonl"_s;
        QVERIFY(QDir().mkpath(QFileInfo(log).absolutePath()));
        {
            QFile f(log);
            QVERIFY(f.open(QIODevice::WriteOnly));
            // A message logged twice while it streamed: once.
            f.write(claudeLine(u"1"_s, u"/home/ola/Notatki"_s, 1'000'000, 10'000));
            f.write(claudeLine(u"1"_s, u"/home/ola/Notatki"_s, 1'000'000, 10'000));
        }
        Workspace ws;
        ws.chats()->setAgents(
                {AgentConfig{u"claude"_s, u"Claude Code"_s, u"npx -y @agentclientprotocol/claude-agent-acp"_s},
                 AgentConfig{u"gemini"_s, u"Gemini CLI"_s, u"npx -y @google/gemini-cli --acp"_s}});
        AgentUsage *usage = ws.chats()->usage();
        QDateTime now = QDateTime::currentDateTime();
        usage->setClock([&now] { return now; });
        usage->setCacheDir(dir.filePath(u"cache"_s));
        usage->setEnvironment({{u"CLAUDE_CONFIG_DIR"_s, config}}, dir.filePath(u"home"_s));
        if (!WorkspaceAgents::available())
            QSKIP("programs do not run here: no usage");
        QVERIFY(usage->applies(u"acp:claude"_s));
        QVERIFY(!usage->applies(u"acp:gemini"_s));
        QVERIFY(!usage->applies(QString()));
        QVERIFY(!usage->applies(u"acp:nieznany"_s));
        QVERIFY(!usage->state(u"acp:gemini"_s).value(u"applies"_s).toBool());
        QVERIFY(!usage->details(u"acp:gemini"_s, 30, false).value(u"applies"_s).toBool());

        QSignalSpy changed(usage, &AgentUsage::changed);
        QVariantMap state = usage->state(u"acp:claude"_s);
        QVERIFY(state.value(u"applies"_s).toBool());
        QVERIFY(!state.value(u"read"_s).toBool());
        usage->show(u"acp:claude"_s);
        QVERIFY(usage->state(u"acp:claude"_s).value(u"busy"_s).toBool());
        QTRY_VERIFY(!usage->state(u"acp:claude"_s).value(u"busy"_s).toBool());
        state = usage->state(u"acp:claude"_s);
        QVERIFY(state.value(u"read"_s).toBool());
        QCOMPARE(state.value(u"summary"_s).toString(), u"Today 1 M tokens · ≈ 4.20 USD · 30 days ≈ 4.20 USD"_s);
        QVERIFY(changed.size() >= 2);
        QVERIFY(usage->details(u"acp:claude"_s, 7, false)
                        .value(u"where"_s)
                        .toString()
                        .contains(QDir::toNativeSeparators(config)));

        // Appended: shown again within five minutes reads nothing, Refresh reads now.
        {
            QFile f(log);
            QVERIFY(f.open(QIODevice::Append));
            f.write(claudeLine(u"2"_s, u"/home/ola/Notatki"_s, 1'000'000, 0));
        }
        now = now.addSecs(120);
        usage->show(u"acp:claude"_s);
        QVERIFY(!usage->state(u"acp:claude"_s).value(u"busy"_s).toBool());
        usage->refresh(u"acp:claude"_s);
        QVERIFY(usage->state(u"acp:claude"_s).value(u"busy"_s).toBool());
        QTRY_VERIFY(!usage->state(u"acp:claude"_s).value(u"busy"_s).toBool());
        QCOMPARE(usage->state(u"acp:claude"_s).value(u"summary"_s).toString(),
                 u"Today 2 M tokens · ≈ 8.20 USD · 30 days ≈ 8.20 USD"_s);
        // This notebook only: the workspace has none open, so its folder holds nothing of it.
        QVERIFY(usage->details(u"acp:claude"_s, 7, true).value(u"empty"_s).toBool());

        // An agent not Claude Code nor Codex is never read; the agents changed: bindings ask again.
        usage->show(u"acp:gemini"_s);
        QVERIFY(!usage->state(u"acp:gemini"_s).value(u"busy"_s).toBool());
        const int serial = usage->serial();
        ws.chats()->setAgents({AgentConfig{u"codex"_s, u"Codex"_s, u"npx -y @agentclientprotocol/codex-acp"_s}});
        QVERIFY(usage->serial() > serial);
        QVERIFY(!usage->applies(u"acp:claude"_s));
        QVERIFY(usage->applies(u"acp:codex"_s));
    }
};

QTEST_MAIN(TestAgentUsageShell)
#include "tst_agentusage.moc"
