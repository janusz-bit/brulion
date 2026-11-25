#include "acp/apptunnel.h"

#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace brulion::acp {

namespace {

bool validHandle(const QString &handle)
{
    if (handle.isEmpty() || handle.size() > MaxAppHandleChars)
        return false;
    for (const QChar c : handle) {
        const char16_t u = c.unicode();
        const bool ok = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9') || u == u'-'
                        || u == u'_';
        if (!ok)
            return false;
    }
    return true;
}

/// A method name of the control protocol: ASCII letters, at most MaxAppMethodChars.
bool validMethod(const QString &method)
{
    if (method.isEmpty() || method.size() > MaxAppMethodChars)
        return false;
    for (const QChar c : method) {
        const char16_t u = c.unicode();
        if (!((u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z')))
            return false;
    }
    return true;
}

qsizetype compactSize(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact).size();
}

} // namespace

QJsonObject AppSession::toJson() const
{
    return {{u"handle"_s, handle}, {u"control"_s, control}, {u"chat"_s, chat}};
}

std::optional<AppSession> AppSession::fromJson(const QJsonValue &value)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject o = value.toObject();
    AppSession s;
    s.handle = o.value(u"handle"_s).toString();
    if (!validHandle(s.handle))
        return std::nullopt;
    // (Only a true boolean asks: anything else is no.)
    s.control = o.value(u"control"_s).toBool(false);
    s.chat = o.value(u"chat"_s).toBool(false);
    return s;
}

QJsonObject withAppSession(QJsonObject params, const AppSession &session)
{
    QJsonObject meta = params.value(u"_meta"_s).toObject();
    meta.insert(AppMetaKey, session.toJson());
    params.insert(u"_meta"_s, meta);
    return params;
}

std::optional<AppSession> takeAppSession(QJsonObject &message)
{
    const QString method = message.value(u"method"_s).toString();
    if (method != u"session/new" && method != u"session/load" && method != u"session/resume")
        return std::nullopt;
    QJsonObject params = message.value(u"params"_s).toObject();
    if (!params.value(u"_meta"_s).isObject())
        return std::nullopt;
    QJsonObject meta = params.value(u"_meta"_s).toObject();
    if (!meta.contains(AppMetaKey))
        return std::nullopt;
    const std::optional<AppSession> session = AppSession::fromJson(meta.take(AppMetaKey));
    if (meta.isEmpty())
        params.remove(u"_meta"_s);
    else
        params.insert(u"_meta"_s, meta);
    message.insert(u"params"_s, params);
    return session;
}

QJsonObject AppCall::toMessage() const
{
    QJsonObject p{{u"handle"_s, handle}, {u"scope"_s, scope}, {u"method"_s, method}};
    if (!params.isEmpty())
        p.insert(u"params"_s, params);
    return {{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"method"_s, AppMethod}, {u"params"_s, p}};
}

std::optional<AppCall> AppCall::fromMessage(const QJsonObject &message, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (message.value(u"method"_s).toString() != AppMethod)
        return fail(u"not a call of the app"_s);
    const QJsonValue id = message.value(u"id"_s);
    if (!id.isString() && !id.isDouble())
        return fail(u"the call has no id"_s);
    if (compactSize(message) > MaxAppCallBytes)
        return fail(u"the call is larger than %1 bytes"_s.arg(MaxAppCallBytes));
    const QJsonValue paramsValue = message.value(u"params"_s);
    if (!paramsValue.isObject())
        return fail(u"the call has no params"_s);
    const QJsonObject p = paramsValue.toObject();
    AppCall call;
    call.id = id;
    call.handle = p.value(u"handle"_s).toString();
    if (!validHandle(call.handle))
        return fail(u"the call names no session (handle)"_s);
    call.scope = p.value(u"scope"_s).toString();
    if (call.scope != AppScopeControl && call.scope != AppScopeChat)
        return fail(u"the call's scope is neither control nor chat"_s);
    call.method = p.value(u"method"_s).toString();
    if (!validMethod(call.method))
        return fail(u"the call names no method of the app"_s);
    const QJsonValue inner = p.value(u"params"_s);
    if (!inner.isUndefined() && !inner.isNull() && !inner.isObject())
        return fail(u"the call's params are no object"_s);
    call.params = inner.toObject();
    return call;
}

AppAnswer AppAnswer::success(const QJsonValue &result)
{
    AppAnswer a;
    a.ok = true;
    a.result = result.isUndefined() ? QJsonValue(QJsonObject()) : result;
    return a;
}

AppAnswer AppAnswer::failure(const QString &code, const QString &message, const QJsonObject &details)
{
    AppAnswer a;
    a.code = code.isEmpty() ? u"failed"_s : code;
    a.message = message;
    a.details = details;
    return a;
}

QJsonObject AppAnswer::toJson() const
{
    if (ok)
        return {{u"ok"_s, true}, {u"result"_s, result}};
    QJsonObject error{{u"code"_s, code}, {u"message"_s, message}};
    if (!details.isEmpty())
        error.insert(u"details"_s, details);
    return {{u"ok"_s, false}, {u"error"_s, error}};
}

AppAnswer AppAnswer::fromResponse(const QJsonObject &response)
{
    if (response.contains(u"error"_s)) {
        const QJsonObject e = response.value(u"error"_s).toObject();
        return failure(u"failed"_s, u"The app on the device did not take the call (%1): %2"_s.arg(
                                            QString::number(e.value(u"code"_s).toInt()),
                                            e.value(u"message"_s).toString().left(500)));
    }
    const QJsonObject r = response.value(u"result"_s).toObject();
    const QJsonValue ok = r.value(u"ok"_s);
    if (ok.isBool() && ok.toBool() && r.contains(u"result"_s))
        return success(r.value(u"result"_s));
    if (ok.isBool() && !ok.toBool() && r.value(u"error"_s).isObject()) {
        const QJsonObject e = r.value(u"error"_s).toObject();
        const QString code = e.value(u"code"_s).toString();
        if (!code.isEmpty() && code.size() <= MaxAppMethodChars)
            return failure(code, e.value(u"message"_s).toString(), e.value(u"details"_s).toObject());
    }
    return failure(u"failed"_s, u"The app on the device gave an answer that is none"_s);
}

QJsonObject appAnswerMessage(const QJsonValue &id, const AppAnswer &answer)
{
    QJsonObject message{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"result"_s, answer.toJson()}};
    if (compactSize(message) <= MaxAppAnswerBytes)
        return message;
    const AppAnswer tooLarge = AppAnswer::failure(
            u"failed"_s, u"The app's answer is larger than %1 MiB: ask for less"_s.arg(MaxAppAnswerBytes >> 20));
    return {{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"result"_s, tooLarge.toJson()}};
}

} // namespace brulion::acp
