# Content views' C++ (library brulion_content, src/app/content): the pure helpers (mention ->
# link edits, highlight positions, tags, sizes, Polish order) and the models against a real
# Workspace on a temporary vault — journals, backlinks grouped by page, unlinked mentions turned
# into links, tags, all pages, search (also of the attachments: a PDF of tests/data), version
# history, the outline and the page tools.
brulion_add_test(tst_content_util SOURCES tst_content_util.cpp
    LIBS brulion_content brulion_core_markdown brulion_core_dates brulion_core_index)
brulion_add_test(tst_content_models SOURCES tst_content_models.cpp
    LIBS brulion_content brulion_shell brulion_core_vault Qt6::Gui brulion_platform brulion_attachments)
# Search by meaning as the app does it (Workspace.semantic + SearchResultModel) with a fake
# embedding model: full text without the plugin, the model or the setting; the notebook embedded in
# the background and fused with the full-text hits; edits, deletions, the vectors kept and read
# back; a failing model; removal; the download when it is turned on (a local HTTP server).
brulion_add_test(tst_content_semantic SOURCES tst_content_semantic.cpp
    LIBS brulion_content brulion_shell brulion_core_semantic brulion_core_ai brulion_test_timing Qt6::Gui Qt6::Network)
# The AI chat on a real Workspace, the model on the device played (tst_content_chat.cpp): a
# conversation's page, its turns, a change asked in it, Stop, a failure, the messages and the list;
# times, pictures, turns side by side (an OpenAI-compatible server played: ../fakeaiserver.h), the
# device's model's line, a long conversation cut by turns, a partner of one's own, rename, delete;
# answers reported or hidden (Report response) and the report's draft.
brulion_add_test(tst_content_chat SOURCES tst_content_chat.cpp ../fakeaiserver.h
    LIBS brulion_content brulion_shell brulion_core_vault brulion_core_ai brulion_core_aireport Qt6::Gui Qt6::Network)

