#include "automation/uri.h"

#include <QByteArray>
#include <QStringDecoder>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace brulion::automation {

namespace {

struct ActionName {
    Action action;
    QStringView name;
};

constexpr std::array<ActionName, 12> kActions{{{Action::Open, u"open"},
                                               {Action::Daily, u"daily"},
                                               {Action::New, u"new"},
                                               {Action::Append, u"append"},
                                               {Action::Prepend, u"prepend"},
                                               {Action::Task, u"task"},
                                               {Action::Capture, u"capture"},
                                               {Action::Search, u"search"},
                                               {Action::View, u"view"},
                                               {Action::Run, u"run"},
                                               {Action::Pair, u"pair"},
                                               {Action::Chat, u"chat"}}};

constexpr std::array<ActionName, 4> kAliases{{{Action::Daily, u"journal"},
                                              {Action::Daily, u"today"},
                                              {Action::New, u"create"},
                                              {Action::Run, u"command"}}};

constexpr QStringView kSchemes[] = {u"web+brulion:", u"brulion:"};

int hexValue(char16_t c)
{
    if (c >= u'0' && c <= u'9')
        return c - u'0';
    if (c >= u'a' && c <= u'f')
        return c - u'a' + 10;
    if (c >= u'A' && c <= u'F')
        return c - u'A' + 10;
    return -1;
}

/// The scheme's length (with its ':') when `text` starts with one of ours, else 0.
qsizetype schemeLength(QStringView text)
{
    for (QStringView scheme : kSchemes) {
        if (text.startsWith(scheme, Qt::CaseInsensitive))
            return scheme.size();
    }
    return 0;
}

/// One component percent-decoded: its characters as UTF-8 bytes with every %XX replaced by its
/// byte, then read as UTF-8. nullopt for a "%" without two hex digits or invalid UTF-8.
std::optional<QString> decodeComponent(QStringView text, bool plusIsSpace)
{
    QByteArray bytes;
    bytes.reserve(text.size());
    const QByteArray raw = text.toUtf8();
    for (qsizetype i = 0; i < raw.size(); ++i) {
        const char c = raw.at(i);
        if (c == '%') {
            if (i + 2 >= raw.size())
                return std::nullopt;
            const int hi = hexValue(char16_t(uchar(raw.at(i + 1))));
            const int lo = hexValue(char16_t(uchar(raw.at(i + 2))));
            if (hi < 0 || lo < 0)
                return std::nullopt;
            bytes.append(char(hi * 16 + lo));
            i += 2;
        } else if (c == '+' && plusIsSpace) {
            bytes.append(' ');
        } else {
            bytes.append(c);
        }
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString out = decoder.decode(bytes);
    if (decoder.hasError())
        return std::nullopt;
    if (out.startsWith(QChar(0xFEFF)))
        out.remove(0, 1);
    return out;
}

/// A decoded value as it is kept: line breaks as "\n", no control characters but "\n" and "\t".
QString cleanValue(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c == u'\r') {
            out.append(u'\n');
            if (i + 1 < value.size() && value.at(i + 1) == u'\n')
                ++i;
        } else if (c == u'\n' || c == u'\t') {
            out.append(c);
        } else if (c.unicode() < 0x20 || c.unicode() == 0x7f || (c.unicode() >= 0x80 && c.unicode() < 0xa0)) {
            continue; // other control characters (C0, DEL, C1)
        } else {
            out.append(c);
        }
    }
    return out;
}

/// The key a common parameter is kept under (its aliases folded), "" for an action's own one.
QString commonKey(const QString &key)
{
    if (key == u"vault" || key == u"notebook")
        return u"vault"_s;
    if (key == u"key" || key == u"x-source" || key == u"id" || key == u"x-success" || key == u"x-error"
        || key == u"x-cancel")
        return key;
    return {};
}

bool isUnreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'
           || c == '_' || c == '~';
}

} // namespace

QString actionName(Action action)
{
    for (const ActionName &a : kActions) {
        if (a.action == action)
            return a.name.toString();
    }
    return {};
}

std::optional<Action> actionFromName(QStringView name)
{
    const QStringView n = name.trimmed();
    for (const ActionName &a : kActions) {
        if (n.compare(a.name, Qt::CaseInsensitive) == 0)
            return a.action;
    }
    for (const ActionName &a : kAliases) {
        if (n.compare(a.name, Qt::CaseInsensitive) == 0)
            return a.action;
    }
    return std::nullopt;
}

