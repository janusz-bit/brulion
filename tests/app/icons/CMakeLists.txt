# BIcon (src/app/qml/components/BIcon.qml) and its pictures (src/app/ui/iconprovider.h, library
# brulion_ui_icons): the same pixels as Qt's IconImage, which BIcon held in a Loader until 2026-11,
# at device pixel ratio 1 and 2; its sizes; names that must not leave the icon folder; one raster
# for many icons. Qt Quick's software backend, offscreen; the app's BIcon.qml from the source tree.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion_ui_icons AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls/impl")
    brulion_add_test(tst_icons
        SOURCES tst_icons.cpp
        LIBS brulion_ui_icons Qt6::Qml Qt6::Quick)
    target_compile_definitions(tst_icons PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    set_tests_properties(tst_icons PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH}"
        TIMEOUT 120)
    add_test(NAME tst_icons_dpr2 COMMAND tst_icons)
    set_tests_properties(tst_icons_dpr2 PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=2;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH}"
        TIMEOUT 120)
endif()
