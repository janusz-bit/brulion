// Inline expressions — code spans whose text starts with `=`: an inline calculation
// (`` `= cena * 2` ``, docs/SYNTAX.md §9 "Inline calculations") or, when the calculator has no
// value for it, an inline Dataview query (`` `= this.file.name` ``, §10 "Inline queries"). The
// editor renders them through RenderContext::inlineCalc / inlineQuery (inlinerenderer.h); the
// documents made from a note (exports) put their values in place of them with these functions.
// Part of the markdown module — see markdown/markdown.h.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

#include <functional>
#include <optional>

namespace brulion::markdown {

/// One inline expression of a note.
struct InlineExpression {
    qsizetype from = 0; ///< the code span, backticks included: [from, to) in the note's text
    qsizetype to = 0;
    int line = 0;       ///< its line (0-based)
    QString expression; ///< the code after the `=`, trimmed ("cena * 2")
};

/// The inline expressions of a whole note — code spans on one line whose text starts with `=`
/// (blanks before it allowed) followed by an expression — outside fenced code and comments.
QList<InlineExpression> findInlineExpressions(QStringView content);

/// `content` with each inline expression findInlineExpressions() finds replaced by `value(e)`
/// (Markdown) — or kept as written when it gives nullopt. Everything else stays byte for byte.
QString replaceInlineExpressions(QStringView content,
                                 const std::function<std::optional<QString>(const InlineExpression &)> &value);

/// Plain text as Markdown that shows it as written: Markdown's punctuation escaped with a
/// backslash (`\*`, `\[`, `\$` …), line breaks as blanks.
QString escapedMarkdownText(QStringView text);

} // namespace brulion::markdown
