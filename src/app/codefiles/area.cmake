# The "Code" mode's project files (included by src/app/CMakeLists.txt in its scope, so relative paths
# are relative to src/app; docs/CODE-FILES.md): the static library brulion_codefiles and its QML.
#
#   FileIndexCache / CodeFileIndex   a folder's files and folders (core codefiles::FileIndex from
#                                    `git ls-files -z` or a walk, built on a pool thread, shared per
#                                    folder), ranked by fuzzy match for the picker and @-mentions
#   CodeFileDocument                 the file viewer's lines, read and coloured on a pool thread
#   CodeFileTable                    a CSV / TSV file of the viewer as a table (io::parseCsv)
#   CodeSearch                       the project search: rg --json, git grep, or Brulion's own
#   CodeFiles                        helpers: agents' file links, @-mentions, review comments
# QML (qml/codefiles): CodeFilesPanel (the right panel's "Files" tab: the viewer with its path, the
# recent files, Open in editor, Copy path), CodeFileView (the lines: numbers, colours, a line
# marked, lines selected → "Comment…"), CodeFilePicker (Ctrl+P), CodeSearchPanel (the "Search" tab,
# Ctrl+Shift+F), CodePanelTabs (Changes | Files | Search). Tests: tests/app/codefiles.

set(_brulion_codefiles_dir ${CMAKE_CURRENT_LIST_DIR})

qt_add_library(brulion_codefiles STATIC
    ${_brulion_codefiles_dir}/codefileindex.h ${_brulion_codefiles_dir}/codefileindex.cpp
    ${_brulion_codefiles_dir}/codefiledocument.h ${_brulion_codefiles_dir}/codefiledocument.cpp
    ${_brulion_codefiles_dir}/codefiletable.h ${_brulion_codefiles_dir}/codefiletable.cpp
    ${_brulion_codefiles_dir}/codesearch.h ${_brulion_codefiles_dir}/codesearch.cpp
    ${_brulion_codefiles_dir}/codefiles.h ${_brulion_codefiles_dir}/codefiles.cpp
    ${_brulion_codefiles_dir}/codefilesbackground_p.h
)
target_include_directories(brulion_codefiles PUBLIC ${_brulion_codefiles_dir})
target_link_libraries(brulion_codefiles PUBLIC Qt6::Core Qt6::Gui brulion_core_codefiles PRIVATE brulion_core_gitcli brulion_core_io)
qt_extract_metatypes(brulion_codefiles)
brulion_set_warnings(brulion_codefiles)

# --- The app: QML registration and QML files ---------------------------------------------------
target_sources(brulion PRIVATE codefiles/codefilesqml.h)
# The generated QML type registration includes the area's headers by file name.
target_include_directories(brulion PRIVATE ${_brulion_codefiles_dir})
target_link_libraries(brulion PRIVATE brulion_codefiles)

qt_target_qml_sources(brulion QML_FILES
    qml/codefiles/CodeFilesPanel.qml
    qml/codefiles/CodeFileView.qml
    qml/codefiles/CodeFileFindBar.qml
    qml/codefiles/CodeFilePicker.qml
    qml/codefiles/CodeSearchPanel.qml
    qml/codefiles/CodePanelTabs.qml
)
