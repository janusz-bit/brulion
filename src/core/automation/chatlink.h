// Prompt links (docs/UI.md "Automation", "AI chat"): brulion://chat?prompt=…&partner=…&page=… opens
// a new conversation of the AI chat with the prompt in its field — never sent: the user reads it
// and presses Send. A prompt comes from anywhere (a web page, a message, a QR code), so what it
// cannot show is taken out before it is shown: zero-width characters, bidirectional controls,
// Unicode tag characters (U+E0000…, which a model reads as ASCII though nobody sees them), the
// variation selectors that hide bytes after an emoji, and every other format character — and the
// view then says that the prompt had such characters, which may hide instructions. A long prompt
// is cut at MaxPromptLength. Pure; part of the automation module — see automation/automation.h.
//
//   const CleanedPrompt p = cleanPrompt(u"Podsumuj\u200B tę stronę"_s);   // {"Podsumuj tę stronę", 1}
//   chatLinkUri(u"Co mam jutro?"_s, u"acp:claude"_s)                       // brulion://chat?prompt=…
#pragma once

#include <QString>
#include <QStringView>

namespace brulion::automation {

/// The longest prompt a link puts into the field (characters, after the hidden ones are taken
/// out); the rest is left out.
inline constexpr qsizetype MaxPromptLength = 8000;

/// A prompt as a link gives it to the chat.
struct CleanedPrompt {
    QString text;   ///< without the hidden characters, line breaks as "\n", cut at the limit
    int hidden = 0; ///< how many hidden characters were taken out
    bool capped = false; ///< it was longer than the limit
};

/// `code` is a character a reader does not see but a model may read: a format character (Unicode
/// category Cf: zero-width space, joiners, word joiner, soft hyphen, bidirectional marks and
/// controls, the invisible operators, the byte order mark, interlinear annotations, tag
/// characters), a variation selector (U+FE00–U+FE0F, U+E0100–U+E01EF) or a Hangul filler. A
/// zero-width joiner between two emoji and one variation selector right after a visible character
/// stay (an emoji's own sequence: cleanPrompt()).
bool isHiddenCharacter(char32_t code);

/// `prompt` without its hidden characters (but a zero-width joiner between two emoji, and a
/// single U+FE0E/U+FE0F after a visible character), line and paragraph separators as "\n",
/// whitespace around it trimmed, cut at `limit` characters (never inside a surrogate pair).
CleanedPrompt cleanPrompt(QStringView prompt, qsizetype limit = MaxPromptLength);

/// The prompt link of `prompt` — "brulion://chat?prompt=…" with `partner` (who answers: "acp:claude",
/// an extension's "id/command"; "" the default) and `page` (the page the conversation is about)
/// when given. The prompt is cleaned and capped first, as the app reads it.
QString chatLinkUri(const QString &prompt, const QString &partner = {}, const QString &page = {});

} // namespace brulion::automation
