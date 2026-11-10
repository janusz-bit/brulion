// QML registration of the GUI-free helpers in brulion_ui_helpers (src/app/ui/CMakeLists.txt):
// the classes themselves know nothing about QML; these wrappers make them singletons of the
// app's QML module "Brulion" (DatesHelper, TextHelper, Js, DiffHelper, ThemePalette, UiModels; the types ThemeColors
// and PaletteFilter), so qmllint/qmlcachegen still see their full API (from the library's extracted metatypes);
// KeyboardInsets and GestureInsets are the instances main() creates and feeds (keyboardsources.h). Also TouchFlick
// (brulion_ui_touch): an attached type, `ListView { TouchFlick.enabled: true }`; and RowScroll
// (brulion_ui_scroll), the scroll bar geometry of BRowScrollBar; SmoothScroll (brulion_ui_smoothscroll),
// the wheel's and the scroll bars' browser-like scrolling (the instance main() installs); and FilePicker (brulion_ui_files),
// the system's file picker (`FilePicker { fileMode: FilePicker.OpenFiles }`).
#pragma once

#include "dateshelper.h"
#include "diffhelper.h"
#include "filepicker.h"
#include "gestureinsets.h"
#include "jshelper.h"
#include "later.h"
#include "keyboardinsets.h"
#include "mapholder.h"
#include "rowscroll.h"
#include "smoothscroll.h"
#include "texthelper.h"
#include "themepalette.h"
#include "touchflick.h"
#include "uimodels.h"

#include <QJSEngine>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;

namespace brulion::ui {

/// `object` (an instance main() created before the engine) as a singleton the engine does not own.
template<typename T>
T *cppOwned(T *object)
{
    QJSEngine::setObjectOwnership(object, QJSEngine::CppOwnership);
    return object;
}

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

struct LaterForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::Later)
    QML_NAMED_ELEMENT(Later)
};

struct JsHelperForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::JsHelper)
    QML_NAMED_ELEMENT(Js)
    QML_SINGLETON
public:
    static JsHelper *create(QQmlEngine *, QJSEngine *) { return new JsHelper; }
};

// A QVariantMap in a typed property, for compiled QML (mapholder.h).
struct MapHolderForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::MapHolder)
    QML_NAMED_ELEMENT(MapHolder)
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

struct UiModelsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::UiModels)
    QML_NAMED_ELEMENT(UiModels)
    QML_SINGLETON
public:
    static UiModels *create(QQmlEngine *, QJSEngine *) { return new UiModels; }
};

struct ToastListModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::ToastListModel)
    QML_NAMED_ELEMENT(ToastListModel)
};

struct PaletteFilterForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::PaletteFilter)
    QML_NAMED_ELEMENT(PaletteFilter)
};

struct ThemeColorsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::ThemeColors)
    QML_NAMED_ELEMENT(ThemeColors)
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
        KeyboardInsets *insets = KeyboardInsets::instance();
        return insets ? cppOwned(insets) : new KeyboardInsets;
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
        GestureInsets *insets = GestureInsets::instance();
        return insets ? cppOwned(insets) : new GestureInsets;
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

struct SmoothScrollForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::SmoothScroll)
    QML_NAMED_ELEMENT(SmoothScroll)
    QML_SINGLETON
public:
    /// The application's filter (installed by main(), else on first use here).
    static SmoothScroll *create(QQmlEngine *, QJSEngine *) { return cppOwned(SmoothScroll::instance()); }
};

struct FilePickerForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ui::FilePicker)
    QML_NAMED_ELEMENT(FilePicker)
};

} // namespace brulion::ui
