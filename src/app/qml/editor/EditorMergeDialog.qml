pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// EditorMergeDialog — "Compare and merge…" of the editor's conflict banner (BlockEditor): this
// device's version of the page and the other one side by side (BMergeView), the merge written as
// the page. Loaded from its file when first opened (docs/PERFORMANCE.md "QML files loaded when first
// used"). Its texts keep BlockEditor's translation context, where it was written first.
BDialog {
    id: dialog

    /// The editor (an Item: BlockEditor passes itself).
    property Item editorItem
    final property BlockEditor editor: editorItem as BlockEditor

    parent: T.Overlay.overlay
    title: qsTranslate("BlockEditor", "Merge changes")
    iconName: "git-merge"
    preferredWidth: Theme.dialogWidthLg
    showFooter: false
    BMergeView {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(560, (dialog.editor ? dialog.editor.height : 800) * 0.7)
        ours: dialog.opened && dialog.editor && dialog.editor.controller ? dialog.editor.controller.sourceText : ""
        theirs: dialog.opened && dialog.editor && dialog.editor.controller ? dialog.editor.controller.conflictText : ""
        oursLabel: qsTranslate("BlockEditor", "This device")
        theirsLabel: qsTranslate("BlockEditor", "Other version")
        onResolved: (text) => {
            dialog.editor.controller.resolveConflict(text)
            dialog.close()
        }
    }
}
