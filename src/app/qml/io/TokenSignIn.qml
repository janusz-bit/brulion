pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TokenSignIn — the "Connect with a token" block of the Todoist and Airtable cards
// (ImportSourcePicker): the personal token (hidden like a password, kept in memory only), where to
// get it, the button; Airtable then lists the bases (a choice when there are several) and the
// chosen base's tables with check boxes; while downloading a spinner and the progress. When the
// data is in, the ImportController shows its preview. In the browser only a hint: the services do
// not answer web pages of other sites, so there the user picks an exported file.
//
//   TokenSignIn { Layout.fillWidth: true; importer: importController; service: "airtable" }
ColumnLayout {
    id: block

    required property ImportController importer
    required property string service
    readonly property TokenImport tokenImport: tokenObject
    readonly property bool airtable: service === "airtable"
    readonly property bool canConnect: !AppController.isWasm
    readonly property list<var> none: []

    component Hint: BText {
        Layout.fillWidth: true
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    spacing: Theme.space3

    TokenImport {
        id: tokenObject
        objectName: "tokenImport"
        service: block.service
        importer: block.importer
    }

    Hint {
        visible: AppController.isWasm
        text: block.airtable
              ? qsTr("In the browser version choose a CSV file downloaded from Airtable – connecting with a token works in the app for Linux, Windows and Android.")
              : qsTr("In the browser version choose a CSV file exported from Todoist – connecting with a token works in the app for Linux, Windows and Android.")
    }

    // ---- The token ------------------------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: block.canConnect && !block.tokenImport.busy
        spacing: Theme.space2

        BText {
            Layout.fillWidth: true
            text: block.airtable ? qsTr("Personal access token") : qsTr("API token")
            variant: "label"
        }
        BTextField {
            objectName: "tokenField"
            Layout.fillWidth: true
            text: block.tokenImport.token
            echoMode: TextInput.Password
            placeholderText: block.airtable ? qsTr("pat… (from airtable.com/create/tokens)") : qsTr("40 characters from Todoist's settings")
            accessibleName: block.airtable ? qsTr("Personal access token") : qsTr("API token")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
            onTextEdited: block.tokenImport.token = text
            onAccepted: block.tokenImport.connectService()
        }
        Hint {
            text: block.airtable
                  ? qsTr("Create it at airtable.com/create/tokens with the scopes data.records:read and schema.bases:read and access to the bases to import. Brulion keeps it only until the download ends; it is not saved anywhere.")
                  : qsTr("In Todoist open Settings → Integrations → Developer and copy the API token. Brulion keeps it only until the download ends; it is not saved anywhere.")
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "tokenConnect"
                text: block.airtable ? qsTr("Connect to Airtable") : qsTr("Download from Todoist")
                iconName: block.airtable ? "link" : "download"
                variant: "primary"
                enabled: Js.trim(block.tokenImport.token) !== ""
                onClicked: block.tokenImport.connectService()
            }
        }
    }

    // ---- Airtable: the base and its tables ------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: block.canConnect && block.tokenImport.state === "choosing"
        spacing: Theme.space2

        RowLayout {
            Layout.fillWidth: true
            visible: block.tokenImport.bases.length > 1
            spacing: Theme.space2
            BText { text: qsTr("Base"); variant: "label" }
            IoChoiceButton {
                objectName: "airtableBase"
                model: block.tokenImport.bases
                currentValue: block.tokenImport.baseId
                fallbackText: qsTr("Choose a base")
                accessibleName: qsTr("Base")
                onActivated: (value) => block.tokenImport.chooseBase(value)
            }
        }
        Repeater {
            model: block.tokenImport.tables
            delegate: BCheckBox {
                id: table
                // (A row's "id" cannot be a property name.)
                required property var modelData
                readonly property string tableId: AppController.routeText(modelData, "id")
                Layout.fillWidth: true
                text: AppController.routeText(table.modelData, "name")
                description: qsTr("%n field(s)", "", AppController.routeInt(table.modelData, "fields", 0))
                checked: AppController.routeFlag(table.modelData, "checked")
                onToggled: block.tokenImport.setTableChecked(table.tableId, checked)
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "tokenDownload"
                visible: block.tokenImport.tables.length > 0
                text: qsTr("Download %n table(s)", "", block.tokenImport.checkedTables)
                iconName: "download"
                variant: "primary"
                enabled: block.tokenImport.checkedTables > 0
                onClicked: block.tokenImport.download()
            }
            BButton {
                text: qsTr("Cancel")
                variant: "ghost"
                onClicked: block.tokenImport.cancel()
            }
        }
    }

    // ---- Connecting / downloading ----------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        visible: block.canConnect && (block.tokenImport.state === "connecting" || block.tokenImport.state === "downloading")
        spacing: Theme.space2
        BSpinner { size: Theme.iconSizeSm; running: parent.visible }
        Hint {
            objectName: "tokenProgress"
            text: block.tokenImport.progressText
        }
        BButton {
            text: qsTr("Cancel")
            size: "small"
            variant: "ghost"
            onClicked: block.tokenImport.cancel()
        }
    }

    // ---- What went wrong ----------------------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        visible: block.tokenImport.state === "error"
        implicitHeight: errorRow.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.dangerBg
        RowLayout {
            id: errorRow
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space2
            BIcon { Layout.alignment: Qt.AlignTop; name: "circle-alert"; size: Theme.iconSize; color: Theme.dangerText }
            BText {
                objectName: "tokenError"
                Layout.fillWidth: true
                text: block.tokenImport.errorText
                wrapMode: Text.Wrap
            }
        }
    }

    Hint {
        visible: block.canConnect
        text: block.airtable ? qsTr("Or choose a CSV file downloaded from Airtable (a view's ••• → Download CSV):")
                             : qsTr("Or choose a project's CSV file (••• → Export as a template) or a backup ZIP:")
    }
}
