// Translation context of the chart module's messages (errors, the text alternative). Internal.
#pragma once

#include <QCoreApplication>

namespace brulion::chart::detail {

/// `ChartMessages::tr("…")` — English source text, translated by the app's translator (context
/// "ChartMessages").
class ChartMessages {
    Q_DECLARE_TR_FUNCTIONS(ChartMessages)
};

} // namespace brulion::chart::detail