QList<Action> allActions()
{
    QList<Action> out;
    for (const ActionName &a : kActions)
        out.append(a.action);
    return out;
}

QString uriErrorCode(UriError error)
{
    switch (error) {
    case UriError::None: return {};
    case UriError::NotBrulion: return u"not_brulion"_s;
    case UriError::TooLong: return u"too_long"_s;
    case UriError::NoAction: return u"no_action"_s;
    case UriError::UnknownAction: return u"unknown_action"_s;
    case UriError::BadEncoding: return u"bad_encoding"_s;
    case UriError::Duplicate: return u"duplicate"_s;
    }
    return {};
}

QString Request::value(std::initializer_list<QStringView> keys) const
{
    for (QStringView wanted : keys) {
        for (const auto &[k, v] : params) {
            if (k == wanted)
                return v;
        }
    }
    return {};
}

bool Request::has(std::initializer_list<QStringView> keys) const
{
    for (QStringView wanted : keys) {
        if (std::ranges::any_of(params, [wanted](const auto &p) { return p.first == wanted; }))
            return true;
    }
    return false;
}

QStringList Request::unknownKeys(const QStringList &known) const
{
    QStringList out;
    for (const auto &[k, v] : params) {
        if (!known.contains(k) && !out.contains(k))
            out.append(k);
    }
    return out;
}

bool isAutomationUri(QStringView text)
{
    return schemeLength(text.trimmed()) > 0;
}

ParsedUri parseUri(QStringView rawUri)
{
    ParsedUri out;
    const auto fail = [&out](UriError error, const QString &detail = {}) {
        out.request.reset();
        out.error = error;
        out.detail = detail;
        return out;
    };
    const QStringView uri = rawUri.trimmed();
    const qsizetype scheme = schemeLength(uri);
    if (scheme == 0)
        return fail(UriError::NotBrulion);
    if (uri.size() > MaxUriLength)
        return fail(UriError::TooLong, u"the address has %1 characters (at most %2)"_s.arg(uri.size()).arg(MaxUriLength));
    Request request;
    request.scheme = uri.first(scheme - 1).toString().toLower();
    QStringView rest = uri.sliced(scheme);
    while (rest.startsWith(u'/'))
        rest = rest.sliced(1);
    const qsizetype question = rest.indexOf(u'?');
    const QStringView path = question < 0 ? rest : rest.first(question);
    const QStringView query = question < 0 ? QStringView() : rest.sliced(question + 1);

    // The path: the action, then (optionally) a page path after it.
    QList<QStringView> segments;
    for (QStringView s : path.tokenize(u'/', Qt::SkipEmptyParts))
        segments.append(s);
    if (!segments.isEmpty() && segments.first().compare(u"x-callback-url", Qt::CaseInsensitive) == 0)
        segments.removeFirst();
    if (segments.isEmpty())
        return fail(UriError::NoAction);
    const auto actionText = decodeComponent(segments.first(), false);
    if (!actionText)
        return fail(UriError::BadEncoding, u"the action"_s);
    const auto action = actionFromName(*actionText);
    if (!action)
        return fail(actionText->trimmed().isEmpty() ? UriError::NoAction : UriError::UnknownAction,
                    actionText->left(64));
    request.action = *action;
    QStringList tail;
    for (qsizetype i = 1; i < segments.size(); ++i) {
        const auto part = decodeComponent(segments.at(i), false);
        if (!part)
            return fail(UriError::BadEncoding, u"the path"_s);
        tail.append(cleanValue(*part));
    }

    int count = 0;
    QStringList seen;
    for (QStringView pair : query.tokenize(u'&', Qt::SkipEmptyParts)) {
        if (++count > MaxParameters)
            return fail(UriError::TooLong, u"more than %1 parameters"_s.arg(MaxParameters));
        const qsizetype eq = pair.indexOf(u'=');
        const auto key = decodeComponent(eq < 0 ? pair : pair.first(eq), true);
        const auto value = decodeComponent(eq < 0 ? QStringView() : pair.sliced(eq + 1), true);
        if (!key || !value)
            return fail(UriError::BadEncoding, u"a parameter"_s);
        const QString name = cleanValue(*key).trimmed().toLower();
        if (name.isEmpty())
            return fail(UriError::BadEncoding, u"a parameter without a name"_s);
        if (value->size() > MaxValueLength)
            return fail(UriError::TooLong,
                        u"%1 has %2 characters (at most %3)"_s.arg(name.left(64)).arg(value->size()).arg(MaxValueLength));
        const QString common = commonKey(name);
        const QString seenAs = common.isEmpty() ? name : common;
        if (seen.contains(seenAs))
            return fail(UriError::Duplicate, name.left(64));
        seen.append(seenAs);
        const QString v = cleanValue(*value);
        if (common == u"vault")
            request.vault = v.trimmed();
        else if (common == u"key")
            request.key = v.trimmed();
        else if (common == u"x-source")
            request.source = v.trimmed().left(80);
        else if (common == u"id")
            request.id = v.trimmed().left(200);
        else if (common == u"x-success")
            request.xSuccess = v.trimmed();
        else if (common == u"x-error")
            request.xError = v.trimmed();
        else if (common == u"x-cancel")
            request.xCancel = v.trimmed();
        else
            request.params.append({name, v});
    }
    if (!tail.isEmpty()) {
        // brulion://open/Projekty/Dom: the page (a new page's title).
        const QString key = request.action == Action::New ? u"title"_s : u"page"_s;
        if (request.has({key}))
            return fail(UriError::Duplicate, key);
        request.params.prepend({key, tail.join(u'/')});
    }
    out.request = std::move(request);
    return out;
}

