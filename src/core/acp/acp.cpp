#include "acp.h"

#include <QJsonDocument>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::acp {

namespace {

QJsonObject cancelledOutcome()
{
    return QJsonObject{{u"outcome"_s, QJsonObject{{u"outcome"_s, u"cancelled"_s}}}};
}

/// `text` cut to `max` characters (an ellipsis after it).
QString capped(const QString &text, int max)
{
    return text.size() > max ? text.left(max) + u'…' : text;
}

/// The end of `text`, at most `max` characters (an ellipsis before it).
QString tail(const QString &text, int max)
{
    if (text.size() <= max)
        return text;
    qsizetype from = text.size() - max;
    if (text.at(from).isLowSurrogate())
        ++from;
    return u'…' + text.mid(from);
}

/// A content block's text ({type: "text", text}); "" for any other block.
QString textOf(const QJsonValue &content)
{
    const QJsonObject o = content.toObject();
    return o.value(u"type"_s).toString() == u"text" ? o.value(u"text"_s).toString() : QString();
}

Failure failureOf(const Connection::Error &error)
{
    return Failure{error.code == Connection::ClosedCode ? Failure::Kind::Closed : Failure::Kind::Error, error.message};
}

} // namespace

// ---- Block ------------------------------------------------------------------------------------------

Block Block::textBlock(const QString &text)
{
    Block b;
    b.text = text;
    return b;
}

Block Block::link(const QString &uri, const QString &name, const QString &mimeType)
{
    Block b;
    b.kind = Kind::Link;
    b.uri = uri;
    b.name = name;
    b.mimeType = mimeType;
    return b;
}

Block Block::image(const QByteArray &data, const QString &mimeType, const QString &uri, const QString &name)
{
    Block b;
    b.kind = Kind::Image;
    b.data = data;
    b.mimeType = mimeType;
    b.uri = uri;
    b.name = name;
    return b;
}

Block Block::resource(const QString &uri, const QString &name, const QString &text, const QString &mimeType)
{
    Block b;
    b.kind = Kind::Resource;
    b.uri = uri;
    b.name = name;
    b.text = text;
    b.mimeType = mimeType;
    return b;
}

QJsonObject Block::toJson(bool images, bool embedded) const
{
    const auto linkJson = [this](const QString &type) {
        QJsonObject o{{u"type"_s, u"resource_link"_s}, {u"uri"_s, uri}, {u"name"_s, name}};
        if (!type.isEmpty())
            o.insert(u"mimeType"_s, type);
        return o;
    };
    switch (kind) {
    case Kind::Text:
        return {{u"type"_s, u"text"_s}, {u"text"_s, text}};
    case Kind::Link:
        return linkJson(mimeType);
    case Kind::Image:
        if (!images && !uri.isEmpty())
            return linkJson(mimeType);
        return {{u"type"_s, u"image"_s}, {u"mimeType"_s, mimeType}, {u"data"_s, QString::fromLatin1(data.toBase64())}};
    case Kind::Resource: {
        if (!embedded)
            return linkJson(mimeType);
        QJsonObject resource{{u"uri"_s, uri}, {u"text"_s, text}};
        if (!mimeType.isEmpty())
            resource.insert(u"mimeType"_s, mimeType);
        return {{u"type"_s, u"resource"_s}, {u"resource"_s, resource}};
    }
    }
    return {};
}

// ---- options ----------------------------------------------------------------------------------------

QString ConfigOption::currentName() const
{
    if (boolean)
        return {};
    const QString value = current.toString();
    for (const Choice &c : choices) {
        if (c.value == value)
            return c.name;
    }
    return value;
}

QJsonObject ConfigOption::toJson() const
{
    QJsonArray list;
    for (const Choice &c : choices) {
        QJsonObject o{{u"value"_s, c.value}, {u"name"_s, c.name}};
        if (!c.description.isEmpty())
            o.insert(u"description"_s, c.description);
        if (!c.group.isEmpty())
            o.insert(u"group"_s, c.group);
        list.append(o);
    }
    QJsonObject o{{u"id"_s, id},
                  {u"name"_s, name},
                  {u"type"_s, boolean ? u"boolean"_s : u"select"_s},
                  {u"currentValue"_s, current},
                  {u"options"_s, list}};
    if (!description.isEmpty())
        o.insert(u"description"_s, description);
    if (!category.isEmpty())
        o.insert(u"category"_s, category);
    if (via != Via::Config)
        o.insert(u"via"_s, via == Via::Mode ? u"mode"_s : u"model"_s);
    return o;
}

std::optional<ConfigOption> ConfigOption::fromJson(const QJsonObject &o)
{
    ConfigOption option;
    option.id = o.value(u"id"_s).toString();
    if (option.id.isEmpty())
        return std::nullopt;
    option.name = o.value(u"name"_s).toString().simplified();
    if (option.name.isEmpty())
        option.name = option.id;
    option.description = o.value(u"description"_s).toString().simplified();
    option.category = o.value(u"category"_s).toString();
    const QString via = o.value(u"via"_s).toString();
    option.via = via == u"mode" ? Via::Mode : via == u"model" ? Via::Model : Via::Config;
    const QString type = o.value(u"type"_s).toString();
    if (type == u"boolean") {
        option.boolean = true;
        option.current = o.value(u"currentValue"_s).toBool();
        return option;
    }
    if (type != u"select")
        return std::nullopt; // (a kind of option this client does not know)
    option.current = o.value(u"currentValue"_s).toString();
    const auto addChoice = [&option](const QJsonObject &c, const QString &group) {
        Choice choice{c.value(u"value"_s).toString(), c.value(u"name"_s).toString().simplified(),
                      c.value(u"description"_s).toString().simplified(),
                      group.isEmpty() ? c.value(u"group"_s).toString() : group};
        if (choice.value.isEmpty())
            return;
        if (choice.name.isEmpty())
            choice.name = choice.value;
        option.choices << choice;
    };
    for (const QJsonValue &v : o.value(u"options"_s).toArray()) {
        const QJsonObject c = v.toObject();
        if (c.contains(u"options"_s)) { // a group
            const QString group = c.value(u"name"_s).toString().simplified();
            for (const QJsonValue &inner : c.value(u"options"_s).toArray())
                addChoice(inner.toObject(), group);
        } else {
            addChoice(c, QString());
        }
    }
    return option;
}

