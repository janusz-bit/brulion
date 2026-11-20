// Macros of extensions in a note's text (docs/SYNTAX.md §13 "Macros", docs/EXTENSIONS.md
// "Macros"): `{{ext:<extension>/<macro> <arguments>}}` — a value an extension computes, shown in
// the text where the macro stands (the note keeps the macro as written). Part of the markdown
// module — see markdown/markdown.h for the module overview.
//
//   {{ext:silownia/rekord Przysiad}}         the gym's record of the squat: "120 kg × 3"
//   {{ext:nawyki/seria Czytanie}}            a habit's streak
//   {{ ext:licznik-slow/dzis }}              blanks inside the braces are allowed
//
// Why this form: `{{…}}` is a macro in Logseq too (`{{query …}}`, `{{embed …}}`, `{{cloze …}}`,
// `{{renderer :…}}`, user macros of config.edn — a name, then arguments separated by commas), and
// Brulion's templates use it for their variables (`{{date}}`, `{{date:FORMAT}}`). The `ext:` prefix
// and the `/` between an extension and its macro are none of theirs: a Logseq macro stays text (as
// Logseq shows a macro it does not know), a template leaves `{{ext:…}}` as written — so a template
// can put a live macro into every note made from it — and Obsidian's Templater (`<% … %>`) is
// another syntax altogether.
//
// The renderer (inlinerenderer.h, RenderContext::macro) asks the app for what a macro shows: a
// MacroView — pieces of text, links and code (always escaped, never HTML), or a placeholder while
// it is computed, when its extension does not run on this device, or after an error.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVariantMap>

namespace brulion::markdown {

/// A macro found in a text.
struct MacroSpan {
    qsizetype from = 0; ///< the `{{`
    qsizetype to = 0; ///< after the `}}`
    QString extension; ///< the extension's id, lower case ("silownia")
    QString name; ///< the macro's name as written ("rekord")
    QString arguments; ///< what follows the name, trimmed ("Przysiad"); "" for none
    bool operator==(const MacroSpan &other) const = default;
};

/// The longest macro, `{{` to `}}`: a longer one is text.
inline constexpr qsizetype MaxMacroLength = 2000;

/// Every macro of `text`, in order, never overlapping: `{{`, blanks, `ext:` (any case), an id
/// (ASCII letters, digits, `-`, `_`), `/`, a name (letters of any script, digits, `-`, `_`, `.`),
/// then `}}` — or blanks and the arguments up to the first `}}` of the same line. Arguments may not
/// hold `{{` (the inner one may start a macro). Code spans, comments and formulas are not looked
/// into by this function: pass the text with them blanked (the renderer's masked text) when that
/// matters. A backslash before the first `{` (`\{{ext:a/b}}`) makes it text.
QList<MacroSpan> findMacros(QStringView text);

/// The arguments of a macro as a list: split at commas outside double quotes, each trimmed, the
/// quotes of a quoted one taken away (`Wyciskanie leżąc` → one, `a, "b, c"` → `a` and `b, c`,
/// `""` → an empty one). At most 32 arguments (the rest are the last one's) and 500 characters
/// each.
QStringList macroArguments(QStringView arguments);

/// A macro as written: `{{ext:silownia/rekord Przysiad}}` (`{{ext:a/b}}` without arguments).
QString macroSource(const QString &extension, const QString &name, const QString &arguments);

/// True for a name b.macro (and b.variable) accepts: 1 … 64 letters, digits, `-`, `_`, `.`, the
/// first a letter or a digit.
bool isMacroName(QStringView name);

/// What a macro shows (RenderContext::macro).
struct MacroPiece {
    enum class Kind { Text, Link, Code };
    Kind kind = Kind::Text;
    QString text; ///< what it shows (a link's label)
    QString page; ///< Link: the page it opens (as a [[link]] names it)
    QString tone; ///< "", "muted", "success", "danger", "accent"
    bool bold = false;
    bool operator==(const MacroPiece &other) const = default;
};

struct MacroView {
    enum class State {
        Ok, ///< `pieces`
        Pending, ///< being computed (nothing yet to show)
        Unapproved, ///< its extension waits for approval on this device: a placeholder to approve
        Off, ///< its extension is switched off on this device (or not running)
        Unknown, ///< no such extension or macro
        Error, ///< it failed: `message`
        TooMany, ///< the page has more macros than are computed (`message` says how many)
    };
    State state = State::Ok;
    QList<MacroPiece> pieces;
    QString message;
    bool operator==(const MacroView &other) const = default;
};

/// A view as one line of text (a test's, a log's): the pieces' texts, a link as [[page|label]],
/// code in backticks; a placeholder as "<state>: message".
QString macroViewText(const MacroView &view);

/// A ```view block (docs/SYNTAX.md §13 "View blocks"): an extension's view drawn in the page —
/// its first line names it, `<extension>/<view>` (`view:` before it may be written), the lines
/// after it are the view's params, `key: value` (keys of letters, digits, `-`, `_`; values as
/// written, trimmed; a key again replaces it); blank lines and `#` / `//` comments are skipped.
///
///   ```view
///   silownia/dzis
///   cwiczenie: Przysiad
///   ```
struct ViewBlock {
    QString extension; ///< lower case ("silownia"); "" when the block names no view
    QString view; ///< as written ("dzis")
    QVariantMap params; ///< key → text
    /// Why it names no view, or a line it could not read ("line 3: not key: value"); "" fine.
    QString problem;
};
/// The fence language of view blocks: "view".
inline constexpr QStringView ViewBlockLanguage = u"view";
/// The block's inner text (between its fences) read: at most 64 params, 64 KB of text.
ViewBlock parseViewBlock(QStringView source);

} // namespace brulion::markdown
