#pragma once

// Scrolling while a drag is held near a view's edge (docs/ATRAMENT.md "Touch", Edge scrolling): a
// selection dragged past what is shown, a row being moved to a place off screen — the view under
// the pointer scrolls by itself, faster the deeper the pointer is in the zone along its edge, and
// stops when the pointer leaves the zone or the drag ends. The speed is the QML editor's
// (BlockEditor.updateAutoScroll): a zone of a fifth of the view (16 to 56 px), from 2 px a frame
// at its inner side to at most 40.
//
//   EdgeScroller scroller;
//   scroller.update(app, edgeScrollSpeed(pointer.y, view.top(), view.bottom()), [this](float dy) {
//       if (handle.scrollBy(dy)) … // the list moved: what the drag points at changed
//   });
//   scroller.stop(); // the drag ended

#include "ui/app.h"

#include <chrono>
#include <functional>

namespace brulion::atrament {

// One step every frame of this length while the pointer stays in an edge's zone.
inline constexpr std::chrono::milliseconds kEdgeScrollInterval{16};

// px per step for a pointer at `y` in a view from `top` to `bottom` (window px): negative near the
// top (towards the start), positive near the bottom, 0 between the zones (also past nothing: a
// view too short has zones of 16 px).
float edgeScrollSpeed(float y, float top, float bottom);

// The steps of a drag held at an edge: `step(speed)` every kEdgeScrollInterval on the app's timers
// (a test platform's virtual clock in tests) while the speed is not 0.
class EdgeScroller {
public:
    // The pointer moved: the speed there (0 stops the steps; another speed keeps their pace).
    void update(App &app, float speed, std::function<void(float speed)> step);
    void stop();
    bool running() const { return m_speed != 0; }
    float speed() const { return m_speed; }

private:
    void arm(App &app);

    float m_speed = 0;
    std::function<void(float)> m_step;
    Subscription m_timer;
};

} // namespace brulion::atrament
