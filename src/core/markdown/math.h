// Wzory matematyczne (docs/SYNTAX.md §11 "Wzory matematyczne"): where a note writes LaTeX math
// and how a formula is turned into the Qt rich text of the editor. Part of the markdown module.
//
// The dialect (Obsidian-like): inline `$…$` / `\(…\)`, display `$$…$$` / `\[…\]` and fenced
// ```math blocks (the block view lives in the app). A formula is rendered by renderMath() — a
// KaTeX-like subset of LaTeX turned into Unicode symbols and rich text (<i>, <sup>, <sub>,
// overlined radicals): Greek letters, operators, ^ / _, \frac, \sqrt, \text, matrices… What is
// not understood stays as written. Formulas are display-only: the file keeps the LaTeX source.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::markdown {

/// A math span of inline text: `$…$`, `$$…$$`, `\(…\)` or `\[…\]`. Positions are UTF-16 indices
/// into the scanned text: [from, to) covers the delimiters, [contentFrom, contentTo) the LaTeX
/// between them. `display` is set for `$$…$$` and `\[…\]`.
struct MathSpan {
    qsizetype from = 0;
    qsizetype to = 0;
    qsizetype contentFrom = 0;
    qsizetype contentTo = 0;
    bool display = false;

    friend bool operator==(const MathSpan &, const MathSpan &) = default;
};

/// All math spans of inline text, in order, by the rules of the dialect. Text that only looks
/// like math is not a span: `$5 i $10` (a space follows the opening `$`; the closing `$` is
/// followed by a digit), `\$100` (escaped), unclosed `$x`. Display spans may hold line breaks.
/// Expects text with inline code and comments masked (InlineRenderer passes its masked text);
/// findMathSpans() itself does not look for code spans.
QList<MathSpan> findMathSpans(QStringView text);

/// The LaTeX `latex` of a formula as a Qt rich-text fragment (`Text { textFormat: RichText }`):
/// Unicode symbols, ^ / _ as <sup> / <sub>, \frac as a fraction, \sqrt with an overlined
/// radicand, \text as it is written. The formula inherits the font and colour around it.
/// Security: everything that is not recognised is escaped — only tags built here are emitted.
/// An empty result means "nothing to show" (empty formula).
QString renderMath(QStringView latex, bool display = false);

} // namespace brulion::markdown
