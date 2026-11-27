// The app's icons as Atrament vector icons (docs/ATRAMENT.md "Vector paths and icons"): BIcon's
// Lucide SVGs (assets/icons, ":/icons/<name>.svg" in the app) read by Atrament's SVG subset
// (scene/vectoricon.h) once per name and kept — a page's app icon in the text it links from
// (RenderedObject::Icon, renderedspans.cpp) is drawn from it in the link's colour. A name comes
// from a note: one that could leave the folder (IconImageProvider::validName's rule) is none.
#pragma once

#include "scene/vectoricon.h"

#include <QString>

#include <memory>

namespace brulion::editor::atr {

// The icon called `name`; nullptr when there is none (an unknown or invalid name, a file Atrament
// cannot read).
std::shared_ptr<const atrament::VectorIcon> pageIcon(const QString &name);
// Where the SVGs are read from (":/icons" by default; tests: the source tree's assets/icons). Drops
// the icons read so far.
void setPageIconFolder(const QString &folder);

} // namespace brulion::editor::atr
