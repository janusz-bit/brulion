// Translation context of the io module's user-visible messages (import / export warnings).
// Internal to the io module.
#pragma once

#include <QCoreApplication>

namespace brulion::io::detail {

/// `IoMessages::tr("…")` — English source text, translated by the app's translator (lupdate
/// picks the calls up under the context "IoMessages").
class IoMessages {
    Q_DECLARE_TR_FUNCTIONS(IoMessages)
};

} // namespace brulion::io::detail
