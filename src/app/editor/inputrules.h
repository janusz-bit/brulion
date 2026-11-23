// Input rules — text the block editor replaces as it is typed (`->` → `→`; docs/EXTENSIONS.md
// "Input rules", docs/UI.md "Input rules"). Extensions declare them (b.app.inputRule, core
// extensions/appapi.h); the shell hands them to the editor through EditorHost::inputRules(), and
// PageController::setEditText() asks inputRuleHit() after each keystroke — no Luau runs while
// typing. Pure functions, GUI-free.
//
//   const QList<InputRule> rules{{u"->"_s, u"→"_s}, {u"teh"_s, u"the"_s, true}};
//   inputRuleHit(u"a ->"_s, 4, 1, rules)    → {2, 4, "→"}
//   inputRuleHit(u"`a ->"_s, 5, 1, rules)   → nullopt (inside a code span)
//   inputRuleHit(u"teh "_s, 4, 1, rules)    → {0, 3, "the"} (a whole word: the space typed after it stays)
#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace brulion::editor {

struct InputRule {
    QString match;     ///< the text typed before the caret (NFC, one line, not empty)
    QString replace;   ///< what it becomes (one line)
    bool word = false; ///< a whole word only: replaced once a character that ends a word follows it
    bool operator==(const InputRule &other) const = default;
};

/// The range of the text a rule replaces and what goes there.
struct InputRuleHit {
    int from = 0;
    int to = 0;
    QString replace;
    bool operator==(const InputRuleHit &other) const = default;
};

/// The rule that fires after `typed` characters were typed (inserted) right before `cursor` in a
/// block's text `text`: the longest match ending at the caret (a later rule wins a tie); a `word`
/// rule when the character typed last ends a word (not a letter, a digit, a mark or `_`) and the
/// match starts one — that character stays after the replacement. Nothing fires when more was
/// typed than the match (a paste), and never inside code (a code span, also one still open before
/// the caret, a fenced block in the text), math (`$…$`, `$$…$$`), a comment (`%%…%%`), a link's
/// target (`[[…`, `](…`) or a URL (`https://…`): inputRuleProtected().
std::optional<InputRuleHit> inputRuleHit(const QString &text, int cursor, int typed, const QList<InputRule> &rules);

/// The position `at` of a block's text lies where nothing is replaced as it is typed — judged by
/// the text before it, as it is being typed: inside a code span (closed or still open), a fenced
/// code block or a `$$` display formula within the text, an inline formula, a `%%` comment, a wiki
/// link (`[[…`, `![[…`), a Markdown link's destination (`](…`) or a URL.
bool inputRuleProtected(const QString &text, int at);

} // namespace brulion::editor
