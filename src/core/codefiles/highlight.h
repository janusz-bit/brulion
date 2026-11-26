// Syntax colours of code for the file viewer (docs/CODE-FILES.md "Syntax colours"): one line at a
// time into coloured spans, carrying what a line leaves open — a block comment, a string over
// lines (Python's triple quotes, JavaScript's template strings, C++ and Rust raw strings, Nix's
// '' strings), an HTML comment, fenced code in Markdown — to the next as a small state. Part of
// the codefiles module; the app's CodeFileDocument paints the spans.
//
//   CodeState state;
//   const QString language = languageForPath(u"src/main.cpp");   // viewer.h: "cpp"
//   for (const QString &line : lines)
//       for (const CodeSpan &s : lexCodeLine(language, line, &state)) …
//
// A table of languages (highlight_languages.cpp) says what each has: line and block comments,
// quotes, keywords, types, constants, builtins, a C preprocessor, decorators, shell variables;
// with it one lexer serves the C family (C, C++, C#, Java, Kotlin, Swift, Go, Rust, Dart, Zig,
// Scala, Groovy, Objective-C, GLSL, WGSL, protobuf), JavaScript and TypeScript (JSX), QML, Python,
// Ruby, PHP, Perl, shells (sh, bash, zsh, fish, PowerShell, batch files), CMake, Nix, Makefiles,
// Dockerfiles, SQL, Haskell, OCaml, Elixir, Erlang, Clojure, R, Julia, HCL, GraphQL, Vim script,
// LaTeX and CSS / SCSS / Less. Own lexers: markup (HTML, XML, Vue and Svelte templates: tags,
// attributes, strings, comments, entities), JSON (keys apart from values), YAML, TOML and INI
// (keys, sections), Markdown (headings, fenced code, inline code, links), diffs (added, removed,
// hunk and file headers), .gitignore; Luau and Lua are core syntax's lexer (syntax/luau.h), the
// same colours as a ```luau block in a note. Other languages, and plain text, have no spans.
// Malformed code never fails: an unclosed string ends with its line (or stays open, for the kinds
// that span lines), an unclosed comment runs on. Lines longer than 10 000 characters get no spans
// past that point.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::codefiles {

enum class CodeToken : quint8 {
    Keyword,
    Type,         ///< a built-in type, a capitalised name where a language uses them for types
    Constant,     ///< true, false, null, nil, None …; ALL_CAPS names in some languages
    Builtin,      ///< a standard library function or global
    Function,     ///< a name followed by "(", the name a definition gives
    Number,
    String,
    Comment,
    Preprocessor, ///< #include, #define; decorators and attributes (@Override, #[derive])
    Tag,          ///< markup tags; Markdown and INI headings / sections
    Attribute,    ///< markup attributes
    Property,     ///< JSON / YAML / TOML / INI keys, CSS properties
    Variable,     ///< shell and Makefile variables, PHP / Perl sigils
    Inserted,     ///< a diff's added line
    Deleted,      ///< a diff's removed line
};

struct CodeSpan {
    int from = 0;
    int length = 0;
    CodeToken kind = CodeToken::Keyword;
    friend bool operator==(const CodeSpan &, const CodeSpan &) = default;
};

/// What a line leaves open for the next one.
struct CodeState {
    quint8 kind = 0; ///< 0 nothing open; the others are the lexer's (highlight.cpp)
    quint16 data = 0;
    friend bool operator==(const CodeState &, const CodeState &) = default;
};

/// The coloured spans of `line` (no line break in it) in `language` (languageForPath()'s names),
/// in order, never overlapping; `state` is what the line starts in and, on return, what the next
/// one starts in. An unknown language: no spans, the state unchanged.
QList<CodeSpan> lexCodeLine(QStringView language, QStringView line, CodeState *state);
/// The lexer knows `language`.
bool hasSyntax(QStringView language);
/// "keyword", "type", "constant", "builtin", "function", "number", "string", "comment",
/// "preprocessor", "tag", "attribute", "property", "variable", "inserted", "deleted".
QString codeTokenName(CodeToken kind);

} // namespace brulion::codefiles
