pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Brulion

// Main — Brulion's window. Shows, by AppController.status: the splash (boot, loading), the
// onboarding (no vault yet), an error screen (the vault could not be opened) or the AppShell
// (docs/UI.md). Applies the user's settings to the design system (theme, fonts, motion) and
// the window size to Theme (narrow/compact layouts), hosts the toasts and handles window
// requests: show/raise (tray, second launch, notification tap), close to the tray, quit, the
// tray calendar (ShellTrayCalendar, created on first use).
//
// Edge-to-edge (Android): the window draws behind the status and navigation bars — its top and
// bottom padding is 0 and the shell's bars pad themselves with the SafeArea margins. The on-screen
// keyboard: AppShell lifts its content above it (KeyboardInsets), toasts stay above it, the
// focused form field is scrolled above it (BKeyboardFocusKeeper); a simulated keyboard
// (--keyboard <px>, tests) is drawn as a grey block.
ApplicationWindow {
    id: window

    width: 1180
    height: 780
    minimumWidth: 320
    minimumHeight: 420
    visible: !AppController.startHidden
    title: Workspace.isOpen ? Workspace.name + " — Brulion" : "Brulion"
    color: Theme.bg
    font.family: Theme.fontUi
    font.pixelSize: Theme.textBase
    topPadding: 0
    bottomPadding: 0

    // ---- Settings → design system ---------------------------------------------------------
    Binding { target: Theme; property: "windowWidth"; value: window.width }
    Binding { target: Theme; property: "windowHeight"; value: window.height }
    // --theme (screenshots) wins over the setting.
    Binding { target: Theme; property: "mode"; value: AppSettings.themeMode; when: DevTools.theme === "" }
    // --accent / --surface / --density (screenshots) win over the settings too.
    Binding { target: Theme; property: "accentName"; value: AppSettings.accentColor; when: DevTools.accentName === "" }
    Binding { target: Theme; property: "customAccent"; value: AppSettings.customAccent; when: DevTools.accentName === "" }
    Binding { target: Theme; property: "lightSurface"; value: AppSettings.lightSurface; when: DevTools.lightSurface === "" }
    Binding { target: Theme; property: "darkSurface"; value: AppSettings.darkSurface; when: DevTools.darkSurface === "" }
    Binding { target: Theme; property: "density"; value: AppSettings.density; when: DevTools.density === "" }
    // Font families: not in the browser (Qt there knows only its bundled DejaVu fonts).
    Binding { target: Theme; property: "uiFontFamily"; value: DeviceInfo.platform === "wasm" ? "" : AppSettings.uiFontFamily }
    Binding { target: Theme; property: "editorFontFamily"; value: DeviceInfo.platform === "wasm" ? "" : AppSettings.editorFontFamily }
    Binding { target: Theme; property: "monoFontFamily"; value: DeviceInfo.platform === "wasm" ? "" : AppSettings.editorMonoFamily }
    Binding { target: Theme; property: "lineSpacing"; value: AppSettings.editorLineSpacing }
    Binding { target: Theme; property: "editorFont"; value: AppSettings.editorFont }
    Binding { target: Theme; property: "fontScale"; value: AppSettings.fontScale }
    Binding { target: Theme; property: "editorFontSize"; value: AppSettings.editorFontSize }
    Binding { target: Theme; property: "reducedMotion"; value: AppSettings.reducedMotion || DevTools.reducedMotion }
    Binding { target: DatesHelper; property: "locale"; value: DevTools.language }
    Binding {
        target: DatesHelper
        property: "weekStart"
        value: Workspace.config.weekStartsOn !== undefined ? Workspace.config.weekStartsOn : 1
    }
    Binding { target: AppController; property: "sidePanelAllowed"; value: !Theme.narrow }

    // A popup that closes hands the focus back to what had it when it opened. When it opened
    // at start-up (quick capture from a launcher or the tray, while the window was not active
    // yet) that is the window's content item — outside every view, where no key (Tab, Escape,
    // the Android back key) reaches the shell any more. Give the focus back to the screen then.
    onActiveFocusItemChanged: {
        if (activeFocusItem === contentItem)
            Qt.callLater(() => {
                if (window.activeFocusItem === window.contentItem)
                    stage.forceActiveFocus()
            })
    }

    function showAndRaise() {
        if (window.visibility === Window.Minimized || !window.visible)
            window.showNormal()
        window.raise()
        window.requestActivate()
    }

    // ---- Content by status ---------------------------------------------------------------
    Loader {
        id: stage
        anchors.fill: parent
        focus: true
        sourceComponent: {
            switch (AppController.status) {
            case "ready": return shellComponent
            case "onboarding": return onboardingComponent
            case "error": return errorComponent
            default: return splashComponent
            }
        }
    }

    Component {
        id: shellComponent
        AppShell { toasts: toastHost }
    }
    Component {
        id: onboardingComponent
        OnboardingView {}
    }
    Component {
        id: splashComponent
        ShellSplash {}
    }
    Component {
        id: errorComponent
        Rectangle {
            color: Theme.bg
            BEmptyState {
                anchors.centerIn: parent
                maximumWidth: 460
                iconName: "triangle-alert"
                title: qsTr("The notebook could not be opened")
                description: AppController.errorString
                actionText: qsTr("Choose another notebook")
                actionIcon: "folder-open"
                secondaryActionText: AppController.failedVaultPath !== "" ? qsTr("Try again") : ""
                onActionTriggered: AppController.showOnboarding()
                onSecondaryActionTriggered: AppController.retryOpenVault()
            }
        }
    }

    readonly property AppShell shell: stage.item as AppShell

    // The tray calendar (desktop): its own window, created on first use; never on Android or in
    // the browser (no tray asks for it there).
    Loader {
        id: trayCalendar
        active: false
        sourceComponent: ShellTrayCalendar {}
    }
    readonly property ShellTrayCalendar trayCalendarWindow: trayCalendar.item as ShellTrayCalendar

    function hideTrayCalendar() {
        if (window.trayCalendarWindow && window.trayCalendarWindow.visible)
            window.trayCalendarWindow.hide()
    }
    /// "trayCalendar" (the tray menu) opens it; "toggleTrayCalendar" (a click on the icon)
    /// closes an open one, and keeps it closed when it has just hidden itself because that very
    /// click took its activation (AppController.trayPopupShouldReopen, asked on every tray action
    /// so that an old hide never swallows a later click).
    function showTrayCalendar(toggle: bool) {
        trayCalendar.active = true
        const popup = window.trayCalendarWindow
        if (!popup)
            return
        const reopen = AppController.trayPopupShouldReopen()
        if (popup.visible) {
            if (toggle)
                popup.hide()
            else
                popup.open()
            return
        }
        if (toggle && !reopen)
            return
        popup.open()
    }

    // The focused form field (settings, onboarding, dialogs) above the on-screen keyboard.
    BKeyboardFocusKeeper { window: window }

    BToastHost {
        id: toastHost
        objectName: "toastHost" // the remote control reads the toasts shown (remotecontrol.h)
        parent: T.Overlay.overlay
        anchors.fill: parent
        z: 1000
        // Above the bottom navigation, the quick-capture button (56 + its margin) and the
        // Android navigation bar: an error toast stays until closed and must not hide them.
        // While typing above the keyboard and the editing toolbar docked on it (AppShell).
        bottomMargin: Theme.space4 + (window.shell ? window.shell.toastClearance
                                                    : (parent ? parent.SafeArea.margins.bottom : 0) + KeyboardInsets.overlap)
        // While typing on a touch screen: at the top, below the status bar (at the bottom it
        // covered the line being written — the caret is kept right above the keyboard and its
        // toolbar).
        atTop: window.shell !== null && window.shell.toastsAtTop
        topMargin: Theme.space2 + (parent ? parent.SafeArea.margins.top : 0)
    }

    // A simulated on-screen keyboard (--keyboard <px>, the self-tests): a grey block with rows of
    // keys where a real one would be, above everything (screenshots show the toolbar on it).
    Loader {
        parent: T.Overlay.overlay
        z: 2000
        active: KeyboardInsets.source === "simulated" && KeyboardInsets.overlap > 0
        anchors.left: parent ? parent.left : undefined
        anchors.right: parent ? parent.right : undefined
        anchors.bottom: parent ? parent.bottom : undefined
        height: KeyboardInsets.overlap
        sourceComponent: BKeyboardPlaceholder {}
    }

    Connections {
        target: AppController

        function onToastRequested(text, kind) {
            // Errors (a note could not be saved, storage is missing) stay until closed.
            toastHost.show(text, kind === "error" ? { kind: kind, timeout: 0 } : { kind: kind })
        }
        // A toast with a button (the editor's "Cofnij" after deleting or cutting blocks).
        function onToastActionRequested(text, kind, actionText, onAction) {
            toastHost.show(text, { kind: kind, actionText: actionText,
                                   onAction: () => { if (typeof onAction === "function") onAction() } })
        }
        function onShowWindowRequested() {
            window.hideTrayCalendar()
            window.showAndRaise()
        }
        function onUiActionRequested(action) {
            switch (action) {
            case "toggleTheme":
                AppSettings.themeMode = Theme.dark ? "light" : "dark"
                return
            case "toggleWindow":
                window.hideTrayCalendar()
                if (window.visible && window.active)
                    window.hide()
                else
                    window.showAndRaise()
                return
            case "trayCalendar":
            case "toggleTrayCalendar":
                window.showTrayCalendar(action === "toggleTrayCalendar")
                return
            }
            // The other actions open the shell's popups (AppShell); without a vault there is none.
            if (AppController.status !== "ready" && (action === "capture" || action === "newPage"))
                toastHost.show(qsTr("Open a notebook first."), { kind: "warning" })
        }
    }

    // Close to the system tray (desktop): reminders keep firing while the window is hidden.
    // Android: an unhandled back key closes the window. That is a back step too (AppShell
    // handles it when it has the focus; this catches it when nothing focused took it); at home
    // the app goes to the background like any Android 12+ app instead of ending its process.
    onClosing: (close) => {
        // A shown tray calendar would keep a really closed window's app running.
        window.hideTrayCalendar()
        if (AppController.isAndroid && !AppController.quitting) {
            close.accepted = false
            if (AppController.status !== "ready" || !AppController.handleBackKey())
                AppController.moveToBackground()
            return
        }
        if (AppController.trayAvailable && AppSettings.closeToTray && !AppController.quitting) {
            close.accepted = false
            window.hide()
            AppController.hiddenToTray()
        }
    }
}
