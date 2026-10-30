// FilePicker's backends (internal: filepicker.cpp and tests/app/filepicker).
#pragma once

#include "filepicker.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

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
/// Qt Widgets' QFileDialog (desktop builds running a QApplication), nullptr elsewhere.
std::unique_ptr<FilePickerBackend> createWidgetFilePicker();

} // namespace brulion::ui
