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

A div with `overflowXScroll()` / `overflowYScroll()` is a scroll view: the wheel scrolls it (an
inner one at its end lets the wheel go on to the one around it), and it paints a thumb along each
axis whose content does not fit — 6 px thick, 2 px in from the edges, as long as the view's share
of the content (24 px at least, the track at most), placed by the offset. A press of the left
mouse button on a thumb drags it: the content scrolls by as much as the thumb moves along its
track, from where it was, the other axis left alone; the content gets neither the press nor a
click, the release ends it, and the wheel still scrolls meanwhile (`Div::ThumbDrag`,
`tests/atrament/ui/tst_atrament_scrollbars`). A thumb as long as its track cannot be dragged.

### Text

* `FontCollection`: faces registered from bytes (bundled fonts) or files; families by name with
  weight and style; a fallback chain searched by codepoint coverage, so an emoji or a math sign
  finds its face without the caller choosing one.
* `shapeLine(text, runs)`: splits into runs of one face (itemization by coverage), shapes each
  with HarfBuzz, returns glyphs with x positions and the UTF-16 cluster they came from.
* `TextLayout`: a paragraph wrapped at a width — lines of glyph runs, ascent/descent/line height,
  and the editing queries: caret position for an offset, offset for a point, selection rectangles
  for a range, the next/previous grapheme and word boundary.
