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

    // What styling the lines found besides their styles (styleMarkdownLine()'s LineStyles), in
    // positions of the whole text: each line's prose for spell checking and its reminder chips
    // (with `chips`: StyleOptions::reminderChips in that locale, relative to that day).
    struct Extras {
        struct Prose {
            int lineStart = 0; // the line's offset in the text
            int lineLength = 0;
            SpellRange range;  // in the line
        };
        bool chips = false;
        dates::UiLocale chipLocale = dates::UiLocale::Polish;
        QDate chipToday;
        std::vector<Prose> prose; // out
        std::vector<LineChip> chipsFound; // out: from / to in the text
    };
    // The spans of `text` (its lines styled one after another, each from the state the one
    // before left, as a QTextDocument's blocks are) in `mode` (Inline: a block's body; Code: a
    // code block's content, Luau with `luau`); `extras` (optional) is filled as it says.
    std::vector<atrament::TextSpan> spans(std::u16string_view text, MarkdownMode mode, bool luau = false,
                                          Extras *extras = nullptr) const;
    // A TextInput styler over a shared copy of this one.
    atrament::TextInput::Styler styler(MarkdownMode mode, bool luau = false) const;

    // The reminder chips' colours (MarkdownHighlighter::refreshChips: reminderBackground /
    // alarmBackground for the pill, reminderChip / alarmChip — else reminder / alarm — for its
    // words).
    atrament::Color chipBackground(bool alarm) const { return alarm ? m_alarmBackground : m_reminderBackground; }
    atrament::Color chipText(bool alarm) const { return alarm ? m_alarmText : m_reminderText; }

private:
    std::array<std::optional<atrament::Color>, 32> m_colors {};
    std::array<std::optional<atrament::Color>, 8> m_backgrounds {};
    std::u16string m_codeFamily;
    float m_boxRadius = 3;
    atrament::Color m_reminderBackground;
    atrament::Color m_alarmBackground;
    atrament::Color m_reminderText;
    atrament::Color m_alarmText;
};

} // namespace brulion::editor::atr