QList<ConfigOption> readOptions(const QJsonObject &result)
{
    QList<ConfigOption> out;
    QSet<QString> categories, ids;
    for (const QJsonValue &v : result.value(u"configOptions"_s).toArray()) {
        if (std::optional<ConfigOption> o = ConfigOption::fromJson(v.toObject()); o && !ids.contains(o->id)) {
            o->via = ConfigOption::Via::Config;
            categories.insert(o->category);
            ids.insert(o->id);
            out << *o;
        }
    }
    const auto freeId = [&ids](const QString &base) {
        QString id = base;
        while (ids.contains(id))
            id.prepend(u'_');
        ids.insert(id);
        return id;
    };
    const auto synthetic = [&](const QString &kind, const QString &name, ConfigOption::Via via, const QJsonObject &from,
                               const QString &list, const QString &current, const QString &idKey) {
        if (categories.contains(kind) || from.value(list).toArray().isEmpty())
            return;
        ConfigOption o;
        o.id = freeId(kind);
        o.name = name;
        o.category = kind;
        o.via = via;
        o.current = from.value(current).toString();
        for (const QJsonValue &v : from.value(list).toArray()) {
            const QJsonObject m = v.toObject();
            if (!m.value(idKey).toString().isEmpty())
                o.choices << Choice{m.value(idKey).toString(), m.value(u"name"_s).toString().simplified(),
                                    m.value(u"description"_s).toString().simplified(), QString()};
        }
        out << o;
    };
    synthetic(u"mode"_s, u"Mode"_s, ConfigOption::Via::Mode, result.value(u"modes"_s).toObject(), u"availableModes"_s,
              u"currentModeId"_s, u"id"_s);
    synthetic(u"model"_s, u"Model"_s, ConfigOption::Via::Model, result.value(u"models"_s).toObject(),
              u"availableModels"_s, u"currentModelId"_s, u"modelId"_s);
    for (ConfigOption &o : out) {
        for (Choice &c : o.choices) {
            if (c.name.isEmpty())
                c.name = c.value;
        }
    }
    return out;
}

// ---- Connection -------------------------------------------------------------------------------------

Connection::Connection(Send send) : m_send(std::move(send)) {}

void Connection::write(const QJsonObject &message)
{
    QJsonObject full = message;
    full.insert(u"jsonrpc"_s, u"2.0"_s);
    if (m_send)
        m_send(QJsonDocument(full).toJson(QJsonDocument::Compact) + '\n');
}

void Connection::request(const QString &method, const QJsonObject &params, Done done)
{
    const qint64 id = m_nextId++;
    m_waiting.insert(id, std::move(done));
    write({{u"id"_s, id}, {u"method"_s, method}, {u"params"_s, params}});
}

void Connection::notify(const QString &method, const QJsonObject &params)
{
    write({{u"method"_s, method}, {u"params"_s, params}});
}

void Connection::respond(const QJsonValue &id, const QJsonValue &result)
{
    write({{u"id"_s, id}, {u"result"_s, result}});
}

void Connection::respondError(const QJsonValue &id, int code, const QString &message)
{
    write({{u"id"_s, id}, {u"error"_s, QJsonObject{{u"code"_s, code}, {u"message"_s, message}}}});
}

void Connection::receive(const QByteArray &bytes)
{
    qsizetype from = 0;
    while (from < bytes.size()) {
        const qsizetype end = bytes.indexOf('\n', from);
        const qsizetype stop = end < 0 ? bytes.size() : end;
        if (!m_skipping) {
            if (m_buffer.size() + (stop - from) > MaxLineBytes) {
                m_buffer.clear();
                m_skipping = true;
                if (onMalformed)
                    onMalformed(u"a line longer than %1 bytes"_s.arg(MaxLineBytes));
            } else {
                m_buffer.append(bytes.constData() + from, stop - from);
            }
        }
        if (end < 0)
            break;
        from = end + 1;
        if (m_skipping) {
            m_skipping = false;
            continue;
        }
        const QByteArray line = std::exchange(m_buffer, {});
        readLine(line);
    }
}

void Connection::readLine(const QByteArray &line)
{
    if (line.trimmed().isEmpty())
        return;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (!doc.isObject()) {
        if (onMalformed)
            onMalformed(error.error != QJsonParseError::NoError ? error.errorString() : u"not an object"_s);
        return;
    }
    const QJsonObject message = doc.object();
    const QJsonValue id = message.value(u"id"_s);
    const QString method = message.value(u"method"_s).toString();
    if (!method.isEmpty()) {
        const QJsonObject params = message.value(u"params"_s).toObject();
        if (id.isUndefined() || id.isNull()) {
            if (onNotification)
                onNotification(method, params);
        } else if (onRequest) {
            onRequest(id, method, params);
        } else {
            respondError(id, MethodNotFound, u"Method not found"_s);
        }
        return;
    }
    // An answer to one of ours (our ids are numbers).
    if (!id.isDouble()) {
        if (onMalformed)
            onMalformed(u"an answer without a request"_s);
        return;
    }
    const auto it = m_waiting.find(static_cast<qint64>(id.toDouble()));
    if (it == m_waiting.end()) {
        if (onMalformed)
            onMalformed(u"an answer to no request"_s);
        return;
    }
    Done done = std::move(it.value());
    m_waiting.erase(it);
    Error err;
    if (message.contains(u"error"_s)) {
        const QJsonObject e = message.value(u"error"_s).toObject();
        err.code = e.value(u"code"_s).toInt(-32603);
        if (err.code == 0)
            err.code = -32603;
        err.message = e.value(u"message"_s).toString();
    }
    if (done)
        done(message.value(u"result"_s), err);
}

