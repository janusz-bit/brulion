// `brulion-cli acp agent` (acpagent.h): the assistant of the app as an ACP agent for editors.
#include "acpagent.h"

#include "cli_p.h"
#include "commands_p.h"
#include "extensions/builtin.h"
#include "extensions/notebookhost.h"
#include "notebook/session.h"
#include "sync/http.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <cstdio>
#include <string>
#include <thread>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace ext = brulion::extensions;

namespace {

/// The assistant and its command for a turn of a conversation.
const QString kAssistant = u"asystent"_s;
const QString kChat = u"chat"_s;
/// The JSON-RPC error of a turn that failed (the message says why).
constexpr int kTurnFailed = -32603;
/// The most of one embedded file put into a prompt, in characters.
constexpr int kMaxEmbedded = 30000;

QJsonObject textContent(const QString &text)
{
    return {{u"type"_s, u"text"_s}, {u"text"_s, text}};
}

void logLine(const QString &line)
{
    const QByteArray bytes =
            (QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss"_s) + u' ' + line + u'\n').toUtf8();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
    std::fflush(stderr);
}

} // namespace

// ---- the model of the app ------------------------------------------------------------------------

ai::Settings aiSettingsOfApp(const Context &context)
{
    ai::Settings s;
    // As AppSettings reads them (src/app/shell/appsettings.cpp): an unknown provider is Claude.
    const QString provider = context.settingsValue(u"ai/provider"_s);
    s.provider = provider == u"openai" || provider == u"local" ? provider : u"anthropic"_s;
    if (s.provider == u"local") {
        s.model = context.settingsValue(u"ai/localModel"_s);
        return s;
    }
    s.model = context.settingsValue(u"ai/model"_s).trimmed();
    s.baseUrl = context.settingsValue(u"ai/baseUrl"_s).trimmed();
    QFile key(context.appDataDir() + u"/secrets/ai-key"_s);
    if (key.open(QIODevice::ReadOnly))
        s.apiKey = QString::fromUtf8(key.read(64 * 1024)).trimmed();
    return s;
}

QString aiProblemForCli(const ai::Settings &settings)
{
    if (settings.provider == u"local")
        return tr("The model on this device runs only in the Brulion app: for brulion-cli choose Claude or an "
                  "OpenAI-compatible API in Brulion → Settings → Extensions → AI");
    QString why;
    if (settings.usable(&why))
        return {};
    if (settings.provider == u"openai")
        return tr("Brulion's AI names no model: set it in Brulion → Settings → Extensions → AI");
    return tr("Brulion's AI has no API key: set it in Brulion → Settings → Extensions → AI");
}

// ---- AcpAgent ---------------------------------------------------------------------------------------

AcpAgent::AcpAgent(notebook::Notebook *notebook, const QString &notebookProblem, AcpAgentOptions options,
                   sync::HttpTransport *transport, acp::Connection::Send send, QObject *parent)
    : QObject(parent),
      m_notebook(notebook),
      m_notebookProblem(notebookProblem),
      m_options(std::move(options)),
      m_connection(std::move(send)),
      m_ai(transport)
{
    m_connection.onRequest = [this](const QJsonValue &id, const QString &method, const QJsonObject &params) {
        onRequest(id, method, params);
    };
    m_connection.onNotification = [this](const QString &method, const QJsonObject &params) {
        onNotification(method, params);
    };
    m_connection.onMalformed = [this](const QString &why) { say(u"a line from the editor that is no message: "_s + why); };
}

AcpAgent::~AcpAgent()
{
    closed();
    m_engine.reset(); // (before its host)
}

bool AcpAgent::running(const QString &session) const
{
    const auto it = m_sessions.constFind(session);
    return it != m_sessions.constEnd() && it->turn.has_value();
}

void AcpAgent::received(const QByteArray &bytes)
{
    m_connection.receive(bytes);
}

void AcpAgent::closed()
{
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (!it->turn)
            continue;
        Turn t = *std::exchange(it->turn, std::nullopt);
        if (t.ai)
            t.ai->stop();
        if (t.call && m_engine && m_engine->waitingCalls().contains(t.call))
            m_engine->cancel(t.call);
    }
    m_connection.close(u"the editor is gone"_s);
}

void AcpAgent::say(const QString &line) const
{
    if (log)
        log(line);
}

