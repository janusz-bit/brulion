#include "controlprotocol.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace Qt::StringLiterals;

namespace brulion::ipc {

QString controlKeyFor(const QString &singleInstanceKey)
{
    return singleInstanceKey + u"-control"_s;
}

QString socketFolderProblem(const QString &key, const QString &what)
{
    if (!QDir::isAbsolutePath(key))
        return u"There is no private runtime folder ($XDG_RUNTIME_DIR): %1 does not listen in the shared temporary "
               u"folder"_s.arg(what);
#ifdef Q_OS_UNIX
    const QString folder = QFileInfo(key).absolutePath();
    struct stat st{};
    // lstat: a symbolic link is not followed (its owner decides where it points).
    if (::lstat(QFile::encodeName(folder).constData(), &st) != 0 || !S_ISDIR(st.st_mode))
        return u"%1 is not a folder (a symbolic link is not used for %2)"_s.arg(folder, what);
    if (qint64(st.st_uid) != ownUid())
        return u"%1 belongs to another user: %2 listens only in the user's own runtime folder"_s.arg(folder, what);
    if (st.st_mode & (S_IWGRP | S_IWOTH))
        return u"Others can write to %1: %2 listens only in a folder only the user can change"_s.arg(folder, what);
#endif
    return {};
}

QString controlFolderProblem(const QString &key)
{
    return socketFolderProblem(key, u"the remote control"_s);
}

#ifdef Q_OS_WIN
namespace {

/// The user (SID, as bytes) of a process's access token; empty when it cannot be read.
QByteArray processUserSid(HANDLE process)
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(process, TOKEN_QUERY, &token))
        return {};
    QByteArray sid;
    DWORD size = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray buffer(qsizetype(size), Qt::Uninitialized);
    if (size > 0 && ::GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        const auto *user = reinterpret_cast<const TOKEN_USER *>(buffer.constData());
        if (::IsValidSid(user->User.Sid))
            sid = QByteArray(static_cast<const char *>(user->User.Sid), qsizetype(::GetLengthSid(user->User.Sid)));
    }
    ::CloseHandle(token);
    return sid;
}

const QByteArray &ownSid()
{
    static const QByteArray sid = processUserSid(::GetCurrentProcess());
    return sid;
}

/// The relative id (the last part, 1001 in S-1-5-21-…-1001) of a SID: the "uid" in messages.
qint64 relativeId(QByteArray sid)
{
    if (sid.isEmpty())
        return -1;
    const PSID p = sid.data();
    const UCHAR count = *::GetSidSubAuthorityCount(p);
    return count > 0 ? qint64(*::GetSidSubAuthority(p, count - 1)) : -1;
}

} // namespace
#endif

qint64 peerUid(qintptr descriptor)
{
#if defined(Q_OS_LINUX)
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (descriptor >= 0 && ::getsockopt(int(descriptor), SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0)
        return qint64(credentials.uid);
#elif defined(Q_OS_WIN)
    // A named pipe knows the processes at both ends: the one that is not this process, then the
    // user of its token. The same user (SID) is ownUid(); another one is its relative id, or -1
    // when that equals this user's (another domain's user of the same number) or nothing can be
    // read (another user's process cannot be opened).
    if (descriptor == -1 || descriptor == 0)
        return -1;
    const auto pipe = reinterpret_cast<HANDLE>(descriptor);
    ULONG server = 0;
    ULONG client = 0;
    if (!::GetNamedPipeServerProcessId(pipe, &server) || !::GetNamedPipeClientProcessId(pipe, &client))
        return -1;
    const ULONG self = ::GetCurrentProcessId();
    const ULONG peer = server == self ? client : server;
    if (peer == self)
        return ownUid();
    const HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, peer);
    if (!process)
        return -1;
    const QByteArray sid = processUserSid(process);
    ::CloseHandle(process);
    if (sid.isEmpty())
        return -1;
    if (sid == ownSid())
        return ownUid();
    const qint64 rid = relativeId(sid);
    return rid == ownUid() ? -1 : rid;
#else
    Q_UNUSED(descriptor);
#endif
    return -1;
}