# The views in their real QML (qml-smoke.sh): every content view (journals, a page with its side
# panel and dialogs, all pages, tags, a tag, search, trash, history) at desktop and phone sizes
# with the sample notebook; QML warnings from the content files fail the test, and so does a run
# that never gets to its screenshot (each run is bounded: a layout loop fails its route quickly).
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion AND NOT ANDROID AND NOT EMSCRIPTEN AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls")
    math(EXPR _content_qml_timeout "300 + 10 * ${BRULION_TEST_SLOWDOWN}")
    math(EXPR _content_qml_run_timeout "30 + 10 * ${BRULION_TEST_SLOWDOWN}")
    # Windows: qml-smoke.sh is a bash script (not run there; the self-tests below are).
    if(NOT WIN32)
        add_test(NAME tst_content_qml
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/qml-smoke.sh $<TARGET_FILE:brulion> ${CMAKE_CURRENT_BINARY_DIR}/qml-smoke)
        set_tests_properties(tst_content_qml PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH};BRULION_SMOKE_RUN_TIMEOUT=${_content_qml_run_timeout}"
            TIMEOUT ${_content_qml_timeout})
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
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH};XDG_DATA_HOME=${_journals_home}/data;XDG_CONFIG_HOME=${_journals_home}/config;XDG_CACHE_HOME=${_journals_home}/cache;XDG_STATE_HOME=${_journals_home}/state${_profile}"
            FIXTURES_REQUIRED content_journals_home TIMEOUT 300 PROCESSORS 2
            FAIL_REGULAR_EXPRESSION "SELFTEST FAIL;TypeError;ReferenceError;Binding loop detected")
        # The same with the days that are not edited drawn as one item each (BRULION_JOURNAL_DAY_ITEM=1,
        # docs/UI.md "A journal day as one item"): every step above, and the drawn rows checked
        # against the editor's (an outline and a document day), a tap's caret, a task box, a fold.
        set(_journals_item_home ${CMAKE_CURRENT_BINARY_DIR}/journals-dayitem-selftest-home)
        brulion_test_profile(_item_profile content-journals-dayitem)
        add_test(NAME tst_content_journals_qml_dayitem_home
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${_journals_item_home} ${_item_profile_DIRS})
        set_tests_properties(tst_content_journals_qml_dayitem_home PROPERTIES FIXTURES_SETUP content_journals_dayitem_home)
        add_test(NAME tst_content_journals_qml_dayitem
            COMMAND brulion --demo --route journals --editor-script journals-selftest --no-single-instance
                --size 412x915 --touch --now "2026-10-14 10:00" --locale pl)
        set_tests_properties(tst_content_journals_qml_dayitem PROPERTIES
            ENVIRONMENT "BRULION_JOURNAL_DAY_ITEM=1;QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH};XDG_DATA_HOME=${_journals_item_home}/data;XDG_CONFIG_HOME=${_journals_item_home}/config;XDG_CACHE_HOME=${_journals_item_home}/cache;XDG_STATE_HOME=${_journals_item_home}/state${_item_profile}"
            FIXTURES_REQUIRED content_journals_dayitem_home TIMEOUT 300 PROCESSORS 2
            FAIL_REGULAR_EXPRESSION "SELFTEST FAIL;TypeError;ReferenceError;Binding loop detected")

        # Blocks selected across several days (`--editor-script journals-span-selftest`,
        # qml/content/ContentJournalsSpanSelfTest.qml): Shift+click, Shift+arrows past a day's
        # edge, a mouse drag, Esc on a desktop; a long press, taps, the docked tools and Back on a
        # phone; copying with the days' links, other tools changing nothing, days scrolled away and
        # back. QtTest's TestEvent gives it real mouse and touch input.
        if(EXISTS "${_brulion_qt_qml_root}/QtTest")
            # (…_dayitem: the same with BRULION_JOURNAL_DAY_ITEM=1 — the days not edited drawn as one
            # item each: the clicks, taps and long presses go through it.)
            foreach(_span "tst_content_journals_span_qml;1280x800;;" "tst_content_journals_span_qml_touch;412x915;--touch;"
                          "tst_content_journals_span_qml_dayitem;1280x800;;BRULION_JOURNAL_DAY_ITEM=1;"
                          "tst_content_journals_span_qml_touch_dayitem;412x915;--touch;BRULION_JOURNAL_DAY_ITEM=1;")
                list(GET _span 0 _span_test)
                list(GET _span 1 _span_size)
                list(GET _span 2 _span_touch)
                list(GET _span 3 _span_env)
                if(_span_env)
                    string(APPEND _span_env ";")
                endif()
                set(_span_home ${CMAKE_CURRENT_BINARY_DIR}/${_span_test}-home)
                brulion_test_profile(_span_profile ${_span_test})
                add_test(NAME ${_span_test}_home COMMAND ${CMAKE_COMMAND} -E rm -rf ${_span_home} ${_span_profile_DIRS})
                set_tests_properties(${_span_test}_home PROPERTIES FIXTURES_SETUP ${_span_test}_home)
                add_test(NAME ${_span_test}
                    COMMAND brulion --demo --route journals --editor-script journals-span-selftest --no-single-instance
                        --size ${_span_size} ${_span_touch} --now "2026-10-14 10:00" --locale pl)
                set_tests_properties(${_span_test} PROPERTIES
                    ENVIRONMENT "${_span_env}QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH};XDG_DATA_HOME=${_span_home}/data;XDG_CONFIG_HOME=${_span_home}/config;XDG_CACHE_HOME=${_span_home}/cache;XDG_STATE_HOME=${_span_home}/state${_span_profile}"
                    FIXTURES_REQUIRED ${_span_test}_home TIMEOUT 300 PROCESSORS 2
                    FAIL_REGULAR_EXPRESSION "SELFTEST FAIL;TypeError;ReferenceError;Binding loop detected")
            endforeach()
        endif()
    endif()
endif()
