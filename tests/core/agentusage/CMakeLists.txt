# Tests of the agentusage module (brulion_core_agentusage): Claude Code's and Codex's session log
# lines (synthetic, in the agents' shapes: a streamed message logged three times, cumulative Codex
# totals, a fork's copies, malformed and huge lines, a line still being written), the price table,
# the folders read with the incremental cache (only appended bytes read; a file shrunk, rewritten,
# gone; a malformed cache rebuilt), local days across DST and a +05:45 zone, the reports, and the
# reader's five-minute rhythm off the calling thread.
brulion_add_test(tst_agentusage SOURCES tst_agentusage.cpp LIBS brulion_core_agentusage)
