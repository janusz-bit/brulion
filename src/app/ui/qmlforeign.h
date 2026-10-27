// QML registration of the GUI-free helpers in brulion_ui_helpers (src/app/ui/CMakeLists.txt):
// the classes themselves know nothing about QML; these wrappers make them singletons of the
// app's QML module "Brulion" (DatesHelper, TextHelper, DiffHelper, ThemePalette), so qmllint/qmlcachegen
// still see their full API (from the library's extracted metatypes); KeyboardInsets and
// GestureInsets are the instances main() creates and feeds (keyboardsources.h). Also TouchFlick
// (brulion_ui_touch): an attached type, `ListView { TouchFlick.enabled: true }`; and RowScroll
// (brulion_ui_scroll), the scroll bar geometry of BRowScrollBar.
#pragma once

#include "dateshelper.h"
#include "diffhelper.h"
#include "gestureinsets.h"
#include "keyboardinsets.h"
#include "rowscroll.h"
#include "texthelper.h"
#include "themepalette.h"
#include "touchflick.h"

#include <QJSEngine>
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

struct ThemePaletteForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::ThemePalette)
    QML_NAMED_ELEMENT(ThemePalette)
    QML_SINGLETON
public:
    static ThemePalette *create(QQmlEngine *, QJSEngine *) { return new ThemePalette; }
};

struct KeyboardInsetsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::KeyboardInsets)
    QML_NAMED_ELEMENT(KeyboardInsets)
    QML_SINGLETON
public:
    /// The app's instance (main() creates it before the engine), else one of this engine's own
    /// (QML tooling: no keyboard reported).
    static KeyboardInsets *create(QQmlEngine *, QJSEngine *)
    {
        if (KeyboardInsets *insets = KeyboardInsets::instance()) {
            QJSEngine::setObjectOwnership(insets, QJSEngine::CppOwnership);
            return insets;
        }
        return new KeyboardInsets;
    }
};

struct GestureInsetsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::GestureInsets)
    QML_NAMED_ELEMENT(GestureInsets)
    QML_SINGLETON
public:
    /// The app's instance (main() creates it before the engine), else one of this engine's own
    /// (QML tooling: no gesture zones).
    static GestureInsets *create(QQmlEngine *, QJSEngine *)
    {
        if (GestureInsets *insets = GestureInsets::instance()) {
            QJSEngine::setObjectOwnership(insets, QJSEngine::CppOwnership);
            return insets;
        }
        return new GestureInsets;
    }
};

struct TouchFlickForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::TouchFlick)
    QML_NAMED_ELEMENT(TouchFlick)
    QML_UNCREATABLE("TouchFlick is an attached property: ListView { TouchFlick.enabled: true }")
    QML_ATTACHED(brulion::ui::TouchFlick)
};

struct RowScrollForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::RowScroll)
    QML_NAMED_ELEMENT(RowScroll)
};

} // namespace brulion::ui
