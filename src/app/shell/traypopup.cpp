#include "traypopup.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

namespace brulion::shell {

namespace {

/// One axis of the popup kept inside [lo, hi) — `margin` px from both ends when it fits, else
/// from `lo` on.
int clampAxis(int pos, int size, int lo, int hi, int margin)
{
    if (size > hi - lo - 2 * margin)
        return size > hi - lo ? lo : lo + (hi - lo - size) / 2;
    return std::clamp(pos, lo + margin, hi - margin - size);
}

} // namespace

TrayEdge trayEdge(QPoint anchor, const QRect &available)
{
    // Distances to the sides (negative outside the work area): the smallest one wins.
    const int bottom = available.top() + available.height() - anchor.y();
    const int right = available.left() + available.width() - anchor.x();
    const int top = anchor.y() - available.top();
    const int left = anchor.x() - available.left();
    TrayEdge edge = TrayEdge::Bottom;
    int best = bottom;
    if (right < best) {
        best = right;
        edge = TrayEdge::Right;
    }
    if (top < best) {
        best = top;
        edge = TrayEdge::Top;
    }
    if (left < best)
        edge = TrayEdge::Left;
    return edge;
}

QPoint trayPopupPosition(const QRect &icon, std::optional<QPoint> cursor, const QRect &available, QSize popup,
                         int margin)
{
    const int w = popup.width();
    const int h = popup.height();
    const int areaLeft = available.left();
    const int areaTop = available.top();
    const int areaRight = areaLeft + available.width(); // exclusive
    const int areaBottom = areaTop + available.height();

    QRect anchor;
    if (!icon.isEmpty())
        anchor = icon;
    else if (cursor)
        anchor = QRect(*cursor, QSize(1, 1));
    else
        return QPoint(clampAxis(areaRight - margin - w, w, areaLeft, areaRight, margin),
                      clampAxis(areaBottom - margin - h, h, areaTop, areaBottom, margin));

    const QPoint centre(anchor.left() + anchor.width() / 2, anchor.top() + anchor.height() / 2);
    const int anchorRight = anchor.left() + anchor.width();
    const int anchorBottom = anchor.top() + anchor.height();
    int x = centre.x() - w / 2;
    int y = centre.y() - h / 2;
    switch (trayEdge(centre, available)) {
    case TrayEdge::Bottom:
        y = std::min(anchor.top(), areaBottom) - margin - h;
        break;
    case TrayEdge::Top:
        y = std::max(anchorBottom, areaTop) + margin;
        break;
    case TrayEdge::Left:
        x = std::max(anchorRight, areaLeft) + margin;
        break;
    case TrayEdge::Right:
        x = std::min(anchor.left(), areaRight) - margin - w;
        break;
    }
    return QPoint(clampAxis(x, w, areaLeft, areaRight, margin), clampAxis(y, h, areaTop, areaBottom, margin));
}

QPoint trayPopupPositionOnScreen(const QRect &icon, std::optional<QPoint> cursor, QSize popup, int margin)
{
    const QPoint anchor = !icon.isEmpty() ? icon.center() : cursor.value_or(QPoint());
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        return anchor;
    QScreen *screen = nullptr;
    if (!icon.isEmpty() || cursor)
        screen = QGuiApplication::screenAt(anchor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return anchor;
    return trayPopupPosition(icon, cursor, screen->availableGeometry(), popup, margin);
}

bool trayPopupShouldReopen(qint64 msSinceHiddenByDeactivation)
{
    return msSinceHiddenByDeactivation < 0 || msSinceHiddenByDeactivation >= TrayPopupReopenGuardMs;
}

} // namespace brulion::shell
