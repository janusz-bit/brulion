# Diagnostics area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths
# are relative to src/app): the event log's page ("Rejestr zdarzeń", route "logs") — the static
# library brulion_diag and its QML.
#
#   LogController   QML singleton `Logs`: copy / share (Android share sheet) / save (Downloads,
#                   Save as, browser download) / clear, the export header, the detailed mode
#   LogModel        the Logger's entries filtered by group, level, diacritic-insensitive search
#                   and a time window, following new entries
#   logexport.h     the export header, file name and clipboard cap (pure functions)
# The Logger itself is the core's (src/core/log), installed by main.cpp. Tests: tests/app/diag.

set(_brulion_diag_dir ${CMAKE_CURRENT_LIST_DIR})

qt_add_library(brulion_diag STATIC
    ${_brulion_diag_dir}/logexport.h ${_brulion_diag_dir}/logexport.cpp
    ${_brulion_diag_dir}/logmodel.h ${_brulion_diag_dir}/logmodel.cpp
    ${_brulion_diag_dir}/logcontroller.h ${_brulion_diag_dir}/logcontroller.cpp
)
target_include_directories(brulion_diag PUBLIC ${_brulion_diag_dir})
target_link_libraries(brulion_diag
    PUBLIC Qt6::Core Qt6::Gui brulion_core_log
    PRIVATE brulion_core_text brulion_ioviews brulion_platform)
if(EMSCRIPTEN)
    # QFileDialog::saveFileContent: the browser's download.
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    target_link_libraries(brulion_diag PRIVATE Qt6::Widgets)
endif()
qt_extract_metatypes(brulion_diag)
brulion_set_warnings(brulion_diag)

# --- The app: QML registration and QML files ---------------------------------------------------
target_sources(brulion PRIVATE diag/diagqml.h)
target_include_directories(brulion PRIVATE ${_brulion_diag_dir})
target_link_libraries(brulion PRIVATE brulion_diag)

qt_target_qml_sources(brulion QML_FILES
    qml/diag/LogsView.qml
    qml/diag/LogRow.qml
)
