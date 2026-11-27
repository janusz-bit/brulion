#include "ui/edgescroll.h"

#include <algorithm>

namespace brulion::atrament {

float edgeScrollSpeed(float y, float top, float bottom)
{
    const float zone = std::max(16.0f, std::min(56.0f, (bottom - top) / 5));
    if (y < top + zone)
        return -std::min(40.0f, (top + zone - y) / 2 + 2);
    if (y > bottom - zone)
        return std::min(40.0f, (y - (bottom - zone)) / 2 + 2);
    return 0;
}

void EdgeScroller::update(App &app, float speed, std::function<void(float)> step)
{
    m_step = std::move(step);
    if (speed == 0) {
        stop();
        return;
    }
    const bool wasRunning = running();
    m_speed = speed;
    if (!wasRunning)
        arm(app);
}

void EdgeScroller::stop()
{
    m_speed = 0;
    m_timer = {};
}

void EdgeScroller::arm(App &app)
{
    m_timer = app.setTimeout(kEdgeScrollInterval, [this](App &a) {
        if (!running())
            return;
        // (The step may stop the scroller, or move the pointer's speed.)
        const std::function<void(float)> step = m_step;
        if (step)
            step(m_speed);
        if (running())
            arm(a);
    });
}

} // namespace brulion::atrament
