# brulion-cli — the command line for scripts and AI agents (docs/CLI.md): headless commands over a
# notebook folder with JSON output, an MCP server (`brulion-cli mcp`), and the `app …` commands for
# the running app (`app open`; with its remote control on, state, input and screenshots). Desktop only (added by the top-level CMakeLists.txt outside Android and the
# browser). One command registry (registry.cpp) gives the argument parser, the help, the JSON
# Schema and the MCP tools. Built on the core module notebook (the app's own logic), never on the
# app's shell: no reminders, no sync, no settings written.
#
# brulion_cli: the static library (the app links it for `brulion cli …`, tests/cli for in-process
# checks); brulion-cli: the executable.
qt_add_library(brulion_cli STATIC
    cli.h cli_p.h commands_p.h
    args.cpp
    commands_app.cpp
    commands_common.cpp
    commands_index.cpp
    commands_journal.cpp
    commands_meta.cpp
    commands_pages.cpp
    commands_tasks.cpp
    commands_tools.cpp
    context.cpp
    mcp.cpp
    output.cpp
    registry.cpp
    run.cpp
)
target_include_directories(brulion_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Public: the internal header cli_p.h (tests/cli) includes core headers.
target_link_libraries(brulion_cli
    PUBLIC Qt6::Core brulion_core_notebook brulion_core_calc brulion_core_reminders brulion_core_index
           brulion_core_markdown brulion_core_dates brulion_core_vault brulion_core_text brulion_core_util
    PRIVATE brulion_core_sync brulion_ipc Qt6::Network)
target_compile_definitions(brulion_cli PRIVATE BRULION_VERSION="${PROJECT_VERSION}")
brulion_set_warnings(brulion_cli)
# Its messages are English (tr(), translatable later); not collected into i18n/brulion_pl.ts.
set_property(TARGET brulion_cli PROPERTY QT_EXCLUDE_FROM_TRANSLATION ON)

qt_add_executable(brulion-cli main.cpp)
target_link_libraries(brulion-cli PRIVATE brulion_cli)
brulion_set_warnings(brulion-cli)
set_property(TARGET brulion-cli PROPERTY QT_EXCLUDE_FROM_TRANSLATION ON)

include(GNUInstallDirs)
install(TARGETS brulion-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# The agent skill (agentskills format) for Claude Code, Hermes and other agents.
install(FILES ${PROJECT_SOURCE_DIR}/packaging/agent/brulion/SKILL.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/brulion/agent-skills/brulion)
