// Internal: the languages of the syntax colours (highlight.h) — what the generic lexer needs to
// know of each. Not part of the module's public API.
#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::codefiles::detail {

struct LanguageSpec {
    enum class Lexer : quint8 { Code, Markup, Json, Yaml, Ini, Markdown, Diff, Luau, Gitignore, Css };
    Lexer lexer = Lexer::Code;
    /// The names languageForPath() gives it.
    QStringList names;
    QStringList lineComments;
    QString blockOpen;
    QString blockClose;
    bool nestedBlocks = false;
    /// The quotes of single-line strings with backslash escapes.
    QString quotes = QStringLiteral("\"'");
    /// A doubled quote inside a string is the quote itself (SQL, Pascal-like).
    bool doubledQuoteEscapes = false;
    /// """…""" and '''…''' over lines (for the quotes it has).
    bool tripleQuotes = false;
    /// `…` strings over lines: JavaScript's template strings (escapes), Go's raw strings (none).
    bool backtickStrings = false;
    bool backtickEscapes = true;
    /// C++'s R"delim(…)delim", Rust's r#"…"#, Nix's ''…''.
    bool cppRawStrings = false;
    bool rustRawStrings = false;
    bool nixStrings = false;
    /// Rust's 'a lifetimes beside 'x' characters.
    bool lifetimes = false;
    /// "#" at the start of a line is the C preprocessor's.
    bool hashPreprocessor = false;
    /// @name before a declaration (Python, Java, Kotlin, TypeScript, Dart, Swift).
    bool decorators = false;
    /// #[…] and #![…] (Rust).
    bool rustAttributes = false;
    /// $name, ${…}, $(…) (shells, Makefiles, CMake's ${…}, PowerShell).
    bool variables = false;
    /// $name / @name / %name are variables (PHP, Perl).
    bool sigils = false;
    /// Keywords in any case (SQL, CMake commands, batch files).
    bool caseInsensitive = false;
    /// A capitalised name is a type (Java, Kotlin, Swift, Rust, TypeScript, QML, Go …).
    bool capitalTypes = false;
    /// An ALL_CAPS name is a constant.
    bool upperConstants = false;
    /// "-" inside names (CSS, Lisp, CMake arguments are no names).
    bool dashInNames = false;
    /// \commands (LaTeX).
    bool backslashCommands = false;
    /// A name right before ":" is a property (QML's bindings: `width: 100`).
    bool colonProperties = false;
    /// "#" starts a comment only at a word's start (shells: a#b is a word).
    bool hashCommentAtWordStart = false;
    QSet<QString> keywords;
    QSet<QString> types;
    QSet<QString> constants;
    QSet<QString> builtins;
    /// The keywords after which a name is defined: a function's (def, fn, func, function …) or a
    /// type's (class, struct, enum, interface, trait, type …).
    QSet<QString> functionDefiners;
    QSet<QString> typeDefiners;
};

/// The language of a languageForPath() name (nullptr: none).
const LanguageSpec *languageSpec(QStringView name);

} // namespace brulion::codefiles::detail
