// Locale-aware number formatting/parsing and currency recognition. Part of the calc module —
// see calc/calc.h for the overview.
//
// Formatting is implemented by hand (not QLocale) so results are identical on every platform
// regardless of ICU data: Polish "12 345,5" (no-break space grouping from 10 000 up), English
// "12,345.5" (grouping from 1,000 up); at most 10 significant digits.
#pragma once

#include "calc/calclocale.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::calc {

/// No-break space: the Polish thousands separator in formatted numbers.
inline constexpr QChar kNbsp = QChar(0x00a0);
/// Maximum significant digits shown for fractional results.
inline constexpr int kSignificantDigits = 10;

/// A currency a number was written with. Currencies are tags on numbers, never converted.
struct Currency {
    QString code;        ///< ISO code: PLN, EUR, USD, GBP, CHF
    QString symbol;      ///< symbol or code as written: "zł", "PLN", "€", "$"
    bool prefix = false; ///< written before the number ("$12", "€5")

    bool isValid() const { return !code.isEmpty(); }
    friend bool operator==(const Currency &a, const Currency &b)
    {
        return a.code == b.code && a.symbol == b.symbol && a.prefix == b.prefix;
    }
};

/// ISO code of a currency symbol or code ("zł" -> "PLN", "€" -> "EUR"), empty when unknown.
/// Recognised: zł PLN € EUR $ USD £ GBP CHF.
QString currencyCode(QStringView symbol);

/// Options of the number formatters.
struct NumberFormatOptions {
    /// Group thousands (default). Off inside English lists, where ',' separates items.
    bool grouping = true;
};

/// Format a number for display: up to 10 significant digits (integers below 1e15 are never
/// rounded), trailing zeros trimmed, "-0" shown as "0", NaN as "NaN", infinities as "∞"/"-∞".
/// Magnitudes >= 1e15 or < 1e-6 use exponent notation ("1,5e21", "1e-7").
QString formatNumber(double value, CalcLocale locale, NumberFormatOptions options = {});

/// Format with exactly `fractionDigits` decimals ("12,50"); non-finite or huge values fall back
/// to formatNumber().
QString formatFixed(double value, int fractionDigits, CalcLocale locale, NumberFormatOptions options = {});

/// Format an amount of money: rounded to cents, integers without decimals, otherwise exactly two
/// ("17 zł", "12,50 zł", "$17", "-$5.50", "1,234.50 €").
QString formatMoney(double value, const Currency &currency, CalcLocale locale, NumberFormatOptions options = {});

/// Result of parseLocaleNumber().
struct ParsedNumber {
    double value = 0;  ///< percentages are already fractions ("23%" -> 0.23)
    Currency currency; ///< invalid when none was written
    bool percent = false;
};

/// Parse a number as people write it in notes and table cells: "12,5", "1 234,50 zł",
/// "€12.50", "-3", "−5", "23%", "1.234,56", "1,234.56" (English), "1e3", "**12**".
/// A single comma is a decimal comma, except "1,234" in English. Returns nullopt otherwise.
std::optional<ParsedNumber> parseLocaleNumber(QStringView text, CalcLocale locale);

/// Remove light Markdown emphasis around a value ("**12**", "_5_", "`7`", "~~3~~"), trimmed.
QString stripInlineMarkup(QStringView text);

} // namespace brulion::calc
