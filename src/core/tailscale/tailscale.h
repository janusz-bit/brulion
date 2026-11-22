#pragma once
// This computer in its tailnet (Tailscale, or headscale behind the Tailscale client) as `brulion-cli
// acp serve --tailscale` and the app's "Share agents with the phone" see it (docs/CLI.md "AI agents
// for other devices" → "In a tailnet"): what tailscaled says of this computer (GET
// /localapi/v0/status: its MagicDNS name, its addresses, its user) and of the other end of a
// connection (GET /localapi/v0/whois?addr=<ip:port>: that device's user and tags) — the client in
// localapi.h —, and the pure decisions on them:
//
// * which identity a pairing request may be judged by (planTrust()): tailscaled's whois for a
//   connection from a tailnet address, or from loopback (Tailscale Serve proxies from 127.0.0.1,
//   and tailscaled knows whom it proxied); Serve's `Tailscale-User-Login` header only for a
//   connection from loopback to a server that listens on loopback alone and was told it sits behind
//   Serve — any other peer could have written the header itself;
// * whether that identity may pair without a code (judge()): a device of one of the allowed users
//   (by default the computer's own login), never a tagged device (it belongs to no person), never
//   the computer itself (any of its local users could have made that connection).
//
// Pure, single-threaded; nothing here trusts a request by itself.

#include <QByteArrayView>
#include <QHostAddress>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace brulion::tailscale {

/// This computer in its tailnet (GET /localapi/v0/status, `tailscale status --json`).
struct Status {
    QString backendState;          ///< "Running", "Stopped", "NeedsLogin", "Starting" …
    QString dnsName;               ///< "nixos.tail1ea20e.ts.net" (no trailing dot; "" without MagicDNS)
    QString hostName;              ///< "nixos"
    QList<QHostAddress> addresses; ///< its tailnet addresses, IPv4 first
    qint64 userId = 0;
    QString login;       ///< its user's login ("janusz-bit@github"; "" unknown)
    QString displayName; ///< "Janusz Janusz"
    QString tailnet;     ///< CurrentTailnet.Name ("janusz-bit.github")
    bool running() const { return backendState == QLatin1StringView("Running"); }
    /// Its IPv4 tailnet address (null without one).
    QHostAddress ipv4() const;
};

/// The status JSON; nullopt (with `error`) when it is none (no object, no Self).
std::optional<Status> parseStatus(QByteArrayView json, QString *error = nullptr);

/// Who is at the other end of a connection (GET /localapi/v0/whois).
struct Identity {
    QString login;       ///< UserProfile.LoginName ("tagged-devices" for a tagged node)
    QString displayName; ///< UserProfile.DisplayName
    QString node;        ///< the device's name ("pixel-9-pro-1": Node.ComputedName, else its DNS name's first label)
    QString nodeDnsName; ///< "pixel-9-pro-1.tail1ea20e.ts.net" (no trailing dot)
    QStringList tags;    ///< Node.Tags ("tag:server"); a tagged device belongs to no user
    qint64 userId = 0;
};

/// The whois JSON; nullopt (with `error`) when it is none.
std::optional<Identity> parseWhois(QByteArrayView json, QString *error = nullptr);

/// An address of a tailnet: 100.64.0.0/10 (CGNAT, Tailscale's IPv4) or fd7a:115c:a1e0::/48 (its
/// IPv6), also as an IPv4-mapped IPv6 address.
bool isTailnetAddress(const QHostAddress &address);
/// A MagicDNS name: "<device>.<tailnet>.ts.net" (letter case aside, a trailing dot allowed).
bool isTailnetName(const QString &host);
/// A tailnet's host as a URL names it: a MagicDNS name or a tailnet address ("[fd7a:…]" too).
/// Traffic to it goes through the tailnet's WireGuard tunnel, encrypted end to end.
bool isTailnetHost(const QString &host);

/// Logins compare without letter case (they are e-mail-like: "Ala@github" is "ala@github").
bool sameLogin(const QString &a, const QString &b);
/// The login tailscaled gives every tagged device: no person.
inline constexpr QLatin1StringView TaggedLogin{"tagged-devices"};
/// The header Tailscale Serve sets to the login of the user whose device made the request.
inline constexpr QLatin1StringView ServeLoginHeader{"Tailscale-User-Login"};

/// What is known of a pairing request before tailscaled is asked.
struct PeerFacts {
    QHostAddress peer;                  ///< the connection's other end
    bool listensOnLoopbackOnly = false; ///< the server listens on 127.0.0.1 / ::1 alone
    bool behindServe = false;           ///< told it sits behind Tailscale Serve (--tailscale-serve)
    bool hasServeHeader = false;        ///< the request has a Tailscale-User-Login field
    QString serveLogin;                 ///< its value
};

/// How a pairing request's identity may be found.
struct TrustPlan {
    /// Ask tailscaled who is at the peer's address and port: a tailnet address, or loopback (a
    /// connection Tailscale Serve proxied — tailscaled knows its user).
    bool askWhois = false;
    /// When whois knows nobody: take the Serve header's login (loopback peer, a server on
    /// loopback alone, behind Serve, the header there).
    bool serveHeader = false;
    /// The request carries a Serve header that is never taken: from another peer, or a server not
    /// behind Serve or listening beyond loopback (for the log: someone may have forged it).
    bool headerIgnored = false;
};
TrustPlan planTrust(const PeerFacts &facts);

/// The decision on a pairing without a code.
struct Judgement {
    bool trusted = false;
    QString login; ///< whose device it is ("" unknown)
    QString node;  ///< the device's name in the tailnet ("" unknown)
    QString why;   ///< English, for the log and the device: why not (or how it was trusted)
};

/// `identity` may pair without a code: one of `allowedLogins` (sameLogin()), no tags, and not this
/// computer itself (`thisNode`, its MagicDNS name: a connection from this computer — to its own
/// tailnet address, or through its own Serve — is the computer's in tailscaled's eyes, whichever of
/// its users made it).
Judgement judge(const Identity &identity, const QStringList &allowedLogins, const QString &thisNode = QString());
/// The whole decision: `whois` what tailscaled said of the peer (nullopt: nothing — not asked,
/// unknown, or tailscaled not reached), `facts` the request.
Judgement decide(const PeerFacts &facts, const std::optional<Identity> &whois, const QStringList &allowedLogins,
                 const QString &thisNode = QString());

} // namespace brulion::tailscale
