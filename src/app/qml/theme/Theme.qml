pragma Singleton
import QtQuick
import Brulion

// Theme — Brulion's design tokens (singleton). Every component reads colours, fonts, sizes
// and durations from here; nothing hard-codes them.
//
// Look: a layered dark theme by default — three clearly different greys ("graphite": side panes
// darkest, the page above them, cards, menus and dialogs lighter still; surfaces part by shade, not
// by hairlines) with a coral accent (#ff7a66; #c4422f in light) and soft, rounded shapes; true OLED
// black ("Nocny") and blue-grey are the other dark backgrounds; and a calm, paper-like light theme — ink #1f2430, paper #fbf7ee,
// margin red #e2574c (destructive; also an accent preset), amber #f2b33d (reminders, alarms —
// whatever the accent), grid blue #9fc3e6 (links, focus, selection). All text colours meet WCAG AA (4.5:1)
// on the backgrounds they are used on (fgFaint on bg/bgElevated/bgSunken included, chip text
// on its tinted chip over any surface, white on the solid danger/accentSolid fills — the brand
// red #e2574c is a fill for non-text marks only); state borders (borderControl, focusRing)
// reach 3:1. The gallery's "Podstawy" (basics) section computes all of this live.
//
// Appearance settings (Settings → Appearance): the surfaces, the accent family and the text colours
// come from ThemePalette (C++, src/app/ui/themepalette.h), which derives a readable accent from
// any colour and whose tests check every accent on every surface (tst_themepalette); the
// defaults are "paper" / "graphite" and "coral". The accent marks things
// (today, the current tab, drop targets) — it never means "destructive": that is `danger*`,
// red whatever the accent.
//
// Overrides (docs/EXTENSIONS.md "The app (`b.app`)": an extension's b.app.theme, an AI agent's live
// theme): every token of the catalog (core extensions/appapi.cpp themeTokens(): the colours below,
// radiusXs … radiusXl, focusRingWidth, fontUi/fontSans/fontSerif/fontMono) can be replaced. The
// palette's tokens have them already (ThemeColors); every other colour keeps its own value as
// `_<name>Base` and reads `_pick("<name>", _<name>Base)` (the override from palette.overrides through
// palette.pickText(): a C++ method's QColor cannot be stored in a compiled `color`), sizes
// `palette.pickNumber()`, fonts `palette.pickText()` (an installed family only). With no overrides
// every token is exactly its own value.
//
// Inputs (set by the app shell / settings; the dev tools set them from the command line):
//   mode           "system" | "light" | "dark"   (system follows Qt.styleHints.colorScheme)
//   reducedMotion  durations become 0
//   forceTouch     touch sizes on any device (gallery --touch)
//   windowWidth/Height  the main window's size (bind it: Binding { target: Theme; ... })
//   editorFont     "sans" | "serif";  fontScale  user font size factor
//   editorFontSize the editor's base size in px (setting; touch UIs add one, fontScale applies)
//   accentName     "coral" | "red" | "blue" | "green" | "teal" | "violet" | "graphite" | "custom" (+ customAccent "#rrggbb")
//   lightSurface   "paper" | "white";  darkSurface  "graphite" (layered) | "black" (OLED) | "slate"
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
    property string editorFont: "sans"
    property real fontScale: 1.0
    property int editorFontSize: 16
    property string accentName: DevTools.accentName !== "" ? DevTools.accentName : "coral"
    property string customAccent: DevTools.customAccent
    property string lightSurface: DevTools.lightSurface !== "" ? DevTools.lightSurface : "paper"
    property string darkSurface: DevTools.darkSurface !== "" ? DevTools.darkSurface : "graphite"
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
    /// The colours the appearance settings change ("#rrggbb" strings by token name, C++:
    /// ThemeColors, the same as ThemePalette.colors()) — typed properties for the tokens below.
    readonly property ThemeColors palette: ThemeColors {
        dark: theme.dark
        lightSurface: theme.lightSurface
        darkSurface: theme.darkSurface
        accentName: theme.accentName
        customAccent: theme.customAccent
    }
    /// The same as a map by token name (`FilePicker { colors: Theme.colors }`).
    readonly property var colors: palette.map

    // ---- Brand ---------------------------------------------------------------------------
    readonly property color ink: "#1f2430"
    readonly property color paper: "#fbf7ee"
    readonly property color marginRed: "#e2574c"
    readonly property color amber: "#f2b33d"
    readonly property color gridBlue: "#9fc3e6"
    // Bases of the translucent tokens below (`base.alpha(a)` is Qt.rgba() of its channels).
    readonly property color _fgDarkBase: "#d3d8e2"
    readonly property color _scrimDarkBase: "#080a10"
    readonly property color _successDarkBase: "#6fbf8a"
    readonly property color _successLightBase: "#3f8f5a"
    readonly property color _scheduledDarkBase: "#b9a3d8"
    readonly property color _scheduledLightBase: "#7a5ea3"
    readonly property color _black: "#000000"
    readonly property color _tagDarkBase: "#c89bf0"
    readonly property color _tagLightBase: "#7a4bb0"

    /// The colour `token` of the theme's overrides (palette.overrides: "#rrggbb" / "#aarrggbb"), else
    /// `base`, the token's own value — exactly that value with no overrides.
    function _pick(token: string, base: color): color {
        const value = "" + theme.palette.pickText(theme.palette.overrides, token, "")
        if (value === "")
            return base
        return value
    }

    // ---- Surfaces ------------------------------------------------------------------------
    // Defaults (paper / graphite): bg #fbf7ee / #1e1f24, bgElevated #fffdf7 / #27292f, bgElevated2
    // #fffefb / #30323a, bgSunken #f3eee2 / #16171b (black: #000000, #0d0f13, #1c2027, #08090b;
    // slate: #1c2130, #232a3b, #2b3245, #171b28).
    readonly property color bg: palette.bg                     // page / window
    readonly property color bgElevated: palette.bgElevated     // cards, dialogs, menus
    readonly property color bgElevated2: palette.bgElevated2   // popups over cards
    readonly property color bgSunken: palette.bgSunken         // sidebar, wells, code
    readonly property color _bgHoverBase: dark ? theme._fgDarkBase.alpha(0.06) : theme.ink.alpha(0.055)
    readonly property color bgHover: theme._pick("bgHover", theme._bgHoverBase)
    readonly property color _bgPressedBase: dark ? theme._fgDarkBase.alpha(0.11) : theme.ink.alpha(0.10)
    readonly property color bgPressed: theme._pick("bgPressed", theme._bgPressedBase)
    readonly property color _bgSelectedBase: dark ? theme.gridBlue.alpha(0.16) : theme.gridBlue.alpha(0.30)
    readonly property color bgSelected: theme._pick("bgSelected", theme._bgSelectedBase)
    readonly property color _bgInverseBase: dark ? "#d3d8e2" : "#1f2430"
    readonly property color bgInverse: theme._pick("bgInverse", theme._bgInverseBase)
    /// Behind edge sheets (bottom sheets, the full-screen palette, the drawer): the page dims.
    readonly property color _scrimBase: dark ? theme._scrimDarkBase.alpha(0.62) : theme.ink.alpha(0.40)
    readonly property color scrim: theme._pick("scrim", theme._scrimBase)
    /// Behind centred modal panels (the floating command palette and quick switcher, a centred
    /// BDialog): the page recedes — light: a wash of the page tinted toward ink (paper about
    /// #ddd9d3, page text about 2.5:1 instead of 7:1, the panel's lighter fill stands out); dark:
    /// the scrim's ink at 72 % (page text about 2:1). ThemePalette.modalScrim, per surface.
    readonly property color scrimModal: palette.scrimModal

    // ---- Text ----------------------------------------------------------------------------
    // fg #1f2430 / #d3d8e2, fgMuted #535a6a / #a9b0bf, fgFaint #676c7b / #8f96a7.
    readonly property color fg: palette.fg
    readonly property color fgMuted: palette.fgMuted
    readonly property color fgFaint: palette.fgFaint
    readonly property color _fgInverseBase: dark ? "#1c2130" : "#fbf7ee"
    readonly property color fgInverse: theme._pick("fgInverse", theme._fgInverseBase)
    /// Text on accentSolid: white, dark ink (#2b0a05) on the dark mode's bright coral.
    readonly property color fgOnAccent: palette.accentOn
    readonly property real disabledOpacity: 0.45

    // ---- Lines ---------------------------------------------------------------------------
    // Defaults: border #e4dccb / #262a32, borderSubtle #ece6d8 / #1a1d23, borderStrong #cdc2ab /
    // #3a404d, borderControl #928775 / #6c7793.
    /// Hairlines between the window's panes (the rail, the page panel, the side panel): drawn
    /// where the panes' shades are too close to part them — not on the graphite background,
    /// whose layers part by their shade (Discord's way).
    readonly property bool paneLines: !(dark && darkSurface === "graphite")
    readonly property color border: palette.border
    readonly property color borderSubtle: palette.borderSubtle
    readonly property color borderStrong: palette.borderStrong
    readonly property color borderControl: palette.borderControl // ≥ 3:1 (checkbox, switch)
    /// Indent guides (the outline's level lines, the page tree's): quiet but seen, about
    /// Obsidian's level (1.28:1 on paper, 1.46:1 on slate). Not text, not a control.
    readonly property color _guideBase: border
    readonly property color guide: theme._pick("guide", theme._guideBase)
    /// Indent guides on bgSunken (the sidebar's and the drawer's tree), where border is only
    /// 1.18:1 in the light: borderStrong there (1.52:1), border in the dark (1.56:1).
    readonly property color _guideOnSunkenBase: dark ? border : borderStrong
    readonly property color guideOnSunken: theme._pick("guideOnSunken", theme._guideOnSunkenBase)

    // ---- Actions -------------------------------------------------------------------------
    // Ink fills: the selected day's disc (date pickers, the calendar), a selected neutral chip.
    readonly property color _primaryBase: dark ? "#d3d8e2" : "#1f2430"
    readonly property color primary: theme._pick("primary", theme._primaryBase)
    readonly property color _primaryHoverBase: dark ? "#e6e9ef" : "#2d3445"
    readonly property color primaryHover: theme._pick("primaryHover", theme._primaryHoverBase)
    readonly property color _primaryPressedBase: dark ? "#bfc5d1" : "#151923"
    readonly property color primaryPressed: theme._pick("primaryPressed", theme._primaryPressedBase)
    readonly property color _primaryFgBase: dark ? "#1c2130" : "#fbf7ee"
    readonly property color primaryFg: theme._pick("primaryFg", theme._primaryFgBase)
    /// The primary button (one per view: Save, Add, Create): the accent's solid fill under
    /// fgOnAccent (≥ 4.5:1, ThemePalette), lighter (dark) or darker (light) under the mouse.
    readonly property color buttonPrimary: theme._pick("buttonPrimary", theme.accentSolid)
    readonly property color _buttonPrimaryHoverBase: dark ? accentSolid.lighter(1.08) : accentSolid.darker(1.1)
    readonly property color buttonPrimaryHover: theme._pick("buttonPrimaryHover", theme._buttonPrimaryHoverBase)
    readonly property color _buttonPrimaryPressedBase: dark ? accentSolid.darker(1.08) : accentSolid.darker(1.22)
    readonly property color buttonPrimaryPressed: theme._pick("buttonPrimaryPressed", theme._buttonPrimaryPressedBase)
    readonly property color buttonPrimaryFg: theme._pick("buttonPrimaryFg", theme.fgOnAccent)
    // The accent (default: coral, #c4422f / #ff7a66; its text and hover shades derived by
    // ThemePalette, its solid fill #c4422f under white / #ff7a66 under dark ink).
    readonly property color accent: palette.accent                         // marks (≥ 3:1)
    readonly property color accentHover: palette.accentHover
    readonly property color accentText: palette.accentText                 // accent text on bg
    readonly property color _accentBgBase: accent.alpha(dark ? 0.18 : 0.12)
    readonly property color accentBg: theme._pick("accentBg", theme._accentBgBase)
    /// Accent fill under fgOnAccent (solid badges, today in the calendar).
    readonly property color accentSolid: palette.accentSolid
    readonly property color link: palette.link                             // #2a62a6 / #8fb8ea
    readonly property color _linkHoverBase: dark ? "#b8d2f2" : "#1f5486"
    readonly property color linkHover: theme._pick("linkHover", theme._linkHoverBase)
    readonly property color linkMissing: palette.linkMissing               // #7a5ea3 / #b9a3d8
    // Tags and due dates: a light tint, well apart from the selection (bgSelected) it used to match.
    readonly property color _tagBgBase: dark ? theme._tagDarkBase.alpha(0.12) : theme._tagLightBase.alpha(0.10)
    readonly property color tagBg: theme._pick("tagBg", theme._tagBgBase)
    readonly property color tagFg: palette.tagFg                           // #7a4bb0 / #c89bf0

    // ---- Status --------------------------------------------------------------------------
    readonly property color _dangerBase: dark ? "#c94a3e" : "#c8473b"
    readonly property color danger: theme._pick("danger", theme._dangerBase)          // fills under white text (≥ 4.5:1)
    readonly property color _dangerHoverBase: dark ? "#bd4337" : "#b23c31"
    readonly property color dangerHover: theme._pick("dangerHover", theme._dangerHoverBase)
    readonly property color _dangerFgBase: "#ffffff"
    readonly property color dangerFg: theme._pick("dangerFg", theme._dangerFgBase)
    readonly property color dangerText: palette.dangerText                 // also on dangerBg chips (#a8382d / #f3877c)
    readonly property color _dangerBgBase: dark ? theme.marginRed.alpha(0.18) : theme.marginRed.alpha(0.12)
    readonly property color dangerBg: theme._pick("dangerBg", theme._dangerBgBase)
    readonly property color _warningBase: "#f2b33d"
    readonly property color warning: theme._pick("warning", theme._warningBase)
    readonly property color warningText: palette.warningText               // #835500 / #f2b33d
    readonly property color _warningBgBase: dark ? theme.amber.alpha(0.16) : theme.amber.alpha(0.22)
    readonly property color warningBg: theme._pick("warningBg", theme._warningBgBase)
    readonly property color _successBase: dark ? "#6fbf8a" : "#2f7a4c"
    readonly property color success: theme._pick("success", theme._successBase)
    readonly property color successText: palette.successText               // #2a6e44 / #7fd1a8
    readonly property color _successBgBase: dark ? theme._successDarkBase.alpha(0.16) : theme._successLightBase.alpha(0.14)
    readonly property color successBg: theme._pick("successBg", theme._successBgBase)
    readonly property color _infoBase: dark ? "#9fc3e6" : "#2c6aa3"
    readonly property color info: theme._pick("info", theme._infoBase)
    readonly property color _infoTextBase: dark ? "#9fc3e6" : "#2c6aa3"
    readonly property color infoText: theme._pick("infoText", theme._infoTextBase)
    readonly property color _infoBgBase: dark ? theme.gridBlue.alpha(0.14) : theme.gridBlue.alpha(0.28)
    readonly property color infoBg: theme._pick("infoBg", theme._infoBgBase)

    // ---- Chips (dates, reminders, tags inside rendered text) ------------------------------
    readonly property color _chipDueBgBase: dark ? theme.gridBlue.alpha(0.10) : theme.gridBlue.alpha(0.16)
    readonly property color chipDueBg: theme._pick("chipDueBg", theme._chipDueBgBase)
    readonly property color chipDueFg: palette.chipDueFg                   // #2b5f8f / #b9d4ee
    readonly property color _chipOverdueBgBase: dangerBg
    readonly property color chipOverdueBg: theme._pick("chipOverdueBg", theme._chipOverdueBgBase)
    readonly property color _chipOverdueFgBase: dangerText
    readonly property color chipOverdueFg: theme._pick("chipOverdueFg", theme._chipOverdueFgBase)
    readonly property color _chipScheduledBgBase: dark ? theme._scheduledDarkBase.alpha(0.16) : theme._scheduledLightBase.alpha(0.12)
    readonly property color chipScheduledBg: theme._pick("chipScheduledBg", theme._chipScheduledBgBase)
    readonly property color chipScheduledFg: palette.chipScheduledFg       // #6a4f93 / #c7b5e2
    // Reminders and alarms are amber whatever the accent (never coral, never the overdue red).
    readonly property color _chipReminderBgBase: dark ? theme.amber.alpha(0.13) : theme.amber.alpha(0.22)
    readonly property color chipReminderBg: theme._pick("chipReminderBg", theme._chipReminderBgBase)
    readonly property color chipReminderFg: palette.chipReminderFg         // #8a5900 / #f2b33d
    readonly property color _chipAlarmBgBase: dark ? theme.amber.alpha(0.28) : "#f2b33d"
    readonly property color chipAlarmBg: theme._pick("chipAlarmBg", theme._chipAlarmBgBase)
    readonly property color _chipAlarmFgBase: dark ? "#ffd27a" : "#1f2430"
    readonly property color chipAlarmFg: theme._pick("chipAlarmFg", theme._chipAlarmFgBase)
    readonly property color _chipRecurrenceBgBase: dark ? theme._successDarkBase.alpha(0.14) : theme._successLightBase.alpha(0.12)
    readonly property color chipRecurrenceBg: theme._pick("chipRecurrenceBg", theme._chipRecurrenceBgBase)
    readonly property color chipRecurrenceFg: palette.chipRecurrenceFg     // #2a6e44 / #8fd3a6
    readonly property color _chipDoneBgBase: dark ? theme._fgDarkBase.alpha(0.08) : theme.ink.alpha(0.06)
    readonly property color chipDoneBg: theme._pick("chipDoneBg", theme._chipDoneBgBase)
    readonly property color _chipDoneFgBase: fgMuted
    readonly property color chipDoneFg: theme._pick("chipDoneFg", theme._chipDoneFgBase)
    readonly property color _chipNeutralBgBase: dark ? theme._fgDarkBase.alpha(0.08) : theme.ink.alpha(0.06)
    readonly property color chipNeutralBg: theme._pick("chipNeutralBg", theme._chipNeutralBgBase)
    readonly property color _chipNeutralFgBase: fgMuted
    readonly property color chipNeutralFg: theme._pick("chipNeutralFg", theme._chipNeutralFgBase)

    /// { bg, fg, icon } for a chip variant: tag, due, overdue, scheduled, reminder, alarm,
    /// recurrence, done, neutral. (A member of its result does not compile: chipBg(), chipFg()
    /// and chipIcon() give the same typed.)
    function chip(kind: string): var {
        return { bg: chipBg(kind), fg: chipFg(kind), icon: chipIcon(kind) }
    }
    /// The background of a chip variant (chip()).
    function chipBg(kind: string): color {
        switch (kind) {
        case "tag": return tagBg
        case "due": return chipDueBg
        case "overdue": return chipOverdueBg
        case "scheduled": return chipScheduledBg
        case "reminder": return chipReminderBg
        case "alarm": return chipAlarmBg
        case "recurrence": return chipRecurrenceBg
        case "done": return chipDoneBg
        default: return chipNeutralBg
        }
    }
    /// The text colour of a chip variant (chip()).
    function chipFg(kind: string): color {
        switch (kind) {
        case "tag": return tagFg
        case "due": return chipDueFg
        case "overdue": return chipOverdueFg
        case "scheduled": return chipScheduledFg
        case "reminder": return chipReminderFg
        case "alarm": return chipAlarmFg
        case "recurrence": return chipRecurrenceFg
        case "done": return chipDoneFg
        default: return chipNeutralFg
        }
    }
    /// The default icon of a chip variant (chip()); "" for neutral.
    function chipIcon(kind: string): string {
        switch (kind) {
        case "tag": return "hash"
        case "due": return "calendar"
        case "overdue": return "calendar"
        case "scheduled": return "hourglass"
        case "reminder": return "bell"
        case "alarm": return "alarm-clock"
        case "recurrence": return "repeat"
        case "done": return "check"
        default: return ""
        }
    }

    /// Dot colour for date marks (BDatePicker): due, overdue, scheduled, reminder, alarm,
    /// journal, done, event.
    function markColor(kind: string): color {
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
    readonly property color _selectionBase: dark ? theme.gridBlue.alpha(0.30) : theme.gridBlue.alpha(0.55)
    readonly property color selection: theme._pick("selection", theme._selectionBase)
    readonly property color _selectionFgBase: fg
    readonly property color selectionFg: theme._pick("selectionFg", theme._selectionFgBase)
    readonly property color _markBase: dark ? theme.amber.alpha(0.30) : theme.amber.alpha(0.38)
    readonly property color mark: theme._pick("mark", theme._markBase)
    readonly property color _focusRingBase: dark ? "#9fc3e6" : "#3b7dc0"
    readonly property color focusRing: theme._pick("focusRing", theme._focusRingBase)
    readonly property color _diffAddBgBase: dark ? theme._successDarkBase.alpha(0.13) : theme._successLightBase.alpha(0.13)
    readonly property color diffAddBg: theme._pick("diffAddBg", theme._diffAddBgBase)
    readonly property color _diffAddStrongBase: dark ? theme._successDarkBase.alpha(0.32) : theme._successLightBase.alpha(0.30)
    readonly property color diffAddStrong: theme._pick("diffAddStrong", theme._diffAddStrongBase)
    readonly property color _diffAddFgBase: successText
    readonly property color diffAddFg: theme._pick("diffAddFg", theme._diffAddFgBase)
    readonly property color _diffDelBgBase: dark ? theme.marginRed.alpha(0.14) : theme.marginRed.alpha(0.11)
    readonly property color diffDelBg: theme._pick("diffDelBg", theme._diffDelBgBase)
    readonly property color _diffDelStrongBase: dark ? theme.marginRed.alpha(0.36) : theme.marginRed.alpha(0.28)
    readonly property color diffDelStrong: theme._pick("diffDelStrong", theme._diffDelStrongBase)
    readonly property color _diffDelFgBase: dangerText
    readonly property color diffDelFg: theme._pick("diffDelFg", theme._diffDelFgBase)
    readonly property color _scrollbarBase: dark ? theme._fgDarkBase.alpha(0.22) : theme.ink.alpha(0.24)
    readonly property color scrollbar: theme._pick("scrollbar", theme._scrollbarBase)
    readonly property color _switchThumbBase: dark ? "#dfe2e8" : "#ffffff"
    readonly property color switchThumb: theme._pick("switchThumb", theme._switchThumbBase)
    readonly property color _shadowBase: dark ? theme._black.alpha(0.5) : theme.ink.alpha(0.18)
    readonly property color shadow: theme._pick("shadow", theme._shadowBase)

    // ---- Typography ----------------------------------------------------------------------
    // The defaults are the bundled fonts (src/app/ui/uifonts.h): Inter for the interface,
    // Literata for note text (serif), JetBrains Mono for code, dates and meta text — the system's
    // when one did not load. A chosen family missing on this device (chosen elsewhere,
    // uninstalled) gives the default.
    // The theme's overrides (b.app.theme: fontUi, fontSans, fontSerif, fontMono) win over these,
    // when installed (_pickFont).
    readonly property string _fontUiBase: uiFontFamily !== "" && DeviceInfo.hasFontFamily(uiFontFamily)
                                          ? uiFontFamily : DeviceInfo.defaultUiFamily
    readonly property string _fontMonoBase: monoFontFamily !== "" && DeviceInfo.hasFontFamily(monoFontFamily)
                                            ? monoFontFamily : DeviceInfo.defaultMonoFamily
    readonly property string fontUi: theme._pickFont("fontUi", theme._fontUiBase)
    readonly property string fontSerif: theme._pickFont("fontSerif", DeviceInfo.defaultSerifFamily)
    readonly property string fontSans: theme._pickFont("fontSans", DeviceInfo.defaultUiFamily)
    readonly property string fontMono: theme._pickFont("fontMono", theme._fontMonoBase)
    /// The family `token` of the theme's overrides when one is set and installed, else `base`.
    function _pickFont(token: string, base: string): string {
        const family = "" + theme.palette.pickText(theme.palette.overrides, token, "")
        return family !== "" && DeviceInfo.hasFontFamily(family) ? family : base
    }
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
    /// Line height in pixels for `pixelSize` (ratio undefined: lineHeight). Compiled callers pass
    /// both arguments (`Theme.lineHeightPx(px, Theme.lineHeight)`): a call with fewer does not compile.
    function lineHeightPx(pixelSize: real, ratio: var): int {
        return Math.round(pixelSize * (ratio === undefined ? lineHeight : ratio))
    }
    /// The UI font at the base size (fontUi, textBase) — a default for a component's `font`.
    property font fontUiBase
    fontUiBase.family: fontUi
    fontUiBase.pixelSize: textBase
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

    // Soft shapes (chat apps rather than an IDE): controls 10, cards and menus 14, dialogs 22,
    // sheets from an edge 28 at their free corners; chips, search fields, the phone's navigation
    // and toggles are capsules (radiusFull or height / 2).
    // (Each overridable: palette.pickNumber() gives the theme's override, else the value.)
    readonly property int radiusXs: palette.pickNumber(palette.overrides, "radiusXs", 4)
    readonly property int radiusSm: palette.pickNumber(palette.overrides, "radiusSm", 7)
    readonly property int radius: palette.pickNumber(palette.overrides, "radius", 10)
    readonly property int radiusLg: palette.pickNumber(palette.overrides, "radiusLg", 14)
    readonly property int radiusXl: palette.pickNumber(palette.overrides, "radiusXl", 22)
    readonly property int radiusSheet: palette.pickNumber(palette.overrides, "radiusSheet", 28)
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
    /// Dense list rows (the sidebar's) with a mouse; on touch UIs dense rows are touchTarget.
    readonly property int listItemHeightDense: dense ? 26 : 30
    /// The touch editing toolbar docked on the keyboard (MobileEditToolbar): a row of small
    /// buttons with a hairline above.
    readonly property int editToolbarHeight: touchTarget + space2 + 1

    readonly property int measure: 680          // editor's comfortable text width (~85 characters)
    readonly property int measureList: 760      // a list of entries' width (the calendar's agenda: time, title, page)
    readonly property int sidebarWidth: dense ? 232 : 256  // the page panel, beside the rail of views
    readonly property int dialogWidthSm: 400
    readonly property int dialogWidth: 520
    readonly property int dialogWidthLg: 760
    readonly property int focusRingWidth: palette.pickNumber(palette.overrides, "focusRingWidth", 2)

    // ---- Elevation (BShadow) --------------------------------------------------------------
    /// { blur, offsetY, spread, opacity } for levels 0 (flat) … 3 (dialogs); each typed:
    /// elevationBlur(), elevationOffsetY(), elevationSpread(), elevationOpacity().
    function elevation(level: int): var {
        return { blur: elevationBlur(level), offsetY: elevationOffsetY(level), spread: elevationSpread(level),
                 opacity: elevationOpacity(level) }
    }
    function elevationBlur(level: int): real {
        switch (level) {
        case 1: return 6
        case 2: return 18
        case 3: return 40
        default: return 0
        }
    }
    function elevationOffsetY(level: int): real {
        switch (level) {
        case 1: return 1
        case 2: return 4
        case 3: return 14
        default: return 0
        }
    }
    function elevationSpread(level: int): real {
        return level === 3 ? -4 : 0
    }
    function elevationOpacity(level: int): real {
        switch (level) {
        case 1: return dark ? 0.35 : 0.10
        case 2: return dark ? 0.45 : 0.14
        case 3: return dark ? 0.6 : 0.22
        default: return 0
        }
    }

    // ---- Motion --------------------------------------------------------------------------
    readonly property int durationInstant: reducedMotion ? 0 : 70
    readonly property int durationFast: reducedMotion ? 0 : 120
    readonly property int duration: reducedMotion ? 0 : 200
    readonly property int durationSlow: reducedMotion ? 0 : 320
    /// A control pressed shrinks a little (buttons, the navigation's items) and springs back.
    readonly property real pressScale: reducedMotion ? 1 : 0.96
    readonly property int easing: Easing.OutCubic
    readonly property int easingEnter: Easing.OutQuint
    readonly property int easingExit: Easing.InCubic

    // ---- Helpers -------------------------------------------------------------------------
    /// `c` with alpha `a` (0–1).
    function alpha(c: color, a: real): color {
        // Clamped like Qt.rgba() (color.alpha() warns outside [0, 1]).
        return c.alpha(Math.max(0, Math.min(1, a)))
    }
}
