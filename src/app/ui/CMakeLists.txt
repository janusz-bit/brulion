# brulion_ui_helpers — the GUI-free C++ helpers of the design system (docs/UI.md): natural-language
# dates and recurrence for QML (DatesHelper), fuzzy matching (TextHelper), line diffs and
# conflict hunks (DiffHelper), Qt's own translations (QtTranslations), the on-screen keyboard's
# height (KeyboardInsets: its sources are connected by the app, ui/keyboardsources.cpp). A static
# library (QtCore + core modules only) so that tests/app links it without the app; the app's QML
# module "Brulion" registers the classes as QML singletons through QML_FOREIGN wrappers
# (src/app/ui/qmlforeign.h), which needs the metatypes extracted below. DevTools and DeviceInfo
# stay in the app (they need QtQuick/QtGui).
qt_add_library(brulion_ui_helpers STATIC
    dateshelper.h dateshelper.cpp
    diffhelper.h diffhelper.cpp
    texthelper.h texthelper.cpp
    qttranslations.h qttranslations.cpp
    keyboardinsets.h keyboardinsets.cpp
)
target_include_directories(brulion_ui_helpers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_helpers PUBLIC Qt6::Core PRIVATE brulion_core_dates brulion_core_text brulion_core_log)
qt_extract_metatypes(brulion_ui_helpers)
brulion_set_warnings(brulion_ui_helpers)

# brulion_ui_touch — TouchFlick (touchflick.h): corrects Qt's touch flicks on screens that report
# no velocities (Android). Qt Quick, no private Qt headers; a static library so tests/app/touch
# drives it with Android-style input without the app; registered as the attached type
# "TouchFlick" by qmlforeign.h.
qt_add_library(brulion_ui_touch STATIC
    touchflick.h touchflick.cpp
)
target_include_directories(brulion_ui_touch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_touch PUBLIC Qt6::Core Qt6::Gui Qt6::Quick)
qt_extract_metatypes(brulion_ui_touch)
brulion_set_warnings(brulion_ui_touch)

# brulion_ui_network — the QML engine's network access manager (localnetwork.h): local URLs only,
# so rich text never fetches a remote picture. A static library so that tests/app/network checks
# it without the app.
qt_add_library(brulion_ui_network STATIC
    localnetwork.h localnetwork.cpp
)
target_include_directories(brulion_ui_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_network PUBLIC Qt6::Core Qt6::Network Qt6::Qml)
brulion_set_warnings(brulion_ui_network)

# brulion_ui_fonts — the fallback fonts for emoji and symbols (fallbackfonts.h; assets/fonts,
# subsets of Noto Emoji and Noto Sans Math under the SIL OFL 1.1, made by
# scripts/fallback-fonts.py) as resources ":/fonts/Brulion*.{ttf,otf}" (~170 KB, ~108 KB
# compressed). Android leaves them out: it always has an emoji font, and the app never loads them
# there. A static library so that tests/app/fonts checks them without the app.
qt_add_library(brulion_ui_fonts STATIC
    fallbackfonts.h fallbackfonts.cpp
)
target_include_directories(brulion_ui_fonts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_fonts PUBLIC Qt6::Gui)
if(NOT ANDROID)
    qt_add_resources(brulion_ui_fonts brulion_fallback_fonts
        PREFIX /fonts
        BASE ${PROJECT_SOURCE_DIR}/assets/fonts
        FILES ${PROJECT_SOURCE_DIR}/assets/fonts/BrulionEmoji.ttf ${PROJECT_SOURCE_DIR}/assets/fonts/BrulionMath.otf)
endif()
brulion_set_warnings(brulion_ui_fonts)

# Qt's own Polish texts (standard dialog buttons, QtQuick.Dialogs, QFileDialog — QtTranslations):
# qtbase_pl.qm and qtdeclarative_pl.qm from Qt's translations folder, bundled as resources
# ":/i18n/qt/…" so Android and the browser have them too. The folder: BRULION_QT_TRANSLATIONS_DIR
# (set by nix/package.nix), else "translations/" of a prefix on the CMake search path (nix: the
# qt6.qttranslations input, also in the dev shell), else the Qt being built against, else the
# host Qt of a cross build (Android and WebAssembly: aqt's desktop Qt has them). Without them the
# app uses Qt's installed translations at run time, when there are any.
set(BRULION_QT_TRANSLATIONS_DIR "" CACHE PATH
    "Folder with Qt's translations (qtbase_pl.qm, qtdeclarative_pl.qm) to bundle")
set(_brulion_qt_tr_dirs ${BRULION_QT_TRANSLATIONS_DIR})
foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
    list(APPEND _brulion_qt_tr_dirs "${_prefix}/translations")
endforeach()
foreach(_var CMAKE_PREFIX_PATH NIXPKGS_CMAKE_PREFIX_PATH)
    if(DEFINED ENV{${_var}})
        string(REPLACE ":" ";" _prefixes "$ENV{${_var}}")
        foreach(_prefix IN LISTS _prefixes)
            list(APPEND _brulion_qt_tr_dirs "${_prefix}/translations")
        endforeach()
    endif()
endforeach()
list(APPEND _brulion_qt_tr_dirs "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
if(QT_HOST_PATH)
    list(APPEND _brulion_qt_tr_dirs "${QT_HOST_PATH}/translations")
endif()
set(_brulion_qt_tr_files "")
foreach(_catalog qtbase qtdeclarative)
    foreach(_dir IN LISTS _brulion_qt_tr_dirs)
        if(_dir AND EXISTS "${_dir}/${_catalog}_pl.qm")
            # Copied next to the build files: resources need a single base folder.
            configure_file("${_dir}/${_catalog}_pl.qm" "${CMAKE_CURRENT_BINARY_DIR}/qt-translations/${_catalog}_pl.qm" COPYONLY)
            list(APPEND _brulion_qt_tr_files "${CMAKE_CURRENT_BINARY_DIR}/qt-translations/${_catalog}_pl.qm")
            break()
        endif()
    endforeach()
endforeach()
if(_brulion_qt_tr_files)
    qt_add_resources(brulion_ui_helpers brulion_qt_translations
        PREFIX /i18n/qt
        BASE ${CMAKE_CURRENT_BINARY_DIR}/qt-translations
        FILES ${_brulion_qt_tr_files})
else()
    message(WARNING "Qt's Polish translations (qtbase_pl.qm) not found: Qt's own texts (dialog buttons) "
                    "stay English unless Qt has them installed. Set BRULION_QT_TRANSLATIONS_DIR.")
endif()
