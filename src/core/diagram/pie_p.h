// Pie charts (`pie`): the model, the parser and the scene. Internal; the tests read the model.
#pragma once

#include "diagram/common_p.h"
#include "diagram/diagram.h"

namespace brulion::diagram::detail {

struct PieSlice {
    QString label;
    double value = 0;
    int line = 0;
};

struct PieChart {
    QString title;
    bool showData = false;
    QList<PieSlice> slices; ///< values above zero only, in the order written
};

Outcome<PieChart> parsePie(QStringView source);
/// Each slice's angle in degrees (they add up to 360).
QList<qreal> pieAngles(const PieChart &chart);
Scene drawPie(const PieChart &chart, const Metrics &metrics);

} // namespace brulion::diagram::detail
