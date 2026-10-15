# Planning area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths
# are relative to src/app): the brulion_planning library (planning/CMakeLists.txt), its QML
# registration (planning/planningqml.h: CalendarModel, TaskListModel, GraphModel, GraphItem,
# PlanningActions, the PlanningContext singleton that hands the Workspace's index to the views)
# and the views' sub-components in qml/planning. CalendarView.qml, TasksView.qml and
# GraphView.qml themselves are registered with the other views (shell/views.cmake).

# EditorHost (editor/editorhost.h) must be moc-ed exactly once: the editor area (included
# before this one) provides brulion_editorhost; define it here only when that area is absent.
if(NOT TARGET brulion_editorhost)
    qt_add_library(brulion_editorhost STATIC ${CMAKE_CURRENT_SOURCE_DIR}/editor/editorhost.h)
    set_target_properties(brulion_editorhost PROPERTIES LINKER_LANGUAGE CXX)
    target_include_directories(brulion_editorhost PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/editor)
    target_link_libraries(brulion_editorhost PUBLIC Qt6::Core)
    qt_extract_metatypes(brulion_editorhost)
    brulion_set_warnings(brulion_editorhost)
endif()

add_subdirectory(planning)
target_link_libraries(brulion PRIVATE brulion_planning)
# The generated QML type registration includes the planning headers by file name.
target_include_directories(brulion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/planning)
target_sources(brulion PRIVATE planning/planningqml.h)
qt_target_qml_sources(brulion
    QML_FILES
        qml/planning/PlanningItemRow.qml
        qml/planning/CalendarHeader.qml
        qml/planning/CalendarMonthGrid.qml
        qml/planning/CalendarWeekGrid.qml
        qml/planning/CalendarAgendaList.qml
        qml/planning/CalendarDayPanel.qml
        qml/planning/CalendarAddDialog.qml
        qml/planning/TasksFilterBar.qml
        qml/planning/TasksQuickAdd.qml
        qml/planning/TasksRow.qml
        qml/planning/TasksQueryDialog.qml
        qml/planning/GraphToolbar.qml
        qml/planning/GraphNodeCard.qml
)
