# agentlimits — the subscription limits of an AI agent's account (docs/UI.md "AI agents (ACP)",
# "Its limits"): Claude Code's and Codex's sign-in files read (credentials, never refreshed), their
# usage endpoints' answers as windows (five hours, the week, a model's week, a month) and the
# requests that read them (limits.h, pure but readCredentials()); LimitsReader (reader.h) reads them
# over sync::HttpTransport at most every five minutes per account. The app's side:
# src/app/shell/agentlimits.h (Workspace.chats.limits).
brulion_add_core_module(agentlimits
    SOURCES
        limits.h limits.cpp
        reader.h reader.cpp
    DEPS sync log)
