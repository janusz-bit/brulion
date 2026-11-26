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
# A computer's agents offered in the notebook's config.json (hostoffer.h): only tailnet addresses,
# names cut, odd ids dropped, the limits; the same computer replaced, the other keys kept.
brulion_add_test(tst_acp_hostoffer SOURCES tst_acp_hostoffer.cpp LIBS brulion_core_acp)
# The app's live tools for an agent on another computer (apptunnel.h): a session's request for them
# in `_meta`, the calls to the device's app and its answers (malformed ones refused, sizes bounded),
# extension methods handed to the client's handler.
brulion_add_test(tst_acp_apptunnel SOURCES tst_acp_apptunnel.cpp LIBS brulion_core_acp)
# Code mode's threads on another computer (codehost.h): ids and worktrees' names, the projects file,
# what a thread's session names in `_meta` (taken out, malformed ones too), a turn's checkpoint as a
# notification and in the turn kept, the client handing an extension's notification on.
brulion_add_test(tst_acp_codehost SOURCES tst_acp_codehost.cpp LIBS brulion_core_acp)