void Connection::close(const QString &why)
{
    const QHash<qint64, Done> waiting = std::exchange(m_waiting, {});
    m_buffer.clear();
    m_skipping = false;
    for (const Done &done : waiting) {
        if (done)
            done(QJsonValue(), Error{ClosedCode, why});
    }
}

// ---- TurnState, SessionInfo ---------------------------------------------------------------------------

int TurnState::apply(const QJsonObject &update)
{
    const QString kind = update.value(u"sessionUpdate"_s).toString();
    if (kind == u"agent_message_chunk") {
        const QString chunk = textOf(update.value(u"content"_s));
        if (chunk.isEmpty())
            return None;
        // (Another message of the agent, or a tool call after the text: a new paragraph.)
        const QString message = update.value(u"messageId"_s).toString();
        if (!message.isEmpty() && message != messageId && !messageId.isEmpty())
            afterTool = true;
        if (!message.isEmpty())
            messageId = message;
        if (afterTool && !text.isEmpty()) {
            while (text.endsWith(u' '))
                text.chop(1);
            if (!text.endsWith(u"\n\n"_s))
                text += text.endsWith(u'\n') ? u"\n"_s : u"\n\n"_s;
        }
        afterTool = false;
        text += chunk;
        return Text;
    }
    if (kind == u"agent_thought_chunk") {
        const QString chunk = textOf(update.value(u"content"_s));
        if (chunk.isEmpty())
            return None;
        thoughts += chunk;
        return Text;
    }
    if (kind == u"plan") {
        QList<PlanEntry> entries;
        for (const QJsonValue &v : update.value(u"entries"_s).toArray()) {
            const QJsonObject e = v.toObject();
            const QString content = e.value(u"content"_s).toString().simplified();
            if (!content.isEmpty())
                entries << PlanEntry{content, e.value(u"status"_s).toString(), e.value(u"priority"_s).toString()};
        }
        if (entries == plan)
            return None;
        plan = entries;
        return Activity;
    }
    if (kind != u"tool_call" && kind != u"tool_call_update")
        return None;

    int changed = None;
    const QString id = update.value(u"toolCallId"_s).toString();
    const QString title = update.value(u"title"_s).toString().simplified();
    // (The status shows this call now: its title as it was.)
    const bool shown = !status.isEmpty() && status == titles.value(id);
    if (!title.isEmpty())
        titles.insert(id, title);
    if (!text.isEmpty())
        afterTool = true;
    const QString state = update.value(u"status"_s).toString();
    const QString known = titles.value(id);
    QString next = status;
    if (state == u"completed" || state == u"failed") {
        if (shown)
            next.clear();
    } else if (kind == u"tool_call" || state == u"in_progress" || shown) {
        next = known;
    }
    if (next != status) {
        status = next;
        changed |= Status;
    }

    // The call in the turn's list.
    auto call = std::ranges::find_if(tools, [&id](const ToolCall &t) { return t.id == id; });
    if (call == tools.end()) {
        if (id.isEmpty())
            return changed;
        tools.append(ToolCall{id, QString(), QString(), u"pending"_s, QString(), QString()});
        while (tools.size() > MaxToolCalls)
            tools.removeFirst();
        call = tools.end() - 1;
    }
    const ToolCall before = *call;
    if (!known.isEmpty())
        call->title = known;
    if (update.contains(u"kind"_s))
        call->kind = update.value(u"kind"_s).toString();
    if (!state.isEmpty())
        call->status = state;
    if (update.contains(u"content"_s)) {
        QStringList texts;
        for (const QJsonValue &v : update.value(u"content"_s).toArray()) {
            const QJsonObject c = v.toObject();
            const QString type = c.value(u"type"_s).toString();
            if (type == u"terminal")
                call->terminal = c.value(u"terminalId"_s).toString();
            else if (type == u"content" && !textOf(c.value(u"content"_s)).isEmpty())
                texts << textOf(c.value(u"content"_s));
            else if (type == u"diff") {
                texts << c.value(u"path"_s).toString();
                if (!call->diff)
                    call->diff = Diff{c.value(u"path"_s).toString(), c.value(u"oldText"_s).toString().left(MaxDiffChars),
                                      c.value(u"newText"_s).toString().left(MaxDiffChars)};
            }
        }
        if (!texts.isEmpty())
            call->output = tail(texts.join(u'\n').trimmed(), MaxToolOutputChars);
    }
    if (call->output.isEmpty() && update.value(u"rawOutput"_s).isString())
        call->output = tail(update.value(u"rawOutput"_s).toString().trimmed(), MaxToolOutputChars);
    if (*call != before)
        changed |= Activity;
    return changed;
}

