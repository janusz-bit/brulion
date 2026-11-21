# The Agent Client Protocol's client (brulion_core_acp), the agent played by the test: lines as a
# pipe delivers them, the handshake, sessions per conversation, the answer put together, questions,
# Stop, signing in, failures, the agent's requests a client does not serve, conversations side by
# side.
brulion_add_test(tst_acp SOURCES tst_acp.cpp LIBS brulion_core_acp)
# The same protocol to an agent on another computer (remote.h) against a host the test plays: the
# host's address and pairing codes as typed, its answers and errors, the stream's batches sent again
# only from what the host lacks, messages in order and never twice, the agent's end, a device
# unpaired, a stream forgotten, a host unreachable for too long, stop(), deletion mid-request.
brulion_add_test(tst_acp_remote SOURCES tst_acp_remote.cpp LIBS brulion_core_acp)
