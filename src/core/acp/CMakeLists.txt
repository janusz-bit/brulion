# acp — the Agent Client Protocol from the client's side (acp.h): an AI agent the user runs on
# this computer (Claude Code, Codex, Gemini CLI …) as a partner of the AI chat. JSON-RPC over
# newline-delimited JSON, the handshake, a session per conversation, the turns with what the agent
# writes, does and asks. Pure protocol over a send function (tests play the agent); the app starts
# the agent's process: src/app/shell/workspaceagents.h.
brulion_add_core_module(acp
    SOURCES
        acp.h acp.cpp)
