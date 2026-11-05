// ExportController — the export half of ImportExportView (QML: `ExportController { workspace:
// Workspace }`): choose a format (io::exporters()) and what to export, produce the file, save it.
//
//   formats   [{ id, name, ext, description, icon }] in the order of io::exporters()
//   scope     "vault" (the whole notebook, its .brulion/config.json included) | "folder" (a folder
//             with its subfolders) | "page" (one page) — folders and pages bring the attachments
//             their pages link to (also from outside the folder), so pictures are never missing
//   summary   { files, pages, attachments, bytes, sizeText, valid, error } of the selection
//   webPictures  "link" (pictures from the internet keep their addresses) | "include" (the HTML
//             and Pandoc exports carry copies: the device's cached ones, the missing ones fetched
//             first — Workspace::loadWebPicture; a failure keeps the address, with a warning);
//             webPicturesApply: the chosen format can include them
//   state     "idle" → prepare() → "preparing" (reading in chunks, then the exporter off the GUI
//             thread; progress) → "ready" (fileName, sizeText, warnings) → saveTo(url) /
//             saveInBrowser() / saveToExportFolder() → "saved" (savedTo) | "error" (errorText)
//
// One page (ContentExportDialog, the page's menu and the palette): scope "page" with the formats
// that make sense for one page — "markdown-file" (the page's file as it is; page scope only),
// markdown-zip (with its attachments), html-single, pdf, docx (io/paper.h, the paper of the
// device's locale: A4 / Letter), pandoc.
//
// Printing (print()): the scope is read like for an export, then
//   desktop     the system's print dialog (QPrintDialog, Qt Print Support) and the pages painted on
//               the chosen printer (io::printNotes: the PDF export's look); setPrintToFile(): a
//               PDF file instead, without a dialog (tests, self-tests)
//   Android     the PDF export handed to the system's print service (PrintManager through the
//               platform's Android bridge: printers, "Save as PDF")
//   browser     the HTML document export in the browser's print dialog (a hidden frame)
// state "preparing" → "printed" (printedMessage) | "idle" (the dialog was cancelled) | "error".
//
// Saving per platform:
//   desktop     QML FilePicker (fileMode: SaveFile, selectedFile = suggestedSaveFile; the system's
//               dialog) → saveTo(url)
//   Android     the same FilePicker opens the system's "Save as" (a content:// document Qt writes
//               to); saveToExportFolder() ("Save to Downloads") puts it into the Download folder
//               through MediaStore — no storage permission needed (Android 10+; Android 9 writes
//               the file, with the storage permission) — the report names the file
//   browser     saveInBrowser(): the browser's save picker or a download (platform/wasm/wasmfiles.h)
#pragma once

#include "types.h"
#include "workspace.h"

#include "io/exporter.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace brulion::ioviews {

/// What a scope exports (vault paths, without .brulion/config.json — the controller adds it for
/// the whole notebook).
struct ExportSelection {
    QList<VaultPath> paths; ///< sorted, unique
    int pages = 0;
    int attachments = 0;
    qint64 bytes = 0;
};

/// The files of `scope` ("vault" | "folder" | "page") among `files` (folders are skipped);
/// `linkedFiles(page)` returns the resolved link targets of a page — the non-Markdown ones are
/// added as attachments (for "folder" and "page").
ExportSelection selectExportFiles(const QList<FileStat> &files, const QString &scope, const QString &folder,
                                  const QString &page,
                                  const std::function<QList<VaultPath>(const VaultPath &)> &linkedFiles);

class ExportController : public QObject {
    Q_OBJECT

