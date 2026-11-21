#include "uiscale.h"

#include <QGuiApplication>
#include <QWindow>
#include <QtGui/private/qhighdpiscaling_p.h>
#include <qpa/qplatformwindow.h>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace brulion::ui {

namespace {

constexpr std::array kSteps{0.8, 0.9, 1.0, 1.1, 1.15, 1.25, 1.35, 1.5, 1.75, 2.0};

qreal s_applied = 1.0;
QtMessageHandler s_previousHandler = nullptr;

void dropScaleWarning(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type == QtWarningMsg && message.startsWith(u"QHighDpiScaling::setFactor"))
        return;
    if (s_previousHandler)
        s_previousHandler(type, context, message);
}

} // namespace

qreal autoInterfaceScale(QSize screen)
{
    const int h = screen.height();
    if (h < 1300)
        return 1.0;
    if (h < 1500)
        return 1.15;
    if (h < 1700)
        return 1.25;
    if (h < 2000)
        return 1.35;
    if (h < 2400)
        return 1.5;
    return 1.75;
}

qreal cleanInterfaceScale(qreal scale)
{
    if (!(scale > 0)) // also NaN
        return 0;
    const qreal stepped = std::round(scale * 20) / 20;
    return std::clamp(stepped, 0.8, 2.0);
}

qreal effectiveInterfaceScale(qreal setting, qreal automatic)
{
    const qreal chosen = cleanInterfaceScale(setting);
    return chosen > 0 ? chosen : (automatic > 0 ? automatic : 1.0);
}

qreal steppedInterfaceScale(qreal current, int steps)
{
    if (steps == 0)
        return cleanInterfaceScale(current) > 0 ? cleanInterfaceScale(current) : 1.0;
    // The step nearest to `current`, then `steps` further.
    qsizetype at = 0;
    for (qsizetype i = 1; i < qsizetype(kSteps.size()); ++i) {
        if (std::abs(kSteps[i] - current) < std::abs(kSteps[at] - current))
            at = i;
    }
    // Between two steps: the first step in the asked direction counts from the side it lies on.
    if (steps > 0 && kSteps[at] > current + 1e-6)
        --steps;
    else if (steps < 0 && kSteps[at] < current - 1e-6)
        ++steps;
    const qsizetype to = std::clamp<qsizetype>(at + steps, 0, qsizetype(kSteps.size()) - 1);
    return kSteps[to];
}

bool applyInterfaceScale(qreal factor)
{
    if (!(factor > 0))
        factor = 1.0;
    if (qFuzzyCompare(factor, s_applied))
        return false;
    s_applied = factor;
    const QList<QWindow *> windows = QGuiApplication::allWindows();
    // Qt warns "Should only be called when no windows exist": the open windows are told below,
    // which is what the warning is about — kept out of the event log.
    s_previousHandler = qInstallMessageHandler(dropScaleWarning);
    QHighDpiScaling::setGlobalFactor(factor);
    qInstallMessageHandler(s_previousHandler);
    s_previousHandler = nullptr;
    // Open windows keep their size in pixels: their logical size and device pixel ratio follow
    // the new factor, so the scene is laid out and drawn again at the new size — as when a window
    // moves to a screen of another scale.
    using Wsi = QWindowSystemInterface;
    for (QWindow *window : windows) {
        if (!window->handle())
            continue;
        Wsi::handleWindowDevicePixelRatioChanged<Wsi::SynchronousDelivery>(window);
        Wsi::handleGeometryChange<Wsi::SynchronousDelivery>(window, window->handle()->geometry());
    }
    return true;
}

qreal appliedInterfaceScale()
{
    return s_applied;
}

} // namespace brulion::ui
