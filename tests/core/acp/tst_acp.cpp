// The Agent Client Protocol from the client's side (acp/acp.h), the agent played by the test: lines
// split and joined as a pipe delivers them, bad and too long lines skipped; the handshake (the
// client offers no files and no terminal, the version checked), a session per conversation with
// the notebook's folder and Brulion's MCP server, the first prompt of a new session and the next
// ones; the answer put together from the agent's chunks (a tool call between them starts a new
// paragraph), its thoughts, what it does; a question (its title from the tool call, a diff, the
// input as JSON, options) answered and cancelled; Stop (session/cancel, nothing more said, a turn
// sent meanwhile waits for the cancelled one); signing in (auth_required, authenticate); a refusal,
// an empty answer, an error, an agent that went away; requests a client does not serve;
// conversations side by side; prompt blocks as JSON; a session's options (configOptions, the older
// modes and models) read and set before the first prompt and during a turn; sessions resumed and
// loaded, a failed load followed by a new session; commands, usage, the plan, tool calls with
// their output and terminals, messages by id; the agent's requests for files and terminals handed
// on; pictures and embedded text falling back to links; open(), forget(), rename().
#include "acp/acp.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::acp;

namespace {

/// The agent, played: what the client sent it (one message a line), and what it says back.
struct Agent
{
    QList<QJsonObject> sent;
    Client *client = nullptr;

    Connection::Send sender()
    {
        return [this](const QByteArray &line) {
            QVERIFY(line.endsWith('\n'));
            QVERIFY(!line.chopped(1).contains('\n'));
            sent << QJsonDocument::fromJson(line).object();
        };
    }
    /// The last message sent with `method` ({} none).
    QJsonObject last(const QString &method) const
    {
        for (qsizetype i = sent.size() - 1; i >= 0; --i) {
            if (sent.at(i).value(u"method"_s).toString() == method)
                return sent.at(i);
        }
        return {};
    }
    int count(const QString &method) const
    {
        return static_cast<int>(std::ranges::count_if(
                sent, [&](const QJsonObject &o) { return o.value(u"method"_s).toString() == method; }));
    }
    void write(const QJsonObject &message)
    {
        QJsonObject full = message;
        full.insert(u"jsonrpc"_s, u"2.0"_s);
        client->received(QJsonDocument(full).toJson(QJsonDocument::Compact) + '\n');
    }
    void reply(const QJsonObject &request, const QJsonValue &result)
    {
        write({{u"id"_s, request.value(u"id"_s)}, {u"result"_s, result}});
    }
    void replyError(const QJsonObject &request, int code, const QString &message)
    {
        write({{u"id"_s, request.value(u"id"_s)}, {u"error"_s, QJsonObject{{u"code"_s, code}, {u"message"_s, message}}}});
    }
    void initialize(const QJsonObject &extra = {})
    {
        QJsonObject result{{u"protocolVersion"_s, 1},
                           {u"agentInfo"_s, QJsonObject{{u"name"_s, u"fake-agent"_s}, {u"title"_s, u"Fake Agent"_s},
                                                         {u"version"_s, u"1.0"_s}}},
                           {u"agentCapabilities"_s,
                            QJsonObject{{u"promptCapabilities"_s, QJsonObject{{u"image"_s, true}}}}},
                           {u"authMethods"_s, QJsonArray()}};
        for (auto it = extra.begin(); it != extra.end(); ++it)
            result.insert(it.key(), it.value());
        reply(last(u"initialize"_s), result);
    }
    void update(const QString &session, const QJsonObject &update)
    {
        write({{u"method"_s, u"session/update"_s},
               {u"params"_s, QJsonObject{{u"sessionId"_s, session}, {u"update"_s, update}}}});
    }
    void chunk(const QString &session, const QString &text, const QString &kind = u"agent_message_chunk"_s)
    {
        update(session, {{u"sessionUpdate"_s, kind},
                         {u"content"_s, QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, text}}}});
    }
};

TurnInput input(const QString &first, const QString &next)
{
    TurnInput in;
    in.cwd = u"/home/ala/Notatki"_s;
    in.mcpServers << McpServer{u"brulion"_s, u"/usr/bin/brulion-cli"_s, {u"mcp"_s, u"--vault"_s, u"/home/ala/Notatki"_s}};
    in.firstPrompt << Block::textBlock(first);
    in.prompt << Block::textBlock(next);
    return in;
}

QString promptText(const QJsonObject &request, int block = 0)
{
    return request.value(u"params"_s).toObject().value(u"prompt"_s).toArray().at(block).toObject().value(u"text"_s).toString();
}

} // namespace

