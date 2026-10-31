# Editor area of the app (included by src/app/CMakeLists.txt): the brulion_editor library
# (src/app/editor/CMakeLists.txt), its QML registration (editorforeign.h) and the block editor's
# QML (qml/editor) plus the editor playground (`brulion --editor`, qml/dev/EditorPlayground.qml)
# and PageScript (qml/dev/PageScript.qml: `--editor-script` states of the editor in the real app).
add_subdirectory(editor)
target_link_libraries(brulion PRIVATE brulion_editor)
# The generated QML type registration includes the editor headers by file name.
target_include_directories(brulion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/editor)
target_sources(brulion PRIVATE editor/editorforeign.h)
qt_target_qml_sources(brulion
    QML_FILES
        qml/editor/BlockEditor.qml
        qml/editor/BlockDelegate.qml
        qml/editor/BlockMenu.qml
        qml/editor/BlockText.qml
        qml/editor/InlineEditor.qml
        qml/editor/TaskBox.qml
        qml/editor/TaskStatusMenu.qml
        qml/editor/DateChipMenu.qml
        qml/editor/LinkMenu.qml
        qml/editor/WebPictureMenu.qml
        qml/editor/WebPictureViewer.qml
        qml/editor/CodeBlockView.qml
        qml/editor/CalcBlockView.qml
        qml/editor/TableBlockView.qml
        qml/editor/QueryBlockView.qml
        qml/editor/MathBlockView.qml
        qml/editor/FrontmatterView.qml
        qml/editor/PropertiesPanel.qml
        qml/editor/PropertyRow.qml
        qml/editor/PropertyListValue.qml
        qml/editor/PropertyMenu.qml
        qml/editor/PropertyChipMenu.qml
        qml/editor/EmbedView.qml
        qml/editor/TransclusionView.qml
        qml/editor/SuggestionPopup.qml
        qml/editor/EditorDialogs.qml
        qml/editor/MobileEditToolbar.qml
        qml/editor/SourceEditor.qml
        qml/editor/ZoomBreadcrumb.qml
        qml/editor/SelectionToolbar.qml
)
# Developer QML (BRULION_DEV_QML, src/app/CMakeLists.txt); PageView loads a PageScript either way.
if(BRULION_DEV_QML_ENABLED)
    qt_target_qml_sources(brulion QML_FILES qml/dev/EditorPlayground.qml qml/dev/PageScript.qml
        qml/dev/SelectionSelfTest.qml qml/dev/RefreshSelfTest.qml qml/dev/DateChipSelfTest.qml
        qml/dev/PropertiesSelfTest.qml)
else()
    qt_target_qml_sources(brulion QML_FILES qml/stubs/PageScript.qml)
endif()
