# Tests of the agentlimits module (brulion_core_agentlimits): Claude Code's and Codex's sign-in
# files and usage answers (recorded shapes, malformed ones), the requests, and the reader's
# five-minute rhythm per account over a fake transport.
brulion_add_test(tst_agentlimits SOURCES tst_agentlimits.cpp LIBS brulion_core_agentlimits)
