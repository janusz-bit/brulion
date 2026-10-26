# brulion-cli (src/cli). In process (library brulion_cli): the argument parser, the registry, its
# JSON Schema, the help and completion, the diff, and that docs/CLI.md lists every command. As a
# process (tst_cli_process: QProcess on the real brulion-cli with a private home): every command's
# JSON and exit code on temporary notebooks, refusals (read-only, destructive, outside the
# notebook, a missing notebook), Polish text in argv (also LANG=C) and on stdin, two processes
# appending to one journal, dry runs, the app's settings left untouched and no reminder state
# written, messages in the system's language (Polish or English; JSON keys, codes, schema and MCP
# English), and the MCP server (initialize, tools/list, tools/call).
brulion_add_test(tst_cli_args SOURCES tst_cli_args.cpp LIBS brulion_cli Qt6::Core)
target_compile_definitions(tst_cli_args PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
brulion_add_test(tst_cli_process SOURCES tst_cli_process.cpp LIBS brulion_ipc brulion_core_notebook)
target_compile_definitions(tst_cli_process PRIVATE BRULION_CLI="$<TARGET_FILE:brulion-cli>")
add_dependencies(tst_cli_process brulion-cli)
if(TARGET brulion)
    # `brulion cli …`: the app's binary hands over to the command line.
    target_compile_definitions(tst_cli_process PRIVATE BRULION_APP="$<TARGET_FILE:brulion>")
    add_dependencies(tst_cli_process brulion)
endif()
math(EXPR _cli_timeout "120 * ${BRULION_TEST_SLOWDOWN}")
set_tests_properties(tst_cli_process PROPERTIES TIMEOUT ${_cli_timeout})
