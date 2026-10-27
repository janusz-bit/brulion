# brulion_widgets — the C++ side of the Android home-screen widgets (docs/ANDROID.md "Widgety"):
# the snapshot they show (widgetsnapshot.h: TasksView's tasks, the calendar's agenda, favourite and
# recent pages, where quick add writes) and the entries quick add queued for the app
# (widgetactions.h). Pure and built on every platform, so tests/app/widgets covers it on the
# desktop; the Android publisher (widgetpublisher.h) is compiled into the app by area.cmake.
qt_add_library(brulion_widgets STATIC
    widgetsnapshot.h widgetsnapshot.cpp
    widgetactions.h widgetactions.cpp
)
target_include_directories(brulion_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_widgets
    PUBLIC Qt6::Core brulion_planning brulion_core_index brulion_core_dates
    PRIVATE brulion_core_notebook brulion_core_vault)
brulion_set_warnings(brulion_widgets)
