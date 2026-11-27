brulion_add_test(tst_atrament_editor SOURCES tst_atrament_editor.cpp LIBS brulion_editor_atrament)
target_compile_definitions(tst_atrament_editor PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
brulion_add_test(tst_atrament_editing SOURCES tst_atrament_editing.cpp LIBS brulion_editor_atrament)
target_compile_definitions(tst_atrament_editing PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# Foreign rows hosted in an AtramentItem (foreignhost.h): QML delegates over a page's code blocks and
# tables, its header and footer, following the scroll, a finger's drag on them scrolling the page —
# an offscreen Qt Quick window with the software renderer (Qt Quick's QML module for the delegates).
get_filename_component(_brulion_atrament_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
brulion_add_test(tst_atrament_editor_hosting SOURCES tst_atrament_editor_hosting.cpp
    LIBS brulion_editor_atrament Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_atrament_editor_hosting PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_tests_properties(tst_atrament_editor_hosting PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_NO_TOUCH_COMPRESSION=1;QML_IMPORT_PATH=${_brulion_atrament_qml_root}")
# Block selection and moving blocks (pageselection.cpp, pagedrag.cpp): the QML selection self-test's
# checks against the page drawn by Atrament (a hosted row's grip in an offscreen Qt Quick window).
brulion_add_test(tst_atrament_selection SOURCES tst_atrament_selection.cpp
    LIBS brulion_editor_atrament Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_atrament_selection PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_tests_properties(tst_atrament_selection PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_NO_TOUCH_COMPRESSION=1;QML_IMPORT_PATH=${_brulion_atrament_qml_root}")
