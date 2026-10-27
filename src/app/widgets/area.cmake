# Android home-screen widgets (included by src/app/CMakeLists.txt in its scope, so relative paths
# are relative to src/app): the library brulion_widgets (widgets/CMakeLists.txt — the snapshot and
# the queued quick-add entries, built everywhere for the tests) and, on Android only, the
# publisher main.cpp creates (widgetpublisher.h: the snapshot to AndroidBridge::updateWidgets(),
# the entries from AndroidBridge::takeWidgetActions() into today's journal). The widgets
# themselves are Kotlin (android/src/…/Widget*.kt, QuickAddActivity.kt). Tests: tests/app/widgets.
add_subdirectory(widgets)
if(ANDROID)
    target_sources(brulion PRIVATE widgets/widgetpublisher.h widgets/widgetpublisher.cpp)
    target_include_directories(brulion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/widgets)
    target_link_libraries(brulion PRIVATE brulion_widgets brulion_shell brulion_platform)
endif()
