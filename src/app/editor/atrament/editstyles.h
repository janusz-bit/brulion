// The edited block's Markdown coloured as the QML editor colours it (docs/ATRAMENT.md "A styled
// TextInput", "The highlighter as a pure function"): the rules of markdownstyles.h
// (styleMarkdownLine(), the same MarkdownHighlighter applies to InlineEditor's text) flattened into
// runs of attributes (flattenStyles()) and turned into the TextSpans of the field's styler — the
// colour of each style's palette role, its background as a box around the glyphs (inline code, a
// highlight, a `<mark>`'s tint), bold / demibold, italic, strike, underline, the code face. The
// palette is BlockEditor's `highlighterPalette` (QVariantMap: "heading", "marker", "link", …,
// colours or colour strings), each role's key, its fallback key, then its built-in colour, as
// MarkdownHighlighter::paletteColor() resolves them.
//
//   EditStyler styler(palette, u"Brulion JetBrains Mono");      // resolved once per palette
//   page.setEditStyler(styler.styler(MarkdownMode::Inline), cx);
#pragma once

#include "markdownstyles.h"
#include "ui/textinput.h"

#include <QVariantMap>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace brulion::editor::atr {

class EditStyler {
public:
    EditStyler(const QVariantMap &palette, std::u16string codeFamily, float boxRadius = 3);

    // The spans of `text` (its lines styled one after another, each from the state the one
    // before left, as a QTextDocument's blocks are) in `mode` (Inline: a block's body; Code: a
    // code block's content, Luau with `luau`).
    std::vector<atrament::TextSpan> spans(std::u16string_view text, MarkdownMode mode, bool luau = false) const;
    // A TextInput styler over a shared copy of this one.
    atrament::TextInput::Styler styler(MarkdownMode mode, bool luau = false) const;

private:
    std::array<std::optional<atrament::Color>, 32> m_colors {};
    std::array<std::optional<atrament::Color>, 8> m_backgrounds {};
    std::u16string m_codeFamily;
    float m_boxRadius = 3;
};

} // namespace brulion::editor::atr