    Q_PROPERTY(brulion::shell::Workspace *workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged FINAL)
    /// Translated: changes with the UI language (formatsChanged).
    Q_PROPERTY(QVariantList formats READ formats NOTIFY formatsChanged FINAL)
    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY optionsChanged FINAL)
    /// The `formats` row of `format` ({} for one without a row: "markdown-file").
    Q_PROPERTY(QVariantMap chosenFormat READ chosenFormat NOTIFY chosenFormatChanged FINAL)
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QString pageTitle READ pageTitle NOTIFY optionsChanged FINAL)
    /// Folders of the notebook (for the folder picker), sorted.
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged FINAL)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY summaryChanged FINAL)
    /// Pictures from the internet in the HTML and Pandoc exports: "link" (default) | "include".
    Q_PROPERTY(QString webPictures READ webPictures WRITE setWebPictures NOTIFY optionsChanged FINAL)
    /// The chosen format can include copies of pictures from the internet (html-site,
    /// html-single, pdf, docx, pandoc).
    Q_PROPERTY(bool webPicturesApply READ webPicturesApply NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged FINAL)
    /// Suggested file name of the produced file ("Brulion-2026-10-15.zip").
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString fileSizeText READ fileSizeText NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged FINAL)
    /// Where the file went (a path or a document name).
    Q_PROPERTY(QString savedTo READ savedTo NOTIFY stateChanged FINAL)
    /// Folder for the file picker to start in (Documents / Download).
    Q_PROPERTY(QUrl defaultSaveFolder READ defaultSaveFolder CONSTANT FINAL)
    /// The produced file's name in defaultSaveFolder, as a file URL for FilePicker.selectedFile
    /// (Android's "Save as" shows its file name); empty without a file.
    Q_PROPERTY(QUrl suggestedSaveFile READ suggestedSaveFile NOTIFY stateChanged FINAL)
    /// Folder used by saveToExportFolder().
    Q_PROPERTY(QString exportFolder READ exportFolder CONSTANT FINAL)
    /// print() can reach a printer here (desktop with Qt Print Support, Android, the browser).
    Q_PROPERTY(bool printAvailable READ printAvailable CONSTANT FINAL)
    /// Paper of the PDF and DOCX exports and of printing in the browser: "a4" | "letter" (default:
    /// io::paperSizeFor the system's locale).
    Q_PROPERTY(QString paperSize READ paperSize WRITE setPaperSize NOTIFY optionsChanged FINAL)

public:
    explicit ExportController(QObject *parent = nullptr);
    ~ExportController() override;

    shell::Workspace *workspace() const { return m_workspace; }
    void setWorkspace(shell::Workspace *workspace);

    QVariantList formats() const;
    QString format() const { return m_format; }
    QVariantMap chosenFormat() const;
    void setFormat(const QString &format);
    QString scope() const { return m_scope; }
    void setScope(const QString &scope);
    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    QString page() const { return m_page; }
    void setPage(const QString &page);
    QString pageTitle() const;
    QStringList folders() const;
    QVariantMap summary() const { return m_summary; }
    QString webPictures() const { return m_webPictures; }
    void setWebPictures(const QString &mode);
    bool webPicturesApply() const;
    QString state() const { return m_state; }
    bool busy() const { return m_state == u"preparing" || m_state == u"saving"; }
    qreal progress() const { return m_progress; }
    QString progressText() const { return m_progressText; }
    QString fileName() const;
    QString fileSizeText() const;
    QStringList warnings() const { return m_warnings; }
    QString errorText() const { return m_errorText; }
    QString savedTo() const { return m_savedTo; }
    QUrl defaultSaveFolder() const;
    QUrl suggestedSaveFile() const;
    QString exportFolder() const;
    bool printAvailable() const;
    QString paperSize() const { return m_paperSize; }
    void setPaperSize(const QString &paper);

    /// Produce the file (async; "ready" when done).
    Q_INVOKABLE void prepare();
    /// Write the produced file to a picked destination (FilePicker.selectedFile).
    Q_INVOKABLE bool saveTo(const QUrl &url);
    /// Write it into exportFolder() under its suggested name (made unique).
    Q_INVOKABLE bool saveToExportFolder();
    /// Browser: the browser's save picker (Chromium on https) or a download, a file each
    /// (platform/wasm/wasmfiles.h). Elsewhere: false.
    Q_INVOKABLE bool saveInBrowser();
    /// Forget the produced file (back to "idle").
    Q_INVOKABLE void discard();
    Q_INVOKABLE void cancel();
    /// Print the scope (async; "printed" when handed to the printer or the system's print service,
    /// "idle" when the print dialog was cancelled).
    Q_INVOKABLE void print();
    /// Desktop: print() paints into this PDF file without the print dialog (tests and self-tests;
    /// "" — the dialog again).
    Q_INVOKABLE void setPrintToFile(const QString &pdfPath);

    /// The produced files (tests).
    QList<io::ExportedFile> result() const { return m_result; }
    /// Files read per event loop turn (default 64; tests).
    void setReadChunk(int files) { m_readChunk = qMax(1, files); }
    /// The most bytes of copies of pictures from the internet one export includes ("include";
    /// default 256 MB; tests). Past it the export keeps their addresses, with a warning.
    void setWebCopiesLimit(qint64 bytes) { m_webLimit = qMax<qint64>(0, bytes); }
    /// Clock for the export's time stamps (default: Workspace::now()).
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }

