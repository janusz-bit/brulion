// Pairing without a code in a tailnet (acptrust.h).
#include "acptrust.h"

#include "tailscale/localapi.h"
#include "tailscale/tailscale.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::cli {

TailnetTrust::TailnetTrust(tailscale::LocalApi *api, const QString &owner, TailnetTrustOptions options)
    : m_api(api), m_options(std::move(options))
{
    const auto add = [this](const QString &login) {
        const QString l = login.trimmed();
        if (l.isEmpty() || tailscale::sameLogin(l, tailscale::TaggedLogin)
            || std::ranges::any_of(m_allowed, [&l](const QString &a) { return tailscale::sameLogin(a, l); }))
            return;
        m_allowed.append(l);
    };
    add(owner);
    for (const QString &user : std::as_const(m_options.users))
        add(user);
}

void TailnetTrust::check(const http::Request &request, std::function<void(const PairTrust &)> done) const
{
    tailscale::PeerFacts facts;
    facts.peer = request.peer;
    facts.listensOnLoopbackOnly = m_options.loopbackOnly;
    facts.behindServe = m_options.behindServe;
    const QByteArrayView header(tailscale::ServeLoginHeader.data(), tailscale::ServeLoginHeader.size());
    facts.hasServeHeader = request.headers.has(header);
    // (Serve sets one; two of them — one written by the client — are no identity.)
    if (request.headers.count(header) == 1)
        facts.serveLogin = QString::fromUtf8(request.headers.value(header)).trimmed();
    const tailscale::TrustPlan plan = tailscale::planTrust(facts);
    const QStringList allowed = m_allowed;
    const QString thisNode = m_options.thisNode;
    const auto answer = [done, plan](const tailscale::Judgement &j, const QString &error) {
        PairTrust verdict{j.trusted, j.login, j.node, j.why};
        if (!j.trusted && !error.isEmpty())
            verdict.why += u" (%1)"_s.arg(error);
        if (plan.headerIgnored)
            verdict.why += u"; its Tailscale-User-Login header was ignored (taken only from Tailscale Serve)"_s;
        done(verdict);
    };
    if (!plan.askWhois || !m_api) {
        answer(tailscale::decide(facts, std::nullopt, allowed, thisNode), QString());
        return;
    }
    m_api->whois(request.peer, request.peerPort,
                 [facts, allowed, thisNode, answer](const std::optional<tailscale::Identity> &who,
                                                    const QString &error) {
                     answer(tailscale::decide(facts, who, allowed, thisNode), error);
                 });
}

} // namespace brulion::cli
