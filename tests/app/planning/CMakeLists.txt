# Planning views' C++ (library brulion_planning, src/app/planning): CalendarModel (month / week /
# agenda grids, week starts, DST months, recurring entries, overdue), TaskListModel (sections,
# filters, the equivalent query, sorting, incremental updates), PlanningActions (task toggles,
# undo, journal appends, quick add) and the graph (ForceLayout determinism and convergence,
# GraphModel toggles / local graph / cap, the worker thread and its inline fallback, the
# scene-graph geometry, GraphItem hit-testing, camera and painting with 2000 nodes, and in a window
# with the scene graph and the software renderer). Offscreen (QGuiApplication: GraphItem is a Qt
# Quick item).
brulion_add_test(tst_planning_calendar SOURCES tst_planning_calendar.cpp planningtest.h LIBS brulion_planning)
brulion_add_test(tst_planning_tasks SOURCES tst_planning_tasks.cpp planningtest.h LIBS brulion_planning)
brulion_add_test(tst_planning_graph SOURCES tst_planning_graph.cpp planningtest.h LIBS brulion_planning)
# GraphItem in a real window: the scene-graph renderer on the RHI null backend, and (a second entry
# of the same binary) the software renderer's QPainter image node.
brulion_add_test(tst_planning_graph_sg SOURCES tst_planning_graph_sg.cpp planningtest.h LIBS brulion_planning)
add_test(NAME tst_planning_graph_sg_software COMMAND tst_planning_graph_sg)
math(EXPR _planning_sg_function_timeout_ms "300000 * ${BRULION_TEST_SLOWDOWN}")
set_tests_properties(tst_planning_graph_sg_software PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QTEST_FUNCTION_TIMEOUT=${_planning_sg_function_timeout_ms}")
if(TARGET brulion_editor)
    # FakeEditorHost (the editor's in-memory EditorHost) stands in for the Workspace.
    brulion_add_test(tst_planning_actions SOURCES tst_planning_actions.cpp planningtest.h
        LIBS brulion_planning brulion_editor)
endif()

# The views in their real QML (qml-smoke.sh): every planning view and its dialogs at desktop and
# phone sizes with the sample notebook; QML warnings from the planning files fail the test.
# Qt's QML modules are found as for tst_editor_qml (tests/app/editor/CMakeLists.txt). Not on
# Windows: qml-smoke.sh is a bash script.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion AND NOT ANDROID AND NOT EMSCRIPTEN AND NOT WIN32 AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls")
    math(EXPR _planning_qml_timeout "240 + 10 * ${BRULION_TEST_SLOWDOWN}")
    add_test(NAME tst_planning_qml
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/qml-smoke.sh $<TARGET_FILE:brulion> ${CMAKE_CURRENT_BINARY_DIR}/qml-smoke)
    set_tests_properties(tst_planning_qml PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT ${_planning_qml_timeout})
endif()
