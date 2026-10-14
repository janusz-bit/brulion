// Locale of the calc module. Part of the calc module — see calc/calc.h for the overview.
#pragma once

#include "dates/names.h"

namespace brulion::calc {

/// Language of formatted numbers ("1 234,5" / "1,234.5"), value words ("prawda" / "true") and
/// error messages: the UI language shared with the dates module (Polish is the default).
/// Enumerators: CalcLocale::Polish, CalcLocale::English.
using CalcLocale = dates::UiLocale;

} // namespace brulion::calc
