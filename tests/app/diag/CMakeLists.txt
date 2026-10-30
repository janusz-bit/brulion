# Tests of the diagnostics area (library brulion_diag, src/app/diag): the event log page's model
# (filters, diacritic-insensitive search, following new entries, repeats, clearing) and the
# export (header, file name, clipboard cap, redaction through the whole path).
brulion_add_test(tst_diag_logmodel SOURCES tst_diag_logmodel.cpp LIBS brulion_diag brulion_core_log)
brulion_add_test(tst_diag_logexport SOURCES tst_diag_logexport.cpp LIBS brulion_diag brulion_core_log)
# The Polish catalog as the app build compiles it (the header test skips without it).
target_compile_definitions(tst_diag_logexport PRIVATE BRULION_TEST_PL_QM="${CMAKE_BINARY_DIR}/src/app/brulion_pl.qm")

# The browser build's self-tests (library brulion_webselftest, src/app/diag/webselftest.h; run in
# browsers by tests/web) on the desktop: a freehand line's facts, the texts a scene shows, pictures
# from the internet settling on the real Workspace and the export with copies after them (a tiny
# HTTP server on 127.0.0.1), a picture that never answers, the pen's report of a drawing canvas.
# Offscreen with the software scene graph.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion_webselftest AND EXISTS "${_brulion_qt_qml_root}/QtQuick")
    brulion_add_test(tst_webselftest SOURCES tst_webselftest.cpp ../minihttpserver.h
        LIBS brulion_webselftest brulion_shell brulion_drawing brulion_core_drawing Qt6::Quick Qt6::Qml Qt6::Network)
    set_tests_properties(tst_webselftest PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 120)
endif()
