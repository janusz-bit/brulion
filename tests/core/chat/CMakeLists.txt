# Conversations with AI as pages (brulion_core_chat): messages read back with their lines, blank
# lines and code, appended byte for byte (CRLF, no final line break), reminders kept quiet, new
# pages, their names, the journal's line, the history for b.ai, previews; malformed pages.
brulion_add_test(tst_chat SOURCES tst_chat.cpp LIBS brulion_core_chat)
