# Code mode area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths are
# relative to src/app): projects and AI agents' threads, as in T3 Code (docs/CODE.md). Its model is
# the shell's (shell/codeprojects.h, WorkspaceChats' threads, WorkspaceAgents' sessions in a
# project's folder); here its QML — the view of route "code" and the sidebar AppShell shows in Code
# mode. The Changes panel is the gitpanel area's, the Terminal panel the terminal area's;
# CodeRemoteChanges stands in for it for a project of another computer (read-only, from there).
qt_target_qml_sources(brulion
    QML_FILES
        qml/code/CodeView.qml
        qml/code/CodeSidebar.qml
        qml/code/CodeProjectDialog.qml
        qml/code/CodeRemoteChanges.qml
        qml/code/CodeProjectSection.qml
        qml/code/CodeThreadRow.qml
        qml/code/CodeScriptsEditor.qml
        qml/code/CodeBranchPicker.qml
        qml/code/CodePullRequestDialog.qml
        qml/code/CodeSetupBanner.qml
        qml/code/CodeOpenIn.qml
        qml/code/CodeThreadTitle.qml
)