bool SessionInfo::apply(const QJsonObject &update)
{
    const QString kind = update.value(u"sessionUpdate"_s).toString();
    const SessionInfo before = *this;
    if (kind == u"config_option_update") {
        QList<ConfigOption> next = readOptions(QJsonObject{{u"configOptions"_s, update.value(u"configOptions"_s)}});
        // (The older modes and models stay where no option of their kind came.)
        for (const ConfigOption &o : std::as_const(options)) {
            if (o.via != ConfigOption::Via::Config
                && std::ranges::none_of(next, [&o](const ConfigOption &n) { return n.category == o.category; }))
                next << o;
        }
        options = next;
    } else if (kind == u"current_mode_update") {
        const QString mode = update.value(u"currentModeId"_s).toString();
        for (ConfigOption &o : options) {
            if (o.via == ConfigOption::Via::Mode)
                o.current = mode;
        }
    } else if (kind == u"available_commands_update") {
        commands.clear();
        for (const QJsonValue &v : update.value(u"availableCommands"_s).toArray()) {
            const QJsonObject c = v.toObject();
            QString name = c.value(u"name"_s).toString().trimmed();
            while (name.startsWith(u'/'))
                name.remove(0, 1);
            if (name.isEmpty() || name.contains(u' '))
                continue;
            commands << Command{name, c.value(u"description"_s).toString().simplified(),
                                c.value(u"input"_s).toObject().value(u"hint"_s).toString().simplified()};
        }
    } else if (kind == u"usage_update") {
        usage.used = static_cast<qint64>(update.value(u"used"_s).toDouble());
        usage.size = static_cast<qint64>(update.value(u"size"_s).toDouble());
        const QJsonObject cost = update.value(u"cost"_s).toObject();
        if (cost.contains(u"amount"_s)) {
            usage.cost = cost.value(u"amount"_s).toDouble();
            usage.currency = cost.value(u"currency"_s).toString();
        }
    } else if (kind == u"session_info_update") {
        if (update.value(u"title"_s).isString())
            title = update.value(u"title"_s).toString().simplified();
    } else {
        return false;
    }
    return !(*this == before);
}

// ---- reading the agent's messages ---------------------------------------------------------------------

std::optional<Permission> readPermission(const QJsonObject &params, const QHash<QString, QString> &titles)
{
    const QJsonObject call = params.value(u"toolCall"_s).toObject();
    Permission p;
    p.title = call.value(u"title"_s).toString().simplified();
    if (p.title.isEmpty())
        p.title = titles.value(call.value(u"toolCallId"_s).toString());
    QStringList details;
    for (const QJsonValue &v : call.value(u"content"_s).toArray()) {
        const QJsonObject c = v.toObject();
        const QString type = c.value(u"type"_s).toString();
        if (type == u"diff") {
            details << c.value(u"path"_s).toString() + u'\n' + c.value(u"newText"_s).toString();
            p.diffs << Diff{c.value(u"path"_s).toString(), c.value(u"oldText"_s).toString().left(MaxDiffChars),
                            c.value(u"newText"_s).toString().left(MaxDiffChars)};
        } else if (type == u"content" && !textOf(c.value(u"content"_s)).isEmpty()) {
            details << textOf(c.value(u"content"_s));
        }
    }
    if (details.isEmpty()) {
        const QJsonValue input = call.value(u"rawInput"_s);
        if (input.isObject() && !input.toObject().isEmpty())
            details << QString::fromUtf8(QJsonDocument(input.toObject()).toJson(QJsonDocument::Indented)).trimmed();
        else if (input.isString())
            details << input.toString();
    }
    p.details = capped(details.join(u"\n\n"_s).trimmed(), MaxDetailChars);
    for (const QJsonValue &v : params.value(u"options"_s).toArray()) {
        const QJsonObject o = v.toObject();
        PermissionOption option{o.value(u"optionId"_s).toString(), o.value(u"name"_s).toString().simplified(),
                                o.value(u"kind"_s).toString()};
        if (option.id.isEmpty())
            continue;
        if (option.name.isEmpty())
            option.name = option.id;
        p.options << option;
    }
    if (p.options.isEmpty())
        return std::nullopt;
    return p;
}

QList<AuthMethod> readAuthMethods(const QJsonValue &methods)
{
    QList<AuthMethod> out;
    for (const QJsonValue &v : methods.toArray()) {
        const QJsonObject o = v.toObject();
        AuthMethod m;
        m.id = o.value(u"id"_s).toString();
        if (m.id.isEmpty())
            continue;
        m.name = o.value(u"name"_s).toString().simplified();
        if (m.name.isEmpty())
            m.name = m.id;
        m.description = o.value(u"description"_s).toString().simplified();
        m.terminal = o.value(u"type"_s).toString() == u"terminal";
        for (const QJsonValue &a : o.value(u"args"_s).toArray())
            m.args << a.toString();
        out << m;
    }
    return out;
}

// ---- Client -----------------------------------------------------------------------------------------

Client::Client(Connection::Send send, const QString &version, QObject *parent)
    : QObject(parent), m_connection(std::move(send)), m_version(version.isEmpty() ? u"0"_s : version)
{
    qRegisterMetaType<brulion::acp::Permission>();
    qRegisterMetaType<brulion::acp::Failure>();
    m_connection.onRequest = [this](const QJsonValue &id, const QString &method, const QJsonObject &params) {
        onRequest(id, method, params);
    };
    m_connection.onNotification = [this](const QString &method, const QJsonObject &params) {
        onNotification(method, params);
    };
}

Client::~Client()
{
    // (No callback of a request waiting may reach this object any more.)
    m_connection.onRequest = nullptr;
    m_connection.onNotification = nullptr;
    m_sessions.clear();
    m_phase = Phase::Failed;
    m_startFailure = Failure{Failure::Kind::Closed, QString()};
    m_connection.close(QString());
}

