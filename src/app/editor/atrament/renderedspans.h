// A block's rendered text as Atrament text spans (docs/ATRAMENT.md "The block editor in Atrament
// (M2)", "What stays"): markdown::RenderedText's flattened runs — bold, italic, strike, code, a
// link, a chip, a tag, a highlight — become TextSpans over the same UTF-16 text (QString and
// Atrament both count UTF-16 code units, so the ranges pass as they are). Colours come from the
// run itself (a CSS colour the renderer wrote) or from the palette by the run's role.
//
// Tinted runs get the QML editor's shapes (RichTextPills, BTintShapes): a chip or a tag a pill, inline
// code and a highlight a box of `boxRadius`, around the glyphs — the font's ascent above and descent
// below the baseline, as tall as the run's text whatever the line's height —, the adjacent runs of
// one tinted element (`tintElement`) one shape. A page's app icon (RenderedObject::Icon over its
// no-break spaces) is an inline object of their room: BIcon's icon drawn there as Atrament vector
// paths (pageicons.h) in the link's colour — a rounded square for a name this build has no icon of.
#pragma once

#include "markdown/renderedtext.h"
#include "ui/text.h"

#include <QString>

#include <string>
#include <vector>

namespace brulion::editor::atr {

// What the page is drawn with (BlockEditor's Theme colours and fonts).
struct PagePalette {
    atrament::Color text = atrament::rgb(0x1f2328);
    atrament::Color muted = atrament::rgb(0x6e7781);
    atrament::Color link = atrament::rgb(0x3b5bdb);
    atrament::Color chip = atrament::rgb(0x5f3dc4);
    atrament::Color tag = atrament::rgb(0x2b8a3e);
    atrament::Color code = atrament::rgb(0x1f2328);
    atrament::Color codeBackground = atrament::rgba(0x8c959f26);
    atrament::Color highlightBackground = atrament::rgba(0xffd43b66);
    atrament::Color chipBackground = atrament::rgba(0x7048e81f);
    atrament::Color tagBackground = atrament::rgba(0x2b8a3e1f);
    float boxRadius = 4; // Theme.radiusXs: inline code's and highlights' corners
    atrament::Color guide = atrament::rgba(0x8c959f59);
    atrament::Color background = atrament::rgb(0xffffff);
    std::u16string bodyFamily = u"Brulion Inter";
    std::u16string codeFamily = u"Brulion JetBrains Mono";
};

// A CSS colour as Atrament's (QColor's syntax: "#rrggbb", "#aarrggbb", names, "rgba(…)");
// `fallback` when it is empty or not a colour.
atrament::Color cssColor(const QString &css, atrament::Color fallback);

// The spans of `text`'s runs, in order (a run with the default look gives none), then one inline
// object per page icon.
std::vector<atrament::TextSpan> renderedSpans(const markdown::RenderedText &text, const PagePalette &palette);

// A page icon's room, in ems of the text's size: the three no-break spaces of the body font it
// replaces (Inter's), its middle where the QML editor centres its icon (RichTextPills' "icon").
constexpr atrament::InlineObjectSize kPageIconSize{0.84f, 0.784f, 0.056f, true};

// The text as Atrament takes it (a view of the QString's UTF-16 data).
inline std::u16string_view u16View(const QString &s)
{
    return {reinterpret_cast<const char16_t *>(s.utf16()), std::size_t(s.size())};
}

} // namespace brulion::editor::atr
