// AI for extensions (src/core/ai/ai.h): b.ai's requests, the HTTP requests to Anthropic's
// Messages API and to OpenAI-compatible chat APIs, the answers, refusals and errors, pictures in a
// request, and the client over a fake transport and a fake model on the device.

#include "ai/ai.h"
#include "ai/local.h"

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

/// A provider that streams: its body comes in `pieces` (request.onData, from the event loop), then
/// the whole answer; a request stopped meanwhile (request.abort) ends as a broken connection.
class StreamingTransport : public sync::HttpTransport
{
public:
    QList<sync::HttpRequest> sent;
    QList<QByteArray> pieces;
    int status = 200;
    bool aborted = false;
    void send(const sync::HttpRequest &request, sync::HttpCallback done) override
    {
        sent << request;
        auto broken = std::make_shared<bool>(false);
        if (request.abort)
            request.abort->onAbort([this, broken] {
                aborted = true;
                *broken = true;
            });
        QByteArray body;
        for (const QByteArray &piece : std::as_const(pieces)) {
            body += piece;
            QTimer::singleShot(0, [request, piece, broken] {
                if (!*broken && request.onData)
                    request.onData(piece);
            });
        }
        QTimer::singleShot(0, [done, body, broken, code = status] {
            sync::HttpResponse r;
            if (*broken) {
                r.networkError = u"Operation canceled"_s;
            } else {
                r.status = code;
                r.body = body;
            }
            done(r);
        });
    }
};

/// `text` cut into pieces at the given byte offsets (inside a letter too).
QList<QByteArray> cutAt(const QByteArray &text, const QList<qsizetype> &at)
{
    QList<QByteArray> out;
    qsizetype from = 0;
    for (const qsizetype to : at) {
        out << text.mid(from, to - from);
        from = to;
    }
    out << text.mid(from);
    return out;
}

/// Every way to cut `text` in two, and byte by byte: the reader sees the same events.
QList<QList<QByteArray>> cuttings(const QByteArray &text)
{
    QList<QList<QByteArray>> out;
    for (qsizetype i = 1; i < text.size(); i += 7)
        out << cutAt(text, {i});
    QList<qsizetype> bytes;
    for (qsizetype i = 1; i < text.size(); ++i)
        bytes << i;
    out << cutAt(text, bytes);
    return out;
}

/// A streamed Anthropic answer: thoughts, Polish words, a tool call whose input comes in parts.
const QByteArray kAnthropicStream =
        "event: message_start\r\n"
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\","
        "\"model\":\"claude-opus-5-5\",\"content\":[],\"stop_reason\":null,\"usage\":{\"input_tokens\":120,\"output_tokens\":1}}}\r\n"
        "\r\n"
        ": a comment, and a ping\n"
        "event: ping\ndata: {\"type\": \"ping\"}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"thinking\",\"thinking\":\"\",\"signature\":\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"Szukam zadań\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"signature_delta\",\"signature\":\"c2lnbg==\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"text_delta\",\"text\":\"Zaraz sprawdzę \"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"text_delta\",\"text\":\"żółte karteczki.\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":2,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"search_notes\",\"input\":{}}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":2,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"query\\\": \\\"karte\"}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":2,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"czki\\\"}\"}}\n\n"
        "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":2}\n\n"
        "event: message_delta\ndata: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\",\"stop_sequence\":null},\"usage\":{\"output_tokens\":42}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

