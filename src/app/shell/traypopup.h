// The tray calendar's placement (ShellTrayCalendar.qml, docs/UI.md "Window layout"): pure
// geometry next to the tray icon, for a taskbar on any edge of the screen, and the guard that
// keeps a click on the icon from reopening the popup it has just hidden.
//
// All rectangles are in logical (device-independent) pixels, as Qt gives them:
// QSystemTrayIcon::geometry() (Windows; often empty on X11 and Wayland), QCursor::pos(),
// QScreen::availableGeometry() (the work area: the screen without the taskbar or panels).
#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

#include <optional>

namespace brulion::shell {

/// The edge of the work area the taskbar (the anchor) sits at.
enum class TrayEdge { Bottom, Top, Left, Right };

/// The edge nearest to `anchor`: outside the work area (a taskbar the work area leaves out) the
/// side it is on, inside it (a panel the desktop does not reserve, a cursor) the closest side.
/// Ties go to the bottom, then the right, the top and the left.
TrayEdge trayEdge(QPoint anchor, const QRect &available);

/// The top-left corner for a popup of `popup` size next to the tray icon `icon`, or next to
/// `cursor` when the icon's rectangle is empty; with neither, the work area's bottom-right
/// corner. The popup sits against the taskbar's edge of the work area (`margin` px away),
/// centred on the anchor along that edge, and is kept inside `available` (`margin` px from its
/// sides when it fits; one larger than the work area starts at its top-left corner).
QPoint trayPopupPosition(const QRect &icon, std::optional<QPoint> cursor, const QRect &available, QSize popup,
                         int margin);

/// trayPopupPosition() on the screen of the anchor (QGuiApplication::screenAt; the primary
/// screen when the anchor is on none). Without a screen (a QCoreApplication: unit tests) the
/// anchor itself — the icon's centre or the cursor.
QPoint trayPopupPositionOnScreen(const QRect &icon, std::optional<QPoint> cursor, QSize popup, int margin);

/// A click on the tray icon first takes the activation from the open popup, which hides itself;
/// the click itself arrives a moment later. Within this many milliseconds after such a hide the
/// click means "close", so the popup stays closed.
inline constexpr int TrayPopupReopenGuardMs = 300;

/// Whether a click on the tray icon may open the popup `msSinceHiddenByDeactivation` ms after it
/// hid itself on losing the activation (negative: it never did).
bool trayPopupShouldReopen(qint64 msSinceHiddenByDeactivation);

} // namespace brulion::shell
