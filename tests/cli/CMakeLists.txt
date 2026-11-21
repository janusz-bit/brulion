# brulion-cli (src/cli). In process (library brulion_cli): the argument parser, the registry, its
# JSON Schema, the help and completion, the diff, and that docs/CLI.md lists every command. As a
# process (tst_cli_process: QProcess on the real brulion-cli with a private home): every command's
# JSON and exit code on temporary notebooks, refusals (read-only, destructive, outside the
# notebook, a missing notebook), Polish text in argv (also LANG=C) and on stdin, two processes
# appending to one journal, brulion:// addresses (uri build, parse, run, open), dry runs, the app's settings left untouched and no reminder state
# written, messages in the system's language (Polish or English; JSON keys, codes, schema and MCP
# English), and the MCP server (initialize, tools/list, tools/call).
brulion_add_test(tst_cli_args SOURCES tst_cli_args.cpp LIBS brulion_cli Qt6::Core)
target_compile_definitions(tst_cli_args PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# The remote MCP server (`mcp serve`): its HTTP parser, secrets, checks and state file in process
# (tst_cli_http), and the whole sign-in flow and MCP over a real local socket with
# QNetworkAccessManager as the client (tst_cli_remote_mcp; HTTPS with tests/data/mcp-remote).
brulion_add_test(tst_cli_http SOURCES tst_cli_http.cpp LIBS brulion_cli Qt6::Network Qt6::Core)
brulion_add_test(tst_cli_remote_mcp SOURCES tst_cli_remote_mcp.cpp LIBS brulion_cli Qt6::Network Qt6::Core)
math(EXPR _remote_timeout "300 * ${BRULION_TEST_SLOWDOWN}")
set_tests_properties(tst_cli_remote_mcp PROPERTIES TIMEOUT ${_remote_timeout})
brulion_add_test(tst_cli_process SOURCES tst_cli_process.cpp LIBS brulion_ipc brulion_core_notebook brulion_core_automation
    brulion_core_attachments Qt6::Network)
target_compile_definitions(tst_cli_process PRIVATE BRULION_CLI="$<TARGET_FILE:brulion-cli>")
add_dependencies(tst_cli_process brulion-cli)
if(TARGET brulion)
    # `brulion cli …`: the app's binary hands over to the command line.
    target_compile_definitions(tst_cli_process PRIVATE BRULION_APP="$<TARGET_FILE:brulion>")
    add_dependencies(tst_cli_process brulion)
endif()
math(EXPR _cli_timeout "120 * ${BRULION_TEST_SLOWDOWN}")
set_tests_properties(tst_cli_process PROPERTIES TIMEOUT ${_cli_timeout})

# The Web Clipper in brulion-cli (docs/CLIPPER.md) as a process: clip save into a page, the journal and a
# heading's section (pictures from a server on 127.0.0.1), the native messaging host's frames, and the
# host manifests installed into the private home.
brulion_add_test(tst_cli_clip SOURCES tst_cli_clip.cpp LIBS brulion_core_clipper Qt6::Network)
target_compile_definitions(tst_cli_clip PRIVATE BRULION_CLI="$<TARGET_FILE:brulion-cli>"
    BRULION_CLIPPER_FIXTURES="${PROJECT_SOURCE_DIR}/tests/core/clipper/fixtures")
add_dependencies(tst_cli_clip brulion-cli)
math(EXPR _cli_clip_timeout "120 * ${BRULION_TEST_SLOWDOWN}")
set_tests_properties(tst_cli_clip PROPERTIES TIMEOUT ${_cli_clip_timeout})

# `import` as a process: a CSV with its mapping, a dry run, nothing overwritten, daily notes
# appended, Apple Notes from the io fixtures, refusals.
brulion_add_test(tst_cli_import SOURCES tst_cli_import.cpp LIBS Qt6::Core)
target_compile_definitions(tst_cli_import PRIVATE BRULION_CLI="$<TARGET_FILE:brulion-cli>"
    BRULION_APPLENOTES_FIXTURES="${PROJECT_SOURCE_DIR}/tests/core/io/fixtures/applenotes")
add_dependencies(tst_cli_import brulion-cli)
math(EXPR _cli_import_timeout "120 * ${BRULION_TEST_SLOWDOWN}")
set_tests_properties(tst_cli_import PROPERTIES TIMEOUT ${_cli_import_timeout})

# `acp serve` (src/cli/acphost.h) in process over a real local socket, with the app's side (core
# acp/remote.h under acp::Client) and the agent of tests/app/shell/fakeacpagent.cpp as a program:
# the rewriting on the way to the agent, a conversation, questions relayed, files and terminals
# served here, pairing and its refusals, unpairing, idle streams, sends that come twice, and the
# notebook synced with an in-memory Nextcloud (tests/core/sync) before and after a turn.
qt_add_executable(cli_fake_acp_agent ${PROJECT_SOURCE_DIR}/tests/app/shell/fakeacpagent.cpp)
target_link_libraries(cli_fake_acp_agent PRIVATE Qt6::Core)
brulion_set_warnings(cli_fake_acp_agent)
brulion_add_test(tst_cli_acp_host SOURCES tst_cli_acp_host.cpp
    LIBS brulion_cli brulion_core_acp brulion_core_notebook brulion_sync_testsupport Qt6::Network Qt6::Core)
target_compile_definitions(tst_cli_acp_host PRIVATE FAKE_ACP_AGENT="$<TARGET_FILE:cli_fake_acp_agent>")
add_dependencies(tst_cli_acp_host cli_fake_acp_agent)
math(EXPR _acp_host_timeout "300 * ${BRULION_TEST_SLOWDOWN}")
set_tests_properties(tst_cli_acp_host PROPERTIES TIMEOUT ${_acp_host_timeout})
