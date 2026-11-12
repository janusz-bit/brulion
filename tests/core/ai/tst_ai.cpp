// AI for extensions (src/core/ai/ai.h): b.ai's requests, the HTTP requests to Anthropic's
// Messages API and to OpenAI-compatible chat APIs, the answers, refusals and errors, and the
// client over a fake transport.

#include "ai/ai.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;

namespace {

class FakeTransport : public sync::HttpTransport
{
public:
    QList<sync::HttpRequest> sent;
    sync::HttpResponse answer;
    void send(const sync::HttpRequest &request, sync::HttpCallback done) override
    {
        sent << request;
        const sync::HttpResponse response = answer;
        QTimer::singleShot(0, [done, response] { done(response); });
    }
};

QJsonObject bodyOf(const sync::HttpRequest &r) { return QJsonDocument::fromJson(r.body).object(); }

ai::Settings claude()
{
    ai::Settings s;
    s.apiKey = u"sk-ant-test"_s;
    return s;
}

ai::Request ask(const QString &prompt)
{
    ai::Request r;
    r.messages << ai::textMessage(u"user"_s, prompt);
    return r;
}

sync::HttpResponse json(int status, const QByteArray &body)
{
    sync::HttpResponse r;
    r.status = status;
    r.body = body;
    return r;
}

} // namespace

