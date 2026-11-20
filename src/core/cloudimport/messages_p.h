// Translation context of the cloudimport module's user-visible messages (download errors and
// warnings). Internal to the cloudimport module.
#pragma once

#include <QCoreApplication>

namespace brulion::cloudimport::detail {

/// `CloudMessages::tr("…")` — English source text, translated by the app's translator (lupdate
/// picks the calls up under the context "CloudMessages").
class CloudMessages {
    Q_DECLARE_TR_FUNCTIONS(CloudMessages)
};

} // namespace brulion::cloudimport::detail