* **Alignment** (a style's `textRight()` / `textCenter()`; `ParagraphStyle::align` and
  `alignWidth`): the layout places each line in the paragraph's box itself (`TextLine::x`, the box
  `TextLayout::boxWidth()`: `alignWidth`, else the wrap width, else the widest line), and every
  position it gives is the placed one — glyphs, carets, hit tests, ↑/↓, selection and glyph
  rectangles, inline boxes. Whoever draws or asks the layout adds no offset of its own
  (`Window::paintText` has no alignment parameters any more): a right-aligned or centred `Text`'s
  links' hit regions, its inline objects, its `TextLayoutHandle`'s offsets and its accessibility
  bounds are where its glyphs are, and a `TextInput` aligns its text, caret, selection and
  placeholder in its width. A `Text` aligns in its own box — a wrapping one at its wrap width, one
  that does not wrap in the width prepaint gives it (laid out again when that differs); `size()`
  stays the widest line, what the text needs. Decision: the alignment lives in the layout, not in
  the painter — the painter's offset was the bug (tables placed their text instead), and a
  second offset in every consumer would be the next one. Tested by
  `tests/atrament/text/tst_atrament_text_layout` (`aligned`: every query against the left-aligned
  layout moved by its line's x, rewrapping, the cache) and `tests/atrament/ui/tst_atrament_align`
  (the glyphs' ink against a link's node and click, the handle's offsets, an object's box, wrapped
  lines, a field's caret).
* **Line clamp** (`lineClamp(n)` on an element, `TextStyle::maxLines`; `ParagraphStyle::
  maxLines`): at most n lines; when text was left out the last one is shortened by whole graphemes
  until "…" (U+2026 in the font of the text before it, a fallback's, else three full stops) fits
  the wrap width, spaces before it dropped, and ends with it — CSS's `line-clamp` with
  `text-overflow: ellipsis`. The text keeps its offsets: the last line's `next` is the text's end,
  offsets past its end are at its end for carets and hit tests, `TextLayout::truncated()` says it
  happened (a truncated layout is shaped again when rewrapped: it lacks the glyphs it left out). A
  screen reader reads the whole text; a `TextInput` shows all of it. Tested by
  `tst_atrament_text_layout` (`maxLines`) and `tst_atrament_align`.
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
* **Baseline shifts and overlines** (a formula's scripts and radicands): `FontRun::baselineShift`
  raises a run's glyphs above the line's baseline (negative: lowers them) and moves its ascent and
  descent with them — the line grows to hold a raised or lowered run (CSS's `vertical-align` by a
  length), `glyphRects` and decorations follow it, x and carets do not move (`LaidOutRun::
  baselineShift`; part of the layout cache's key, kept by rewrapping). In `ui`, a span's
  `style.baselineShift` (px) and `style.overline` (a line at the font's ascent over its glyphs,
  `TextPaintStyle::overline`). Tested by `tests/atrament/text/tst_atrament_text_shift` and
  `tests/atrament/ui/tst_atrament_vectorpaint`.
* **At most N lines** (`Text::maxLines(n)`, CSS's line-clamp: a query result's four lines): the text
  past the n-th line is left out and that line ends with "…" in the style of its last character,
  cut at the last grapheme from which the ellipsis fits the width (spaces before it dropped). The
  layout the element keeps — carets, hit tests, links, `trackLayout`'s handle — is of the text
  shown; a screen reader still gets the whole text. Tested by `tests/atrament/ui/tst_atrament_inline`.
* **A text field's caret, objects and read-only state** (`TextInput`, `ui/textinput.h`): the caret
  is a bar before the character (the default, blinking), a block over the character after it (as
  wide as that character on its line — its caret stop: an inline object whole —, half an em at a
  line's end; painted over the text in the caret's colour at 45 %) or a 2 px line under it
  (`CaretShape`, `setCaret(shape, blinks)`, `caretBox()`); a block or a line is not drawn while
  something is selected. The styler's spans may carry inline objects as a `Text`'s do (laid out the
  same way, painted after the glyphs): one unit for the caret — ←/→ step over one (the layout's
  caret stops), Backspace / Delete remove it whole, a click never lands inside — and an object
  with `onClick` (any button; `up.button`) or `onLongPress` takes its presses before the field (a
  hitbox over its box, the pointing hand; the caret and the selection stay). `setReadOnly(true)`:
  keys still move the caret and select, nothing but the owner's own calls changes the text (no
  typing, Backspace, Enter, paste, cut, undo, an assistive technology's SetValue), and the field
  gives the window no input handler (below). `restyle()` asks the styler again when what it reads
  besides the text changed (a spell checker's verdicts, the caret).
* Glyphs are rasterized once per (face, glyph, size, subpixel x offset) into an A8 atlas; the
  renderers read the same atlas.
* Caches are generational: what was used in the last frame stays, the rest is dropped at the end
  of the next.

### Vector paths and icons

`scene/vectorpath.h`, `scene/vectoricon.h` (Qt-free): what diagrams, charts and icons are drawn with.

* **`VectorPath`**: moveTo / lineTo / quadTo / cubicTo / close, SVG's elliptical `arcTo` (endpoint
  parameters, radii grown to join the points, zero radii a line), `addRect` with rounded corners,
  `addEllipse`; `flatten(PathTransform, tolerance)` cuts curves into lines (Wang's formula) under a
  uniform scale and a translation into contours that know whether they were closed.
* **Strokes are paths**: `strokeContours(contours, StrokeStyle, PathBuilder&)` makes the outline of
  a line of a width — a rectangle per segment, a join per corner (`LineJoin::Round`: a disc, or a
  wedge where the turn is flatter than the tolerance; `Miter` up to `miterLimit`, else `Bevel`), a
  cap per open end (`Butt`, `Round`, `Square`; a contour of one point is a dot) — as polygons of
  one orientation, so the non-zero fill of the rasterizer (min(|winding|, 1)) is their union; discs
  are polygons of the circle's area. Dashes (`dashes`, `dashOffset`, SVG's rules: an odd list
  repeats, a negative length or a pattern too fine for the line's length is solid) cut the
  flattened line first (`dashContours`). Decision: no stroke primitive — the result is the scene's
  `PathPrimitive`, which the CPU raster fills and the QRhi renderer rasterizes into its path atlas
  as before; one shader less, the same pixels in both.
* **`Window::fillPath(path, transform, colour)`, `strokePath(path, transform, style, colour)`**
  (logical px, the stroke's width and dashes in the path's units scaled by the transform) and
  **`paintIcon(icon, bounds, colour)`**.
* **`VectorIcon::parseSvg(text)`**: a monochrome icon from SVG's subset — `<svg viewBox>`, `<g>`,
  `<path d>` (every command of SVG 1.1, implicit repeats, compact numbers and flags), `<rect rx
  ry>`, `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`; fill, stroke, stroke-width,
  stroke-linecap, stroke-linejoin, stroke-miterlimit inherited (any paint but `none` paints: one
  colour, the caller's); `<defs>`, `<clipPath>`, text, styles skipped; malformed input gives what
  was read before the mistake, oversized input nothing. Every icon of `assets/icons` (Lucide's,
  BIcon's) reads. `outline(side)` — the filled and the stroked outline fitted into a square of
  `side` device px — is kept for the icon's last four sizes; `paintIcon` snaps the square to whole
  device px and copies the outline's points into the frame.
* Tested by `tests/atrament/scene/tst_atrament_vector` (coverage of strokes, caps, joins, holes,
  dashes, path data, the subset, malformed input) and `tests/atrament/ui/tst_atrament_vectorpaint`
  (pixels at scale 1 and 2, masks, an icon's outline reused).

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
a hidden `<textarea>` in the browser. A read-only `TextInput` installs none: no input method
composes into it, the platform's keyboard is not shown for it, and keys reach it as key events with
their text (Qt sends them so when `ImEnabled` is false) — what Vim's command modes need, as Qt
Quick's read-only TextEdit gives the QML editor.

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
  nothing) and the finger scrolls nothing until it lifts (a block's touch selection mode). Inside
  the focused field only a listener whose hitbox lies within the field's bounds — something drawn
  in its text: an inline object's `onLongPress`, a reminder chip's menu — runs instead of the
  platform's selection. The
  listener may keep the finger as a drag (`Window::dragAfterLongPress()`): its moves and its
  release then reach the elements' mouse listeners — still no scroll, and the release clicks
  nothing (the press was cancelled) — a block's selection extended as the finger moves, blocks
  dragged to another place.
* **Drags an element claims.** A finger's or a pen's press on an element that claims its drag
  (`div().claimTouchDrag()`, `Window::claimTouchDrag(hitbox)`: a grip at a selection's end, a
  slider's thumb, a handle) is that element's from the press on: no wait, no slop, no container
  scrolls, no long press — the press, every move and the release reach the mouse listeners at
  once, as a mouse's do, also inside a list that scrolls the same way (the topmost claim under the
  finger; a cancelled touch ends it with a cancelled release, over nothing). Over a hosted Qt
  Quick item the item takes such a press from the child (below).
* **Edge scrolling** (`ui/edgescroll.h`): a drag held near a scrolling view's top or bottom edge
  (a selection dragged past what is shown, a row being moved) scrolls the view by itself —
  `edgeScrollSpeed(y, top, bottom)`, px per 16 ms frame, the QML editor's curve: a zone of a fifth
  of the view (16–56 px), from 2 px at its inner side to at most 40 — and `EdgeScroller` steps it on
  the app's timers (the virtual clock in tests) while the pointer stays in the zone; the view's
  owner moves its content and follows what came under the pointer.
* **Through Qt** (`qt/qthost.h`) the item takes `QTouchEvent`s itself (`setAcceptTouchEvents`):
  the first finger down is the pointer until it lifts, with the events' times; `TouchCancel`, or
  the grab taken away (a Drawer's edge, a Flickable around: `touchUngrabEvent`, and
  `mouseUngrabEvent` while a press is still down — Qt Quick ungrabs after every release too), ends
  the press as cancelled. Not the mouse Qt Quick makes of a touch: that one says nothing when the
  touch is cancelled or stolen (a press stuck down, a scroll that never ends), brings hover events
  where the finger was and double clicks of its own; and the touch events have the second finger
  for a pinch later. A pen comes as the mouse Qt makes of it, of kind Pen, and scrolls as a finger.
* **Over hosted Qt Quick items** (qthost.h "Hosted children": a QML delegate over a row Atrament
  does not draw) the item filters its children's touch events, as a Flickable does its delegates':
  the window's gesture follows a finger pressed on a child without giving its elements the press
  (`MouseDownEvent::scrollOnly`: no press, no tap, no long press — only the slop, the container,
  the scroll), the child keeps the press; once the drag becomes a scroll the item takes the touch
  (the child's grab is cancelled, its handlers that watched passively too — a TapHandler would
  otherwise fire its long press mid-scroll), and the content follows the finger and flings. A
  child that keeps its grab (`keepTouchGrab` / `keepMouseGrab`: a slider, an inner Flickable
  dragging its own way) keeps the drag; a drag nothing scrolls stays the child's; a tap is the
  child's; a finger on a flinging list stops it and drags on, a press the child never gets. A
  press on an element of the window's that claims its drag (a selection's grip over a hosted row)
  is the item's at once: the child never has it. A press that no child takes reaches the item as
  any other. The wheel a child leaves reaches the item by Qt Quick's own delivery.
* Left for later: two fingers (pinch, two-finger scroll), an overscroll effect, smooth
  programmatic scrolling.

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
| `uniformList`, `list` | a **list** with its row count; each shown row a **list item** with its index, described by the row's root element (its click, `selected(…)`, its text as the name); a row whose root is `ariaHidden()` is none (a hosted Qt Quick item over it has its own nodes); `list()`'s header and footer are no rows |
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

**Tables**: a `Role::Table` node's `Row` children are its rows (other children — a filter's
fields — are none), their `Cell` and `ColumnHeader` children its cells, in order; the Qt bridge
gives a table Qt's `QAccessibleTableInterface` (rows and columns counted, the cell at a row and a
column, a column's description from its header, the selected cells, rows and columns; selecting
through it is the view's own keys') and its cells `QAccessibleTableCellInterface` (their row and
column, an extent of one, the column's header cell, selected, the table). Tested by
`tests/atrament/qt/tst_atrament_qt_accessibility` (`tableInterfaces`).

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
window lists it; `Accessible.name` set in QML names it. Its hosted children (Qt Quick items over the
frame, qthost.h) follow the tree's nodes among its children — Qt Quick's own interfaces, whose
parent is the item — and are found first under a point. The tree is turned on when Qt says an assistive technology is active, or when one first
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
| **M2 First Brulion view** | the journal / page block editor in Atrament inside the QML shell, behind a setting (design: "The block editor in Atrament" below); the QRhi GPU renderer (in place: every primitive kind, within 2 levels of the CPU renderer) | Memory per block < 2 KB (today 100–200 KB), scrolling a 1 000-block page at 120 fps on the desktop, the editor's existing behaviour tests pass against it. Measured 2026-11-27 (`tests/bench/bench_atrament_page`, docs/PERFORMANCE.md "Atrament's block editor against M2's gate"): the view keeps nothing per block shown (the model's rendered-text caches 5.2 kB), a scroll step of a 1 000-block page 1.2 ms of scene and 2.8 ms with the CPU renderer at 1280 × 800 (median; p95 3.5 ms), a keystroke 1.3 ms. After step 5 (every kind of block Atrament's), a 1 000-row page of every kind has no foreign row and scrolls at 1.9 ms a step with the CPU renderer (median; p95 4.9 ms, the machine under load). The behaviour tests' counterparts: `tst_editor_qml_atrament` (130 checks a run, desktop and phone, "Tests" below) and the C++ suites of each step; left: the journal stream, the tables' and the media's **Not yet** items. |
| **M3 Views** | calendar, tasks, search, settings, Code mode (editor, terminal), the chat; the shell (design and order: "The views in Atrament (M3)" below). First (2026-11-26, behind a setting): Code mode's terminal drawn by Atrament — `TerminalScreen` paints the grid, cursor, selection and matches from the shared glyph atlas while `TerminalView` keeps its Qt input (docs/CODE-TERMINAL.md "Drawn by Atrament"): a view's drawing can move before its input | No QML in the default journey. |
| **M4 Own platforms** | Wayland (xdg-shell, layer-shell, text-input-v3, fractional scale), X11, Win32, Android (GameActivity + our own `InputConnection` in Kotlin), web (canvas + WebGPU / WebGL2, hidden textarea for the IME); the WebGPU renderer (Dawn) | The app runs with no QtGui/QtQuick loaded on Linux and Android. |
| **M5 Qt Quick out** | QtGui, Qt Quick, QML, Controls, Svg, Pdf leave the build (PDF through PDFium/poppler into a picture) | APK < 20 MB, start-up memory < 60 MB, first browser visit < 6 MB. |

The targets in M5 are estimates of what the change can reach, not measurements.

## The block editor in Atrament (M2)

The design of M2: the page and journal block editor drawn by Atrament, inside today's QML shell,
behind a setting, replacing `BlockEditor.qml` one kind of row at a time. This section is the plan
its code follows (and changes first); built so far (2026-11-27): `list()` (below) and every step —
reading, editing, selection, structure (the journal stream as one list), and every kind of block
but audio, video, Obsidian bases and database views, which stay hosted QML delegates (each step's
entry under "Steps", with what is **not yet** done).

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
  `scrollToRowPart(row, part, position, above, below)` — a part of a row (a block inside a journal day's
  section) given by a resolver once the row is laid out in that frame, with air around it —,
  `rowsInserted` / `rowsRemoved` in front of the anchor (the top is sticky), `scrolledSinceFrame()`
  (the scroll its next frame takes), `anchorOnPart(row, part, into)` (the view kept on a part of a
  row for one frame: a long journal day's block, "The journal stream as one list"); a user's scroll
  re-anchors on the first measured row in view (rows above measured later move nothing on
  screen); a list scrolled to its end stays there (the rows filling the view from the bottom
  measured first); a new width makes every height an estimate (`markAllEstimated`). A header and a
  footer (`list(…).header(render).footer(render)`: a page's title above its blocks, the room under
  them) scroll with the rows, laid out at the list's width in every frame — no rows of the extents,
  no list items; a list at its very top stays there while its header grows.
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
  The icon is BIcon's SVG drawn as vector paths ("Vector paths and icons";
  `src/app/editor/atrament/pageicons.h`: read once per name from `:/icons`, never outside it) in
  the link's colour; a rounded square outlined in that colour stands for a name with no icon.
* **Hit regions in text**: the range of a span reported under the pointer (a link's `href`, a
  chip), its hover look, and a link node per span in the accessibility tree (`Role::Link`). **Built**:
  `Text::link(range, onClick)` (`src/atrament/ui/text.h`, `tests/atrament/ui/tst_atrament_links`)
  — a hitbox per line of the range, the pointing hand, an underline while hovered (the range's
  pieces get paint styles of their own), the click (or a tap) runs `onClick` and stops there,
  a link node named by the range's text that a screen reader activates. A link may have a menu
  of its own (`link(range, onClick, onMenu)`: a chip's, a tag's): a right-click on it or a finger
  held on it runs `onMenu` at that point instead of the row's right-click or long press around it
  (the page's chips open DateChipMenu, its tags and links the link menu).
* **A styled `TextInput`**: the input's text with spans from a callback (the edited block's
  Markdown highlighted: markers dimmed, bold bold, code in its face), so editing keeps the look of
  `InlineEditor.qml`'s `MarkdownHighlighter`. **Built** (the rules: `src/app/editor/atrament/editstyles.h`,
  `EditStyler` over markdownstyles.h's `styleMarkdownLine` / `flattenStyles` with BlockEditor's
  `highlighterPalette` — the same colours, weights, faces and boxes InlineEditor shows):
  `TextInput::setStyler(f)` — `f(text)`
  gives the spans, asked once per text (a revision counter; `restyle()` asks again), applied over
  the inherited style as a `Text`'s spans are, inline objects included ("Text" above: one unit for
  the caret, their own clicks and long presses); `setColors(caret, selection)`, `setPlaceholder`,
  `clearUndo()` (an owner with an undo stack of its own), `layout()`, the event `SelectionChanged`
  next to `Changed`, the caret's shape (`setCaret`) and `setReadOnly` (Vim's command modes).
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
  both the QML highlighter and the Atrament input (one set of rules, tested once). **Built**
  (`src/app/editor/markdownstyles.h`, QtCore only; `tests/app/editor/tst_editor_markdownstyles.cpp`):
  `styleMarkdownLine(line, previousState, StyleOptions, LineStyles *)` — the mode (inline,
  source, code; Luau) — gives a line's `StyleOp`s (`{from, length, style, merge, modifier, tint}`:
  a `MarkdownStyle` — "heading", "marker", … "luau.comment" — set or merged in the highlighter's
  order, a DemiBold or on-the-heading variant, a coloured `<mark>`'s tint), the state its next line
  starts in (source mode's fences — a Luau fence's lexer state inside —, the frontmatter, `%%`
  comments, `$$` formulas), its `SpellRange` and its reminder `LineChip`s; `flattenStyles(ops,
  length)` turns them into non-overlapping `StyleRun`s of `StyleAttributes` — a `ColorRole` (the
  palette key with its fallbacks: `colorRolePalette()`), a `BackgroundRole` or tint, the weight,
  italic, strike, underline, monospace — for the Atrament input. `MarkdownHighlighter` applies the
  operations with `setFormat` and builds its formats from the same table (`styleAttributes()`); the
  test compares, character by character over a corpus in every mode, the flattened runs with its
  `QTextLayout` formats.
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

**Built** (2026-11-27; `src/app/editor/atrament/foreignrows.h`, `foreignhost.h`, `qt/qthost.h`
"Hosted children"):

* **PageView** paints a foreign row as an empty box of the row's height (the delegate's once
  measured, the estimate before), aria-hidden (no list item: the delegate's nodes are Qt Quick's),
  with a `canvas` that records where the frame put it — row, block uid, bounds in window px, the
  list's content mask: `PageView::foreignFrames()`. The page's **header** (the title, the first
  property's draft, the zoom's breadcrumb: BlockEditor's `header`) and **footer** (the room under the
  last block; a tap there appends an empty block and edits it) are the list's header and footer,
  boxes of their QML items' heights recorded the same way. `setForeignHeight(row, px)` makes the
  delegate's height the row's measured one — the list's own layout of a foreign row never counts
  as a measurement (`PageExtents::foreign`) —, `setHeaderHeight` / `setFooterHeight` the edges';
  `foreignRowsNear(above, below)` and `rowBounds(row)` place rows not in the frame.
* **ForeignRowHost** (owned by `AtramentView` for "page"; `delegate`, `header`, `footer` properties,
  given by `EditorAtramentPage.qml`) runs after every frame the item builds
  (`AtramentItem::placeHostedChildren`, in the same polish, before Qt Quick syncs): each painted
  foreign row gets its delegate — made as `PageItem` makes its hosted rows (`hostedrow.h`, shared
  with it: the row's roles as required properties, `index`, a model object), kept by the block's
  uid — over its box; rows half a screen above and below the view get theirs too, measured before
  they scroll in; delegates more than one and a half screens away go unless edited or holding the
  keyboard. The heights go back to the page, and when one changed the item builds the next frame
  at once, in the same polish (`AtramentItem` builds up to four in a row): nothing lags behind,
  and rows above the anchor measured late move nothing on screen. A height that changes later (a
  picture loaded) is told when it changes. Delegates follow the model's moves and roles.
* **Input**: the delegates take their own taps, clicks and keyboard focus; a finger's drag that
  starts on one scrolls the page ("Touch" above: the hosted children's drags), the wheel over one
  scrolls it; a press on the page's own rows takes the keyboard back from a delegate (while one has
  it the Atrament window is inactive: no caret).
* Tested: `tst_atrament_editor` (the frames as the list scrolls, heights fed back, a height above
  the anchor moving nothing, the header and footer, rows near the view, no list items) and
  `tst_atrament_editor_hosting` (a real offscreen Qt Quick window: a code block and a table hosted
  at their rows, following a scroll and released far away, a finger's drag on a MouseArea's and on
  a TapHandler's delegate scrolling, taps staying theirs, the wheel, the accessible children).
* A hosted editor's caret (a code block's InlineEditor) stays in view: `BlockEditor.ensureVisible`
  asks the page drawn by Atrament (`AtramentView.revealArea` → `PageView::reveal` →
  `ListHandle::scrollBy`, taken by the list in its next frame as the wheel's offset is).
* A selected hosted row is on the page's band like the rows Atrament draws: the band (and the
  touch selection mode's grips) is painted in the row's box, under its delegate, which draws none of
  its own here (BlockDelegate's band and grips are off `usesAtrament`); a grip there takes the
  finger from the delegate (`claimTouchDrag`, "Touch" above). The find bar's matches in a hosted
  row are its delegate's own, as in the QML editor (a code block or a table tints none there
  either; a block the find bar edits shows its match selected in its InlineEditor).

### The journal stream as one list

M2 step 4's second stage: the journal stream (JournalsView's lazy `ListView` of `ContentJournalDay`
items, each a FocusScope with its own `PageController` and autoHeight `BlockEditor` whose rows an
`AtramentView` drew) becomes **one Atrament list with no QML item per day**, behind the same
setting; with it off the QML stream stays as it is.

**Decision: a row per day, each day's page a nested view.** The stream's `list()` has one row per
day of the days' model — the day's *section*: the line between days, the day's heading (its date,
"Today" / "Yesterday", "Open as page"), then the prompt of a day without a note or the day's
page. A day's page is a `PageView` entity in **embedded mode**, drawn into the section as a nested
view (`ViewElement`: an entity converts to an element): a column of its header, every block it
shows and its footer — no list and no scrolling of its own. Considered and left: the days' blocks
flattened into the stream's rows (a row per block, the days' `PageRows` behind one composite
`RowExtents`). It would virtualize inside a long day, but every part of PageView that scrolls,
reveals, drags, selects or hosts foreign rows works on one page's rows and its own list; a nested
page keeps all of it, the stream's anchor — a day and an offset into it — is what
`viewState()` already saves. A long day is laid out only where it shows (built: "A long day laid out
where it shows" below), unlike the QML stream (ContentJournalDay builds all of a day's blocks:
BlockEditor.autoHeight).

**Embedded mode** (`PageView::setEmbedding(PageEmbedding *)`):
* `render()` returns the page's column — the header, each shown row, the footer, the layers — at
  the width the section gives it. Each row is wrapped in a box that remembers its layout node and
  tells the page's `PageRows` its height in prepaint: after every frame the rows' heights are exact
  and `rowBounds`, `rowAtY`, the drags' rows and the foreign rows' places work as in the page's own
  list (they go from the header's frame and the rows' offsets).
* What scrolls the page's list scrolls the embedding instead (`PageEmbedding`, the stream): a row
  revealed — the edited one after Enter, ↑/↓ or a zoom, `revealRow`, a table's cell, Vim's zt / zz /
  zb —, `reveal()` of a hosted caret, the drag's edge scrolling, the scroll since the last frame a
  drag follows. A reveal is a part of the day's row: `ListHandle::scrollToRowPart(row, part,
  position, margin)` (generic, `ui/measuredlist.h`), the part — a top and a height inside the row —
  given by the page after the list laid the row out in that frame (`PageView::embeddedPart(row)`,
  from the rows' layout nodes), so a block Enter just made is shown by its real height at once.
* The block selection's key layer (keys no focused element took) is the active day's only
  (`setKeysActive`); ↑ on the first line of a day's first block and ↓ on the last line of its last
  block are the embedding's (`onPastEdge`): the stream edits the day above / below that has blocks,
  the caret at the same x (the QML stream stops there). Enter and Backspace at a day's edges stay
  inside the day (a new block at its end; Backspace at its first block's start merges nothing).

**`JournalView`** (`src/app/editor/atrament/journalview.{h,cpp}`), an entity over the days' model
(any `QAbstractItemModel` with JournalListModel's roles `date`, `path`, `exists`, `isToday`, found
by name):
* **Days near the view** — the rows the last frame showed and a screen above and below them — are
  made in `render()`, before the list lays them out: the day's `PageController` from the host's
  factory (`makeController`: its host, path, read-only state…), a PageView in embedded mode set up by
  the host (`setUpPage`: looks, callbacks; the same as a page's). Days farther away are released
  after the frame (their controllers save on destruction), unless being edited, zoomed into, or —
  without a selection over several days, whose parts the stream selection keeps by path and row —
  with blocks selected or the active day: one PageView and controller per day near the view. A
  day released keeps its measured height; heights are kept by date across the model's resets and
  moves (a new day at midnight is inserted above: the anchor follows its day).
* **The active day** (the one written in: the side panel's, the find bar's, Back's zoom, the
  commands'): the day that started editing, selected blocks or zoomed (its controller's signals), or
  that the host made active (`activateDay`); the day active before stops editing, unless a selection
  over several days spans them (`spanActive`), and loses its selection once the new day selects or
  edits on its own (a Shift+click there extends it first). `onActiveDayChanged` (−1 when the day
  written in went far away with only a part of a span).
* **The heading** (ContentJournalDay's): the long date (the host's `formatDate`) in the editor's
  face, semibold, larger for today; "Today" / "Yesterday" as a badge; "Open as page" at the right —
  compact (phones): the weekday and the badge's word as a small line above the date, the button an
  icon; the date and the button open the day as a page (`onOpenAsPage`). A day without blocks
  shows the prompt ("What's on today? Click to start writing." …) whose click begins the day
  (`beginDay`: its last block edited, or the journal template's first empty block of a new page —
  the file is made by the first edit; `onBeginRequested` lets the host refuse in reading mode). For
  screen readers: the section a group named by the date, the date a heading, the button and the
  prompt buttons.
* **The stream's header and footer** (the missed reminders, "%n day(s) with notes", "Go to day…";
  the empty journal's hint) are QML items placed over the list's header and footer, as a page's.
* **Scrolling**: `scrollToDay(day, position)`, `scrollToDate(date)` (the day or the nearest earlier
  one), `viewState()` / `restoreViewState()` (the day at the top and the offset into it), the
  room the touch toolbar covers at the bottom (`setBottomInset`: reveals keep above it).
* **Across days**: the find bar's matches tinted in every day (`setFind`), the days attached to the
  stream's `StreamSelection` and `StreamFind` while they are made (Shift+↑/↓ past a day's edge and
  the selection keys' `ExtendPastTop` / `ExtendPastBottom` extend the span into the next day;
  Shift+click and taps go through the host as on a page).

**Hosting**: `AtramentView { view: "journal" }` (qml/editor/EditorAtramentJournal.qml) takes the
QML stream's place in JournalsView when the editor is drawn by Atrament. There is **one BlockEditor
for the whole stream**, without rows of its own (`BlockEditor.atramentHost`: the stream's
AtramentView is its page), whose `controller` is the active day's: every signal of a day (a click
asking to edit, a link, a menu, a selection key…) makes that day the active one first
(`AtramentView.activeController`, `activeDayChanged`), so the editor's functions — menus,
suggestions, the touch toolbar docked on the stream's frame, Vim, the find bar's current match —
act on that day with the handlers a page has (EditorAtramentPage's). The AtramentView's page API
(`editRow`, `editText`, `revealRow`, `selectInEdited`, `rowTop`…) is the active day's. A day's
foreign rows (its properties, what Atrament does not draw yet) are BlockDelegates over its rows
as on a page (a `ForeignRowHost` per day made), their editor the stream's: a press on one makes its
day the active one.

**Built** (2026-11-27): `ListHandle::scrollToRowPart` (`tests/atrament/ui/tst_atrament_list`),
PageView's embedded mode (`pageembed.cpp`: `setEmbedding`, `takeReveal`, `embeddedPart`,
`setKeysActive`, `setFooterTapHeight` — the strip under a day's blocks appends one —,
`pointerReleased`, `pressedRow`), `JournalView` as above, with what the design left open decided
so: a day's sections are known by date (the list's row ids, the heights kept across resets); days
are made within a screen of the last frame's view and released beyond two (a little hysteresis);
the stream's own defaults for the selection's keys past a day's edge (`StreamSelection.extendBy`)
and Shift+↑/↓ from an edited block when the host takes neither; the selection's span moving into
another day (`focusMoved`) makes it the active day and reveals the row, `revealRequested` scrolls
to the day; a mouse's press on a block dragged into another day extends the span
(`StreamSelection.extendFrom`); the docked toolbar is the list's bottom padding (its rows, reveals
and the focused field's caret end above it); a word an input method composes in the day being left
is committed first. AtramentView's "journal" (`daysModel`, `editorHost`, the days' options,
`streamSelection`, `streamFind`, `journalLook`, `streamHeader` / `streamFooter`, `bottomInset`;
`activeController`, `dayController`, `scrollToDay` / `scrollToDate` / `scrollBy`, `activateDay`,
`beginDay`, `viewState` / `restoreViewState`, `dayTop` / `dayRowTop` / `dayPoint`, `followRename`,
`refreshModes`, `markFoldsRestored`; signals `activeControllerChanged`, `openAsPageRequested`,
`beginRequested`, `dayMade` / `dayLoaded` / `dayReleased`, `dayFoldStateChanged`,
`dayExternallyChanged`); its looks and callbacks are per page (`forEachPage`), the touch selection
mode and Vim the active day's. In the app: qml/editor/EditorAtramentJournal.qml,
`BlockEditor.atramentHost`, JournalsView's `atramentStream` / `streamEditor` / `streamView` (docs/UI.md
"The journal stream drawn by Atrament"). Tested by `tests/atrament/editor/tst_atrament_journal`
(18 cases) and `tst_content_journals_atrament_qml` (ContentJournalsAtramentSelfTest.qml: the app
with `--atrament-editor`). **A day zoomed into a block** has the zoom's breadcrumb above its
blocks as ContentJournalDay's editor has it in its header: AtramentView's `dayHeader` (a Component,
EditorAtramentJournal's: ZoomBreadcrumb over the day's controller) is made for a day when its
controller zooms and released when the zoom ends (`updateDayHeader`), the day's `ForeignRowHost`
placing it over the page's header as a page's header is (a host tells each edge's height while it has
an item for it and 0 once the item goes: a day's footer, the stream's room under its blocks, is never
the host's); the zoom reveals the day's top to the zoomed block (`JournalView::revealZoom`: the day
got shorter, what was in view may be past its end). Tested by `tst_atrament_editor_hosting`
(`headerComesAndGoes`), `tst_atrament_journal` (`zoomShowsTheDaysTop`) and the app's self-test (the
breadcrumb above the day's blocks, its parent crumb clicked zooming out to it, Back). **Drags across
days** (`journaldrag.cpp`): a day's drag goes on in the stream when it leaves the day — a mouse's
drag selection and a finger's (a long press, a grip) select on into the day under the pointer
(`PageEmbedding::selectTo` → `JournalView::selectAcross` → `StreamSelection.extendFrom`, the day
and block where the days are now: the pointer held into the view, the scroll since the last frame
added; over the day it began in the page selects itself, which ends a span); held near the stream's
top or bottom edge it scrolls the stream (`PageEmbedding::viewport`: the stream's view, not the
page's, whose header may be far away) and the days that scroll in take the selection's end; its
moves and its release are followed by the stream (a capture listener painted after the days:
`JournalView::followDrag` → `PageView::followDrag`; an embedded page registers none) so a drag
whose day scrolled away — no longer laid out, no layer of its own — still ends where it is let go.
Tested by `tst_atrament_journal` (`mouseDragAcrossDaysScrollsTheStream`,
`fingerSelectionAcrossDaysScrolls`: they failed before — the day's own drag took the moves into the
next day and selected inside its own day). **A long day laid out where it shows**: an embedded
page lays out only its rows within a screen of what the stream shows of it — where the last frame
put it (its header's place, the scroll since), where the stream says its next frame goes
(`PageView::setEmbeddedView`, told to every day made before the list lays them out:
`JournalView::tellDaysTheirView`), the edited row and a row being revealed (`takeReveal`: its part is
what the stream scrolls to) — and the rest of its column is room of the rows' heights (measured, or
the estimate); the rows are chosen once per frame, before the page's frames of the last one go
(`embeddedRows`). Rows above the view laid out as they come near get their real heights, and the
stream's anchor — a day and an offset into it — would let what is on screen move by the difference:
after each frame the stream keeps the view on the block at its top
(`JournalView::keepAnchorOnBlock` → `ListHandle::anchorOnPart(row, part, into)`, generic in
`ui/measuredlist.h`: the next frame lays that row out, asks the resolver where the part starts now
and puts the view `into` px below it, the scroll since added; one frame's, dropped by a jump, the
list at its top or end, rows inserted or removed). Measured (docs/PERFORMANCE.md "Atrament's journal
stream: a long day laid out where it shows"): a wheel step through a 1 000-block day 1.6 ms median
(17–22 ms laid out whole), 81 of its rows laid out a frame. Tested by `tst_atrament_list`
(`anchorOnAPartOfARow`) and `tst_atrament_journal` (`longDayLaidOutWhereItShows`,
`longDayRowsMeasuredLateMoveNothing` — a row in view moves by the scroll alone, 48 px off without
the anchor —, `longDayRevealsADeepBlock`). **The span self-test against this stream**:
`tst_content_journals_span_atrament_qml` (desktop) and `…_touch` (a phone) run
ContentJournalsSpanSelfTest's steps (qml/content/ContentJournalsSpanAtramentSelfTest.qml, loaded by
JournalsView for `--editor-script journals-span-selftest --atrament-editor`) with real mouse, touch and
key events at the places the stream says its days' blocks are — Shift+click and a tap in the
selection mode into the next day, the floating and the docked tools with the stream's count, Ctrl+C
and the tools' Copy with the days' links, Delete, Tab, Cut and the block menu's Move up changing
nothing, Shift+↓/↑ across a day's edge, Esc and Back, a mouse drag into the next day and on, held at
the stream's bottom edge, into the days coming in, the span's days scrolled far away and back, a tap
with none of them on screen editing and ending it. It found, and these changed: a day made the one
written in by a click took the selection of the day before away before the editor could extend it
(Shift+click and the selection mode's tap selected the clicked block alone) — its selection now
goes when the new day selects or edits on its own (`dayActivity` → `clearOtherSelections`); two keys
before a frame went to the day whose key layer the last frame drew, not to the day the span's end had
just moved into (an embedded page's key layer listens always and takes a key only while its day is
the active one); a long press on a row Atrament draws set the editor's "the release is no tap" flag,
which nothing reset there (Atrament cancels the press itself), so the next tap — into the next day —
did nothing (`BlockEditor.longPressBlock`, EditorAtramentPage's long press, without the flag); a day's
part of a selection over several days kept the day made, and the day written in too, so its tools
showed with no day of the span on screen and a tap far away extended the span instead of editing (a
span's days, the day written in among them, go when far away; the stream is told no day is written
in, and the editor's selection mode, one for the stream, ends with a day that has no block selected
and no span to extend). **Not yet**: a hosted row's delegate showing the active day's editing state
while its own day is not active.

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
   `tests/atrament/editor/tst_atrament_editor`. Foreign rows are the editor's QML delegates placed
   over the list, the page's title and properties its header ("Rows Atrament does not draw yet");
   callouts and a tap that edits a block came with step 2. Links, chips and tags open
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
   Tested by `tests/atrament/editor/tst_atrament_editing`. Also built (2026-11-27), what
   InlineEditor, MarkdownHighlighter and spellmarks.h do for the QML editor:
   * **Spell marks** (`pagespell.cpp`; `PageView::setSpellCheck`, `AtramentView.spellCheck` /
     `spellColor`: the `SpellCheck` singleton, Theme.danger): the misspelled words of the edited
     text's prose — each line's `SpellRange` from the highlighter's rules (`EditStyler::Extras`),
     `spell::findWords` over it, `SpellCheck::verdict` — underlined with a wavy line (Qt Quick drew
     the QML editor's straight), the word ending at the caret left alone while it is typed
     (MarkdownHighlighter's rule: styled again when the caret leaves it, `TextInput::restyle`),
     never in code; with `SpellCheck.wholePage` the drawn rows' words too —
     `SpellCheck::wrongWords` over the block's body, each put on the display text through the
     rendered text's source map only where every piece over it maps character by character
     (spellmarks.h's verbatim rule: a Markdown link's text or a chip is shown as a whole, left
     alone even when its text is the word). New verdicts (`revisionChanged`), the checker switched
     on or off and `wholePage` style the field and draw the rows again. A right-click on a marked
     word — on touch UIs a tap on one in the edited text, after the caret went there — asks for
     the spelling's menu (`onSpellingMenu`, `AtramentView.spellingMenuRequested(row, edited,
     {from, to, word, text}, x, y)` → `BlockEditor.openAtramentSpellingMenu`: the text menu's
     spelling items; a correction through `PageController.replaceRange` in the edited text,
     `replaceInBody` in a drawn block); a right-click elsewhere is the block menu as before.
   * **Reminder chips while editing** (`pagechips.cpp`): every ⏰ / ⏰! token of the edited text
     (`LineChip`s of the same rules, labelled in the controller's locale relative to its day, both
     followed) is an inline object of the field's spans — the room of its words ("⏰ jutro 18:00")
     in ems of the field's font, the pill of the palette's `reminderBackground` /
     `alarmBackground` 2 px beyond it, the words in `reminderChip` / `alarmChip` on the line's
     baseline — while the text keeps the token byte for byte; one unit for the caret (a caret put
     inside by the controller or Vim goes to its end, a selection's moving end over it: chipSnap);
     a click is its dialog, a right-click or a finger held on it its menu, neither moving the caret
     (`onReminderChip`, `AtramentView.reminderChipActivated` →
     `BlockEditor.editReminderInText` / `openReminderMenuAt`).
   * **Vim** (`pagevim.cpp`; `PageView::setVim`, `AtramentView.vimSession`: `BlockEditor.vimSession`;
     the decision under "Open questions"): the edited block's keys go to the `VimSession` first by
     `BlockEditor.handleEditorKey`'s rules (an idle Esc goes on to the editor, a mouse's selection
     in normal mode becomes the visual one, in the command modes every key but the editor's
     shortcuts — `vimPasses` — is Vim's or dropped; after a key Vim took `vimKeyHandled` →
     `BlockEditor.afterVimKey`); the field follows the mode — a steady block caret in normal and
     visual mode, an underline in replace mode, read-only there (no typing, no input method), the
     bar in insert mode —, Vim's visual selection in the block is the field's
     (`textSelectionRequested`), over several blocks their band; zt / zz / zb scroll the list
     (`scrollRequested`), H M L are told the rows in view (`viewRequested` → `setView`); Vim's Ctrl
     keys win over the window's shortcuts in its command modes (`AtramentView`'s ShortcutOverride,
     `BlockEditor.vimOwnsShortcut`); VimStatus shows the session as before. Keys reach the session
     as Qt's key events would (`Qt::Key`, modifiers, the typed text; Ctrl+letter its control
     character).
   * **The touch toolbar's highlight menu**: with the page drawn by Atrament the toolbar's Highlight
     opens the same colours (`BlockEditor.openHighlightMenuAbove` without an InlineEditor); a colour
     goes to `EditorAtramentPage.applyHighlight` — the composition committed, the field's selection
     (`AtramentView.editSelectionStart` / `editSelectionEnd`) through `PageController.setHighlight`,
     the highlighted text selected again (`selectInEdited`).
   Tested by `tst_atrament_spell`, `tst_atrament_chips` and `tst_atrament_vim` (tst_editor_vim's
   key rows typed into the Atrament window), TextInput's carets, read-only state and objects by
   `tests/atrament/ui/tst_atrament_textinput`.
   * **The edited text's menu** (`pagetextmenu.cpp`; InlineEditor's right-click): a right-click
     inside the edited field (beside it — the bullet, the margin — the block menu as before) commits
     the input method's word, moves the caret to the pointer unless it is inside the selection, and
     asks for the text menu (`onTextMenu(row, misspelling, at)` → `AtramentView.textMenuRequested`
     → `BlockEditor.openAtramentTextMenu`): BlockEditor's own menu, its items acting on the field
     (`textMenuAtrament`) — Cut, Copy, Paste and Select all through `AtramentView.textCommand`
     (`PageView::textCommand`: what the keys do, the controller's exact copy and paste, a paste's
     pictures from the internet offered as after Ctrl+V), "Highlight…" and "Block…" under the
     caret once it closed (`EditorAtramentPage.openMenuAtCaret`; the colours through
     `applyHighlight`), the AI's items and "Encrypt selection…" on the field's selection
     (`BlockEditor.pointedAt()` "atrament"), a misspelled word's corrections at its top
     (`replaceRange`); the keyboard back in the field when it closes. On touch UIs the platform's
     long press selects a word through the input method and its handles and floating toolbar ask
     the item what is selected (`ImCurrentSelection`, `ImReadOnly`); the toolbar's Cut / Copy /
     Paste / Select all reach the field as Ctrl+X / C / V / A without text
     (`QAndroidInputContext::sendShortcut`) — the editor's keys, the controller's copy and paste. A
     tap on a misspelled word still opens only its corrections. Tested by `tst_atrament_editing`
     (`textMenuOnARightClick`, `textCommandsActOnTheSelection`), `tst_atrament_spell` and the
     self-test (`atramenttest`: the menu's items with real clicks, the highlight, a correction; on
     a phone the input method's selection and the toolbar's shortcuts).
3. **Selection.** Block selection (Shift+click, Shift+arrows, a mouse's drag across rows), its band,
   copy / cut / paste through `PageController`'s selection; on a phone the long press and the
   handles. **Built** (2026-11-27; `src/app/editor/atrament/pageselection.cpp`, `pagedrag.cpp`):
   the band (`selectionEdge`) in BlockPartSelectionBand's geometry — a block alone framed from its
   own level, a range one straight band from the column's edge, a little past the text column on
   the right; also under hosted rows —, Shift+click and Ctrl+click
   (`BlockEditor.blockTappedWith`), Shift+↑/↓ out of the edited block, Ctrl+A twice, Esc, a
   mouse's press dragged from one block into another (`PageView::dragOver`: the row under the
   pointer from the list's anchor; its release edits nothing; `dragSelectRequested` →
   `leaveEditingForSelection` + `selectRange`), a finger's long press (the touch selection mode).
   **The selection's keys are the page's**: the page keeps the keyboard while blocks are selected
   (BlockEditor's `keysHolder` is the AtramentView, not the list), and a listener on the window's
   root dispatch node takes every key no focused element took (`PageView::selectionKey`,
   BlockEditor.handleSelectionKeyCode's): Shift+↑/↓ extend or shrink the selection, ↑/↓ move it
   (with none: ↓ the first block, ↑ the last), Alt+↑/↓ move the blocks, Tab / Shift+Tab indent
   them, Ctrl+Enter their tasks' next status (a parent selected alone: itself), Ctrl+. folds, Enter
   edits (a table: its delegate takes the keys), Delete / Backspace, Ctrl+C / Ctrl+Insert, Ctrl+X /
   Shift+Delete, Ctrl+V / Shift+Insert, Ctrl+A, Ctrl+Z / Ctrl+Y, Esc, Android's Back. Each asks
   the host first (`onSelectionCommand` → `AtramentView.selectionCommand(command, row)` →
   `BlockEditor.atramentSelectionCommand`, `acceptSelectionCommand()`): copies, cuts, deletions and
   pastes with their toasts, Esc and Back leaving the selection mode, the task status menu
   (Ctrl+Shift+Enter), source mode (Ctrl+E), the block reference (Ctrl+Shift+C), the journal
   stream's selection over several days (refused, or extended across the day's edge), a selection
   with gaps that does not move; the rest the page does through `PageController`, scrolling the
   selected row into view (`PageView::revealRow`, also `AtramentView.revealRow` for
   BlockEditor.ensureRowVisible). The selection's Ctrl keys win over application-wide shortcuts
   (`Keys.onShortcutOverride` on the AtramentView). **Touch**: in the touch selection mode
   (`AtramentView.selectionGrips`, BlockEditor.selectionGrips) the band's first and last rows carry
   BlockSelectionGrip's grips (a round 18 px grip in a 44 px hit area, inside the window's edge,
   named for screen readers) that claim the finger's drag (`claimTouchDrag`: no scroll, also over a
   hosted row's delegate) — the other end stays, the finger moves this one; a long press goes on as
   a drag that extends the selection from its anchor (`Window::dragAfterLongPress`; "One by one",
   `selectionPicking`, picks only). **Edge scrolling**: a mouse's drag selection, a grip's or a long
   press's drag held near the view's top or bottom edge (above the touch toolbar:
   `dragBottomInset`) scrolls the page (`EdgeScroller` → `ListHandle::scrollBy`), the selection
   following the rows that come under the pointer (where they are since the last frame:
   `lastY`); a drag's moves and release are the page's (a capture listener: no hover, no click).
   `AtramentView.selectionRect` is where the band is (its rows shown) for the selection's tools.
   Tested by `tests/atrament/editor/tst_atrament_selection` (the selection self-test's checks;
   `tests/atrament/ui/tst_atrament_drag` for the gestures). A mouse's drag from one journal day into
   another extends the stream's selection, and the edge scrolling scrolls the journal stream ("The
   journal stream as one list": drags across days). Not yet: Vim's keys over a selection.
4. **Structure.** Folding, zoom into a block, drag to move, dates and reminders chips, the journal
   — a stream of days as sections of one list (what `JournalDayItem` draws today).
   **Built so far** (2026-11-27): folding (the arrows, step 2's entry), the bullet's zoom, and the
   journal's days drawn by Atrament as a first stage — each day's editor (`ContentJournalDay`'s
   autoHeight `BlockEditor`) shows its blocks in an `AtramentView` as tall as they are
   (`PageView::contentHeight` / `onContentHeight` after each frame's measuring →
   `AtramentView.contentHeight` → `BlockEditor.atramentContentHeight` → the day's
   `fitEditorHeight()`); the journal's list scrolls (a wheel Atrament does not take goes on to it)
   and keeps the caret in view (`BlockEditor.ensureVisible` through its `scroller`). The stream as
   one Atrament list (no item per day) is the second stage, built (2026-11-27): "The journal
   stream as one list" above. **Drag to move** (`pagedrag.cpp`; the QML
   editor has none): an outline's bullet (a list item's marker, also a document's) pressed with the
   mouse and dragged past 4 px moves its block — the selection when it is in it, else the block
   alone, selected — and a finger's long press on a selected block while the grips show moves the
   selection; a drop line (2 px of Theme.focusRing with a dot at its start, from the bullet's column
   of the drop's level to the column's right edge) shows where they would go — **between any two
   rows, as deep as the pointer's x says**, as outliners do: the gap above or below the half of the
   row under the pointer, and of the levels it allows (from the next block's, a sibling of it, to a
   child of the block above) the one nearest the moved blocks' text column (it follows the pointer
   from where the drag began: `PageView::dropAt`), the nearest one that takes them when that one does
   not; none inside the blocks or over their own place. The release drops them there in one undo
   step (`PageController::moveSelectionTo(BlockPlace)`; `canMoveSelectionTo` for the line), the
   blocks staying selected, a folded new parent opened. The move is planned as the range's own steps
   (`src/app/editor/blockmove.h`, pure over `BlockDocument`, `tests/app/editor/tst_editor_blockmove`:
   `planBlockMove(doc, first, last, place)` — Alt+↑/↓ to the place's siblings, Tab into a deeper
   parent after sliding past the child on the way, Shift+Tab out of one at the end of the siblings,
   so it takes no sibling along — tried on a copy first, then `applyMoveStep` on the page): the
   blocks keep their uids (the selection, folds), every byte no step touches stays, numbered lists
   are renumbered as the steps do; refused inside the blocks or under one of them, under a block that
   is no list item, deeper than the block above allows, where the next block's parent would change,
   above the page's properties, outside a zoom, and for a paragraph, a heading or a table below the
   top level. The edge scrolling as in step 3; the release clicks nothing (a bullet's click without a
   drag still zooms). **Across the journal's days**: blocks dragged out of their day show the drop
   line in the day under the pointer (`PageEmbedding::dragBlocksTo` → `JournalView::dragBlocksTo`:
   the same `dropAt` on that day's page, its places those `PageController::canInsertBlocksAt` takes —
   a paragraph, a heading, a table only at the top level —, drawn there: `PageView::setIncomingDrop`),
   the stream scrolling at its edges and the days coming in taking it; let go there
   (`endBlockDrag`), their Markdown (`selectionMarkdown()`) goes into that day's file first
   (`insertBlocksAt`: their nesting below the place, their block ids kept — a move) and then out of
   theirs (`deleteSelection`), selected where they went. **Undo**: two steps, one per file (each day
   has its own undo stack, PageController's); the stream pairs them — Ctrl+Z in either day while both
   are still those days' last steps (`undoToken()`) takes the move back in both, Ctrl+Y right after
   does it again in both (`JournalView::undoMoveAcross` around the day's `onUndo`); after another edit
   in either day each undoes its own half. Not yet: a drop into a day without blocks (its prompt),
   the toolbar's and the menu's Undo pairing them (they undo the day they act on).
5. **Blocks with views.** Code (the input with the code highlighter), tables, math (Brulion Math's
   glyphs through Atrament's text), charts and Mermaid (their core layouts painted as Atrament paths),
   pictures (`img`), queries, transclusions, properties. When nothing is foreign, M2's gate is
   measured; the QML delegates go in M3.
   **Built so far** (2026-11-27): code blocks (`PageView::setDrawsCode`, `AtramentView.drawsCode`
   / `codeLook`): CodeBlockView's sunken panel with its language label, the code in the code face
   coloured by the highlighter's Code mode (`EditStyler`, Luau for ```luau / ```lua), a click
   editing it at the character (the code is shown as it is edited), Enter a line break and Tab a
   tab in the field, ↑/↓ across blocks. With line numbers on (the device's setting or the note's
   `line-numbers`: `CodeLook::lineNumbers`, `codeLook.lineNumbers` in EditorAtramentPage) the panel
   has CodeBlockView's gutter (`pagecode.cpp`): Theme.space3 in, a column as wide as the longest
   number, Theme.space3 to the code; each line of the code numbered on its first visual line (a
   wrapped line once), right-aligned, in the code face and size and the label's faint colour, on
   that line's baseline — painted in the frame from the code's own layout of that frame (the drawn
   `Text`'s or the edited `TextInput`'s, laid out in prepaint), so a new wrapping or a typed line
   moves the numbers at once; no node for screen readers. Code blocks are Atrament's with or without
   them (`drawsCode` no longer depends on the setting; tested by `tst_atrament_codelines`).
   Calculations
   (`PageView::setDrawsCalc`, `AtramentView.drawsCalc` / `calcLook`, `calcrows.cpp`): CalcBlockView's
   panel under its small "Calculation", the lines in the note's face, each line's result
   (`calcResults`) right-aligned at its line's top past a thin separator — an assignment's muted,
   an error smaller in red, a total semibold under a rule —, a result node per line for screen
   readers ("Result: 178"); a click edits the lines at the character, Enter is a line break, the
   highlighter's Code rules colour comments in the note's face. Where a line starts comes from the
   text's layout of the last frame: a frame that wraps the lines anew draws once more. Thematic
   breaks (`---`, `rulerows.cpp`): BlockPartRule's 1-px line in `AtramentView.ruleColor` across the
   text column, a line's height tall (a separator for screen readers), its Markdown in the field
   while edited.
   Charts and Mermaid diagrams (`PageView::setDrawsDiagrams`, `AtramentView.drawsDiagrams` — on —
   and `diagramLook`; `src/app/editor/atrament/diagramrows.cpp`, `diagrampaint.h`): the core scene
   (`diagram::render`, `chart::layoutChart` at the column's width) painted by `paintDiagram` with
   Atrament's paths and text — MermaidView's painter: the same items and colours
   (`diagram::themedPalette`, light and dark), round caps and joins, dotted 2/3 and dashed 6/4,
   labels in the app's font measured by Atrament's own fonts (`atramentMetrics`) — on
   ChartBlockView's / MermaidBlockView's sunken panel; `DiagramElement` is as tall as the scene at
   its width (scaled down, never up; a diagram to 70 % at most, then the row scrolls sideways);
   the text alternative is the panel's image node (a chart: its type, every value as the
   description; a diagram: its title or kind); a source that cannot be drawn shows itself with the
   translated reason under it (danger colour, a type Brulion does not draw muted); a click edits the
   fenced source in the field (the code highlighter's colours; on the shown source at the
   character) with the diagram drawn under it as it is typed, Enter a line break, Tab a tab, Esc
   and ↑/↓ leave it. Math formulas (`setDrawsMath`, `AtramentView.drawsMath` — on —;
   `mathrows.cpp`): `renderMath`'s rich text read into runs (`renderedTextFromHtml`, the block's
   `renderedText`) and drawn as Atrament text, centred on the panel in the editor's font (Brulion
   Math's symbols by fallback): scripts at 0.7 of the size raised 0.4 em or lowered 0.2 em
   (`baselineShift`), the radicand overlined, \texttt in the code face; a formula with what the
   text does not draw yet (small capitals, `PageView::mathDrawable`) stays the QML view's, decided
   per formula — and when the editing starts: an edited formula stays whose editor it was until it
   is left. A click edits the LaTeX source (Enter a line break, the field left-aligned where
   MathBlockView centres it). Page icons drawn from BIcon's SVGs ("Vector paths and icons").
   Tested by `tests/atrament/editor/tst_atrament_diagrams`.
   Tables (`PageView::setDrawsTables`, `AtramentView.drawsTables` — on by default — / `tableLook`;
   `src/app/editor/atrament/tablerows.cpp`, `tablelayout.h`): TableBlockView's grid — the header on
   its sunken band, the frame (the focus ring at half while a cell is edited), the lines between the
   rows (a stronger one under the header) and the columns, formula cells tinted and right-aligned in
   a column without an alignment — with each cell's rendering (the model's `tableData`: its `html`
   read into RenderedText, drawn with `renderedSpans()`, so links, chips and tags open:
   `AtramentView.tableLinkClicked`, handled as TableBlockView handles them), the columns'
   alignments (`:--`, `:-:`, `--:`), TableBlockView's natural column widths (the header's lines and
   each cell's visible text measured by Atrament's text layout, 56…280 px) shared out in proportion
   to them on a wider page (flex-grow), a table wider than the page scrolled sideways inside its row
   (the wheel with Shift, a touchpad, a finger), a cell longer than its column on one line ending
   with "…" (a header: its `<br>` lines; "Text", line clamp), as TableBlockView elides it, each cell
   aligned by Atrament's own text alignment ("Text": its links, its layout handle's offsets and the
   caret a click puts are where its glyphs are). A click or a tap on a cell edits it at the
   character under the pointer (`BlockListModel::tableCellText`: the cell rendered with its source map; a formula as written,
   the caret at its end): a TextInput styled by EditStyler's Inline rules, each change written by
   `PageController::setTableCell(…, typing)` (one undo step per cell, nothing waits for the cell to
   be left), the controller editing no block meanwhile. The keys, TableBlockView's cell field's:
   Enter the cell below, Tab / Shift+Tab the next / previous cell (Enter in the last row and Tab in
   the last cell add a row), ↑/↓ the cell above / below at the caret's x and — where the QML table
   stops — past the header or the last row the block above / below (↑/↓ from those edit the
   header's or the last row's cell under the caret), ←/→ at the text's edge the cell beside,
   Ctrl+Enter a row below, Alt+↑/↓ and Alt+←/→ move the row / the column with the cell, Alt+Enter
   the link at the caret, Ctrl+V of several cells pastes them from this one (then selected), Esc
   cancels the cell's typing (`cancelTableCell`) and selects the table, Ctrl+Z / Ctrl+Y the page's undo with the cell
   edited on (the touch toolbar's undo in a cell). "+ Row" / "+ Column" under the grid while a cell
   is edited or the table is selected (always on touch UIs); a right-click on a cell opens the block
   menu with its Row and Column (`tableCellMenuRequested` → `BlockEditor.openCellBlockMenuAt`) —
   also on the edited cell, whose editing ends first (the table stays the current block) —, and a
   right-click or a finger held on a cell's link or chip its own menu (`link(range, onClick,
   onMenu)` → `AtramentView.tableLinkMenuRequested` → `BlockEditor.openCellTokenMenu`, else the
   link's menu; in the touch selection mode a finger held there is the row's long press). For
   screen readers a table of rows of cells — Atrament's new generic roles `Table`, `Row`, `Cell`,
   `ColumnHeader` (`ui/accessibility.h`, mapped to Qt's in `qt/qtaccessibility.cpp`) —, each cell
   named by its text and described by its column's header, the edited one a text input "Cell …", the
   table with Qt's table interfaces ("Accessibility" above). With `drawsTables` off every table is the QML view's. Tested by `tests/atrament/editor/
   tst_atrament_tables` (TableSelfTest's checks of the keys, of the cell selection and of the filter
   row among them) and `tst_editor_qml_atrament_table[_touch]` (qml/dev/AtramentTableSelfTest.qml,
   `--editor-script atramenttabletest --atrament-editor`: TableSelfTest's steps with real input
   against the page drawn by Atrament — the cell's block menus and their new cells edited after
   them, the keys in a cell, sorting, the filter row, CSV, the cell selection with Shift+arrows and
   Shift+click, copying, cutting, emptying, pasting, typing over a cell, Enter on the selected
   table; on a phone the toolbar's More, Next cell, Undo, Redo and Hide keyboard with the edited
   cell above it; TableSelfTest's chart part is the chart's, drawn by Atrament too, and left out).
   * **The cell selection** (`tableselection.cpp`; TableBlockView's): Shift+arrows from the edited
     cell (a horizontal one at its text's edge) and Shift+click on a cell of its table select a
     rectangle from it; Shift+click moves its end. While it is there no cell is edited and its keys
     are the page's (the window's root key listener, before the block selection's:
     `PageView::cellRangeKey`): the arrows move it, with Shift its end (the header and the last row
     stop), Home / End the row's first / last cell, with Ctrl the table's, Tab / Shift+Tab the
     next / previous cell (a new row after the last cell), Enter / F2 edit the cursor cell, a typed
     character edits it with that character in place of its text (written at once), Delete /
     Backspace empty the cells, Ctrl+C / Ctrl+X copy and cut them as TSV, Ctrl+V pastes the
     clipboard over them (TSV, CSV, a Markdown table, lines; one value fills the selection; rows
     added past the last) with the pasted cells selected after — as a paste of several cells from
     the edited cell does —, Ctrl+A every cell, Ctrl+Z / Ctrl+Y the page's undo with the selection
     kept, Esc ends it with the table selected; each change one undo step (`PageController.
     copyTableCells` / `clearTableCells` / `pasteClipboardCells`), its toasts through
     `PageView::onNotice` (`AtramentView.noticeRequested` → `BlockEditor.notice`, in TableBlockView's
     translations). It ends when a cell or a block is edited or blocks are selected. Drawn in the
     cells: Theme.accent's tint, the cursor cell outlined in the focus ring and scrolled into view,
     also sideways (`canvas(…).onPrepaint(f)`, new in Atrament: a painter that also runs in prepaint,
     where the scroll containers around are known); selected cells are selected for screen
     readers. Its Ctrl keys win over the application's shortcuts (`AtramentView.cellsSelected`).
   * **The filter row** (`tablefilters.cpp`): while a table filters (the block menu's "Filter
     rows", "Filter by this column…") a field per column under the header — BTextField's look,
     "Filter" in it, named "Filter of column …" for screen readers, its × emptying it —; typing
     filters the rows (`PageController::setTableFilter`: nothing written; a composed word once
     committed), Esc empties a field and in an empty one hides the row and selects the table, Enter
     leaves the field; a filtered column's header has the funnel (BIcon "filter" in Theme.accent);
     under the grid "N of M rows shown" and "Clear filters" next to "+ Row" / "+ Column" (also in
     reading mode). `PageView::focusFilter` gives a column's field the keys.
   * **The editor reaches a table drawn by Atrament as it reaches a TableBlockView**:
     `AtramentView.tableAt(row)` is an `AtramentTableItem` (an item never shown, the view's) with
     TableBlockView's functions and properties by name — `startCell`, `focusFirstCell`,
     `openFilter`, `inSelection`, `selectedRows`, `selectionLeft` / `Right`, `nextCell`,
     `undoInCell`, `redoInCell`, `closeCell`, `cellLength`, `cellRowCount`, `cellAlignOf`,
     `commitComposition`, `blockIndex`, `editRow`, `editColumn`, `columns`, `filtering` —, which
     `BlockEditor.tableAt` returns first (through the new `QmlCall.objectOf`): the block menu's Row
     and Column items edit their new cell afterwards (`BlockEditor.editTableCell`), "Filter by this
     column…" gives its field the keys, a right-click on selected cells offers the cells' commands
     (`BlockMenu.readPicked`). While a cell is edited that item is the editor's `editingTable`
     (`AtramentView.tableEditingChanged`, `editedTable()`): the touch toolbar's cell tools work on it
     (Undo and Redo with the cell edited on, Next cell, More: the table's block menu at its row,
     Hide keyboard; `cellHasKeys` for the toolbar), and the Atrament page ends at the toolbar's top
     while a toolbar of typing is docked (BlockEditor's `atramentLoader`), so the edited cell — and an
     edited block's caret — stays above it: Atrament's field keeps its caret in view when the window
     gets another size. Enter on a selected table edits its first cell (`AtramentView.
     editFirstTableCell`, `PageView::editFirstCell`), and Alt+←/→/↑/↓ in a cell move its row or
     column instead of being the app's Back and Forward (the page's ShortcutOverride while
     `AtramentView.editTableRow` is a row). The tools are `PageView::nextCell`, `undoInCell`,
     `closeCell`, `onCellEditingChanged`.
   A wide table's sideways scroll bar is dragged with the mouse as any Atrament scroll view's
   ("Views and entities"). The mouse pointer resting on a cell whose formula failed shows the
   error as a tooltip, on a cell cut short ("…") its whole text (TableBlockView's BToolTip:
   `PageView::onToolTip` → `AtramentView.toolTipRequested` → EditorAtramentPage's BToolTip over the
   cell), and on a link of a cell its preview as everywhere on the page (below).
   **Pictures, files, embedded blocks and query results** (2026-11-27; `PageView::setDrawsMedia`,
   `src/app/editor/atrament/mediarows.cpp`, `queryrows.cpp`, `picturecache.h`; `AtramentView.
   drawsPictures` / `drawsFiles` / `drawsTransclusions` / `drawsQueries`, all on, and `mediaLook`).
   A row is Atrament's only when every part of it is drawable and its kind switched on
   (`PageView::drawsMediaRow`), else the whole row stays foreign, its text too (before, such a
   row's text was drawn and its pictures lost). A block's embeds go below its text as EmbedView
   lays them out (from the text column's edge; no text line for a block of nothing but embedded
   blocks, nor for a document's paragraph of nothing but pictures, unless edited — the edited row
   keeps them under its field):
   * **Pictures** at their natural size up to the column's width and 420 px high, their shape kept,
     the frame's size from the file's header (the model's `embeds`): the row is as tall before the
     picture is decoded as after; one without a size in its header is 320 × 180 until decoded, then
     what it measured (once); rounded, the hairline frame by the note's or the device's setting or
     while nothing is shown (loading, a format the device cannot decode — its frame names it, as the
     picture view does), the light backing in a dark theme; its alt text (else its name) an image
     node for screen readers. A click opens it (`AtramentView.embedClicked`: what EmbedView's
     signals open — the picture, WebPictureViewer for one from the internet, the PDF viewer, a
     file), a click beside it edits the block, a right-click or a finger held on a picture from the
     internet its menu (`embedMenuRequested` → WebPictureMenu), elsewhere the block's menu. A
     PDF's thumbnail is PdfEmbedTile's (white paper of the page's shape, the caption with the page
     count; while it is drawn the spinner on it, a PDF that cannot be shown a lock or a warning and
     the reason, its button named with it — `PageView::pdfTile`), other files are BChip's chips (an icon of Atrament's own — a page, a picture — and the
     name). While a picture loads BSpinner turns in its frame (a ring and an arc of 100°, once in
     850 ms: `paintSpinner`, the page drawn again every 33 ms on Atrament's clock while it does). An
     **animated picture** (a GIF) plays while it is shown — its frames from the cache, each as long
     as it says (`Media::animationFrame`: one timer on Atrament's clock for the page, the earliest
     frame due; `PictureFrame::frame`) — and waits where it was while scrolled away (nothing is drawn
     for it then: no timer), going on with the time it had left when it comes back; it loops.
     A **picture from the internet not shown yet** (`web`, its state "ask", "loading" or "failed")
     is EmbedView's placeholder: a 320 px frame at least 180 high naming its alt text (or why it
     could not be fetched) and its site ("Picture from …"), "Show" ("Try again" after a failure)
     and "Save to notebook" (`onWebPicture` → `AtramentView.webPictureRequested` →
     `PageController.showWebPicture` / `BlockEditor.downloadWebPictures`; nothing is fetched
     before), only the spinner while it loads (the words keeping their room); then its picture.
     A **drawing** (`drawing`: its preview from the app's "drawing" image provider,
     `EditorHost::drawingUrl`, the dark version in the dark theme — `MediaLook::dark`) is drawn at
     its own size, never enlarged (fitted into 1024 × 420), fitted into its frame as an SVG, without
     the light backing, a pen in its corner (BIcon's `pen-tool` on a raised square) when the frame
     is at least 72 × 48; a click opens the drawing editor (`embedClicked`).
   * **PictureCache** (one per process, `PictureCache::shared()`): QImageReader on two worker
     threads, from `file:` and `data:` URLs (a picture from the internet once cached), at the
     frame's longer side in device pixels in PictureDecode's steps (never enlarged; an SVG rendered
     at that size), premultiplied RGBA as `ImageData`, a photo stored sideways turned upright by its
     EXIF orientation (as QML's Image with autoTransform and `editor::pictureSize`: a quarter turn
     transposes the size and the side it is scaled to). A 48 MB budget over what nobody holds — a
     page keeps the pictures of its last two frames, so a screenful larger than the budget stays
     whole and nothing is decoded twice per frame —, the least recently asked for first; one
     picture per file, decoded again for another side while the old one is shown; a header of more
     than 50 MP refused, a failure remembered until the file's size changes. An animated picture is
     decoded whole on the worker (every frame, composed by the decoder, with its delay — 0 or 10 ms
     shown for 100, as browsers do; `Result::animation`, its first frame `image`), at most 32 MB of
     frames (`MaxAnimationBytes`: a larger one decoded smaller to fit) and 1 000 frames; the budget
     counts every frame and lets go of none while a page holds the animation. Pictures of image
     providers (`image://<id>/…`) come from a provider function registered for the process
     (`PictureCache::setImageProvider`, QQuickImageProvider::requestImage's contract, called on the
     worker threads: AtramentView registers DrawingImageProvider as "drawing").
   * **Embedded pages, sections and blocks** in TransclusionView's frame: the source's edge line,
     the header (a link: opens the source), the blocks rendered read-only (their HTML read into
     `RenderedText`: headings, quotes, closed tasks, code as plain text in the code face, rules,
     nested embeds' lines and what stops them, guide lines by depth), links clickable
     (`embeddedLinkActivated`: a date chip's dialog, a task's or a reminder's place, else the
     link), a task's box changing the task in its own file (`PageController::toggleTaskAt`), a click
     on the text editing the block, "N more blocks — open the page". A block with math or
     preformatted text keeps the QML view.
   * **Query results** (QueryBlockView, its `QueryResultModel` kept for the block while it is
     drawn, run when it is first drawn): the header with the count, the filters as chips, group
     headers, tasks with a working box (`QueryResultModel::toggleTask`), items rendered as text with
     their links and their page as a quiet link (a daily note by its date), Dataview TABLE rows as
     columns under their headers, at most 4 lines an item ("…" after the fourth), "Show N more", the messages
     (still indexing, nothing found, an error, no notebook). A click on an item opens it
     (`queryAnswered` → BlockEditor.revealRow on this page), a link goes through
     `queryLinkActivated` (a date chip's dialog, else `AtramentView.activateQueryLink`), a click on
     the header or the filters edits the query — while edited the row is the QML delegate's (its
     InlineEditor, the results following the typed query).
   Tested by `tests/atrament/editor/tst_atrament_media` (an animation playing, waiting off screen
   and going on; the spinner; a picture from the internet's placeholder and its buttons; a
   drawing's preview from a provider, its dark version) and `tst_atrament_picturecache`. **Not
   yet** (the QML delegate's rows): sounds and videos, Obsidian bases and database views (`view:`,
   ```base). Nor: BIcon's icons
   for files' chips and the transclusions' headers (Atrament's own glyphs there), an animation's
   loop count (every animation loops), Theme.reducedMotion for the spinner, the pen badge's
   hover look.
   * **Link previews** (2026-11-28): the mouse pointer coming onto a link of the page — a block's
     text, a table cell, an embedded block, a query result — tells the page its href and the file
     it is written in (`TextLink::onHover`, Atrament's: the link it came onto told before the one
     it left, none for a finger; `PageView::onLinkHover`, which keeps the link told last so a late
     leave of another is not told, and resolves a day chip to its day's page as BlockText's
     `previewLink` does) → `AtramentView.linkHovered` → `LinkPreviews.hover` on the view, which
     opens the preview after its delay and closes it when the pointer leaves (an empty href).
     Tested by `tst_atrament_links` (`hoverComesAndGoes`), `tst_atrament_editor` (`linkHovered`),
     `tst_atrament_tables` (`linkInACellHovered`, `cellToolTips`) and the self-test (the preview
     opening and closing over a `[[link]]`).
   * **Menus inside embedded blocks and results** (2026-11-27): a right-click or a finger held on
     a chip or a link of an embedded block's text or of a query result's opens its own menu, not the
     block's (`onEmbeddedLinkMenu`, `onQueryLinkMenu` → `AtramentView.embeddedLinkMenuRequested` /
     `queryLinkMenuRequested` → `EditorAtramentPage.openPartLinkMenu`): the token's menu in the
     block's own file (`BlockEditor.openTokenMenuIn`), else the link menu, whose "Open" follows the
     link as TransclusionView and QueryBlockView do (`linkMenuOpenRequested` with the page as its
     requester; a result's link through `activateQueryLink(…, newPane)`). A result's quiet page
     link ("open:") has none. A result's item ends with "…" after its fourth line
     (`Text::maxLines`). A selected block with pictures or embedded blocks lies on one band
     (`selectionBand()` under the whole block, its grips at its ends). The embedded blocks' and the
     results' texts are kept per frame (`PageView::partTexts()`), so `linkPoint()` — what the
     self-tests click — finds their links too. Tested by `tst_atrament_media` and the self-test.

### Tests

* The C++ tests of `PageController` and `BlockListModel` stay as they are.
* `tests/atrament/editor/tst_atrament_editor` (QtTest runner, `TestWindow` + `FakeEditorHost`): a
  page drawn headless, typing, Enter splitting, Backspace merging, Tab, composition, a tap on a
  link, a finger's scroll, golden frames of each row kind.
* The QML scripts (`tst_editor_qml_*`: selftest, taskstatus, datechips, table, selection,
  keyboard_touch…) drive the QML delegates and cannot run against Atrament; each script's checks
  get their counterpart in `tst_atrament_editor` as its step lands — the gate's "the editor's
  existing behaviour tests pass against it" is that list, complete.
* `tests/atrament/editor/tst_atrament_journal`: the journal stream as one list ("The journal stream
  as one list"); `tst_content_journals_atrament_qml`: its self-test in the app;
  `tst_content_journals_span_atrament_qml[_touch]`: the selection over several days in the app.
* `tst_editor_qml_atrament` (desktop) and `tst_editor_qml_atrament_touch` (a phone, `--touch`):
  the editor playground's `atramenttest` (qml/dev/AtramentSelfTest.qml) runs the outline and keys
  parts of `selftest` against the page drawn by Atrament (`--atrament-editor`, `DevTools.atramentEditor`: the
  device's setting is never written) with real input — keys and typed letters through the window, an input
  method's composition to the AtramentView, clicks and taps on its rows (`AtramentView.rowTop` /
  `rowHeight` say where they are) — and checks the model, the field (`editRow`, `editText`,
  `editCursor`), what an input method is told and the suggestion popup — then document mode's
  keys and its toggle, the zoom's breadcrumb (a hosted QML item) clicked, reading mode's taps and
  toast, "/" commands setting a task's status, the gestures on chips, tags, links and task boxes
  (`AtramentView.linkPoint` / `taskBoxPoint` say where they are), sources taking "[[" as text,
  spell checking's correction, a task box's status menu and Ctrl+Shift+Enter's, the edited text's
  menu (a right-click's items with real clicks: copy, cut, paste, select all, a highlight, a
  misspelled word's correction; on a phone the input method's selection and the floating
  toolbar's shortcuts) and the menus of chips and links inside an embedded block and a query
  result: the QML around the view and the view tested together. It found a composition left
  marked by Backspace, a zoom's end leaving the edited block out of view, another page opening at
  the last one's scroll, a tap in reading mode saying nothing, a right-click on a chip opening the
  block menu (and a finger held on a tag the selection mode), a revealed row left short of the view
  past estimated heights, `BlockEditor.ensureRowVisible` doing nothing for the Atrament page,
  suggestions opening in code, a task box with no status menu.
  `tst_editor_qml_atrament_table[_touch]` (qml/dev/AtramentTableSelfTest.qml) does the same with
  `tabletest`'s table steps (step 5's tables entry).

### Open questions

* ~~Vim mode's block cursor and visual selection~~: decided (2026-11-27) — Vim stays with the
  editor drawn by Atrament, not on the QML editor until M3: the session is the same object and its
  logic is all C++ (the engine, VimPageBuffer over PageController), so what the view adds is small —
  `TextInput`'s caret shapes (a generic option: bar, block, underline) and read-only state, the
  field's own selection for the visual one, the keys routed by BlockEditor's rules (step 2's entry,
  `pagevim.cpp`). Built.
* ~~The find highlight~~: spans with a background over `text::findMatches` of the display text
  (and of the edited text), built.
* ~~Spell marks~~ as wavy underlines mapped through the display ↔ source map: built (step 2's
  entry, `pagespell.cpp`).

## The views in Atrament (M3)

The design of M3's first part: the Tasks, Search and Calendar views drawn by Atrament inside the
QML shell, behind a setting, the way M2 moved the editor. Their models are C++ already and stay; what
moves is the drawing of their rows (the bulk of a view's items, memory and frame time) first, then
the chrome around them, until the view's route shows no QML item ("No QML in the default journey").

### What stays

* The models and their tests: `TaskListModel` (src/app/planning/tasklistmodel.h: sections, filters
  through the query engine, subtasks, incremental updates), `SearchResultModel`
  (src/app/content/searchmodel.h) and `CalendarModel` (src/app/planning/calendarmodel.h), with
  `tests/app/planning` and `tests/app/content` over them.
* The views' actions in QML until their chrome moves: TasksView's toggle with its undo toast, the
  token menus (PlanningTokenMenu), the dialogs (date, query, time), opening a page at a line, the
  route and its parameters, the shell's keys. The Atrament list asks for them; the QML view does them
  — the same functions its QML delegates call today.

### Hosting and the setting

* `AppSettings.viewsAtrament` ("Draw the task list with Atrament" while only Tasks is drawn —
  Search and Calendar join it under the same switch —, Settings → Editor, next to the editor's),
  off by default;
  `--atrament-views` (`DevTools.atramentViews`) turns it on for one run without writing the
  device's setting, as `--atrament-editor` does.
* `AtramentView { view: "tasks"; viewModel: taskModel }` (likewise `"search"`, `"calendar"`): the
  same QML element as the editor's, with one QObject property for the view's model and one signal,
  `viewAction(action: string, args: var)`, for everything a row asks of its view (`"toggle"`,
  `"open"`, `"tokenMenu"`, `"link"`, `"status"`, `"fold"` …) — one decision in one place, and the
  editor's 101 properties not grown further. Where a row's parts are (a self-test's
  click) comes from `viewPartRect(row, part)`; menus open at the click's point, in the actions' `x`, `y`.
* Each view reads a row's roles once into a typed struct (`TasksView::RowData`, from the roles the
  QML delegates read: one source of truth, the model untouched) and draws from it — only the rows
  in view are read. Model signals (rows inserted, moved, removed, data changed) update a measured
  `list()`'s heights (`ModelRows`), each row's element keyed by the task's `key` (path:line), so
  the anchor row stays put when rows above it change (as the journal stream's days do).

### Rows

* **Tasks** (step 1): a section header (label, count, the fold chevron) and a task row — the
  checkbox (the editor's task box: status shapes, a click toggles, a right click or long press its
  status menu), the text as the editor's rendered spans (`renderedspans.h`: links and tags
  clickable, a link's menu, the 4-line "…"), the chips (`pagechips.cpp`'s date, reminder,
  recurrence and priority chips, their menus), where it is written, the subtasks' fold and
  progress, "Waits for …", the clock. Keys: Up/Down, Space, Enter, the Menu key in the view's focus
  handle; the filter bar, the quick-add field and the empty state stay QML around the list at
  first.
* **Search** (step 2): a result row — the kind's icon, the title and the excerpt with the matched
  words highlighted (ranges from the model, spans with a background, as the find highlight), the
  folder or the PDF's page / the document's part, "Close in meaning"; the recent searches when the
  field is empty. The field is an Atrament `TextInput` from the start (its IME path is M1's), Up/Down
  and Enter move and open in the list, Escape clears.
* **Calendar** (step 3): the agenda first (day headers and entries: the tasks' rows' parts again),
  then the month grid (7 × 6 cells: the day's number, its entries as chips, today and the selected
  day marked, a week number column) and the week grid (hour rows, events placed by their times), the
  day panel's list last.

### Tests

* `tests/atrament/views/tst_atrament_tasks` (QtTest, the test platform, a `VaultIndex` over a temp
  notebook as `tests/app/planning` builds it): sections and counts drawn, a click on the checkbox
  asking for "toggle" with the task's path and line, Enter and a click opening, a section folded,
  rows moving when the model changes with the anchor kept, the chips' menus, the accessibility tree
  (a list, its items, checkboxes with their state). Likewise `tst_atrament_search` (step 2) and
  `tst_atrament_calendar` as its step lands.
* The in-app self-test `tasks-atrament-selftest` (qml/dev/TasksAtramentSelfTest.qml, ctest
  `tst_views_qml_atrament`; Search's `search-atrament-selftest`, qml/dev/SearchAtramentSelfTest.qml,
  ctest `tst_views_qml_atrament_search`; Calendar's as it lands): TasksView, SearchView and CalendarView with
  `--atrament-views`, real clicks and keys on the rows (`viewPartRect`), the toggle's toast and undo,
  a token menu, a query typed and a result opened — the QML around the view and the view together.
* A gate measured as M2's: a 1 000-task view's memory per row, a scroll step's scene and CPU-renderer
  time at 1280 × 800, the time from the route to its first frame, against the QML view
  (`tests/bench/bench_atrament_views`).

### Steps

1. **Tasks** (2026-11-28; `src/app/views/atrament/tasksview.h`, `qml/planning/TasksAtramentList.qml`,
   `AppSettings.viewsAtrament`, `--atrament-views`; docs/UI.md "The task list drawn by Atrament"). TasksView's rows are Atrament's under the
   ListView's header (the quick-add field, the filters, the summary stay QML above them; the
   ListView keeps no rows): a section's header with its count and chevron folds it; a task its box
   (a click toggles it — TasksView's toggle with its undo toast —, a right-click its status menu,
   TaskStatusMenu made by TasksView), its text as the model's rich text in rendered spans (links and
   #tags clickable, a link's menu, at most four lines), its chips (the due date — "…, overdue" in
   red —, scheduled, "from …", the next reminder, the recurrence, "Waits for: …", progress or the
   subtasks' count, the clocked time, the priority; a right-click on a date, reminder, recurrence or
   priority chip opens its token's menu, PlanningTokenMenu) and where it is written, a subtask
   indented by its depth. Keys: ↓ / ↑ through the tasks (headers skipped), Space toggles, Enter
   opens. For screen readers: the list, a header a heading (expanded or not), a task's box a
   checkbox named by its text. The model's changes keep the list's anchor on its row. Tested by
   `tests/atrament/views/tst_atrament_tasks` (sections and chips drawn, every click's request, the
   menus at their point, the keys, rows following a filter) and the app's self-test
   `tst_views_qml_atrament` (qml/dev/TasksAtramentSelfTest.qml: real clicks and keys through the
   window, the file written). The subtasks' chip folds them ("fold"). The current task gone from
   the list (ticked off into the folded Done, a filter) leaves the task now at its place current,
   as a ListView's currentIndex stays (`nearestTask`). `TasksLook::fromMap` reads
   AtramentView.viewLook; `ModelRows` (views/atrament/modelrows.h) keeps a list's heights with a
   model's rows. **Not yet**: the "Add date" chip (QuickDateMenu), a tag's page with Ctrl+click, the
   dependency problem and "Blocks n tasks" chips, a done task's text struck through, the header
   items drawn by Atrament (they stay QML above the list), touch's long press for the menus, the
   gate's measurements.
2. **Search** (2026-11-28; `src/app/views/atrament/searchview.h`, `qml/content/SearchAtramentList.qml`,
   the same `viewsAtrament`; docs/UI.md "The task list drawn by Atrament"). SearchView's results are
   Atrament's in its ListView's place (the ListView stays, without rows, for the empty state): a
   result its kind's icon, its title (a daily note's date in words, `formatDate` from DatesHelper)
   and its excerpt (two lines at most) with the model's matched characters in the accent and bold
   (one span per matched word; on the current row the text's colour, as the delegate), its folder
   (not a daily note's), "Close in meaning", an attachment's place (`pageInfo`: a PDF's page and how
   many more pages match, a slide, a part, "Found by its name") and "Recognized text". A click opens
   it (`viewAction("open", {row, modifiers})`: OpenIn.clickTarget, Ctrl a new tab), a middle click in
   a new tab. The field keeps the keyboard: ↓ / ↑ call `viewRevealRow` (SearchView.currentResult
   replaces the ListView's currentIndex), the current result highlighted and scrolled into view
   (the least scroll), Enter opens it. For screen readers the list's items are the results, read as
   their texts. A new look (zoom, theme) measures the rows again, the row at the view's top staying
   there. `views/atrament/partrecorder.h` (the parts a frame drew, `viewPartRect`) and
   `lookmap.h` (a look from AtramentView.viewLook by its fields' names) are shared with Tasks;
   AtramentView's views are one template (`EntityViews`). Tested by
   `tests/atrament/views/tst_atrament_search` (rows, a11y texts, the matches' glyphs in the accent, a
   journal's heading, every place's words, clicks and middle clicks, the current row revealed, the
   model's changes followed, a restyle) and the self-test `tst_views_qml_atrament_search`
   (qml/dev/SearchAtramentSelfTest.qml: a query typed, ↓ in the field, a click opening a result).
   **Not yet**: the field as an Atrament `TextInput` and the recent searches drawn by Atrament (they
   stay QML), a pressed row's background (the delegate's `down`), the gate's measurements.

### After the three views

The order after Tasks, Search and Calendar, each behind `viewsAtrament` until it passes its own
self-test, by how much of the default journey it covers and how much it reuses:

1. **Code mode's file view and diff** (`CodeFileView.qml`, `GitDiffView.qml`): read-only code with
   line numbers and the highlighter's colours — the editor's code rows (`pagecode.cpp`,
   `EditStyler`) over a measured list of lines, so a file of 50 000 lines lays out what is shown;
   a diff's hunks as rows (added / removed tints, the gutter's two line numbers, word-level marks);
   find in the file as the editor's find spans. The terminal is drawn already ("M3" in the roadmap).
2. **The chat** (`ChatView.qml` and its segments): the messages as a measured list in reverse
   (anchored at the end while a reply streams in, the anchor kept when one scrolls up), a message's
   Markdown as the editor's rendered text and its blocks as the page's row kinds (code, tables,
   math), tool calls and their results as cards with a fold, the composer an Atrament `TextInput`
   (multiline, its chips for @-mentions and attachments). Streaming appends to the last row only:
   one row measured again per token batch, not the list.
3. **Settings** (`SettingsView.qml`): a form of sections — rows with a label, a description and a
   control: Atrament's own switch, segmented choice and slider (new elements: `switchControl`,
   `segmented`, `slider`, each with its accessibility role and keys), text fields as `TextInput`,
   the pickers (a folder, a colour, a font) still QML dialogs opened from a row. The search over
   settings filters the rows.
4. **The shell** (sidebar, bottom bar, route header, toasts, the command palette): last, once no
   view in the default journey is QML, so the window's root becomes an Atrament view hosting the
   QML views that remain (the reverse of today's hosting: `QQuickItem`s placed by Atrament, as
   `ForeignRowHost` does for rows).

M4 then replaces the Qt host under that root view, platform by platform: Wayland first (the
desktop the project is built on: xdg-shell, text-input-v3, fractional scale, the cursor shape and
clipboard protocols), then Android (GameActivity, our `InputConnection` in Kotlin, AccessibilityNodeProvider
from the tree), X11 and Win32, the web last (a canvas with WebGPU / WebGL2, a hidden textarea for
the IME) — each starting with the gallery and the test suite's golden frames on that platform.

## Risks

* **Input methods on Android** are the hardest single part (composition regions, autocorrect,
  gesture typing, selection handles, the magnifier). Hosted mode keeps Qt's implementation until
  M4; M4 does not ship on Android until Gboard and SwiftKey pass the same checklist.
* **Accessibility** has to work from the first view that ships (M2), not later.
* **WebGPU** is not in every browser (Firefox on Linux and Android only in test builds as of
  2026), hence the WebGL2 renderer of the same scene.
* **Breadth**: 334 QML files are a lot of UI. The hosted mode is what makes the migration
  survivable: every view moved ships on its own.
