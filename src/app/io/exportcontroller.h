// ExportController — the export half of ImportExportView (QML: `ExportController { workspace:
// Workspace }`): choose a format (io::exporters()) and what to export, produce the file, save it.
//
//   formats   [{ id, name, ext, description, icon }] in the order of io::exporters()
//   scope     "vault" (the whole notebook, its .brulion/config.json included) | "folder" (a folder
//             with its subfolders) | "page" (one page) — folders and pages bring the attachments
//             their pages link to (also from outside the folder), so pictures are never missing
//   summary   { files, pages, attachments, bytes, sizeText, valid, error } of the selection
//   state     "idle" → prepare() → "preparing" (reading in chunks, then the exporter off the GUI
//             thread; progress) → "ready" (fileName, sizeText, warnings) → saveTo(url) /
//             saveInBrowser() / saveToExportFolder() → "saved" (savedTo) | "error" (errorText)
//
// Saving per platform:
//   desktop     QML FileDialog (fileMode: SaveFile, selectedFile = suggested name) → saveTo(url)
//   Android     the same FileDialog opens the system's "Save as" (a content:// document Qt writes
//               to); saveToExportFolder() ("Save to Downloads") puts it into the Download folder
//               through MediaStore — no storage permission needed (Android 10+; Android 9 writes
//               the file, with the storage permission) — the report names the file
//   browser     saveInBrowser(): QFileDialog::saveFileContent (a download)
#pragma once

#include "types.h"
#include "workspace.h"

#include "io/exporter.h"

#include <QList>
#include <QObject>
#include <QPointer>
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
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QString pageTitle READ pageTitle NOTIFY optionsChanged FINAL)
    /// Folders of the notebook (for the folder picker), sorted.
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged FINAL)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY summaryChanged FINAL)
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
    /// Folder for the file dialog to start in (Documents / Download).
    Q_PROPERTY(QUrl defaultSaveFolder READ defaultSaveFolder CONSTANT FINAL)
    /// Folder used by saveToExportFolder().
    Q_PROPERTY(QString exportFolder READ exportFolder CONSTANT FINAL)

public:
    explicit ExportController(QObject *parent = nullptr);
    ~ExportController() override;

    shell::Workspace *workspace() const { return m_workspace; }
    void setWorkspace(shell::Workspace *workspace);

    QVariantList formats() const;
    QString format() const { return m_format; }
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
    QString exportFolder() const;

    /// Produce the file (async; "ready" when done).
    Q_INVOKABLE void prepare();
    /// Write the produced file to a picked destination (FileDialog.selectedFile).
    Q_INVOKABLE bool saveTo(const QUrl &url);
    /// Write it into exportFolder() under its suggested name (made unique).
    Q_INVOKABLE bool saveToExportFolder();
    /// Browser: offer it as a download (QFileDialog::saveFileContent). Elsewhere: false.
    Q_INVOKABLE bool saveInBrowser();
    /// Forget the produced file (back to "idle").
    Q_INVOKABLE void discard();
    Q_INVOKABLE void cancel();

    /// The produced files (tests).
    QList<io::ExportedFile> result() const { return m_result; }
    /// Files read per event loop turn (default 64; tests).
    void setReadChunk(int files) { m_readChunk = qMax(1, files); }
    /// Clock for the export's time stamps (default: Workspace::now()).
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }

Q_SIGNALS:
    void workspaceChanged();
    /// The UI language changed: `formats` (names, descriptions) read again.
    void formatsChanged();
    void optionsChanged();
    void foldersChanged();
    void summaryChanged();
    void stateChanged();
    void progressChanged();
    /// The produced file was saved (translated one-line message).
    void saved(const QString &message);

protected:
    /// QEvent::LanguageChange of the application (a translator was installed or removed).
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setState(const QString &state, const QString &error = {});
    void setProgress(qreal value, const QString &text = {});
    void updateSummary();
    ExportSelection currentSelection() const;
    void readStep();
    void runExporter();
    QString exportName() const;

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
};

} // namespace brulion::ioviews