qint64 ownUid()
{
#if defined(Q_OS_UNIX)
    return qint64(::geteuid());
#elif defined(Q_OS_WIN)
    return relativeId(ownSid());
#else
    return -1;
#endif
}

bool trustedPeer(qint64 uid, qint64 own)
{
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    return uid >= 0 && uid == own;
#else
    return uid < 0 || own < 0 || uid == own;
#endif
}

QString listenerProblem(qint64 uid, const QString &key)
{
    if (trustedPeer(uid, ownUid()))
        return {};
    return uid < 0 ? u"Could not tell who listens at %1"_s.arg(key)
                   : u"Another user (uid %1) listens at %2: not this user's Brulion"_s.arg(uid).arg(key);
}

std::unique_ptr<QLocalSocket> connectToTrusted(const QString &key, int timeoutMs, QString *error)
{
    error->clear();
    auto socket = std::make_unique<QLocalSocket>();
    socket->connectToServer(key);
    if (!socket->waitForConnected(timeoutMs))
        return nullptr;
    // A socket another user created at that name (the shared temp folder when there is no runtime
    // folder) gets nothing.
    *error = listenerProblem(peerUid(socket->socketDescriptor()), key);
    if (!error->isEmpty()) {
        socket->abort();
        return nullptr;
    }
    return socket;
}

std::optional<ControlRequest> parseRequest(const QByteArray &rawLine, ControlFailure *failure)
{
    const auto fail = [failure](const QJsonValue &id, const char *code, const QString &message) {
        if (failure)
            *failure = ControlFailure{id, QString::fromLatin1(code), message, {}};
        return std::nullopt;
    };
    if (rawLine.size() > MaxLineBytes)
        return fail(QJsonValue(), ControlError::InvalidRequest,
                    u"The request is longer than %1 bytes"_s.arg(MaxLineBytes));
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(rawLine.trimmed(), &error);
    if (error.error != QJsonParseError::NoError)
        return fail(QJsonValue(), ControlError::ParseError, u"Not JSON: %1"_s.arg(error.errorString()));
    if (!doc.isObject())
        return fail(QJsonValue(), ControlError::InvalidRequest, u"A request is one JSON object"_s);
    const QJsonObject o = doc.object();
    ControlRequest request;
    const QJsonValue id = o.value(u"id"_s);
    if (!id.isUndefined() && !id.isNull() && !id.isString() && !id.isDouble())
        return fail(QJsonValue(), ControlError::InvalidRequest, u"The id is a number or a string"_s);
    request.id = id.isUndefined() ? QJsonValue() : id;
    const QJsonValue method = o.value(u"method"_s);
    if (!method.isString() || method.toString().isEmpty())
        return fail(request.id, ControlError::InvalidRequest, u"The request has no method"_s);
    request.method = method.toString();
    const QJsonValue params = o.value(u"params"_s);
    if (!params.isUndefined() && !params.isNull() && !params.isObject())
        return fail(request.id, ControlError::InvalidRequest, u"params is an object"_s);
    request.params = params.toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (it.key() != u"id" && it.key() != u"method" && it.key() != u"params" && it.key() != u"jsonrpc")
            return fail(request.id, ControlError::InvalidRequest, u"Unknown request field %1"_s.arg(it.key()));
    }
    return request;
}