void AcpAgent::onRequest(const QJsonValue &id, const QString &method, const QJsonObject &params)
{
    if (method == u"initialize")
        initialize(id, params);
    else if (method == u"session/new")
        newSession(id, params);
    else if (method == u"session/prompt")
        prompt(id, params);
    else if (method == u"authenticate")
        m_connection.respondError(id, acp::InvalidParams, u"Brulion's assistant needs no sign-in"_s);
    else
        m_connection.respondError(id, acp::MethodNotFound, u"Method not found: %1"_s.arg(method));
}

void AcpAgent::onNotification(const QString &method, const QJsonObject &params)
{
    if (method == u"session/cancel")
        cancel(params.value(u"sessionId"_s).toString());
    // (Nothing else is said to an agent.)
}

void AcpAgent::initialize(const QJsonValue &id, const QJsonObject &params)
{
    Q_UNUSED(params); // (any version asked: the agent answers with its own, as the protocol says)
    m_initialized = true;
    const QJsonObject capabilities{
            {u"loadSession"_s, false},
            {u"promptCapabilities"_s,
             QJsonObject{{u"image"_s, false}, {u"audio"_s, false}, {u"embeddedContext"_s, true}}},
            {u"mcpCapabilities"_s, QJsonObject{{u"http"_s, false}, {u"sse"_s, false}}}};
    m_connection.respond(id, QJsonObject{{u"protocolVersion"_s, acp::ProtocolVersion},
                                         {u"agentCapabilities"_s, capabilities},
                                         {u"authMethods"_s, QJsonArray()},
                                         {u"agentInfo"_s, QJsonObject{{u"name"_s, u"brulion"_s},
                                                                      {u"title"_s, u"Brulion"_s},
                                                                      {u"version"_s, m_options.version}}}});
}

bool AcpAgent::ensureEngine(QString *error)
{
    if (m_engine)
        return true;
    if (!m_notebook) {
        *error = m_notebookProblem;
        return false;
    }
    ext::NotebookHost::Options options;
    options.now = m_options.now;
    options.language = m_options.language;
    options.uiLanguage = m_options.language;
    options.updateIndex = true; // (the assistant may read what it just wrote)
    m_host = std::make_unique<ext::NotebookHost>(m_notebook->vault(), m_notebook->index(), options);
    m_host->onStatus = [this](const QString &, const QString &text) {
        if (!m_running.isEmpty())
            showStatus(m_running, text);
    };
    m_host->onLog = [this](const QString &extension, const QString &text) { say(extension + u": "_s + text); };
    auto engine = std::make_unique<ext::Engine>();
    QString startError;
    if (!engine->start(m_host.get(), m_options.limits, &startError)) {
        *error = tr("The Luau engine did not start: %1").arg(startError);
        m_host.reset();
        return false;
    }
    const ext::Result loaded = engine->load(ext::builtinExtension(kAssistant));
    if (!loaded.ok()) {
        *error = tr("The AI assistant does not load: %1").arg(loaded.error);
        engine.reset();
        m_host.reset();
        return false;
    }
    m_engine = std::move(engine);
    return true;
}

void AcpAgent::newSession(const QJsonValue &id, const QJsonObject &params)
{
    Q_UNUSED(params); // (the notebook is the program's, whatever folder the editor has open)
    QString error;
    if (!ensureEngine(&error)) {
        m_connection.respondError(id, kTurnFailed, error);
        return;
    }
    Session s;
    s.id = u"brulion-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sessions.insert(s.id, s);
    say(u"session %1"_s.arg(s.id));
    m_connection.respond(id, QJsonObject{{u"sessionId"_s, s.id}});
}

QString AcpAgent::promptText(const QJsonArray &blocks) const
{
    const QString root = m_notebook ? QDir::cleanPath(m_notebook->root()) : QString();
    // A file of the notebook as a link to its page, anything else as a Markdown link.
    const auto linkTo = [&root](const QString &uri, const QString &name) -> QString {
        const QUrl url(uri);
        if (url.isLocalFile() && !root.isEmpty()) {
            const QString path = QDir::cleanPath(url.toLocalFile());
            if (path.startsWith(root + u'/') && path.endsWith(u".md"_s, Qt::CaseInsensitive))
                return u"[["_s + path.mid(root.size() + 1).chopped(3) + u"]]"_s;
        }
        return name.isEmpty() ? uri : u"[%1](%2)"_s.arg(name, uri);
    };
    QString text;
    QStringList embedded;
    for (const QJsonValue &v : blocks) {
        const QJsonObject b = v.toObject();
        const QString type = b.value(u"type"_s).toString();
        if (type == u"text") {
            text += b.value(u"text"_s).toString();
        } else if (type == u"resource_link") {
            text += linkTo(b.value(u"uri"_s).toString(), b.value(u"name"_s).toString());
        } else if (type == u"resource") {
            const QJsonObject r = b.value(u"resource"_s).toObject();
            QString content = r.value(u"text"_s).toString();
            if (content.isEmpty())
                continue; // (a binary file's blob: no picture leaves unconverted)
            if (content.size() > kMaxEmbedded)
                content = content.left(kMaxEmbedded) + u"\n…"_s;
            const QString uri = r.value(u"uri"_s).toString();
            embedded << linkTo(uri, QFileInfo(QUrl(uri).path()).fileName()) + u":\n\n```\n"_s + content + u"\n```"_s;
        }
        // (Pictures and sound are not offered: promptCapabilities.)
    }
    text = text.trimmed();
    if (!embedded.isEmpty())
        text += (text.isEmpty() ? QString() : u"\n\n"_s) + embedded.join(u"\n\n"_s);
    return text;
}

