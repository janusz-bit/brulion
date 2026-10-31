// The hook of openexternal.h and the dispatch to this build's handover.
#include "openexternal.h"

#include <mutex>

namespace brulion::platform {

namespace {
std::mutex s_hookLock;
std::function<OpenResult(const QString &, const QString &)> s_hook;
} // namespace

void setOpenFileHook(std::function<OpenResult(const QString &, const QString &)> hook)
{
    const std::lock_guard lock(s_hookLock);
    s_hook = std::move(hook);
}

OpenResult openFileExternally(const QString &absolutePath, const QString &mimeType, const QString &displayName,
                              const QByteArray &bytes)
{
    std::function<OpenResult(const QString &, const QString &)> hook;
    {
        const std::lock_guard lock(s_hookLock);
        hook = s_hook;
    }
    if (hook)
        return hook(absolutePath, mimeType);
    return detail::openFileExternallyNow(absolutePath, mimeType, displayName, bytes);
}

} // namespace brulion::platform