QByteArray encodeRequest(const QJsonValue &id, const QString &method, const QJsonObject &params)
{
    QJsonObject o{{u"id"_s, id}, {u"method"_s, method}};
    if (!params.isEmpty())
        o.insert(u"params"_s, params);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray encodeResult(const QJsonValue &id, const QJsonValue &result)
{
    return QJsonDocument(QJsonObject{{u"id"_s, id}, {u"ok"_s, true}, {u"result"_s, result}})
            .toJson(QJsonDocument::Compact);
}

QByteArray encodeError(const QJsonValue &id, const QString &code, const QString &message, const QJsonObject &details)
{
    QJsonObject error{{u"code"_s, code}, {u"message"_s, message}};
    if (!details.isEmpty())
        error.insert(u"details"_s, details);
    return QJsonDocument(QJsonObject{{u"id"_s, id}, {u"ok"_s, false}, {u"error"_s, error}})
            .toJson(QJsonDocument::Compact);
}

// ---- Methods ------------------------------------------------------------------------------------

namespace {

ParamSpec param(const QString &name, ParamType type, bool required = false)
{
    ParamSpec p;
    p.name = name;
    p.type = type;
    p.required = required;
    return p;
}

ParamSpec range(const QString &name, ParamType type, double min, double max, bool required = false)
{
    ParamSpec p = param(name, type, required);
    p.min = min;
    p.max = max;
    return p;
}

ParamSpec oneOf(const QString &name, const QStringList &values)
{
    ParamSpec p = param(name, ParamType::String);
    p.values = values;
    return p;
}

/// The fields of a target at the top level of a request (click …).
const QStringList &targetFields()
{
    static const QStringList fields{u"handle"_s, u"x"_s, u"y"_s, u"text"_s, u"objectName"_s, u"type"_s, u"exact"_s};
    return fields;
}

QList<ParamSpec> targetParams()
{
    return {param(u"handle"_s, ParamType::String),
            range(u"x"_s, ParamType::Number, -100000, 100000),
            range(u"y"_s, ParamType::Number, -100000, 100000),
            param(u"text"_s, ParamType::String),
            param(u"objectName"_s, ParamType::String),
            param(u"type"_s, ParamType::String),
            param(u"exact"_s, ParamType::Bool)};
}

MethodSpec method(const QString &name, const QString &summary, QList<ParamSpec> params, bool target = false,
                  bool targetRequired = false)
{
    MethodSpec m;
    m.name = name;
    m.summary = summary;
    m.params = std::move(params);
    m.target = target;
    m.targetRequired = targetRequired;
    if (target)
        m.params = targetParams() + m.params;
    return m;
}

QString typeName(ParamType type)
{
    switch (type) {
    case ParamType::String: return u"a string"_s;
    case ParamType::Integer: return u"a whole number"_s;
    case ParamType::Number: return u"a number"_s;
    case ParamType::Bool: return u"true or false"_s;
    case ParamType::StringOrObject: return u"a string or an object"_s;
    case ParamType::Modifiers: return u"modifier names (\"ctrl+shift\" or [\"ctrl\", \"shift\"])"_s;
    case ParamType::Target: return u"a target object ({handle}, {x, y} or {text, objectName, type})"_s;
    case ParamType::Json: return u"a JSON value"_s;
    case ParamType::Object: return u"an object"_s;
    case ParamType::StringList: return u"names (\"a, b\" or [\"a\", \"b\"])"_s;
    }
    return {};
}

ControlFailure badParams(const ControlRequest &request, const QString &message, const QString &param = QString())
{
    ControlFailure f{request.id, QString::fromLatin1(ControlError::BadParams), message, {}};
    if (!param.isEmpty())
        f.details.insert(u"param"_s, param);
    return f;
}

/// Why `v` is no valid value of the parameter `p` of `request`; nullopt when it is one.
std::optional<ControlFailure> checkParam(const ControlRequest &request, const ParamSpec &p, const QJsonValue &v)
{
    bool ok = true;
    switch (p.type) {
    case ParamType::String: ok = v.isString(); break;
    case ParamType::Integer: ok = v.isDouble() && std::floor(v.toDouble()) == v.toDouble(); break;
    case ParamType::Number: ok = v.isDouble() && std::isfinite(v.toDouble()); break;
    case ParamType::Bool: ok = v.isBool(); break;
    case ParamType::StringOrObject: ok = v.isString() || v.isObject(); break;
    case ParamType::Modifiers: ok = parseModifiers(v).has_value(); break;
    case ParamType::Target: ok = v.isObject(); break;
    case ParamType::Json: ok = !v.isUndefined(); break;
    case ParamType::Object: ok = v.isObject(); break;
    case ParamType::StringList:
        ok = v.isString()
             || (v.isArray() && std::ranges::all_of(v.toArray(), [](const QJsonValue &x) { return x.isString(); }));
        break;
    }
    if (!ok)
        return badParams(request, u"%1 is %2"_s.arg(p.name, typeName(p.type)), p.name);
    if ((p.type == ParamType::Integer || p.type == ParamType::Number) && (v.toDouble() < p.min || v.toDouble() > p.max))
        return badParams(request, u"%1 is between %2 and %3"_s.arg(p.name).arg(p.min).arg(p.max), p.name);
    if (!p.values.isEmpty() && !p.values.contains(v.toString().toLower()))
        return badParams(request, u"%1 is one of: %2"_s.arg(p.name, p.values.join(u", "_s)), p.name);
    if (p.type == ParamType::Target) {
        const QJsonObject target = v.toObject();
        if (const QString why = checkTarget(target, true); !why.isEmpty())
            return badParams(request, p.name + u": "_s + why, p.name);
        for (auto f = target.constBegin(); f != target.constEnd(); ++f) {
            if (!targetFields().contains(f.key()))
                return badParams(request, u"%1 has no field %2"_s.arg(p.name, f.key()), p.name);
        }
    }
    return std::nullopt;
}

} // namespace

const QList<MethodSpec> &controlMethods()
{
    static const QList<MethodSpec> methods = [] {
        const QStringList devices{u"mouse"_s, u"touch"_s};
        const QStringList buttons{u"left"_s, u"right"_s, u"middle"_s};
        QList<MethodSpec> out;
        out.append(method(u"hello"_s,
                          u"The protocol version, the app's version and the methods; names the client, shows a "
                          u"grant's token"_s,
                          {param(u"client"_s, ParamType::String), param(u"token"_s, ParamType::String)}));
        out.append(method(u"state"_s,
                          u"What the app shows: status, notebook, routes, focus, the edited block, toasts, window"_s,
                          {}));
        out.append(method(u"navigate"_s, u"Show a route ({view, …} or a view name); side: in the side panel"_s,
                          {param(u"route"_s, ParamType::StringOrObject, true), param(u"side"_s, ParamType::Bool)}));
        out.append(method(u"open"_s,
                          u"Open a page, a line, a block or a daily note by a ref as brulion-cli takes it (default: "
                          u"today)"_s,
                          {param(u"ref"_s, ParamType::String), range(u"line"_s, ParamType::Integer, 0, 1e7),
                           param(u"blockId"_s, ParamType::String), param(u"heading"_s, ParamType::String),
                           param(u"side"_s, ParamType::Bool)}));
        out.append(method(u"commands"_s, u"The command palette's commands (id, title, enabled, shortcut)"_s, {}));
        out.append(method(u"command"_s, u"Run a command of the palette by its id"_s,
                          {param(u"id"_s, ParamType::String, true)}));
        out.append(method(u"capture"_s, u"Save a text with quick capture (today's journal; a date makes a task)"_s,
                          {param(u"text"_s, ParamType::String, true)}));
        out.append(method(u"screenshot"_s,
                          u"Save the window as a PNG (path: absolute, .png; default: the runtime folder)"_s,
                          {param(u"path"_s, ParamType::String)}));
        out.append(method(u"find"_s, u"Items by text, objectName or QML type, topmost first, with handles"_s,
                          {param(u"text"_s, ParamType::String), param(u"objectName"_s, ParamType::String),
                           param(u"type"_s, ParamType::String), param(u"exact"_s, ParamType::Bool),
                           param(u"visibleOnly"_s, ParamType::Bool), param(u"within"_s, ParamType::String),
                           range(u"limit"_s, ParamType::Integer, 1, 1000)}));
        out.append(method(
                u"click"_s, u"Click (or tap) a target"_s,
                {oneOf(u"button"_s, buttons), param(u"modifiers"_s, ParamType::Modifiers), oneOf(u"device"_s, devices)},
                true, true));
        out.append(method(u"longPress"_s, u"Press and hold a target"_s,
                          {range(u"ms"_s, ParamType::Integer, 100, 10000), oneOf(u"device"_s, devices)}, true, true));
        out.append(method(
                u"doubleClick"_s, u"Double-click (or double-tap) a target"_s,
                {oneOf(u"button"_s, buttons), param(u"modifiers"_s, ParamType::Modifiers), oneOf(u"device"_s, devices)},
                true, true));
        out.append(method(u"drag"_s, u"Drag from one target to another"_s,
                          {param(u"from"_s, ParamType::Target, true), param(u"to"_s, ParamType::Target, true),
                           range(u"holdMs"_s, ParamType::Integer, 0, 10000), oneOf(u"button"_s, buttons),
                           param(u"modifiers"_s, ParamType::Modifiers), oneOf(u"device"_s, devices)}));
        out.append(method(u"scroll"_s, u"Scroll at a target (default: the window's middle) by dx, dy pixels"_s,
                          {range(u"dx"_s, ParamType::Number, -100000, 100000),
                           range(u"dy"_s, ParamType::Number, -100000, 100000), oneOf(u"device"_s, devices)},
                          true, false));
        out.append(method(u"type"_s, u"Type text with key events into the focused item"_s,
                          {param(u"text"_s, ParamType::String, true)}));
        out.append(
                method(u"key"_s, u"Press keys: \"Ctrl+K\", \"Escape\", \"Shift+Tab, Return\""_s,
                       {param(u"sequence"_s, ParamType::String, true), range(u"count"_s, ParamType::Integer, 1, 100)}));
        out.append(method(u"waitFor"_s,
                          u"Wait until a route shows, an item (text, objectName, type, handle) is shown or gone, or "
                          u"the app is idle"_s,
                          {param(u"route"_s, ParamType::StringOrObject), param(u"text"_s, ParamType::String),
                           param(u"objectName"_s, ParamType::String), param(u"type"_s, ParamType::String),
                           param(u"exact"_s, ParamType::Bool), param(u"handle"_s, ParamType::String),
                           param(u"idle"_s, ParamType::Bool), param(u"gone"_s, ParamType::Bool),
                           range(u"ms"_s, ParamType::Integer, 0, 120000)}));
        out.append(method(u"quit"_s, u"Quit the app"_s, {}));
        // Live changes (docs/CLI.md "Live changes"): code, extensions, the theme, settings and the
        // window's items of the running app, for this run; every change noted and undone by revert.
        out.append(method(u"eval"_s,
                          u"Evaluate Luau in the console's environment (or an extension's): its value and what it "
                          u"printed"_s,
                          {param(u"code"_s, ParamType::String, true), param(u"extension"_s, ParamType::String)}));
        out.append(method(u"extensions"_s, u"Every extension with its state (builtin, page, live), and the live ones"_s,
                          {}));
        out.append(method(u"load"_s,
                          u"Load Luau as a live extension for this run (it replaces a page or built-in of that id "
                          u"while loaded)"_s,
                          {param(u"id"_s, ParamType::String, true), param(u"source"_s, ParamType::String, true),
                           param(u"title"_s, ParamType::String), param(u"permissions"_s, ParamType::StringList)}));
        out.append(method(u"unload"_s, u"Take out a live extension (the page or built-in of its id runs again)"_s,
                          {param(u"id"_s, ParamType::String, true)}));
        out.append(method(u"reload"_s, u"Load every extension again (live ones stay)"_s, {}));
        out.append(method(
                u"console"_s, u"The Luau console's lines after a line number"_s,
                {range(u"since"_s, ParamType::Integer, 0, 1e15), range(u"limit"_s, ParamType::Integer, 1, 1000)}));
        out.append(method(u"settings"_s, u"The device settings a live client may read and change"_s, {}));
        out.append(method(u"set"_s, u"Change a setting (settings lists them)"_s,
                          {param(u"key"_s, ParamType::String, true), param(u"value"_s, ParamType::Json, true)}));
        out.append(method(u"theme"_s,
                          u"The theme's overrides: set this client's tokens, clear them, or the token catalog"_s,
                          {param(u"tokens"_s, ParamType::Object), param(u"clear"_s, ParamType::Bool),
                           param(u"catalog"_s, ParamType::Bool)}));
        out.append(method(u"contributions"_s, u"The items extensions put into the UI (all, or one slot's)"_s,
                          {param(u"slot"_s, ParamType::String)}));
        out.append(method(u"inspect"_s,
                          u"The window's item tree (or an item's) with handles, geometry and optionally properties"_s,
                          {param(u"handle"_s, ParamType::String), range(u"depth"_s, ParamType::Integer, 0, 8),
                           param(u"properties"_s, ParamType::Bool), range(u"limit"_s, ParamType::Integer, 1, 2000)}));
        out.append(method(u"setProperty"_s,
                          u"Write a property of an item (its binding is removed; revert writes the old value)"_s,
                          {param(u"handle"_s, ParamType::String, true), param(u"name"_s, ParamType::String, true),
                           param(u"value"_s, ParamType::Json, true)}));
        out.append(method(
                u"events"_s,
                u"What happened in the app after an event id (routes, toasts, console, live changes, "
                u"notes); waits up to wait ms for one"_s,
                {range(u"since"_s, ParamType::Integer, 0, 1e15), range(u"wait"_s, ParamType::Integer, 0, 30000)}));
        out.append(method(u"changes"_s, u"The live changes not reverted, oldest first"_s, {}));
        out.append(method(u"revert"_s, u"Undo the live changes (of one client, or everyone's)"_s,
                          {param(u"who"_s, ParamType::String)}));
        // The AI chat (docs/CLI.md "Conversations"): what an AI agent's chat grant may call.
        const auto chat = [&out](MethodSpec m) {
            m.chat = true;
            out.append(std::move(m));
        };
        const QStringList destinations{u"new"_s, u"continue"_s};
        chat(method(u"chats"_s,
                    u"The conversations, newest change first: path, name, partner, state, parent (parent: only "
                    u"its sub-conversations)"_s,
                    {param(u"parent"_s, ParamType::String), range(u"limit"_s, ParamType::Integer, 1, 500)}));
        chat(method(u"chatRead"_s,
                    u"A conversation's messages from a position (0 the first), long texts cut with the offset "
                    u"to go on from"_s,
                    {param(u"path"_s, ParamType::String, true), range(u"from"_s, ParamType::Integer, 0, 1e7),
                     range(u"limit"_s, ParamType::Integer, 1, 200), range(u"offset"_s, ParamType::Integer, 0, 1e9)}));
        chat(method(u"chatStart"_s,
                    u"Start a conversation with a partner (\"\" the assistant, acp:<id>, an extension's); its "
                    u"turn runs"_s,
                    {param(u"text"_s, ParamType::String, true), param(u"partner"_s, ParamType::String),
                     param(u"page"_s, ParamType::String), param(u"parent"_s, ParamType::String)}));
        chat(method(u"chatSend"_s, u"Send a message into a conversation whose turn does not run; its turn runs"_s,
                    {param(u"path"_s, ParamType::String, true), param(u"text"_s, ParamType::String, true)}));
        chat(method(u"chatWait"_s,
                    u"Wait up to ms for a conversation's turn to end: its answer, the question it waits on, or "
                    u"why it failed"_s,
                    {param(u"path"_s, ParamType::String, true), range(u"ms"_s, ParamType::Integer, 0, 600000)}));
        chat(method(u"chatStop"_s, u"Stop a conversation's turn (nothing of it is written)"_s,
                    {param(u"path"_s, ParamType::String, true)}));
        chat(method(u"chatPartners"_s, u"Who may answer a conversation here: id, name, kind"_s, {}));
        chat(method(u"chatSchedules"_s, u"The scheduled conversations of the open notebook, with their next run"_s,
                    {}));
        chat(method(u"chatScheduleAdd"_s,
                    u"Schedule a conversation: a prompt sent when the rule says (\"codziennie o 7:30\", "
                    u"\"co 3 godziny\", \"jutro o 8\")"_s,
                    {param(u"prompt"_s, ParamType::String, true), param(u"when"_s, ParamType::String, true),
                     param(u"title"_s, ParamType::String), param(u"partner"_s, ParamType::String),
                     oneOf(u"destination"_s, destinations), param(u"enabled"_s, ParamType::Bool)}));
        chat(method(u"chatScheduleSet"_s, u"Change a scheduled conversation (the fields given)"_s,
                    {param(u"id"_s, ParamType::String, true), param(u"prompt"_s, ParamType::String),
                     param(u"when"_s, ParamType::String), param(u"title"_s, ParamType::String),
                     param(u"partner"_s, ParamType::String), oneOf(u"destination"_s, destinations),
                     param(u"enabled"_s, ParamType::Bool)}));
        chat(method(u"chatScheduleRemove"_s, u"Delete a scheduled conversation"_s,
                    {param(u"id"_s, ParamType::String, true)}));
        chat(method(u"chatScheduleRun"_s, u"Run a scheduled conversation now"_s,
                    {param(u"id"_s, ParamType::String, true)}));
        return out;
    }();
    return methods;
}

const MethodSpec *findControlMethod(const QString &name)
{
    const QList<MethodSpec> &methods = controlMethods();
    const auto it = std::ranges::find(methods, name, &MethodSpec::name);
    return it == methods.cend() ? nullptr : &*it;
}

TargetKind targetKind(const QJsonObject &object)
{
    if (object.contains(u"handle"_s))
        return TargetKind::Handle;
    if (object.contains(u"x"_s) || object.contains(u"y"_s))
        return TargetKind::Point;
    if (object.contains(u"text"_s) || object.contains(u"objectName"_s) || object.contains(u"type"_s))
        return TargetKind::Selector;
    return TargetKind::None;
}

QString checkTarget(const QJsonObject &object, bool required)
{
    const bool handle = object.contains(u"handle"_s);
    const bool point = object.contains(u"x"_s) || object.contains(u"y"_s);
    const bool selector = object.contains(u"text"_s) || object.contains(u"objectName"_s) || object.contains(u"type"_s);
    if (int(handle) + int(point) + int(selector) > 1)
        return u"Give one target: a handle, x and y, or text / objectName / type"_s;
    if (point && !(object.value(u"x"_s).isDouble() && object.value(u"y"_s).isDouble()))
        return u"A point needs both x and y (numbers, window coordinates)"_s;
    if (handle && (!object.value(u"handle"_s).isString() || object.value(u"handle"_s).toString().isEmpty()))
        return u"handle is a string from find"_s;
    for (const QString &key : {u"text"_s, u"objectName"_s, u"type"_s}) {
        if (object.contains(key) && (!object.value(key).isString() || object.value(key).toString().isEmpty()))
            return u"%1 is a non-empty string"_s.arg(key);
    }
    if (object.contains(u"exact"_s) && !selector)
        return u"exact goes with text / objectName / type"_s;
    if (required && !handle && !point && !selector)
        return u"A target is needed: a handle (from find), x and y, or text / objectName / type"_s;
    return {};
}

std::optional<ControlFailure> checkRequest(const ControlRequest &request)
{
    const MethodSpec *spec = findControlMethod(request.method);
    if (!spec) {
        QStringList names;
        for (const MethodSpec &m : controlMethods())
            names.append(m.name);
        return ControlFailure{request.id,
                              QString::fromLatin1(ControlError::UnknownMethod),
                              u"Unknown method %1"_s.arg(request.method),
                              {{u"methods"_s, QJsonArray::fromStringList(names)}}};
    }
    const QJsonObject &params = request.params;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        const auto p = std::ranges::find(spec->params, it.key(), &ParamSpec::name);
        if (p == spec->params.cend())
            return badParams(request, u"%1 takes no parameter %2"_s.arg(spec->name, it.key()), it.key());
        if (auto problem = checkParam(request, *p, it.value()))
            return problem;
    }
    for (const ParamSpec &p : spec->params) {
        if (p.required && !params.contains(p.name))
            return badParams(request, u"%1 needs %2"_s.arg(spec->name, p.name), p.name);
    }
    if (spec->target) {
        const QString why = checkTarget(params, spec->targetRequired);
        if (!why.isEmpty())
            return badParams(request, why);
    }
    if (spec->name == u"find") {
        const QString why = checkTarget(params, false);
        if (targetKind(params) != TargetKind::Selector || !why.isEmpty())
            return badParams(request, why.isEmpty() ? u"find needs text, objectName or type"_s : why);
    }
    if (spec->name == u"waitFor") {
        QJsonObject item = params; // the item's fields: all but route, idle, gone and ms
        for (const QString &key : {u"route"_s, u"idle"_s, u"gone"_s, u"ms"_s})
            item.remove(key);
        if (const QString why = checkTarget(item, false); !why.isEmpty())
            return badParams(request, why);
        const bool hasItem = targetKind(item) != TargetKind::None;
        if (!params.contains(u"route"_s) && !params.value(u"idle"_s).toBool() && !hasItem)
            return badParams(request, u"waitFor needs a condition: route, text, objectName, type, handle or idle"_s);
        if (params.contains(u"gone"_s) && !hasItem)
            return badParams(request, u"gone goes with an item: text, objectName, type or handle"_s, u"gone"_s);
    }
    if (spec->name == u"key" && params.value(u"sequence"_s).toString().trimmed().isEmpty())
        return badParams(request, u"sequence names keys, e.g. \"Ctrl+K\" or \"Escape\""_s, u"sequence"_s);
    if (spec->name == u"scroll" && params.value(u"dx"_s).toDouble() == 0 && params.value(u"dy"_s).toDouble() == 0)
        return badParams(request, u"scroll needs dx or dy: pixels, not 0 (positive: down, right)"_s, u"dy"_s);
    if (spec->name == u"type" && params.value(u"text"_s).toString().size() > 20000)
        return badParams(request, u"type takes at most 20000 characters at once"_s, u"text"_s);
    if (spec->name == u"eval" && params.value(u"code"_s).toString().trimmed().isEmpty())
        return badParams(request, u"eval needs Luau code"_s, u"code"_s);
    if (spec->name == u"load" && params.value(u"source"_s).toString().trimmed().isEmpty())
        return badParams(request, u"load needs the extension's Luau source"_s, u"source"_s);
    if (spec->name == u"theme" && params.contains(u"tokens"_s) && params.value(u"clear"_s).toBool())
        return badParams(request, u"theme takes tokens or clear, not both"_s, u"clear"_s);
    if (spec->name == u"setProperty" && params.value(u"name"_s).toString().trimmed().isEmpty())
        return badParams(request, u"name is a property's name (\"color\", \"font.pixelSize\")"_s, u"name"_s);
    return std::nullopt;
}

