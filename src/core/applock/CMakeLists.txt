# applock — the app lock (applock/applock.h; docs/UI.md "App lock"): the state machine of locking
# (start, background, idle, unlock), the waits after wrong PINs, the PIN's stored hash. Pure and
# GUI-free; the platform's own checks (Android's biometrics) and the clock are the app's.
brulion_add_core_module(applock
    SOURCES
        applock.h applock.cpp
    DEPS crypto)