void Client::received(const QByteArray &bytes)
{
    m_connection.receive(bytes);
}

void Client::closed(const QString &why)
{
    m_phase = Phase::Failed;
    m_startFailure = Failure{Failure::Kind::Closed, why};
    m_connection.close(why); // (each request's callback fails its turn)
    failAll(*m_startFailure);
}

bool Client::running(const QString &key) const
{
    const auto it = m_sessions.constFind(key);
    return it != m_sessions.cend() && ((it->turn && !it->turn->cancelled && !it->turn->openOnly) || it->next);
}

bool Client::hasSession(const QString &key) const
{
    const auto it = m_sessions.constFind(key);
    return it != m_sessions.cend() && !it->id.isEmpty();
}

QString Client::sessionId(const QString &key) const
{
    return m_sessions.value(key).id;
}

SessionInfo Client::session(const QString &key) const
{
    return m_sessions.value(key).info;
}

const TurnState *Client::turn(const QString &key) const
{
    const auto it = m_sessions.constFind(key);
    if (it == m_sessions.cend() || !it->turn || it->turn->cancelled || it->turn->openOnly)
        return nullptr;
    return &it->turn->state;
}

const TurnState *Client::lastTurn(const QString &key) const
{
    const auto it = m_sessions.constFind(key);
    return it == m_sessions.cend() || !it->last ? nullptr : &*it->last;
}

bool Client::prompt(const QString &key, const TurnInput &input)
{
    if (running(key))
        return false;
    Session &s = m_sessions[key];
    if (s.turn && s.turn->openOnly && !s.turn->cancelled) {
        // (The session being made by open() takes this turn.)
        s.turn->input.firstPrompt = input.firstPrompt;
        s.turn->input.prompt = input.prompt;
        for (auto it = input.config.cbegin(); it != input.config.cend(); ++it)
            s.turn->input.config.insert(it.key(), it.value());
        s.turn->openOnly = false;
        return true;
    }
    if (s.turn) { // (a cancelled turn still ends: this one waits for it)
        s.next = input;
        return true;
    }
    s.turn = Turn{};
    s.turn->input = input;
    switch (m_phase) {
    case Phase::None:
        start();
        break;
    case Phase::Starting:
        break;
    case Phase::Ready:
        startTurn(key);
        break;
    case Phase::Failed:
        fail(key, m_startFailure.value_or(Failure{Failure::Kind::Closed, QString()}));
        break;
    }
    return true;
}

bool Client::open(const QString &key, const TurnInput &input)
{
    if (m_phase == Phase::Failed)
        return false;
    Session &s = m_sessions[key];
    if (s.turn || !s.id.isEmpty() || s.next)
        return true;
    s.turn = Turn{};
    s.turn->input = input;
    s.turn->openOnly = true;
    if (m_phase == Phase::None)
        start();
    else if (m_phase == Phase::Ready)
        startTurn(key);
    return true;
}

void Client::start()
{
    m_phase = Phase::Starting;
    const QJsonObject params{
            {u"protocolVersion"_s, ProtocolVersion},
            {u"clientCapabilities"_s,
             QJsonObject{{u"fs"_s, QJsonObject{{u"readTextFile"_s, m_capabilities.files},
                                               {u"writeTextFile"_s, m_capabilities.files}}},
                         {u"terminal"_s, m_capabilities.terminals}}},
            {u"clientInfo"_s, QJsonObject{{u"name"_s, u"brulion"_s}, {u"title"_s, u"Brulion"_s}, {u"version"_s, m_version}}}};
    m_connection.request(u"initialize"_s, params, [this](const QJsonValue &result, const Connection::Error &error) {
        if (error.code != 0) {
            m_phase = Phase::Failed;
            m_startFailure = failureOf(error);
            failAll(*m_startFailure);
            return;
        }
        const QJsonObject r = result.toObject();
        const QJsonValue version = r.value(u"protocolVersion"_s);
        if (!version.isDouble() || version.toInt() != ProtocolVersion) {
            m_phase = Phase::Failed;
            m_startFailure = Failure{Failure::Kind::Version,
                                     version.isDouble() ? QString::number(version.toInt()) : version.toString()};
            failAll(*m_startFailure);
            return;
        }
        const QJsonObject info = r.value(u"agentInfo"_s).toObject();
        m_agentName = info.value(u"title"_s).toString().simplified();
        if (m_agentName.isEmpty())
            m_agentName = info.value(u"name"_s).toString().simplified();
        m_agentVersion = info.value(u"version"_s).toString().simplified();
        const QJsonObject caps = r.value(u"agentCapabilities"_s).toObject();
        const QJsonObject prompts = caps.value(u"promptCapabilities"_s).toObject();
        m_images = prompts.value(u"image"_s).toBool();
        m_embedded = prompts.value(u"embeddedContext"_s).toBool();
        m_load = caps.value(u"loadSession"_s).toBool();
        m_resume = caps.value(u"sessionCapabilities"_s).toObject().value(u"resume"_s).isObject();
        m_close = caps.value(u"sessionCapabilities"_s).toObject().value(u"close"_s).isObject();
        m_authMethods = readAuthMethods(r.value(u"authMethods"_s));
        m_phase = Phase::Ready;
        const QStringList keys = m_sessions.keys();
        for (const QString &key : keys)
            startTurn(key);
    });
}

void Client::startTurn(const QString &key)
{
    const auto it = m_sessions.find(key);
    if (it == m_sessions.end() || !it->turn || it->turn->prompting || it->creating)
        return;
    if (it->turn->cancelled) {
        endTurn(key);
        return;
    }
    if (it->id.isEmpty())
        newSession(key, true);
    else
        applyOptions(key);
}

