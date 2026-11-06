// The tray calendar as a layer-shell surface through KDE's LayerShellQt (traywindow.h).
#include "traywindow.h"

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QMargins>
#include <QWindow>

using namespace Qt::StringLiterals;

namespace brulion::platform {

bool trayPopupAnchorable()
{
    // "wayland", "wayland-egl", "wayland-brcm": Qt's Wayland platform plugins.
    return QGuiApplication::platformName().startsWith(u"wayland"_s);
}

bool anchorTrayPopup(QWindow *window, int margin)
{
    if (!window || !trayPopupAnchorable())
        return false;
    // Set before the window is shown: LayerShellQt gives it the layer-shell integration when its
    // platform window is created (now, when it exists already); a compositor without the protocol
    // leaves it an ordinary window (a warning in the log).
    auto *layer = LayerShellQt::Window::get(window);
    if (!layer)
        return false;
    layer->setScope(u"brulion-tray-calendar"_s);
    layer->setLayer(LayerShellQt::Window::LayerTop);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom)
                      | LayerShellQt::Window::AnchorRight);
    // 0: no space of its own reserved, and placed off the panels' reserved space.
    layer->setExclusiveZone(0);
    layer->setMargins(QMargins(margin, margin, margin, margin));
    // The keyboard (Escape) once shown or clicked; a click elsewhere takes it away, so the
    // calendar hides itself as a flyout does (ShellTrayCalendar.qml).
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
    layer->setActivateOnShow(true);
    // The screen the user works on (the tray clicked is there), not the one the window last was.
    layer->setWantsToBeOnActiveScreen(true);
    return true;
}

} // namespace brulion::platform
