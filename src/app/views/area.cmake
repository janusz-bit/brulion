# Views drawn by Atrament (docs/ATRAMENT.md "The views in Atrament (M3)"; included by
# src/app/CMakeLists.txt in its scope, after the editor's and the planning areas): the
# brulion_views_atrament library — the Tasks view's list (views/atrament/tasksview.h) — where the
# block editor drawn by Atrament is built, its rendered spans and icons shared. AtramentView
# (ui/atramentview.cpp, part of the app) shows it as `view: "tasks"` (BRULION_HAS_ATRAMENT_VIEWS).
if(TARGET brulion_editor_atrament AND TARGET brulion_planning)
    add_subdirectory(views/atrament)
    target_link_libraries(brulion PRIVATE brulion_views_atrament)
    target_compile_definitions(brulion PRIVATE BRULION_HAS_ATRAMENT_VIEWS)
    # TasksView loads them by file name where they exist (they name AtramentView).
    qt_target_qml_sources(brulion QML_FILES qml/planning/TasksAtramentList.qml)
    if(BRULION_DEV_QML_ENABLED)
        qt_target_qml_sources(brulion QML_FILES qml/dev/TasksAtramentSelfTest.qml)
    endif()
endif()
