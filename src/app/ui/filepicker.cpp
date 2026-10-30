#include "filepicker.h"

#include "filepicker_p.h"
#include "log/categories.h"

#include <QCoreApplication>
#include <QDir>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWindow>

#ifndef Q_OS_WASM
#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatformdialoghelper.h>
#include <qpa/qplatformtheme.h>
#endif

#ifdef BRULION_FILEPICKER_WIDGETS
#include <QApplication>
#include <QFileDialog>
#include <QPalette>
#endif

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

FilePicker::BackendFactory &backendFactory()
{
    static FilePicker::BackendFactory factory;
    return factory;
}

/// Whether a picker can start in `folder`: an existing local folder; Android's document URLs
/// (content://) are passed on as hints, as QtQuick.Dialogs did.
[[maybe_unused]] bool usableFolder(const QUrl &folder)
{
    if (folder.isEmpty() || !folder.isValid())
        return false;
    if (folder.isLocalFile())
        return QDir(folder.toLocalFile()).exists();
    return folder.scheme() == u"content";
}

#ifndef Q_OS_WASM

/// The platform theme's file dialog helper — the code path of QtQuick.Dialogs' FileDialog and
/// FolderDialog (qquickabstractdialog.cpp, qquickfiledialog.cpp of Qt 6.11), without its
/// non-native Qt Quick fallback.
class NativeFilePicker final : public FilePickerBackend {
public:
    explicit NativeFilePicker(QPlatformFileDialogHelper *helper) : m_helper(helper)
    {
        // Android emits these from its activity-result callback (another thread): queued here.
        connect(m_helper.get(), &QPlatformDialogHelper::accept, this, [this] {
            if (!m_shown)
                return;
            m_shown = false;
            const QList<QUrl> files = m_helper->selectedFiles(); // before hide(), as Qt Quick does
            m_helper->hide();
            Q_EMIT accepted(files);
        });
        connect(m_helper.get(), &QPlatformDialogHelper::reject, this, [this] {
            if (!m_shown)
                return;
            m_shown = false;
            m_helper->hide();
            Q_EMIT rejected();
        });
    }
    ~NativeFilePicker() override { hide(); }

    bool show(const FilePickerRequest &request, QWindow *parent) override
    {
        const QSharedPointer<QFileDialogOptions> options = QFileDialogOptions::create();
        switch (request.mode) {
        case FilePicker::OpenFile:
            options->setFileMode(QFileDialogOptions::ExistingFile);
            options->setAcceptMode(QFileDialogOptions::AcceptOpen);
            break;
        case FilePicker::OpenFiles:
            options->setFileMode(QFileDialogOptions::ExistingFiles);
            options->setAcceptMode(QFileDialogOptions::AcceptOpen);
            break;
        case FilePicker::OpenFolder:
            options->setFileMode(QFileDialogOptions::Directory);
            options->setAcceptMode(QFileDialogOptions::AcceptOpen);
            options->setOption(QFileDialogOptions::ShowDirsOnly);
            break;
        case FilePicker::SaveFile:
            options->setFileMode(QFileDialogOptions::AnyFile);
            options->setAcceptMode(QFileDialogOptions::AcceptSave);
            break;
        }
        options->setWindowTitle(request.title);
        if (!request.nameFilters.isEmpty()) {
            options->setNameFilters(request.nameFilters);
            options->setInitiallySelectedNameFilter(request.nameFilters.first());
        }
        if (!request.defaultSuffix.isEmpty())
            options->setDefaultSuffix(request.defaultSuffix);
        if (!request.folder.isEmpty())
            options->setInitialDirectory(request.folder);
        // Android names the new document after this (EXTRA_TITLE).
        if (!request.initialFile.isEmpty())
            options->setInitiallySelectedFiles({request.initialFile});
        m_helper->setOptions(options);
        if (!request.nameFilters.isEmpty())
            m_helper->selectNameFilter(request.nameFilters.first());
        // Both set: the file wins (QQuickFileDialog::onShow).
        if (!request.initialFile.isEmpty())
            m_helper->selectFile(request.initialFile);
        else if (usableFolder(request.folder))
            m_helper->setDirectory(request.folder);
        m_shown = m_helper->show(Qt::Dialog, Qt::WindowModal, parent);
        return m_shown;
    }

