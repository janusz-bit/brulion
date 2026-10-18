pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// BDialog — modal dialog: a centred card on wide windows, a bottom sheet with a drag handle
// on narrow ones (< Theme.narrowBreakpoint). Asynchronous: open() returns at once; react to
// accepted() / rejected() (never a nested event loop — required on WebAssembly).
//
//   BDialog {
//       id: rename
//       title: qsTr("Zmień nazwę strony")
//       acceptText: qsTr("Zmień nazwę")
//       acceptEnabled: nameField.text.trim() !== ""
//       initialFocusItem: nameField
//       onAccepted: pages.rename(nameField.text)
//       BTextField { id: nameField; Layout.fillWidth: true }
//   }
//
// Children go into a scrolling ColumnLayout (use Layout.fillWidth). The footer has the
// reject (ghost) and accept buttons; `footerLeading` adds items on its left (e.g. a
// "Usuń" button). Escape, a click on the scrim, the × button, dragging the sheet down
// and Android back all reject. Ctrl+Enter accepts. Focus moves to `initialFocusItem` (or
// the dialog) on open and returns to the previously focused item on close.
// acceptRequested() comes first when the accept button or Ctrl+Enter asks to accept: apply
// text still being typed there (a field keeps the focus with Ctrl+Enter or a touch keyboard);
// `acceptEnabled` is checked again afterwards (onAcceptRequested: picker.commitPending()).
T.Dialog {
    id: control

    property string iconName
    property color iconColor: Theme.fgMuted
    property string subtitle
    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property string acceptVariant: "primary"   // "primary" | "danger"
    property bool acceptEnabled: true
    property bool busy: false
    property bool showFooter: true
    property bool showReject: true
    property bool showCloseButton: true
    property int preferredWidth: Theme.dialogWidth
    property bool sheetOnNarrow: true
    property Item initialFocusItem: null
    /// Focus the reject (cancel) button on open — for destructive confirmations.
    property bool focusReject: false
    property alias footerLeading: footerLeadingRow.data
    default property alias body: bodyColumn.data

    readonly property real overlayWidth: parent ? parent.width : Theme.windowWidth
    readonly property real overlayHeight: parent ? parent.height : Theme.windowHeight
    readonly property bool sheet: sheetOnNarrow && overlayWidth < Theme.narrowBreakpoint
    readonly property real safeBottom: parent && parent.SafeArea ? parent.SafeArea.margins.bottom : 0
    readonly property real safeTop: parent && parent.SafeArea ? parent.SafeArea.margins.top : 0
    property real dragOffset: 0

    signal acceptRequested()

    function tryAccept() {
        if (!acceptEnabled || busy)
            return
        acceptRequested()
        if (acceptEnabled && !busy)
            accept()
    }

    parent: T.Overlay.overlay
    modal: true
    dim: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    width: sheet ? overlayWidth : Math.min(preferredWidth, overlayWidth - 2 * Theme.space6)
    height: Math.min(implicitHeight, overlayHeight - (sheet ? safeTop + Theme.space10 : 2 * Theme.space6))
    x: sheet ? 0 : Math.round((overlayWidth - width) / 2)
    y: sheet ? overlayHeight - height + dragOffset
             : Math.max(Theme.space6, Math.round((overlayHeight - height) * 0.42))

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth, implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))
    leftPadding: sheet ? Theme.space4 : Theme.space6
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: showFooter ? 0 : Theme.space5 + (sheet ? safeBottom : 0)
    spacing: Theme.space3

    onOpened: {
        if (focusReject && showReject && showFooter)
            rejectButton.forceActiveFocus(Theme.touchUi ? Qt.OtherFocusReason : Qt.TabFocusReason)
        else if (initialFocusItem)
            initialFocusItem.forceActiveFocus(Qt.PopupFocusReason)
        else
            control.contentItem.forceActiveFocus(Qt.PopupFocusReason)
    }
    onAboutToShow: flick.contentY = 0

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: control.opened
        onActivated: control.tryAccept()
    }

    header: Item {
        implicitWidth: headerColumn.implicitWidth
        implicitHeight: headerColumn.implicitHeight

        // Drag the sheet down to dismiss it.
        DragHandler {
            id: sheetDrag
            enabled: control.sheet
            target: null
            xAxis.enabled: false
            onTranslationChanged: control.dragOffset = Math.max(0, translation.y)
            onActiveChanged: {
                if (active)
                    return
                if (control.dragOffset > Math.min(140, control.height * 0.3) || centroid.velocity.y > 900)
                    control.reject()
                else
                    settle.start()
            }
        }
        NumberAnimation {
            id: settle
            target: control
            property: "dragOffset"
            to: 0
            duration: Theme.duration
            easing.type: Theme.easing
        }

        Column {
            id: headerColumn
            width: parent.width

            Item {
                visible: control.sheet
                width: parent.width
                height: 20
                Rectangle {
                    anchors.centerIn: parent
                    width: 36
                    height: 4
                    radius: 2
                    color: Theme.borderStrong
                }
            }

            RowLayout {
                width: parent.width - 2 * control.leftPadding + (control.showCloseButton ? control.leftPadding - 8 : 0)
                x: control.leftPadding
                spacing: Theme.space3
                visible: control.title !== "" || control.showCloseButton

                BIcon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: (control.sheet ? 8 : 20) + Math.max(0, (titleText.lineHeightPx - height) / 2)
                    name: control.iconName
                    size: Theme.iconSizeLg
                    color: control.iconColor
                    visible: name !== ""
                }
                Column {
                    Layout.fillWidth: true
                    Layout.topMargin: control.sheet ? 8 : 20
                    Layout.bottomMargin: 2
                    spacing: 2
                    Text {
                        id: titleText
                        readonly property real lineHeightPx: font.pixelSize * 1.3
                        width: parent.width
                        text: control.title
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textLg
                        font.weight: Theme.weightSemiBold
                        color: Theme.fg
                        wrapMode: Text.Wrap
                    }
                    Text {
                        width: parent.width
                        visible: control.subtitle !== ""
                        text: control.subtitle
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        color: Theme.fgMuted
                        wrapMode: Text.Wrap
                    }
                }
                BIconButton {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: control.sheet ? 2 : 12
                    visible: control.showCloseButton
                    iconName: "x"
                    label: qsTr("Close")
                    shortcut: "Esc"
                    onClicked: control.reject()
                }
            }
        }
    }

    contentItem: Flickable {
        id: flick
        implicitWidth: bodyColumn.implicitWidth
        implicitHeight: bodyColumn.implicitHeight + 4
        contentWidth: width
        contentHeight: bodyColumn.implicitHeight + 4
        clip: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        T.ScrollBar.vertical: BScrollBar {}

        ColumnLayout {
            id: bodyColumn
            y: 2
            width: flick.width
            spacing: Theme.space4
        }
    }

    footer: Item {
        visible: control.showFooter
        implicitHeight: control.showFooter ? footerRow.implicitHeight + Theme.space4 + Theme.space5 + (control.sheet ? control.safeBottom : 0) : 0
        implicitWidth: footerRow.implicitWidth + 2 * control.leftPadding

        RowLayout {
            id: footerRow
            x: control.leftPadding
            y: Theme.space4
            width: parent.width - 2 * control.leftPadding
            // Gaps are margins rather than `spacing`: an empty footerLeading row (or one whose
            // items are hidden) has zero width but would still take a spacing. On sheets the
            // buttons share the row equally; explicit widths, because fill-width items with
            // margins are not shared equally by the layout.
            spacing: 0
            readonly property bool hasLeading: footerLeadingRow.implicitWidth > 0
            readonly property int buttonCount: control.showReject ? 2 : 1
            readonly property real sheetButtonWidth:
                (width - (hasLeading ? footerLeadingRow.implicitWidth + Theme.space2 : 0)
                 - (buttonCount - 1) * Theme.space2) / buttonCount

            Row {
                id: footerLeadingRow
                spacing: Theme.space2
                Layout.alignment: Qt.AlignVCenter
            }
            Item {
                visible: !control.sheet
                Layout.fillWidth: true
                Layout.minimumWidth: footerRow.hasLeading ? Theme.space2 : 0
            }
            BButton {
                id: rejectButton
                Layout.preferredWidth: control.sheet ? footerRow.sheetButtonWidth : implicitWidth
                Layout.leftMargin: control.sheet && footerRow.hasLeading ? Theme.space2 : 0
                visible: control.showReject
                text: control.rejectText
                variant: control.sheet ? "secondary" : "ghost"
                onClicked: control.reject()
            }
            BButton {
                Layout.preferredWidth: control.sheet ? footerRow.sheetButtonWidth : implicitWidth
                Layout.leftMargin: rejectButton.visible || (control.sheet && footerRow.hasLeading) ? Theme.space2 : 0
                text: control.acceptText
                variant: control.acceptVariant
                enabled: control.acceptEnabled
                busy: control.busy
                onClicked: control.tryAccept()
            }
        }
    }

    background: Item {
        BShadow {
            target: panel
            level: 3
        }
        Rectangle {
            id: panel
            anchors.fill: parent
            anchors.bottomMargin: control.sheet ? -Theme.radiusXl : 0 // square bottom corners
            radius: control.sheet ? Theme.radiusXl : Theme.radiusXl - 2
            color: Theme.bgElevated
            border.width: Theme.dark ? 1 : 0
            border.color: Theme.border
        }
    }

    T.Overlay.modal: Rectangle {
        color: Theme.scrim
        Behavior on opacity { NumberAnimation { duration: Theme.duration } }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: control.sheet ? Theme.durationFast : Theme.duration; easing.type: Theme.easing }
            NumberAnimation { property: "scale"; from: control.sheet ? 1 : 0.96; to: 1; duration: Theme.duration; easing.type: Theme.easingEnter }
            NumberAnimation { property: "dragOffset"; from: control.sheet ? control.height * 0.6 : 0; to: 0; duration: Theme.durationSlow; easing.type: Theme.easingEnter }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast; easing.type: Theme.easingExit }
            NumberAnimation { property: "scale"; from: 1; to: control.sheet ? 1 : 0.98; duration: Theme.durationFast }
            NumberAnimation { property: "dragOffset"; to: control.sheet ? control.height * 0.5 : 0; duration: Theme.duration; easing.type: Theme.easingExit }
        }
    }
}
