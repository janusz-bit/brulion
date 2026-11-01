// openexternal.h on Android: ACTION_VIEW through Brulion's FileProvider (AndroidBridge::viewFile).
#include "openexternal.h"

#include "android/androidbridge.h"

namespace brulion::platform::detail {

OpenResult openFileExternallyNow(const QString &absolutePath, const QString &mimeType, const QString &displayName,
                                 const QByteArray &bytes)
{
    Q_UNUSED(displayName);
    Q_UNUSED(bytes);
    switch (AndroidBridge::viewFile(absolutePath, mimeType)) {
    case AndroidBridge::ViewResult::Started:
    case AndroidBridge::ViewResult::Copying: return OpenResult::Opened;
    case AndroidBridge::ViewResult::NoApp: return OpenResult::NoApp;
    case AndroidBridge::ViewResult::Failed: break;
    }
    return OpenResult::Failed;
}

} // namespace brulion::platform::detail
