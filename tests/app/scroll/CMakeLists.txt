# RowScroll (src/app/ui/rowscroll.h, library brulion_ui_scroll): the row arithmetic of the scroll
# bar of long lists (pure, data-driven), and a live ListView with rows of 18 to 600 px — offscreen,
# software rendering: the thumb's progress moves with the content only (while Qt's own visibleArea
# jumps) — also with section headers and spacing, whose row above the view is not created —, its
# size stays while the list moves, dragging and the wheel over a free bar, rows
# inserted above the view, and folded (hidden) rows of a model that says which rows it shows.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion_ui_scroll AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Templates")
    brulion_add_test(tst_row_scroll
        SOURCES tst_row_scroll.cpp
        LIBS brulion_ui_scroll Qt6::Qml Qt6::Quick)
    set_tests_properties(tst_row_scroll PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 120)
endif()