void AcpAgent::prompt(const QJsonValue &id, const QJsonObject &params)
{
    const QString sid = params.value(u"sessionId"_s).toString();
    const auto it = m_sessions.find(sid);
    if (it == m_sessions.end()) {
        m_connection.respondError(id, acp::InvalidParams, u"Unknown session: %1"_s.arg(sid));
        return;
    }
    if (it->turn) {
        m_connection.respondError(id, acp::InvalidParams, u"A turn runs in this session already"_s);
        return;
    }
    if (!m_options.aiProblem.isEmpty()) {
        say(u"a prompt without a model: "_s + m_options.aiProblem);
        m_connection.respondError(id, kTurnFailed, m_options.aiProblem);
        return;
    }
    const QString text = promptText(params.value(u"prompt"_s).toArray());
    if (text.isEmpty()) {
        m_connection.respondError(id, acp::InvalidParams, tr("The message has no text"));
        return;
    }
    chat::Message message;
    message.role = chat::Role::User;
    message.text = text;
    it->conversation.messages.append(message);
    const int from = chat::firstSent(it->conversation, m_options.historyBudget);
    const QVariantMap ctx{{u"page"_s, QString()},
                          {u"selection"_s, QString()},
                          {u"messages"_s, chat::history(it->conversation, from, false)},
                          {u"conversation"_s, QString()},
                          {u"omitted"_s, from}};
    Turn turn;
    turn.promptId = id;
    turn.serial = ++m_serial;
    it->turn = turn;
    run(sid, [this, &ctx] { return m_engine->runCommand(kAssistant, kChat, ctx); });
}

void AcpAgent::cancel(const QString &session)
{
    const auto it = m_sessions.find(session);
    if (it == m_sessions.end() || !it->turn)
        return;
    closeStatus(session, *it->turn, u"failed"_s);
    const Turn t = *std::exchange(it->turn, std::nullopt);
    if (t.ai) {
        ai::Pending request = *t.ai;
        request.stop(); // (its answer finds no turn)
    }
    if (t.call && m_engine && m_engine->waitingCalls().contains(t.call))
        m_engine->cancel(t.call); // (a question on its way: its answer finds no turn either)
    say(u"session %1: the turn is cancelled"_s.arg(session));
    m_connection.respond(t.promptId, QJsonObject{{u"stopReason"_s, u"cancelled"_s}});
}

AcpAgent::Turn *AcpAgent::turnOf(const QString &session, quint64 serial)
{
    const auto it = m_sessions.find(session);
    if (it == m_sessions.end() || !it->turn || it->turn->serial != serial)
        return nullptr;
    return &*it->turn;
}

void AcpAgent::run(const QString &session, const std::function<ext::Result()> &step)
{
    const QString before = std::exchange(m_running, session);
    const ext::Result r = step();
    m_running = before;
    handle(session, r);
}

void AcpAgent::handle(const QString &session, const ext::Result &r)
{
    const auto it = m_sessions.find(session);
    if (it == m_sessions.end() || !it->turn) {
        if (r.status == ext::Status::Suspended && m_engine)
            m_engine->cancel(r.call);
        return;
    }
    if (r.status != ext::Status::Suspended) {
        finish(session, r);
        return;
    }
    it->turn->call = r.call;
    const QVariantMap question = r.value.toMap();
    const QString kind = question.value(u"kind"_s).toString();
    if (kind == u"ai") {
        askAi(session, question.value(u"request"_s).toMap());
    } else if (kind == u"choose") {
        askPermission(session, question);
    } else {
        // b.ask, b.form: an editor shows no such question — answered with nothing.
        const int call = r.call;
        run(session, [this, call] { return m_engine->resume(call, QVariant()); });
    }
}