QString cleanClientName(const QString &name)
{
    QString out;
    for (const QChar c : name)
        out += (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format) ? u' ' : c;
    return out.simplified().left(MaxClientName).trimmed();
}

QStringList stringList(const QJsonValue &value)
{
    QStringList raw;
    if (value.isString()) {
        raw = value.toString().split(u',');
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &v : array)
            raw.append(v.toString());
    }
    QStringList out;
    for (const QString &name : std::as_const(raw)) {
        if (const QString trimmed = name.trimmed(); !trimmed.isEmpty())
            out.append(trimmed);
    }
    return out;
}

std::optional<Qt::KeyboardModifiers> parseModifiers(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
        return Qt::KeyboardModifiers();
    QStringList names;
    if (value.isString()) {
        names = value.toString().split(u'+', Qt::SkipEmptyParts);
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const auto &v : array) {
            if (!v.isString())
                return std::nullopt;
            names.append(v.toString());
        }
    } else {
        return std::nullopt;
    }
    Qt::KeyboardModifiers out;
    for (const QString &raw : std::as_const(names)) {
        const QString name = raw.trimmed().toLower();
        if (name == u"ctrl" || name == u"control")
            out |= Qt::ControlModifier;
        else if (name == u"shift")
            out |= Qt::ShiftModifier;
        else if (name == u"alt")
            out |= Qt::AltModifier;
        else if (name == u"meta" || name == u"super")
            out |= Qt::MetaModifier;
        else if (!name.isEmpty())
            return std::nullopt;
    }
    return out;
}

std::optional<Qt::MouseButton> parseMouseButton(const QString &name)
{
    const QString n = name.trimmed().toLower();
    if (n.isEmpty() || n == u"left")
        return Qt::LeftButton;
    if (n == u"right")
        return Qt::RightButton;
    if (n == u"middle")
        return Qt::MiddleButton;
    return std::nullopt;
}

bool routeMatches(const QJsonObject &route, const QJsonValue &wanted)
{
    if (wanted.isString()) {
        const QString view = wanted.toString() == u"home" ? u"journals"_s : wanted.toString();
        return route.value(u"view"_s).toString() == view;
    }
    if (!wanted.isObject())
        return false;
    const QJsonObject w = wanted.toObject();
    for (auto it = w.constBegin(); it != w.constEnd(); ++it) {
        const QJsonValue want = it.value();
        const QJsonValue have = route.value(it.key());
        const bool same = (want.isDouble() && have.isDouble()) ? want.toDouble() == have.toDouble() : want == have;
        if (!same)
            return false;
    }
    return true;
}

} // namespace brulion::ipc
