// Shared helpers of the calc tests.
#pragma once

#include "calc/calc.h"

#include <QString>
#include <QStringList>

namespace calctest {

using brulion::calc::CalcLineResult;
using brulion::calc::CalcLocale;
using brulion::calc::Scope;

inline constexpr CalcLocale Pl = CalcLocale::Polish;
inline constexpr CalcLocale En = CalcLocale::English;

/// "value", "!error" or "" (nothing shown) for one result.
inline QString show(const CalcLineResult &r)
{
    if (r.hasError())
        return u'!' + r.error;
    return r.value;
}

/// show() of every line of a calc block (fresh scope).
inline QStringList results(const QString &source, CalcLocale locale = Pl)
{
    Scope scope;
    QStringList out;
    for (const CalcLineResult &r : brulion::calc::evaluateCalc(source, scope, locale))
        out.append(show(r));
    return out;
}

/// show() of the last line of a calc block.
inline QString last(const QString &source, CalcLocale locale = Pl)
{
    const QStringList all = results(source, locale);
    return all.isEmpty() ? QString() : all.last();
}

/// show() of an inline calculation.
inline QString inl(const QString &expression, CalcLocale locale = Pl, const Scope &scope = Scope())
{
    return show(brulion::calc::evaluateInline(expression, scope, locale));
}

/// Replace '_' by a no-break space (expected Polish groupings: "24_691").
inline QString nb(QString s)
{
    return s.replace(u'_', QChar(0x00a0));
}

} // namespace calctest
