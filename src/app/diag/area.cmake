# Diagnostics area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths
# are relative to src/app): the event log's page ("Rejestr zdarzeń", route "logs") — the static
# library brulion_diag and its QML.
#
#   LogController   QML singleton `Logs`: copy / share (Android share sheet) / save (Downloads,
#                   Save as, browser download) / clear, the export header, the detailed mode
#   LogModel        the Logger's entries filtered by group, level, diacritic-insensitive search
#                   and a time window, following new entries
#   logexport.h     the export header, file name and clipboard cap (pure functions)
#   webselftest.h   the browser build's self-tests that need the running app (library
#                   brulion_webselftest): ?selftest=webpictures (pictures from the internet as the
#                   editor fetches them, an export with copies) and ?selftest=pen (the drawing
#                   editor's canvas under a pen) — tests/web, docs/WEB.md "Testy w przeglądarkach"
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

# brulion_webselftest: built on every platform (tests/app/diag runs it on the desktop), linked
# into the app for the browser only (main.cpp's WebAssembly block starts it). brulion_shell,
# brulion_ioviews and brulion_drawing come from later areas (CMake resolves target names when it
# generates the build).
qt_add_library(brulion_webselftest STATIC ${_brulion_diag_dir}/webselftest.h ${_brulion_diag_dir}/webselftest.cpp)
target_include_directories(brulion_webselftest PUBLIC ${_brulion_diag_dir})
target_link_libraries(brulion_webselftest
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick
    PRIVATE brulion_shell brulion_ioviews brulion_drawing brulion_core_drawing brulion_core_io brulion_core_vault)
brulion_set_warnings(brulion_webselftest)
if(EMSCRIPTEN)
    target_link_libraries(brulion PRIVATE brulion_webselftest)
endif()

# --- The app: QML registration and QML files ---------------------------------------------------
target_sources(brulion PRIVATE diag/diagqml.h)
target_include_directories(brulion PRIVATE ${_brulion_diag_dir})
target_link_libraries(brulion PRIVATE brulion_diag)

qt_target_qml_sources(brulion QML_FILES
    qml/diag/LogsView.qml
    qml/diag/LogRow.qml
)