QJsonObject Client::sessionParams(const TurnInput &input) const
{
    QJsonArray servers;
    for (const McpServer &m : input.mcpServers)
        servers.append(QJsonObject{{u"name"_s, m.name},
                                   {u"command"_s, m.command},
                                   {u"args"_s, QJsonArray::fromStringList(m.args)},
                                   {u"env"_s, QJsonArray()}});
    return QJsonObject{{u"cwd"_s, input.cwd}, {u"mcpServers"_s, servers}};
}

void Client::newSession(const QString &key, bool tryResume)
{
    Session &s = m_sessions[key];
    s.creating = true;
    const TurnInput &input = s.turn->input;
    QJsonObject params = sessionParams(input);
    const QString resume = tryResume && (m_resume || m_load) ? input.resume : QString();
    QString method = u"session/new"_s;
    if (!resume.isEmpty()) {
        method = m_resume ? u"session/resume"_s : u"session/load"_s;
        params.insert(u"sessionId"_s, resume);
    }
    m_connection.request(method, params, [this, key, resume](const QJsonValue &result, const Connection::Error &error) {
        const auto it = m_sessions.find(key);
        if (it == m_sessions.end())
            return;
        it->creating = false;
        const QString id = error.code != 0 ? QString()
                           : resume.isEmpty() ? result.toObject().value(u"sessionId"_s).toString()
                                              : resume;
        if (!id.isEmpty() && (!it->turn || it->turn->cancelled)) {
            // (A turn cancelled meanwhile: the next one goes on in this session.)
            it->id = id;
            it->info.options = readOptions(result.toObject());
            Q_EMIT sessionStarted(key, id, !resume.isEmpty());
        }
        if (!it->turn)
            return;
        if (it->turn->cancelled) {
            endTurn(key);
            return;
        }
        if (error.code == AuthRequired) {
            Q_EMIT authRequired(key, m_authMethods, error.message);
            return;
        }
        if (!resume.isEmpty() && error.code != 0 && error.code != Connection::ClosedCode) {
            newSession(key, false); // (the earlier session is gone: a new one, with the conversation so far)
            return;
        }
        if (error.code != 0) {
            fail(key, failureOf(error));
            return;
        }
        if (id.isEmpty()) {
            fail(key, Failure{Failure::Kind::Malformed, u"session/new gave no sessionId"_s});
            return;
        }
        sessionReady(key, id, result.toObject(), resume.isEmpty(), !resume.isEmpty());
    });
}

void Client::sessionReady(const QString &key, const QString &id, const QJsonObject &result, bool fresh, bool resumed)
{
    Session &s = m_sessions[key];
    s.id = id;
    s.info.options = readOptions(result);
    s.turn->fresh = fresh;
    Q_EMIT sessionStarted(key, id, resumed);
    Q_EMIT sessionChanged(key);
    applyOptions(key);
}

void Client::applyOptions(const QString &key)
{
    const auto it = m_sessions.find(key);
    if (it == m_sessions.end() || !it->turn)
        return;
    if (it->turn->cancelled) {
        endTurn(key);
        return;
    }
    // The options asked for and not set yet, one at a time (an agent may answer them in any order).
    QHash<QString, QJsonValue> wanted = it->turn->input.config;
    for (auto w = it->wanted.cbegin(); w != it->wanted.cend(); ++w)
        wanted.insert(w.key(), w.value());
    it->wanted.clear();
    it->turn->input.config.clear();
    for (auto w = wanted.cbegin(); w != wanted.cend(); ++w) {
        const auto option = std::ranges::find_if(it->info.options, [&w](const ConfigOption &o) { return o.id == w.key(); });
        if (option == it->info.options.end() || option->current == w.value())
            continue;
        if (!option->boolean && std::ranges::none_of(option->choices, [&w](const Choice &c) {
                return c.value == w.value().toString();
            }))
            continue;
        QHash<QString, QJsonValue> rest = wanted;
        rest.remove(w.key());
        it->turn->input.config = rest;
        it->creating = true; // (no prompt nor other option meanwhile)
        sendOption(key, *option, w.value(), [this, key] {
            if (auto s = m_sessions.find(key); s != m_sessions.end())
                s->creating = false;
            applyOptions(key);
        });
        return;
    }
    if (it->turn->openOnly) {
        endTurn(key);
        Q_EMIT opened(key);
        return;
    }
    sendPrompt(key);
}

void Client::sendOption(const QString &key, const ConfigOption &option, const QJsonValue &value, std::function<void()> then)
{
    const QString session = m_sessions.value(key).id;
    QString method;
    QJsonObject params{{u"sessionId"_s, session}};
    switch (option.via) {
    case ConfigOption::Via::Config:
        method = u"session/set_config_option"_s;
        params.insert(u"configId"_s, option.id);
        if (option.boolean)
            params.insert(u"type"_s, u"boolean"_s);
        params.insert(u"value"_s, value);
        break;
    case ConfigOption::Via::Mode:
        method = u"session/set_mode"_s;
        params.insert(u"modeId"_s, value.toString());
        break;
    case ConfigOption::Via::Model:
        method = u"session/set_model"_s;
        params.insert(u"modelId"_s, value.toString());
        break;
    }
    const QString id = option.id;
    m_connection.request(method, params,
                         [this, key, id, value, then = std::move(then)](const QJsonValue &result, const Connection::Error &error) {
                             const auto it = m_sessions.find(key);
                             if (it == m_sessions.end())
                                 return;
                             if (error.code == 0) {
                                 const QJsonObject r = result.toObject();
                                 if (r.value(u"configOptions"_s).isArray()) {
                                     it->info.apply(QJsonObject{{u"sessionUpdate"_s, u"config_option_update"_s},
                                                                {u"configOptions"_s, r.value(u"configOptions"_s)}});
                                 } else {
                                     for (ConfigOption &o : it->info.options) {
                                         if (o.id == id)
                                             o.current = value;
                                     }
                                 }
                                 Q_EMIT sessionChanged(key);
                             }
                             // (An option the agent refused is left as it is: the turn goes on.)
                             if (then)
                                 then();
                         });
}

