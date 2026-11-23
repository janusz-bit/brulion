# automation — brulion:// addresses (docs/UI.md "Automation"): reading and writing them (uri.h),
# what they may do without asking and the notebooks' automation keys (policy.h), the blocks they
# add to a note as pure functions (edits.h), and a request worked out for a notebook and carried out
# through the app's Workspace or a headless notebook::Notebook (plan.h). Used by the app (links,
# Android's intents, the browser's protocol handler), brulion-cli (`uri`) and Android's background
# run of an automation request. Public headers: automation.h (overview), uri.h, policy.h, edits.h,
# plan.h.
brulion_add_core_module(automation
    SOURCES
        automation.h
        uri.h uri.cpp
        policy.h policy.cpp
        edits.h edits.cpp
        plan.h plan.cpp
        chatlink.h chatlink.cpp
    DEPS util text dates markdown vault index notebook)
