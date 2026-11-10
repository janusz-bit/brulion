#include "traypopup.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>

#include <algorithm>
#include <array>

namespace brulion::shell {

namespace {

/// One axis of the popup kept inside [lo, hi) — `margin` px from both ends when it fits so, else
/// centred, or from `lo` on when it is larger.
int clampAxis(int pos, int size, int lo, int hi, int margin)
{
    if (size > hi - lo - 2 * margin)
        return size > hi - lo ? lo : lo + (hi - lo - size) / 2;
    return std::clamp(pos, lo + margin, hi - margin - size);
}

} // namespace

TrayEdge trayEdge(QPoint anchor, const QRect &available)
{
    // Distances to the sides (negative outside the work area): the smallest one wins, the first of
    // equal ones.
    struct Side {
        int distance;
        TrayEdge edge;
    };
    const std::array<Side, 4> sides{{{available.top() + available.height() - anchor.y(), TrayEdge::Bottom},
                                     {available.left() + available.width() - anchor.x(), TrayEdge::Right},
                                     {anchor.y() - available.top(), TrayEdge::Top},
                                     {anchor.x() - available.left(), TrayEdge::Left}}};
    return std::ranges::min_element(sides, {}, &Side::distance)->edge;
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
    // With neither an icon nor a cursor: the work area's bottom-right corner.
    int x = areaRight - margin - w;
    int y = areaBottom - margin - h;
    if (!icon.isEmpty() || cursor) {
        const QRect anchor = !icon.isEmpty() ? icon : QRect(*cursor, QSize(1, 1));
        const QPoint centre(anchor.left() + anchor.width() / 2, anchor.top() + anchor.height() / 2);
        x = centre.x() - w / 2;
        y = centre.y() - h / 2;
        switch (trayEdge(centre, available)) {
        case TrayEdge::Bottom: y = std::min(anchor.top(), areaBottom) - margin - h; break;
        case TrayEdge::Top: y = std::max(anchor.top() + anchor.height(), areaTop) + margin; break;
        case TrayEdge::Left: x = std::max(anchor.left() + anchor.width(), areaLeft) + margin; break;
        case TrayEdge::Right: x = std::min(anchor.left(), areaRight) - margin - w; break;
        }
    }
    return QPoint(clampAxis(x, w, areaLeft, areaRight, margin), clampAxis(y, h, areaTop, areaBottom, margin));
}

QPoint trayPopupPositionOnScreen(const QRect &icon, std::optional<QPoint> cursor, QSize popup, int margin)
{
    const QPoint anchor = !icon.isEmpty() ? icon.center() : cursor.value_or(QPoint());
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        return anchor;
    QScreen *screen = (!icon.isEmpty() || cursor) ? QGuiApplication::screenAt(anchor) : nullptr;
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen ? trayPopupPosition(icon, cursor, screen->availableGeometry(), popup, margin) : anchor;
}

bool trayPopupShouldReopen(qint64 msSinceHiddenByDeactivation, std::optional<QPoint> cursorAtHide,
                           const QRect &icon, std::optional<QPoint> cursorAtClick)
{
    if (msSinceHiddenByDeactivation < 0)
        return true;
    if (msSinceHiddenByDeactivation < TrayPopupReopenGuardMs)
        return false;
    const bool sameClick = cursorAtHide
                           && ((!icon.isEmpty() && icon.contains(*cursorAtHide))
                               || (cursorAtClick
                                   && (*cursorAtClick - *cursorAtHide).manhattanLength() <= TrayPopupClickSlopPx));
    return !sameClick || msSinceHiddenByDeactivation >= TrayPopupHeldClickMs;
}

} // namespace brulion::shell