void AcpAgent::askAi(const QString &session, const QVariantMap &request)
{
    Turn &t = *m_sessions[session].turn;
    const quint64 serial = t.serial;
    QString error;
    const std::optional<ai::Request> r = ai::requestFrom(request, &error); // (no picture loader)
    if (!r) {
        QTimer::singleShot(0, this, [this, session, serial, error] {
            aiAnswered(session, serial, {{u"error"_s, error}}, QString());
        });
        return;
    }
    t.newRequest = true;
    t.requestThoughts.clear();
    const QPointer<AcpAgent> self(this);
    t.ai = m_ai.send(
            m_options.ai, *r,
            [self, session, serial](const ai::Reply &reply) {
                if (self)
                    self->aiAnswered(session, serial, reply.toVariant(), reply.thoughts);
            },
            [self, session, serial](const ai::Partial &partial) {
                if (self)
                    self->aiPartial(session, serial, partial);
            });
}

void AcpAgent::aiPartial(const QString &session, quint64 serial, const ai::Partial &partial)
{
    if (Turn *t = turnOf(session, serial))
        thoughts(session, *t, partial.thoughts);
}

void AcpAgent::aiAnswered(const QString &session, quint64 serial, const QVariantMap &reply, const QString &thought)
{
    Turn *t = turnOf(session, serial);
    if (!t)
        return; // (cancelled meanwhile)
    t->ai.reset();
    thoughts(session, *t, thought); // (what was not written while it came: a server that answers at once)
    if (reply.contains(u"error"_s))
        say(u"session %1: the model answered with an error"_s.arg(session));
    const int call = t->call;
    run(session, [this, call, &reply] { return m_engine->resume(call, reply); });
}

void AcpAgent::thoughts(const QString &session, Turn &turn, const QString &text)
{
    // What the model's request thought so far: the part not sent yet. (A new request's thoughts
    // start a new paragraph after the earlier ones.)
    if (text.isEmpty() || !text.startsWith(turn.requestThoughts))
        return;
    QString delta = text.mid(turn.requestThoughts.size());
    if (delta.isEmpty())
        return;
    if (turn.newRequest && turn.anyThoughts)
        delta.prepend(u"\n\n"_s);
    turn.newRequest = false;
    turn.anyThoughts = true;
    turn.requestThoughts = text;
    update(session, {{u"sessionUpdate"_s, u"agent_thought_chunk"_s}, {u"content"_s, textContent(delta)}});
}

void AcpAgent::askPermission(const QString &session, const QVariantMap &question)
{
    Turn &t = *m_sessions[session].turn;
    closeStatus(session, t, u"completed"_s);
    const QString prompt = question.value(u"prompt"_s).toString();
    const QStringList options = question.value(u"options"_s).toStringList();
    if (options.isEmpty()) {
        const int call = t.call;
        run(session, [this, call] { return m_engine->resume(call, QVariant()); });
        return;
    }
    // The question's first line is its title ("The AI wants to add to the page "Zakupy":"), the
    // rest what it would write.
    const qsizetype lineEnd = prompt.indexOf(u'\n');
    const QString title = (lineEnd < 0 ? prompt : prompt.left(lineEnd)).trimmed().left(ext::QuestionLimits::title);
    const QString details = lineEnd < 0 ? QString() : prompt.mid(lineEnd + 1).trimmed();
    const QString id = u"brulion-%1"_s.arg(++m_toolCalls);
    QJsonObject call{{u"toolCallId"_s, id}, {u"title"_s, title}, {u"kind"_s, u"edit"_s}, {u"status"_s, u"pending"_s}};
    if (!details.isEmpty())
        call.insert(u"content"_s,
                    QJsonArray{QJsonObject{{u"type"_s, u"content"_s}, {u"content"_s, textContent(details)}}});
    QJsonObject shown = call;
    shown.insert(u"sessionUpdate"_s, u"tool_call"_s);
    update(session, shown);
    // Its first option allows, its last declines (the assistant's Allow … No), any between them
    // allows more than this once (Allow every change of this question).
    QJsonArray offered;
    for (qsizetype i = 0; i < options.size(); ++i) {
        const QString kind = i == 0                                         ? u"allow_once"_s
                             : i == options.size() - 1 && options.size() > 1 ? u"reject_once"_s
                                                                            : u"allow_always"_s;
        offered.append(QJsonObject{{u"optionId"_s, u"option-%1"_s.arg(i)}, {u"name"_s, options.at(i)}, {u"kind"_s, kind}});
    }
    const quint64 serial = t.serial;
    const QPointer<AcpAgent> self(this);
    m_connection.request(u"session/request_permission"_s,
                         QJsonObject{{u"sessionId"_s, session}, {u"toolCall"_s, call}, {u"options"_s, offered}},
                         [self, session, serial, id, options](const QJsonValue &result, const acp::Connection::Error &error) {
                             if (self)
                                 self->permissionAnswered(session, serial, id, options, result, error);
                         });
}

