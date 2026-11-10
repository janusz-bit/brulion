// The hook of openexternal.h and the dispatch to this build's handover.
#include "openexternal.h"

#include <mutex>

namespace brulion::platform {

namespace {
using Hook = std::function<OpenResult(const QString &, const QString &)>;
std::mutex s_hookLock;
Hook s_hook;
} // namespace

void setOpenFileHook(Hook hook)
{
    const std::lock_guard lock(s_hookLock);
    s_hook = std::move(hook);
}

OpenResult openFileExternally(const QString &absolutePath, const QString &mimeType, const QString &displayName,
                              const QByteArray &bytes)
{
    std::unique_lock lock(s_hookLock);
    const Hook hook = s_hook;
    lock.unlock();
    if (hook)
        return hook(absolutePath, mimeType);
    return detail::openFileExternallyNow(absolutePath, mimeType, displayName, bytes);
}

} // namespace brulion::platform