QString encodeValue(QStringView value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    const QByteArray bytes = value.toUtf8();
    QString out;
    out.reserve(bytes.size());
    for (const char c : bytes) {
        if (isUnreserved(c) || c == '/') {
            out.append(QLatin1Char(c));
        } else {
            out.append(u'%');
            out.append(QLatin1Char(hex[(uchar(c) >> 4) & 0xf]));
            out.append(QLatin1Char(hex[uchar(c) & 0xf]));
        }
    }
    return out;
}

QString buildUri(const Request &request)
{
    QList<std::pair<QString, QString>> params = request.params;
    const auto add = [&params](const QString &key, const QString &value) {
        if (!value.isEmpty())
            params.append({key, value});
    };
    add(u"vault"_s, request.vault);
    add(u"x-source"_s, request.source);
    add(u"id"_s, request.id);
    add(u"x-success"_s, request.xSuccess);
    add(u"x-error"_s, request.xError);
    add(u"x-cancel"_s, request.xCancel);
    add(u"key"_s, request.key);
    QString out = (request.scheme.isEmpty() ? u"brulion"_s : request.scheme) + u"://"_s + actionName(request.action);
    QChar separator = u'?';
    for (const auto &[key, value] : std::as_const(params)) {
        out += separator + encodeValue(key) + u'=' + encodeValue(value);
        separator = u'&';
    }
    return out;
}

QString redactedUri(QStringView uri)
{
    QString out = uri.toString();
    const qsizetype question = out.indexOf(u'?');
    if (question < 0)
        return out;
    qsizetype start = question + 1;
    while (start < out.size()) {
        qsizetype end = out.indexOf(u'&', start);
        if (end < 0)
            end = out.size();
        const QStringView pair = QStringView(out).sliced(start, end - start);
        const qsizetype eq = pair.indexOf(u'=');
        const QStringView key = eq < 0 ? pair : pair.first(eq);
        if (eq >= 0 && key.trimmed().compare(u"key", Qt::CaseInsensitive) == 0 && eq + 1 < pair.size()) {
            const qsizetype valueStart = start + eq + 1;
            out.replace(valueStart, end - valueStart, u"•••"_s);
            end = valueStart + 3;
        }
        start = end + 1;
    }
    return out;
}

QString withQuery(const QString &base, const QList<std::pair<QString, QString>> &params)
{
    const qsizetype hash = base.indexOf(u'#');
    QString head = hash < 0 ? base : base.left(hash);
    const QString fragment = hash < 0 ? QString() : base.mid(hash);
    for (const auto &[key, value] : params) {
        if (!head.contains(u'?'))
            head += u'?';
        else if (!head.endsWith(u'?') && !head.endsWith(u'&'))
            head += u'&';
        head +=encodeValue(key) + u'=' + encodeValue(value);
    }
    return head + fragment;
}

} // namespace brulion::automation