class TestAi : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void requestsFromScripts()
    {
        QString error;
        std::optional<ai::Request> r = ai::requestFrom({{u"prompt"_s, u"Streść: żółw"_s}, {u"system"_s, u"Krótko."_s},
                                                        {u"maxTokens"_s, 300}, {u"effort"_s, u"low"_s}, {u"model"_s, u"m"_s}},
                                                       &error);
        QVERIFY2(r, qPrintable(error));
        QCOMPARE(r->messages.size(), 1);
        QCOMPARE(r->messages[0].role, u"user"_s);
        QCOMPARE(r->messages[0].content, u"Streść: żółw"_s);
        QCOMPARE(r->system, u"Krótko."_s);
        QCOMPARE(r->maxTokens, 300);
        QCOMPARE(r->effort, u"low"_s);
        QCOMPARE(r->model, u"m"_s);
        // A conversation.
        r = ai::requestFrom({{u"messages"_s, QVariantList{QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"a"_s}},
                                                          QVariantMap{{u"role"_s, u"assistant"_s}, {u"content"_s, u"b"_s}},
                                                          QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"c"_s}}}}},
                            &error);
        QVERIFY2(r, qPrintable(error));
        QCOMPARE(r->messages.size(), 3);
        QCOMPARE(r->maxTokens, 0);
        // Refused, each with why.
        const auto refused = [&](const QVariantMap &table, const QString &why) {
            QString e;
            const bool none = !ai::requestFrom(table, &e);
            if (!none || !e.contains(why))
                qWarning("%s", qPrintable(e));
            return none && e.contains(why);
        };
        QVERIFY(refused({}, u"needs a prompt"_s));
        QVERIFY(refused({{u"prompt"_s, 5}}, u"prompt must be a text"_s));
        QVERIFY(refused({{u"messages"_s, QVariantList{QVariantMap{{u"role"_s, u"system"_s}, {u"content"_s, u"x"_s}}}}},
                        u"role must be"_s));
        QVERIFY(refused({{u"messages"_s, QVariantList{QVariantMap{{u"role"_s, u"assistant"_s}, {u"content"_s, u"x"_s}}}}},
                        u"last message must be the user's"_s));
        QVERIFY(refused({{u"prompt"_s, QString(ai::MaxRequestChars + 1, u'a')}}, u"too long"_s));
        QVERIFY(refused({{u"prompt"_s, u"x"_s}, {u"maxTokens"_s, 0}}, u"maxTokens"_s));
        QVERIFY(refused({{u"prompt"_s, u"x"_s}, {u"maxTokens"_s, 2.5}}, u"maxTokens"_s));
        QVERIFY(refused({{u"prompt"_s, u"x"_s}, {u"effort"_s, u"ogromny"_s}}, u"effort must be"_s));
    }

    void anthropicRequests()
    {
        QString error;
        ai::Request request = ask(u"Cześć"_s);
        request.system = u"Odpowiadaj po polsku."_s;
        request.effort = u"low"_s;
        std::optional<sync::HttpRequest> http = ai::httpRequest(claude(), request, &error);
        QVERIFY2(http, qPrintable(error));
        QCOMPARE(http->method, QByteArray("POST"));
        QCOMPARE(http->url.toString(), u"https://api.anthropic.com/v1/messages"_s);
        QCOMPARE(http->header("x-api-key"), QByteArray("sk-ant-test"));
        QCOMPARE(http->header("anthropic-version"), QByteArray("2026-10-21"));
        QCOMPARE(http->header("content-type"), QByteArray("application/json"));
        QCOMPARE(http->header("anthropic-beta"), QByteArray("server-side-fallback-2026-07-01"));
        QVERIFY(!http->hasHeader("anthropic-dangerous-direct-browser-access"));
        QVERIFY(http->timeoutMs > 0);
        QJsonObject body = bodyOf(*http);
        QCOMPARE(body.value(u"model"_s).toString(), u"claude-opus-5-5"_s);
        QCOMPARE(body.value(u"max_tokens"_s).toInt(), ai::DefaultMaxTokens);
        QCOMPARE(body.value(u"system"_s).toString(), u"Odpowiadaj po polsku."_s);
        QCOMPARE(body.value(u"fallbacks"_s).toString(), u"default"_s);
        QCOMPARE(body.value(u"output_config"_s).toObject().value(u"effort"_s).toString(), u"low"_s);
        QCOMPARE(body.value(u"messages"_s).toArray().first().toObject().value(u"content"_s).toString(), u"Cześć"_s);
        QVERIFY(!body.contains(u"temperature"_s)); // refused by the API version
        // A model without server-side fallbacks (Haiku), the browser's header, another address.
        ai::Settings s = claude();
        s.model = u"claude-haiku-5-5"_s;
        s.browser = true;
        s.baseUrl = u"https://proxy.example/anthropic/"_s;
        http = ai::httpRequest(s, ask(u"x"_s), &error);
        QVERIFY(http);
        QCOMPARE(http->url.toString(), u"https://proxy.example/anthropic/v1/messages"_s);
        QVERIFY(!bodyOf(*http).contains(u"fallbacks"_s));
        QVERIFY(!http->hasHeader("anthropic-beta"));
        QCOMPARE(http->header("anthropic-dangerous-direct-browser-access"), QByteArray("true"));
        QVERIFY(!bodyOf(*http).contains(u"system"_s));
        // A request's own model wins over the settings'.
        request.model = u"claude-sonnet-5-5"_s;
        QCOMPARE(bodyOf(*ai::httpRequest(s, request, &error)).value(u"model"_s).toString(), u"claude-sonnet-5-5"_s);
        // Nothing to send without a key.
        QVERIFY(!ai::httpRequest(ai::Settings(), ask(u"x"_s), &error));
        QVERIFY2(error.contains(u"no API key"_s), qPrintable(error));
    }

    void openAiCompatibleRequests()
    {
        QString error;
        ai::Settings s;
        s.provider = u"openai"_s;
        s.apiKey = u"sk-test"_s;
        s.model = u"gpt-test"_s;
        ai::Request request = ask(u"Cześć"_s);
        request.system = u"Krótko."_s;
        request.maxTokens = 500;
        std::optional<sync::HttpRequest> http = ai::httpRequest(s, request, &error);
        QVERIFY2(http, qPrintable(error));
        QCOMPARE(http->url.toString(), u"https://api.openai.com/v1/chat/completions"_s);
        QCOMPARE(http->header("authorization"), QByteArray("Bearer sk-test"));
        QJsonObject body = bodyOf(*http);
        QCOMPARE(body.value(u"model"_s).toString(), u"gpt-test"_s);
        const QJsonArray messages = body.value(u"messages"_s).toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages[0].toObject().value(u"role"_s).toString(), u"system"_s);
        QCOMPARE(messages[1].toObject().value(u"content"_s).toString(), u"Cześć"_s);
        QCOMPARE(body.value(u"max_completion_tokens"_s).toInt(), 500);
        // A server of one's own: no key, its own address, max_tokens.
        s.apiKey.clear();
        s.baseUrl = u"http://192.168.1.10:11434/v1/"_s;
        s.model = u"llama3.2"_s;
        http = ai::httpRequest(s, request, &error);
        QVERIFY2(http, qPrintable(error));
        QCOMPARE(http->url.toString(), u"http://192.168.1.10:11434/v1/chat/completions"_s);
        QVERIFY(!http->hasHeader("authorization"));
        QCOMPARE(bodyOf(*http).value(u"max_tokens"_s).toInt(), 500);
        // A model is needed; an address must be one.
        s.model.clear();
        QVERIFY(!ai::httpRequest(s, request, &error));
        QVERIFY2(error.contains(u"no model"_s), qPrintable(error));
        s.model = u"m"_s;
        s.baseUrl = u"ftp://x"_s;
        QVERIFY(!ai::httpRequest(s, request, &error));
        QVERIFY2(error.contains(u"not a web address"_s), qPrintable(error));
    }

    void answers()
    {
        const ai::Settings anthropic = claude();
        ai::Reply r = ai::readReply(
                anthropic, json(200, R"({"model":"claude-opus-5-5","stop_reason":"end_turn","content":[)"
                                     R"({"type":"thinking","thinking":""},{"type":"text","text":"Żółw "},)"
                                     R"({"type":"text","text":"to gad."}],"usage":{"input_tokens":12,"output_tokens":7}})"));
        QVERIFY(r.ok);
        QCOMPARE(r.text, u"Żółw to gad."_s);
        QCOMPARE(r.model, u"claude-opus-5-5"_s);
        QCOMPARE(r.inputTokens, 12);
        QCOMPARE(r.outputTokens, 7);
        const QVariantMap v = r.toVariant();
        QCOMPARE(v.value(u"text"_s).toString(), u"Żółw to gad."_s);
        QVERIFY(!v.contains(u"error"_s));
        // A refusal is an error, never an empty answer.
        r = ai::readReply(anthropic, json(200, R"({"stop_reason":"refusal","content":[]})"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(u"declined"_s));
        // HTTP errors with the provider's message; no connection; no answer in time.
        r = ai::readReply(anthropic, json(401, R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})"));
        QCOMPARE(r.error, u"the API key was refused (HTTP 401): invalid x-api-key"_s);
        QCOMPARE(r.toVariant().value(u"error"_s).toString(), r.error);
        r = ai::readReply(anthropic, json(529, "<html>"));
        QVERIFY2(r.error.startsWith(u"the provider is overloaded"_s), qPrintable(r.error));
        sync::HttpResponse offline;
        offline.networkError = u"Host not found"_s;
        QCOMPARE(ai::readReply(anthropic, offline).error, u"no connection: Host not found"_s);
        offline.timedOut = true;
        QVERIFY(ai::readReply(anthropic, offline).error.startsWith(u"no answer in"_s));
        QVERIFY(!ai::readReply(anthropic, json(200, "nie json")).ok);

        ai::Settings openai;
        openai.provider = u"openai"_s;
        r = ai::readReply(openai, json(200, R"({"model":"gpt-test","choices":[{"finish_reason":"stop",)"
                                            R"("message":{"role":"assistant","content":"Cześć!","refusal":null}}],)"
                                            R"("usage":{"prompt_tokens":3,"completion_tokens":2}})"));
        QVERIFY(r.ok);
        QCOMPARE(r.text, u"Cześć!"_s);
        QCOMPARE(r.stopReason, u"stop"_s);
        QCOMPARE(r.outputTokens, 2);
        r = ai::readReply(openai, json(200, R"({"choices":[{"message":{"content":null,"refusal":"No."}}]})"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(u"declined"_s));
        r = ai::readReply(openai, json(429, R"({"error":{"message":"Rate limit reached","type":"requests"}})"));
        QVERIFY2(r.error.endsWith(u"Rate limit reached"_s), qPrintable(r.error));
        QVERIFY(!ai::readReply(openai, json(200, R"({"choices":[]})")).ok);
    }

    // Tools (an agent's loop): a script's table → each provider's request, the assistant's turn sent
    // back verbatim (thinking blocks included), one turn's results in one message, the calls read.
    void tools()
    {
        // The conversation of a loop: the question, the assistant's turn as it came, the results.
        const QString anthropicTurn = QString::fromUtf8(
                R"([{"type":"thinking","thinking":"","signature":"sig=="},)"
                R"({"type":"text","text":"Szukam."},)"
                R"({"type":"tool_use","id":"toolu_1","name":"search_notes","input":{"query":"żółw"}}])");
        const QVariantMap schema{{u"type"_s, u"object"_s},
                                 {u"properties"_s, QVariantMap{{u"query"_s, QVariantMap{{u"type"_s, u"string"_s}}}}},
                                 {u"required"_s, QVariantList{u"query"_s}}};
        const QVariantMap table{
                {u"system"_s, u"Notatki."_s},
                {u"tools"_s, QVariantList{QVariantMap{{u"name"_s, u"search_notes"_s},
                                                      {u"description"_s, u"Szuka w notatkach."_s},
                                                      {u"input"_s, schema}},
                                          // `{}` of a script reads as an empty list: an empty object.
                                          QVariantMap{{u"name"_s, u"today"_s},
                                                      {u"description"_s, u"Dzisiejsza data."_s},
                                                      {u"input"_s, QVariantMap{{u"properties"_s, QVariantList{}}}}}}},
                {u"messages"_s,
                 QVariantList{QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"Co wiem o żółwiach?"_s}},
                              QVariantMap{{u"role"_s, u"assistant"_s}, {u"raw"_s, anthropicTurn}},
                              QVariantMap{{u"role"_s, u"user"_s},
                                          {u"results"_s, QVariantList{QVariantMap{{u"id"_s, u"toolu_1"_s},
                                                                                  {u"content"_s, u"[[Żółw]]: gad"_s}},
                                                                      QVariantMap{{u"id"_s, u"toolu_2"_s},
                                                                                  {u"content"_s, u"no such page"_s},
                                                                                  {u"isError"_s, true}}}}}}}};
        QString error;
        const std::optional<ai::Request> request = ai::requestFrom(table, &error);
        QVERIFY2(request, qPrintable(error));
        QCOMPARE(request->tools.size(), 2);
        QCOMPARE(request->messages.size(), 3);
        QCOMPARE(request->messages[2].results.size(), 2);
        QVERIFY(request->messages[2].results[1].isError);

        std::optional<sync::HttpRequest> http = ai::httpRequest(claude(), *request, &error);
        QVERIFY2(http, qPrintable(error));
        QJsonObject body = bodyOf(*http);
        const QJsonArray tools = body.value(u"tools"_s).toArray();
        QCOMPARE(tools.size(), 2);
        QCOMPARE(tools[0].toObject().value(u"name"_s).toString(), u"search_notes"_s);
        QCOMPARE(tools[0].toObject()
                         .value(u"input_schema"_s)
                         .toObject()
                         .value(u"required"_s)
                         .toArray()
                         .first()
                         .toString(),
                 u"query"_s);
        QVERIFY(tools[1].toObject().value(u"input_schema"_s).toObject().value(u"properties"_s).isObject());
        QCOMPARE(tools[1].toObject().value(u"input_schema"_s).toObject().value(u"type"_s).toString(), u"object"_s);
        QVERIFY(!body.contains(u"tool_choice"_s)); // (forced tool use is refused by the current models)
        QJsonArray messages = body.value(u"messages"_s).toArray();
        QCOMPARE(messages.size(), 3);
        // The assistant's content blocks as they came: the thinking block and its signature too.
        QCOMPARE(messages[1].toObject().value(u"content"_s).toArray(),
                 QJsonDocument::fromJson(anthropicTurn.toUtf8()).array());
        const QJsonArray results = messages[2].toObject().value(u"content"_s).toArray();
        QCOMPARE(messages[2].toObject().value(u"role"_s).toString(), u"user"_s);
        QCOMPARE(results.size(), 2);
        QCOMPARE(results[0].toObject().value(u"type"_s).toString(), u"tool_result"_s);
        QCOMPARE(results[0].toObject().value(u"tool_use_id"_s).toString(), u"toolu_1"_s);
        QCOMPARE(results[0].toObject().value(u"content"_s).toString(), u"[[Żółw]]: gad"_s);
        QVERIFY(!results[0].toObject().contains(u"is_error"_s));
        QVERIFY(results[1].toObject().value(u"is_error"_s).toBool());

        // An OpenAI-compatible API: functions; the assistant's message as it came; a "tool"
        // message per result.
        ai::Settings openai;
        openai.provider = u"openai"_s;
        openai.model = u"qwen3"_s;
        openai.baseUrl = u"http://192.168.1.10:11434/v1"_s;
        // (Built, not written out: the arguments are JSON in a JSON text.)
        const QJsonObject openAiCall{{u"id"_s, u"call_1"_s},
                                     {u"type"_s, u"function"_s},
                                     {u"function"_s, QJsonObject{{u"name"_s, u"search_notes"_s},
                                                                 {u"arguments"_s, u"{\"query\":\"żółw\"}"_s}}}};
        const QString openAiTurn =
                QString::fromUtf8(QJsonDocument(QJsonObject{{u"role"_s, u"assistant"_s},
                                                            {u"content"_s, QJsonValue::Null},
                                                            {u"tool_calls"_s, QJsonArray{openAiCall}}})
                                          .toJson(QJsonDocument::Compact));
        QVariantMap second = table;
        QVariantList conversation = second.value(u"messages"_s).toList();
        conversation[1] = QVariantMap{{u"role"_s, u"assistant"_s}, {u"raw"_s, openAiTurn}};
        second.insert(u"messages"_s, conversation);
        const std::optional<ai::Request> openAiRequest = ai::requestFrom(second, &error);
        QVERIFY2(openAiRequest, qPrintable(error));
        http = ai::httpRequest(openai, *openAiRequest, &error);
        QVERIFY2(http, qPrintable(error));
        body = bodyOf(*http);
        const QJsonObject function = body.value(u"tools"_s).toArray().first().toObject();
        QCOMPARE(function.value(u"type"_s).toString(), u"function"_s);
        QCOMPARE(function.value(u"function"_s).toObject().value(u"name"_s).toString(), u"search_notes"_s);
        QVERIFY(function.value(u"function"_s).toObject().value(u"parameters"_s).isObject());
        messages = body.value(u"messages"_s).toArray();
        // system, user, the assistant's message, a tool message per result
        QCOMPARE(messages.size(), 5);
        QCOMPARE(messages[2].toObject(), QJsonDocument::fromJson(openAiTurn.toUtf8()).object());
        QCOMPARE(messages[3].toObject().value(u"role"_s).toString(), u"tool"_s);
        QCOMPARE(messages[3].toObject().value(u"tool_call_id"_s).toString(), u"toolu_1"_s);
        QCOMPARE(messages[4].toObject().value(u"content"_s).toString(), u"Error: no such page"_s);
        // A turn of the other provider (the provider changed mid-conversation): its text.
        http = ai::httpRequest(claude(), *openAiRequest, &error);
        QVERIFY(http);
        QVERIFY(bodyOf(*http).value(u"messages"_s).toArray()[1].toObject().value(u"content"_s).isString());

        // Reading the calls: Anthropic's tool_use blocks, the turn to send back.
        ai::Reply reply =
                ai::readReply(claude(), json(200, R"({"model":"claude-opus-5-5","stop_reason":"tool_use","content":)"
                                                          + anthropicTurn.toUtf8() + "}"));
        QVERIFY(reply.ok);
        QCOMPARE(reply.stopReason, u"tool_use"_s);
        QCOMPARE(reply.text, u"Szukam."_s);
        QCOMPARE(reply.toolCalls.size(), 1);
        QCOMPARE(reply.toolCalls[0].id, u"toolu_1"_s);
        QCOMPARE(reply.toolCalls[0].name, u"search_notes"_s);
        QCOMPARE(reply.toolCalls[0].input.value(u"query"_s).toString(), u"żółw"_s);
        QCOMPARE(QJsonDocument::fromJson(reply.raw).array(), QJsonDocument::fromJson(anthropicTurn.toUtf8()).array());
        QVariantMap v = reply.toVariant();
        QCOMPARE(v.value(u"toolCalls"_s).toList().size(), 1);
        QCOMPARE(v.value(u"toolCalls"_s).toList()[0].toMap().value(u"input"_s).toMap().value(u"query"_s).toString(),
                 u"żółw"_s);
        // The raw text goes back into a request unchanged.
        QVERIFY(ai::requestFrom(
                {{u"messages"_s, QVariantList{QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"a"_s}},
                                              QVariantMap{{u"role"_s, u"assistant"_s}, {u"raw"_s, v.value(u"raw"_s)}},
                                              QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"b"_s}}}}},
                &error));
        // An OpenAI-compatible API's tool_calls, their arguments a JSON text.
        reply = ai::readReply(openai,
                              json(200, R"({"model":"qwen3","choices":[{"finish_reason":"tool_calls","message":)"
                                                + openAiTurn.toUtf8() + "}]}"));
        QVERIFY(reply.ok);
        QCOMPARE(reply.stopReason, u"tool_calls"_s);
        QCOMPARE(reply.toolCalls.size(), 1);
        QCOMPARE(reply.toolCalls[0].id, u"call_1"_s);
        QCOMPARE(reply.toolCalls[0].input.value(u"query"_s).toString(), u"żółw"_s);
        QCOMPARE(QJsonDocument::fromJson(reply.raw).object(), QJsonDocument::fromJson(openAiTurn.toUtf8()).object());
        // Arguments that are no JSON object: no input, the call still named.
        reply = ai::readReply(
                openai,
                json(200, R"({"choices":[{"message":{"role":"assistant","content":null,)"
                          R"("tool_calls":[{"id":"c","function":{"name":"today","arguments":"nie json"}}]}}]})"));
        QVERIFY(reply.ok);
        QCOMPARE(reply.toolCalls.size(), 1);
        QVERIFY(reply.toolCalls[0].input.isEmpty());
        // A plain answer has no toolCalls in b.ai's table.
        reply = ai::readReply(claude(),
                              json(200, R"({"stop_reason":"end_turn","content":[{"type":"text","text":"Gad."}]})"));
        QVERIFY(!reply.toVariant().contains(u"toolCalls"_s));
        // (The same blocks; the keys in Qt's order.)
        QCOMPARE(QJsonDocument::fromJson(reply.toVariant().value(u"raw"_s).toString().toUtf8()).array(),
                 QJsonDocument::fromJson(R"([{"type":"text","text":"Gad."}])").array());

        // Refused, each with why.
        const auto refused = [&](const QVariantMap &t, const QString &why) {
            QString e;
            const bool none = !ai::requestFrom(t, &e);
            if (!none || !e.contains(why))
                qWarning("%s", qPrintable(e));
            return none && e.contains(why);
        };
        const QVariantMap user{{u"role"_s, u"user"_s}, {u"content"_s, u"a"_s}};
        const auto withTools = [&](const QVariantList &list) {
            return QVariantMap{{u"prompt"_s, u"a"_s}, {u"tools"_s, list}};
        };
        QVERIFY(refused(withTools({QVariantMap{{u"name"_s, u"szukaj notatek"_s}}}), u"letters, digits"_s));
        QVERIFY(refused(withTools({QVariantMap{{u"name"_s, u"x"_s}}, QVariantMap{{u"name"_s, u"x"_s}}}),
                        u"two tools"_s));
        QVERIFY(refused(
                withTools({QVariantMap{{u"name"_s, u"x"_s}, {u"input"_s, QVariantMap{{u"type"_s, u"string"_s}}}}}),
                u"type \"object\""_s));
        QVERIFY(refused({{u"prompt"_s, u"a"_s}, {u"tools"_s, u"wszystkie"_s}}, u"tools must be a list"_s));
        QVERIFY(refused(
                {{u"messages"_s,
                  QVariantList{user, QVariantMap{{u"role"_s, u"assistant"_s}, {u"raw"_s, u"nie json"_s}}, user}}},
                u"raw"_s));
        QVERIFY(refused(
                {{u"messages"_s, QVariantList{QVariantMap{{u"role"_s, u"user"_s}, {u"results"_s, QVariantList{}}}}}},
                u"results must be"_s));
        QVERIFY(refused({{u"messages"_s, QVariantList{QVariantMap{
                                                 {u"role"_s, u"user"_s},
                                                 {u"results"_s, QVariantList{QVariantMap{{u"content"_s, u"x"_s}}}}}}}},
                        u"call's id"_s));
        QVERIFY(refused({{u"messages"_s,
                          QVariantList{user, QVariantMap{{u"role"_s, u"assistant"_s}, {u"raw"_s, anthropicTurn}}}}},
                        u"last message must be the user's"_s));
        // Results count toward the request's length.
        QVERIFY(refused({{u"messages"_s,
                          QVariantList{QVariantMap{
                                  {u"role"_s, u"user"_s},
                                  {u"results"_s, QVariantList{QVariantMap{
                                                         {u"id"_s, u"t"_s},
                                                         {u"content"_s, QString(ai::MaxRequestChars + 1, u'a')}}}}}}}},
                        u"too long"_s));
    }

    void clientOverATransport()
    {
        FakeTransport transport;
        transport.answer = json(200, R"({"model":"claude-opus-5-5","stop_reason":"end_turn","content":[{"type":"text","text":"OK"}]})");
        ai::Client client(&transport);
        ai::Reply got;
        int calls = 0;
        client.send(claude(), ask(u"Say OK."_s), [&](const ai::Reply &r) {
            got = r;
            ++calls;
        });
        QCOMPARE(calls, 0); // from the event loop
        QTRY_COMPARE(calls, 1);
        QVERIFY(got.ok);
        QCOMPARE(got.text, u"OK"_s);
        QCOMPARE(transport.sent.size(), 1);
        // Settings that cannot send: an error, still from the event loop, nothing sent.
        client.send(ai::Settings(), ask(u"x"_s), [&](const ai::Reply &r) {
            got = r;
            ++calls;
        });
        QCOMPARE(calls, 1);
        QTRY_COMPARE(calls, 2);
        QVERIFY(!got.ok);
        QVERIFY(got.error.contains(u"no API key"_s));
        QCOMPARE(transport.sent.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestAi)
#include "tst_ai.moc"