Q_SIGNALS:
    void workspaceChanged();
    /// The UI language changed: `formats` (names, descriptions) read again.
    void formatsChanged();
    void optionsChanged();
    /// formatsChanged() or optionsChanged().
    void chosenFormatChanged();
    void foldersChanged();
    void summaryChanged();
    void stateChanged();
    void progressChanged();
    /// The produced file was saved (translated one-line message).
    void saved(const QString &message);
    /// The scope went to the printer (translated one-line message).
    void printed(const QString &message);

protected:
    /// QEvent::LanguageChange of the application (a translator was installed or removed).
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setState(const QString &state, const QString &error = {});
    void setProgress(qreal value, const QString &text = {});
    void updateSummary();
    /// prepare() and print(): the scope's files read in chunks, then the exporter or the printing.
    void startReading();
    ExportSelection currentSelection() const;
    void readStep();
    /// "include": the pictures from the internet of the read notes, copies from the cache, the
    /// missing ones fetched — then runExporter().
    void collectWebPictures();
    /// The copies of `urls`: from the cache in chunks (webCacheStep()), the missing ones of the
    /// internet fetched (not those of private hosts), then finishWebPictures().
    void fetchWebPictures(const QStringList &urls);
    void webCacheStep();
    void finishWebPictures();
    void webPictureChanged(const QString &url);
    void addWebPicture(const QString &url, const QByteArray &data, const QString &format);
    void webPictureProgress();
    void stopWebPictures();
    void runExporter();
    /// print(): the read files to the platform's printing.
    void runPrint(io::ExportInput input);
    void finishPrint(bool ok, const QString &error, const QStringList &warnings);
    QString exportName() const;
    io::ExportInput exportInput();

    QPointer<shell::Workspace> m_workspace;
    QString m_format = QStringLiteral("markdown-zip");
    QString m_scope = QStringLiteral("vault");
    QString m_folder;
    QString m_page;
    QVariantMap m_summary;
    QString m_state = QStringLiteral("idle");
    qreal m_progress = 0;
    QString m_progressText;
    QStringList m_warnings;
    QString m_errorText;
    QString m_savedTo;
    QList<io::ExportedFile> m_result;
    int m_generation = 0;
    int m_readChunk = 64;
    QTimer m_summaryTimer;
    bool m_foldersDirty = false;
    QTimer m_readTimer;
    std::function<QDateTime()> m_clock;
    // The export being read.
    QList<VaultPath> m_readQueue;
    qsizetype m_readIndex = 0;
    QList<io::FileEntry> m_readFiles;
    QStringList m_readWarnings;
    QString m_webPictures = QStringLiteral("link");
    QString m_paperSize;
    bool m_printing = false;
    QString m_printToFile;
    // The copies of pictures from the internet being collected (webPictures "include").
    QHash<QString, io::WebPictureCopy> m_webCopies;
    QStringList m_webQueue; ///< the pictures of the notes, looked up in the cache
    qsizetype m_webIndex = 0;
    QStringList m_webMissing;
    QStringList m_webPrivateHosts;
    qint64 m_webBytes = 0;
    qint64 m_webLimit = 256LL * 1024 * 1024;
    qsizetype m_webOverLimit = 0;
    QTimer m_webTimer;
    QSet<QString> m_webPending;
    qsizetype m_webTotal = 0;
    QMetaObject::Connection m_webConnection;
};

} // namespace brulion::ioviews
