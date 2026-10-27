qt_add_executable(brulion WIN32 MACOSX_BUNDLE
    main.cpp
)

# Developer QML: the component gallery (`--gallery`), the editor playground (`--editor`) and the
# in-app scripts and self-tests (`--editor-script`: qml/dev/PageScript.qml,
# qml/content/ContentJournalsSelfTest.qml), about 0.7 MB of compiled QML. Left out of the
# Android and browser builds that are shipped (Release, MinSizeRel), where these options then
# load nothing; PageView and JournalsView get the stand-ins from qml/stubs/. Desktop builds and
# the other build types have them (the ctest QML self-tests need them).
# BRULION_DEV_QML=AUTO (the default) decides by platform and CMAKE_BUILD_TYPE on every configure,
# so a build dir reused for another build type follows it; ON or OFF force it. The result is
# BRULION_DEV_QML_ENABLED (also read by tests/app).
set(BRULION_DEV_QML AUTO CACHE STRING
    "Build the developer QML (gallery, editor playground, in-app self-tests): AUTO, ON or OFF")
set_property(CACHE BRULION_DEV_QML PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${BRULION_DEV_QML}" _brulion_dev_qml)
if(_brulion_dev_qml STREQUAL "AUTO")
    if((ANDROID OR EMSCRIPTEN) AND CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
        set(_brulion_dev_qml OFF)
    else()
        set(_brulion_dev_qml ON)
    endif()
elseif(_brulion_dev_qml MATCHES "^(ON|YES|TRUE|Y|1)$")
    set(_brulion_dev_qml ON)
elseif(_brulion_dev_qml MATCHES "^(OFF|NO|FALSE|N|0)$")
    set(_brulion_dev_qml OFF)
else()
    message(FATAL_ERROR "BRULION_DEV_QML must be AUTO, ON or OFF, not '${BRULION_DEV_QML}'")
endif()
set(BRULION_DEV_QML_ENABLED ${_brulion_dev_qml} CACHE INTERNAL "Effective BRULION_DEV_QML")
message(STATUS "Developer QML: ${BRULION_DEV_QML_ENABLED} (BRULION_DEV_QML=${BRULION_DEV_QML})")

# --- Design system (docs/UI.md): Theme singleton, shared B* components, the dev gallery and
# their C++ helpers (QML singletons DatesHelper, TextHelper, DiffHelper, KeyboardInsets,
# DeviceInfo, DevTools).
set(BRULION_UI_QML
    qml/theme/Theme.qml
    qml/components/BAccentDialog.qml
    qml/components/BBadge.qml
    qml/components/BBreadcrumb.qml
    qml/components/BButton.qml
    qml/components/BCard.qml
    qml/components/BCheckBox.qml
    qml/components/BChip.qml
    qml/components/BColorSwatch.qml
    qml/components/BCommandPalette.qml
    qml/components/BConfirmDialog.qml
    qml/components/BDatePicker.qml
    qml/components/BDialog.qml
    qml/components/BDiffView.qml
    qml/components/BEmptyState.qml
    qml/components/BFocusRing.qml
    qml/components/BFontPickerDialog.qml
    qml/components/BIcon.qml
    qml/components/BIconButton.qml
    qml/components/BKbd.qml
    qml/components/BKeyboardFocusKeeper.qml
    qml/components/BKeyboardPlaceholder.qml
    qml/components/BListItem.qml
    qml/components/BMenu.qml
    qml/components/BMenuItem.qml
    qml/components/BMenuSeparator.qml
    qml/components/BMergeView.qml
    qml/components/BPalette.qml
    qml/components/BProgressBar.qml
    qml/components/BQuickSwitcher.qml
    qml/components/BReminderDialog.qml
    qml/components/BRowScrollBar.qml
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
    qml/components/BTimePicker.qml
    qml/components/BTimePickerDialog.qml
    qml/components/BToast.qml
    qml/components/BToastHost.qml
    qml/components/BToolTip.qml
)
if(BRULION_DEV_QML_ENABLED)
    list(APPEND BRULION_UI_QML qml/dev/Gallery.qml qml/dev/GallerySection.qml)
endif()
# DatesHelper, TextHelper and DiffHelper live in the GUI-free static library brulion_ui_helpers
# (src/app/ui/CMakeLists.txt, unit-tested in tests/app) and are registered here by
# ui/qmlforeign.h; DevTools and DeviceInfo are app-only.
add_subdirectory(ui)
# The scroll probe (ui/scrollprobe.h, `--scroll-probe`): a measuring tool for developers, ~160 KB
# of code, nothing at run time unless asked for. Left out of the Android and browser builds that
# are shipped (Release, MinSizeRel; then `--scroll-probe` only says so); measurement builds
# (RelWithDebInfo — a debuggable APK — or Debug) and desktop builds have it (ctest
# tst_scroll_jumps_qml needs it).
if((ANDROID OR EMSCRIPTEN) AND CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
    set(_brulion_scroll_probe_default OFF)
else()
    set(_brulion_scroll_probe_default ON)
endif()
option(BRULION_SCROLL_PROBE "Build the scroll probe (--scroll-probe)" ${_brulion_scroll_probe_default})
if(BRULION_SCROLL_PROBE)
    set(_brulion_scroll_probe_sources ui/scrollprobe.cpp)
else()
    set(_brulion_scroll_probe_sources ui/scrollprobe_off.cpp)
endif()
set(BRULION_UI_SOURCES
    ui/qmlforeign.h
    ui/devtools.h ui/devtools.cpp
    ui/scrollprobe.h ${_brulion_scroll_probe_sources}
    ui/deviceinfo.h ui/deviceinfo.cpp
    ui/keyboardsources.h ui/keyboardsources.cpp
)
set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# The generated QML type registration includes the helper headers by file name.
target_include_directories(brulion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(brulion PRIVATE brulion_ui_helpers brulion_ui_touch brulion_ui_scroll brulion_ui_network brulion_ui_fonts)
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

# main.cpp removes QQmlApplicationEngine's QML file selector (startup time): file selector
# directories (+android/, +wasm/ …) among the app's QML and resources would be ignored.
file(GLOB_RECURSE _brulion_file_selectors LIST_DIRECTORIES true
    ${CMAKE_CURRENT_SOURCE_DIR}/* ${PROJECT_SOURCE_DIR}/assets/*)
list(FILTER _brulion_file_selectors INCLUDE REGEX "/\\+[^/]*$")
if(_brulion_file_selectors)
    message(FATAL_ERROR "QML file selectors are not used (src/app/main.cpp): ${_brulion_file_selectors}")
endif()

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
# Windows: the icon and version information of brulion.exe.
brulion_windows_resources(brulion "Brulion")

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

    # Shipped APKs (Release, MinSizeRel) without the QML debugging plugins (qmltooling, 12
    # libraries, 1 MB per ABI) that androiddeployqt would deploy with QtQml: they only serve a QML
    # debugger or profiler (-qmljsdebugger), which these builds never start.
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
        qt_import_plugins(brulion EXCLUDE_BY_TYPE qmltooling)
    endif()

    # HTTPS (Nextcloud / WebDAV sync): Qt's OpenSSL TLS plugin loads libssl_3.so and
    # libcrypto_3.so when the first secure connection starts; Qt for Android does not ship them.
    # Bundled as Qt documents it ("Adding OpenSSL Support for Android"): the prebuilt OpenSSL 3 of
    # KDAB/android_openssl (Apache-2.0, docs/ANDROID.md), pinned to one commit and checked by
    # SHA-256, for every ABI of the build (androiddeployqt puts each one into lib/<its ABI>/).
    set(BRULION_ANDROID_OPENSSL_DIR "" CACHE PATH
        "android_openssl checkout with ssl_3/<abi>/lib{crypto,ssl}_3.so (default: download the pinned files)")
    # OpenSSL 3.1.8, 16 KB pages for 64-bit ABIs; https://github.com/KDAB/android_openssl. The Nix
    # build (nix/android.nix) passes this commit as BRULION_ANDROID_OPENSSL_DIR: pin the same one in
    # nix/android/openssl.json (nix/android/update-locks.sh --openssl <commit>).
    set(_brulion_openssl_commit b71f1470962019bd89534a2919f5925f93bc5779)
    set(_brulion_openssl_files
        arm64-v8a/libcrypto_3.so=1e6c12ae0c2dadfe9d178d7f80f0ab248a1877a234066098bf5594e5205e5740
        arm64-v8a/libssl_3.so=01d2bd0baac626efd3309f35f99c4b826dd9b885a7e4d14d5b12b3603d3a407f
        armeabi-v7a/libcrypto_3.so=a9d5352e52600e1c1c05d12e3112a479af7a2a3287760e3fcde432a423266b19
        armeabi-v7a/libssl_3.so=a1a9fda0b13d93a30cf103f993c080eafc6e1dee249e1a29d68cd7616dc052a4
        x86_64/libcrypto_3.so=bc4bdd31835659e639c2dc8f0a722342f51230074c23ee3dd6d1fa2353d72db1
        x86_64/libssl_3.so=2af32571317adf90fb7a007756a17dd15cd2469da9ebbd4e973883f8753369c1
        x86/libcrypto_3.so=420507adf9896267252d93e6bbf5eae5d8555a7693c026019980babf51065f3d
        x86/libssl_3.so=5ed7b9c925755af7a0e163b244c535d283869270e55c77777d1cec7cfcfe88d3
    )
    set(_brulion_openssl_abis ${QT_ANDROID_ABIS})
    if(NOT _brulion_openssl_abis)
        set(_brulion_openssl_abis ${CMAKE_ANDROID_ARCH_ABI})
    endif()
    set(_brulion_openssl_libs "")
    foreach(_entry IN LISTS _brulion_openssl_files)
        string(REGEX MATCH "^([^/]+)/([^=]+)=(.+)$" _match "${_entry}")
        set(_abi ${CMAKE_MATCH_1})
        set(_name ${CMAKE_MATCH_2})
        set(_sha256 ${CMAKE_MATCH_3})
        if(NOT _abi IN_LIST _brulion_openssl_abis)
            continue()
        endif()
        if(BRULION_ANDROID_OPENSSL_DIR)
            set(_file ${BRULION_ANDROID_OPENSSL_DIR}/ssl_3/${_abi}/${_name})
        else()
            set(_file ${CMAKE_BINARY_DIR}/android_openssl/${_brulion_openssl_commit}/${_abi}/${_name})
            if(EXISTS ${_file})
                file(SHA256 ${_file} _actual)
            else()
                set(_actual "")
            endif()
            if(NOT _actual STREQUAL _sha256)
                set(_url "https://raw.githubusercontent.com/KDAB/android_openssl/${_brulion_openssl_commit}/ssl_3/${_abi}/${_name}")
                message(STATUS "Downloading ${_url}")
                file(DOWNLOAD ${_url} ${_file}.part TLS_VERIFY ON STATUS _status
                    EXPECTED_HASH SHA256=${_sha256})
                list(GET _status 0 _code)
                if(NOT _code EQUAL 0)
                    file(REMOVE ${_file}.part)
                    message(FATAL_ERROR "Cannot download OpenSSL for Android (${_status}): ${_url}. Offline: "
                        "set BRULION_ANDROID_OPENSSL_DIR to a checkout of KDAB/android_openssl at ${_brulion_openssl_commit}.")
                endif()
                file(RENAME ${_file}.part ${_file})
            endif()
        endif()
        if(NOT EXISTS ${_file})
            message(FATAL_ERROR "${_file} not found (BRULION_ANDROID_OPENSSL_DIR)")
        endif()
        file(SHA256 ${_file} _actual)
        if(NOT _actual STREQUAL _sha256)
            message(FATAL_ERROR "${_file}: SHA-256 ${_actual}, expected ${_sha256} (KDAB/android_openssl ${_brulion_openssl_commit})")
        endif()
        list(APPEND _brulion_openssl_libs ${_file})
    endforeach()
    foreach(_abi IN LISTS _brulion_openssl_abis)
        if(NOT "${_brulion_openssl_libs}" MATCHES "/${_abi}/libssl_3\\.so")
            message(FATAL_ERROR "No OpenSSL for the Android ABI ${_abi} (src/app/CMakeLists.txt)")
        endif()
    endforeach()
    set_property(TARGET brulion APPEND PROPERTY QT_ANDROID_EXTRA_LIBS ${_brulion_openssl_libs})
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

# -no-obsolete: strings that left the sources are dropped, not kept as vanished entries.
qt_add_translations(brulion
    TS_FILE_DIR ${PROJECT_SOURCE_DIR}/i18n
    TS_FILE_BASE brulion
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete)

include(GNUInstallDirs)
install(TARGETS brulion
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    # Android: the app is a shared library (libbrulion_<abi>.so) packaged by androiddeployqt.
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
