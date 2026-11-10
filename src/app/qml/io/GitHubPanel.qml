pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GitHubPanel — the sync center's GitHub tab (Workspace.github; docs/UI.md "Notebooks from
// GitHub"): a notebook kept in a folder of a GitHub repository's branch.
//   linked      the repository, branch and folder with "Open on GitHub"; the state ("The same as on
//               GitHub · checked today 10:32", "3 change(s) not on GitHub yet", what failed,
//               offline) with the progress of a pull, commit or proposal and "Stop"; "Get the
//               newest version", "Commit…" (when the token may push), "Propose changes…" (a pull
//               request — "Update the proposal…" while one is open); the proposal (#12, its state,
//               "Open on GitHub", "Forget" once it is merged or closed); the notes not on GitHub yet;
//               what the last pull left out or could not do; "Unlink"
//   not linked  what it is, the form linking this notebook (address, branch, folder) and
//               "Open as a new notebook…"; in the documentation: how to propose a correction of it
//   both        the account: "Signed in as @login", a field for a personal access token, the page
//               that makes one, "Forget the token"
//
//   GitHubPanel { Layout.fillWidth: true; onShowConflicts: root.showTab("conflicts") }
ColumnLayout {
    id: panel

    readonly property WorkspaceGitHub github: Workspace.github
    readonly property bool linked: github !== null && github.linked
    readonly property bool busy: github !== null && github.busy
    readonly property string ghState: github !== null ? github.state : "disabled"
    readonly property bool hasProposal: github !== null && AppController.routeInt(github.proposal, "number", 0) > 0
    readonly property bool proposalOpen: github !== null && AppController.routeFlag(github.proposal, "open")
    /// The last pull's files left out / not done: [{ path, title, code, text, message }].
    readonly property list<var> skipped: reportRows("skipped")
    readonly property list<var> failed: reportRows("errors")
    readonly property list<var> noRows: []
    property string linkMessage
    property bool linkFailed: false
    property string tokenMessage
    property bool tokenFailed: false
    property bool showToken: false
    property bool showProblems: false
    /// The Brulion repository's folder of the documentation in the UI language.
    readonly property string docsAddress: "https://github.com/janusz-bit/brulion/tree/main/src/app/shell/guide/"
                                          + (AppSettings.language === "pl" ? "pl" : "en")

    /// What the address typed says is wrong with it ("" when nothing).
    readonly property string addressError: AppController.routeText(panel.github.parseAddress(addressField.typedText), "error")

    signal showConflicts()

    function reportRows(key: string): list<var> {
        if (panel.github === null)
            return []
        return AppController.routeList(panel.github.lastReport, key)
    }

    Layout.fillWidth: true
    spacing: Theme.space5

    Connections {
        target: panel.github
        function onLinkFinished(ok: bool, message: string): void {
            panel.linkFailed = !ok
            panel.linkMessage = message
        }
        function onTokenChecked(ok: bool, message: string): void {
            panel.tokenFailed = !ok
            panel.tokenMessage = message
            if (ok)
                tokenField.text = ""
        }
    }

    GitHubCommitDialog { id: commitDialog }
    GitHubOpenDialog { id: openDialog }
    BConfirmDialog {
        id: unlinkDialog
        title: qsTr("Unlink from GitHub?")
        message: qsTr("The notes stay in this notebook; Brulion stops getting new versions and sending changes. Changes not on GitHub yet are not lost.")
        confirmText: qsTr("Unlink")
        onAccepted: panel.github.unlink()
    }

    /// Small muted explanatory text.
    component Hint: BText {
        Layout.fillWidth: true
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
    /// A file the last pull left out (muted) or could not do (`isError`): `path — text`.
    component ProblemRow: RowLayout {
        id: problem
        required property string path
        required property string text
        property bool isError: false
        Layout.fillWidth: true
        spacing: Theme.space2
        BIcon {
            Layout.alignment: Qt.AlignTop
            name: problem.isError ? "circle-alert" : "info"
            size: Theme.iconSizeSm
            color: problem.isError ? Theme.dangerText : Theme.fgMuted
        }
        BText {
            Layout.fillWidth: true
            text: problem.path + " — " + problem.text
            variant: "small"
            color: problem.isError ? Theme.fg : Theme.fgMuted
            wrapMode: Text.Wrap
        }
    }
    /// One reassuring line with a tick.
    component Check: RowLayout {
        property alias text: line.text
        Layout.fillWidth: true
        spacing: Theme.space2
        BIcon { name: "check"; size: Theme.iconSizeSm; color: Theme.fgMuted }
        BText { id: line; Layout.fillWidth: true; variant: "small"; wrapMode: Text.Wrap }
    }

    /// A labelled form field.
    component Field: ColumnLayout {
        id: field
        property string label
        property string help
        default property alias control: slot.data
        Layout.fillWidth: true
        spacing: Theme.space1
        BText {
            Layout.fillWidth: true
            text: field.label
            variant: "label"
            wrapMode: Text.Wrap
        }
        ColumnLayout {
            id: slot
            Layout.fillWidth: true
            spacing: Theme.space1
        }
        BText {
            Layout.fillWidth: true
            visible: field.help !== ""
            text: field.help
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
    }

    // ---- Linked: state and actions ---------------------------------------------------------------
    IoCard {
        visible: panel.linked
        iconName: panel.ghState === "offline" ? "cloud-off" : panel.ghState === "error" ? "circle-alert" : "git-branch"
        iconColor: panel.ghState === "error" ? Theme.dangerText
                 : panel.ghState === "offline" ? Theme.warningText
                 : panel.ghState === "idle" && panel.github.changeCount === 0 ? Theme.successText : Theme.fgMuted
        tone: panel.ghState === "error" ? "danger" : panel.ghState === "offline" ? "warning" : "default"
        title: panel.linked ? panel.github.repository : ""
        subtitle: panel.linked ? panel.github.statusText : ""
        trailing: [
            BBadge {
                visible: panel.linked && !panel.busy
                text: panel.github.changeCount > 0 ? qsTr("%n change(s)", "", panel.github.changeCount)
                    : panel.ghState === "error" ? qsTr("Problem")
                    : panel.ghState === "offline" ? qsTr("Offline") : qsTr("Up to date")
                variant: panel.github.changeCount > 0 ? "info"
                       : panel.ghState === "error" ? "danger"
                       : panel.ghState === "offline" ? "warning" : "success"
            }
        ]

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BIcon { Layout.alignment: Qt.AlignVCenter; name: "folder"; size: Theme.iconSizeSm; color: Theme.fgMuted }
            Hint {
                text: panel.github.folder !== "" ? qsTr("Branch “%1”, folder “%2”").arg(panel.github.branch).arg(panel.github.folder)
                                                 : qsTr("Branch “%1”, the whole repository").arg(panel.github.branch)
            }
            BButton {
                text: qsTr("Open on GitHub")
                iconName: "external-link"
                size: "small"
                variant: "ghost"
                onClicked: Qt.openUrlExternally(panel.github.webUrl)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: panel.busy
            spacing: Theme.space2
            BProgressBar {
                Layout.fillWidth: true
                indeterminate: panel.github.progress < 0
                value: Math.max(0, panel.github.progress)
            }
            RowLayout {
                Layout.fillWidth: true
                Hint { text: panel.github.progressText }
                BButton {
                    text: qsTr("Stop")
                    iconName: "square"
                    size: "small"
                    onClicked: panel.github.cancel()
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "githubPull"
                text: qsTr("Get the newest version")
                iconName: "download"
                variant: panel.github.changeCount === 0 ? "primary" : "secondary"
                enabled: !panel.busy
                busy: panel.ghState === "pulling"
                onClicked: panel.github.pull()
            }
            BButton {
                objectName: "githubCommit"
                visible: panel.github.canPush
                text: qsTr("Commit…")
                iconName: "git-commit-horizontal"
                variant: panel.github.changeCount > 0 ? "primary" : "secondary"
                enabled: !panel.busy && panel.github.changeCount > 0
                busy: panel.ghState === "committing"
                onClicked: commitDialog.openFor(false)
            }
            BButton {
                objectName: "githubPropose"
                text: panel.proposalOpen ? qsTr("Update the proposal…") : qsTr("Propose changes…")
                iconName: "git-pull-request"
                variant: !panel.github.canPush && panel.github.changeCount > 0 ? "primary" : "secondary"
                enabled: !panel.busy && panel.github.changeCount > 0 && panel.github.signedIn
                busy: panel.ghState === "proposing"
                onClicked: commitDialog.openFor(true)
            }
            BButton {
                visible: panel.github.headUrl !== ""
                text: qsTr("Commit %1").arg(panel.github.headText)
                iconName: "external-link"
                variant: "ghost"
                onClicked: Qt.openUrlExternally(panel.github.headUrl)
            }
        }
        Hint {
            visible: !panel.github.signedIn
            text: qsTr("Without a token Brulion only reads this repository. To send changes, save a token below.")
        }
        Hint {
            visible: panel.github.signedIn && panel.github.pushKnown && !panel.github.canPush
            text: qsTr("Your token may not write to this repository: changes go as a pull request from your copy of it.")
        }

        // What the last operation did.
        Hint {
            visible: !panel.busy && AppController.routeText(panel.github.lastReport, "text") !== ""
                     && panel.ghState !== "error"
            text: AppController.routeText(panel.github.lastReport, "text")
        }
        RowLayout {
            Layout.fillWidth: true
            visible: AppController.routeInt(panel.github.lastReport, "conflicts", 0) > 0
            spacing: Theme.space2
            BIcon { Layout.alignment: Qt.AlignVCenter; name: "git-merge"; size: Theme.iconSize; color: Theme.warningText }
            BText {
                Layout.fillWidth: true
                text: qsTr("Some notes changed both here and on GitHub: both versions are kept until you merge them.")
                variant: "small"
                wrapMode: Text.Wrap
            }
            BButton {
                text: qsTr("Merge")
                size: "small"
                onClicked: panel.showConflicts()
            }
        }
        BButton {
            visible: panel.skipped.length + panel.failed.length > 0
            text: panel.showProblems ? qsTr("Hide what was left out")
                                     : qsTr("%n file(s) left out or not done", "", panel.skipped.length + panel.failed.length)
            iconName: panel.showProblems ? "chevron-up" : "chevron-down"
            size: "small"
            variant: "ghost"
            onClicked: panel.showProblems = !panel.showProblems
        }
        Repeater {
            model: panel.showProblems ? panel.failed : panel.noRows
            // lastReport.errors: [{ path, title, code, text, message }]
            delegate: ProblemRow { isError: true }
        }
        Repeater {
            model: panel.showProblems ? panel.skipped : panel.noRows
            // lastReport.skipped: [{ path, title, code, text, message }]
            delegate: ProblemRow {}
        }
    }

    // ---- The proposal --------------------------------------------------------------------------
    IoCard {
        visible: panel.linked && panel.hasProposal
        iconName: "git-pull-request"
        iconColor: panel.proposalOpen ? Theme.infoText : Theme.fgMuted
        title: panel.hasProposal ? qsTr("Proposal #%1").arg(AppController.routeInt(panel.github.proposal, "number", 0)) : ""
        subtitle: panel.hasProposal ? AppController.routeText(panel.github.proposal, "title") : ""
        trailing: [
            BBadge {
                text: panel.hasProposal ? AppController.routeText(panel.github.proposal, "stateText") : ""
                variant: AppController.routeText(panel.github.proposal, "state") === "merged" ? "success"
                       : panel.proposalOpen ? "info" : "neutral"
            }
        ]
        Hint {
            text: panel.proposalOpen
                  ? qsTr("Waiting for the repository's people to look at it. Proposing again adds your new changes to it.")
                  : AppController.routeText(panel.github.proposal, "state") === "merged"
                    ? qsTr("Accepted: its notes are on the branch now.")
                    : qsTr("Closed without being accepted. Your changes are still here; a new proposal can send them again.")
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                text: qsTr("Open on GitHub")
                iconName: "external-link"
                size: "small"
                onClicked: Qt.openUrlExternally(AppController.routeText(panel.github.proposal, "url"))
            }
            BButton {
                visible: panel.proposalOpen
                text: qsTr("Check its state")
                iconName: "refresh-cw"
                size: "small"
                variant: "ghost"
                enabled: !panel.busy
                onClicked: panel.github.refreshProposal()
            }
            BButton {
                visible: !panel.proposalOpen
                text: qsTr("Forget")
                iconName: "x"
                size: "small"
                variant: "ghost"
                onClicked: panel.github.forgetProposal()
            }
        }
    }

    // ---- Notes not on GitHub yet ----------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: panel.linked && panel.github.changeCount > 0
        spacing: 2
        BSectionHeader {
            Layout.fillWidth: true
            text: qsTr("Not on GitHub yet")
            count: panel.github.changeCount
        }
        Repeater {
            model: panel.linked ? panel.github.changes : panel.noRows
            // Workspace.github.changes: [{ path, title, kind, kindText, proposed }]
            delegate: BListItem {
                id: changeRow
                required property string path
                required property string title
                required property string kind
                required property string kindText
                required property bool proposed
                Layout.fillWidth: true
                text: changeRow.title
                subtitle: changeRow.path
                iconName: changeRow.kind === "added" ? "plus" : changeRow.kind === "deleted" ? "minus" : "pencil"
                enabled: changeRow.kind !== "deleted"
                onClicked: AppController.openPage(changeRow.path)
                trailing: BBadge {
                    text: changeRow.proposed ? qsTr("In the proposal") : changeRow.kindText
                    variant: changeRow.proposed ? "info" : changeRow.kind === "deleted" ? "danger" : "neutral"
                }
            }
        }
    }

    // ---- Not linked ---------------------------------------------------------------------------
    IoCard {
        visible: !panel.linked && AppController.inDocsVault
        iconName: "git-pull-request"
        title: qsTr("Found a mistake in the documentation?")
        subtitle: qsTr("Its pages are kept in Brulion's repository on GitHub. Open them from there as a notebook of their own, correct them and propose the correction — the documentation here is written again by every new version.")
        BButton {
            text: qsTr("Open the documentation from GitHub…")
            iconName: "git-branch"
            variant: "primary"
            onClicked: openDialog.openWith(panel.docsAddress, "")
        }
    }

    IoCard {
        visible: !panel.linked && !AppController.inDocsVault
        iconName: "git-branch"
        title: qsTr("Keep this notebook on GitHub")
        subtitle: qsTr("Link it to a GitHub repository — all of it or one folder: Brulion gets its newest version, and sends your changes back as commits or as pull requests.")
        contentSpacing: Theme.space4

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space1
            Check { text: qsTr("Works on every device, in the browser too — no git needed.") }
            Check { text: qsTr("Notes changed here and on GitHub are merged line by line; when they cannot be, both versions are kept.") }
            Check { text: qsTr("Notes in this notebook that are not in the repository stay here until you commit them.") }
        }

        Field {
            label: qsTr("Repository")
            help: panel.addressError === ""
                  ? qsTr("“owner/name” or the address of a page of the repository on github.com (also of a folder).")
                  : ""
            BTextField {
                id: addressField
                objectName: "githubLinkAddress"
                Layout.fillWidth: true
                leadingIcon: "git-branch"
                accessibleName: qsTr("Repository")
                placeholderText: qsTr("owner/name or https://github.com/…")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                errorText: panel.addressError
                onTextEdited: panel.linkMessage = ""
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: Theme.compact ? 1 : 2
            columnSpacing: Theme.space4
            rowSpacing: Theme.space4
            Field {
                label: qsTr("Branch")
                help: qsTr("Empty: the one in the address, else the repository's default.")
                BTextField {
                    id: branchField
                    Layout.fillWidth: true
                    accessibleName: qsTr("Branch")
                    placeholderText: "main"
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                }
            }
            Field {
                label: qsTr("Folder in the repository")
                help: qsTr("Empty: the folder in the address, else the whole repository.")
                BTextField {
                    id: folderField
                    Layout.fillWidth: true
                    leadingIcon: "folder"
                    accessibleName: qsTr("Folder in the repository")
                    placeholderText: qsTr("e.g. Notes")
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                }
            }
        }
        BText {
            Layout.fillWidth: true
            visible: panel.linkMessage !== ""
            text: panel.linkMessage
            color: panel.linkFailed ? Theme.dangerText : Theme.successText
            wrapMode: Text.Wrap
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "githubLink"
                text: qsTr("Link this notebook")
                iconName: "link"
                variant: "primary"
                enabled: Workspace.isOpen && Js.trim(addressField.typedText) !== "" && !panel.github.checkingLink
                busy: panel.github.checkingLink
                onClicked: {
                    panel.linkMessage = ""
                    panel.github.linkNotebook(addressField.text, branchField.text, folderField.text) // trimmed there
                }
            }
            BButton {
                text: qsTr("Open as a new notebook…")
                iconName: "notebook-pen"
                onClicked: openDialog.openWith(addressField.text, folderField.text)
            }
        }
    }

    // ---- The account --------------------------------------------------------------------------
    IoCard {
        iconName: "lock"
        title: qsTr("GitHub account")
        subtitle: panel.github.signedIn
                  ? (panel.github.login !== "" ? qsTr("Signed in as %1.").arg("@" + panel.github.login) : qsTr("A token is saved on this device."))
                  : qsTr("Public repositories can be read without an account. Private ones, commits and pull requests need a personal access token.")
        contentSpacing: Theme.space3

        Field {
            label: panel.github.signedIn ? qsTr("Replace the token") : qsTr("Personal access token")
            BTextField {
                id: tokenField
                objectName: "githubToken"
                Layout.fillWidth: true
                leadingIcon: "lock"
                echoMode: panel.showToken ? TextInput.Normal : TextInput.Password
                accessibleName: qsTr("Personal access token")
                placeholderText: panel.github.signedIn ? qsTr("Saved – paste a new one to replace it") : "ghp_…"
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhHiddenText
                onTextEdited: panel.tokenMessage = ""
                onAccepted: panel.github.saveToken(tokenField.text)
                trailingItem: BIconButton {
                    iconName: panel.showToken ? "eye-off" : "eye"
                    label: panel.showToken ? qsTr("Hide the token") : qsTr("Show the token")
                    size: "small"
                    onClicked: panel.showToken = !panel.showToken
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: tokenHelp.implicitHeight + 2 * Theme.space3
            radius: Theme.radius
            color: Theme.bgSunken
            ColumnLayout {
                id: tokenHelp
                anchors.fill: parent
                anchors.margins: Theme.space3
                spacing: Theme.space2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BIcon { Layout.alignment: Qt.AlignTop; name: "info"; size: Theme.iconSize; color: Theme.infoText }
                    BText {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        variant: "small"
                        text: qsTr("On GitHub: Settings → Developer settings → Personal access tokens → “Generate new token (classic)” with the “repo” scope (“public_repo” is enough for public repositories). A fine-grained token works for your own repositories with read and write access to “Contents” and “Pull requests”. The token is kept only on this device and sent only to GitHub.")
                    }
                }
                BButton {
                    Layout.leftMargin: Theme.iconSize + Theme.space2
                    text: qsTr("Make a token on GitHub")
                    iconName: "external-link"
                    size: "small"
                    variant: "ghost"
                    onClicked: Qt.openUrlExternally(panel.github.tokenUrl)
                }
            }
        }
        BText {
            Layout.fillWidth: true
            visible: panel.tokenMessage !== ""
            text: panel.tokenMessage
            color: panel.tokenFailed ? Theme.dangerText : Theme.successText
            wrapMode: Text.Wrap
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "githubSaveToken"
                text: qsTr("Check and save")
                iconName: "check"
                variant: "primary"
                enabled: Js.trim(tokenField.typedText) !== "" && !panel.github.checkingToken
                busy: panel.github.checkingToken
                onClicked: panel.github.saveToken(tokenField.text)
            }
            BButton {
                visible: panel.github.signedIn
                text: qsTr("Forget the token")
                iconName: "trash-2"
                variant: "ghost"
                enabled: !panel.github.checkingToken
                onClicked: {
                    panel.github.forgetToken()
                    panel.tokenMessage = ""
                }
            }
        }
    }

    // ---- Unlink ---------------------------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        visible: panel.linked
        spacing: Theme.space2
        BIcon { Layout.alignment: Qt.AlignTop; name: "info"; size: Theme.iconSize; color: Theme.fgFaint }
        Hint { text: qsTr("Brulion checks GitHub when the notebook opens and every 15 minutes while it is in front. Your changes go to GitHub only when you commit or propose them.") }
        BButton {
            text: qsTr("Unlink…")
            size: "small"
            variant: "ghost"
            enabled: !panel.busy
            onClicked: unlinkDialog.open()
        }
    }
}
