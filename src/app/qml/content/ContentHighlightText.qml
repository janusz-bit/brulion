import QtQuick
import Brulion

// ContentHighlightText — one line of plain text (a title, a search excerpt) with the characters
// at `positions` (UTF-16 offsets from the content models: search words, list filters) in bold
// accent colour. The text is escaped (TextHelper.markMatches), so note text is never markup.
//
//   ContentHighlightText { source: model.title; positions: model.titleMatches }
Text {
    id: label

    /// The plain text.
    property string source
    /// Offsets of the highlighted characters, ascending.
    property var positions: []
    property color highlightColor: Theme.accentText

    text: positions && positions.length > 0 ? TextHelper.markMatches(source, positions, String(highlightColor))
                                            : TextHelper.escapeHtml(source)
    textFormat: Text.StyledText
    color: Theme.fg
    font.family: Theme.fontUi
    font.pixelSize: Theme.textBase
    elide: Text.ElideRight
    Accessible.name: source
}
