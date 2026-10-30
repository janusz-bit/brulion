// FilePicker — the system's file picker for QML, without QtQuick.Dialogs (docs/UI.md "File
// pickers", docs/PERFORMANCE.md L9: QtQuick.Dialogs and the run-time style selection put every Qt
// Quick Controls style into the packages).
//
//   FilePicker {
//       id: saveDialog
//       fileMode: FilePicker.SaveFile
//       title: qsTr("Save the export")
//       nameFilters: [qsTr("ZIP archives (*.zip)")]
//       onAccepted: exporter.saveTo(selectedFile)
//   }
//   saveDialog.selectedFile = suggestion; saveDialog.open()
//
// Which picker: the platform theme's own one first — exactly what QtQuick.Dialogs showed —
// Android's document picker (ACTION_OPEN_DOCUMENT / _TREE / ACTION_CREATE_DOCUMENT with the
// suggested name as EXTRA_TITLE, persistable permissions), Windows' IFileDialog, the GTK / KDE /
// XDG-portal dialog on Linux where the platform theme has one; else Qt Widgets' QFileDialog
// (desktop builds, which have a QApplication for the tray; also offscreen). Asynchronous, never
// exec(). Not in the browser (`available` false): there the controllers use
// platform/wasm/wasmfiles.h (the browser's picker reads the files' bytes, not URLs).
//
// The QML API keeps QtQuick.Dialogs' names (fileMode, selectedFile(s), selectedFolder,
// currentFolder, nameFilters, defaultSuffix, title, visible, open/reject, accepted/rejected):
// the self-test of the real app (src/app/io/ioselftest.cpp) watches `visible` and `selectedFile`.
// Registered as "FilePicker" by qmlforeign.h; library brulion_ui_files; tests: tests/app/filepicker.
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <vector>

class QWindow;

namespace brulion::ui {

class FilePickerBackend; // filepicker_p.h
struct FilePickerRequest;

class FilePicker : public QObject {
    Q_OBJECT
    Q_PROPERTY(Mode fileMode READ fileMode WRITE setFileMode NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY optionsChanged FINAL)
    /// Qt's filter syntax ("Obrazy (*.png *.jpg)"); empty entries are dropped. Empty: every file.
    Q_PROPERTY(QStringList nameFilters READ nameFilters WRITE setNameFilters NOTIFY optionsChanged FINAL)
    /// SaveFile: appended to a picked local file name without a dot ("Notatka" → "Notatka.md").
    Q_PROPERTY(QString defaultSuffix READ defaultSuffix WRITE setDefaultSuffix NOTIFY optionsChanged FINAL)
    /// The folder the picker starts in (used when it exists; Android: any URL, as a hint).
    Q_PROPERTY(QUrl currentFolder READ currentFolder WRITE setCurrentFolder NOTIFY optionsChanged FINAL)
    /// In: the file a save picker suggests (Android: its name as EXTRA_TITLE). Out: the result.
    Q_PROPERTY(QUrl selectedFile READ selectedFile WRITE setSelectedFile NOTIFY selectedFilesChanged FINAL)
    Q_PROPERTY(QList<QUrl> selectedFiles READ selectedFiles NOTIFY selectedFilesChanged FINAL)
    /// OpenFolder: the picked folder (a file URL, or an Android document tree content:// URL).
    Q_PROPERTY(QUrl selectedFolder READ selectedFile NOTIFY selectedFilesChanged FINAL)
    /// Between open() and accepted()/rejected() (QtQuick.Dialogs' name: ioselftest watches it).
    Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged FINAL)
    /// False in the browser, where the controllers show the browser's own picker.
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    /// The fallback widget dialog's colours follow the app's theme: `dark` (Theme.dark) and
    /// `colors` (Theme.colors — bg, bgElevated, fg, accentSolid, … as "#rrggbb"; missing or
    /// malformed entries: Brulion's default colours of the mode). widgetDialogPalette().
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QVariantMap colors READ colors WRITE setColors NOTIFY optionsChanged FINAL)

public:
    enum Mode { OpenFile, OpenFiles, OpenFolder, SaveFile };
    Q_ENUM(Mode)

    explicit FilePicker(QObject *parent = nullptr);
    ~FilePicker() override; // closes an open picker, without signals

    /// Shows the picker. Already open: nothing (the system's picker is in front; Android has only
    /// one — a second request would orphan the first one's result).
    Q_INVOKABLE void open();
    /// Closes an open picker as cancelled: rejected() (tests, leaving the view).
    Q_INVOKABLE void reject();

    Mode fileMode() const { return m_mode; }
    void setFileMode(Mode mode);
    QString title() const { return m_title; }
    void setTitle(const QString &title);
    QStringList nameFilters() const { return m_nameFilters; }
    void setNameFilters(const QStringList &filters);
    QString defaultSuffix() const { return m_defaultSuffix; }
    void setDefaultSuffix(const QString &suffix);
    QUrl currentFolder() const { return m_currentFolder; }
    void setCurrentFolder(const QUrl &folder);
    QUrl selectedFile() const { return m_selected.value(0); }
    void setSelectedFile(const QUrl &file);
    QList<QUrl> selectedFiles() const { return m_selected; }
    bool isVisible() const { return m_visible; }
    static bool available();
    bool dark() const { return m_dark; }
    void setDark(bool dark);
    QVariantMap colors() const { return m_colors; }
    void setColors(const QVariantMap &colors);

    /// The window the picker is modal to: the one set here, else the window of the nearest
    /// QQuickItem among the QObject parents (nullptr: none — application-modal).
    QWindow *parentWindow() const;
    void setParentWindow(QWindow *window);

    /// What open() asks the backends for (also what tests check).
    FilePickerRequest request() const;

    /// Backends to try in order for each open(): the first whose show() succeeds is used.
    using BackendFactory = std::function<std::vector<std::unique_ptr<FilePickerBackend>>(Mode)>;
    /// Tests: every open() uses backends from `factory` instead of the real ones ({}: the real ones).
    static void setBackendFactoryForTests(BackendFactory factory);

    /// `file` with `.suffix` appended when its name has no dot — never for content:// URLs
    /// (Android names documents itself), folders (a path ending in '/') or an empty suffix.
    static QUrl withDefaultSuffix(const QUrl &file, const QString &suffix);
    /// The filters without empty entries (a malformed one is kept: the pickers tolerate them).
    static QStringList cleanNameFilters(const QStringList &filters);

Q_SIGNALS:
    void accepted();
    void rejected();
    void visibleChanged();
    void selectedFilesChanged();
    void optionsChanged();

private:
    void finish(FilePickerBackend *backend, const QList<QUrl> &files);
    void cancelled(FilePickerBackend *backend);
    void releaseBackend();
    void setVisible(bool visible);

    Mode m_mode = OpenFile;
    QString m_title;
    QStringList m_nameFilters;
    QString m_defaultSuffix;
    QUrl m_currentFolder;
    QList<QUrl> m_selected;
    bool m_dark = false;
    QVariantMap m_colors;
    bool m_visible = false;
    QPointer<QWindow> m_parentWindow;
    bool m_parentWindowSet = false;
    std::unique_ptr<FilePickerBackend> m_backend;
};

} // namespace brulion::ui
