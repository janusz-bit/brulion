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
        qml/editor/BlockPartBullet.qml
        qml/editor/BlockPartCalc.qml
        qml/editor/BlockPartCallout.qml
        qml/editor/BlockPartChart.qml
        qml/editor/BlockPartCode.qml
        qml/editor/BlockPartDateButton.qml
        qml/editor/BlockPartEmbeds.qml
        qml/editor/BlockPartFlash.qml
        qml/editor/BlockPartFoldArrow.qml
        qml/editor/BlockPartFrontmatter.qml
        qml/editor/BlockPartMath.qml
        qml/editor/BlockPartMermaid.qml
        qml/editor/BlockPartNumber.qml
        qml/editor/BlockPartQuery.qml
        qml/editor/BlockPartRule.qml
        qml/editor/BlockPartSelectionBand.qml
        qml/editor/BlockPartTable.qml
        qml/editor/BlockPartTagTint.qml
        qml/editor/BlockPartTaskBox.qml
        qml/editor/BlockPartTransclusions.qml
        qml/editor/BlockPartView.qml
        qml/editor/BlockMenuTap.qml
        qml/editor/BlockSelectionGrip.qml
        qml/editor/BlockPartGripStart.qml
        qml/editor/BlockPartGripEnd.qml
        qml/editor/BlockMenu.qml
        qml/editor/BlockText.qml
        qml/editor/EditorToastAction.qml
        qml/editor/EditorScroller.qml
        qml/editor/InlineEditor.qml
        qml/editor/TaskBox.qml
        qml/editor/TaskStatusMenu.qml
        qml/editor/TaskDependencyDialog.qml
        qml/editor/DateChipMenu.qml
        qml/editor/QuickDateMenu.qml
        qml/editor/LinkMenu.qml
        qml/editor/WebPictureMenu.qml
        qml/editor/WebPictureViewer.qml
        qml/editor/CodeBlockView.qml
        qml/editor/CalcBlockView.qml
        qml/editor/TableBlockView.qml
        qml/editor/QueryBlockView.qml
        qml/editor/DatabaseView.qml
        qml/editor/MathBlockView.qml
        qml/editor/MermaidBlockView.qml
        qml/editor/ChartBlockView.qml
        qml/editor/ViewBlockView.qml
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
        qml/editor/EditorMergeDialog.qml
        qml/editor/EncryptedTextDialog.qml
        qml/editor/MobileEditToolbar.qml
        qml/editor/SourceEditor.qml
        qml/editor/FindBar.qml
        qml/editor/ZoomBreadcrumb.qml
        qml/editor/VimStatus.qml
        qml/editor/SelectionToolbar.qml
        qml/editor/WritingDialog.qml
        qml/editor/PageItemView.qml
)
# The page drawn by Atrament (AppSettings.editorAtrament; docs/ATRAMENT.md M2), where Atrament and
# its AtramentView are built: BlockEditor loads it by its file name.
if(TARGET brulion_editor_atrament AND TARGET brulion_atrament_gallery)
    qt_target_qml_sources(brulion QML_FILES qml/editor/EditorAtramentPage.qml)
endif()
# Developer QML (BRULION_DEV_QML, src/app/CMakeLists.txt); PageView loads PageScript from its file
# when `--editor-script` asks for it.
if(BRULION_DEV_QML_ENABLED)
    qt_target_qml_sources(brulion QML_FILES qml/dev/EditorPlayground.qml qml/dev/PageScript.qml
        qml/dev/SelectionSelfTest.qml qml/dev/RefreshSelfTest.qml qml/dev/DateChipSelfTest.qml
        qml/dev/PropertiesSelfTest.qml qml/dev/LinkPartsSelfTest.qml qml/dev/ReadingSelfTest.qml qml/dev/TableSelfTest.qml
        qml/dev/SpellingSelfTest.qml qml/dev/AtramentSelfTest.qml qml/dev/AtramentTableSelfTest.qml)
endif()