    void hide() override
    {
        if (!m_shown)
            return;
        m_shown = false;
        // Android: also unregisters the activity-result listener, so a late result goes nowhere.
        m_helper->hide();
    }

private:
    std::unique_ptr<QPlatformFileDialogHelper> m_helper;
    bool m_shown = false;
};

#endif // !Q_OS_WASM

#ifdef BRULION_FILEPICKER_WIDGETS

/// Qt Widgets' own file dialog where the platform theme has none (a Linux desktop without a GTK,
/// KDE or portal theme; offscreen). Its colours follow the app's theme.
class WidgetFilePicker final : public FilePickerBackend {
public:
    ~WidgetFilePicker() override { hide(); }

    bool show(const FilePickerRequest &request, QWindow *parent) override
    {
        auto *dialog = new QFileDialog(nullptr, request.title);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setOption(QFileDialog::DontUseNativeDialog); // the native one was tried first
        switch (request.mode) {
        case FilePicker::OpenFile: dialog->setFileMode(QFileDialog::ExistingFile); break;
        case FilePicker::OpenFiles: dialog->setFileMode(QFileDialog::ExistingFiles); break;
        case FilePicker::OpenFolder:
            dialog->setFileMode(QFileDialog::Directory);
            dialog->setOption(QFileDialog::ShowDirsOnly);
            break;
        case FilePicker::SaveFile:
            dialog->setFileMode(QFileDialog::AnyFile);
            dialog->setAcceptMode(QFileDialog::AcceptSave);
            break;
        }
        if (!request.nameFilters.isEmpty())
            dialog->setNameFilters(request.nameFilters);
        if (!request.defaultSuffix.isEmpty())
            dialog->setDefaultSuffix(request.defaultSuffix);
        if (request.folder.isLocalFile() && usableFolder(request.folder))
            dialog->setDirectoryUrl(request.folder);
        if (!request.initialFile.isEmpty())
            dialog->selectUrl(request.initialFile);
        if (request.dark)
            dialog->setPalette(darkPalette());
        dialog->setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
        if (parent) {
            dialog->winId(); // creates the native window, so that it can have a transient parent
            if (QWindow *handle = dialog->windowHandle())
                handle->setTransientParent(parent);
        }
        connect(dialog, &QDialog::finished, this, [this, dialog](int result) {
            m_dialog.clear();
            if (result == QDialog::Accepted)
                Q_EMIT accepted(dialog->selectedUrls());
            else
                Q_EMIT rejected();
        });
        m_dialog = dialog;
        dialog->open(); // never exec()
        return true;
    }

    void hide() override
    {
        QFileDialog *dialog = m_dialog.data();
        if (!dialog)
            return;
        m_dialog.clear();
        disconnect(dialog, nullptr, this, nullptr);
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        dialog->hide();
        dialog->deleteLater();
    }

private:
    static QPalette darkPalette()
    {
        QPalette p;
        const QColor text(0xe6, 0xe6, 0xe6);
        const QColor disabled(0x7a, 0x7a, 0x7a);
        p.setColor(QPalette::Window, QColor(0x2b, 0x2b, 0x2b));
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
        p.setColor(QPalette::AlternateBase, QColor(0x26, 0x26, 0x26));
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x8a));
        p.setColor(QPalette::Button, QColor(0x35, 0x35, 0x35));
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::BrightText, Qt::white);
        p.setColor(QPalette::Highlight, QColor(0x3d, 0x7b, 0xd9));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, QColor(0x6e, 0xa8, 0xfe));
        p.setColor(QPalette::ToolTipBase, QColor(0x35, 0x35, 0x35));
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Light, QColor(0x45, 0x45, 0x45));
        p.setColor(QPalette::Midlight, QColor(0x3a, 0x3a, 0x3a));
        p.setColor(QPalette::Mid, QColor(0x30, 0x30, 0x30));
        p.setColor(QPalette::Dark, QColor(0x1a, 0x1a, 0x1a));
        p.setColor(QPalette::Shadow, QColor(0x10, 0x10, 0x10));
        for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            p.setColor(QPalette::Disabled, role, disabled);
        return p;
    }

    QPointer<QFileDialog> m_dialog;
};