void AcpAgent::permissionAnswered(const QString &session, quint64 serial, const QString &toolCall,
                                  const QStringList &options, const QJsonValue &result, const acp::Connection::Error &error)
{
    Turn *t = turnOf(session, serial);
    if (!t)
        return; // (cancelled meanwhile)
    QVariant answer; // nothing chosen: b.choose gives nil (the change is not made)
    bool allowed = false;
    const QJsonObject outcome = result.toObject().value(u"outcome"_s).toObject();
    if (error.code == 0 && outcome.value(u"outcome"_s).toString() == u"selected") {
        const QString option = outcome.value(u"optionId"_s).toString();
        bool ok = false;
        const int i = option.startsWith(u"option-"_s) ? option.mid(7).toInt(&ok) : -1;
        if (ok && i >= 0 && i < options.size()) {
            answer = options.at(i);
            allowed = i < options.size() - 1 || options.size() == 1;
        }
    }
    update(session, {{u"sessionUpdate"_s, u"tool_call_update"_s},
                     {u"toolCallId"_s, toolCall},
                     {u"status"_s, allowed ? u"completed"_s : u"failed"_s}});
    const int call = t->call;
    run(session, [this, call, &answer] { return m_engine->resume(call, answer); });
}

void AcpAgent::showStatus(const QString &session, const QString &text)
{
    const auto it = m_sessions.find(session);
    if (it == m_sessions.end() || !it->turn)
        return;
    Turn &t = *it->turn;
    if (text == t.status)
        return;
    closeStatus(session, t, u"completed"_s);
    if (text.isEmpty())
        return;
    t.statusId = u"brulion-%1"_s.arg(++m_toolCalls);
    t.status = text;
    update(session, {{u"sessionUpdate"_s, u"tool_call"_s},
                     {u"toolCallId"_s, t.statusId},
                     {u"title"_s, text.left(ext::QuestionLimits::title)},
                     {u"kind"_s, u"read"_s},
                     {u"status"_s, u"in_progress"_s}});
}

void AcpAgent::closeStatus(const QString &session, Turn &turn, const QString &state)
{
    if (turn.statusId.isEmpty())
        return;
    update(session, {{u"sessionUpdate"_s, u"tool_call_update"_s},
                     {u"toolCallId"_s, std::exchange(turn.statusId, QString())},
                     {u"status"_s, state}});
    turn.status.clear();
}

void AcpAgent::finish(const QString &session, const ext::Result &r)
{
    Session &s = m_sessions[session];
    closeStatus(session, *s.turn, r.ok() ? u"completed"_s : u"failed"_s);
    const QJsonValue promptId = s.turn->promptId;
    s.turn.reset();
    const QString text = r.ok() && r.value.metaType().id() == QMetaType::QString ? r.value.toString().trimmed() : QString();
    if (!text.isEmpty()) {
        update(session, {{u"sessionUpdate"_s, u"agent_message_chunk"_s}, {u"content"_s, textContent(text)}});
        chat::Message answer;
        answer.role = chat::Role::Assistant;
        answer.text = text;
        s.conversation.messages.append(answer);
        say(u"session %1: answered"_s.arg(session));
        m_connection.respond(promptId, QJsonObject{{u"stopReason"_s, u"end_turn"_s}});
        return;
    }
    QString why;
    switch (r.status) {
    case ext::Status::Ok: why = tr("The AI assistant gave no answer."); break;
    case ext::Status::Timeout: why = tr("The AI assistant took too long and was stopped."); break;
    case ext::Status::OutOfMemory: why = tr("The AI assistant needed too much memory and was stopped."); break;
    default: {
        // The model's or the provider's own words ("b.ai: no connection: …") without the
        // script's line before them.
        const QString first = r.error.section(u'\n', 0, 0);
        const qsizetype at = first.indexOf(u"b.ai: "_s);
        why = at >= 0 ? tr("AI: %1").arg(first.mid(at + 6)) : tr("The AI assistant failed: %1").arg(first);
    }
    }
    say(u"session %1: the turn failed: %2"_s.arg(session, r.error.section(u'\n', 0, 0)));
    m_connection.respondError(promptId, kTurnFailed, why);
}

