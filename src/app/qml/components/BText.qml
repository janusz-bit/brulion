pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BText — themed text with typographic presets, so views do not repeat font settings.
//
//   BText { text: qsTr("Synchronizacja"); variant: "title" }
//
// variant: "body" (default) | "muted" | "faint" | "small" | "caption" | "label" (medium
// weight) | "title" | "heading" | "display" | "mono". Plain text by default: never set
// textFormat to RichText/StyledText for note content unless it was escaped (see
// TextHelper.markMatches / the core InlineRenderer).
Text {
    id: label

    property string variant: "body"

    color: variant === "muted" || variant === "caption" ? Theme.fgMuted
         : variant === "faint" ? Theme.fgFaint
         : Theme.fg
    font.family: variant === "mono" ? Theme.fontMono : Theme.fontUi
    font.pixelSize: {
        switch (variant) {
        case "small": case "caption": return Theme.textSm
        case "title": return Theme.textLg
        case "heading": return Theme.textXl
        case "display": return Theme.text2xl
        case "mono": return Theme.textSm
        default: return Theme.textBase
        }
    }
    font.weight: variant === "title" || variant === "heading" || variant === "display" ? Theme.weightSemiBold
               : variant === "label" ? Theme.weightMedium
               : Theme.weightNormal
    lineHeightMode: Text.FixedHeight
    lineHeight: Theme.lineHeightPx(font.pixelSize, variant === "title" || variant === "heading" || variant === "display"
                                                   ? Theme.lineHeightTight : Theme.lineHeight)
    textFormat: Text.PlainText
    linkColor: Theme.link
}
