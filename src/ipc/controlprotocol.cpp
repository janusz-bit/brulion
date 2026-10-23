#include "controlprotocol.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace brulion::ipc {

QString controlKeyFor(const QString &singleInstanceKey)
{
    return singleInstanceKey + u"-control"_s;
}

QString controlFolderProblem(const QString &key)
{
    if (!QDir::isAbsolutePath(key))
        return u"There is no private runtime folder ($XDG_RUNTIME_DIR): the remote control does not listen in the "
               u"shared temporary folder"_s;
#ifdef Q_OS_UNIX
    const QString folder = QFileInfo(key).absolutePath();
    struct stat st{};
    // lstat: a symbolic link is not followed (its owner decides where it points).
    if (::lstat(QFile::encodeName(folder).constData(), &st) != 0 || !S_ISDIR(st.st_mode))
        return u"%1 is not a folder (a symbolic link is not used for the remote control)"_s.arg(folder);
    if (qint64(st.st_uid) != ownUid())
        return u"%1 belongs to another user: the remote control listens only in the user's own runtime folder"_s.arg(
                folder);
    if (st.st_mode & (S_IWGRP | S_IWOTH))
        return u"Others can write to %1: the remote control listens only in a folder only the user can change"_s.arg(
                folder);
#endif
    return {};
}

qint64 peerUid(qintptr descriptor)
{
#ifdef Q_OS_LINUX
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (descriptor >= 0 && ::getsockopt(int(descriptor), SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0)
        return qint64(credentials.uid);
#else
    Q_UNUSED(descriptor);
#endif
    return -1;
}

qint64 ownUid()
{
#ifdef Q_OS_UNIX
    return qint64(::geteuid());
#else
    return -1;
#endif
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

} // namespace

const QList<MethodSpec> &controlMethods()
{
    static const QList<MethodSpec> methods = [] {
        const QStringList devices{u"mouse"_s, u"touch"_s};
        const QStringList buttons{u"left"_s, u"right"_s, u"middle"_s};
        QList<MethodSpec> out;
        out.append(method(u"hello"_s, u"The protocol version, the app's version and the methods"_s, {}));
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
        return out;
    }();
    return methods;
}

const MethodSpec *findControlMethod(const QString &name)
{
    for (const MethodSpec &m : controlMethods()) {
        if (m.name == name)
            return &m;
    }
    return nullptr;
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
    if (selector) {
        for (const QString &key : {u"text"_s, u"objectName"_s, u"type"_s}) {
            if (object.contains(key) && (!object.value(key).isString() || object.value(key).toString().isEmpty()))
                return u"%1 is a non-empty string"_s.arg(key);
        }
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
        const ParamSpec *p = nullptr;
        for (const ParamSpec &candidate : spec->params) {
            if (candidate.name == it.key())
                p = &candidate;
        }
        if (!p)
            return badParams(request, u"%1 takes no parameter %2"_s.arg(spec->name, it.key()), it.key());
        const QJsonValue v = it.value();
        bool ok = true;
        switch (p->type) {
        case ParamType::String: ok = v.isString(); break;
        case ParamType::Integer: ok = v.isDouble() && std::floor(v.toDouble()) == v.toDouble(); break;
        case ParamType::Number: ok = v.isDouble() && std::isfinite(v.toDouble()); break;
        case ParamType::Bool: ok = v.isBool(); break;
        case ParamType::StringOrObject: ok = v.isString() || v.isObject(); break;
        case ParamType::Modifiers: ok = parseModifiers(v).has_value(); break;
        case ParamType::Target: ok = v.isObject(); break;
        }
        if (!ok)
            return badParams(request, u"%1 is %2"_s.arg(it.key(), typeName(p->type)), it.key());
        if ((p->type == ParamType::Integer || p->type == ParamType::Number)
            && (v.toDouble() < p->min || v.toDouble() > p->max))
            return badParams(request, u"%1 is between %2 and %3"_s.arg(it.key()).arg(p->min).arg(p->max), it.key());
        if (!p->values.isEmpty() && !p->values.contains(v.toString().toLower()))
            return badParams(request, u"%1 is one of: %2"_s.arg(it.key(), p->values.join(u", "_s)), it.key());
        if (p->type == ParamType::Target) {
            const QJsonObject target = v.toObject();
            const QString why = checkTarget(target, true);
            if (!why.isEmpty())
                return badParams(request, it.key() + u": "_s + why, it.key());
            for (auto f = target.constBegin(); f != target.constEnd(); ++f) {
                if (!targetFields().contains(f.key()))
                    return badParams(request, u"%1 has no field %2"_s.arg(it.key(), f.key()), it.key());
            }
        }
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
        QJsonObject item = params;
        item.remove(u"route"_s);
        item.remove(u"idle"_s);
        item.remove(u"gone"_s);
        item.remove(u"ms"_s);
        const QString why = checkTarget(item, false);
        if (!why.isEmpty())
            return badParams(request, why);
        const bool anything =
                params.contains(u"route"_s) || params.value(u"idle"_s).toBool() || targetKind(item) != TargetKind::None;
        if (!anything)
            return badParams(request, u"waitFor needs a condition: route, text, objectName, type, handle or idle"_s);
        if (params.contains(u"gone"_s) && targetKind(item) == TargetKind::None)
            return badParams(request, u"gone goes with an item: text, objectName, type or handle"_s, u"gone"_s);
    }
    if (spec->name == u"key") {
        if (params.value(u"sequence"_s).toString().trimmed().isEmpty())
            return badParams(request, u"sequence names keys, e.g. \"Ctrl+K\" or \"Escape\""_s, u"sequence"_s);
    }
    if (spec->name == u"scroll" && params.value(u"dx"_s).toDouble() == 0 && params.value(u"dy"_s).toDouble() == 0)
        return badParams(request, u"scroll needs dx or dy: pixels, not 0 (positive: down, right)"_s, u"dy"_s);
    if (spec->name == u"type" && params.value(u"text"_s).toString().size() > 20000)
        return badParams(request, u"type takes at most 20000 characters at once"_s, u"text"_s);
    return std::nullopt;
}

std::optional<Qt::KeyboardModifiers> parseModifiers(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
        return Qt::KeyboardModifiers();
    QStringList names;
    if (value.isString()) {
        names = value.toString().split(u'+', Qt::SkipEmptyParts);
    } else if (value.isArray()) {
        for (const QJsonValue &v : value.toArray()) {
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
        const QJsonValue have = route.value(it.key());
        if (it.value().isDouble() && have.isDouble()) {
            if (it.value().toDouble() != have.toDouble())
                return false;
        } else if (have != it.value()) {
            return false;
        }
    }
    return true;
}

} // namespace brulion::ipc
