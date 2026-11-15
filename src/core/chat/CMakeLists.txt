# chat — conversations with the AI assistant as pages of the notebook (docs/SYNTAX.md §14
# "Conversations with AI"): reading a conversation's messages, appending one byte for byte, the
# first lines of a new one, its page's name, the journal's line that links to it. GUI-free and
# without the vault; the app's chat view and its turns: src/app/shell/workspacechats.h.
brulion_add_core_module(chat
    SOURCES
        chat.h chat.cpp
    DEPS util)
