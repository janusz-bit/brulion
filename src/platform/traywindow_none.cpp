// A build without layer-shell support (traywindow.h): the tray calendar stays an ordinary window.
#include "traywindow.h"

namespace brulion::platform {

bool trayPopupAnchorable()
{
    return false;
}

bool anchorTrayPopup(QWindow *, int)
{
    return false;
}

} // namespace brulion::platform
