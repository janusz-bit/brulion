// The task's box as TasksRow and PlanningItemRow draw it (TaskBox.qml): 18 px, rounded, a 1.5 px
// border; a closed task's filled, with a check (done) or a cross (cancelled). The views add its
// place, its role and its clicks.
#pragma once

#include "ui/atrament.h"

#include <QString>

namespace brulion::views::atr {

struct TaskBoxInk {
    atrament::Color open;   // an open task's border (Theme.borderStrong)
    atrament::Color closed; // a closed task's fill (Theme.accent)
    atrament::Color mark;   // its check or cross (the background)
};

inline constexpr float kTaskBoxSize = 18;

atrament::Div taskBoxShape(const QString &status, const TaskBoxInk &ink);

} // namespace brulion::views::atr
