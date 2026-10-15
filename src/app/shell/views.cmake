# The views shown for routes (docs/UI.md "Navigation"; mapping in qml/shell/ShellRouteView.qml).
# OnboardingView and SettingsView belong to the shell; the others are placeholders until the
# views are written. Whoever takes the views over may move this list into their own area
# fragment (src/app/views/area.cmake) — then delete this file and its include in area.cmake
# (a QML file must be registered once).
qt_target_qml_sources(brulion QML_FILES
    qml/views/OnboardingView.qml
    qml/views/SettingsView.qml
    qml/views/AllPagesView.qml
    qml/views/CalendarView.qml
    qml/views/GraphView.qml
    qml/views/HistoryView.qml
    qml/views/ImportExportView.qml
    qml/views/JournalsView.qml
    qml/views/PageView.qml
    qml/views/SearchView.qml
    qml/views/SyncView.qml
    qml/views/TagsView.qml
    qml/views/TasksView.qml
    qml/views/TrashView.qml
)
