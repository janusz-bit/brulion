# ai — AI for extensions (docs/EXTENSIONS.md "AI"): b.ai's requests to a model the user chose on
# the device — Anthropic's Messages API (Claude) or any OpenAI-compatible chat API (OpenAI, a server
# of the user's own: Ollama, llama.cpp, LM Studio …). Requests and answers as pure functions
# (tests); Client sends through sync::HttpTransport (no network in tests).
brulion_add_core_module(ai
    SOURCES
        ai.h ai.cpp
    DEPS util sync
    QT Network)
