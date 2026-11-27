// Random ids of the caldav module (calendars, accounts, synced tasks' block ids, events, the
// inbox's intents). Internal to the caldav module.
#pragma once

#include <QString>

namespace brulion::caldav::detail {

/// `length` random lower-case letters and digits.
QString randomId(int length);

} // namespace brulion::caldav::detail
