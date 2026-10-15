# brulion_ui_helpers — the GUI-free C++ helpers of the design system (docs/UI.md): natural-language
# dates and recurrence for QML (DatesHelper), fuzzy matching (TextHelper), line diffs and
# conflict hunks (DiffHelper), Qt's own translations (QtTranslations). A static library (QtCore + core modules only) so that
# tests/app links it without the app; the app's QML module "Brulion" registers the classes as
# QML singletons through QML_FOREIGN wrappers (src/app/ui/qmlforeign.h), which needs the
# metatypes extracted below. DevTools and DeviceInfo stay in the app (they need QtQuick/QtGui).
qt_add_library(brulion_ui_helpers STATIC
    dateshelper.h dateshelper.cpp
    diffhelper.h diffhelper.cpp
    texthelper.h texthelper.cpp
    qttranslations.h qttranslations.cpp
)
target_include_directories(brulion_ui_helpers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_helpers PUBLIC Qt6::Core PRIVATE brulion_core_dates brulion_core_text)
qt_extract_metatypes(brulion_ui_helpers)
brulion_set_warnings(brulion_ui_helpers)

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
