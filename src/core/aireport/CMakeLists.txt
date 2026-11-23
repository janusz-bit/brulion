# aireport — a report of an AI response the user finds offensive or wrong (docs/UI.md "AI chat":
# Report response): its title and text — the reason, the user's comment, the response, the model,
# the app and the device; the question before it only when the user adds it — and the link that
# opens it, a new issue of Brulion's repository on GitHub or an email when the build names an
# address (BRULION_AI_REPORT_EMAIL), the texts cut so the link stays short. Pure functions: Brulion
# sends nothing itself.
brulion_add_core_module(aireport
    SOURCES
        aireport.h aireport.cpp
    DEPS ai)
