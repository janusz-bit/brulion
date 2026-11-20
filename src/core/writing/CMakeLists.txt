# writing — the editor's AI assistant (docs/UI.md "AI in the editor"): a selection rewritten after an
# instruction or a preset, continued or summed up by the model the user chose — the request, the
# answer cleaned for the note, where it goes (a pure edit) and the change word by word. GUI-free;
# the app sends the request (ai::Client) and applies the edit as one undo step.
brulion_add_core_module(writing
    SOURCES
        writing.h writing.cpp
    DEPS ai)