bool Client::setOption(const QString &key, const QString &id, const QJsonValue &value)
{
    Session &s = m_sessions[key];
    if (s.id.isEmpty() || s.creating || (s.turn && !s.turn->prompting)) {
        s.wanted.insert(id, value); // (set when the session is there, before its prompt)
        return true;
    }
    const auto option = std::ranges::find_if(s.info.options, [&id](const ConfigOption &o) { return o.id == id; });
    if (option == s.info.options.end())
        return false;
    if (option->current == value)
        return true;
    sendOption(key, *option, value, {});
    return true;
}

void Client::forget(const QString &key)
{
    const auto it = m_sessions.find(key);
    if (it == m_sessions.end() || it->turn || it->next || it->creating)
        return;
    m_sessions.erase(it);
}

void Client::close(const QString &key)
{
    const auto it = m_sessions.find(key);
    if (it == m_sessions.end())
        return;
    cancel(key);
    const QString id = it->id;
    if (!id.isEmpty() && m_close && m_phase == Phase::Ready)
        m_connection.request(u"session/close"_s, QJsonObject{{u"sessionId"_s, id}}, nullptr);
    // (A cancelled turn's answer finds no session: nothing is said of it.)
    m_sessions.erase(m_sessions.find(key));
}

void Client::rename(const QString &from, const QString &to)
{
    if (from == to || !m_sessions.contains(from) || m_sessions.contains(to))
        return;
    m_sessions.insert(to, m_sessions.take(from));
}

void Client::authenticate(const QString &key, const QString &methodId)
{
    const auto it = m_sessions.find(key);
    if (it == m_sessions.end() || !it->turn || it->turn->cancelled || it->creating || !it->id.isEmpty())
        return;
    it->creating = true;
    m_connection.request(u"authenticate"_s, QJsonObject{{u"methodId"_s, methodId}},
                         [this, key](const QJsonValue &, const Connection::Error &error) {
                             const auto it = m_sessions.find(key);
                             if (it == m_sessions.end())
                                 return;
                             it->creating = false;
                             if (!it->turn)
                                 return;
                             if (it->turn->cancelled) {
                                 endTurn(key);
                                 return;
                             }
                             if (error.code != 0) {
                                 fail(key, failureOf(error));
                                 return;
                             }
                             newSession(key, true);
                         });
}

void Client::sendPrompt(const QString &key)
{
    Session &s = m_sessions[key];
    Turn &t = *s.turn;
    t.prompting = true;
    const QList<Block> &blocks = t.fresh && !t.input.firstPrompt.isEmpty() ? t.input.firstPrompt : t.input.prompt;
    QJsonArray prompt;
    for (const Block &b : blocks)
        prompt.append(b.toJson(m_images, m_embedded));
    m_connection.request(u"session/prompt"_s, QJsonObject{{u"sessionId"_s, s.id}, {u"prompt"_s, prompt}},
                         [this, key](const QJsonValue &result, const Connection::Error &error) {
                             const auto it = m_sessions.find(key);
                             if (it == m_sessions.end() || !it->turn)
                                 return;
                             if (it->turn->cancelled) {
                                 endTurn(key);
                                 return;
                             }
                             if (error.code != 0) {
                                 if (error.code == Connection::ClosedCode)
                                     it->id.clear(); // (the session went with the agent)
                                 fail(key, failureOf(error));
                                 return;
                             }
                             const QString reason = result.toObject().value(u"stopReason"_s).toString();
                             const QString text = it->turn->state.text.trimmed();
                             if (reason == u"refusal") {
                                 fail(key, Failure{Failure::Kind::Refusal, text});
                                 return;
                             }
                             // (A turn of tool calls only — a file written — ends without words.)
                             if (text.isEmpty() && (it->turn->state.tools.isEmpty() || reason == u"cancelled")) {
                                 fail(key, Failure{Failure::Kind::Empty, reason});
                                 return;
                             }
                             it->last = it->turn->state;
                             endTurn(key);
                             Q_EMIT finished(key, text, reason);
                         });
}

void Client::cancel(const QString &key)
{
    const auto it = m_sessions.find(key);
    if (it == m_sessions.end())
        return;
    it->next.reset();
    if (!it->turn || it->turn->cancelled)
        return;
    Turn &t = *it->turn;
    for (const auto &[id, question] : std::as_const(t.questions))
        m_connection.respond(id, cancelledOutcome());
    t.questions.clear();
    if (t.prompting) {
        t.cancelled = true;
        m_connection.notify(u"session/cancel"_s, QJsonObject{{u"sessionId"_s, it->id}});
    } else if (it->creating) {
        t.cancelled = true; // (its session, or an option set on it, still answers)
    } else {
        it->turn.reset(); // (waiting for the handshake, or for a sign-in)
    }
}

