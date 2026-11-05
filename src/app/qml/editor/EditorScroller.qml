pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// EditorScroller — what scrolls a BlockEditor instead of its own list (BlockEditor.scroller: the
// journal stream for its days, JournalsView). This is its interface: the stream is an
// EditorScroller that overrides every function below; a BlockEditor calls them through its typed
// `scroller` (compiled ahead of time, docs/UI.md "Compiled QML"). The functions here do nothing.
FocusScope {
    /// How far above and below the view a day stays created (a drag selecting into the days
    /// around one scrolls only that far): 0 = no other day.
    property real selectionReach: 0

    /// The editor of the day at `scene` (scene coordinates), null when none is there.
    function editorAt(scene: point): BlockEditor { return null }
    /// Keep `rect` of `item` (e.g. a caret) inside the visible part.
    function ensureVisible(item: Item, rect: rect): void {}
    /// Typewriter scrolling: `rect` of `item` to the middle of the visible part.
    function centerInView(item: Item, rect: rect): void {}
    /// Scroll by `dy` within the content.
    function scrollBy(dy: real): void {}
    /// A finger selecting blocks: the stream must not flick meanwhile (`held`), or may again.
    function holdScrolling(held: bool): void {}
}
