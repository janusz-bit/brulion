// FilePicker's backends (internal: filepicker.cpp and tests/app/filepicker).
#pragma once

#include "filepicker.h"

#include <QList>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <memory>

class QWindow;

namespace brulion::ui {

/// What a backend is asked to show (FilePicker::request()).
struct FilePickerRequest {
    FilePicker::Mode mode = FilePicker::OpenFile;
    QString title;
    QStringList nameFilters; ///< cleaned (FilePicker::cleanNameFilters)
    QString defaultSuffix;
    QUrl folder;      ///< the folder to start in (may not exist)
    QUrl initialFile; ///< SaveFile only: the suggested file
    bool dark = false;
    QVariantMap colors; ///< the app's colours (Theme.colors), for the widget fallback
};

/// One way of showing a file picker. show() returns false when this backend cannot show one
/// (the next backend is tried then); afterwards exactly one of accepted() / rejected() follows,
/// unless hide() came first.
class FilePickerBackend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual bool show(const FilePickerRequest &request, QWindow *parent) = 0;
    /// Closes the picker without a signal.
    virtual void hide() = 0;

Q_SIGNALS:
    void accepted(const QList<QUrl> &files);
    void rejected();
};

/// The platform theme's own file dialog (QPlatformFileDialogHelper: Android's document picker,
/// Windows' IFileDialog, GTK, KDE, the XDG portal), nullptr when the theme has none.
std::unique_ptr<FilePickerBackend> createNativeFilePicker();
/// Qt Widgets' QFileDialog (desktop builds running a QApplication), nullptr elsewhere. It is
/// drawn in the Fusion style (also its child widgets: the desktop's own style — Breeze, Kvantum —
/// would ignore the palette) with widgetDialogPalette().
std::unique_ptr<FilePickerBackend> createWidgetFilePicker();
/// The widget dialog's palette in Brulion's colours: `colors` as Theme.colors names them ("#rrggbb"
/// strings or QColors), each one missing or malformed taken from ThemePalette's defaults of the
/// mode (`dark`). Window = bg, Base = bgElevated (the text fields' colour), AlternateBase =
/// bgSunken, Button = bgElevated2, the texts fg (placeholders and disabled ones fgFaint),
/// Highlight = accentSolid under white text, Accent = accent, Link = link, the bevels from the
/// border colours.
QPalette widgetDialogPalette(bool dark, const QVariantMap &colors);

} // namespace brulion::ui