/// A streamed OpenAI-compatible answer (llama.cpp's server): thoughts, words, a call in parts.
const QByteArray kOpenAiStream =
        "data: {\"id\":\"c1\",\"object\":\"chat.completion.chunk\",\"model\":\"qwen\",\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"content\":null}}]}\n\n"
        "data: {\"model\":\"qwen\",\"choices\":[{\"index\":0,\"delta\":{\"reasoning_content\":\"Użytkownik pyta\"}}]}\n\n"
        "data: {\"model\":\"qwen\",\"choices\":[{\"index\":0,\"delta\":{\"reasoning_content\":\" o zakupy.\"}}]}\n\n"
        "data: {\"model\":\"qwen\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Sprawdzę \"}}]}\n\n"
        "data: {\"model\":\"qwen\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"listę.\"}}]}\n\n"
        "data: {\"model\":\"qwen\",\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_7\",\"type\":\"function\",\"function\":{\"name\":\"read_page\",\"arguments\":\"\"}}]}}]}\n\n"
        "data: {\"model\":\"qwen\",\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"{\\\"name\\\":\"}}]}}]}\n\n"
        "data: {\"model\":\"qwen\",\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\" \\\"Zakupy\\\"}\"}}]}}]}\n\n"
        "data: {\"model\":\"qwen\",\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: {\"model\":\"qwen\",\"choices\":[],\"usage\":{\"prompt_tokens\":300,\"completion_tokens\":25}}\n\n"
        "data: [DONE]\n\n";

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

/// A picture loader that knows two pictures and how often it was asked.
struct Pictures {
    QStringList asked;
    ai::ImageLoader loader()
    {
        return [this](const QString &name, QString *why) -> std::optional<ai::Image> {
            asked << name;
            if (name == u"paragon.jpg")
                return ai::Image{QByteArray("\xff\xd8 paragon"), u"image/jpeg"_s};
            if (name == u"Zdjęcia/żółw.png")
                return ai::Image{QByteArray("\x89PNG zolw"), u"image/png"_s};
            *why = u"no such picture in the notebook"_s;
            return std::nullopt;
        };
    }
};

/// A model on the device that answers at once.
class FakeLocal : public ai::LocalBackend {
public:
    QList<ai::Request> requests;
    QList<ai::Settings> settings;
    QList<bool> partials; ///< each request: was it watched as it was written
    std::shared_ptr<std::atomic<bool>> stopped;
    void send(const ai::Settings &s, const ai::Request &request, std::function<void(const ai::Reply &)> done,
              ai::PartialCallback partial, std::shared_ptr<std::atomic<bool>> stop) override
    {
        settings << s;
        requests << request;
        partials << bool(partial);
        stopped = stop;
        ai::Reply reply;
        reply.ok = true;
        reply.text = u"Z urządzenia."_s;
        reply.model = s.model;
        QTimer::singleShot(0, [done, reply] { done(reply); });
    }
};

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

    void addressesThatAreNotEncrypted_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<bool>("unencrypted");
        QTest::newRow("the provider's own") << QString() << false;
        QTest::newRow("https") << u"https://api.example.com/v1"_s << false;
        QTest::newRow("this device") << u"http://localhost:11434/v1"_s << false;
        QTest::newRow("this device by number") << u"http://127.0.0.1:11434/v1"_s << false;
        QTest::newRow("this device, IPv6") << u"http://[::1]:11434/v1"_s << false;
        QTest::newRow("a name of this device") << u"http://ollama.localhost/v1"_s << false;
        QTest::newRow("a server at home") << u"http://192.168.1.10:11434/v1"_s << true;
        QTest::newRow("a name at home") << u"http://serwer.lan:11434/v1"_s << true;
        QTest::newRow("the internet") << u"http://api.example.com/v1"_s << true;
        QTest::newRow("in capitals") << u"HTTP://API.EXAMPLE.COM/v1"_s << true;
        QTest::newRow("a host that only starts like this device") << u"http://localhost.example.com/v1"_s << true;
        QTest::newRow("no address at all") << u"zażółć"_s << false;
    }
    void addressesThatAreNotEncrypted()
    {
        QFETCH(QString, address);
        QFETCH(bool, unencrypted);
        for (const QString &provider : {u"anthropic"_s, u"openai"_s}) {
            ai::Settings settings;
            settings.provider = provider;
            settings.baseUrl = address;
            QCOMPARE(settings.unencrypted(), unencrypted);
        }
        // The model on the device sends nothing: an address left from another provider is not used.
        ai::Settings onDevice;
        onDevice.provider = u"local"_s;
        onDevice.baseUrl = address;
        QVERIFY(!onDevice.unencrypted());
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

    void picturesInRequests()
    {
        Pictures pictures;
        QString error;
        // With a prompt.
        std::optional<ai::Request> r =
                ai::requestFrom({{u"prompt"_s, u"Co jest na paragonie?"_s},
                                 {u"images"_s, QVariantList{u"paragon.jpg"_s, u" Zdjęcia/żółw.png "_s}}},
                                &error, pictures.loader());
        QVERIFY2(r, qPrintable(error));
        QCOMPARE(r->messages.size(), 1);
        QCOMPARE(r->messages[0].content, u"Co jest na paragonie?"_s);
        QCOMPARE(r->messages[0].images.size(), 2);
        QCOMPARE(r->messages[0].images[0], (ai::Image{QByteArray("\xff\xd8 paragon"), u"image/jpeg"_s}));
        QCOMPARE(r->messages[0].images[1].mediaType, u"image/png"_s);
        QCOMPARE(pictures.asked, (QStringList{u"paragon.jpg"_s, u"Zdjęcia/żółw.png"_s}));
        // In a user's message of a conversation (its text may be empty).
        r = ai::requestFrom(
                {{u"messages"_s, QVariantList{QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"a"_s}},
                                              QVariantMap{{u"role"_s, u"assistant"_s}, {u"content"_s, u"b"_s}},
                                              QVariantMap{{u"role"_s, u"user"_s},
                                                          {u"content"_s, QString()},
                                                          {u"images"_s, QVariantList{u"paragon.jpg"_s}}}}}},
                &error, pictures.loader());
        QVERIFY2(r, qPrintable(error));
        QVERIFY(r->messages[0].images.isEmpty());
        QCOMPARE(r->messages[2].images.size(), 1);

        const auto refused = [&](const QVariantMap &table, const QString &why, const ai::ImageLoader &loader) {
            QString e;
            const bool none = !ai::requestFrom(table, &e, loader);
            if (!none || !e.contains(why))
                qWarning("got: %s", qPrintable(e));
            return none && e.contains(why);
        };
        const QVariantMap one{{u"prompt"_s, u"?"_s}, {u"images"_s, QVariantList{u"paragon.jpg"_s}}};
        // Nowhere to load pictures from (brulion-cli, a test).
        QVERIFY(refused(one, u"cannot be sent from here"_s, {}));
        // A picture the notebook does not have: the loader's reason, with the name.
        QVERIFY(refused({{u"prompt"_s, u"?"_s}, {u"images"_s, QVariantList{u"brak.jpg"_s}}},
                        u"the picture \"brak.jpg\": no such picture in the notebook"_s, pictures.loader()));
        QVERIFY(refused({{u"prompt"_s, u"?"_s}, {u"images"_s, u"paragon.jpg"_s}}, u"a list of picture names"_s,
                        pictures.loader()));
        QVERIFY(refused({{u"prompt"_s, u"?"_s}, {u"images"_s, QVariantList{7}}}, u"a list of picture names"_s,
                        pictures.loader()));
        QVERIFY(refused({{u"prompt"_s, u"?"_s}, {u"images"_s, QVariantList{u"  "_s}}}, u"a list of picture names"_s,
                        pictures.loader()));
        QVariantList many;
        for (int i = 0; i <= ai::MaxRequestImages; ++i)
            many << u"paragon.jpg"_s;
        QVERIFY(refused({{u"prompt"_s, u"?"_s}, {u"images"_s, many}}, u"at most 8 pictures"_s, pictures.loader()));
        // Only the user shows pictures.
        QVERIFY(refused({{u"messages"_s, QVariantList{QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"a"_s}},
                                                      QVariantMap{{u"role"_s, u"assistant"_s},
                                                                  {u"content"_s, u"b"_s},
                                                                  {u"images"_s, QVariantList{u"paragon.jpg"_s}}},
                                                      QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"c"_s}}}}},
                        u"only a user's message has pictures"_s, pictures.loader()));
        // No pictures asked for: no loader needed.
        QVERIFY(ai::requestFrom({{u"prompt"_s, u"?"_s}}, &error));
    }

    void picturesInHttpRequests()
    {
        ai::Request request = ask(u"Co to?"_s);
        request.messages[0].images << ai::Image{QByteArray("JPEG bytes"), u"image/jpeg"_s}
                                   << ai::Image{QByteArray("PNG bytes"), u"image/png"_s};
        QString error;
        // Claude: image blocks (base64), then the text.
        std::optional<sync::HttpRequest> http = ai::httpRequest(claude(), request, &error);
        QVERIFY2(http, qPrintable(error));
        QJsonArray content =
                bodyOf(*http).value(u"messages"_s).toArray().at(0).toObject().value(u"content"_s).toArray();
        QCOMPARE(content.size(), 3);
        QCOMPARE(content[0].toObject().value(u"type"_s).toString(), u"image"_s);
        const QJsonObject source = content[0].toObject().value(u"source"_s).toObject();
        QCOMPARE(source.value(u"type"_s).toString(), u"base64"_s);
        QCOMPARE(source.value(u"media_type"_s).toString(), u"image/jpeg"_s);
        QCOMPARE(QByteArray::fromBase64(source.value(u"data"_s).toString().toLatin1()), QByteArray("JPEG bytes"));
        QCOMPARE(content[1].toObject().value(u"source"_s).toObject().value(u"media_type"_s).toString(), u"image/png"_s);
        QCOMPARE(content[2].toObject().value(u"type"_s).toString(), u"text"_s);
        QCOMPARE(content[2].toObject().value(u"text"_s).toString(), u"Co to?"_s);

        // An OpenAI-compatible API: image_url parts with data: addresses, then the text.
        ai::Settings openai;
        openai.provider = u"openai"_s;
        openai.model = u"llava"_s;
        openai.baseUrl = u"http://192.168.1.10:11434/v1"_s;
        http = ai::httpRequest(openai, request, &error);
        QVERIFY2(http, qPrintable(error));
        content = bodyOf(*http).value(u"messages"_s).toArray().at(0).toObject().value(u"content"_s).toArray();
        QCOMPARE(content.size(), 3);
        QCOMPARE(content[0].toObject().value(u"type"_s).toString(), u"image_url"_s);
        QCOMPARE(content[0].toObject().value(u"image_url"_s).toObject().value(u"url"_s).toString(),
                 u"data:image/jpeg;base64,"_s + QString::fromLatin1(QByteArray("JPEG bytes").toBase64()));
        QCOMPARE(content[1].toObject().value(u"image_url"_s).toObject().value(u"url"_s).toString(),
                 u"data:image/png;base64,"_s + QString::fromLatin1(QByteArray("PNG bytes").toBase64()));
        QCOMPARE(content[2].toObject().value(u"text"_s).toString(), u"Co to?"_s);

        // A picture alone: no empty text part.
        request.messages[0].content.clear();
        http = ai::httpRequest(claude(), request, &error);
        QCOMPARE(bodyOf(*http).value(u"messages"_s).toArray().at(0).toObject().value(u"content"_s).toArray().size(), 2);
        // A message without pictures is a plain text, as before.
        http = ai::httpRequest(claude(), ask(u"x"_s), &error);
        QVERIFY(bodyOf(*http).value(u"messages"_s).toArray().at(0).toObject().value(u"content"_s).isString());
    }

    void theModelOnTheDevice()
    {
        // The provider "local": a model the app knows, no key, no address.
        ai::Settings local;
        local.provider = u"local"_s;
        QString why;
        QVERIFY2(local.usable(&why), qPrintable(why));
        local.model = u"lfm3-1.7b"_s;
        QVERIFY(local.usable());
        local.model = u"claude-opus-5-5"_s; // left from another provider
        QVERIFY(!local.usable(&why));
        QVERIFY2(why.contains(u"no such model on this device") && why.contains(u"claude-opus-5-5"), qPrintable(why));
        QVERIFY(!ai::defaultModel(u"local"_s).isEmpty());
        QVERIFY(ai::localModel(ai::defaultModel(u"local"_s)));
        // Nothing goes to the network for it.
        local.model.clear();
        QString error;
        QVERIFY(!ai::httpRequest(local, ask(u"x"_s), &error));
        QVERIFY2(error.contains(u"no network request"), qPrintable(error));

        // The client hands the request to the model on the device …
        FakeTransport transport;
        FakeLocal device;
        ai::Client client(&transport);
        client.setLocal(&device);
        ai::Reply got;
        int calls = 0;
        const auto done = [&](const ai::Reply &r) {
            got = r;
            ++calls;
        };
        ai::Request request = ask(u"Co słychać?"_s);
        request.messages[0].images << ai::Image{"JPEG", u"image/jpeg"_s};
        client.send(local, request, done);
        QCOMPARE(calls, 0);
        QTRY_COMPARE(calls, 1);
        QVERIFY(got.ok);
        QCOMPARE(got.text, u"Z urządzenia."_s);
        QCOMPARE(device.requests.size(), 1);
        QCOMPARE(device.requests[0].messages[0].content, u"Co słychać?"_s);
        QCOMPARE(device.requests[0].messages[0].images.size(), 1);
        QVERIFY(transport.sent.isEmpty());
        // … other providers still go to the network …
        transport.answer = json(
                200, R"({"model":"claude-opus-5-5","stop_reason":"end_turn","content":[{"type":"text","text":"OK"}]})");
        client.send(claude(), ask(u"x"_s), done);
        QTRY_COMPARE(calls, 2);
        QCOMPARE(got.text, u"OK"_s);
        QCOMPARE(transport.sent.size(), 1);
        QCOMPARE(device.requests.size(), 1);
        // … a model the app does not know is an error, as is a build without a model runner.
        local.model = u"gpt"_s;
        client.send(local, ask(u"x"_s), done);
        QTRY_COMPARE(calls, 3);
        QVERIFY(!got.ok);
        QVERIFY2(got.error.contains(u"no such model on this device"), qPrintable(got.error));
        local.model.clear();
        ai::Client bare(&transport);
        bare.send(local, ask(u"x"_s), done);
        QCOMPARE(calls, 3);
        QTRY_COMPARE(calls, 4);
        QVERIFY(!got.ok);
        QVERIFY2(got.error.contains(u"runs no model on the device"), qPrintable(got.error));
        QCOMPARE(transport.sent.size(), 1);
    }

    void streamedRequests()
    {
        // Asked to stream: Anthropic's "stream", an OpenAI-compatible API's too, with the counts.
        QString error;
        const auto anthropic = ai::httpRequest(claude(), ask(u"Hej"_s), &error, true);
        QVERIFY(anthropic);
        QCOMPARE(bodyOf(*anthropic).value(u"stream"_s).toBool(), true);
        ai::Settings openai;
        openai.provider = u"openai"_s;
        openai.model = u"qwen"_s;
        openai.baseUrl = u"http://localhost:8080/v1"_s;
        const auto compatible = ai::httpRequest(openai, ask(u"Hej"_s), &error, true);
        QVERIFY(compatible);
        QCOMPARE(bodyOf(*compatible).value(u"stream"_s).toBool(), true);
        QCOMPARE(bodyOf(*compatible).value(u"stream_options"_s).toObject().value(u"include_usage"_s).toBool(), true);
        // Not asked: neither (b.ai's requests stay as they were).
        const auto plain = ai::httpRequest(openai, ask(u"Hej"_s), &error);
        QVERIFY(!bodyOf(*plain).contains(u"stream"_s));
        QVERIFY(!bodyOf(*plain).contains(u"stream_options"_s));
    }

    void anthropicStreams()
    {
        // However the body is cut — inside a letter, between \r and \n —, the reader sees the
        // same: the thoughts and the words as they come, then the answer readReply would read.
        for (const QList<QByteArray> &pieces : cuttings(kAnthropicStream)) {
            ai::StreamReader reader(u"anthropic"_s);
            QList<ai::Partial> seen;
            for (const QByteArray &piece : pieces) {
                if (reader.feed(piece))
                    seen << reader.partial();
            }
            QCOMPARE(reader.partial().thoughts, u"Szukam zadań"_s);
            QCOMPARE(reader.partial().text, u"Zaraz sprawdzę żółte karteczki."_s);
            QVERIFY(!seen.isEmpty() && seen.size() <= 3);
            QVERIFY(reader.error().isEmpty());
            if (pieces.size() > 2) { // byte by byte: every change on its own
                QCOMPARE(seen.size(), 3); // the thoughts, then the words in two steps
                QCOMPARE(seen.first(), (ai::Partial{QString(), u"Szukam zadań"_s}));
                QCOMPARE(seen.at(1).text, u"Zaraz sprawdzę "_s);
            }
        }
        sync::HttpResponse response = json(200, kAnthropicStream);
        const ai::Reply reply = ai::readStream(claude(), response);
        QVERIFY2(reply.ok, qPrintable(reply.error));
        QCOMPARE(reply.text, u"Zaraz sprawdzę żółte karteczki."_s);
        QCOMPARE(reply.model, u"claude-opus-5-5"_s);
        QCOMPARE(reply.stopReason, u"tool_use"_s);
        QCOMPARE(reply.inputTokens, 120);
        QCOMPARE(reply.outputTokens, 42);
        QCOMPARE(reply.toolCalls.size(), 1);
        QCOMPARE(reply.toolCalls.first().id, u"toolu_1"_s);
        QCOMPARE(reply.toolCalls.first().name, u"search_notes"_s);
        QCOMPARE(reply.toolCalls.first().input, (QVariantMap{{u"query"_s, u"karteczki"_s}}));
        // The turn as the provider gives it at once: the thinking block with its signature.
        const QJsonArray raw = QJsonDocument::fromJson(reply.raw).array();
        QCOMPARE(raw.size(), 3);
        QCOMPARE(raw.at(0).toObject(), (QJsonObject{{u"type"_s, u"thinking"_s},
                                                    {u"thinking"_s, u"Szukam zadań"_s},
                                                    {u"signature"_s, u"c2lnbg=="_s}}));
        QCOMPARE(raw.at(2).toObject().value(u"input"_s).toObject(),
                 (QJsonObject{{u"query"_s, u"karteczki"_s}}));
    }

    void anthropicStreamsEdgeCases()
    {
        // A call without arguments: its input is {}.
        const QByteArray noInput =
                "data: {\"type\":\"message_start\",\"message\":{\"model\":\"claude-opus-5-5\",\"usage\":{\"input_tokens\":5}}}\n\n"
                "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"tool_use\",\"id\":\"t\",\"name\":\"list_pages\",\"input\":{}}}\n\n"
                "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
                "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"},\"usage\":{\"output_tokens\":9}}\n\n";
        ai::Reply reply = ai::readStream(claude(), json(200, noInput));
        QVERIFY(reply.ok);
        QCOMPARE(reply.toolCalls.size(), 1);
        QCOMPARE(reply.toolCalls.first().input, QVariantMap());

        // A fallback in the middle (the server's: another model goes on on the same stream): the
        // declined part's thinking and tool call are not sent back, its words stay the start.
        const QByteArray fallback =
                "data: {\"type\":\"message_start\",\"message\":{\"model\":\"claude-opus-4-8\",\"usage\":{\"input_tokens\":5}}}\n\n"
                "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"thinking\",\"thinking\":\"\",\"signature\":\"\"}}\n\n"
                "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"hm\"}}\n\n"
                "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"text\",\"text\":\"Zacz\"}}\n\n"
                "data: {\"type\":\"content_block_start\",\"index\":2,\"content_block\":{\"type\":\"tool_use\",\"id\":\"t\",\"name\":\"read_page\",\"input\":{}}}\n\n"
                "data: {\"type\":\"content_block_start\",\"index\":3,\"content_block\":{\"type\":\"fallback\",\"model\":\"claude-opus-4-8\"}}\n\n"
                "data: {\"type\":\"content_block_start\",\"index\":4,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
                "data: {\"type\":\"content_block_delta\",\"index\":4,\"delta\":{\"type\":\"text_delta\",\"text\":\"ynam.\"}}\n\n"
                "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},\"usage\":{\"output_tokens\":9}}\n\n";
        reply = ai::readStream(claude(), json(200, fallback));
        QVERIFY(reply.ok);
        QCOMPARE(reply.text, u"Zaczynam."_s);
        QVERIFY(reply.toolCalls.isEmpty());
        QStringList types;
        for (const QJsonValue block : QJsonDocument::fromJson(reply.raw).array())
            types << block.toObject().value(u"type"_s).toString();
        QCOMPARE(types, (QStringList{u"text"_s, u"fallback"_s, u"text"_s}));

        // An error in the stream: an error, not a cut answer.
        const QByteArray overloaded =
                "data: {\"type\":\"message_start\",\"message\":{\"model\":\"claude-opus-5-5\"}}\n\n"
                "event: error\ndata: {\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\",\"message\":\"Overloaded\"}}\n\n";
        reply = ai::readStream(claude(), json(200, overloaded));
        QVERIFY(!reply.ok);
        QCOMPARE(reply.error, u"the provider stopped the answer: Overloaded"_s);
        // A refusal: as one that came at once.
        reply = ai::readStream(claude(), json(200, "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"refusal\"}}\n\n"));
        QVERIFY(!reply.ok);
        QVERIFY(reply.error.contains(u"declined"_s));
        // An HTTP error's JSON, and a server that answered at once although asked to stream.
        reply = ai::readStream(claude(), json(401, R"({"type":"error","error":{"type":"authentication_error","message":"bad key"}})"));
        QVERIFY(!reply.ok);
        QVERIFY(reply.error.contains(u"API key"_s));
        reply = ai::readStream(claude(), json(200, R"({"model":"m","stop_reason":"end_turn","content":[{"type":"text","text":"Od razu."}]})"));
        QVERIFY(reply.ok);
        QCOMPARE(reply.text, u"Od razu."_s);
        // No connection.
        reply = ai::readStream(claude(), sync::HttpResponse());
        QVERIFY(!reply.ok);
        QVERIFY(reply.error.startsWith(u"no connection"_s));
    }

    void openAiCompatibleStreams()
    {
        ai::Settings openai;
        openai.provider = u"openai"_s;
        for (const QList<QByteArray> &pieces : cuttings(kOpenAiStream)) {
            ai::StreamReader reader(openai.provider);
            for (const QByteArray &piece : pieces)
                reader.feed(piece);
            QCOMPARE(reader.partial(), (ai::Partial{u"Sprawdzę listę."_s, u"Użytkownik pyta o zakupy."_s}));
        }
        const ai::Reply reply = ai::readStream(openai, json(200, kOpenAiStream));
        QVERIFY2(reply.ok, qPrintable(reply.error));
        QCOMPARE(reply.text, u"Sprawdzę listę."_s);
        QCOMPARE(reply.model, u"qwen"_s);
        QCOMPARE(reply.stopReason, u"tool_calls"_s);
        QCOMPARE(reply.inputTokens, 300);
        QCOMPARE(reply.outputTokens, 25);
        QCOMPARE(reply.toolCalls.size(), 1);
        QCOMPARE(reply.toolCalls.first().id, u"call_7"_s);
        QCOMPARE(reply.toolCalls.first().name, u"read_page"_s);
        QCOMPARE(reply.toolCalls.first().input, (QVariantMap{{u"name"_s, u"Zakupy"_s}}));
        // The message sent back as the server wrote it: its thoughts under the server's name.
        const QJsonObject raw = QJsonDocument::fromJson(reply.raw).object();
        QCOMPARE(raw.value(u"reasoning_content"_s).toString(), u"Użytkownik pyta o zakupy."_s);
        QCOMPARE(raw.value(u"tool_calls"_s).toArray().first().toObject().value(u"function"_s).toObject()
                         .value(u"arguments"_s).toString(),
                 u"{\"name\": \"Zakupy\"}"_s);

        // Only tool calls: content null, as at once. OpenRouter's "reasoning"; an error event.
        const QByteArray calls =
                "data: {\"model\":\"m\",\"choices\":[{\"index\":0,\"delta\":{\"reasoning\":\"hmm\",\"tool_calls\":[{\"id\":\"a\",\"function\":{\"name\":\"list_pages\",\"arguments\":\"{}\"}}]}}]}\n\n"
                "data: {\"model\":\"m\",\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n";
        const QJsonObject onlyCalls = QJsonDocument::fromJson(ai::readStream(openai, json(200, calls)).raw).object();
        QVERIFY(onlyCalls.value(u"content"_s).isNull());
        QCOMPARE(onlyCalls.value(u"reasoning"_s).toString(), u"hmm"_s);
        const ai::Reply failed =
                ai::readStream(openai, json(200, "data: {\"error\":{\"message\":\"model unloaded\"}}\n\n"));
        QCOMPARE(failed.error, u"the provider stopped the answer: model unloaded"_s);
    }

    void clientStreamsAndStops()
    {
        // With a `partial`, the request streams: the words as they come, then the reply.
        StreamingTransport transport;
        transport.pieces = cutAt(kAnthropicStream, {300, 900, 1400});
        ai::Client client(&transport);
        QList<ai::Partial> partials;
        ai::Reply got;
        int calls = 0;
        client.send(
                claude(), ask(u"Co mam?"_s),
                [&](const ai::Reply &r) {
                    got = r;
                    ++calls;
                },
                [&](const ai::Partial &p) { partials << p; });
        QTRY_COMPARE(calls, 1);
        QVERIFY(got.ok);
        QCOMPARE(got.toolCalls.size(), 1);
        QVERIFY(!partials.isEmpty());
        QCOMPARE(partials.last(), (ai::Partial{u"Zaraz sprawdzę żółte karteczki."_s, u"Szukam zadań"_s}));
        QCOMPARE(bodyOf(transport.sent.first()).value(u"stream"_s).toBool(), true);

        // Stopped on its way: "stopped" at once (from the event loop), the connection closed,
        // nothing more of it — no partial, no second `done`.
        partials.clear();
        calls = 0;
        ai::Pending pending = client.send(
                claude(), ask(u"Co mam?"_s),
                [&](const ai::Reply &r) {
                    got = r;
                    ++calls;
                },
                [&](const ai::Partial &p) { partials << p; });
        pending.stop();
        QVERIFY(pending.stopped());
        QVERIFY(transport.aborted);
        QCOMPARE(calls, 0);
        QTRY_COMPARE(calls, 1);
        QCOMPARE(got.error, u"stopped"_s);
        QTest::qWait(20);
        QCOMPARE(calls, 1);
        QVERIFY(partials.isEmpty());
        pending.stop(); // (again: nothing)
        QCOMPARE(calls, 1);

        // Without a partial nothing streams (b.ai's requests).
        client.send(claude(), ask(u"x"_s), [&](const ai::Reply &) { ++calls; });
        QVERIFY(!bodyOf(transport.sent.last()).contains(u"stream"_s));
        QTRY_COMPARE(calls, 2);
    }

    void clientHandsTheModelOnTheDeviceItsWatchers()
    {
        FakeLocal local;
        FakeTransport transport;
        ai::Client client(&transport);
        client.setLocal(&local);
        ai::Settings device;
        device.provider = u"local"_s;
        int calls = 0;
        ai::Pending pending = client.send(device, ask(u"Hej"_s), [&](const ai::Reply &) { ++calls; },
                                          [](const ai::Partial &) {});
        QCOMPARE(local.partials, QList<bool>{true});
        QVERIFY(local.stopped && !local.stopped->load());
        pending.stop(); // the model on the device sees it
        QVERIFY(local.stopped->load());
        QTRY_COMPARE(calls, 1);
        QTest::qWait(20);
        QCOMPARE(calls, 1); // (its own answer, late, is dropped)
        client.send(device, ask(u"Hej"_s), [&](const ai::Reply &) { ++calls; });
        QCOMPARE(local.partials, (QList<bool>{true, false}));
        QTRY_COMPARE(calls, 2);
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