#endif // BRULION_FILEPICKER_WIDGETS

} // namespace

std::unique_ptr<FilePickerBackend> createNativeFilePicker()
{
#ifdef Q_OS_WASM
    // Qt's browser helper hands out weblocalfile: URLs that need synchronous reads (JSPI or
    // asyncify), which the single-threaded build has neither of: platform/wasm/wasmfiles.h.
    return nullptr;
#else
    if (QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs))
        return nullptr;
    const QPlatformTheme *theme = QGuiApplicationPrivate::platformTheme();
    if (!theme || !theme->usePlatformNativeDialog(QPlatformTheme::FileDialog))
        return nullptr;
    QPlatformDialogHelper *helper = theme->createPlatformDialogHelper(QPlatformTheme::FileDialog);
    auto *fileHelper = qobject_cast<QPlatformFileDialogHelper *>(helper);
    if (!fileHelper) {
        delete helper;
        return nullptr;
    }
    return std::make_unique<NativeFilePicker>(fileHelper);
#endif
}

std::unique_ptr<FilePickerBackend> createWidgetFilePicker()
{
#ifdef BRULION_FILEPICKER_WIDGETS
    if (qobject_cast<QApplication *>(QCoreApplication::instance()))
        return std::make_unique<WidgetFilePicker>();
#endif
    return nullptr;
}

// --- FilePicker -------------------------------------------------------------------------------

FilePicker::FilePicker(QObject *parent) : QObject(parent) { }

FilePicker::~FilePicker()
{
    releaseBackend();
}

void FilePicker::open()
{
    if (m_visible)
        return;
    const FilePickerRequest req = request();
    std::vector<std::unique_ptr<FilePickerBackend>> candidates;
    if (backendFactory()) {
        candidates = backendFactory()(m_mode);
    } else if (available()) {
        candidates.push_back(createNativeFilePicker());
        candidates.push_back(createWidgetFilePicker());
    }
    QWindow *parent = parentWindow();
    for (std::unique_ptr<FilePickerBackend> &candidate : candidates) {
        FilePickerBackend *backend = candidate.get();
        if (!backend)
            continue;
        connect(backend, &FilePickerBackend::accepted, this,
                [this, backend](const QList<QUrl> &files) { finish(backend, files); });
        connect(backend, &FilePickerBackend::rejected, this, [this, backend] { cancelled(backend); });
        m_backend = std::move(candidate);
        if (backend->show(req, parent)) {
            setVisible(true);
            return;
        }
        disconnect(backend, nullptr, this, nullptr);
        m_backend.reset(); // it showed nothing
    }
    qCWarning(logging::lcPlatform) << "No file picker could be shown (" << req.title << ")";
    Q_EMIT rejected();
}

void FilePicker::reject()
{
    if (!m_visible)
        return;
    releaseBackend();
    setVisible(false);
    Q_EMIT rejected();
}

void FilePicker::finish(FilePickerBackend *backend, const QList<QUrl> &files)
{
    if (backend != m_backend.get())
        return;
    QList<QUrl> result;
    for (const QUrl &file : files) {
        if (!file.isEmpty())
            result.append(m_mode == SaveFile ? withDefaultSuffix(file, m_defaultSuffix) : file);
    }
    releaseBackend();
    if (result.isEmpty()) { // some portals accept with nothing chosen
        setVisible(false);
        Q_EMIT rejected();
        return;
    }
    m_selected = result;
    Q_EMIT selectedFilesChanged();
    setVisible(false);
    Q_EMIT accepted();
}

