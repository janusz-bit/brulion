qt_add_executable(brulion WIN32 MACOSX_BUNDLE
    main.cpp
)

# --- Design system (docs/UI.md): Theme singleton, shared B* components, the dev gallery and
# their C++ helpers (QML singletons DatesHelper, TextHelper, DiffHelper, DeviceInfo, DevTools).
set(BRULION_UI_QML
    qml/theme/Theme.qml
    qml/components/BBadge.qml
    qml/components/BBreadcrumb.qml
    qml/components/BButton.qml
    qml/components/BCard.qml
    qml/components/BCheckBox.qml
    qml/components/BChip.qml
    qml/components/BCommandPalette.qml
    qml/components/BConfirmDialog.qml
    qml/components/BDatePicker.qml
    qml/components/BDialog.qml
    qml/components/BDiffView.qml
    qml/components/BEmptyState.qml
    qml/components/BFocusRing.qml
    qml/components/BIcon.qml
    qml/components/BIconButton.qml
    qml/components/BKbd.qml
    qml/components/BListItem.qml
    qml/components/BMenu.qml
    qml/components/BMenuItem.qml
    qml/components/BMenuSeparator.qml
    qml/components/BMergeView.qml
    qml/components/BPalette.qml
    qml/components/BProgressBar.qml
    qml/components/BQuickSwitcher.qml
    qml/components/BReminderDialog.qml
    qml/components/BScrollBar.qml
    qml/components/BSearchField.qml
    qml/components/BSectionHeader.qml
    qml/components/BSegmented.qml
    qml/components/BShadow.qml
    qml/components/BSpinner.qml
    qml/components/BSplitHandle.qml
    qml/components/BSwitch.qml
    qml/components/BTabBar.qml
    qml/components/BTabButton.qml
    qml/components/BText.qml
    qml/components/BTextArea.qml
    qml/components/BTextField.qml
    qml/components/BToast.qml
    qml/components/BToastHost.qml
    qml/components/BToolTip.qml
    qml/dev/Gallery.qml
    qml/dev/GallerySection.qml
)
# DatesHelper, TextHelper and DiffHelper live in the GUI-free static library brulion_ui_helpers
# (src/app/ui/CMakeLists.txt, unit-tested in tests/app) and are registered here by
# ui/qmlforeign.h; DevTools and DeviceInfo are app-only.
add_subdirectory(ui)
set(BRULION_UI_SOURCES
    ui/qmlforeign.h
    ui/devtools.h ui/devtools.cpp
    ui/deviceinfo.h ui/deviceinfo.cpp
)
set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# The generated QML type registration includes the helper headers by file name.
target_include_directories(brulion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(brulion PRIVATE brulion_ui_helpers)
# Lucide icons (assets/icons, ISC licence) as :/icons/<name>.svg, used by BIcon; the logo as
# :/images/logo.svg.
file(GLOB _brulion_icons CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/icons/*.svg)
qt_add_resources(brulion brulion_icons
    PREFIX /icons
    BASE ${PROJECT_SOURCE_DIR}/assets/icons
    FILES ${_brulion_icons})
qt_add_resources(brulion brulion_logo
    PREFIX /images
    BASE ${PROJECT_SOURCE_DIR}/assets
    FILES ${PROJECT_SOURCE_DIR}/assets/logo.svg)

qt_add_qml_module(brulion
    URI Brulion
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        ${BRULION_UI_QML}
    SOURCES
        ${BRULION_UI_SOURCES}
    RESOURCES
        ../../assets/logo.svg
)

set_target_properties(brulion PROPERTIES OUTPUT_NAME brulion)

# --- App areas (editor, shell, views, ...): every src/app/<area>/area.cmake is included here.
# An area adds its C++ with target_sources(brulion PRIVATE <area>/x.cpp ...) and its QML with
# qt_target_qml_sources(brulion QML_FILES qml/<area>/X.qml ...) (paths relative to src/app),
# so parallel work never edits this file. C++ types use QML_ELEMENT/QML_SINGLETON.
file(GLOB _brulion_app_areas CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*/area.cmake)
list(SORT _brulion_app_areas)
foreach(_area IN LISTS _brulion_app_areas)
    include(${_area})
endforeach()

# Android package (APK/AAB via androiddeployqt): docs/ANDROID.md, scripts/android-build.sh.
# android/ holds the manifest, resources and the Kotlin sources (alarms, permissions,
# notification taps). ABIs come from the configure option QT_ANDROID_ABIS (default: the ABI
# of the Qt used). Qt 6.11 supports API 28 (Android 9) to 36 (Android 16).
if(ANDROID)
    set(BRULION_ANDROID_VERSION_CODE "" CACHE STRING
        "Android versionCode (default: major * 1000000 + minor * 1000 + patch)")
    if(BRULION_ANDROID_VERSION_CODE)
        set(_brulion_version_code ${BRULION_ANDROID_VERSION_CODE})
    else()
        # The formula of the previous (Tauri) Brulion: its installs update to this one.
        math(EXPR _brulion_version_code
            "${PROJECT_VERSION_MAJOR} * 1000000 + ${PROJECT_VERSION_MINOR} * 1000 + ${PROJECT_VERSION_PATCH}")
    endif()
    set_target_properties(brulion PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${PROJECT_SOURCE_DIR}/android
        QT_ANDROID_PACKAGE_NAME io.github.januszbit.brulion
        QT_ANDROID_APP_NAME Brulion
        QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
        QT_ANDROID_VERSION_CODE ${_brulion_version_code}
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 36
        QT_ANDROID_COMPILE_SDK_VERSION 36
    )
endif()

# Browser (Qt for WebAssembly, single-threaded): docs/WEB.md, scripts/wasm-build.sh, which
# assembles the deployable site (our PWA shell from wasm/ + brulion.js/.wasm + qtloader.js).
# The Qt-generated brulion.html in the build directory is only Qt's default page.
if(EMSCRIPTEN)
    # Browser-only output: no Node.js/worker glue in brulion.js.
    target_link_options(brulion PRIVATE "SHELL:-s ENVIRONMENT=web")
endif()

target_compile_definitions(brulion PRIVATE BRULION_VERSION="${PROJECT_VERSION}")
target_link_libraries(brulion PRIVATE brulion_core brulion_platform Qt6::Quick Qt6::QuickControls2 Qt6::Svg)
brulion_set_warnings(brulion)

qt_add_translations(brulion
    TS_FILE_DIR ${PROJECT_SOURCE_DIR}/i18n
    TS_FILE_BASE brulion
    RESOURCE_PREFIX /i18n)

include(GNUInstallDirs)
install(TARGETS brulion
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    # Android: the app is a shared library (libbrulion_<abi>.so) packaged by androiddeployqt.
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
