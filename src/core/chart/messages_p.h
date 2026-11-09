// The chart module's internals: the translation context of its messages (errors, the text
// alternative) and cutting text from a note. Internal.
#pragma once

#include <QCoreApplication>
#include <QStringView>

namespace brulion::chart::detail {

/// `ChartMessages::tr("…")` — English source text, translated by the app's translator (context
/// "ChartMessages").
class ChartMessages {
    Q_DECLARE_TR_FUNCTIONS(ChartMessages)
};

/// The first `n` code units of `text`, one fewer where the cut would split a surrogate pair.
inline QStringView cutAt(QStringView text, qsizetype n)
{
    if (n > 0 && n < text.size() && text.at(n - 1).isHighSurrogate())
        --n;
    return text.left(n);
}

} // namespace brulion::chart::detail