void FilePicker::cancelled(FilePickerBackend *backend)
{
    if (backend != m_backend.get())
        return;
    releaseBackend();
    setVisible(false);
    Q_EMIT rejected();
}

void FilePicker::releaseBackend()
{
    if (!m_backend)
        return;
    // Possibly inside the backend's own signal: deleted later, after hide() without signals.
    FilePickerBackend *backend = m_backend.release();
    disconnect(backend, nullptr, this, nullptr);
    backend->hide();
    backend->deleteLater();
}

void FilePicker::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    Q_EMIT visibleChanged();
}

void FilePicker::setFileMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    Q_EMIT optionsChanged();
}

void FilePicker::setTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    Q_EMIT optionsChanged();
}

void FilePicker::setNameFilters(const QStringList &filters)
{
    if (m_nameFilters == filters)
        return;
    m_nameFilters = filters;
    Q_EMIT optionsChanged();
}

void FilePicker::setDefaultSuffix(const QString &suffix)
{
    if (m_defaultSuffix == suffix)
        return;
    m_defaultSuffix = suffix;
    Q_EMIT optionsChanged();
}

void FilePicker::setCurrentFolder(const QUrl &folder)
{
    if (m_currentFolder == folder)
        return;
    m_currentFolder = folder;
    Q_EMIT optionsChanged();
}

void FilePicker::setSelectedFile(const QUrl &file)
{
    const QList<QUrl> selected = file.isEmpty() ? QList<QUrl>() : QList<QUrl>{file};
    if (m_selected == selected)
        return;
    m_selected = selected;
    Q_EMIT selectedFilesChanged();
}

bool FilePicker::available()
{
#ifdef Q_OS_WASM
    return false;
#else
    return true;
#endif
}

void FilePicker::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    Q_EMIT optionsChanged();
}

QWindow *FilePicker::parentWindow() const
{
    if (m_parentWindowSet)
        return m_parentWindow;
    for (QObject *p = parent(); p; p = p->parent()) {
        if (const auto *item = qobject_cast<QQuickItem *>(p))
            return item->window();
    }
    return nullptr;
}

void FilePicker::setParentWindow(QWindow *window)
{
    m_parentWindowSet = window != nullptr;
    m_parentWindow = window;
}

FilePickerRequest FilePicker::request() const
{
    FilePickerRequest r;
    r.mode = m_mode;
    r.title = m_title;
    r.nameFilters = cleanNameFilters(m_nameFilters);
    r.defaultSuffix = m_defaultSuffix;
    r.folder = m_currentFolder;
    if (m_mode == SaveFile)
        r.initialFile = selectedFile();
    r.dark = m_dark;
    return r;
}

void FilePicker::setBackendFactoryForTests(BackendFactory factory)
{
    backendFactory() = std::move(factory);
}

QUrl FilePicker::withDefaultSuffix(const QUrl &file, const QString &suffix)
{
    QString ext = suffix;
    while (ext.startsWith(u'.'))
        ext.remove(0, 1);
    if (ext.isEmpty() || file.isEmpty() || file.scheme() == u"content")
        return file;
    const QString path = file.path(QUrl::FullyEncoded);
    if (path.isEmpty() || path.endsWith(u'/') || file.fileName().contains(u'.'))
        return file;
    QUrl out = file;
    out.setPath(path + u'.' + QString::fromLatin1(QUrl::toPercentEncoding(ext)), QUrl::TolerantMode);
    return out;
}

QStringList FilePicker::cleanNameFilters(const QStringList &filters)
{
    QStringList out;
    for (const QString &f : filters) {
        if (!f.trimmed().isEmpty())
            out.append(f);
    }
    return out;
}

} // namespace brulion::ui
