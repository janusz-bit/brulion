pragma Singleton
import QtQuick
import Brulion

// Theme — Brulion's design tokens (singleton). Every component reads colours, fonts, sizes
// and durations from here; nothing hard-codes them.
//
// Look: calm, paper-like light theme and a deep blue-grey dark theme from the logo — ink
// #1f2430, paper #fbf7ee, margin red #e2574c (accent, destructive), amber #f2b33d (reminders,
// alarms), grid blue #9fc3e6 (links, focus, selection). All text colours meet WCAG AA (4.5:1)
// on the backgrounds they are used on (fgFaint on bg/bgElevated/bgSunken included, chip text
// on its tinted chip over any surface, white on the solid danger/accentSolid fills — the brand
// red #e2574c is a fill for non-text marks only); state borders (borderControl, focusRing)
// reach 3:1. The gallery's "Podstawy" section computes all of this live.
//
// Appearance settings (Ustawienia → Wygląd): the surfaces, the accent family and the text colours
// come from ThemePalette (C++, src/app/ui/themepalette.h), which derives a readable accent from
// any colour and whose tests check every accent on every surface (tst_ui_themepalette); the
// defaults ("paper" / "slate", "red") are exactly the colours above. The accent marks things
// (today, the current tab, drop targets) — it never means "destructive": that is `danger*`,
// red whatever the accent.
//
// Inputs (set by the app shell / settings; the dev tools set them from the command line):
//   mode           "system" | "light" | "dark"   (system follows Qt.styleHints.colorScheme)
//   reducedMotion  durations become 0
//   forceTouch     touch sizes on any device (gallery --touch)
//   windowWidth/Height  the main window's size (bind it: Binding { target: Theme; ... })
//   editorFont     "serif" | "sans";  fontScale  user font size factor
//   editorFontSize the editor's base size in px (setting; touch UIs add one, fontScale applies)
//   accentName     "red" | "blue" | "green" | "teal" | "violet" | "graphite" | "custom" (+ customAccent "#rrggbb")
//   lightSurface   "paper" | "white";  darkSurface  "slate" | "black" (OLED)
//   uiFontFamily, editorFontFamily, monoFontFamily  installed families ("" or one missing on
//                  this device: the default font)
//   lineSpacing    "compact" | "normal" | "relaxed" (lineHeightLoose: editor prose)
//   density        "comfortable" | "compact" (smaller controls and rows; never on touch UIs)
//
// Derived: dark, narrow (< 900 px: drawer + bottom navigation, dialogs as bottom sheets),
// compact (< 600 px), touchUi (Android, or narrow window on a touch screen, or forceTouch),
// dense (compact density on a non-touch UI).
QtObject {
    id: theme

    // ---- Inputs --------------------------------------------------------------------------
    property string mode: DevTools.theme !== "" ? DevTools.theme : "system"
    property bool reducedMotion: DevTools.reducedMotion
    property bool forceTouch: DevTools.touch
    property real windowWidth: 1280
    property real windowHeight: 800
    property string editorFont: "serif"
    property real fontScale: 1.0
    property int editorFontSize: 16
    property string accentName: DevTools.accentName !== "" ? DevTools.accentName : "red"
    property string customAccent: DevTools.customAccent
    property string lightSurface: DevTools.lightSurface !== "" ? DevTools.lightSurface : "paper"
    property string darkSurface: DevTools.darkSurface !== "" ? DevTools.darkSurface : "slate"
    property string uiFontFamily: ""
    property string editorFontFamily: ""
    property string monoFontFamily: ""
    property string lineSpacing: "normal"
    property string density: DevTools.density !== "" ? DevTools.density : "comfortable"

    // ---- Derived state -------------------------------------------------------------------
    readonly property bool dark: mode === "dark" || (mode === "system" && Application.styleHints.colorScheme === Qt.ColorScheme.Dark)
    readonly property int narrowBreakpoint: 900
    readonly property int compactBreakpoint: 600
    readonly property bool narrow: windowWidth < narrowBreakpoint
    readonly property bool compact: windowWidth < compactBreakpoint
    readonly property bool touchUi: forceTouch || DeviceInfo.mobile || (narrow && DeviceInfo.hasTouchScreen)
    /// Compact density: smaller controls and rows with a mouse (touch UIs keep their targets).
    readonly property bool dense: density === "compact" && !touchUi
    /// The colours the appearance settings change (ThemePalette.colors(): "#rrggbb" by token name).
    readonly property var colors: ThemePalette.colors(dark, lightSurface, darkSurface, accentName, customAccent)

    // ---- Brand ---------------------------------------------------------------------------
    readonly property color ink: "#1f2430"
    readonly property color paper: "#fbf7ee"
    readonly property color marginRed: "#e2574c"
    readonly property color amber: "#f2b33d"
    readonly property color gridBlue: "#9fc3e6"

    // ---- Surfaces ------------------------------------------------------------------------
    // Defaults (paper / slate): bg #fbf7ee / #1c2130, bgElevated #fffdf7 / #232a3b, bgElevated2
    // #fffefb / #2b3245, bgSunken #f3eee2 / #171b28.
    readonly property color bg: colors.bg                     // page / window
    readonly property color bgElevated: colors.bgElevated     // cards, dialogs, menus
    readonly property color bgElevated2: colors.bgElevated2   // popups over cards
    readonly property color bgSunken: colors.bgSunken         // sidebar, wells, code
    readonly property color bgHover: dark ? Qt.rgba(232/255, 228/255, 218/255, 0.06) : Qt.rgba(31/255, 36/255, 48/255, 0.055)
    readonly property color bgPressed: dark ? Qt.rgba(232/255, 228/255, 218/255, 0.11) : Qt.rgba(31/255, 36/255, 48/255, 0.10)
    readonly property color bgSelected: dark ? Qt.rgba(159/255, 195/255, 230/255, 0.16) : Qt.rgba(159/255, 195/255, 230/255, 0.30)
    readonly property color bgInverse: dark ? "#e8e4da" : "#1f2430"
    readonly property color scrim: dark ? Qt.rgba(8/255, 10/255, 16/255, 0.62) : Qt.rgba(31/255, 36/255, 48/255, 0.40)

    // ---- Text ----------------------------------------------------------------------------
    // fg #1f2430 / #e8e4da, fgMuted #535a6a / #a9aebb, fgFaint #676c7b / #8f96a7.
    readonly property color fg: colors.fg
    readonly property color fgMuted: colors.fgMuted
    readonly property color fgFaint: colors.fgFaint
    readonly property color fgInverse: dark ? "#1c2130" : "#fbf7ee"
    readonly property color fgOnAccent: "#ffffff"
    readonly property real disabledOpacity: 0.45

    // ---- Lines ---------------------------------------------------------------------------
    // Defaults: border #e4dccb / #343c51, borderSubtle #ece6d8 / #2b3245, borderStrong #cdc2ab /
    // #48526b, borderControl #928775 / #6c7793.
    readonly property color border: colors.border
    readonly property color borderSubtle: colors.borderSubtle
    readonly property color borderStrong: colors.borderStrong
    readonly property color borderControl: colors.borderControl // ≥ 3:1 (checkbox, switch)

    // ---- Actions -------------------------------------------------------------------------
    readonly property color primary: dark ? "#e8e4da" : "#1f2430"        // primary buttons
    readonly property color primaryHover: dark ? "#f5f1e8" : "#2d3445"
    readonly property color primaryPressed: dark ? "#d6d1c5" : "#151923"
    readonly property color primaryFg: dark ? "#1c2130" : "#fbf7ee"
    // The accent (default: the margin red #e2574c; hover #d14b40 / #ec6a5f, text #ad3e2c /
    // #ef7a6f, solid #c4463a).
    readonly property color accent: colors.accent                         // marks (≥ 3:1)
    readonly property color accentHover: colors.accentHover
    readonly property color accentText: colors.accentText                 // accent text on bg
    readonly property color accentBg: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.18 : 0.12)
    /// Accent fill under white text (solid badges): the brand red itself is only 3.7:1 with white.
    readonly property color accentSolid: colors.accentSolid
    readonly property color link: colors.link                             // #2c6aa3 / #9fc3e6
    readonly property color linkHover: dark ? "#c3daf1" : "#1f5486"
    readonly property color linkMissing: colors.linkMissing               // #7a5ea3 / #b9a3d8
    readonly property color tagBg: dark ? Qt.rgba(159/255, 195/255, 230/255, 0.16) : Qt.rgba(159/255, 195/255, 230/255, 0.32)
    readonly property color tagFg: colors.tagFg                           // #2b5f8f / #b9d4ee

    // ---- Status --------------------------------------------------------------------------
    readonly property color danger: dark ? "#c94a3e" : "#c8473b"          // fills under white text (≥ 4.5:1)
    readonly property color dangerHover: dark ? "#bd4337" : "#b23c31"
    readonly property color dangerFg: "#ffffff"
    readonly property color dangerText: colors.dangerText                 // also on dangerBg chips (#a8382d / #f3877c)
    readonly property color dangerBg: dark ? Qt.rgba(226/255, 87/255, 76/255, 0.18) : Qt.rgba(226/255, 87/255, 76/255, 0.12)
    readonly property color warning: "#f2b33d"
    readonly property color warningText: colors.warningText               // #835500 / #f2b33d
    readonly property color warningBg: dark ? Qt.rgba(242/255, 179/255, 61/255, 0.16) : Qt.rgba(242/255, 179/255, 61/255, 0.22)
    readonly property color success: dark ? "#6fbf8a" : "#2f7a4c"
    readonly property color successText: colors.successText               // #2a6e44 / #7fcb98
    readonly property color successBg: dark ? Qt.rgba(111/255, 191/255, 138/255, 0.16) : Qt.rgba(63/255, 143/255, 90/255, 0.14)
    readonly property color info: dark ? "#9fc3e6" : "#2c6aa3"
    readonly property color infoText: dark ? "#9fc3e6" : "#2c6aa3"
    readonly property color infoBg: dark ? Qt.rgba(159/255, 195/255, 230/255, 0.14) : Qt.rgba(159/255, 195/255, 230/255, 0.28)

    // ---- Chips (dates, reminders, tags inside rendered text) ------------------------------
    readonly property color chipDueBg: dark ? Qt.rgba(159/255, 195/255, 230/255, 0.14) : Qt.rgba(159/255, 195/255, 230/255, 0.28)
    readonly property color chipDueFg: colors.chipDueFg                   // #2b5f8f / #b9d4ee
    readonly property color chipOverdueBg: dangerBg
    readonly property color chipOverdueFg: dangerText
    readonly property color chipScheduledBg: dark ? Qt.rgba(185/255, 163/255, 216/255, 0.16) : Qt.rgba(122/255, 94/255, 163/255, 0.12)
    readonly property color chipScheduledFg: colors.chipScheduledFg       // #6a4f93 / #c7b5e2
    readonly property color chipReminderBg: dark ? Qt.rgba(242/255, 179/255, 61/255, 0.16) : Qt.rgba(242/255, 179/255, 61/255, 0.24)
    readonly property color chipReminderFg: colors.chipReminderFg         // #7a4f00 / #f5c66a
    readonly property color chipAlarmBg: dark ? Qt.rgba(242/255, 179/255, 61/255, 0.28) : "#f2b33d"
    readonly property color chipAlarmFg: dark ? "#ffd27a" : "#1f2430"
    readonly property color chipRecurrenceBg: dark ? Qt.rgba(111/255, 191/255, 138/255, 0.14) : Qt.rgba(63/255, 143/255, 90/255, 0.12)
    readonly property color chipRecurrenceFg: colors.chipRecurrenceFg     // #2a6e44 / #8fd3a6
    readonly property color chipDoneBg: dark ? Qt.rgba(232/255, 228/255, 218/255, 0.08) : Qt.rgba(31/255, 36/255, 48/255, 0.06)
    readonly property color chipDoneFg: fgMuted
    readonly property color chipNeutralBg: dark ? Qt.rgba(232/255, 228/255, 218/255, 0.08) : Qt.rgba(31/255, 36/255, 48/255, 0.06)
    readonly property color chipNeutralFg: fgMuted

    /// { bg, fg, icon } for a chip variant: tag, due, overdue, scheduled, reminder, alarm,
    /// recurrence, done, neutral.
    function chip(kind) {
        switch (kind) {
        case "tag": return { bg: tagBg, fg: tagFg, icon: "hash" }
        case "due": return { bg: chipDueBg, fg: chipDueFg, icon: "calendar" }
        case "overdue": return { bg: chipOverdueBg, fg: chipOverdueFg, icon: "calendar" }
        case "scheduled": return { bg: chipScheduledBg, fg: chipScheduledFg, icon: "hourglass" }
        case "reminder": return { bg: chipReminderBg, fg: chipReminderFg, icon: "bell" }
        case "alarm": return { bg: chipAlarmBg, fg: chipAlarmFg, icon: "alarm-clock" }
        case "recurrence": return { bg: chipRecurrenceBg, fg: chipRecurrenceFg, icon: "repeat" }
        case "done": return { bg: chipDoneBg, fg: chipDoneFg, icon: "check" }
        default: return { bg: chipNeutralBg, fg: chipNeutralFg, icon: "" }
        }
    }

    /// Dot colour for date marks (BDatePicker): due, overdue, scheduled, reminder, alarm,
    /// journal, done, event.
    function markColor(kind) {
        switch (kind) {
        case "overdue": return dangerText
        case "reminder": case "alarm": return dark ? "#f2b33d" : "#c98a12"
        case "scheduled": return chipScheduledFg
        case "done": return fgFaint
        case "journal": return fgMuted
        default: return link
        }
    }

    // ---- Editing, selection, focus, diff ---------------------------------------------------
    readonly property color selection: dark ? Qt.rgba(159/255, 195/255, 230/255, 0.30) : Qt.rgba(159/255, 195/255, 230/255, 0.55)
    readonly property color selectionFg: fg
    readonly property color mark: dark ? Qt.rgba(242/255, 179/255, 61/255, 0.30) : Qt.rgba(242/255, 179/255, 61/255, 0.38)
    readonly property color focusRing: dark ? "#9fc3e6" : "#3b7dc0"
    readonly property color diffAddBg: dark ? Qt.rgba(111/255, 191/255, 138/255, 0.13) : Qt.rgba(63/255, 143/255, 90/255, 0.13)
    readonly property color diffAddStrong: dark ? Qt.rgba(111/255, 191/255, 138/255, 0.32) : Qt.rgba(63/255, 143/255, 90/255, 0.30)
    readonly property color diffAddFg: successText
    readonly property color diffDelBg: dark ? Qt.rgba(226/255, 87/255, 76/255, 0.14) : Qt.rgba(226/255, 87/255, 76/255, 0.11)
    readonly property color diffDelStrong: dark ? Qt.rgba(226/255, 87/255, 76/255, 0.36) : Qt.rgba(226/255, 87/255, 76/255, 0.28)
    readonly property color diffDelFg: dangerText
    readonly property color scrollbar: dark ? Qt.rgba(232/255, 228/255, 218/255, 0.22) : Qt.rgba(31/255, 36/255, 48/255, 0.24)
    readonly property color switchThumb: dark ? "#dfe2e8" : "#ffffff"
    readonly property color shadow: dark ? Qt.rgba(0, 0, 0, 0.5) : Qt.rgba(31/255, 36/255, 48/255, 0.18)

    // ---- Typography ----------------------------------------------------------------------
    // A chosen family missing on this device (chosen elsewhere, uninstalled) gives the default.
    readonly property string fontUi: uiFontFamily !== "" && DeviceInfo.hasFontFamily(uiFontFamily)
                                     ? uiFontFamily : DeviceInfo.systemFontFamily
    readonly property string fontSerif: "serif" // fontconfig / Android map it to the system serif
    readonly property string fontSans: DeviceInfo.systemFontFamily
    readonly property string fontMono: monoFontFamily !== "" && DeviceInfo.hasFontFamily(monoFontFamily)
                                       ? monoFontFamily : DeviceInfo.fixedFontFamily
    readonly property string fontEditor: editorFontFamily !== "" && DeviceInfo.hasFontFamily(editorFontFamily)
                                         ? editorFontFamily : editorFont === "sans" ? fontSans : fontSerif

    // Pixel sizes (scaled by fontScale; touch UIs one step larger for legibility).
    readonly property int textXs: Math.round((touchUi ? 12 : 11) * fontScale)
    readonly property int textSm: Math.round((touchUi ? 13 : 12) * fontScale)
    readonly property int textBase: Math.round((touchUi ? 15 : 14) * fontScale)   // UI default
    readonly property int textControl: Math.round((touchUi ? 15 : 14) * fontScale) // buttons, inputs
    readonly property int textMd: Math.round((touchUi ? 16 : 15) * fontScale)
    readonly property int textLg: Math.round((touchUi ? 18 : 17) * fontScale)
    readonly property int textXl: Math.round((touchUi ? 21 : 20) * fontScale)
    readonly property int text2xl: Math.round((touchUi ? 25 : 24) * fontScale)
    readonly property int text3xl: Math.round((touchUi ? 31 : 30) * fontScale)
    readonly property int textEditor: Math.round((editorFontSize + (touchUi ? 1 : 0)) * fontScale)
    // Line heights as multiples of the font size. Fonts differ a lot in their own line spacing
    // (DejaVu/Roboto ≈ 1.17 em, Noto/Segoe ≈ 1.36 em), so a proportional Text.lineHeight looks
    // cramped on one system and airy on another: apply these as fixed pixel heights —
    //   lineHeightMode: Text.FixedHeight; lineHeight: Theme.lineHeightPx(font.pixelSize)
    readonly property real lineHeightTight: 1.25    // headings
    readonly property real lineHeight: 1.5          // paragraphs, descriptions
    readonly property real lineHeightLoose: lineSpacing === "compact" ? 1.45 : lineSpacing === "relaxed" ? 1.9 : 1.65 // editor prose
    /// Line height in pixels for `pixelSize` (default ratio: lineHeight).
    function lineHeightPx(pixelSize, ratio) {
        return Math.round(pixelSize * (ratio === undefined ? lineHeight : ratio))
    }
    readonly property int weightNormal: Font.Normal
    readonly property int weightMedium: Font.Medium
    readonly property int weightSemiBold: Font.DemiBold
    readonly property int weightBold: Font.Bold

    // ---- Spacing (4 px grid), radii, sizes ------------------------------------------------
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 20
    readonly property int space6: 24
    readonly property int space8: 32
    readonly property int space10: 40
    readonly property int space12: 48
    readonly property int space16: 64

    readonly property int radiusXs: 3
    readonly property int radiusSm: 5
    readonly property int radius: 7
    readonly property int radiusLg: 10
    readonly property int radiusXl: 16
    readonly property int radiusFull: 999

    /// Minimum hit area for anything interactive (44 px on touch UIs).
    readonly property int touchTarget: touchUi ? 44 : dense ? 24 : 28
    /// Visual control heights. On touch UIs small controls stay visually compact but their
    /// hit area (implicitHeight) is touchTarget — components use insets for the difference.
    readonly property int controlHeightSm: touchUi ? 36 : dense ? 24 : 28
    readonly property int controlHeight: touchUi ? 44 : dense ? 30 : 34
    readonly property int controlHeightLg: touchUi ? 48 : dense ? 36 : 42
    readonly property int iconSizeSm: touchUi ? 16 : 14
    readonly property int iconSize: touchUi ? 20 : 16
    readonly property int iconSizeLg: touchUi ? 24 : 20
    readonly property int listItemHeight: touchUi ? 48 : dense ? 28 : 34
    readonly property int listItemHeightTwoLine: touchUi ? 64 : dense ? 42 : 48
    /// Dense list rows (the sidebar's) with a mouse; touch UIs use listItemHeight.
    readonly property int listItemHeightDense: dense ? 26 : 30
    /// The touch editing toolbar docked on the keyboard (MobileEditToolbar): a row of small
    /// buttons with a hairline above.
    readonly property int editToolbarHeight: touchTarget + space2 + 1

    readonly property int measure: 760          // editor's comfortable text width
    readonly property int sidebarWidth: dense ? 248 : 272
    readonly property int dialogWidthSm: 400
    readonly property int dialogWidth: 520
    readonly property int dialogWidthLg: 760
    readonly property int focusRingWidth: 2

    // ---- Elevation (BShadow) --------------------------------------------------------------
    /// { blur, offsetY, spread, opacity } for levels 0 (flat) … 3 (dialogs).
    function elevation(level) {
        switch (level) {
        case 1: return { blur: 6, offsetY: 1, spread: 0, opacity: dark ? 0.35 : 0.10 }
        case 2: return { blur: 18, offsetY: 4, spread: 0, opacity: dark ? 0.45 : 0.14 }
        case 3: return { blur: 40, offsetY: 14, spread: -4, opacity: dark ? 0.6 : 0.22 }
        default: return { blur: 0, offsetY: 0, spread: 0, opacity: 0 }
        }
    }

    // ---- Motion --------------------------------------------------------------------------
    readonly property int durationInstant: reducedMotion ? 0 : 70
    readonly property int durationFast: reducedMotion ? 0 : 120
    readonly property int duration: reducedMotion ? 0 : 180
    readonly property int durationSlow: reducedMotion ? 0 : 280
    readonly property int easing: Easing.OutCubic
    readonly property int easingEnter: Easing.OutQuint
    readonly property int easingExit: Easing.InCubic

    // ---- Helpers -------------------------------------------------------------------------
    /// `c` with alpha `a` (0–1).
    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }
}
