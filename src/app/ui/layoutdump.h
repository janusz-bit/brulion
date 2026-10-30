// LayoutDump — what a window shows, for the UI benchmark (docs/UI.md "UI benchmark (Brulion vs
// Obsidian)"): every visible text with its colour, font and painted rectangle, every interactive
// target with its hit area, every filled box, in window coordinates (logical px), JSON schema 1 —
// the same schema the Obsidian driver writes from the DOM (tests/app/uibench/obsidian-shots.mjs).
// Qt Quick public API only (class names through QObject::inherits, properties through
// QObject::property).
//
// Schema 1 (rectangles are [x, y, w, h] in logical px; colours "#rrggbbaa"):
//   {schema: 1, app: "brulion", viewport: {w, h, dpr},
//    texts:   [{type, name, text, chars, rect, color, alpha, px, weight, family, lines, enabled,
//                placeholder, occluded, paint, fragments: [{text, chars, color, bg?}]}],
//    targets: [{type, name, kind: button|mouse|tap|field, label, rect, enabled, inline, occluded}],
//    boxes:   [{type, rect, color, alpha, border, borderColor, radius, paint}],
//    tokens:  {fg, fgMuted, fgFaint, bg, bgElevated, bgSunken, accent, link}}
// `text` is the first 80 characters with whitespace collapsed, `chars` the length of the whole
// collapsed text; `alpha` the accumulated opacity of the item and its ancestors (the colour keeps
// its own); `paint` the paint order (children by z after their parent, negative z before it). A
// text's rectangle is its visible part, a target's its whole hit area (when any of it is visible;
// its label: text, label, title, Accessible.name or the first text inside). A text or target is
// `occluded` when an opaque box painted later (alpha ≥ 0.9, or ≥ 0.3 for a popup's dim in the
// overlay) covers its centre.
#pragma once

#include <QJsonObject>
#include <QString>

class QObject;
class QQuickWindow;

namespace brulion::ui {

/// The dump of `window` now. `theme`: the Theme singleton (its colour tokens go into "tokens").
QJsonObject layoutDump(QQuickWindow *window, const QObject *theme = nullptr);
/// layoutDump() written to `path` (UTF-8, indented); false with `error` set when it cannot be.
bool writeLayoutDump(QQuickWindow *window, const QString &path, const QObject *theme, QString *error);

} // namespace brulion::ui
