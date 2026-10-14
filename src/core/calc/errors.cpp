#include "calc/errors.h"

namespace brulion::calc {

namespace {

struct Message {
    const char16_t *pl;
    const char16_t *en;
};

// Polish typographic quotes „…” and English “…” around user text.
Message messageFor(CalcErrorCode code)
{
    switch (code) {
    case CalcErrorCode::None:
        return {u"", u""};
    case CalcErrorCode::Generic:
        return {u"Błąd obliczenia", u"Calculation error"};
    case CalcErrorCode::UndefinedSymbol:
        return {u"Nieznana zmienna: %1", u"Unknown variable: %1"};
    case CalcErrorCode::UndefinedFunction:
        return {u"Nieznana funkcja: %1", u"Unknown function: %1"};
    case CalcErrorCode::UnitsMismatch:
        return {u"Niezgodne jednostki", u"Units do not match"};
    case CalcErrorCode::UnexpectedEnd:
        return {u"Niedokończone wyrażenie", u"Unexpected end of expression"};
    case CalcErrorCode::ParenExpected:
        return {u"Brakuje nawiasu „%1”", u"Missing “%1”"};
    case CalcErrorCode::ValueExpected:
        return {u"Brakuje wartości", u"Value expected"};
    case CalcErrorCode::UnexpectedOperator:
        return {u"Nieoczekiwany operator „%1”", u"Unexpected operator “%1”"};
    case CalcErrorCode::UnexpectedPart:
        return {u"Nieoczekiwany fragment „%1”", u"Unexpected “%1”"};
    case CalcErrorCode::CannotAssign:
        return {u"Nie można przypisać do „%1”", u"Cannot assign to “%1”"};
    case CalcErrorCode::NotANumber:
        return {u"Nie można zamienić „%1” na liczbę", u"Cannot convert “%1” to a number"};
    case CalcErrorCode::WrongType:
        return {u"Nieprawidłowy typ argumentu: %1", u"Wrong argument type: %1"};
    case CalcErrorCode::TooManyArgs:
        return {u"Za dużo argumentów funkcji %1", u"Too many arguments for %1"};
    case CalcErrorCode::TooFewArgs:
        return {u"Za mało argumentów funkcji %1", u"Too few arguments for %1"};
    case CalcErrorCode::Recursion:
        return {u"Zbyt głęboka rekurencja", u"Recursion too deep"};
    case CalcErrorCode::TooLong:
        return {u"Wyrażenie jest za długie", u"Expression is too long"};
    case CalcErrorCode::TooLarge:
        return {u"Wynik jest za duży (limit %1)", u"Result is too large (limit %1)"};
    case CalcErrorCode::TooComplex:
        return {u"Obliczenie jest zbyt złożone", u"Calculation is too complex"};
    case CalcErrorCode::TooBig:
        return {u"Liczba jest za duża", u"Number is too large"};
    case CalcErrorCode::NotAFunction:
        return {u"„%1” nie jest funkcją", u"“%1” is not a function"};
    case CalcErrorCode::CurrencyConversion:
        return {u"Przeliczanie walut nie jest obsługiwane", u"Currency conversion is not supported"};
    case CalcErrorCode::NoValues:
        return {u"Brak wartości liczbowych", u"No numeric values"};
    case CalcErrorCode::Cycle:
        return {u"Cykliczne odwołanie", u"Circular reference"};
    case CalcErrorCode::RefError:
        return {u"Błąd w komórce %1", u"Error in cell %1"};
    case CalcErrorCode::RefOutside:
        return {u"Odwołanie poza tabelą: %1", u"Reference outside the table: %1"};
    case CalcErrorCode::Domain:
        return {u"Argument poza dziedziną funkcji %1", u"Argument out of range for %1"};
    case CalcErrorCode::UnterminatedString:
        return {u"Brakuje cudzysłowu zamykającego", u"Missing closing quote"};
    case CalcErrorCode::NotAUnit:
        return {u"„%1” nie jest jednostką", u"“%1” is not a unit"};
    case CalcErrorCode::ListNotAllowed:
        return {u"Zakres lub lista może być tylko argumentem funkcji",
                u"A range or list can only be a function argument"};
    }
    return {u"Błąd obliczenia", u"Calculation error"};
}

} // namespace

QString CalcError::message(CalcLocale locale) const
{
    const Message m = messageFor(code);
    QString text = QString::fromUtf16(locale == CalcLocale::English ? m.en : m.pl);
    if (text.contains(u"%1"))
        text = text.arg(arg);
    return text;
}

QString booleanWord(bool value, CalcLocale locale)
{
    if (locale == CalcLocale::English)
        return value ? QStringLiteral("true") : QStringLiteral("false");
    return value ? QStringLiteral("prawda") : QStringLiteral("fałsz");
}

} // namespace brulion::calc
