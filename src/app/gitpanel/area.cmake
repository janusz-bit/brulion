# The "Code" mode's git area (included by src/app/CMakeLists.txt in its scope, so relative paths are
# relative to src/app; docs/CODE-GIT.md): the static library brulion_gitpanel and its QML.
#
#   CodeGit   one folder's git for the changes panel: branch, upstream, ahead / behind, the working
#             tree's changes (and those since a turn's checkpoint), a file's two texts for BDiffView,
#             commit / push / pull, branches, pull requests (gh), worktrees, checkpoints — all
#             asynchronous through the core module gitcli (src/core/gitcli), never blocking the UI
# QML (qml/gitpanel): GitChangesPanel (the panel: header, list, diff, footer), GitChangeRow (a file's
# row), GitCommitDialog (Commit / Commit and push / Commit, push and create PR),
# GitPullRequestDialog. Tests: tests/app/gitpanel.

set(_brulion_gitpanel_dir ${CMAKE_CURRENT_LIST_DIR})

qt_add_library(brulion_gitpanel STATIC
    ${_brulion_gitpanel_dir}/codegit.h ${_brulion_gitpanel_dir}/codegit.cpp
)
target_include_directories(brulion_gitpanel PUBLIC ${_brulion_gitpanel_dir})
target_link_libraries(brulion_gitpanel PUBLIC Qt6::Core brulion_core_gitcli)
qt_extract_metatypes(brulion_gitpanel)
brulion_set_warnings(brulion_gitpanel)

# --- The app: QML registration and QML files ---------------------------------------------------
target_sources(brulion PRIVATE gitpanel/gitpanelqml.h)
# The generated QML type registration includes the area's headers by file name.
target_include_directories(brulion PRIVATE ${_brulion_gitpanel_dir})
target_link_libraries(brulion PRIVATE brulion_gitpanel)

qt_target_qml_sources(brulion QML_FILES
    qml/gitpanel/GitChangesPanel.qml
    qml/gitpanel/GitChangeRow.qml
    qml/gitpanel/GitCommitDialog.qml
    qml/gitpanel/GitPullRequestDialog.qml
)
