// Translation context of the caldav module's user-visible messages. Internal to the caldav module.
#pragma once

#include <QCoreApplication>

namespace brulion::caldav::detail {

/// `CalDavMessages::tr("…")` — English source text, translated by the app's translator (lupdate
/// picks the calls up under the context "CalDavMessages").
class CalDavMessages {
    Q_DECLARE_TR_FUNCTIONS(CalDavMessages)
};

} // namespace brulion::caldav::detail
