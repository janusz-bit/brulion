# tailscale — this computer in its tailnet (tailscale.h): tailscaled's LocalAPI read over its local
# socket, or the `tailscale` program (localapi.h: GET /localapi/v0/status — the computer's MagicDNS
# name, addresses and user — and /localapi/v0/whois — who is at the other end of a connection), and
# the pure trust decisions of `brulion-cli acp serve --tailscale` on them (a pairing without a code
# for the user's own devices in the tailnet; docs/CLI.md "AI agents for other devices").
brulion_add_core_module(tailscale
    SOURCES
        tailscale.h tailscale.cpp
        localapi.h localapi.cpp
    QT Network)
