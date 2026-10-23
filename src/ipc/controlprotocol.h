// The remote control protocol of the running app (desktop; docs/CLI.md "Remote control"): JSON
// lines on a socket of its own next to the single-instance one (controlserver.h), for AI agents
// and scripts through `brulion-cli app …` (controlclient.h). Off unless the app runs with
// --remote-control or the device setting devtools/remoteControl is on.
//
//   request   {"id": 1, "method": "state", "params": {}}       id: a number or a string; params
//                                                                optional
//   response  {"id": 1, "ok": true, "result": {…}}
//             {"id": 1, "ok": false, "error": {"code": "bad_params", "message": "…", "details"?: {…}}}
//
// One request per line (UTF-8, at most MaxLineBytes); several may be in flight on one
// connection and are answered as they finish (waitFor later than the rest): the id ties a
// response to its request. A line that is no request gets a response with the id it had, else
// null. Error codes are stable (ControlError); messages are for people (English).
//
// This file holds what both ends share: parsing and encoding, the methods and their parameters
// (checkRequest() refuses unknown methods, unknown parameters and wrong types before the app runs
// anything), targets ({handle} | {x, y} | {text, objectName, type}), and the checks of who is at
// the other end and where a socket may be (peerUid(), trustedPeer(), socketFolderProblem()), which
// the single-instance socket (singleinstance.h) shares.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace brulion::ipc {

/// The protocol's version (hello: "protocol"); bumped when a method changes incompatibly.
constexpr int ControlProtocolVersion = 1;
/// The longest request line accepted (a longer one closes the connection after an error).
constexpr qsizetype MaxLineBytes = 1024 * 1024;

/// The control socket's name for the single-instance key (SingleInstance::keyFor()): the same
/// folder (the user's runtime folder), "-control" appended.
QString controlKeyFor(const QString &singleInstanceKey);

/// Why a socket of the app must not be at `key` ("" when it may): it lives only in a folder of
/// this user that nobody else can write to (a real folder, not a symbolic link) — never a bare
/// name, which QLocalServer puts into the shared temp folder (no runtime folder,
/// $XDG_RUNTIME_DIR), where another user could take the name first. `what` names the socket in
/// the reason ("the remote control", "the single-instance socket").
QString socketFolderProblem(const QString &key, const QString &what);
/// socketFolderProblem() of the control socket.
QString controlFolderProblem(const QString &key);

/// The uid of the process at the other end of the connected local socket `descriptor` (Linux:
/// SO_PEERCRED); -1 when it cannot be told. The servers refuse other users' connections, the
/// clients a socket another user listens on (trustedPeer()).
qint64 peerUid(qintptr descriptor);
/// This process's effective uid; -1 where uids are not known (not Unix).
qint64 ownUid();
/// May the process at the other end of a connection (`uid`: peerUid()) talk with this user's
/// (`own`: ownUid())? On Linux only the same uid — credentials that cannot be read count as a
/// stranger's; elsewhere uids are not known and the socket file's permissions decide.
bool trustedPeer(qint64 uid, qint64 own);
/// Why a client must not talk to the process listening at `key` (`uid`: peerUid() of the
/// connection), "" when it may (trustedPeer()).
QString listenerProblem(qint64 uid, const QString &key);

struct ControlRequest {
    QJsonValue id; ///< a number or a string (null when the line had none)
    QString method;
    QJsonObject params;
};

/// Stable error codes.
namespace ControlError {
inline constexpr auto ParseError = "parse_error";         ///< the line is no JSON
inline constexpr auto InvalidRequest = "invalid_request"; ///< no object, no method, a bad id or params
inline constexpr auto UnknownMethod = "unknown_method";
inline constexpr auto BadParams = "bad_params";     ///< unknown parameter, wrong type, missing one
inline constexpr auto NotReady = "not_ready";       ///< no notebook open, no window yet
inline constexpr auto NotFound = "not_found";       ///< no such item, page, block, command
inline constexpr auto StaleHandle = "stale_handle"; ///< the item of a handle is gone
inline constexpr auto Ambiguous = "ambiguous_ref";  ///< several pages have that name
inline constexpr auto Refused = "refused";          ///< a disabled command, another notebook
inline constexpr auto Timeout = "timeout";          ///< waitFor's condition was not met in time
inline constexpr auto Failed = "failed";            ///< could not be done (a screenshot not saved)
} // namespace ControlError

struct ControlFailure {
    QJsonValue id;
    QString code;
    QString message;
    QJsonObject details;
};

/// Parse one request line (without the line break). A failure carries the request's id when it
/// had one (parse_error / invalid_request).
std::optional<ControlRequest> parseRequest(const QByteArray &line, ControlFailure *failure);

QByteArray encodeRequest(const QJsonValue &id, const QString &method, const QJsonObject &params = {});
/// A response line (without the line break).
QByteArray encodeResult(const QJsonValue &id, const QJsonValue &result);
QByteArray encodeError(const QJsonValue &id, const QString &code, const QString &message,
                       const QJsonObject &details = {});
inline QByteArray encodeError(const ControlFailure &failure)
{
    return encodeError(failure.id, failure.code, failure.message, failure.details);
}

// ---- Methods ------------------------------------------------------------------------------------

enum class ParamType {
    String,
    Integer,
    Number,
    Bool,
    StringOrObject, ///< navigate's route: a view name or a route object
    Modifiers,      ///< "ctrl+shift" or ["ctrl", "shift"] (parseModifiers())
    Target,         ///< drag's from/to: a target object ({handle} | {x, y} | {text, …})
};

struct ParamSpec {
    QString name;
    ParamType type = ParamType::String;
    bool required = false;
    QStringList values; ///< String: one of these (lower case)
    double min = -1e9;  ///< Integer, Number
    double max = 1e9;
};

struct MethodSpec {
    QString name;
    QList<ParamSpec> params;
    /// Takes a target at the top level (click …); `targetRequired`: one must be given.
    bool target = false;
    bool targetRequired = false;
    QString summary; ///< one line, English
};

/// Every method of the protocol, in documentation order.
const QList<MethodSpec> &controlMethods();
const MethodSpec *findControlMethod(const QString &name);

/// Check a request before running it: the method exists, its parameters are known, of the right
/// type and present when required, a target is well formed (see checkTarget()). nullopt = fine.
std::optional<ControlFailure> checkRequest(const ControlRequest &request);

/// What a target object names, after checkTarget() accepted it.
enum class TargetKind { None, Handle, Point, Selector };
TargetKind targetKind(const QJsonObject &object);
/// A target is {handle}, or {x, y} (window coordinates), or a selector with at least one of
/// text, objectName, type (plus exact); never two kinds at once. "" when fine, else why not
/// (`required`: an empty object is refused too).
QString checkTarget(const QJsonObject &object, bool required);

/// Modifier names ("ctrl", "shift", "alt", "meta"; any case; a list or "ctrl+shift") as Qt's
/// flags; nullopt for an unknown name. Undefined / null: none.
std::optional<Qt::KeyboardModifiers> parseModifiers(const QJsonValue &value);
/// "left" (also ""), "right", "middle"; nullopt otherwise.
std::optional<Qt::MouseButton> parseMouseButton(const QString &name);

/// Does `route` (the app's current route) show what `wanted` asks for: every key of `wanted`
/// equal ("view" alone, or with "path", "date" …; numbers compared as numbers)? A string is the
/// view name.
bool routeMatches(const QJsonObject &route, const QJsonValue &wanted);

} // namespace brulion::ipc
