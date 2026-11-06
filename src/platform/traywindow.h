// The tray calendar's placement under Wayland (docs/UI.md "The tray calendar"): an ordinary
// window cannot place itself there — the compositor puts it where it wants (often the screen's
// middle), and the click on the tray icon arrives without its position (Qt's and KDE's
// StatusNotifierItem drop it). Compositors with the layer-shell protocol (KDE Plasma's KWin, sway,
// Hyprland, other wlroots ones; not GNOME) can anchor a surface to the screen's edges, as their own
// panels and pop-ups are: the popup becomes such a surface in the bottom-right corner of the work
// area (the compositor keeps it off the panels), above the windows, taking the keyboard when
// clicked and when shown.
//
// Through KDE's LayerShellQt when the build has it (the Nix package; BRULION_LAYER_SHELL);
// elsewhere, on X11 and on Windows nothing (the caller positions the window itself).
#pragma once

class QWindow;

namespace brulion::platform {

/// Whether anchorTrayPopup() can work in this run: the build has layer-shell support and the app
/// runs on Wayland (the compositor may still lack the protocol: the window then stays ordinary).
bool trayPopupAnchorable();

/// Make `window` (not shown yet, or hidden) a layer-shell surface in the bottom-right corner of
/// the work area of the active screen, `margin` px from its edges. Once per window is enough (the
/// settings survive hide and show). False when it cannot (trayPopupAnchorable() is false).
bool anchorTrayPopup(QWindow *window, int margin);

} // namespace brulion::platform