void Client::answer(const QString &key, const QString &optionId)
{
    const auto it = m_sessions.find(key);
    if (it == m_sessions.end() || !it->turn || it->turn->cancelled || it->turn->questions.isEmpty())
        return;
    Turn &t = *it->turn;
    const auto [id, question] = t.questions.takeFirst();
    const bool offered = std::ranges::any_of(question.options,
                                             [&optionId](const PermissionOption &o) { return o.id == optionId; });
    if (offered)
        m_connection.respond(id, QJsonObject{{u"outcome"_s,
                                              QJsonObject{{u"outcome"_s, u"selected"_s}, {u"optionId"_s, optionId}}}});
    else
        m_connection.respond(id, cancelledOutcome());
    if (!t.questions.isEmpty())
        Q_EMIT permissionAsked(key, t.questions.first().second);
}

void Client::endTurn(const QString &key)
{
    const auto it = m_sessions.find(key);
    if (it == m_sessions.end())
        return;
    it->turn.reset();
    if (!it->next)
        return;
    it->turn = Turn{};
    it->turn->input = *std::exchange(it->next, std::nullopt);
    if (m_phase == Phase::Ready)
        startTurn(key);
    else if (m_phase == Phase::Failed)
        fail(key, m_startFailure.value_or(Failure{Failure::Kind::Closed, QString()}));
}

void Client::fail(const QString &key, const Failure &failure)
{
    const auto it = m_sessions.find(key);
    if (it == m_sessions.end() || !it->turn)
        return;
    const bool quiet = it->turn->cancelled;
    it->turn.reset();
    it->next.reset();
    if (!quiet)
        Q_EMIT failed(key, failure);
}

void Client::failAll(const Failure &failure)
{
    const QStringList keys = m_sessions.keys();
    for (const QString &key : keys) {
        const auto it = m_sessions.find(key);
        if (it == m_sessions.end())
            continue;
        it->creating = false;
        if (failure.kind == Failure::Kind::Closed)
            it->id.clear();
        fail(key, failure);
    }
}

QString Client::keyOf(const QString &sessionId) const
{
    if (sessionId.isEmpty())
        return {};
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
        if (it->id == sessionId)
            return it.key();
    }
    return {};
}

void Client::onNotification(const QString &method, const QJsonObject &params)
{
    if (method != u"session/update")
        return; // (nothing else is said to a client)
    const QString key = keyOf(params.value(u"sessionId"_s).toString());
    const auto it = m_sessions.find(key);
    if (key.isEmpty() || it == m_sessions.end())
        return;
    const QJsonObject update = params.value(u"update"_s).toObject();
    if (it->info.apply(update)) {
        Q_EMIT sessionChanged(key);
        return;
    }
    if (!it->turn || it->turn->cancelled || !it->turn->prompting)
        return; // (a session's history replayed by session/load, or a cancelled turn's last words)
    TurnState &state = it->turn->state;
    const QString was = state.status;
    const int changed = state.apply(update);
    if (changed & TurnState::Text)
        Q_EMIT progress(key, state.text, state.thoughts);
    if ((changed & TurnState::Status) && state.status != was)
        Q_EMIT status(key, state.status);
    if (changed & (TurnState::Activity | TurnState::Status))
        Q_EMIT activityChanged(key);
}

void Client::onRequest(const QJsonValue &id, const QString &method, const QJsonObject &params)
{
    const QString key = keyOf(params.value(u"sessionId"_s).toString());
    const auto it = m_sessions.find(key);
    if (method.startsWith(u"fs/"_s) || method.startsWith(u"terminal/"_s)) {
        const bool offered = method.startsWith(u"fs/"_s) ? m_capabilities.files : m_capabilities.terminals;
        if (!offered || !m_handler) {
            m_connection.respondError(id, MethodNotFound, u"Method not found: %1"_s.arg(method));
            return;
        }
        if (key.isEmpty() || it == m_sessions.end()) {
            m_connection.respondError(id, InvalidParams, u"Unknown session"_s);
            return;
        }
        m_handler(key, id, method, params);
        return;
    }
    if (method != u"session/request_permission") {
        m_connection.respondError(id, MethodNotFound, u"Method not found: %1"_s.arg(method));
        return;
    }
    if (key.isEmpty() || it == m_sessions.end() || !it->turn || it->turn->cancelled) {
        m_connection.respond(id, cancelledOutcome());
        return;
    }
    const std::optional<Permission> permission = readPermission(params, it->turn->state.titles);
    if (!permission) {
        m_connection.respond(id, cancelledOutcome());
        return;
    }
    it->turn->questions.append({id, *permission});
    if (it->turn->questions.size() == 1)
        Q_EMIT permissionAsked(key, *permission);
}

QString textLines(const QString &text, int line, int limit)
{
    if (line <= 1 && limit <= 0)
        return text;
    qsizetype from = 0;
    for (int n = 1; n < line && from >= 0; ++n) {
        from = text.indexOf(u'\n', from);
        if (from >= 0)
            ++from;
    }
    if (from < 0)
        return {};
    if (limit <= 0)
        return text.mid(from);
    qsizetype end = from;
    for (int n = 0; n < limit && end >= 0; ++n) {
        end = text.indexOf(u'\n', end);
        if (end >= 0)
            ++end;
    }
    return end < 0 ? text.mid(from) : text.mid(from, end - from);
}

QByteArray keepLast(const QByteArray &output, qint64 limit, bool *cut)
{
    if (limit <= 0 || output.size() <= limit) {
        if (cut)
            *cut = false;
        return output;
    }
    qsizetype from = output.size() - limit;
    // (Not in the middle of a character: UTF-8's continuation bytes are 10xxxxxx.)
    while (from < output.size() && (static_cast<unsigned char>(output.at(from)) & 0xC0) == 0x80)
        ++from;
    if (cut)
        *cut = true;
    return output.mid(from);
}

} // namespace brulion::acp
