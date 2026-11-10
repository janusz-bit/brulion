// openexternal.h on the desktop (Linux, Windows): the file's default app.
#include "openexternal.h"

#include <QDesktopServices>
#include <QUrl>

namespace brulion::platform::detail {

OpenResult openFileExternallyNow(const QString &absolutePath, const QString &, const QString &, const QByteArray &)
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath)) ? OpenResult::Opened : OpenResult::Failed;
}

} // namespace brulion::platform::detail
