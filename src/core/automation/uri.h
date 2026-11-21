// brulion:// addresses (docs/UI.md "Automation"): what a link on a web page, a launcher, a script,
// Tasker or the browser's protocol handler asks of Brulion, read into a Request — and the other
// way round, a Request written as an address (brulion-cli uri build, "Copy Brulion link"). Pure:
// nothing here reads the clock or touches a notebook. Part of the automation module — see
// automation/automation.h.
//
//   brulion://append?page=Zakupy&heading=Dzi%C5%9B&text=Mleko
//   brulion://task?text=Zadzwoni%C4%87%20do%20mamy&remind=jutro%20o%2018&key=…
//   brulion:open?page=Projekty/Dom            ("//" is optional)
//   brulion://open/Projekty/Dom               (a path after the action: its page)
//   brulion://x-callback-url/append?…         (the x-callback-url spelling)
//   web+brulion://daily?date=wczoraj          (the browser's protocol handler, docs/WEB.md)
//
// The query is read as a form (application/x-www-form-urlencoded, like the browser's share
// target): "+" is a space, %XX are UTF-8 bytes — a literal plus is "%2B". Invalid UTF-8 or a "%"
// without two hex digits makes the address invalid (nothing is guessed for text that is written
// into a note). Keys are case-insensitive, a repeated key is refused (which one would win?).
// Control characters other than a line break and a tab are dropped from values; "\r\n" and "\r"
// become "\n".
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>
#include <utility>

namespace brulion::automation {

/// The longest address read (characters): a whole page of text fits, a megabyte does not.
inline constexpr qsizetype MaxUriLength = 100'000;
/// The longest value of one parameter once decoded (the text to write); longer: TooLong.
inline constexpr qsizetype MaxValueLength = 50'000;
/// The most parameters one address may have.
inline constexpr int MaxParameters = 32;

/// What an address asks for (docs/UI.md "Automation" lists the parameters of each).
enum class Action {
    Open,    ///< a page, a heading of it, a block, a line
    Daily,   ///< the daily note of a day (today by default)
    New,     ///< a new page (optionally from a template)
    Append,  ///< text at the end of a page, a heading's section, a block's children, a daily note
    Prepend, ///< text at the start of the same
    Task,    ///< a task with a due date, a reminder or an alarm (natural-language dates)
    Capture, ///< quick capture into today's note (a date makes a task); no text: the capture dialog
    Search,  ///< the search view with a query
    View,    ///< a view of the app (calendar, tasks, …)
    Run,     ///< a command of an extension approved on this device
    Pair,    ///< pairing with a computer's AI agents (brulion-cli acp pair): the dialog, filled in
};

/// The action's name in addresses ("open", "daily", "new", "append", "prepend", "task", "capture",
/// "search", "view", "run", "pair").
QString actionName(Action action);
/// The action an address names; also the aliases "journal" and "today" (daily), "create" (new),
/// "command" (run). Case-insensitive.
std::optional<Action> actionFromName(QStringView name);
/// Every action, in the order of the documentation.
QList<Action> allActions();

/// Why an address could not be read.
enum class UriError {
    None,
    NotBrulion,    ///< another scheme (not brulion: / web+brulion:)
    TooLong,       ///< over MaxUriLength, a value over MaxValueLength, over MaxParameters
    NoAction,      ///< no action named
    UnknownAction, ///< an action this version does not know
    BadEncoding,   ///< a "%" without two hex digits, invalid UTF-8, an empty key
    Duplicate,     ///< a parameter given twice
};
/// "not_brulion", "too_long", "no_action", "unknown_action", "bad_encoding", "duplicate".
QString uriErrorCode(UriError error);

/// A read address.
struct Request {
    Action action = Action::Open;
    /// Parameters as given (keys lower-case, values decoded), in the address's order — without the
    /// common ones below, which have their own fields.
    QList<std::pair<QString, QString>> params;
    QString vault;    ///< `vault` (also `notebook`): the notebook by name or folder; "" = the open one
    QString key;      ///< `key`: the notebook's automation key (docs/UI.md "Automation")
    QString source;   ///< `x-source`: the caller's name as it gives it (shown, never trusted)
    QString id;       ///< `id`: a caller's request id, echoed in results and callbacks
    QString xSuccess; ///< `x-success`: an address opened after success (only with a valid key)
    QString xError;   ///< `x-error`
    QString xCancel;  ///< `x-cancel`
    QString scheme;   ///< "brulion" or "web+brulion"

    /// The first of `keys` that is given (aliases: value("page", "file", "path")); "" when none.
    QString value(std::initializer_list<QStringView> keys) const;
    bool has(std::initializer_list<QStringView> keys) const;
    /// Keys given that `known` does not list (a typo the caller should hear about).
    QStringList unknownKeys(const QStringList &known) const;
};

struct ParsedUri {
    std::optional<Request> request;
    UriError error = UriError::None;
    QString detail; ///< what was wrong (the action's name, the parameter), English, for logs
};

/// Read `uri` (see the file comment). Leading and trailing whitespace is ignored.
ParsedUri parseUri(QStringView uri);

/// The address starts with "brulion:" or "web+brulion:" (any letter case) — an argument a launch
/// hands over as an address, not as a file.
bool isAutomationUri(QStringView text);

/// `request` written as an address: "brulion://<action>?<params>" with the common parameters
/// after the action's own (vault, then x-source, id, the callbacks, the key last), values
/// percent-encoded as UTF-8 ("%20" for a space, unreserved characters and "/" as they are).
/// parseUri(buildUri(r)) gives r back.
QString buildUri(const Request &request);

/// One value percent-encoded as buildUri() writes it.
QString encodeValue(QStringView value);

/// `uri` with the value of its `key` parameter replaced by "•••" — for logs, the event log and
/// anything shown. Other text is kept as given (no decoding).
QString redactedUri(QStringView uri);

/// `base` (an x-callback address) with `params` added to its query ("?" or "&" as needed, before a
/// "#fragment"), values encoded as encodeValue() does.
QString withQuery(const QString &base, const QList<std::pair<QString, QString>> &params);

} // namespace brulion::automation
