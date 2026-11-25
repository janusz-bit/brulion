pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartTagTint — the tint of a styled tag of a block that has one, made by a BlockDelegate Loader through
// BlockParts.part("TagTint") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block).
Rectangle {
    id: tintFrame
    final property BlockDelegate block: BlockViews.ancestor(tintFrame, 2) as BlockDelegate
    objectName: "tagTint"
    readonly property int mode: tintFrame.block.rowData.tagTintMode
    /// The colour of `tint`: the strong one (the bar) or the soft one (the background) — the
    /// theme's own pair for its names, a translucent shade of a "#rrggbb" in both themes.
    function tagTintColor(tint: string, strong: bool): color {
        switch (tint) {
        case "accent": return strong ? Theme.accent : Theme.accentBg
        case "success": return strong ? Theme.success : Theme.successBg
        case "danger": return strong ? Theme.danger : Theme.dangerBg
        case "warning": return strong ? Theme.warning : Theme.warningBg
        case "info": return strong ? Theme.info : Theme.infoBg
        case "neutral": return strong ? Theme.borderStrong : Theme.bgSunken
        }
        return strong ? tagTintHex : tagTintHex.alpha(Theme.dark ? 0.2 : 0.13)
    }
    /// The tint as a colour when it is one ("#rrggbb").
    readonly property string tint: tintFrame.block.tagTint
    readonly property color tagTintHex: Js.startsWith(tint, "#") ? tint : "transparent"
    radius: Theme.radiusSm
    color: (mode & 1) !== 0 ? tintFrame.tagTintColor(tintFrame.tint, false) : "transparent"
    Rectangle {
        objectName: "tagTintBar"
        visible: (tintFrame.mode & 2) !== 0
        width: 3
        height: tintFrame.height
        radius: 1.5
        color: tintFrame.tagTintColor(tintFrame.tint, true)
    }
}
