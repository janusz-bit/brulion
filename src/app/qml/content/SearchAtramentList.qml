pragma ComponentBehavior: Bound

import QtQuick
import Brulion

// SearchAtramentList — SearchView's results drawn by Atrament (AppSettings.viewsAtrament, Settings →
// Editor, or `--atrament-views`; docs/ATRAMENT.md "The views in Atrament (M3)", Search): the rows of
// SearchView's SearchResultModel as an Atrament list (views/atrament/searchview.h). SearchView loads
// it by file name where Atrament is built and shows it in its ListView's place; a result clicked
// (viewAction "open") goes to SearchView's atramentAction(), the field keeping the keyboard (its ↑ /
// ↓ move the current result: viewRevealRow).
AtramentView {
    id: list

    /// SearchView (an Item: its function is called by name).
    property Item searchView
    /// The rows' inset on each side (SearchView.sideMargin).
    property real sideMargin: Theme.space6

    view: "search"
    viewLook: ({
        background: Theme.bg,
        text: Theme.fg,
        muted: Theme.fgMuted,
        faint: Theme.fgFaint,
        match: Theme.accentText,
        hover: Theme.bgHover,
        selected: Theme.bgSelected,
        textSize: Theme.textBase,
        smallSize: Theme.textXs,
        lineHeight: Theme.lineHeightPx(Theme.textBase, 1.45),
        sideMargin: list.sideMargin,
        radius: Theme.radius,
        uiFamily: Theme.fontUi
    })
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Search results")
    onViewAction: (action, args) => QmlCall.call2(list.searchView, "atramentAction", action, args)
}
