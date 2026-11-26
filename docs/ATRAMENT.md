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
| D10 | Input: hitboxes registered in prepaint, listeners in paint, dispatched in reverse paint order with a capture and a bubble phase; focus handles; actions and keymaps in key contexts; an `InputHandler` interface for input methods modelled on NSTextInputClient / Android `InputConnection` | GPUI's proven model; the IME interface maps 1:1 onto every platform's. |
| D11 | Execution: one main thread, a platform dispatcher, timers; a test platform with a virtual clock and deterministic ordering | Timing-free tests; no flaky waits. |
| D12 | First host: `AtramentItem`, a `QQuickItem` that draws a window's scene with the QRhi GPU renderer in a `QSGRenderNode`, inside Qt Quick's own render pass (the CPU renderer into a texture where Qt Quick has no QRhi, the GPU lacks OpenGL ES 3, or `BRULION_ATRAMENT_RENDERER=cpu`), and translates Qt's events and input-method queries | Views move one by one; Qt keeps providing windows, input methods and accessibility until our own platform layers replace it. |
| D13 | Accessibility: elements describe themselves (role, name, value, state, actions, bounds) into a semantic tree each frame; hosted, a bridge to `QAccessibleInterface`; standalone, AccessKit's C API | Accessibility from the first view that ships, on every platform. |
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
* Shaders: Vulkan GLSL 440 in `src/atrament/qt/shaders/`, compiled by `qt_add_shaders`. Measured
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
* **Strings**: `std::u16string` / `std::u16string_view`, offsets in UTF-16 code units — what
  `QString` and Brulion's core use, so text from the core passes without conversion.
* **Hosting in the Qt app**: `auto *item = new AtramentItem(parent); item->attach(app, view)` — the
  item is a `QQuickItem` like any other; one `App` serves every item of the process. In the app's
  QML, `AtramentView { view: "gallery" }` (src/app/ui/atramentview.h) shows a named view of the
  process's App; `brulion --atrament` opens it in a window of the design system
  (qml/dev/AtramentDemo.qml).

## Code

* `src/atrament/<area>/` — `base`, `scene`, `raster`, `text`, `layout`, `ui`, each a static
  library `brulion_atrament_<area>`; `src/atrament/qt` — the Qt Quick host and the QRhi renderer
  (`brulion_atrament_qt`); `src/atrament/gallery` — a developer app showing every element
  (`atrament-gallery`; `--screenshot <file> [--renderer gpu]` renders a frame headless with
  either renderer and prints their times, `--grab <file>` saves what a Qt Quick window drew).
* Namespace `brulion::atrament`. No Qt, no exceptions thrown, no RTTI needed outside the host.
  Headers relative to `src/atrament` (`#include "text/textlayout.h"`).
* Tests: `tests/atrament/<area>/tst_atrament_<area>*.cpp` (QtTest is only the runner; golden
  images go through `QImage` in the tests, never in the framework).
* External dependency: HarfBuzz (pkg-config `harfbuzz`; vendored for the Android, Windows and
  browser builds when Atrament ships there). CMake option `BRULION_ATRAMENT` (default ON where
  HarfBuzz is found).

## Testing

* `TestPlatform` / `TestWindow`: a window without a screen — size, scale, a virtual clock, input
  simulated in process (`simulateClick`, `simulateKeys(u"ctrl-a backspace")`, `simulateText`,
  `simulateComposition`), a frame rendered by `raster` to an RGBA image.
* Golden images compared with a tolerance of 2 per channel (fused multiply-add differs between
  processors), regenerated with `BRULION_ATRAMENT_UPDATE_GOLDENS=1`.
* Layout tests: the flex algorithm against hand-computed CSS results; text tests: Polish
  diacritics, combining marks, emoji, wrapping at a width, carets and hit tests round-tripping.
* The GPU renderer is compared with the CPU renderer on every QRhi backend that can be made
  headless (`tests/atrament/qt`: Vulkan from the loader under any platform plugin, OpenGL with a
  display; skipped with neither), and `AtramentItem` is drawn inside Qt Quick through a
  `QQuickRenderControl` on that QRhi.

## Roadmap

Each milestone ends with something running and measured; the gates decide whether to go on.

| Milestone | Content | Gate |
|---|---|---|
| **M0 Foundation** | base, scene, CPU raster, text (fonts, shaping, glyph raster, atlas, paragraph layout, hit tests), flex layout, ui (App, entities, window, div/text/canvas, events, hover, click, scroll, focus), test platform, Qt Quick host, gallery | The gallery runs in Brulion's Qt; every area has tests; a frame of the gallery in < 4 ms of CPU raster at 1280×800 (-O2). |
| **M1 Editing** | text input element with selection, clipboard, undo hooks, the `InputHandler` through Qt (desktop + Android IME), virtualized lists (uniform and measured), images, keymaps, accessibility bridge | Polish text typed with Gboard (composition, autocorrect, gesture typing) and with a Linux IME into an Atrament field, TalkBack reads it. |
| **M2 First Brulion view** | the journal / page block editor in Atrament inside the QML shell, behind a setting; the QRhi GPU renderer (in place: every primitive kind, within 2 levels of the CPU renderer) | Memory per block < 2 KB (today 100–200 KB), scrolling a 1 000-block page at 120 fps on the desktop, the editor's existing behaviour tests pass against it. |
| **M3 Views** | calendar, tasks, search, settings, Code mode (editor, terminal), the chat; the shell | No QML in the default journey. |
| **M4 Own platforms** | Wayland (xdg-shell, layer-shell, text-input-v3, fractional scale), X11, Win32, Android (GameActivity + our own `InputConnection` in Kotlin), web (canvas + WebGPU / WebGL2, hidden textarea for the IME); the WebGPU renderer (Dawn) | The app runs with no QtGui/QtQuick loaded on Linux and Android. |
| **M5 Qt Quick out** | QtGui, Qt Quick, QML, Controls, Svg, Pdf leave the build (PDF through PDFium/poppler into a picture) | APK < 20 MB, start-up memory < 60 MB, first browser visit < 6 MB. |

The targets in M5 are estimates of what the change can reach, not measurements.

## Risks

* **Input methods on Android** are the hardest single part (composition regions, autocorrect,
  gesture typing, selection handles, the magnifier). Hosted mode keeps Qt's implementation until
  M4; M4 does not ship on Android until Gboard and SwiftKey pass the same checklist.
* **Accessibility** has to work from the first view that ships (M2), not later.
* **WebGPU** is not in every browser (Firefox on Linux and Android only in test builds as of
  2026), hence the WebGL2 renderer of the same scene.
* **Breadth**: 334 QML files are a lot of UI. The hosted mode is what makes the migration
  survivable: every view moved ships on its own.
