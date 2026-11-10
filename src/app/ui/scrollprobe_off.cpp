// startScrollProbe() of a build without the scroll probe (BRULION_SCROLL_PROBE=OFF, the shipped
// Android and browser builds: src/app/CMakeLists.txt). It only says so.
#include "scrollprobe.h"

#include <QDebug>
#include <QStringList>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ui {

void startScrollProbe(QQmlApplicationEngine *, const QStringList &arguments)
{
    if (std::ranges::any_of(arguments, [](const QString &arg) { return arg.startsWith(u"--scroll-probe"_s); }))
        qWarning("--scroll-probe: not part of this build (BRULION_SCROLL_PROBE=OFF; build with "
                 "RelWithDebInfo or -DBRULION_SCROLL_PROBE=ON)");
}

} // namespace brulion::ui
