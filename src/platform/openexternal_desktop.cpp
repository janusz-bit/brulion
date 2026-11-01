// openexternal.h on the desktop (Linux, Windows): the file's default app.
#include "openexternal.h"

#include <QDesktopServices>
#include <QUrl>

namespace brulion::platform::detail {

OpenResult openFileExternallyNow(const QString &absolutePath, const QString &mimeType, const QString &displayName,
                                 const QByteArray &bytes)
{
    Q_UNUSED(mimeType);
    Q_UNUSED(displayName);
    Q_UNUSED(bytes);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath)) ? OpenResult::Opened : OpenResult::Failed;
}

} // namespace brulion::platform::detail
