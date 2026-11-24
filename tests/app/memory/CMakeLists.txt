# What keeps the app's memory down while pictures and notes are opened (library brulion_ui_memory,
# src/app/ui; docs/PERFORMANCE.md "Pictures and memory"): NearView — items of an offscreen window
# in, near and far from its view, hidden and shown, as a scrolled page moves them, and pictures of
# a QML scene loaded and let go by it; MemoryKeeper — when freed memory is given back to the
# system; PictureDecode — the size a picture is decoded at.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion_ui_memory)
    brulion_add_test(tst_memory_keeper
        SOURCES tst_memory_keeper.cpp
        LIBS brulion_ui_memory brulion_platform)
    if(EXISTS "${_brulion_qt_qml_root}/QtQuick")
        brulion_add_test(tst_near_view
            SOURCES tst_near_view.cpp
            LIBS brulion_ui_memory brulion_platform Qt6::Qml Qt6::Quick)
        set_tests_properties(tst_near_view PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH}"
            TIMEOUT 120)
    endif()
endif()
