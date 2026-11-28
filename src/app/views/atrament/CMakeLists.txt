# brulion_views_atrament — the app's views drawn by Atrament beside the editor (docs/ATRAMENT.md
# "The views in Atrament (M3)"): TasksView over TaskListModel (ModelRows: a model's rows as a
# list's heights). Built where the editor drawn by
# Atrament is (src/app/views/area.cmake).
qt_add_library(brulion_views_atrament STATIC
    modelrows.h modelrows.cpp
    tasksview.h tasksview.cpp
)
target_include_directories(brulion_views_atrament PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_views_atrament PUBLIC brulion_planning brulion_editor_atrament brulion_atrament_ui Qt6::Core Qt6::Gui)
brulion_set_warnings(brulion_views_atrament)
