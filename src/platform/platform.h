#pragma once

#include <QString>

namespace brulion::platform {

/// "linux", "android" or "wasm" — the target this binary was built for.
QString targetName();

} // namespace brulion::platform
