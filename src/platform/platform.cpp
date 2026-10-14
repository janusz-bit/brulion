#include "platform.h"

namespace brulion::platform {

QString targetName()
{
#if defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_WASM)
    return QStringLiteral("wasm");
#else
    return QStringLiteral("linux");
#endif
}

} // namespace brulion::platform