void AcpAgent::update(const QString &session, const QJsonObject &update)
{
    m_connection.notify(u"session/update"_s, QJsonObject{{u"sessionId"_s, session}, {u"update"_s, update}});
}

// ---- the command ----------------------------------------------------------------------------------

namespace {

Result acpAgentCommand(Call &call)
{
    // run() starts the agent itself (it talks to the editor until its input ends).
    return call.fail(ExitUsage, u"not_a_tool"_s, tr("acp agent talks to an editor; it is no tool of its own"));
}

} // namespace

void addAcpAgentCommands(QList<Command> &out)
{
    Command c;
    c.id = u"acp.agent"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                          "Brulion's AI assistant as an agent for Zed, JetBrains and other ACP editors"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "The editor starts it and talks the Agent Client Protocol over standard input and output: each "
            "conversation is one with the app's AI assistant over the notebook, asking the model of the app's "
            "Settings → Extensions → AI (its key from the app's secrets). A change of a note waits for the editor's "
            "yes. Logs on standard error."));
    c.run = acpAgentCommand;
    c.needsVault = false; // (a missing notebook is said in the editor, at its first session)
    c.mcp = false;
    out.append(c);
}

int serveAcpAgent(Context &context, const Command &command, const QJsonObject &args,
                  const std::function<int(const Command *, const Result &)> &output)
{
    Q_UNUSED(command);
    Q_UNUSED(output);
    Q_UNUSED(args);
    // Standard output is the protocol's: everything for people goes to standard error.
    context.serving = true; // (the notebook watched: other programs' changes are seen)
    QString problem;
    notebook::Notebook *nb = nullptr;
    if (const std::optional<Error> e = context.resolveVault()) {
        problem = e->message;
    } else {
        std::optional<Error> error;
        nb = context.notebook(&error);
        if (!nb)
            problem = error ? error->message : QString();
    }
    if (nb)
        nb->index(); // (ready before the first prompt)
    AcpAgentOptions options;
    options.ai = aiSettingsOfApp(context);
    options.aiProblem = aiProblemForCli(options.ai);
    options.language = context.language() == u"pl" ? u"pl"_s : u"en"_s;
    if (!context.globals.now.isEmpty()) {
        const QDateTime fixed = context.now();
        options.now = [fixed] { return fixed; };
    }
    options.version = QCoreApplication::applicationVersion();
    sync::NetworkHttpTransport transport;
    AcpAgent agent(nb, problem, options, &transport, [](const QByteArray &line) {
        std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
        std::fflush(stdout);
    });
    agent.log = logLine;
    if (nb)
        logLine(u"Brulion's assistant for %1, asking %2%3"_s.arg(
                context.vaultRoot(), options.ai.provider,
                options.ai.model.isEmpty() ? QString() : u" (%1)"_s.arg(options.ai.model)));
    else
        logLine(u"no notebook: "_s + problem);
    if (!options.aiProblem.isEmpty())
        logLine(options.aiProblem);
    // Standard input is read on a thread of its own; its bytes go to the main thread as they come
    // (a turn waits for the model there while the editor's next message arrives).
    QCoreApplication *app = QCoreApplication::instance();
    std::thread reader([app, &agent] {
        const auto deliver = [app, &agent](std::string &&bytes) {
            const QByteArray chunk = QByteArray::fromStdString(bytes);
            QMetaObject::invokeMethod(app, [&agent, chunk] { agent.received(chunk); }, Qt::QueuedConnection);
        };
        std::string chunk;
        int c = 0;
        while ((c = std::fgetc(stdin)) != EOF) {
            chunk.push_back(char(c));
            if (c == '\n' || chunk.size() >= 64 * 1024)
                deliver(std::exchange(chunk, {}));
        }
        if (!chunk.empty())
            deliver(std::move(chunk));
        QMetaObject::invokeMethod(
                app,
                [&agent] {
                    agent.closed();
                    QCoreApplication::quit();
                },
                Qt::QueuedConnection);
    });
    const int code = QCoreApplication::exec();
    reader.join();
    return code;
}

} // namespace brulion::cli
