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
    # Windows: qml-smoke.sh is a bash script (not run there; the self-tests below are).
    if(NOT WIN32)
        add_test(NAME tst_content_qml
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/qml-smoke.sh $<TARGET_FILE:brulion> ${CMAKE_CURRENT_BINARY_DIR}/qml-smoke)
        set_tests_properties(tst_content_qml PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
            TIMEOUT 300 RUN_SERIAL TRUE)
    endif()

    # The journal stream's data safety (`--editor-script journals-selftest`,
    # qml/content/ContentJournalsSelfTest.qml, developer QML: BRULION_DEV_QML): days edited and
    # scrolled away at once (also with a word still being composed) keep their text, midnight
    # while editing. A fresh home each run.
    if(BRULION_DEV_QML_ENABLED)
        set(_journals_home ${CMAKE_CURRENT_BINARY_DIR}/journals-selftest-home)
        brulion_test_profile(_profile content-journals)
        add_test(NAME tst_content_journals_qml_home COMMAND ${CMAKE_COMMAND} -E rm -rf ${_journals_home} ${_profile_DIRS})
        set_tests_properties(tst_content_journals_qml_home PROPERTIES FIXTURES_SETUP content_journals_home)
        add_test(NAME tst_content_journals_qml
            COMMAND brulion --demo --route journals --editor-script journals-selftest --no-single-instance
                --size 412x915 --touch --now "2026-10-14 10:00" --locale pl)
        set_tests_properties(tst_content_journals_qml PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root};XDG_DATA_HOME=${_journals_home}/data;XDG_CONFIG_HOME=${_journals_home}/config;XDG_CACHE_HOME=${_journals_home}/cache;XDG_STATE_HOME=${_journals_home}/state${_profile}"
            FIXTURES_REQUIRED content_journals_home TIMEOUT 300 RUN_SERIAL TRUE
            FAIL_REGULAR_EXPRESSION "SELFTEST FAIL;TypeError;ReferenceError;Binding loop detected")
    endif()
endif()
