pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// EditorToastAction — the button of a toast the block editor asks for (BlockEditor.notice() →
// message(text, kind, action)): its `text`, and run(), which the host calls when the button is
// pressed. `kind` says what run() does with the values kept here — what a closure did before the
// QML was compiled ahead of time (docs/UI.md "Compiled QML"):
//   "undoStep"       PageController.undoStep(token) while the page is still `path`
//                    (BlockEditor.undoAction(); else it says the page changed since)
//   "undoTokenEdit"  the editor's controller undoTokenEdit(undo) (a token edited in another file)
//   "urlToPicture"   the pasted picture address [from, to) of `row` made the picture, downloaded
//   "downloadWeb"    the pictures from the internet `urls` stored in the notebook
//   "streamUndo"     StreamFind.undo(token): a replace over days of the journal stream
//   "leaveReadOnly"  the editor's readOnlyLeaveRequested(): its host leaves reading mode
// Made by the editor (BlockEditor.toastAction()) as its child: a toast outliving the editor finds
// it gone, and nothing happens (the closure's try/catch made sure of that).
QtObject {
    id: action

    /// The editor as an Item, for the editor itself to pass (a file cannot give its own root to a
    /// property of its own type in compiled code); `editor` follows it unless set directly.
    property QtObject editorItem
    property BlockEditor editor: editorItem as BlockEditor
    property string text
    property string kind
    property PageController controller
    property string path
    property string token
    property int undo: 0
    property int row: -1
    property int from: 0
    property int to: 0
    property string url
    property list<string> urls
    property StreamFind stream: null

    function run(): void {
        const ctl = action.controller
        const ed = action.editor
        switch (action.kind) {
        case "undoStep":
            if (ctl === null)
                return // the page (or the whole editor) is gone: nothing to undo here
            if (ctl.path === action.path && ctl.undoStep(action.token))
                return
            if (ed !== null && ed.controller === ctl)
                ed.notice(qsTr("Not undone: the page has changed since"), "info", null)
            return
        case "undoTokenEdit":
            if (ed === null || ed.controller === null)
                return
            if (!ed.controller.undoTokenEdit(action.undo))
                ed.notice(qsTr("The page changed in the meantime; nothing was undone."), "info", null)
            return
        case "urlToPicture":
            if (ed === null)
                return
            if (!ed.samePage(ctl, action.path)
                    || ed.resultRow(ctl.urlToPicture(action.row, action.from, action.to, action.url, true)) < 0)
                ed.notice(qsTr("Not changed: the address was edited since"), "info", null)
            return
        case "downloadWeb":
            if (ed === null)
                return
            if (!ed.samePage(ctl, action.path)) {
                ed.notice(qsTr("Open that page again to save its pictures"), "info", null)
                return
            }
            for (let i = 0; i < action.urls.length; ++i)
                ed.downloadWebPictures(action.urls[i])
            return
        case "streamUndo": {
            if (action.stream === null)
                return
            const left = ed !== null ? ed.leftCount(action.stream.undo(action.undo)) : 0
            if (left > 0 && ed !== null)
                ed.notice(qsTr("Not undone in %n day(s): they changed since", "", left), "info", null)
            return
        }
        case "leaveReadOnly":
            if (ed !== null)
                ed.readOnlyLeaveRequested()
            return
        }
    }
}
