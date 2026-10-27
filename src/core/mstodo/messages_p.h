// Translation context of the mstodo module's user-visible messages (sign-in and download
// errors). Internal to the mstodo module.
#pragma once

#include <QCoreApplication>

namespace brulion::mstodo::detail {

/// `MsTodoMessages::tr("…")` — English source text, translated by the app's translator (lupdate
/// picks the calls up under the context "MsTodoMessages").
class MsTodoMessages {
    Q_DECLARE_TR_FUNCTIONS(MsTodoMessages)
};

} // namespace brulion::mstodo::detail
