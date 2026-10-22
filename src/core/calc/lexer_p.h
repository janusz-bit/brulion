// Internal: tokenizer of calc expressions. Not part of the module's public API.
#pragma once

#include "calc/calclocale.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <optional>
#include <utility>

namespace brulion::calc {

enum class TokenKind {
    Number,      ///< `text` = canonical digits ("12.5"), `number` = value
    Ident,       ///< `text` = name (NFC); also "°C", "°F", "°", "∞"
    String,      ///< `text` = content without quotes, escapes resolved
    Operator,    ///< `text` = canonical operator: + - * / ^ % ! = == != < <= > >= ? : √ and or
    LParen,
    RParen,
    LBracket,
    RBracket,
    Comma,
    Semicolon,
    Currency,    ///< currency symbol character: € $ £
    Superscript, ///< ² ³ ⁻¹ ... — `number` = the exponent
    Unknown,     ///< any other character
};

struct Token {
    TokenKind kind = TokenKind::Unknown;
    QString text;
    qsizetype pos = 0;    ///< offset in the lexed string
    qsizetype length = 0; ///< length in the lexed string
    double number = 0;
    bool unterminated = false; ///< String without closing quote
    /// LParen of a call like `round(2,5)`, where a ',' between digits could be a decimal comma as
    /// well as an argument separator, and the function takes both counts (normalizeNumbers()).
    bool ambiguousComma = false;

    bool is(TokenKind k) const { return kind == k; }
    bool isOperator(QStringView op) const { return kind == TokenKind::Operator && text == op; }
    bool isIdent(QStringView name) const { return kind == TokenKind::Ident && text == name; }
};

/// Split an NFC-normalised expression into tokens; a `#` or `//` outside strings starts a
/// comment (the rest is dropped). Strings use "…" or '…', but a `'` right after a word, number or
/// closing bracket is an apostrophe ("Jan's": an Unknown token). Never fails: unknown characters
/// become Unknown tokens.
QList<Token> lex(QStringView input);

/// Merge number tokens written with separators, depending on context and locale:
/// "12,5" (decimal comma — outside function arguments and lists, or inside calls that use ';'
/// as argument separator), English "1,000,000.5", and "1 000 000" / "12 345,5" (groups of three
/// digits separated by one space, no-break space or thin space).
/// In a call without ';' a ',' between digits ("2,5", no spaces) separates arguments, unless only
/// the decimal reading gives the builtin function a valid number of arguments (`sqrt(2,25)`).
/// When both readings fit (`round(2,5)`, `max(1,5)`), the separators stay and the call's LParen
/// is marked `ambiguousComma`: evaluating it is an error, never a guess. Runs of such commas
/// (`sum(1,2,3)`) always separate.
void normalizeNumbers(QList<Token> &tokens, QStringView source, CalcLocale locale);

/// Numbers of arguments {min, max (-1 = any)} of the builtin function `name` (case-insensitive);
/// nullopt for other names. Implemented by the evaluator (its table of builtins).
std::optional<std::pair<int, int>> builtinArity(const QString &name);

/// Words that are always operators: to, mod, and, or, xor, not (case-insensitive).
bool isOperatorWord(QStringView word);
/// Words meaning "of" after a percentage: of, z, od.
bool isOfWord(QStringView word);

} // namespace brulion::calc
