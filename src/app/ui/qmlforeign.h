// QML registration of the GUI-free helpers in brulion_ui_helpers (src/app/ui/CMakeLists.txt):
// the classes themselves know nothing about QML; these wrappers make them singletons of the
// app's QML module "Brulion" (DatesHelper, TextHelper, DiffHelper), so qmllint/qmlcachegen
// still see their full API (from the library's extracted metatypes).
#pragma once

#include "dateshelper.h"
#include "diffhelper.h"
#include "texthelper.h"

#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace brulion::ui {

struct DatesHelperForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::DatesHelper)
    QML_NAMED_ELEMENT(DatesHelper)
    QML_SINGLETON
public:
    static DatesHelper *create(QQmlEngine *, QJSEngine *) { return new DatesHelper; }
};

struct TextHelperForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::TextHelper)
    QML_NAMED_ELEMENT(TextHelper)
    QML_SINGLETON
public:
    static TextHelper *create(QQmlEngine *, QJSEngine *) { return new TextHelper; }
};

struct DiffHelperForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::DiffHelper)
    QML_NAMED_ELEMENT(DiffHelper)
    QML_SINGLETON
public:
    static DiffHelper *create(QQmlEngine *, QJSEngine *) { return new DiffHelper; }
};

} // namespace brulion::ui
