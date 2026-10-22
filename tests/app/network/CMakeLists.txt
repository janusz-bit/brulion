# The QML engine's network access manager (src/app/ui/localnetwork.h, library brulion_ui_network):
# remote URLs are refused without a connection, local ones (data:, file:) served, and a rich-text
# Text naming a remote <img> contacts nobody. Qt Quick offscreen with the software backend.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion_ui_network AND EXISTS "${_brulion_qt_qml_root}/QtQuick")
    brulion_add_test(tst_local_network
        SOURCES tst_local_network.cpp
        LIBS brulion_ui_network Qt6::Quick)
    set_tests_properties(tst_local_network PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${_brulion_qt_qml_root}")
endif()
