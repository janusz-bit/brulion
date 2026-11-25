# webhook — webhooks that start a scheduled conversation of the AI chat (docs/UI.md "Scheduled
# conversations" → "Webhooks"): the secret token of a hook's address and its constant-time check,
# the token in a request's path, a request's body as untrusted data in the run's message (UTF-8
# only, cut at 16 KiB), and the rate limits. GUI-free and without a network: the server is
# `brulion-cli app hooks serve` (src/cli/hooks.cpp), the runner src/app/shell/chatschedules.h.
brulion_add_core_module(webhook
    SOURCES
        webhook.h webhook.cpp)
