# acp — the Agent Client Protocol from the client's side (acp.h): an AI agent the user runs on
# this computer (Claude Code, Codex, Gemini CLI …) as a partner of the AI chat. JSON-RPC over
# newline-delimited JSON, the handshake, a session per conversation, the turns with what the agent
# writes, does and asks. Pure protocol over a send function (tests play the agent); the app starts
# the agent's process: src/app/shell/workspaceagents.h. remote.h: the same protocol to an agent on
# another computer, relayed by `brulion-cli acp serve` there (pairing, the host's info, a stream of
# lines over HTTPS through sync::HttpTransport). hostoffer.h: a computer's agents offered to the
# notebook's other devices in its synced .brulion/config.json (an address of a tailnet and names).
# apptunnel.h: the app's live tools for an agent on another computer — its calls tunnelled over the
# device's stream to the app, which answers them in process. codehost.h: Code mode's threads on
# another computer — its projects offered to the devices, a thread's session in a project's folder
# or worktree there, the checkpoint of each turn, the changes since one. appchats.h: the
# conversations the desktop app runs, followed and steered from another device through the app's
# own `acp serve` (their live state, the question answered, Stop, the next message).
brulion_add_core_module(acp
    SOURCES
        acp.h acp.cpp
        remote.h remote.cpp
        hostoffer.h hostoffer.cpp
        apptunnel.h apptunnel.cpp
        codehost.h codehost.cpp
        appchats.h appchats.cpp
    DEPS sync tailscale chat)
