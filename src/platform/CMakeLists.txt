# brulion_platform — OS integration (notifications, storage access, WASM persistence).
#   all builds   platform (target, log folder, device), autostart ("Start when you sign in": the
#                Windows Run key over any QSettings — the tests use INI files), appidentity (the
#                Windows AppUserModelID of the toasts; pure helpers everywhere). Tests: tests/platform.
qt_add_library(brulion_platform STATIC
    platform.h platform.cpp
    autostart.h autostart.cpp
    appidentity.h appidentity.cpp
)
target_include_directories(brulion_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_platform PUBLIC brulion_core Qt6::Gui)
brulion_set_warnings(brulion_platform)

# Android: JNI bridge to the Kotlin side in android/src (alarms, permissions, notification
# taps) — docs/ANDROID.md. Include as "android/androidbridge.h".
if(ANDROID)
    target_sources(brulion_platform PRIVATE
        android/androidbridge.h android/androidbridge.cpp
    )
    option(BRULION_ANDROID_SELFTEST
        "Also build brulion_android_selftest, an APK exercising the bridge on a device/emulator" OFF)
    if(BRULION_ANDROID_SELFTEST)
        add_subdirectory(android/selftest)
    endif()
endif()

# WebAssembly: IndexedDB persistence (IDBFS), browser notifications, loading-screen bridge and
# the self-tests (?selftest=…) — docs/WEB.md. Include as "wasm/wasmstorage.h" etc.
if(EMSCRIPTEN)
    target_sources(brulion_platform PRIVATE
        wasm/wasmstorage.h wasm/wasmstorage.cpp
        wasm/wasmnotifier.h wasm/wasmnotifier.cpp
        wasm/wasmshell.h wasm/wasmshell.cpp
        wasm/wasmselftest.h wasm/wasmselftest.cpp
    )
    # IDBFS is an Emscripten JS library; it must be linked into the final executable.
    target_link_options(brulion_platform INTERFACE "-lidbfs.js")
endif()
