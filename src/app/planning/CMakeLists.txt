# brulion_planning — the C++ side of the planning views (docs/UI.md: CalendarView, TasksView,
# GraphView): CalendarModel (month / week / agenda over reminders::calendarItems), TaskListModel
# (sections Zaległe … Zrobione, filters evaluated by the query engine, the equivalent ```query),
# ForceLayout (deterministic Barnes–Hut force layout) run on a worker thread by GraphSimulation
# (inline in slices in the browser) + GraphModel (the link graph, local graph, toggles, cap) +
# GraphItem (zoom / pan / drag / pinch; drawn through the scene graph — vertex-antialiased meshes
# from graphgeometry, QSGTextNode labels, GraphSceneNode — or with QPainter into an image node
# under the software renderer), and PlanningActions (task toggles with undo and journal appends
# through an EditorHost).
# A static library so tests/app/planning links it without the app; the app's QML module
# "Brulion" registers the classes through QML_FOREIGN wrappers (planningqml.h, included by
# area.cmake), which needs the metatypes extracted below. Models take a VaultIndex directly.
qt_add_library(brulion_planning STATIC
    planningutil.h planningutil.cpp
    calendarmodel.h calendarmodel.cpp
    tasklistmodel.h tasklistmodel.cpp
    forcelayout.h forcelayout.cpp
    graphsimulation.h graphsimulation.cpp
    graphgeometry.h graphgeometry.cpp
    graphscene.h graphscene.cpp
    graphmodel.h graphmodel.cpp
    graphitem.h graphitem.cpp
    planningactions.h planningactions.cpp
)
target_include_directories(brulion_planning PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_planning
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick brulion_editorhost brulion_core_index brulion_core_reminders
           brulion_core_markdown brulion_core_dates
    PRIVATE brulion_core_text brulion_core_util brulion_core_notebook)
qt_extract_metatypes(brulion_planning)
brulion_set_warnings(brulion_planning)
