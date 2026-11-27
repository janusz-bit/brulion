if(NOT TARGET brulion_zstd)
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/zstd ${CMAKE_BINARY_DIR}/3rdparty/zstd)
endif()

# agentusage — what the AI agents of this computer used, read from their own session logs (docs/UI.md
# "AI agents (ACP)", "Its usage"): Claude Code's projects/*.jsonl and Codex's rollouts (plain, or
# compressed by Codex: rollout-*.jsonl.zst, through the vendored zstd decoder of 3rdparty/zstd) parsed into
# requests (a streamed message once), tokens by kind and an API-equivalent cost from a dated price
# table (usage.h, prices.cpp: pure); the folders read with an incremental cache of their own
# (history.h); UsageReader (reader.h) reads them on a thread of its own at most every five minutes per
# source. The app's side: src/app/shell/agentusage.h (Workspace.chats.usage); brulion-cli acp usage.
brulion_add_core_module(agentusage
    SOURCES
        usage.h usage.cpp prices.cpp
        history.h history.cpp
        logstream_p.h logstream.cpp
        reader.h reader.cpp
    DEPS agentlimits log)
target_link_libraries(brulion_core_agentusage PRIVATE brulion_zstd)
