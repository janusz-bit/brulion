// Math formulas of web pages (docs/CLIPPER.md "Math formulas"): KaTeX, MathJax 2 and 3, Wikipedia's
// `<math>` and plain MathML found in HTML and turned into the TeX of `$…$` / `$$…$$`. Internal to
// the io module; htmlToMarkdown() uses it when HtmlToMarkdownOptions::mathFormulas is set.
#pragma once

#include <QList>
#include <QString>

namespace brulion::io::detail {

/// One formula found in a page.
struct MathFormula {
    QString tex;          ///< the TeX source, on one line, without the delimiters
    bool display = false; ///< `$$…$$`, a block of its own
};

/// A slot in the HTML that extractMath() leaves where a formula was: MathSlotOpen, the formula's
/// index in decimal and MathSlotClose (private-use characters, removed from the input first).
constexpr char16_t MathSlotOpen = 0xE0F4;
constexpr char16_t MathSlotClose = 0xE0F5;

struct MathExtracted {
    QString html;                ///< `html` with each formula replaced by its slot, its rendering dropped
    QList<MathFormula> formulas; ///< what the slots stand for
};

/// The formulas of `html`, replaced by slots (a display formula in a `<div>` of its own). What is
/// recognised, as the elements it leaves are matched by their end tags, not by a DOM:
///   - KaTeX: `.katex-display` / `.katex` with the `<annotation encoding="application/x-tex">`
///     of its MathML; its `.katex-html` copy is dropped with the rest of the container;
///   - MathJax 3: `<mjx-container>` with its assistive MathML (an annotation's TeX, else the MathML
///     converted), else a `data-tex` / `data-latex` attribute; MathJax 2: `<script type="math/tex">`
///     (`; mode=display`), `<script type="math/mml">`, their rendered `MathJax_*` / `MathJax`
///     spans dropped;
///   - Wikipedia: `.mwe-math-element` with its `<math>` (annotation, else converted) or the TeX
///     in the `alt` of its fallback picture;
///   - any `<math>`: the annotation's TeX, else the MathML converted to TeX (mi, mn, mo, mtext,
///     mfrac, msqrt, mroot, msup, msub, msubsup, munder, mover, munderover, mfenced, mtable…),
///     else its `alttext`, else its text;
///   - Pandoc's and Sphinx's `<span class="math">\(x\)</span>`.
/// Formulas inside `<pre>` and `<code>` stay as they are. Linear time; at most 10000 formulas.
MathExtracted extractMath(const QString &html);

/// The TeX of a MathML element (`<math>…</math>` as written in HTML; entities decoded), "" when
/// it holds nothing. Used by extractMath(); public to the module for tests.
QString mathmlToTex(const QString &mathml);

} // namespace brulion::io::detail
