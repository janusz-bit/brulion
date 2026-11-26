// A block's rendered text as Atrament text spans (docs/ATRAMENT.md "The block editor in Atrament
// (M2)", "What stays"): markdown::RenderedText's flattened runs — bold, italic, strike, code, a
// link, a chip, a tag, a highlight — become TextSpans over the same UTF-16 text (QString and
// Atrament both count UTF-16 code units, so the ranges pass as they are). Colours come from the
// run itself (a CSS colour the renderer wrote) or from the palette by the run's role.
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
    atrament::Color guide = atrament::rgba(0x8c959f59);
    atrament::Color background = atrament::rgb(0xffffff);
    std::u16string bodyFamily = u"Brulion Inter";
    std::u16string codeFamily = u"Brulion JetBrains Mono";
};

// A CSS colour as Atrament's (QColor's syntax: "#rrggbb", "#aarrggbb", names, "rgba(…)");
// `fallback` when it is empty or not a colour.
atrament::Color cssColor(const QString &css, atrament::Color fallback);

// The spans of `text`'s runs, in order (a run with the default look gives none).
std::vector<atrament::TextSpan> renderedSpans(const markdown::RenderedText &text, const PagePalette &palette);

// The text as Atrament takes it (a view of the QString's UTF-16 data).
inline std::u16string_view u16View(const QString &s)
{
    return {reinterpret_cast<const char16_t *>(s.utf16()), std::size_t(s.size())};
}

} // namespace brulion::editor::atr
