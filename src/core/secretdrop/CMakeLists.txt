# secretdrop — a secret the user gives an AI agent of this computer (docs/UI.md "AI chat" → "A
# secret the agent asks for"), handed over as an owner-only file (0600 in a 0700 folder of this
# run, under the app's runtime folder) that is deleted when the conversation's turn ends or its
# time is up (15 minutes at most), and at the next start for what a crashed run left; the value
# registered with the event log's redaction while it lives. GUI-free; the app's side:
# src/app/shell/chatsecrets.h.
brulion_add_core_module(secretdrop
    SOURCES
        secretdrop.h secretdrop.cpp
    DEPS log)
