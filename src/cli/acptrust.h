// Pairing without a code in a tailnet (`acp serve --tailscale`, docs/CLI.md "AI agents for other
// devices" → "In a tailnet"): a device of the computer's own user — or of a user named with
// --tailnet-user — pairs with nothing typed, because tailscaled itself vouches for who is at the
// other end of the connection (its whois; core tailscale/tailscale.h decides on it). The Serve
// header (`Tailscale-User-Login`) is taken only behind Tailscale Serve (--tailscale-serve), from
// loopback, by a server that listens on loopback alone — never from any other peer, who could have
// written it. Anything else needs the code, as without a tailnet.
//
//   TailnetTrust trust(&api, status.login, {.users = {…}, .behindServe = false, .loopbackOnly = false});
//   host.setPairTrust([&trust](const http::Request &r, auto done) { trust.check(r, std::move(done)); });
#pragma once

#include "acphost.h"
#include "http.h"

#include <QString>
#include <QStringList>

#include <functional>

namespace brulion::tailscale {
class LocalApi;
}

namespace brulion::cli {

struct TailnetTrustOptions {
    /// More logins whose devices pair without a code (--tailnet-user).
    QStringList users;
    /// The server sits behind Tailscale Serve (--tailscale-serve).
    bool behindServe = false;
    /// It listens on loopback alone (127.0.0.1, ::1).
    bool loopbackOnly = false;
    /// This computer's MagicDNS name: a connection tailscaled says is this computer's own (any of
    /// its users could have made it) never pairs without a code.
    QString thisNode;
};

class TailnetTrust {
public:
    /// `owner`: the computer's own login in the tailnet (tailscale::Status::login; "" none — then
    /// only `options.users`). `api` is not owned and outlives the checks it answers.
    TailnetTrust(tailscale::LocalApi *api, const QString &owner, TailnetTrustOptions options);

    /// The logins whose devices pair without a code: the owner's first.
    QStringList allowedLogins() const { return m_allowed; }
    /// Judges a pairing request that carries no code: `done` at once (no tailnet peer) or after
    /// tailscaled's whois.
    void check(const http::Request &request, std::function<void(const PairTrust &verdict)> done) const;

private:
    tailscale::LocalApi *m_api = nullptr;
    TailnetTrustOptions m_options;
    QStringList m_allowed;
};

} // namespace brulion::cli
