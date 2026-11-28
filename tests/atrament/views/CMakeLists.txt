# The app's views drawn by Atrament (src/app/views/atrament): their models over an in-memory
# VaultIndex (tests/app/planning/planningtest.h) on Atrament's test platform.
brulion_add_test(tst_atrament_tasks SOURCES tst_atrament_tasks.cpp LIBS brulion_views_atrament)
target_include_directories(tst_atrament_tasks PRIVATE ${PROJECT_SOURCE_DIR}/tests/app/planning)
target_compile_definitions(tst_atrament_tasks PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
brulion_add_test(tst_atrament_search SOURCES tst_atrament_search.cpp LIBS brulion_views_atrament Qt6::Gui)
target_compile_definitions(tst_atrament_search PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# The Tasks view's rows drawn by Atrament in the real app (`--atrament-views`,
# qml/dev/TasksAtramentSelfTest.qml): a task's box ticking it off and on in its file, its status
# menu, a header folding its section, ↓ / Space / Enter. A fresh home each run.
if(TARGET brulion AND BRULION_DEV_QML_ENABLED AND NOT ANDROID AND NOT EMSCRIPTEN)
    set(_views_home ${CMAKE_CURRENT_BINARY_DIR}/views-atrament-home)
    brulion_test_profile(_views_profile views-atrament)
    add_test(NAME tst_views_qml_atrament_home COMMAND ${CMAKE_COMMAND} -E rm -rf ${_views_home} ${_views_profile_DIRS})
    set_tests_properties(tst_views_qml_atrament_home PROPERTIES FIXTURES_SETUP views_atrament_home)
    add_test(NAME tst_views_qml_atrament
        COMMAND brulion --demo --route tasks --editor-script tasks-atrament-selftest --atrament-views
            --no-single-instance --size 1000x800 --now "2026-10-14 10:00" --locale pl)
    set_tests_properties(tst_views_qml_atrament PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH};XDG_DATA_HOME=${_views_home}/data;XDG_CONFIG_HOME=${_views_home}/config;XDG_CACHE_HOME=${_views_home}/cache;XDG_STATE_HOME=${_views_home}/state${_views_profile}"
        FIXTURES_REQUIRED views_atrament_home TIMEOUT 300 PROCESSORS 2
        FAIL_REGULAR_EXPRESSION "SELFTEST FAIL;TypeError;ReferenceError;Binding loop detected")
    # The Search view's results drawn by Atrament (qml/dev/SearchAtramentSelfTest.qml): a query typed,
    # ↓ in the field, a result clicked.
    set(_search_home ${CMAKE_CURRENT_BINARY_DIR}/views-search-atrament-home)
    brulion_test_profile(_search_profile views-search-atrament)
    add_test(NAME tst_views_qml_atrament_search_home COMMAND ${CMAKE_COMMAND} -E rm -rf ${_search_home} ${_search_profile_DIRS})
    set_tests_properties(tst_views_qml_atrament_search_home PROPERTIES FIXTURES_SETUP views_search_atrament_home)
    add_test(NAME tst_views_qml_atrament_search
        COMMAND brulion --demo --route search --editor-script search-atrament-selftest --atrament-views
            --no-single-instance --size 1000x800 --now "2026-10-14 10:00" --locale pl)
    set_tests_properties(tst_views_qml_atrament_search PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH};XDG_DATA_HOME=${_search_home}/data;XDG_CONFIG_HOME=${_search_home}/config;XDG_CACHE_HOME=${_search_home}/cache;XDG_STATE_HOME=${_search_home}/state${_search_profile}"
        FIXTURES_REQUIRED views_search_atrament_home TIMEOUT 300 PROCESSORS 2
        FAIL_REGULAR_EXPRESSION "SELFTEST FAIL;TypeError;ReferenceError;Binding loop detected")
endif()
