# Atrament — Brulion's own UI framework

Atrament ("ink") is a GPU-first, retained-state / immediate-tree UI framework in C++20, built
after the architecture of Zed's GPUI, to replace Qt Quick and QML in Brulion. It draws every pixel
itself — boxes, text, pictures — from a flat list of primitives, the way a game engine or a modern
code editor does, and talks to the operating system directly where Qt sits in between today.

This document is the design and the roadmap. It is the reference for anything under
`src/atrament/`; a change to a decision recorded here changes this document first.

## Why

Measured on 0.30 (docs/PERFORMANCE.md): about 130 MB of memory a quarter of a minute after the
start with a small notebook, 0.1–0.2 MB per prepared block in the editor's list, an Android APK of
43.5 MB to download, about 17 MB for a first visit of the browser version. The core — parsing,
the index, sync, the calculator, Luau — is a minority of all of that; Qt Quick, the QML engine
and the objects behind 334 QML files are the majority.

What Brulion's UI is made of — long scrolling lists of rich text blocks, an outline, a calendar,
a code view with a terminal — is exactly what GPUI was built for: a frame is a few thousand
quads and glyphs, drawn in a dozen GPU calls, from a tree of elements rebuilt only when state
changes.

No existing framework covers what Brulion needs at once — Android with a complete input method
(composition, autocorrect, gesture typing, selection handles), the browser, accessibility, and
rich text editing — and no existing framework is written in the language of Brulion's 200 000
lines of core. The missing parts would be written by us in any case; Atrament makes them ours
from the start.

## Goals and non-goals

Goals, in order:

1. **Light.** Memory proportional to what is on screen; no interpreter, no reflection, no object
   per row of a list; a release binary measured in single megabytes for the framework itself.
2. **Native rendering.** Our own renderer on the platform's GPU API (WebGPU everywhere it exists:
   Vulkan, Metal, D3D12 natively, the browser's WebGPU on the web), with a CPU renderer of the same
   scene as the reference and the fallback.
3. **Text first.** Shaping, line breaking, hit testing, carets, selections and input-method
   composition are the framework's core, not a widget.
4. **Every platform Brulion ships on:** Linux (Wayland, X11), Windows, Android, the browser; macOS
   and iOS when Brulion goes there.
5. **Testable and deterministic.** Every frame can be produced headless, bit-for-bit, from a
   simulated input sequence and a simulated clock.
6. **Incremental adoption.** Atrament runs inside today's Qt Quick shell, one view at a time, until
   nothing of Qt Quick is left.

Non-goals: native-looking widgets (Brulion has its own design system), a declarative markup
language, runtime reflection or scripting of the UI from QML/JavaScript (Luau extensions get a
typed element API instead), print support.

## Decisions

