# Content views' C++ (library brulion_content, src/app/content): the pure helpers (mention ->
# link edits, highlight positions, tags, sizes, Polish order) and the models against a real
# Workspace on a temporary vault — journals, backlinks grouped by page, unlinked mentions turned
# into links, tags, all pages, search, version history, the outline and the page tools.
brulion_add_test(tst_content_util SOURCES tst_content_util.cpp
    LIBS brulion_content brulion_core_markdown brulion_core_dates)
brulion_add_test(tst_content_models SOURCES tst_content_models.cpp
    LIBS brulion_content brulion_shell brulion_core_vault Qt6::Gui)

# The views in their real QML (qml-smoke.sh): every content view (journals, a page with its side
# panel and dialogs, all pages, tags, a tag, search, trash, history) at desktop and phone sizes
# with the sample notebook; QML warnings from the content files fail the test.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion AND NOT ANDROID AND NOT EMSCRIPTEN AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls")
    add_test(NAME tst_content_qml
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/qml-smoke.sh $<TARGET_FILE:brulion> ${CMAKE_CURRENT_BINARY_DIR}/qml-smoke)
    set_tests_properties(tst_content_qml PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 300 RUN_SERIAL TRUE)
endif()
