#pragma once
// A computer's AI agents offered to the notebook's other devices (docs/CLI.md "AI agents for other
// devices" → "A phone finds the computer"): the desktop app that shares its agents ("Share agents
// with the phone", src/app/shell/agentshare.h) writes where it can be reached into the notebook's
// synced settings —
//
//   .brulion/config.json: "agentHosts": [{"url": "http://nixos.tail1ea20e.ts.net:8790",
//                                         "host": "nixos", "agents": [{"id": "claude", "name": "Claude Code"}]}]
//
// — so a phone with the same notebook lists those agents in Settings → AI agents with nothing to
// type. Only an address and names: no token, no code, no command (the notebook comes from anywhere,
// and is no secret). Offered only for an address of a tailnet (tailscale::isTailnetHost(): reached
// only from inside the user's own tailnet, through its WireGuard tunnel); adding one is the user's
// tap, and it pairs without a code only where the computer recognises the phone as its own user's
// (tailscaled's whois there), else the computer's code is asked for. A page or a synced change can
// write any address here: a phone never pairs with one by itself, and shows its address.
//
// Pure functions over the config's JSON object (unknown keys kept, as vault/vaultconfig.h does).

#include "acp/remote.h"

#include <QJsonObject>
#include <QLatin1StringView>
#include <QList>
#include <QString>

namespace brulion::acp {

/// A computer that offers its agents in the notebook.
struct HostOffer {
    QString url;               ///< its address as normalizeRemoteBase() makes it
    QString host;              ///< the computer's name ("nixos")
    QList<RemoteAgent> agents; ///< {id, name}; never a command
    friend bool operator==(const HostOffer &, const HostOffer &) = default;
};

/// The key of config.json.
inline constexpr QLatin1StringView HostOffersKey{"agentHosts"};
/// The most computers, and agents of one, read from a notebook.
inline constexpr int MaxHostOffers = 8;
inline constexpr int MaxOfferedAgents = 16;

/// The offers of a config.json object, the valid ones only: an http(s) address of a tailnet host
/// (no user info, query or fragment), a name, agents with an id of letters, digits, "-" and "_"
/// (a name each, one line, at most 80 characters); a computer named twice once.
QList<HostOffer> hostOffers(const QJsonObject &config);
/// `config` with `offer` in the place of the one of the same computer (sameHost()), else first.
QJsonObject withHostOffer(const QJsonObject &config, const HostOffer &offer);
/// `config` without the offers of the computer at `url` (sameHost()); the key gone with the last.
QJsonObject withoutHostOffer(const QJsonObject &config, const QString &url);
/// Two addresses name one computer: the same host, letter case aside (any port or scheme: it moved).
bool sameHost(const QString &a, const QString &b);

} // namespace brulion::acp
