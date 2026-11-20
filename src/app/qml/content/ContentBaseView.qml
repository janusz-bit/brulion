pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ContentBaseView — an Obsidian base (a `.base` file, docs/SYNTAX.md §10 "Obsidian Bases") opened
// as a page: its name, its views as tabs, and the view shown (DatabaseView over the file: cells
// edit the notes, a card dragged to another column writes its property, the columns and the sort
// changed in the header are written into the .base file — those lines only). An edit of a note
// or of the base gets a toast with Undo (ViewResultModel.undoLast(): while the file holds what was
// written). The page's editor stays unloaded (PageView: the YAML never reaches the block editor);
// "Show as text" in the page menu is the file read as text.
//
//   ContentBaseView { path: routePath; controller: page }
Item {
    id: root

    objectName: "baseView" // (the shell's self-test)

    property string path
    /// The page's controller (renderer colours, the host); its own page stays unloaded.
    property PageController controller
    /// A view by its name (`![[Books.base#Reading]]` opened as a link).
    property string viewName

    readonly property string fileName: Js.replaceRegExp(Js.replaceRegExp(root.path, "^.*/", "", ""), "\\.base$", "i", "")
    /// The toast whose Undo puts the last edit back.
    property int undoToast: -1
    property ViewResultModel undoModel: null

    Connections {
        target: AppController
        function onToastActionTriggered(id: int) {
            if (id === root.undoToast && root.undoModel !== null && !root.undoModel.undoLast())
                AppController.toast(qsTr("Not undone: the file has changed since"), "info")
        }
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.height + 2 * Theme.space6
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        interactive: !view.results.held

        Column {
            id: body
            x: Theme.narrow ? Theme.space4 : Theme.space8
            y: Theme.space6
            width: scroller.width - 2 * x
            spacing: Theme.space4

            Row {
                spacing: Theme.space2
                BIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "table"
                    size: Theme.iconSize
                    color: Theme.fgMuted
                }
                Text {
                    text: root.fileName
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXl
                    font.weight: Theme.weightSemiBold
                    color: Theme.fg
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, body.width - Theme.iconSize - Theme.space2)
                }
            }

            // The base's views.
            Flickable {
                width: body.width
                height: tabs.height
                visible: view.results.viewNames.length > 1
                contentWidth: tabs.width
                contentHeight: tabs.height
                interactive: contentWidth > width
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                clip: true
                Row {
                    id: tabs
                    spacing: Theme.space1
                    Repeater {
                        model: view.results.viewNames
                        delegate: BButton {
                            required property int index
                            required property string modelData
                            objectName: "baseViewTab" + index
                            size: "small"
                            variant: index === view.results.viewIndex ? "secondary" : "ghost"
                            iconName: index < view.results.viewKinds.length ? view.kindIcon(view.results.viewKinds[index]) : "table"
                            text: modelData
                            focusPolicy: Qt.NoFocus
                            onClicked: view.results.viewIndex = index
                        }
                    }
                }
            }

            DatabaseView {
                id: view
                width: body.width
                controller: root.controller
                language: "base"
                basePath: root.path
                viewName: root.viewName
                onToastRequested: (text, kind, undo) => {
                    if (undo) {
                        root.undoModel = view.results
                        root.undoToast = AppController.toastWithAction(text, kind, qsTr("Undo"))
                    } else {
                        AppController.toast(text, kind)
                    }
                }
            }
        }
    }
}