class TestAcp : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void lines()
    {
        QList<QJsonObject> notes;
        QStringList bad;
        Connection c([](const QByteArray &) {});
        c.onNotification = [&](const QString &, const QJsonObject &params) { notes << params; };
        c.onMalformed = [&](const QString &why) { bad << why; };
        // A message split over three reads, two in one, CRLF, a blank line, a bad one between.
        c.receive(R"({"jsonrpc":"2.0","method":"a","par)");
        c.receive(R"(ams":{"n":1}})");
        QVERIFY(notes.isEmpty());
        c.receive("\n{\"jsonrpc\":\"2.0\",\"method\":\"a\",\"params\":{\"n\":2}}\r\n\n{nie json}\n");
        c.receive(R"({"jsonrpc":"2.0","method":"a","params":{"n":3,"t":"żółć"}})"
                  "\n");
        QCOMPARE(notes.size(), 3);
        QCOMPARE(notes.at(1).value(u"n"_s).toInt(), 2);
        QCOMPARE(notes.at(2).value(u"t"_s).toString(), u"żółć"_s);
        QCOMPARE(bad.size(), 1);

        // A line too long is dropped to its end, the next one read.
        QByteArray huge(MaxLineBytes / 4, 'x');
        for (int i = 0; i < 5; ++i)
            c.receive(huge);
        c.receive("x\n{\"jsonrpc\":\"2.0\",\"method\":\"a\",\"params\":{\"n\":4}}\n");
        QCOMPARE(notes.size(), 4);
        QCOMPARE(notes.last().value(u"n"_s).toInt(), 4);
        QCOMPARE(bad.size(), 2);
        // An answer to no request.
        c.receive(R"({"jsonrpc":"2.0","id":99,"result":{}})"
                  "\n");
        QCOMPARE(bad.size(), 3);
    }

    void requestsAndClose()
    {
        QList<QByteArray> out;
        Connection c([&](const QByteArray &line) { out << line; });
        int code = 1;
        QString message;
        c.request(u"x"_s, {}, [&](const QJsonValue &, const Connection::Error &e) {
            code = e.code;
            message = e.message;
        });
        QCOMPARE(out.size(), 1);
        const QJsonObject sent = QJsonDocument::fromJson(out.first()).object();
        QCOMPARE(sent.value(u"jsonrpc"_s).toString(), u"2.0"_s);
        QCOMPARE(sent.value(u"method"_s).toString(), u"x"_s);
        c.close(u"gone"_s);
        QCOMPARE(code, Connection::ClosedCode);
        QCOMPARE(message, u"gone"_s);
        // A request of the other side without a handler: method not found.
        c.receive(R"({"jsonrpc":"2.0","id":"r1","method":"fs/read_text_file","params":{}})"
                  "\n");
        const QJsonObject answer = QJsonDocument::fromJson(out.last()).object();
        QCOMPARE(answer.value(u"id"_s).toString(), u"r1"_s);
        QCOMPARE(answer.value(u"error"_s).toObject().value(u"code"_s).toInt(), MethodNotFound);
    }

    void turn()
    {
        Agent agent;
        Client client(agent.sender(), u"0.25.0"_s);
        agent.client = &client;
        QSignalSpy progress(&client, &Client::progress);
        QSignalSpy status(&client, &Client::status);
        QSignalSpy finished(&client, &Client::finished);
        QSignalSpy failed(&client, &Client::failed);
        const QString key = u"AI/2026-11-20 Pytanie.md"_s;

        QVERIFY(client.prompt(key, input(u"Kontekst i pytanie"_s, u"Pytanie"_s)));
        QVERIFY(client.running(key));
        QVERIFY(!client.prompt(key, input(u"x"_s, u"x"_s))); // (one turn at a time)
        // The handshake first, nothing more before its answer.
        QCOMPARE(agent.sent.size(), 1);
        const QJsonObject init = agent.last(u"initialize"_s).value(u"params"_s).toObject();
        QCOMPARE(init.value(u"protocolVersion"_s).toInt(), 1);
        const QJsonObject caps = init.value(u"clientCapabilities"_s).toObject();
        QCOMPARE(caps.value(u"fs"_s).toObject().value(u"readTextFile"_s).toBool(true), false);
        QCOMPARE(caps.value(u"fs"_s).toObject().value(u"writeTextFile"_s).toBool(true), false);
        QCOMPARE(caps.value(u"terminal"_s).toBool(true), false);
        QCOMPARE(init.value(u"clientInfo"_s).toObject().value(u"version"_s).toString(), u"0.25.0"_s);

        agent.initialize();
        QCOMPARE(client.agentName(), u"Fake Agent"_s);
        QVERIFY(client.takesImages());
        const QJsonObject create = agent.last(u"session/new"_s).value(u"params"_s).toObject();
        QCOMPARE(create.value(u"cwd"_s).toString(), u"/home/ala/Notatki"_s);
        const QJsonObject server = create.value(u"mcpServers"_s).toArray().at(0).toObject();
        QCOMPARE(server.value(u"name"_s).toString(), u"brulion"_s);
        QCOMPARE(server.value(u"command"_s).toString(), u"/usr/bin/brulion-cli"_s);
        QCOMPARE(server.value(u"args"_s).toArray().size(), 3);
        QVERIFY(server.value(u"env"_s).isArray());
        QVERIFY(!client.hasSession(key));
        agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"s1"_s}});
        QVERIFY(client.hasSession(key));
        QCOMPARE(promptText(agent.last(u"session/prompt"_s)), u"Kontekst i pytanie"_s);
        QCOMPARE(agent.last(u"session/prompt"_s).value(u"params"_s).toObject().value(u"sessionId"_s).toString(), u"s1"_s);

        // The answer as it comes, a tool call in between, thoughts.
        agent.chunk(u"s1"_s, u"Myślę o tym…"_s, u"agent_thought_chunk"_s);
        agent.chunk(u"s1"_s, u"Sprawdzę notatki."_s);
        agent.update(u"s1"_s, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"t1"_s},
                               {u"title"_s, u"Read Dom.md"_s}, {u"kind"_s, u"read"_s}, {u"status"_s, u"pending"_s}});
        QCOMPARE(status.last().at(1).toString(), u"Read Dom.md"_s);
        agent.update(u"s1"_s, {{u"sessionUpdate"_s, u"tool_call_update"_s}, {u"toolCallId"_s, u"t1"_s},
                               {u"status"_s, u"completed"_s}});
        QCOMPARE(status.last().at(1).toString(), QString());
        agent.chunk(u"s1"_s, u"Jutro: zapłacić czynsz."_s);
        agent.chunk(u"other"_s, u"nie tej rozmowy"_s); // (an unknown session)
        QCOMPARE(progress.last().at(1).toString(), u"Sprawdzę notatki.\n\nJutro: zapłacić czynsz."_s);
        QCOMPARE(progress.last().at(2).toString(), u"Myślę o tym…"_s);
        agent.reply(agent.last(u"session/prompt"_s), QJsonObject{{u"stopReason"_s, u"end_turn"_s}});
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toString(), key);
        QCOMPARE(finished.first().at(1).toString(), u"Sprawdzę notatki.\n\nJutro: zapłacić czynsz."_s);
        QCOMPARE(finished.first().at(2).toString(), u"end_turn"_s);
        QVERIFY(!client.running(key));

        // The next turn: the same session, the next prompt.
        QVERIFY(client.prompt(key, input(u"Kontekst"_s, u"A pojutrze?"_s)));
        QCOMPARE(agent.count(u"session/new"_s), 1);
        QCOMPARE(agent.count(u"initialize"_s), 1);
        QCOMPARE(promptText(agent.last(u"session/prompt"_s)), u"A pojutrze?"_s);
        agent.chunk(u"s1"_s, u"Nic."_s);
        agent.reply(agent.last(u"session/prompt"_s), QJsonObject{{u"stopReason"_s, u"max_tokens"_s}});
        QCOMPARE(finished.size(), 2);
        QCOMPARE(finished.last().at(1).toString(), u"Nic."_s); // (the progress of the last turn not kept)
        QCOMPARE(failed.size(), 0);
    }

    void permission()
    {
        Agent agent;
        Client client(agent.sender());
        agent.client = &client;
        QSignalSpy asked(&client, &Client::permissionAsked);
        const QString key = u"k"_s;
        client.prompt(key, input(u"p"_s, u"p"_s));
        agent.initialize();
        agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"s"_s}});
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"w1"_s},
                              {u"title"_s, u"Write Dom.md"_s}, {u"status"_s, u"pending"_s}});
        const QJsonArray options{
                QJsonObject{{u"optionId"_s, u"allow"_s}, {u"name"_s, u"Allow"_s}, {u"kind"_s, u"allow_once"_s}},
                QJsonObject{{u"optionId"_s, u"always"_s}, {u"name"_s, u"Always allow"_s}, {u"kind"_s, u"allow_always"_s}},
                QJsonObject{{u"optionId"_s, u"reject"_s}, {u"name"_s, u"Reject"_s}, {u"kind"_s, u"reject_once"_s}}};
        agent.write({{u"id"_s, u"q1"_s},
                     {u"method"_s, u"session/request_permission"_s},
                     {u"params"_s,
                      QJsonObject{{u"sessionId"_s, u"s"_s},
                                  {u"toolCall"_s,
                                   QJsonObject{{u"toolCallId"_s, u"w1"_s},
                                               {u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"diff"_s},
                                                                                     {u"path"_s, u"/home/ala/Notatki/Dom.md"_s},
                                                                                     {u"oldText"_s, u"- czynsz"_s},
                                                                                     {u"newText"_s, u"- czynsz w piątek"_s}}}}}},
                                  {u"options"_s, options}}}});
        QCOMPARE(asked.size(), 1);
        const Permission p = asked.first().at(1).value<Permission>();
        QCOMPARE(p.title, u"Write Dom.md"_s); // (the call's title from its tool_call)
        QCOMPARE(p.diffs, (QList<Diff>{{u"/home/ala/Notatki/Dom.md"_s, u"- czynsz"_s, u"- czynsz w piątek"_s}}));
        QCOMPARE(p.details, u"/home/ala/Notatki/Dom.md\n- czynsz w piątek"_s);
        QCOMPARE(p.options.size(), 3);
        QCOMPARE(p.options.at(1).kind, u"allow_always"_s);
        // A second question waits behind the first.
        agent.write({{u"id"_s, 7},
                     {u"method"_s, u"session/request_permission"_s},
                     {u"params"_s, QJsonObject{{u"sessionId"_s, u"s"_s},
                                               {u"toolCall"_s, QJsonObject{{u"toolCallId"_s, u"x"_s},
                                                                           {u"title"_s, u"Run git status"_s},
                                                                           {u"rawInput"_s, QJsonObject{{u"command"_s, u"git status"_s}}}}},
                                               {u"options"_s, options}}}});
        QCOMPARE(asked.size(), 1);
        client.answer(key, u"allow"_s);
        QJsonObject answer = agent.sent.last();
        QCOMPARE(answer.value(u"id"_s).toString(), u"q1"_s);
        QCOMPARE(answer.value(u"result"_s).toObject().value(u"outcome"_s).toObject().value(u"outcome"_s).toString(), u"selected"_s);
        QCOMPARE(answer.value(u"result"_s).toObject().value(u"outcome"_s).toObject().value(u"optionId"_s).toString(), u"allow"_s);
        QCOMPARE(asked.size(), 2);
        const Permission second = asked.last().at(1).value<Permission>();
        QCOMPARE(second.title, u"Run git status"_s);
        QVERIFY(second.details.contains(u"\"command\": \"git status\""_s));
        // An option it did not offer: cancelled.
        client.answer(key, u"nonsense"_s);
        answer = agent.sent.last();
        QCOMPARE(answer.value(u"id"_s).toInt(), 7);
        QCOMPARE(answer.value(u"result"_s).toObject().value(u"outcome"_s).toObject().value(u"outcome"_s).toString(), u"cancelled"_s);
        // A question of a session that runs no turn, or without options: cancelled at once.
        agent.write({{u"id"_s, 8},
                     {u"method"_s, u"session/request_permission"_s},
                     {u"params"_s, QJsonObject{{u"sessionId"_s, u"s"_s}, {u"toolCall"_s, QJsonObject()}, {u"options"_s, QJsonArray()}}}});
        QCOMPARE(agent.sent.last().value(u"result"_s).toObject().value(u"outcome"_s).toObject().value(u"outcome"_s).toString(),
                 u"cancelled"_s);
        QCOMPARE(asked.size(), 2);
    }

    void readPermissionDetails()
    {
        // The input as JSON when there is no content; long details cut; options without a name.
        QJsonObject params{{u"toolCall"_s, QJsonObject{{u"toolCallId"_s, u"a"_s},
                                                       {u"rawInput"_s, QJsonObject{{u"text"_s, QString(MaxDetailChars * 2, u'ż')}}}}},
                           {u"options"_s, QJsonArray{QJsonObject{{u"optionId"_s, u"ok"_s}}, QJsonObject{{u"name"_s, u"no id"_s}}}}};
        const std::optional<Permission> p = readPermission(params, {{u"a"_s, u"Edit Zakupy.md"_s}});
        QVERIFY(p);
        QCOMPARE(p->title, u"Edit Zakupy.md"_s);
        QCOMPARE(p->details.size(), MaxDetailChars + 1);
        QVERIFY(p->details.endsWith(u'…'));
        QCOMPARE(p->options.size(), 1);
        QCOMPARE(p->options.first().name, u"ok"_s);
        // Text content.
        params.insert(u"toolCall"_s, QJsonObject{{u"content"_s, QJsonArray{QJsonObject{
                                                         {u"type"_s, u"content"_s},
                                                         {u"content"_s, QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"rm -rf ~"_s}}}}}}});
        QCOMPARE(readPermission(params, {})->details, u"rm -rf ~"_s);
        QCOMPARE(readPermission(params, {})->title, QString());
    }

    void cancel()
    {
        Agent agent;
        Client client(agent.sender());
        agent.client = &client;
        QSignalSpy finished(&client, &Client::finished);
        QSignalSpy failed(&client, &Client::failed);
        QSignalSpy progress(&client, &Client::progress);
        const QString key = u"k"_s;
        client.prompt(key, input(u"p"_s, u"p"_s));
        agent.initialize();
        agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"s"_s}});
        const QJsonObject first = agent.last(u"session/prompt"_s);
        agent.write({{u"id"_s, u"q"_s},
                     {u"method"_s, u"session/request_permission"_s},
                     {u"params"_s, QJsonObject{{u"sessionId"_s, u"s"_s},
                                               {u"toolCall"_s, QJsonObject{{u"title"_s, u"Write"_s}}},
                                               {u"options"_s, QJsonArray{QJsonObject{{u"optionId"_s, u"a"_s}}}}}}});
        client.cancel(key);
        QVERIFY(!client.running(key));
        // Its question answered "cancelled", then session/cancel.
        const QJsonObject answer = agent.sent.at(agent.sent.size() - 2);
        QCOMPARE(answer.value(u"id"_s).toString(), u"q"_s);
        QCOMPARE(answer.value(u"result"_s).toObject().value(u"outcome"_s).toObject().value(u"outcome"_s).toString(), u"cancelled"_s);
        QCOMPARE(agent.sent.last().value(u"method"_s).toString(), u"session/cancel"_s);
        QCOMPARE(agent.sent.last().value(u"params"_s).toObject().value(u"sessionId"_s).toString(), u"s"_s);
        // What it still says is not heard; a turn sent meanwhile waits for the cancelled one.
        agent.chunk(u"s"_s, u"jeszcze"_s);
        QCOMPARE(progress.size(), 0);
        QVERIFY(client.prompt(key, input(u"p"_s, u"Drugie pytanie"_s)));
        QCOMPARE(agent.count(u"session/prompt"_s), 1);
        agent.reply(first, QJsonObject{{u"stopReason"_s, u"cancelled"_s}});
        QCOMPARE(finished.size(), 0);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(agent.count(u"session/prompt"_s), 2);
        QCOMPARE(promptText(agent.last(u"session/prompt"_s)), u"Drugie pytanie"_s);
        agent.chunk(u"s"_s, u"Odpowiedź"_s);
        agent.reply(agent.last(u"session/prompt"_s), QJsonObject{{u"stopReason"_s, u"end_turn"_s}});
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(1).toString(), u"Odpowiedź"_s);

        // Cancelled before the handshake answered: nothing is asked of the agent for it.
        Agent other;
        Client late(other.sender());
        other.client = &late;
        QSignalSpy lateFailed(&late, &Client::failed);
        late.prompt(key, input(u"p"_s, u"p"_s));
        late.cancel(key);
        other.initialize();
        QCOMPARE(other.count(u"session/new"_s), 0);
        // Cancelled while its session is made: kept for the next turn, no prompt.
        late.prompt(key, input(u"p"_s, u"p"_s));
        late.cancel(key);
        other.reply(other.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"z"_s}});
        QCOMPARE(other.count(u"session/prompt"_s), 0);
        QVERIFY(late.hasSession(key));
        QCOMPARE(lateFailed.size(), 0);
    }

    void signIn()
    {
        Agent agent;
        Client client(agent.sender());
        agent.client = &client;
        QSignalSpy auth(&client, &Client::authRequired);
        QSignalSpy finished(&client, &Client::finished);
        const QString key = u"k"_s;
        client.prompt(key, input(u"p"_s, u"p"_s));
        agent.initialize({{u"authMethods"_s,
                           QJsonArray{QJsonObject{{u"id"_s, u"oauth"_s}, {u"name"_s, u"Log in with Google"_s},
                                                  {u"description"_s, u"Opens the browser"_s}},
                                      QJsonObject{{u"id"_s, u"login"_s}, {u"name"_s, u"Claude login"_s},
                                                  {u"type"_s, u"terminal"_s}, {u"args"_s, QJsonArray{u"/login"_s}}},
                                      QJsonObject{{u"name"_s, u"no id"_s}}}}});
        agent.replyError(agent.last(u"session/new"_s), AuthRequired, u"Authentication required"_s);
        QCOMPARE(auth.size(), 1);
        const auto methods = auth.first().at(1).value<QList<AuthMethod>>();
        QCOMPARE(methods.size(), 2);
        QCOMPARE(methods.first().description, u"Opens the browser"_s);
        QVERIFY(!methods.first().terminal);
        QVERIFY(methods.last().terminal);
        QCOMPARE(methods.last().args, QStringList{u"/login"_s});
        QCOMPARE(auth.first().at(2).toString(), u"Authentication required"_s);
        QVERIFY(client.running(key));
        client.authenticate(key, u"oauth"_s);
        QCOMPARE(agent.last(u"authenticate"_s).value(u"params"_s).toObject().value(u"methodId"_s).toString(), u"oauth"_s);
        agent.reply(agent.last(u"authenticate"_s), QJsonObject());
        QCOMPARE(agent.count(u"session/new"_s), 2);
        agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"s"_s}});
        agent.chunk(u"s"_s, u"Zalogowano."_s);
        agent.reply(agent.last(u"session/prompt"_s), QJsonObject{{u"stopReason"_s, u"end_turn"_s}});
        QCOMPARE(finished.size(), 1);
    }

    void failures()
    {
        // Another version of the protocol: every turn fails, now and later.
        {
            Agent agent;
            Client client(agent.sender());
            agent.client = &client;
            QSignalSpy failed(&client, &Client::failed);
            client.prompt(u"a"_s, input(u"p"_s, u"p"_s));
            client.prompt(u"b"_s, input(u"p"_s, u"p"_s));
            agent.initialize({{u"protocolVersion"_s, 2}});
            QCOMPARE(failed.size(), 2);
            QCOMPARE(failed.first().at(1).value<Failure>(), (Failure{Failure::Kind::Version, u"2"_s}));
            client.prompt(u"a"_s, input(u"p"_s, u"p"_s));
            QCOMPARE(failed.size(), 3);
            QCOMPARE(agent.count(u"session/new"_s), 0);
        }
        // A refusal, an empty answer, an error.
        {
            Agent agent;
            Client client(agent.sender());
            agent.client = &client;
            QSignalSpy failed(&client, &Client::failed);
            client.prompt(u"a"_s, input(u"p"_s, u"p"_s));
            agent.initialize();
            agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"s"_s}});
            agent.reply(agent.last(u"session/prompt"_s), QJsonObject{{u"stopReason"_s, u"refusal"_s}});
            QCOMPARE(failed.last().at(1).value<Failure>().kind, Failure::Kind::Refusal);
            client.prompt(u"a"_s, input(u"p"_s, u"p"_s));
            agent.reply(agent.last(u"session/prompt"_s), QJsonObject{{u"stopReason"_s, u"max_turn_requests"_s}});
            QCOMPARE(failed.last().at(1).value<Failure>(), (Failure{Failure::Kind::Empty, u"max_turn_requests"_s}));
            client.prompt(u"a"_s, input(u"p"_s, u"p"_s));
            agent.replyError(agent.last(u"session/prompt"_s), -32603, u"Internal error: limit reached"_s);
            QCOMPARE(failed.last().at(1).value<Failure>(), (Failure{Failure::Kind::Error, u"Internal error: limit reached"_s}));
            // A session/new without a session id.
            client.prompt(u"b"_s, input(u"p"_s, u"p"_s));
            agent.reply(agent.last(u"session/new"_s), QJsonObject());
            QCOMPARE(failed.last().at(1).value<Failure>().kind, Failure::Kind::Malformed);
            QCOMPARE(failed.size(), 4);
        }
        // The agent went away: the running turns fail, and every later one.
        {
            Agent agent;
            Client client(agent.sender());
            agent.client = &client;
            QSignalSpy failed(&client, &Client::failed);
            client.prompt(u"a"_s, input(u"p"_s, u"p"_s));
            agent.initialize();
            agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"s"_s}});
            client.closed(u"exit code 1: npx: command not found"_s);
            QCOMPARE(failed.size(), 1);
            QCOMPARE(failed.first().at(1).value<Failure>(),
                     (Failure{Failure::Kind::Closed, u"exit code 1: npx: command not found"_s}));
            QVERIFY(!client.hasSession(u"a"_s));
            QVERIFY(!client.running(u"a"_s));
            client.prompt(u"a"_s, input(u"p"_s, u"p"_s));
            QCOMPARE(failed.size(), 2);
        }
        // Gone before the handshake answered.
        {
            Agent agent;
            Client client(agent.sender());
            agent.client = &client;
            QSignalSpy failed(&client, &Client::failed);
            client.prompt(u"a"_s, input(u"p"_s, u"p"_s));
            client.closed(u"no such program"_s);
            QCOMPARE(failed.size(), 1);
            QCOMPARE(failed.first().at(1).value<Failure>().kind, Failure::Kind::Closed);
        }
    }

    void agentRequests()
    {
        Agent agent;
        Client client(agent.sender());
        agent.client = &client;
        client.prompt(u"a"_s, input(u"p"_s, u"p"_s));
        agent.initialize();
        agent.write({{u"id"_s, 5}, {u"method"_s, u"fs/write_text_file"_s},
                     {u"params"_s, QJsonObject{{u"path"_s, u"/etc/passwd"_s}, {u"content"_s, u"x"_s}}}});
        QCOMPARE(agent.sent.last().value(u"id"_s).toInt(), 5);
        QCOMPARE(agent.sent.last().value(u"error"_s).toObject().value(u"code"_s).toInt(), MethodNotFound);
        agent.write({{u"id"_s, 6}, {u"method"_s, u"terminal/create"_s}, {u"params"_s, QJsonObject()}});
        QCOMPARE(agent.sent.last().value(u"error"_s).toObject().value(u"code"_s).toInt(), MethodNotFound);
    }

    void sideBySide()
    {
        Agent agent;
        Client client(agent.sender());
        agent.client = &client;
        QSignalSpy finished(&client, &Client::finished);
        client.prompt(u"a"_s, input(u"pa"_s, u"pa"_s));
        client.prompt(u"b"_s, input(u"pb"_s, u"pb"_s));
        agent.initialize();
        QCOMPARE(agent.count(u"session/new"_s), 2);
        QList<QJsonObject> created;
        for (const QJsonObject &o : std::as_const(agent.sent)) {
            if (o.value(u"method"_s).toString() == u"session/new")
                created << o;
        }
        agent.reply(created.at(1), QJsonObject{{u"sessionId"_s, u"s2"_s}});
        agent.reply(created.at(0), QJsonObject{{u"sessionId"_s, u"s1"_s}});
        // Each session's prompt is its conversation's; what each session says goes to its turn.
        QHash<QString, QJsonObject> prompts; // by session
        for (const QJsonObject &o : std::as_const(agent.sent)) {
            if (o.value(u"method"_s).toString() == u"session/prompt")
                prompts.insert(o.value(u"params"_s).toObject().value(u"sessionId"_s).toString(), o);
        }
        QCOMPARE(prompts.size(), 2);
        const QString first = promptText(prompts.value(u"s1"_s)) == u"pa" ? u"a"_s : u"b"_s;
        const QString second = first == u"a" ? u"b"_s : u"a"_s;
        QCOMPARE(promptText(prompts.value(u"s2"_s)), u"p"_s + second);
        agent.chunk(u"s1"_s, u"Odpowiedź "_s + first);
        agent.chunk(u"s2"_s, u"Odpowiedź "_s + second);
        agent.reply(prompts.value(u"s2"_s), QJsonObject{{u"stopReason"_s, u"end_turn"_s}});
        agent.reply(prompts.value(u"s1"_s), QJsonObject{{u"stopReason"_s, u"end_turn"_s}});
        QCOMPARE(finished.size(), 2);
        QCOMPARE(finished.at(0).at(0).toString(), second);
        QCOMPARE(finished.at(0).at(1).toString(), u"Odpowiedź "_s + second);
        QCOMPARE(finished.at(1).at(0).toString(), first);
        QCOMPARE(finished.at(1).at(1).toString(), u"Odpowiedź "_s + first);
    }

    void options()
    {
        // configOptions (a flat list and groups, a bool), the older modes and models as options of
        // their own — only where no option of their kind came.
        const QJsonObject result{
                {u"sessionId"_s, u"s"_s},
                {u"configOptions"_s,
                 QJsonArray{QJsonObject{{u"id"_s, u"model"_s}, {u"name"_s, u"Model"_s}, {u"category"_s, u"model"_s},
                                        {u"type"_s, u"select"_s}, {u"currentValue"_s, u"sonnet"_s},
                                        {u"options"_s, QJsonArray{QJsonObject{{u"value"_s, u"sonnet"_s}, {u"name"_s, u"Sonnet 5.5"_s}},
                                                                  QJsonObject{{u"name"_s, u"Większe"_s},
                                                                              {u"options"_s, QJsonArray{QJsonObject{{u"value"_s, u"opus"_s},
                                                                                                                    {u"name"_s, u"Opus 5.5"_s},
                                                                                                                    {u"description"_s, u"Najlepszy"_s}}}}}}}},
                            QJsonObject{{u"id"_s, u"fast"_s}, {u"name"_s, u"Fast"_s}, {u"type"_s, u"boolean"_s}, {u"currentValue"_s, true}},
                            QJsonObject{{u"id"_s, u"odd"_s}, {u"type"_s, u"slider"_s}},
                            QJsonObject{{u"name"_s, u"no id"_s}, {u"type"_s, u"select"_s}}}},
                {u"modes"_s, QJsonObject{{u"currentModeId"_s, u"ask"_s},
                                         {u"availableModes"_s, QJsonArray{QJsonObject{{u"id"_s, u"ask"_s}, {u"name"_s, u"Ask"_s}},
                                                                          QJsonObject{{u"id"_s, u"code"_s}, {u"name"_s, u"Code"_s}}}}}},
                {u"models"_s, QJsonObject{{u"currentModelId"_s, u"x"_s},
                                          {u"availableModels"_s, QJsonArray{QJsonObject{{u"modelId"_s, u"x"_s}}}}}}};
        const QList<ConfigOption> options = readOptions(result);
        QCOMPARE(options.size(), 3); // model, fast, the modes (the models: a model option is there)
        QCOMPARE(options.at(0).choices.size(), 2);
        QCOMPARE(options.at(0).choices.at(1).group, u"Większe"_s);
        QCOMPARE(options.at(0).currentName(), u"Sonnet 5.5"_s);
        QVERIFY(options.at(1).boolean);
        QCOMPARE(options.at(1).current, QJsonValue(true));
        QCOMPARE(options.at(2).via, ConfigOption::Via::Mode);
        QCOMPARE(options.at(2).id, u"mode"_s);
        QCOMPARE(options.at(2).currentName(), u"Ask"_s);
        // Kept as JSON and read back.
        for (const ConfigOption &o : options)
            QCOMPARE(ConfigOption::fromJson(o.toJson()), std::optional<ConfigOption>(o));
        // Only models: an option of their own.
        const QList<ConfigOption> models = readOptions(QJsonObject{{u"models"_s, result.value(u"models"_s)}});
        QCOMPARE(models.size(), 1);
        QCOMPARE(models.first().via, ConfigOption::Via::Model);
        QCOMPARE(models.first().choices.first().name, u"x"_s);
    }

    void optionsSet()
    {
        Agent agent;
        Client client(agent.sender());
        agent.client = &client;
        QSignalSpy changed(&client, &Client::sessionChanged);
        QSignalSpy started(&client, &Client::sessionStarted);
        const QString key = u"k"_s;
        TurnInput in = input(u"p"_s, u"p"_s);
        in.config.insert(u"model"_s, u"opus"_s);
        in.config.insert(u"mode"_s, u"code"_s);
        in.config.insert(u"missing"_s, u"x"_s);  // (no such option: left out)
        client.setOption(key, u"fast"_s, false); // (before the session: set when it comes)
        client.prompt(key, in);
        agent.initialize();
        const QJsonArray configOptions{
                QJsonObject{{u"id"_s, u"model"_s}, {u"category"_s, u"model"_s}, {u"type"_s, u"select"_s}, {u"currentValue"_s, u"sonnet"_s},
                            {u"options"_s, QJsonArray{QJsonObject{{u"value"_s, u"sonnet"_s}}, QJsonObject{{u"value"_s, u"opus"_s}}}}},
                QJsonObject{{u"id"_s, u"fast"_s}, {u"type"_s, u"boolean"_s}, {u"currentValue"_s, true}}};
        agent.reply(agent.last(u"session/new"_s),
                    QJsonObject{{u"sessionId"_s, u"s"_s},
                                {u"configOptions"_s, configOptions},
                                {u"modes"_s, QJsonObject{{u"currentModeId"_s, u"ask"_s},
                                                         {u"availableModes"_s, QJsonArray{QJsonObject{{u"id"_s, u"ask"_s}},
                                                                                          QJsonObject{{u"id"_s, u"code"_s}}}}}}});
        QCOMPARE(started.size(), 1);
        QCOMPARE(started.first().at(2).toBool(), false);
        QCOMPARE(client.session(key).options.size(), 3);
        // One option at a time, the prompt after the last.
        QStringList sets;
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(agent.count(u"session/prompt"_s), 0);
            const QJsonObject last = agent.sent.last();
            const QString method = last.value(u"method"_s).toString();
            const QJsonObject params = last.value(u"params"_s).toObject();
            if (method == u"session/set_mode") {
                sets << u"mode="_s + params.value(u"modeId"_s).toString();
                agent.reply(last, QJsonObject());
            } else {
                QCOMPARE(method, u"session/set_config_option"_s);
                const QString id = params.value(u"configId"_s).toString();
                if (id == u"fast") {
                    QCOMPARE(params.value(u"type"_s).toString(), u"boolean"_s);
                    sets << u"fast="_s + (params.value(u"value"_s).toBool() ? u"on"_s : u"off"_s);
                } else {
                    sets << id + u'=' + params.value(u"value"_s).toString();
                }
                QJsonArray updated = configOptions;
                agent.reply(last, QJsonObject{{u"configOptions"_s, updated}});
            }
        }
        sets.sort();
        QCOMPARE(sets, (QStringList{u"fast=off"_s, u"mode=code"_s, u"model=opus"_s}));
        QCOMPARE(agent.count(u"session/prompt"_s), 1);
        QVERIFY(changed.size() >= 3);
        // The mode set locally (set_mode answers nothing), an update from the agent.
        const QList<ConfigOption> now = client.session(key).options;
        QCOMPARE(std::ranges::find_if(now, [](const ConfigOption &o) { return o.id == u"mode"; })->current,
                 QJsonValue(u"code"_s));
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"current_mode_update"_s}, {u"currentModeId"_s, u"ask"_s}});
        QCOMPARE(client.session(key).options.last().current, QJsonValue(u"ask"_s));
        // While the turn runs: at once.
        QVERIFY(client.setOption(key, u"model"_s, u"opus"_s)); // (its reply above said "sonnet")
        QCOMPARE(agent.sent.last().value(u"method"_s).toString(), u"session/set_config_option"_s);
        QVERIFY(!client.setOption(key, u"nonsense"_s, u"x"_s));
    }

    void resume()
    {
        // session/resume of an earlier run's session: no first prompt, no session/new.
        {
            Agent agent;
            Client client(agent.sender());
            agent.client = &client;
            QSignalSpy started(&client, &Client::sessionStarted);
            TurnInput in = input(u"Kontekst"_s, u"Dalej"_s);
            in.resume = u"old"_s;
            client.prompt(u"k"_s, in);
            agent.initialize({{u"agentCapabilities"_s, QJsonObject{{u"loadSession"_s, true},
                                                                  {u"sessionCapabilities"_s, QJsonObject{{u"resume"_s, QJsonObject()}}}}}});
            QVERIFY(client.resumes());
            const QJsonObject resume = agent.last(u"session/resume"_s);
            QCOMPARE(resume.value(u"params"_s).toObject().value(u"sessionId"_s).toString(), u"old"_s);
            QCOMPARE(resume.value(u"params"_s).toObject().value(u"cwd"_s).toString(), u"/home/ala/Notatki"_s);
            agent.reply(resume, QJsonObject());
            QCOMPARE(started.first().at(1).toString(), u"old"_s);
            QCOMPARE(started.first().at(2).toBool(), true);
            QCOMPARE(promptText(agent.last(u"session/prompt"_s)), u"Dalej"_s);
            QCOMPARE(agent.count(u"session/new"_s), 0);
        }
        // Only session/load: its replay is not heard; a load that fails: a new session, the first prompt.
        {
            Agent agent;
            Client client(agent.sender());
            agent.client = &client;
            QSignalSpy progress(&client, &Client::progress);
            TurnInput in = input(u"Kontekst"_s, u"Dalej"_s);
            in.resume = u"old"_s;
            client.prompt(u"k"_s, in);
            agent.initialize({{u"agentCapabilities"_s, QJsonObject{{u"loadSession"_s, true}}}});
            const QJsonObject load = agent.last(u"session/load"_s);
            QVERIFY(!load.isEmpty());
            agent.replyError(load, -32603, u"No such session"_s);
            agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"fresh"_s}});
            QCOMPARE(promptText(agent.last(u"session/prompt"_s)), u"Kontekst"_s);
            QCOMPARE(progress.size(), 0);
        }
        // An agent that resumes nothing: the id is not sent.
        {
            Agent agent;
            Client client(agent.sender());
            agent.client = &client;
            TurnInput in = input(u"Kontekst"_s, u"Dalej"_s);
            in.resume = u"old"_s;
            client.prompt(u"k"_s, in);
            agent.initialize();
            QVERIFY(agent.last(u"session/new"_s).value(u"params"_s).toObject().value(u"sessionId"_s).isUndefined());
        }
    }

    void sessionAndActivity()
    {
        Agent agent;
        Client client(agent.sender());
        agent.client = &client;
        QSignalSpy activity(&client, &Client::activityChanged);
        QSignalSpy session(&client, &Client::sessionChanged);
        QSignalSpy finished(&client, &Client::finished);
        const QString key = u"k"_s;
        client.prompt(key, input(u"p"_s, u"p"_s));
        agent.initialize();
        agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"s"_s}});
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"available_commands_update"_s},
                              {u"availableCommands"_s,
                               QJsonArray{QJsonObject{{u"name"_s, u"review"_s}, {u"description"_s, u"Przejrzyj zmiany"_s}},
                                          QJsonObject{{u"name"_s, u"/plan"_s}, {u"input"_s, QJsonObject{{u"hint"_s, u"czego dotyczy"_s}}}},
                                          QJsonObject{{u"name"_s, u"zły zapis"_s}}}}});
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"usage_update"_s}, {u"used"_s, 42552}, {u"size"_s, 1000000},
                              {u"cost"_s, QJsonObject{{u"amount"_s, 0.21}, {u"currency"_s, u"USD"_s}}}});
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"session_info_update"_s}, {u"title"_s, u"Czynsz"_s}});
        QCOMPARE(session.size(), 4); // (the session's start, then three)
        const SessionInfo info = client.session(key);
        QCOMPARE(info.commands.size(), 2);
        QCOMPARE(info.commands.at(1).name, u"plan"_s);
        QCOMPARE(info.commands.at(1).hint, u"czego dotyczy"_s);
        QCOMPARE(info.usage, (Usage{42552, 1000000, 0.21, u"USD"_s}));
        QCOMPARE(info.title, u"Czynsz"_s);

        // A plan, tool calls with their output, messages with their ids.
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"plan"_s},
                              {u"entries"_s, QJsonArray{QJsonObject{{u"content"_s, u"Przeczytać Dom.md"_s}, {u"status"_s, u"completed"_s}},
                                                        QJsonObject{{u"content"_s, u"Dopisać mleko"_s}, {u"status"_s, u"in_progress"_s}}}}});
        QCOMPARE(client.turn(key)->plan.size(), 2);
        QCOMPARE(client.turn(key)->plan.at(1).status, u"in_progress"_s);
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"agent_message_chunk"_s}, {u"messageId"_s, u"m1"_s},
                              {u"content"_s, QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"Najpierw."_s}}}});
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"t"_s}, {u"title"_s, u"Terminal"_s},
                              {u"kind"_s, u"execute"_s}, {u"status"_s, u"pending"_s},
                              {u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"terminal"_s}, {u"terminalId"_s, u"term-1"_s}}}}});
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"tool_call_update"_s}, {u"toolCallId"_s, u"t"_s}, {u"title"_s, u"ls -la"_s},
                              {u"status"_s, u"completed"_s},
                              {u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"content"_s},
                                                                    {u"content"_s, QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"Dom.md\nŻółw.md"_s}}}}}}});
        const ToolCall call = client.turn(key)->tools.first();
        QCOMPARE(call, (ToolCall{u"t"_s, u"ls -la"_s, u"execute"_s, u"completed"_s, u"term-1"_s, u"Dom.md\nŻółw.md"_s}));
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"e"_s}, {u"title"_s, u"Edit Dom.md"_s},
                              {u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"diff"_s}, {u"path"_s, u"Dom.md"_s},
                                                                    {u"oldText"_s, u"a"_s}, {u"newText"_s, u"b"_s}}}}});
        QCOMPARE(client.turn(key)->tools.last().diff, (std::optional<Diff>(Diff{u"Dom.md"_s, u"a"_s, u"b"_s})));
        QCOMPARE(client.turn(key)->tools.last().output, u"Dom.md"_s);
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"agent_message_chunk"_s}, {u"messageId"_s, u"m2"_s},
                              {u"content"_s, QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"Potem."_s}}}});
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"agent_message_chunk"_s}, {u"messageId"_s, u"m3"_s},
                              {u"content"_s, QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"Koniec."_s}}}});
        QCOMPARE(client.turn(key)->text, u"Najpierw.\n\nPotem.\n\nKoniec."_s);
        QVERIFY(activity.size() >= 3);
        agent.reply(agent.last(u"session/prompt"_s), QJsonObject{{u"stopReason"_s, u"end_turn"_s}});
        QCOMPARE(finished.size(), 1);
        QVERIFY(!client.turn(key));

        // A turn of tool calls only ends without words; one with neither fails.
        QSignalSpy failed(&client, &Client::failed);
        client.prompt(key, input(u"p"_s, u"p"_s));
        agent.update(u"s"_s, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"w"_s}, {u"title"_s, u"Write Dom.md"_s}});
        agent.reply(agent.last(u"session/prompt"_s), QJsonObject{{u"stopReason"_s, u"end_turn"_s}});
        QCOMPARE(finished.size(), 2);
        QCOMPARE(finished.last().at(1).toString(), QString());
        QCOMPARE(failed.size(), 0);
    }

    void filesAndTerminals()
    {
        Agent agent;
        Client client(agent.sender());
        agent.client = &client;
        client.setCapabilities({true, true});
        QList<QStringList> handled;
        client.setHandler([&](const QString &key, const QJsonValue &id, const QString &method, const QJsonObject &params) {
            handled << QStringList{key, method, params.value(u"path"_s).toString()};
            client.respond(id, QJsonObject{{u"content"_s, u"- [ ] czynsz"_s}});
        });
        client.prompt(u"k"_s, input(u"p"_s, u"p"_s));
        const QJsonObject caps = agent.last(u"initialize"_s).value(u"params"_s).toObject().value(u"clientCapabilities"_s).toObject();
        QVERIFY(caps.value(u"fs"_s).toObject().value(u"readTextFile"_s).toBool());
        QVERIFY(caps.value(u"fs"_s).toObject().value(u"writeTextFile"_s).toBool());
        QVERIFY(caps.value(u"terminal"_s).toBool());
        agent.initialize();
        agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"s"_s}});
        agent.write({{u"id"_s, 10}, {u"method"_s, u"fs/read_text_file"_s},
                     {u"params"_s, QJsonObject{{u"sessionId"_s, u"s"_s}, {u"path"_s, u"/home/ala/Notatki/Dom.md"_s}}}});
        QCOMPARE(handled.size(), 1);
        QCOMPARE(handled.first(), (QStringList{u"k"_s, u"fs/read_text_file"_s, u"/home/ala/Notatki/Dom.md"_s}));
        QCOMPARE(agent.sent.last().value(u"result"_s).toObject().value(u"content"_s).toString(), u"- [ ] czynsz"_s);
        // Of a session it does not know: refused, not handed on.
        agent.write({{u"id"_s, 11}, {u"method"_s, u"terminal/create"_s},
                     {u"params"_s, QJsonObject{{u"sessionId"_s, u"obcy"_s}, {u"command"_s, u"ls"_s}}}});
        QCOMPARE(handled.size(), 1);
        QCOMPARE(agent.sent.last().value(u"error"_s).toObject().value(u"code"_s).toInt(), InvalidParams);
        agent.write({{u"id"_s, 12}, {u"method"_s, u"terminal/create"_s},
                     {u"params"_s, QJsonObject{{u"sessionId"_s, u"s"_s}, {u"command"_s, u"ls"_s}}}});
        QCOMPARE(handled.size(), 2);
    }

    void promptBlocks()
    {
        // Pictures and embedded text as the agent takes them, else as links to their files.
        Agent agent;
        Client client(agent.sender());
        agent.client = &client;
        TurnInput in;
        in.cwd = u"/n"_s;
        in.prompt << Block::image("jpg", u"image/jpeg"_s, u"file:///n/a.jpg"_s, u"a.jpg"_s)
                  << Block::resource(u"file:///n/Dom.md"_s, u"Dom.md"_s, u"- czynsz"_s, u"text/markdown"_s);
        client.prompt(u"k"_s, in);
        agent.initialize({{u"agentCapabilities"_s,
                           QJsonObject{{u"promptCapabilities"_s, QJsonObject{{u"image"_s, false}, {u"embeddedContext"_s, false}}}}}});
        agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"s"_s}});
        const QJsonArray sent = agent.last(u"session/prompt"_s).value(u"params"_s).toObject().value(u"prompt"_s).toArray();
        QCOMPARE(sent.at(0).toObject().value(u"type"_s).toString(), u"resource_link"_s);
        QCOMPARE(sent.at(0).toObject().value(u"uri"_s).toString(), u"file:///n/a.jpg"_s);
        QCOMPARE(sent.at(1).toObject().value(u"type"_s).toString(), u"resource_link"_s);
        const QJsonObject embedded = in.prompt.at(1).toJson(true, true);
        QCOMPARE(embedded.value(u"type"_s).toString(), u"resource"_s);
        QCOMPARE(embedded.value(u"resource"_s).toObject().value(u"text"_s).toString(), u"- czynsz"_s);
        QCOMPARE(in.prompt.at(0).toJson(true, true).value(u"type"_s).toString(), u"image"_s);
    }

    void openForgetRename()
    {
        Agent agent;
        Client client(agent.sender());
        agent.client = &client;
        QSignalSpy opened(&client, &Client::opened);
        QVERIFY(client.open(u"k"_s, input(u"p"_s, u"p"_s)));
        QVERIFY(!client.running(u"k"_s));
        agent.initialize();
        agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"s"_s}});
        QCOMPARE(opened.size(), 1);
        QCOMPARE(agent.count(u"session/prompt"_s), 0);
        QVERIFY(client.hasSession(u"k"_s));
        // A prompt while open() makes its session takes it over.
        QVERIFY(client.open(u"j"_s, input(u"p"_s, u"p"_s)));
        QVERIFY(client.prompt(u"j"_s, input(u"pierwszy"_s, u"p"_s)));
        agent.reply(agent.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"t"_s}});
        QCOMPARE(promptText(agent.last(u"session/prompt"_s)), u"pierwszy"_s);
        QCOMPARE(opened.size(), 1);
        // Renamed: the session goes along; forgotten: a new one next time.
        client.rename(u"k"_s, u"k2"_s);
        QVERIFY(!client.hasSession(u"k"_s));
        QCOMPARE(client.sessionId(u"k2"_s), u"s"_s);
        QCOMPARE(client.keyOf(u"s"_s), u"k2"_s);
        client.forget(u"k2"_s);
        QVERIFY(!client.hasSession(u"k2"_s));
        client.forget(u"j"_s); // (a turn runs: kept)
        QVERIFY(client.hasSession(u"j"_s));
        // Closed (the conversation deleted): its turn cancelled, session/close where the agent closes.
        QSignalSpy failed(&client, &Client::failed);
        client.close(u"j"_s);
        QCOMPARE(agent.sent.last().value(u"method"_s).toString(), u"session/cancel"_s); // (no close offered)
        QVERIFY(!client.hasSession(u"j"_s));
        agent.reply(agent.last(u"session/prompt"_s), QJsonObject{{u"stopReason"_s, u"cancelled"_s}});
        QCOMPARE(failed.size(), 0);
        Agent closing;
        Client other(closing.sender());
        closing.client = &other;
        other.open(u"k"_s, input(u"p"_s, u"p"_s));
        closing.initialize({{u"agentCapabilities"_s, QJsonObject{{u"sessionCapabilities"_s, QJsonObject{{u"close"_s, QJsonObject()}}}}}});
        closing.reply(closing.last(u"session/new"_s), QJsonObject{{u"sessionId"_s, u"z"_s}});
        other.close(u"k"_s);
        QCOMPARE(closing.last(u"session/close"_s).value(u"params"_s).toObject().value(u"sessionId"_s).toString(), u"z"_s);
    }

    void blocks()
    {
        QCOMPARE(Block::textBlock(u"Cześć"_s).toJson(), (QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"Cześć"_s}}));
        QCOMPARE(Block::link(u"file:///home/ala/Notatki/Dom.md"_s, u"Dom.md"_s, u"text/markdown"_s).toJson(),
                 (QJsonObject{{u"type"_s, u"resource_link"_s},
                              {u"uri"_s, u"file:///home/ala/Notatki/Dom.md"_s},
                              {u"name"_s, u"Dom.md"_s},
                              {u"mimeType"_s, u"text/markdown"_s}}));
        QVERIFY(!Block::link(u"file:///a"_s, u"a"_s).toJson().contains(u"mimeType"_s));
        const QJsonObject image = Block::image(QByteArray("\xff\xd8\xff", 3), u"image/jpeg"_s).toJson();
        QCOMPARE(image.value(u"type"_s).toString(), u"image"_s);
        QCOMPARE(image.value(u"data"_s).toString(), u"/9j/"_s);
        QCOMPARE(image.value(u"mimeType"_s).toString(), u"image/jpeg"_s);
    }

    void turnState()
    {
        // Non-text chunks, a status that changes with its call's title, a call in progress after
        // another one ended.
        TurnState s;
        QVERIFY(!s.apply({{u"sessionUpdate"_s, u"agent_message_chunk"_s},
                          {u"content"_s, QJsonObject{{u"type"_s, u"image"_s}, {u"data"_s, u""_s}}}}));
        QVERIFY(!s.apply({{u"sessionUpdate"_s, u"plan"_s}, {u"entries"_s, QJsonArray()}}));
        QVERIFY(s.apply({{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"1"_s}, {u"title"_s, u"Search"_s}}));
        QCOMPARE(s.status, u"Search"_s);
        QVERIFY(s.apply({{u"sessionUpdate"_s, u"tool_call_update"_s}, {u"toolCallId"_s, u"1"_s}, {u"title"_s, u"Search “czynsz”"_s}}));
        QCOMPARE(s.status, u"Search “czynsz”"_s);
        QVERIFY(!s.afterTool); // (no text before it)
        QVERIFY(s.apply({{u"sessionUpdate"_s, u"agent_message_chunk"_s},
                         {u"content"_s, QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"Mam.\n"_s}}}}));
        QVERIFY(s.apply({{u"sessionUpdate"_s, u"tool_call_update"_s}, {u"toolCallId"_s, u"1"_s}, {u"status"_s, u"failed"_s}}));
        QCOMPARE(s.status, QString());
        QVERIFY(s.apply({{u"sessionUpdate"_s, u"agent_message_chunk"_s},
                         {u"content"_s, QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"Dalej"_s}}}}));
        QCOMPARE(s.text, u"Mam.\n\nDalej"_s);
    }
};

QTEST_GUILESS_MAIN(TestAcp)
#include "tst_acp.moc"
