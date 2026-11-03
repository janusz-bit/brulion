// A diagram's Scene as SVG text (the HTML export: io/htmlrender.cpp). Safe whatever the note
// held: the only elements are <svg>, <title>, <style>, <path>, <rect> and <text>; every text is
// XML-escaped (characters XML forbids left out), colours are validated "#rrggbb" (an alpha as
// fill-/stroke-opacity), numbers are ours. Never scripts, <foreignObject>, event attributes,
// links or other external references.
//
//   <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 W H" width="W" height="H" role="img"
//        font-family="…" class="brulion-diagram"><title>…</title>
//   [<style>@media (prefers-color-scheme:dark){.bmd-f3{fill:#…}.bmd-s4{stroke:#…}…}</style>]
//   <path d="M… C… Z" fill="#…" stroke="#…" stroke-width="1.2" class="bmd-f3 bmd-s4"/> …
//   <text x="…" y="…" font-size="14" text-anchor="middle" dominant-baseline="central" …>…</text>
//   </svg>
//
// The attributes hold `light`'s colours; with `dark`, items painted in a role carry classes and
// the style sheet holds one `@media (prefers-color-scheme: dark)` block of `dark`'s colours, so a
// browser follows its colour scheme (the export's own CSS does the same). The class names are
// fixed (`bmd-f<role>`, `bmd-s<role>`): several diagrams in one page share the same rules.
#pragma once

#include "diagram/diagram.h"

namespace brulion::diagram {

struct SvgOptions {
    /// The CSS font-family of the text (a constant of the caller's, escaped all the same).
    QString fontFamily = QStringLiteral("system-ui, -apple-system, 'Segoe UI', Roboto, Ubuntu, sans-serif");
    /// The <title> when the scene has none.
    QString fallbackTitle = QStringLiteral("Diagram");
};

QString toSvg(const Scene &scene, const Palette &light, const Palette *dark = nullptr, const SvgOptions &options = {});

} // namespace brulion::diagram
