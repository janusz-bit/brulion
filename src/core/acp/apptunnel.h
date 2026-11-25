#pragma once
// The running app's tools for an AI agent on another computer (docs/CLI.md "AI agents for other
// devices" → "The app's live tools"): `brulion-cli acp serve` there gives such an agent's session
// Brulion's MCP server in a mode whose brulion_app_* tools call back into the server (its tunnel:
// a control socket of its own, the remote control's protocol, src/ipc/controlprotocol.h), and the
// server hands each call to the device over the device's stream — a JSON-RPC request of its own
// (`_brulion/app`, an extension method of the Agent Client Protocol) among the agent's messages —,
// which the app answers like its remote control's methods, in process (no socket: Android, and the
// desktop for an agent elsewhere), with a grant it made for that agent (RemoteControl::dispatch()).
//
//   device → host   POST streams {agent, features: ["app"]}   the device answers the app's calls
//                   201 {stream, root, features: ["app"]}     the host tunnels them (an older one
//                                                              names none and never sends them)
//                   session/new | load | resume  params._meta["brulion/app"] = {handle, control, chat}
//                                                — the host takes it out: that session's tools
//   host → device   {"jsonrpc": "2.0", "id": "_brulion/app#…", "method": "_brulion/app",
//                    "params": {handle, scope: "control" | "chat", method, params}}
//   device → host   {"jsonrpc": "2.0", "id": "_brulion/app#…", "result": {ok: true, result}}
//                   {"jsonrpc": "2.0", "id": "_brulion/app#…",
//                    "result": {ok: false, error: {code, message, details?}}}
//
// The device decides what a session gets: its agent's "Let it change the app while it runs"
// (`control`: the live tools; never for a sub-conversation's agent) and the conversation's chat
// grant (`chat`: brulion_app_chat_*); `handle` names those grants of the device — random, of that
// stream alone, no secret (the grants' tokens never leave the device). The host gives the session's
// MCP server grants of its own socket and tells the device which session asks through the handle.
// A screenshot comes back as the picture itself (`png`, base64; the host writes the file its tool
// reads), never as a file of the device. Sizes are bounded both ways; the host waits a limited
// time for an answer and keeps a few calls of a stream in flight at most.
//
// Pure (QtCore): the messages and their checks; acp serve (src/cli/acphost.h) and the app
// (src/app/shell/workspaceagents.h) carry them.

#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QString>

#include <optional>

namespace brulion::acp {

/// The feature a stream's request names when the device answers the app's calls (POST streams
/// {features}; GET info's `features` when the host tunnels them).
inline constexpr QLatin1StringView AppFeature{"app"};
/// The method of a call to the device's app.
inline constexpr QLatin1StringView AppMethod{"_brulion/app"};
/// The key of a session's AppSession in its request's params' `_meta`.
inline constexpr QLatin1StringView AppMetaKey{"brulion/app"};
/// The scopes of a call: the app's live tools, the chat's alone.
inline constexpr QLatin1StringView AppScopeControl{"control"};
inline constexpr QLatin1StringView AppScopeChat{"chat"};
/// The largest call the device takes (the control protocol's request line is 1 MiB at most).
inline constexpr qsizetype MaxAppCallBytes = 1024 * 1024 + 4096;
/// The largest answer the device sends (a screenshot's PNG in base64 fits).
inline constexpr qsizetype MaxAppAnswerBytes = 12 * 1024 * 1024;
/// The largest screenshot sent as a picture (as `brulion-cli mcp` hands one to its agent).
inline constexpr qsizetype MaxAppPngBytes = 8 * 1024 * 1024;
/// The longest handle and method name.
inline constexpr qsizetype MaxAppHandleChars = 64;
inline constexpr qsizetype MaxAppMethodChars = 64;

/// What a session asks of the device's app (its request's `_meta`).
struct AppSession {
    QString handle;       ///< the device's name of the session's grants (letters, digits, "-", "_")
    bool control = false; ///< the live tools (the app driven and changed)
    bool chat = false;    ///< the chat's tools (brulion_app_chat_*: sub-conversations of its conversation)
    bool isEmpty() const { return handle.isEmpty() || (!control && !chat); }
    QJsonObject toJson() const;
    /// nullopt: no object, or a handle that is none (empty, too long, other characters).
    static std::optional<AppSession> fromJson(const QJsonValue &value);
    friend bool operator==(const AppSession &, const AppSession &) = default;
};

/// `params` of a session's request with `session` in its `_meta` (other keys kept).
QJsonObject withAppSession(QJsonObject params, const AppSession &session);
/// The AppSession of a session/new, session/load or session/resume message, taken out of it (its
/// `_meta` left without it, dropped when nothing is left): what the host reads before the agent
/// gets the message. nullopt: none (also a malformed one — taken out all the same).
std::optional<AppSession> takeAppSession(QJsonObject &message);

/// A call to the device's app (the host's request).
struct AppCall {
    QJsonValue id;      ///< the host's (a string or a number)
    QString handle;     ///< AppSession::handle of the session whose tools call
    QString scope;      ///< AppScopeControl or AppScopeChat: which of its grants
    QString method;     ///< a method of the remote control's protocol ("state", "theme", "chatStart" …)
    QJsonObject params; ///< its parameters (checked by the device as a socket's are)
    QJsonObject toMessage() const;
    /// The call of a JSON-RPC request of method AppMethod; nullopt with `*error` when it is
    /// malformed (no id, no handle, another scope, no method, params no object, too large).
    static std::optional<AppCall> fromMessage(const QJsonObject &message, QString *error = nullptr);
};

/// The device's answer to a call: the remote control's, as its socket would give it.
struct AppAnswer {
    bool ok = false;
    QJsonValue result;
    QString code; ///< a control error code ("bad_params", "unauthorized" …)
    QString message;
    QJsonObject details;
    static AppAnswer success(const QJsonValue &result);
    static AppAnswer failure(const QString &code, const QString &message, const QJsonObject &details = {});
    /// {ok: true, result} | {ok: false, error: {code, message, details?}}.
    QJsonObject toJson() const;
    /// The answer in a JSON-RPC response: its result as toJson() gives it; a JSON-RPC error (the
    /// device knows no such method: an older app) or anything else is a failure "failed" saying so.
    static AppAnswer fromResponse(const QJsonObject &response);
};

/// The device's JSON-RPC response to the call `id`: `answer`, or — larger than MaxAppAnswerBytes —
/// a failure "failed" saying it was too large.
QJsonObject appAnswerMessage(const QJsonValue &id, const AppAnswer &answer);

} // namespace brulion::acp
