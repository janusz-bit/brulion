# The Agent Client Protocol's client (brulion_core_acp), the agent played by the test: lines as a
# pipe delivers them, the handshake, sessions per conversation, the answer put together, questions,
# Stop, signing in, failures, the agent's requests a client does not serve, conversations side by
# side.
brulion_add_test(tst_acp SOURCES tst_acp.cpp LIBS brulion_core_acp)
