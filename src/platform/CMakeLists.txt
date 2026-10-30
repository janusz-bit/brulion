# brulion_platform — OS integration (notifications, storage access, WASM persistence).
#   all builds   platform (target, log folder, device), autostart ("Start when you sign in": the
#                Windows Run key over any QSettings — the tests use INI files), appidentity (the
#                Windows AppUserModelID of the toasts; pure helpers everywhere), pickedfiles (the
#                bookkeeping of the browser's file picker, wasm/wasmfiles.h; pure, tested
#                everywhere), pdfpages (PDF pages as pictures: a drawing's backdrop — one backend per
#                build, below), documentpaths (the file an Android document URI names: a picked file
#                of the notebook used where it is; pure, tested everywhere). Tests: tests/platform.
qt_add_library(brulion_platform STATIC
    platform.h platform.cpp
    autostart.h autostart.cpp
    appidentity.h appidentity.cpp
    pickedfiles.h pickedfiles.cpp
    pdfpages.h pdfpages.cpp
    documentpaths.h documentpaths.cpp
)
target_include_directories(brulion_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_platform PUBLIC brulion_core Qt6::Gui)
brulion_set_warnings(brulion_platform)

# PDF pages as pictures (pdfpages.h; sizes and the choice: docs/PERFORMANCE.md "Rozmiar"):
# Android — the system's PdfRenderer through JNI (no library); the browser — none (Qt has no Qt PDF
# for WebAssembly); elsewhere Qt PDF when Qt has the module (aqt's `qtpdf`: Windows, the AppImage,
# CI), else poppler-qt6 (the Nix build: nixpkgs has Qt PDF only inside QtWebEngine), else none.
# -DBRULION_PDF_BACKEND=<android|qtpdf|poppler|none> chooses one by hand.
set(BRULION_PDF_BACKEND "auto" CACHE STRING "PDF page renderer: auto, qtpdf, poppler, android or none")
set(_pdf_backend "${BRULION_PDF_BACKEND}")
if(_pdf_backend STREQUAL "auto")
    if(ANDROID)
        set(_pdf_backend android)
    elseif(EMSCRIPTEN)
        set(_pdf_backend none)
    else()
        find_package(Qt6 QUIET COMPONENTS Pdf)
        if(TARGET Qt6::Pdf)
            set(_pdf_backend qtpdf)
        else()
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(POPPLER_QT6 QUIET IMPORTED_TARGET GLOBAL poppler-qt6)
            endif()
            if(TARGET PkgConfig::POPPLER_QT6)
                set(_pdf_backend poppler)
            else()
                set(_pdf_backend none)
            endif()
        endif()
    endif()
endif()
if(_pdf_backend STREQUAL "qtpdf")
    find_package(Qt6 REQUIRED COMPONENTS Pdf)
    # Seen by every target that links brulion_platform (Qt's finalizer walks their libraries in
    # their own directories; found here, the target is this directory's only).
    get_target_property(_pdf_global Qt6::Pdf IMPORTED_GLOBAL)
    if(NOT _pdf_global)
        set_target_properties(Qt6::Pdf PROPERTIES IMPORTED_GLOBAL TRUE)
    endif()
    target_sources(brulion_platform PRIVATE pdfpages_qtpdf.cpp)
    target_link_libraries(brulion_platform PRIVATE Qt6::Pdf)
elseif(_pdf_backend STREQUAL "poppler")
    if(NOT TARGET PkgConfig::POPPLER_QT6)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(POPPLER_QT6 REQUIRED IMPORTED_TARGET GLOBAL poppler-qt6)
    endif()
    target_sources(brulion_platform PRIVATE pdfpages_poppler.cpp)
    target_link_libraries(brulion_platform PRIVATE PkgConfig::POPPLER_QT6)
elseif(_pdf_backend STREQUAL "android")
    target_sources(brulion_platform PRIVATE pdfpages_android.cpp)
elseif(_pdf_backend STREQUAL "none")
    target_sources(brulion_platform PRIVATE pdfpages_none.cpp)
else()
    message(FATAL_ERROR "BRULION_PDF_BACKEND: unknown backend '${BRULION_PDF_BACKEND}'")
endif()
message(STATUS "Brulion: PDF pages through ${_pdf_backend} (src/platform/pdfpages.h)")

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

# WebAssembly: IndexedDB persistence (IDBFS), browser notifications, shares handed over by another
# tab (the share target), the browser's file picker and downloads (wasmfiles: Qt's private
# QWasmLocalFileAccess, hence Qt6::GuiPrivate — no Qt Widgets in the browser build), loading-screen
# bridge and the self-tests (?selftest=…) — docs/WEB.md. Include as "wasm/wasmstorage.h" etc.
if(EMSCRIPTEN)
    target_sources(brulion_platform PRIVATE
        wasm/wasmstorage.h wasm/wasmstorage.cpp
        wasm/wasmnotifier.h wasm/wasmnotifier.cpp
        wasm/wasmshare.h wasm/wasmshare.cpp
        wasm/wasmshell.h wasm/wasmshell.cpp
        wasm/wasmfiles.h wasm/wasmfiles.cpp
        wasm/wasmselftest.h wasm/wasmselftest.cpp
    )
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    target_link_libraries(brulion_platform PRIVATE Qt6::GuiPrivate)
    # IDBFS is an Emscripten JS library; it must be linked into the final executable.
    target_link_options(brulion_platform INTERFACE "-lidbfs.js")
endif()
