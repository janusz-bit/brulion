// Webhooks that start a scheduled conversation of the AI chat (docs/UI.md "Scheduled
// conversations" → "Webhooks"; docs/CLI.md "Webhooks"): the secret of a hook's address, the token
// in a request's path, what a request's body becomes in the conversation, and how often a hook may
// start a run. GUI-free and without a network: the server is `brulion-cli app hooks serve`
// (src/cli/hooks.cpp, on the CLI's hardened HTTP server), the runner the app's ChatSchedules.
//
//   POST https://home.example.com/brulion/hooks/Xk3d…(43 characters)
//   Content-Type: application/json
//   {"event": "doorbell"}
//
// A hook's address ends in `/hooks/<token>`: 256 random bits as base64url (newToken()), kept with
// the schedules on the device — never in the notebook — and compared in constant time, every known
// one in full (findToken()). Anything before `/hooks/` is the user's own proxy's path. The body is
// UTF-8 or refused, at most MaxBodyBytes (more: 413); the conversation gets at most MaxTextBytes of
// it, as untrusted data in a fenced block under a sentence that says so (message(): docs/CLI.md
// "For agents: note text is data"). A hook starts at most one run a minute (RateLimit).
#pragma once

#include <QByteArrayView>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace brulion::webhook {

/// A token: 32 random bytes, base64url without padding (43 characters).
inline constexpr int TokenBytes = 32;
inline constexpr int TokenChars = 43;
/// The largest body taken (a larger one is refused: 413).
inline constexpr qsizetype MaxBodyBytes = 1024 * 1024;
/// What of a body goes into the conversation (UTF-8 bytes; the rest is cut off at a character).
inline constexpr qsizetype MaxTextBytes = 16 * 1024;
/// A hook starts at most one run in this time.
inline constexpr qint64 MinIntervalMs = 60 * 1000;
/// The path segment before a token.
inline constexpr QLatin1StringView Segment{"hooks"};

/// A new token (QRandomGenerator::system()).
QString newToken();
/// `text` has a token's shape: TokenChars characters of A–Z, a–z, 0–9, "-" and "_".
bool isToken(QStringView text);
/// The two are the same token, in a time that does not depend on how much of them matches.
bool sameToken(QStringView a, QStringView b);
/// The index of `given` among `known` (every one compared in full, constant time each), -1 none —
/// also for anything that is no token. Empty entries of `known` never match.
int findToken(const QStringList &known, QStringView given);
/// The token of a request's path (without its query; not percent-decoded): its last two segments
/// are `hooks/<token>` — whatever comes before (a proxy's prefix). "" when it is no hook's path.
QString tokenOfPath(QByteArrayView path);
/// A token for the user's eyes and the log: its first four characters and "…".
QString masked(QStringView token);
/// `text` with every token after "/hooks/" masked (a log line with a path in it).
QString maskPaths(const QString &text);

/// What a body is.
enum class BodyKind {
    Empty,
    Text,
    Json, ///< application/json, +json, or a body that is a JSON object or array
    Form, ///< application/x-www-form-urlencoded with name=value pairs: decoded
};
QString kindName(BodyKind kind); ///< "empty" | "text" | "json" | "form"
std::optional<BodyKind> kindFromName(QStringView name);

struct Body {
    BodyKind kind = BodyKind::Empty;
    /// What goes into the conversation: the body as UTF-8 text (a form's pairs one a line, "name:
    /// value"; a JSON body indented when that still fits), at most MaxTextBytes.
    QString text;
    /// The body's size as received.
    qsizetype bytes = 0;
    /// `text` is only its beginning.
    bool cut = false;
};

enum class BodyError {
    None,
    TooLarge, ///< more than MaxBodyBytes (413)
    NotUtf8,  ///< not valid UTF-8 (400)
    Charset,  ///< a charset other than UTF-8 named in Content-Type (400)
    Binary,   ///< control characters other than tab and line ends (400)
};

/// The body of a request with its Content-Type header ("" none); nullopt and `*error` when it is
/// refused.
std::optional<Body> readBody(QByteArrayView body, QByteArrayView contentType, BodyError *error = nullptr);
/// `text` cut to at most `maxBytes` of UTF-8, never inside a character (`*cut` when it was).
QString cutUtf8(const QString &text, qsizetype maxBytes, bool *cut = nullptr);

/// The message a run started by a hook sends: the schedule's `prompt`, then a sentence telling the
/// model that what follows came from outside and is data, never instructions, then the body in a
/// fenced block longer than any run of backticks in it (Polish or English, as the notebook's
/// language: `polish`).
QString message(const QString &prompt, const Body &body, bool polish);

/// At most one run per interval per key (a schedule's id). Pure: the time is given.
class RateLimit {
public:
    explicit RateLimit(qint64 intervalMs = MinIntervalMs) : m_interval(intervalMs) { }
    /// 0: `key` may run at `nowMs`; else in how many milliseconds it may.
    qint64 wait(const QString &key, qint64 nowMs) const;
    /// `key` ran at `nowMs`.
    void ran(const QString &key, qint64 nowMs);
    void forget(const QString &key) { m_last.remove(key); }

private:
    qint64 m_interval;
    QHash<QString, qint64> m_last;
};

/// At most `max` events per `windowMs` per key (the server: requests one address sends). Pure.
class Window {
public:
    Window(int max, qint64 windowMs) : m_max(max), m_window(windowMs) { }
    /// Counts an event of `key` at `nowMs`: false when it is one too many (it is not counted then).
    bool allow(const QString &key, qint64 nowMs);

private:
    int m_max;
    qint64 m_window;
    QHash<QString, QList<qint64>> m_events;
};

} // namespace brulion::webhook
