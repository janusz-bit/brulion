// Internal: tokenizer of calc expressions. Not part of the module's public API.
#pragma once

#include "calc/calclocale.h"

#include <QList>
#include <QString>
#include <QStringView>

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
void normalizeNumbers(QList<Token> &tokens, QStringView source, CalcLocale locale);

/// Words that are always operators: to, mod, and, or, xor, not (case-insensitive).
bool isOperatorWord(QStringView word);
/// Words meaning "of" after a percentage: of, z, od.
bool isOfWord(QStringView word);

} // namespace brulion::calc
