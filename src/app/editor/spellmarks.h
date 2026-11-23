// Misspelled words underlined in a block's rendered text (docs/UI.md "Spell checking": "Underline
// in the whole page"). The words are found in the block's Markdown — spell::findWords() over its
// lines, exactly as in the edited block (SpellCheck::wrongWords) — and put on the rendered HTML
// through the source anchors of InlineRenderer's traced rendering (renderBlockTraced): a word's
// characters shown verbatim (plain text, emphasis, headings, quotes, callouts) are wrapped in a
// span with an underline in the spelling colour; a word inside a construct shown as a whole (a
// Markdown link's text, a chip) is left as it is — never misplaced.
//
//   const QString marked = markMisspelledHtml(traced.html, traced.anchors, body,
//                                             spell->wrongWords(body), QColor(u"#c0392b"_s));
#pragma once

#include "markdown/renderedtext.h"
#include "spell/spellwords.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::editor {

/// The style of a misspelled word's span in rendered text (`color`: the underline's).
QString spellMarkStyle(const QColor &color);

/// `html` — InlineRenderer's traced rendering of `source`, whose `anchors` map its pieces back —
/// with every verbatim piece of the words `wrong` (ranges of `source`) in a
/// `<span style="text-decoration:underline;text-decoration-color:…">`. Pieces shown as a whole
/// (constructs), anchors that do not fit `html` or `source`, and ranges outside them are left
/// alone; `html` unchanged when nothing is marked.
QString markMisspelledHtml(const QString &html, const QList<markdown::HtmlSourceAnchor> &anchors, QStringView source,
                           const QList<spell::WordSpan> &wrong, const QColor &color);

} // namespace brulion::editor
