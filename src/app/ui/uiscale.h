#pragma once

#include <QSize>
#include <QtGlobal>

// The interface scale (Settings → Appearance → "Interface size", docs/UI.md "Look & feel"): the
// whole window drawn larger or smaller — text, controls, icons, gaps and every size the QML
// writes as a number — through Qt's global scale factor (the factor QT_SCALE_FACTOR sets),
// multiplied with the system's own scaling. "Automatic" (the default) makes the interface larger
// on large screens, where Brulion's desktop sizes looked small: by the screen's height in
// device-independent pixels (after the system's scaling), so a 4K monitor at 100 % and a 1440p
// one get larger text than a laptop or a 1080p monitor, and a 4K monitor the system already
// scales by 200 % stays at 1. Desktop builds only; phones and the browser keep their sizes.
//
// QHighDpiScaling (Qt's private API) changes the factor at run time: windows are drawn again at
// the new size, their size in pixels kept.
namespace brulion::ui {

/// The scale "Automatic" picks for a screen whose available area is `screen` device-independent
/// pixels: 1 up to 1300 px high (a laptop, a 1080p or 1200p monitor), then 1.15 (1440p), 1.25
/// (1600p), 1.35 (1800p), 1.5 (2160p: 4K at 100 %) and 1.75 beyond. By the height, so an ultrawide
/// 3440×1440 counts as 1440p; an empty size (no screen) gives 1.
qreal autoInterfaceScale(QSize screen);

/// A chosen scale on the settings' 5 % steps within 0.8 … 2; 0 or less (or not a number) means
/// "Automatic" and stays 0.
qreal cleanInterfaceScale(qreal scale);

/// The factor of a setting: the chosen one, or `automatic` for 0.
qreal effectiveInterfaceScale(qreal setting, qreal automatic);

/// The next larger (`steps` > 0) or smaller (`steps` < 0) of the zoom commands' steps (80, 90,
/// 100, 110, 115, 125, 135, 150, 175, 200 %) from `current`, kept within the ends.
qreal steppedInterfaceScale(qreal current, int steps);

/// Sets Qt's global scale factor (1: none). Returns whether it changed. Before the first window
/// it is what QT_SCALE_FACTOR would have been; later Qt draws the open windows again.
bool applyInterfaceScale(qreal factor);

/// The factor applyInterfaceScale() set last (1 before).
qreal appliedInterfaceScale();

} // namespace brulion::ui
