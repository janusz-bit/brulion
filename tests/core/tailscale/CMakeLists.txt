# This computer in its tailnet (brulion_core_tailscale): tailscaled's status and whois read, the
# tailnet's addresses and names, the trust decisions of a pairing without a code (whois allowed and
# denied, tagged devices, the Serve header only from loopback to a server behind Serve on loopback
# alone, a forged header refused, the code outside the tailnet), the LocalAPI client against a fake
# tailscaled over a real local socket (tests/common/faketailscaled.h) and the `tailscale` program.
brulion_add_test(tst_tailscale SOURCES tst_tailscale.cpp ${PROJECT_SOURCE_DIR}/tests/common/faketailscaled.h
    LIBS brulion_core_tailscale Qt6::Network)