| # | Decision | Why |
|---|---|---|
| D1 | Own framework, GPUI's architecture, C++20 | The core is C++; no FFI; the text engine is the product. |
| D2 | `src/atrament/` uses no Qt at all; Qt appears only in the host adapter `src/atrament/qt/` | The framework must outlive Qt Quick in Brulion, and the core of Atrament compiles for any target as plain C++. |
| D3 | Text is UTF-16 (`char16_t`, `std::u16string_view`) | Offsets are `QString` indices and `BlockDocument` positions: an editor caret needs no conversion, and a `QStringView` passes without a copy. HarfBuzz shapes UTF-16 natively. |
| D4 | The element tree is rebuilt for every frame, in a frame arena; state lives in entities (views, models) and in element state keyed by `ElementId` | GPUI's model: no stale bindings, no per-element objects that outlive a frame, allocation is a pointer bump. |
| D5 | A frame produces a `Scene`: a flat, ordered list of primitives (quads, shadows, glyphs, pictures, paths, underlines) | Any renderer draws it; the GPU renderer batches it into a handful of instanced draws. |
| D6 | The CPU renderer (`raster`) is the reference renderer | Golden-image tests on every platform without a GPU; the first host renders with it; a fallback for devices without a usable GPU. |
| D7 | Fonts: HarfBuzz for shaping and for glyph outlines (`hb-draw`), our own coverage rasterizer, a glyph atlas; no FreeType, no hinting, subpixel x positions in quarters of a pixel | One code path on every platform, pixel-identical output, the fonts Brulion already bundles (Inter, Literata, JetBrains Mono, Brulion Emoji, Brulion Math). Color glyphs (COLRv1, `hb-paint`) come later. |
| D8 | Line breaking and grapheme clusters: our own implementation of the parts of UAX #14 / #29 Brulion's languages need, tables later generated from the Unicode data; bidirectional text later | Small, no ICU (≈30 MB of data). |
| D9 | Layout: our own flexbox (the CSS algorithm's subset Brulion uses: direction, justify, align, gap, padding, margin, grow/shrink/basis, min/max, absolute position, measured leaves) | GPUI's Taffy is Rust; Yoga brings a node object per element; the tree is ours already. |
| D10 | Input: hitboxes registered in prepaint, listeners in paint, dispatched in reverse paint order with a capture and a bubble phase; focus handles; actions and keymaps in key contexts; an `InputHandler` interface for input methods modelled on NSTextInputClient / Android `InputConnection`; a finger's or a pen's drags go through the window's touch gesture first — slop, the innermost container that can scroll that way, a fling on Android's curve ("Touch" below) | GPUI's proven model; the IME interface maps 1:1 onto every platform's; scrolling by touch is the window's, so every scroll container gets it and no element has to tell a tap from a drag. |
| D11 | Execution: one main thread, a platform dispatcher, timers; a test platform with a virtual clock and deterministic ordering | Timing-free tests; no flaky waits. |
| D12 | First host: `AtramentItem`, a `QQuickItem` that draws a window's scene with the QRhi GPU renderer in a `QSGRenderNode`, inside Qt Quick's own render pass (the CPU renderer into a texture where Qt Quick has no QRhi, the GPU lacks OpenGL ES 3, or `BRULION_ATRAMENT_RENDERER=cpu`), and translates Qt's events and input-method queries | Views move one by one; Qt keeps providing windows, input methods and accessibility until our own platform layers replace it. |
| D13 | Accessibility: elements describe themselves (role, name, value, state, actions, bounds) into a semantic tree each frame; hosted, a bridge to `QAccessibleInterface`; standalone, AccessKit's C API | Accessibility from the first view that ships, on every platform. Done hosted ("Accessibility" below): the tree in `ui/accessibility.h`, the bridge in `qt/qtaccessibility.h` (tested through `QAccessible`; the gallery read by an AT-SPI client); AccessKit with the own platforms (M4). |
| D14 | Styling with GPUI-like builder methods (`p(12)`, `gap(8)`, `rounded(10)`, `bg(…)`) over theme tokens | Code completion, no style language to parse, compiled. |
| D15 | Geometry in logical pixels (`float`), snapped to device pixels when painted | One scale factor per window; crisp edges at fractional scales. |

## Architecture

```
┌ Brulion views (src/app/…): Entities with render() ────────────────────────────────────┐
├ ui      App · Entity<T> · Context<T> · Window · elements (div, text, img, list, canvas, │
│         input) · events · focus · actions/keymaps · element state · test platform      │
├ layout  flexbox over the frame's nodes, measured leaves                                │
├ text    font collection + fallback · shaping (HarfBuzz) · glyph rasterizer · atlas ·   │
│         line layout · paragraph wrapping · carets / hit tests / selection rectangles   │
├ scene   primitives, draw order, content masks                                          │
├ raster  CPU renderer of a scene (reference, tests, fallback)                            │
├ gpu     QRhi renderer (hosted, in qt/) · WebGPU renderer (standalone) · WebGL2 fallback │
├ base    geometry · colours · arena · ids · small callables                             │
└ platform interface: windows, events, input method, clipboard, cursors, timers, frames ┘
   hosts / platforms: qt (QQuickItem; now) · wayland · x11 · win32 · android · web (later)
```

### A frame

1. **Render.** The window's root view's `render()` returns an element tree; child views render
   when reached (a view not notified since the last frame may reuse its previous elements' paint
   later — view caching, as in GPUI).
2. **Request layout.** Each element turns its style into a layout node (leaf nodes such as text
   give a measure function).
3. **Compute layout** for the window's size.
4. **Prepaint.** Elements learn their bounds, register hitboxes, update element state (a scroll
   offset clamped to the content), lay out text at its final width.
5. **Paint.** Elements append primitives to the scene and register event listeners on their
   hitboxes.
6. **Present.** The renderer draws the scene; the platform presents it. The hovered hitboxes are
   recomputed from the mouse position; a change of hover requests another frame only where a
   style depends on it.

The arena is reset when the next frame starts. Nothing in it outlives the frame, so listeners
capture entity handles (cheap ids), never pointers into the tree.

### The GPU renderer

`src/atrament/qt/qrhirenderer.h`, after GPUI's Metal/Blade/D3D11 renderers, on whatever Qt Quick
runs on (Vulkan, Metal, Direct3D, OpenGL ES 3 / OpenGL 3.3):

* **Batches.** `GpuFrameBuilder` (`gpuframe.h`, Qt-free) turns the scene into instances of five
  shader kinds — quads, shadows, monochrome sprites, images (colour sprites and pictures), wavy
  underlines — and one batch per run of the same kind and texture in painter's order: one
  instanced draw each (≈ 10 for a busy frame of 7 000 primitives). Every value a shader needs is
  computed there with the CPU renderer's arithmetic, so each fragment shader is the CPU renderer's
  per-pixel formula; blending is premultiplied source-over in sRGB space, as the CPU's.
* **Content masks** cut each instance's rectangle in whole pixels on the CPU, as the CPU renderer
  cuts each primitive: the same pixels as GPUI's test in the fragment shader, without the
  fragments.
* **Paths** are rasterized on the CPU (`CoverageRasterizer`) into tiles of a path atlas, cached by
  content and subpixel offset, and drawn as monochrome sprites.
* **The atlas** is mirrored as textures (A8 → R8): what changed since a renderer's copy goes up —
  the dirty rectangle, or the whole page when another window's renderer cleaned it since
  (`AtlasPage::cleanRevision`) — copied in Qt Quick's sync, uploaded before its render pass.
* **Opacity** inherited from Qt Quick multiplies every primitive (as Qt Quick fades its own nodes;
  the CPU fallback's texture fades as one image).
* Shaders: Vulkan GLSL 440 in `src/atrament/qt/shaders/`, compiled by `qt_add_shaders` (Qt Shader
  Tools' `qsb`, in every build's Qt: SPIR-V for Vulkan, GLSL ES 3.0 for Android's OpenGL ES and the
  browser's WebGL 2, GLSL 3.30, HLSL for Direct3D 11, MSL; a Qt without Shader Tools builds the
  host with the CPU renderer only). Measured
  against the CPU renderer on every primitive kind, masks, scales and atlas updates within 2 levels
  per channel (3 where translucent layers stack) on Intel, NVIDIA and llvmpipe Vulkan and on
  llvmpipe OpenGL (`tst_atrament_qt`).

### Views and entities

```cpp
class Counter {
public:
    int count = 0;
    AnyElement render(Window& window, Context<Counter>& cx)
    {
        return div().flexCol().gap(8).p(16).bg(theme.surface).rounded(12)
            .child(text(u"Kliknięcia: "s + toU16(count)))
            .child(button(u"Dodaj").onClick(cx.listener(
                [](Counter& self, const ClickEvent&, Window&, Context<Counter>& cx) {
                    ++self.count;
                    cx.notify();
                })));
    }
};
```

`App::newEntity<T>(…)` returns an `Entity<T>`, a counted handle to state the app owns;
`entity.update(app, fn)` mutates it, `cx.notify()` marks the windows that drew it for a frame.
`cx.listener(fn)` turns a member-style callback into a listener that holds a weak handle.

### Text

* `FontCollection`: faces registered from bytes (bundled fonts) or files; families by name with
  weight and style; a fallback chain searched by codepoint coverage, so an emoji or a math sign
  finds its face without the caller choosing one.
* `shapeLine(text, runs)`: splits into runs of one face (itemization by coverage), shapes each
  with HarfBuzz, returns glyphs with x positions and the UTF-16 cluster they came from.
* `TextLayout`: a paragraph wrapped at a width — lines of glyph runs, ascent/descent/line height,
  and the editing queries: caret position for an offset, offset for a point, selection rectangles
  for a range, the next/previous grapheme and word boundary.
* **Background shapes** (`TextBackground`, `ui/style.h`: a span's `style.background`): a pill
  (`pill`), a rounded box (`radius`) or a square box behind a span's glyphs, `paddingX` beyond them
  on each side, `inset` in from their extent — the fonts' ascent above and descent below the
  baseline (`TextBackgroundExtent::Glyphs`, CSS's content area: the default) or the line box
  (`Line`). One rounded quad per line the span shows on (`TextLayout::glyphRects`: caret to caret,
  no hanging spaces at a wrap, the extent of the runs' own fonts — an emoji from the fallback face
  makes no shape taller), each a whole shape, rounded and padded at both ends (CSS's
  `box-decoration-break: clone`); adjacent pieces with equal backgrounds are one shape (a
  highlight with a bold word in it), `group` keeps two of one look apart (two tags side by side).
  The layout reserves no room for a shape: padding reaches under the neighbours (a chip's room is
  the no-break spaces inside it, as in the QML editor), so lines and rows keep their sizes.
  `Window::paintText` draws the paragraph's shapes, then its glyphs and decorations; an element
  with a selection draws `paintTextBackgrounds`, its selection, `paintTextGlyphs` — a selection is
  over the shapes and under the glyphs.
* **Inline boxes**: a run laid out as one box of a width, an ascent and a descent instead of its
  glyphs (`FontRun::inlineBox`; in `ui`, `Text::inlineObject(range, size, painter)` or a span's
  `object`, the size in px or in ems of the text there) — an icon over the no-break spaces
  reserved for it, a checkbox at U+FFFC. The line reserves its width and counts its ascent and
  descent as a font's; it wraps as one unit (no break inside it; before and after it where its
  characters allow one — U+FFFC breaks around it as UAX #14's CB); carets and hit tests see one
  cluster (`nextCaretStop` / `previousCaretStop`, `offsetAt` never inside it); `inlineBoxes()`
  says where each ended up. `Text` calls the object's painter in paint, after the text, with its
  box in window coordinates, the baseline, the text's colour and size there.
* Glyphs are rasterized once per (face, glyph, size, subpixel x offset) into an A8 atlas; the
  renderers read the same atlas.
* Caches are generational: what was used in the last frame stays, the rest is dropped at the end
  of the next.

### Input methods

```cpp
class InputHandler {
    virtual std::u16string textForRange(Range r) = 0;
    virtual std::optional<Selection> selectedRange() = 0;
    virtual std::optional<Range> markedRange() = 0;          // the composition (preedit)
    virtual void replaceText(std::optional<Range> r, std::u16string_view text) = 0;
    virtual void replaceAndMark(std::optional<Range> r, std::u16string_view text,
                                std::optional<Range> selectedWithinMarked) = 0;
    virtual void unmark() = 0;
    virtual std::optional<Rect> boundsForRange(Range r) = 0; // candidate window position
    virtual std::optional<int> offsetForPoint(Point p) = 0;
};
```

A focused element that edits text installs one for the frame; the host maps it onto Qt's
`inputMethodQuery` / `QInputMethodEvent`, later onto `text-input-v3`, `InputConnection`, TSF and
a hidden `<textarea>` in the browser.

**Through Qt** (`qt/qthost.h`): Qt's input methods — its Android input context over
`InputConnection`, IBus and Fcitx, the browser's hidden input, Windows' — expect a field as Qt's own
controls keep it, with the preedit *outside* the text, at the caret; Atrament's marked text is part
of the text (as in NSTextInputClient). `AtramentItem` converts both ways: the text it reports
(`ImSurroundingText`, the text before and after the caret, the selection) leaves the composition
out, the caret and anchor sit where it is, and a `QInputMethodEvent` is applied with Qt's meaning —
the old preedit goes, the commit replaces `replacementStart` / `replacementLength` around the caret
(typing replaces a selection), a `Selection` attribute sets caret and anchor, the new preedit is
marked at the caret with the `Cursor` attribute inside it — with the common cases one handler call
each, so that a composition stays one undo step (an input method that marks a word already there,
Android's `setComposingRegion`, adds none). The item also answers the queries with an argument
(`ImCursorPosition` at a point, for Android's selection handles; the text before or after the
caret of a length), emits `cursorPositionChanged()` whenever the field's text, selection or
composition changes (Android's input context listens to it and tells the keyboard:
`InputMethodManager.updateSelection`), sets the input item's rectangle to the field, shows the
virtual keyboard when a field takes the focus or is tapped and hides it when none has the focus,
asks the input method to commit before a press and makes it forget a composition that ended
without it (another field, a press, the focus gone; the field keeps the text). It has an
`inputMethodHints` property: Qt's Android input context drops every call of the keyboard's
`InputConnection` to a focus object without one (Qt Quick's text fields have it). A screen reader
moving its focus onto a field opens no keyboard; activating it does. Tested with the event
sequences of Qt's Android input context for Gboard (`tests/atrament/qt/tst_atrament_qt_input`).

A field keeps its caret in view (`Window::revealRect`: the scroll containers around an element —
scrolling `div()`s and `uniformList`s push themselves in prepaint — scroll the least that shows a
rectangle after the frame): when it takes the focus, when the caret or the text changes, and when
the window gets another size, which is how an on-screen keyboard reaches Atrament (the QML around
ends the view at the keyboard's top). A `uniformList` keeps the row with the focus in its frame
while that row is scrolled away (painted outside the view, so unseen): the field keeps its input
method, and comes back into view when a keyboard covers it or the caret moves. A finger's tap only
places the caret: selecting is the platform's long press and handles; a finger's or a pen's drag
scrolls (below), only a mouse's selects.

### Touch

A finger's (or a pen's) press, drag and release go through the window's touch gesture
(`ui/touch.h`, `Window::dispatchTouch`) before any element sees them. A mouse never scrolls by
dragging; the wheel does as before.

* **Which container.** Every scrolling `div()` and `uniformList` registers itself with its hitbox
  in prepaint. When a drag passes the slop (`Platform::touchSlop`: Qt's `startDragDistance`, 10 px;
  Android's 8 dp on the test platform), the innermost container under the press that can still
  move in the drag's direction takes it — vertical when the drag is at least as vertical as
  horizontal — for the whole drag: latched, as browsers and Android's lists do, so an inner list
  reaching its end leaves the page around it to the next drag, not to the rest of this one. A drag
  nothing takes stays the elements' (a slider's thumb, a sideways drag over a vertical list) and
  never becomes a scroll.
* **1:1.** The content follows the finger from where the drag passed the slop (no jump by it).
* **A press waits.** Inside a container that can scroll, the elements get a finger's press after
  100 ms (Android's tap timeout): a drag that starts sooner is a scroll they never hear of — a
  field it started on is not focused, no keyboard opens, its caret stays —, a release is a tap
  (the press and the release at once: a click), a finger that stays gives them the press (pressed
  looks). Outside scroll containers the press is immediate.
* **A scroll clicks nothing.** When a drag becomes a scroll, the press the elements had is
  cancelled: a `MouseUpEvent` with `cancelled`, dispatched over nothing, so nothing is clicked and
  pressed looks end. The hover goes too: a finger hovers nothing, no hover look follows the content
  under it or stays behind after a tap.
* **Fling.** The release velocity is a least-squares quadratic over the finger's last 100 ms of
  samples, its slope at the newest (Android's and Flutter's `VelocityTracker`), zero when the
  finger rested 40 ms. The fling follows Android's `OverScroller`: the distance and the duration
  from the velocity (friction 0.015, deceleration rate ln 0.78 / ln 0.9, gravity at 160 px per
  inch — a logical px is Android's dp), the position along its spline, the Bézier with time
  (0, 0.175, 0.35, 1) and position (0, 0.5, 1, 1); flings below 50 px/s do not start, faster than
  8000 px/s are capped (ViewConfiguration); each axis on its own. 2500 px/s glides 954 px in
  1.09 s, 5000 px/s 3178 px in 1.81 s — a hard flick crosses a long list, as on Android.
* **Frames.** A fling moves its container at the start of each frame to where the curve is at the
  platform's time, and asks for the next one (`PlatformWindow::requestAnimationFrame`). Qt Quick
  builds it when its render loop starts the next frame, paced by the display (asked from inside a
  frame, the item is polished after that pass: within it, Qt Quick would polish it again at once,
  a busy loop with nothing shown); the test platform draws it at the next 16 ms of its virtual
  clock (`TestWindow::advance`).
* **Stops.** A finger on the flinging container stops it and drags it on at once, a press no
  element sees (a tap that stops a list opens no row); a press of the mouse or a wheel stops it
  too. An edge stops it on its axis: no bounce, no glow — Android 12's stretch needs a transform of
  the content's layer that the scene does not have yet, and a bounce would be the iOS look on
  Android.
* **Long press.** A finger held still for the long-press time (`Platform::longPressInterval`:
  Android's `ViewConfiguration.getLongPressTimeout()`, 400 ms by default; elsewhere Qt's
  press-and-hold interval) on the focused field scrolls nothing more: Android's input context
  selects the word under it then (`QAndroidInputContext::longPress`, from its own
  `GestureDetector`), and the finger moving on must not carry the selection and its handles away.
  Held anywhere else, the finger still scrolls when it moves, as native lists do — unless an
  element under it takes long presses (`div().onLongPress(f)`, `Window::onLongPress`): the topmost
  such listener runs at the finger's place, the elements' press is cancelled (the release clicks
  nothing) and the finger scrolls nothing until it lifts (a block's touch selection mode).
* **Through Qt** (`qt/qthost.h`) the item takes `QTouchEvent`s itself (`setAcceptTouchEvents`):
  the first finger down is the pointer until it lifts, with the events' times; `TouchCancel`, or
  the grab taken away (a Drawer's edge, a Flickable around: `touchUngrabEvent`, and
  `mouseUngrabEvent` while a press is still down — Qt Quick ungrabs after every release too), ends
  the press as cancelled. Not the mouse Qt Quick makes of a touch: that one says nothing when the
  touch is cancelled or stolen (a press stuck down, a scroll that never ends), brings hover events
  where the finger was and double clicks of its own; and the touch events have the second finger
  for a pinch later. A pen comes as the mouse Qt makes of it, of kind Pen, and scrolls as a finger.
* Left for later: two fingers (pinch, two-finger scroll), an overscroll effect, a drag an element
  claims in a container's own direction (a vertical slider in a vertical list: today the list
  takes it), smooth programmatic scrolling.

### Accessibility

While an assistive technology listens (`Window::setAccessibilityEnabled`, which the host sets),
paint also builds a **semantic tree** (`ui/accessibility.h`): nodes with a role, a name, a value,
a description, states (focusable, focused, selected, checked, disabled, expanded, editable,
multiline, offscreen…), actions and bounds in window pixels; a text input's text, selection and
laid-out text; a list's row count and each shown row's index; a scrolling node's offset. The nodes
live in the frame arena next to the elements and are known across frames by their element's id
path, as element state is — a screen reader follows a node from frame to frame, and a list's row
by its index. Nothing of it is built, and paint costs nothing more, while it is off.

Elements describe themselves:

| Element | Node |
|---|---|
| `div()` | none by default; a **button** with an `id` and `onClick`, a **checkbox** with `checked(…)`, a **scroll view** when it scrolls, a **group** when it tracks focus or has a label; `role(Role::…)` gives any role (`Role::None`: no node) |
| `Text` / a string | names the node around it when that one is named by its content (button, checkbox, link, heading, tab, list item, menu item — as on the web), else a **text** node of its own |
| `TextInput` | a **text input**: the text as its value, the selection and caret, `label` as its name and the placeholder as its description (the placeholder names it when there is no label) |
| `uniformList` | a **list** with its row count; each shown row a **list item** with its index, described by the row's root element (its click, `selected(…)`, its text as the name) |
| `img(…).alt(u"…")` | an **image**; without `alt` decoration |

The builder methods of `div()`: `role(Role)`, `ariaLabel(u"…")` (the name, in place of the text
inside), `ariaDescription`, `ariaValue`, `ariaLevel(n)` (a heading's), `checked(bool)`,
`selected(bool)`, `expanded(bool)`, `disabled()` (also keeps its click listeners from running),
`rangeValue(value, min, max, step)`, `ariaHidden()` (it and what is inside are left out — icons
beside their label, decoration, text that changes every frame), `onAccessibilityAction(action, f)`
(a slider's Increment / Decrement, a custom SetValue).

Actions of an assistive technology go through `Window::performAccessibilityAction(node, action)`
to the element's own listeners, as input does: Click (a `ClickEvent` with `fromAccessibility`),
Focus, SetValue / ReplaceText / SetSelection of a text input, ScrollIntoView (every scrolling node
around it moves the least that shows it), ScrollForward / ScrollBackward (a page less an eighth),
ScrollTo. After each frame the window compares the tree with the previous frame's and gives the
platform the differences (`PlatformWindow::accessibilityChanged`): nodes added and removed (the
topmost of each part), children changed, focus, name, description, value (with the old text),
selection, states, role, the focused node's bounds, scrolling.

**The Qt bridge** (`qt/qtaccessibility.h`): AtramentItem's `QAccessibleInterface` (from a factory,
so it answers before Qt Quick's) has the root node's children as its own; every node an
assistive technology asks for gets an object-less interface registered with Qt's cache — role and
states mapped, the text and editable-text interfaces for text, actions (Press, Toggle, SetFocus,
Increase / Decrease and Scroll Up / Down for scrolling — TalkBack scrolls forward with Increase —,
ShowOnScreen inside scrolling content), the value interface for ranges and the viewport interface
for scrolling (a list counts rows). On Android a selectable tab is also checkable, and checked when
selected: Qt's Android bridge (6.12) gives Android's nodes no selected state at all (it never calls
`AccessibilityNodeInfo.setSelected`), which is why the emulator's tree showed the gallery's tabs
without one — Qt Quick's own TabButton is a checkable button for the same reason. The item is
marked accessible as QML's `Accessible` would (`QQuickItemPrivate`, Qt6::QuickPrivate), so its
window lists it; `Accessible.name` set in QML names it. The tree is turned on when Qt says an assistive technology is active, or when one first
asks; an action draws its frame at once, so the next question sees its result. The tree's changes
leave as `QAccessibleEvent`s — Focus, NameChanged, TextUpdated, TextCaretMoved and
TextSelectionChanged, StateChanged one state at a time, ObjectCreated / ObjectShow and
ObjectHide / ObjectDestroyed, ObjectReorder, ScrollingPositionChanged — for the nodes an assistive
technology has seen (and the focus); a removed node's interface answers for its last place until
the event loop runs again.

## Writing a view

```cpp
#include "ui/atrament.h"
using namespace brulion::atrament;

class TaskRow {
public:
    TaskRow(Context<TaskRow> &cx, std::u16string title)
        : m_title(std::move(title))
        , m_note(cx.newEntity<TextInput>(TextInputOptions { u"Notatka…", true }))
    {
        // Another entity's events, while this view lives.
        m_subs.push_back(cx.subscribe<TextInput::Changed>(m_note,
                [](TaskRow &self, const TextInput::Changed &, Context<TaskRow> &cx) { cx.notify(); }));
    }

    AnyElement render(Window &window, Context<TaskRow> &cx)
    {
        return div()
                .flexRow().itemsCenter().gap(8).px(12).h(36).rounded(8)
                .hover([](Style &s) { s.background = Background::solid(rgb(0x2c2e33)); })
                .child(div().id(u"check").size(16).rounded(4).border(1.5f).borderColor(rgb(0x9a9ca4))
                               .checked(m_done).ariaLabel(u"Zrobione") // a checkbox for screen readers
                               .when(m_done, [](Div &&d) { return std::move(d).bg(rgb(0x8b93ff)); })
                               .onClick(cx.listener([](TaskRow &self, const ClickEvent &, Window &, Context<TaskRow> &cx) {
                                   self.m_done = !self.m_done;
                                   cx.notify();
                               })))
                .child(div().flex1().child(m_title).when(m_done, [](Div &&d) { return std::move(d).lineThrough(); }))
                .child(m_note);
    }

private:
    std::u16string m_title;
    bool m_done = false;
    Entity<TextInput> m_note;
    std::vector<Subscription> m_subs;
};
```

Rules of thumb:

* **State lives in entities** (views and models); elements are throwaway descriptions of one frame.
  A view's `render()` is called whenever its window draws: keep it cheap and free of side effects
  other than starting and stopping its own timers.
* **Listeners capture handles, never pointers into the tree** — `cx.listener(…)` does this for you.
  A pointer to the element itself (`this` inside an element's paint) is fine: the element and the
  listeners it registers live in the same frame arena.
* **Elements that keep state across frames need a stable identity**: `id(…)` on rows of lists whose
  order changes; without one an element is known by its index among its siblings.
* **Accessibility comes with the elements**: buttons, text, fields and lists describe themselves;
  name what has no text (`ariaLabel` on an icon button, `alt` on a picture), say what a custom
  control is (`role`, `checked`, `selected`, `expanded`), and leave decoration out (`ariaHidden`).
* **Strings**: `std::u16string` / `std::u16string_view`, offsets in UTF-16 code units — what
  `QString` and Brulion's core use, so text from the core passes without conversion.
* **Hosting in the Qt app**: `auto *item = new AtramentItem(parent); item->attach(app, view)` — the
  item is a `QQuickItem` like any other; one `App` serves every item of the process. In the app's
  QML, `AtramentView { view: "gallery" }` (src/app/ui/atramentview.h) shows a named view of the
  process's App; `brulion --atrament` opens it in a window of the design system
  (qml/dev/AtramentDemo.qml, developer QML: desktop builds and debuggable APKs — on a phone
  `adb shell am start -n io.github.januszbit.brulion.test/io.github.januszbit.brulion.android.BrulionActivity
  --es applicationArguments --atrament` for the APK of
  `BRULION_ANDROID_ID_SUFFIX=.test BRULION_ANDROID_BUILD_TYPE=Debug scripts/android-build.sh`: Qt passes start
  arguments only to a debuggable APK).

## Code

* `src/atrament/<area>/` — `base`, `scene`, `raster`, `text`, `layout`, `ui`, each a static
  library `brulion_atrament_<area>`; `src/atrament/qt` — the Qt Quick host, the QRhi renderer and
  the accessibility bridge (`brulion_atrament_qt`); `src/atrament/gallery` — a developer app showing
  every element (`atrament-gallery`; `--screenshot <file> [--renderer gpu]` renders a frame headless
  with either renderer and prints their times, `--accessibility` prints the frame's tree,
  `--grab <file>` saves what a Qt Quick window drew).
* Namespace `brulion::atrament`. No Qt, no exceptions thrown, no RTTI needed outside the host.
  Headers relative to `src/atrament` (`#include "text/textlayout.h"`).
* Tests: `tests/atrament/<area>/tst_atrament_<area>*.cpp` (QtTest is only the runner; golden
  images go through `QImage` in the tests, never in the framework).
* External dependency: HarfBuzz 14.5.1, vendored in `3rdparty/harfbuzz` (the release's files its
  amalgamation needs, 4.9 MB of sources; `scripts/harfbuzz-vendor.sh <tag>` updates it) and
  compiled as one static library with hidden symbols — the same sources on every platform, never
  the system's (the same shaping and outlines everywhere, nothing to find or download; Qt's own copy
  never meets it). CMake option `BRULION_ATRAMENT` (default: with the app — Linux, Windows,
  Android, the browser).

## Testing

* `TestPlatform` / `TestWindow`: a window without a screen — size, scale, a virtual clock, input
  simulated in process (`simulateClick`, `simulateKeys(u"ctrl-a backspace")`, `simulateText`,
  `simulateComposition`), a frame rendered by `raster` to an RGBA image.
* Touch on the virtual clock: `simulateTouchDown` / `Move` / `Up` / `Cancel` (events stamped with
  the clock's time; a pen with `PointerKind::Pen`), `simulateTouchDrag(from, to, duration, steps)`,
  `simulateTap`, and `advance(ms)`, which moves the clock in frames of 16 ms and draws what each
  asks for — a fling's frames too (`tests/atrament/ui/tst_atrament_touch.cpp`; the Qt host's
  `QTouchEvent`s in `tests/atrament/qt/tst_atrament_qt_touch.cpp`).
* Golden images compared with a tolerance of 2 per channel (fused multiply-add differs between
  processors), regenerated with `BRULION_ATRAMENT_UPDATE_GOLDENS=1`.
* Layout tests: the flex algorithm against hand-computed CSS results; text tests: Polish
  diacritics, combining marks, emoji, wrapping at a width, carets and hit tests round-tripping.
* The GPU renderer is compared with the CPU renderer on every QRhi backend that can be made
  headless (`tests/atrament/qt`: Vulkan from the loader under any platform plugin, OpenGL with a
  display; skipped with neither), and `AtramentItem` is drawn inside Qt Quick through a
  `QQuickRenderControl` on that QRhi.
* Accessibility: `window.setAccessibilityEnabled(true)`, then `accessibilityTree().dump()` (one
  node per line: `button "Zapisz" [focusable]`), `performAccessibilityAction`, and the events a
  frame gave in `TestWindow::takeAccessibilityEvents()`; the Qt bridge through `QAccessible` itself
  (`tests/atrament/qt`, an offscreen window, `QAccessible::installUpdateHandler` for the events).

## Roadmap

Each milestone ends with something running and measured; the gates decide whether to go on.

| Milestone | Content | Gate |
|---|---|---|
| **M0 Foundation** | base, scene, CPU raster, text (fonts, shaping, glyph raster, atlas, paragraph layout, hit tests), flex layout, ui (App, entities, window, div/text/canvas, events, hover, click, scroll, focus), test platform, Qt Quick host, gallery | The gallery runs in Brulion's Qt; every area has tests; a frame of the gallery in < 4 ms of CPU raster at 1280×800 (-O2). |
| **M1 Editing** | text input element with selection, clipboard, undo hooks, the `InputHandler` through Qt (desktop + Android IME), virtualized lists (uniform and measured), images, keymaps, accessibility bridge | Polish text typed with Gboard (composition, autocorrect, gesture typing) and with a Linux IME into an Atrament field, TalkBack reads it. First check (2026-11-26, on the Android 16 emulator with Gboard — the phone was not connected): typing, gesture typing, a suggestion replacing the word (`replaceText`), Backspace, Enter in the multiline field, autocapitalisation, a long press selecting a word with Android's handles and Cut / Copy / Select all, the clipboard both ways (a Polish word cut and pasted from Gboard's clipboard), the caret kept above the keyboard, TalkBack's tree (tabs, buttons, a toggle, both fields with label and placeholder) and its focus following the field; three bugs found and fixed on the way (below). Gboard committed every letter there (no composition, so no autocorrect on space) — Qt Quick's own text fields alike: composition and Polish autocorrect still need the phone. Since then: scrolling with a finger and the fling ("Touch" above; tested on the test platform and through Qt's touch events), the tabs' selected state for TalkBack (a checked tab on Android), the focused row of a `uniformList` kept while scrolled away. Left, on the phone: composition and Polish autocorrect with Gboard, the fling's feel against native lists, TalkBack's speech. |
| **M2 First Brulion view** | the journal / page block editor in Atrament inside the QML shell, behind a setting (design: "The block editor in Atrament" below); the QRhi GPU renderer (in place: every primitive kind, within 2 levels of the CPU renderer) | Memory per block < 2 KB (today 100–200 KB), scrolling a 1 000-block page at 120 fps on the desktop, the editor's existing behaviour tests pass against it. Measured 2026-11-27 (`tests/bench/bench_atrament_page`, docs/PERFORMANCE.md "Atrament's block editor against M2's gate"): the view keeps nothing per block shown (the model's rendered-text caches 5.2 kB), a scroll step of a 1 000-block page 1.2 ms of scene and 2.8 ms with the CPU renderer at 1280 × 800 (median; p95 3.5 ms), a keystroke 1.3 ms. Left: the behaviour tests' counterparts (step 5). |
| **M3 Views** | calendar, tasks, search, settings, Code mode (editor, terminal), the chat; the shell. First (2026-11-26, behind a setting): Code mode's terminal drawn by Atrament — `TerminalScreen` paints the grid, cursor, selection and matches from the shared glyph atlas while `TerminalView` keeps its Qt input (docs/CODE-TERMINAL.md "Drawn by Atrament"): a view's drawing can move before its input | No QML in the default journey. |
| **M4 Own platforms** | Wayland (xdg-shell, layer-shell, text-input-v3, fractional scale), X11, Win32, Android (GameActivity + our own `InputConnection` in Kotlin), web (canvas + WebGPU / WebGL2, hidden textarea for the IME); the WebGPU renderer (Dawn) | The app runs with no QtGui/QtQuick loaded on Linux and Android. |
| **M5 Qt Quick out** | QtGui, Qt Quick, QML, Controls, Svg, Pdf leave the build (PDF through PDFium/poppler into a picture) | APK < 20 MB, start-up memory < 60 MB, first browser visit < 6 MB. |

The targets in M5 are estimates of what the change can reach, not measurements.

## The block editor in Atrament (M2)

The design of M2: the page and journal block editor drawn by Atrament, inside today's QML shell,
behind a setting, replacing `BlockEditor.qml` one kind of row at a time. This section is the plan
its code follows (and changes first); built so far: `list()` (below), step 1 "Reading" and step 2
"Editing text rows" (their entries under "Steps").

### What stays

Everything below the view is already C++ and stays as it is — the editor's behaviour lives there,
and its tests (`tests/app/editor/tst_editor_*.cpp` over `FakeEditorHost`) keep testing it whatever
draws the page:

* **The model.** `BlockDocument` (src/core/markdown/blockdocument.h): the blocks of a page in
  order, every structural edit (`split`, `mergeWithPrevious`, `indent`, `moveUp`, `toggleTask`…)
  an `EditResult` of `BlockChange`s with the row and caret to focus; undo by `Delta`s.
* **The controller.** `PageController` (src/app/editor/pagecontroller*.cpp): loading and saving
  through `EditorHost`, the undo stack, the edited row and caret, folding, zoom, block selection,
  find, properties, Vim. The Atrament view calls its C++ methods directly (not the QVariant API QML
  uses) and follows `BlockListModel`'s row signals, as `PageRows` does.
* **Rendered text without rich text.** `InlineRenderer::renderBlockRuns` gives a block's display
  text as a `RenderedText` (src/core/markdown/renderedtext.h): plain text, flattened style runs
  (bold, italic, code, colours, a pill or box background, a role: link, chip, tag…), inline
  objects (an icon, a rule) and the display ↔ source map — the format the Z10 prototypes
  (`RenderedTextItem`, `PageItem`, `JournalDayItem`) already draw, and exactly what Atrament's text
  needs: `QString` is UTF-16 (D3), so the text passes as a `std::u16string_view` over the
  `QString`'s data, and each run becomes a `TextSpan`. Links stay `href` schemes (`page:`, `tag:`,
  `block:`, `task:toggle`, `date:…`) handled by the code that handles them today.
* **Rows and heights.** `PageRows`, `RowHeightTree`, `ScrollAnchor` (pure QtCore): which rows are
  shown, their summed heights, the scroll position as a row and an offset.
* Suggestions (`SuggestionModel`'s logic), input rules, `blocktext.h` (token edits, slash
  commands), spell checking, the Vim session — all logic the view calls.

### What is new

In **Atrament** (`src/atrament/ui`, Qt-free, generic — nothing Brulion-specific):

* **`list()`, a measured list**: rows of different heights, virtualized like `uniformList`. The
  list asks a `RowExtents` interface (row count, a row's height or its estimate, the row at an
  offset, the offset of a row) and tells it the heights it measured; it scrolls by an anchor (a
  row and an offset into it), so rows measured above the view never move what is on screen. The
  editor's `RowExtents` is a thin adapter over `PageRows` / `RowHeightTree` on the app side (D2:
  the list itself knows no Qt). Touch scrolling, the fling and the focused row kept in the frame
  come from M1 as they are for `uniformList`. **Built** (`src/atrament/ui/measuredlist.h`,
  `tests/atrament/ui/tst_atrament_list.cpp`): `list(id, extents, render)`; `RowHeights`, the
  ready `RowExtents` (a Fenwick tree in 1/64 px, like `RowHeightTree`); a row measured as 0 is
  hidden (a fold) and never shown, a row the list lays out is at least 1 px; a `ListHandle`
  (`trackScroll`) shares the anchor: `scrollToRow(row, Start | Center | End | Nearest)`,
  `rowsInserted` / `rowsRemoved` in front of the anchor (the top is sticky); a user's scroll
  re-anchors on the first measured row in view (rows above measured later move nothing on
  screen); a list scrolled to its end stays there (the rows filling the view from the bottom
  measured first); a new width makes every height an estimate (`markAllEstimated`).
* **Inline boxes in text**: a `TextSpan` with a background shape (a pill, a box, rounded, padded)
  drawn under its glyphs across line breaks, and an inline object of a given size (an icon, a
  checkbox) the line layout reserves room for — what `RenderedBackground` and `RenderedObject`
  ask for. **Built** ("Text" above; `tests/atrament/text/tst_atrament_text_boxes`,
  `tests/atrament/ui/tst_atrament_inline`, the gallery's typography page): `TextBackground {
  color, radius, pill, paddingX, inset, extent, group }` is the type of `TextStyle::background`
  (the field that held a flat colour, so spans and `TextPaintStyle`s written before keep
  compiling; a plain `TextBackground{c}` is now a square box around the glyphs, CSS's look, where
  it was the line's height); `TextLayout::glyphRects(range)`, `runs()`, `inlineBoxes()`,
  `nextCaretStop` / `previousCaretStop`; `InlineBox` on a `FontRun` (part of the layout cache's
  key); `InlineObject { InlineObjectSize { width, ascent, descent, em }, painter }` on a
  `TextSpan`, `Text::inlineObject(range, size, painter)`, the painter told an
  `InlineObjectPaint { bounds, baseline, range, color, fontSize }`; `Window::paintTextBackgrounds`
  / `paintTextGlyphs`. Decisions: shapes reserve no room (the QML editor's rows keep their
  heights), each line's piece is a whole shape, the extent comes from the runs' own fonts, shapes
  go under the glyphs and under a selection; an object is a span, so a block's objects come with
  its styles from one function and the view building the text needs no second list (and the
  styled `TextInput` can take them the same way), and its painter is a `std::function` (spans may
  be kept across frames); objects do not overlap (the first given wins) and a span's end inside
  one does not cut it. In the app, `renderedSpans()` gives Pill and Box runs the QML editor's
  shapes — pills, and boxes of `PagePalette::boxRadius` (Theme.radiusXs, 4 px), around the glyphs,
  no padding or inset (RichTextPills' geometry), `tintElement` as the group, the run's CSS colour
  or the palette's (`chipBackground`, `tagBackground`, `codeBackground`, `highlightBackground`) —
  and a page's app icon (`RenderedObject::Icon`) an inline object of `kPageIconSize` (the room of
  the three no-break spaces it covers, its middle where the QML editor centres its icon).
  Atrament has no icon renderer yet: a placeholder — a rounded square outlined in the link's
  colour — is drawn there until one comes (`BIcon`'s icons as paths or atlas sprites).
* **Hit regions in text**: the range of a span reported under the pointer (a link's `href`, a
  chip), its hover look, and a link node per span in the accessibility tree (`Role::Link`). **Built**:
  `Text::link(range, onClick)` (`src/atrament/ui/text.h`, `tests/atrament/ui/tst_atrament_links`)
  — a hitbox per line of the range, the pointing hand, an underline while hovered (the range's
  pieces get paint styles of their own), the click (or a tap) runs `onClick` and stops there,
  a link node named by the range's text that a screen reader activates.
* **A styled `TextInput`**: the input's text with spans from a callback (the edited block's
  Markdown highlighted: markers dimmed, bold bold, code in its face), so editing keeps the look of
  `InlineEditor.qml`'s `MarkdownHighlighter`. **Built**: `TextInput::setStyler(f)` — `f(text)`
  gives the spans, asked once per text (a revision counter), applied over the inherited style as a
  `Text`'s spans are; `setColors(caret, selection)`, `setPlaceholder`, `clearUndo()` (an owner
  with an undo stack of its own), `layout()`, and the event `SelectionChanged` next to `Changed`.
* **Keys before the field**: `div().captureKeyDown(f)` — a key down on its way to the focused
  element, in the capture phase (GPUI's `capture_key_down`): an editor around a text field sees
  Enter, Backspace, Tab, ↑/↓ first and stops the ones it takes. **Built** (`ui/div.h`).
* **A text's layout after the frame**: `Text::trackLayout(handle)` — a `TextLayoutHandle` the
  element fills in prepaint with its laid-out paragraph and bounds, so code outside the frame
  resolves a click to an offset (`offsetAt`) or finds the x of a caret on a line. **Built**
  (`ui/text.h`).

In the **app** (`src/app/editor/atrament/`, may use QtCore; only in builds with Atrament):

* **`PageView`**, an Atrament entity over a `PageController`: renders the page as a `list()` of
  rows; a row is a `div()` with its guides, bullet or number, fold arrow, task box, tag tint and
  selection band drawn as quads and paths (no element survives the frame, no object per block),
  and its text — a `Text` from the block's `RenderedText`, or, for the edited row, the styled
  `TextInput` over the block's source.
* **The highlighter as a pure function**: `MarkdownHighlighter` is a `QSyntaxHighlighter` over a
  `QTextDocument`; its rules move into a function from a block's source to style ranges, used by
  both the QML highlighter and the Atrament input (one set of rules, tested once).
* **Keymaps**: the editor's keys (Enter splits, Backspace at the start merges, Tab / Shift+Tab
  indent, Alt+↑/↓ move, Ctrl+Enter toggles the task…), each one `PageController` call; the edited
  row's caret and the row to focus come from the `EditResult`. **Built** as a capture listener on
  the edited row (`PageView::editorKey`), not as keymap actions: Atrament's key bindings run after
  the focused element's own key listeners, and the field takes Enter, Backspace, Delete, ↑/↓
  itself — the editor's meaning depends on the caret (Backspace merges only at the start, ↑ leaves
  the block only from its first line), which a binding cannot see. Rebindable keys come with the
  app's keymap settings.
* **Layout cache**: a block's laid-out text is kept by (uid, revision, width) while its row is
  shown and one frame after (Atrament's generational caches); a row off screen costs its
  `BlockEntry` and its height in the tree — the < 2 KB per block of the gate.

### Hosting and the setting

`AtramentView { view: "page" }` takes the place of the `ListView` inside `BlockEditor.qml` when
the device setting **Settings → Editor → "Draw the editor with Atrament"**
(`AppSettings.editorAtrament`, off by default, shown only in builds with Atrament) is on — the
same pattern as `terminalAtrament` (docs/CODE-TERMINAL.md "Drawn by Atrament"). The QML around
it stays: the toolbar, the find bar, the suggestion popup (placed from the input's
`boundsForRange`), menus, dialogs, source mode. `BlockEditor.qml` hands the view its
`PageController`; the view hands back what QML needs (the edited row, a link clicked, a context
menu asked for at a point).

### Rows Atrament does not draw yet

Until every kind of block is Atrament's, a row it cannot draw is a **foreign row**: the list
reserves the height the QML delegate reports and `AtramentItem` positions that delegate (today's
`BlockDelegate`, made by `BlockParts`) over the row after each frame's layout, clipped to the
view — what `PageItem` does with its "rich" rows. Foreign rows scroll with the list (their
positions change in the same frame), take their own input, and leave the accessibility tree to
Qt Quick's own nodes. Each step below turns kinds of foreign rows into Atrament rows.

### Steps

Each step ships behind the setting and is measured (memory per block, frame time while
scrolling, `tst_atrament_editor`) before the next:

1. **Reading.** Text rows whose `RenderedText` is `drawable` (paragraphs, list items, headings,
   quotes, callouts, tasks shown) drawn by Atrament; everything else foreign. Links, chips and
   task boxes clickable; touch scrolling and fling; the accessibility tree of the rows.
   **Built** (2026-11-26): `src/app/editor/atrament/` (library `brulion_editor_atrament`, only
   where Atrament is): `PageView` over `BlockListModel` (`PageExtents`: the list's `RowExtents` over
   `PageRows`, folds hidden; the model's inserts, removals and moves told to the `ListHandle`), text,
   heading and quote rows with drawable text — bullets, numbers, the task box (a checkbox for screen
   readers), done tasks struck through, a document's and an outline's indents (`PageItem::textX`'s
   geometry) —, `renderedSpans()` (a `RenderedText`'s runs as `TextSpan`s: bold, italic, strike,
   code, links, chips, tags, highlights; `cssColor()` reads the renderer's colours). Hosted by
   `AtramentView { view: "page" }` (qml/editor/EditorAtramentPage.qml) instead of the
   `ListView`'s rows when Settings → Editor → "Draw the editor with Atrament"
   (`AppSettings.editorAtrament`, shown where `DeviceInfo.atrament`) is on; tested by
   `tests/atrament/editor/tst_atrament_editor`. **Not yet**: foreign rows are placeholders of their
   height (no QML delegate placed over them), the page's title and properties above the blocks are
   not shown. (Callouts and a tap that edits a block came with step 2.) Links, chips and tags open
   (`AtramentView.linkActivated` → `BlockEditor.activateLink`) and a task's box toggles it
   (`taskToggled` → `PageController.toggleTask`), through the hit regions below.
2. **Editing text rows.** The styled input in the edited row: typing, the keymaps, Enter / Backspace
   / Tab through `PageController`, input methods as in M1 (composition is one undo step), the caret
   kept above the keyboard, suggestions, input rules, slash commands.
   **Built** (2026-11-27, `src/app/editor/atrament/pageediting.cpp`): with a `PageController`
   (`PageView::setController`; `AtramentView.pageController`), the controller's editing row —
   when Atrament draws it and it is a paragraph, a list item, a task, a heading or a quote — is a
   `TextInput` over the block's `editText` in the text's place, styled by `setEditStyler`.
   Typing goes to `PageController::setEditText` (one undo stack: the controller's; its input
   rules and normalisation come back through `editTextReset` and are shown); a word an input
   method composes stays the field's until it is committed (the controller never sees half a word:
   a composed word is one step), and every command key commits it first. The keys
   (`PageView::editorKey`, BlockEditor.handleEditorKeyCode's): Enter splits (Shift+Enter, or Enter
   in a quote with its "> ", a line break), Backspace at the start merges with the block above —
   a task loses its marker first, a document's list item leaves the list —, Delete at the end
   joins the next block, Tab / Shift+Tab indent (a document: list items only), Alt+↑/↓ move the
   block, ↑ on the first line and ↓ on the last go to the block above / below with the caret at
   the same x (through that row's drawn text), Ctrl+Enter the next task status, Ctrl+. folds,
   Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z the page's undo (`BlockEditor.undoStep`), Ctrl+C / Ctrl+X the
   controller's exact copy, Ctrl+V its paste (Markdown into blocks, HTML as Markdown, a picture
   as an attachment), Alt+Enter follows the link at the caret, Esc stops editing and selects the
   block (`BlockEditor.stopEditing(true)`: the selection's band is drawn, its keys are the QML
   list's for now). A click or a tap on a drawn row edits it at the character under the pointer —
   the row's text layout (`trackLayout`) gives the display offset, the rendered text's display ↔
   source map the source one, `renderOffset` and `editOffset` the `editText`'s; Shift / Ctrl+click
   and the touch selection mode go to `BlockEditor.blockTappedWith`. An input method's line break
   in a one-line block splits it and a paste with line breaks becomes blocks
   (`PageController::pasteText`), as in InlineEditor. Suggestions: the field's state
   (`AtramentView.editText`, `editCursor`, `editCaretRect`, `editStateChanged`) drives
   `BlockEditor.updateCompletionAt`, the popup is placed at Atrament's caret (`SuggestionPopup.
   caretItem` / `caretBox`), its keys come back while it is open (`suggestionsOpen`,
   `suggestionKeyPressed`) and accepting one commits the composition first
   (`AtramentView.commitComposition`); slash commands follow. An empty paragraph shows "Type / for
   commands". Also built with it: callouts drawn and edited by Atrament (BlockPartCallout's tinted
   frame and bar; Enter a line break with its "> "), the outline's fold arrows (under the mouse,
   while folded, always on touch; a click folds), the bullet's click zooming into its block, a
   right-click opening the block menu, a finger held on a block's text its touch selection mode
   (`BlockEditor.longPressText`, through Atrament's new long press), Shift+↑/↓ past a block's
   text selecting blocks
   (`BlockEditor.extendBlocksFrom`), Ctrl+A on a block's whole text selecting every block.
   The find bar's matches are tinted in the drawn rows and in the edited one
   (`PageView::setFind`, `AtramentView.findQuery` …) and its current match is selected in the
   edited block (`selectInEdited`); while the bar keeps the keyboard, a block it edits does not
   take it (`AtramentView.editingStarted`: the QML around decides). The edited field is a text
   input named "Block text" for screen readers, as InlineEditor.
   Tested by `tests/atrament/editor/tst_atrament_editing`. **Not yet**: spell marks in the edited
   text, Vim, the touch toolbar's formatting buttons (they act on an InlineEditor), the reminder
   chips while editing.
3. **Selection.** Block selection (Shift+click, Shift+arrows, a mouse's drag across rows), its band,
   copy / cut / paste through `PageController`'s selection; on a phone the long press and the
   handles. **Built so far** (2026-11-27): the band (`selectionEdge`), Shift+click and Ctrl+click
   (`BlockEditor.blockTappedWith`), Shift+↑/↓ out of the edited block, Ctrl+A twice, Esc, a
   mouse's press dragged from one block into another (`PageView::dragOver`: the row under the
   pointer from the list's anchor; its release edits nothing; `dragSelectRequested` →
   `leaveEditingForSelection` + `selectRange`), a finger's long press (the touch selection mode);
   the selection's keys, copy, cut and paste are still the QML list's (it takes the keyboard). Not
   yet: the touch grips, the edge auto-scroll while dragging, a drag across the journal's days.
4. **Structure.** Folding, zoom into a block, drag to move, dates and reminders chips, the journal
   — a stream of days as sections of one list (what `JournalDayItem` draws today).
   **Built so far** (2026-11-27): folding (the arrows, step 2's entry), the bullet's zoom, and the
   journal's days drawn by Atrament as a first stage — each day's editor (`ContentJournalDay`'s
   autoHeight `BlockEditor`) shows its blocks in an `AtramentView` as tall as they are
   (`PageView::contentHeight` / `onContentHeight` after each frame's measuring →
   `AtramentView.contentHeight` → `BlockEditor.atramentContentHeight` → the day's
   `fitEditorHeight()`); the journal's list scrolls (a wheel Atrament does not take goes on to it)
   and keeps the caret in view (`BlockEditor.ensureVisible` through its `scroller`). The stream as
   one Atrament list (no item per day) is the next stage.
5. **Blocks with views.** Code (the input with the code highlighter), tables, math (Brulion Math's
   glyphs through Atrament's text), charts and Mermaid (their core layouts painted as Atrament paths),
   pictures (`img`), queries, transclusions, properties. When nothing is foreign, M2's gate is
   measured; the QML delegates go in M3.

### Tests

* The C++ tests of `PageController` and `BlockListModel` stay as they are.
* `tests/atrament/editor/tst_atrament_editor` (QtTest runner, `TestWindow` + `FakeEditorHost`): a
  page drawn headless, typing, Enter splitting, Backspace merging, Tab, composition, a tap on a
  link, a finger's scroll, golden frames of each row kind.
* The QML scripts (`tst_editor_qml_*`: selftest, taskstatus, datechips, table, selection,
  keyboard_touch…) drive the QML delegates and cannot run against Atrament; each script's checks
  get their counterpart in `tst_atrament_editor` as its step lands — the gate's "the editor's
  existing behaviour tests pass against it" is that list, complete.

### Open questions

* Vim mode's block cursor and visual selection: the input's caret shape and a selection drawn by
  the view, or Vim stays on the QML editor until M3.
* ~~The find highlight~~: spans with a background over `text::findMatches` of the display text
  (and of the edited text), built.
* Spell marks (`renderTraced` anchors) as wavy underlines (the scene has them) mapped through the
  display ↔ source map.

## Risks

* **Input methods on Android** are the hardest single part (composition regions, autocorrect,
  gesture typing, selection handles, the magnifier). Hosted mode keeps Qt's implementation until
  M4; M4 does not ship on Android until Gboard and SwiftKey pass the same checklist.
* **Accessibility** has to work from the first view that ships (M2), not later.
* **WebGPU** is not in every browser (Firefox on Linux and Android only in test builds as of
  2026), hence the WebGL2 renderer of the same scene.
* **Breadth**: 334 QML files are a lot of UI. The hosted mode is what makes the migration
  survivable: every view moved ships on its own.
