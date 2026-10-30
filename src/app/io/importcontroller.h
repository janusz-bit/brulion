// ImportController — the import half of ImportExportView (QML: `ImportController { workspace:
// Workspace }`): pick the source app (or let Brulion recognise the files), read what the user
// picked, show what will be written before anything is written, write it, report.
//
//   state       "empty"     nothing picked yet
//               "reading"   reading the picked files / folder (progress)
//               "analyzing" unpacking + a dry run of the importer (no writes)
//               "ready"     `preview` is up to date; startImport() writes it
//               "importing" writing (progress)
//               "done"      `report` describes what was written
//               "error"     `errorText` (nothing recognised, nothing to import, no notebook)
//
//   importers   [{ id, name, howTo (translated steps: where to export in that app), phoneHowTo
//                 (the same on an Android phone: where the files are, what to pick in the
//                 system's picker), pick: "files" | "folder" | "both", fileTypes ["*.zip", …],
//                 icon }] in the order of io::importers()
//   importerId  the user's choice ("" = recognise the files: detectedId); effectiveId is what runs
//   targetFolder  pages go there (default "Import/<App>"; daily notes always go to the journal)
//   preview     { pages, journals, journalsAppended, attachments, renamed (taken names: " 2"),
//                 files (first 200: [{ path,
//                 kind: page|journal|attachment, append, size }]), fileCount, bytes, warnings,
//                 folderExists }
//   report      { pages, journals, appended, attachments, written, failed [{ path, message }],
//                 renamed, warnings, folder, firstPage, cancelled }
//
// Collision policy (applied by the dry run and again right before writing, against the vault as
// it is then): a daily note that exists already gets the imported text appended under a heading
// "## Import (<source>)" (appendImportedJournal()); any other file that exists gets a unique name
// ("Plan 2.md", links to it rewritten by io::runImport); files are written with
// ChangeSource::Import, so replaced journal text stays in the version history.
//
// Heavy work (unpacking, the importer) runs off the GUI thread (iobackground.h); reading and
// writing go through the event loop in chunks, so the window stays responsive with big exports.
#pragma once

#include "fileexchange.h"
#include "io/files.h"
#include "io/importer.h"
#include "types.h"
#include "workspace.h"

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
#include <optional>

namespace brulion::ioviews {

/// Daily note collision policy: `existing` with `imported` appended under "## Import (<source>)"
/// (imported frontmatter dropped, the existing line endings and BOM kept). An empty existing
/// note simply takes the imported text; an empty import leaves it unchanged.
QByteArray appendImportedJournal(const QByteArray &existing, const QByteArray &imported, const QString &source);

/// "journal" (a daily note of `config`, or a file the importer marked for appending), "page"
/// (Markdown) or "attachment".
QString importedKind(const VaultPath &path, bool append, const VaultConfig &config);

/// "Import/<name>" with a file-name-safe name ("Import/Google Keep").
QString defaultImportFolder(const QString &importerName);

/// A user-typed target folder made relative and clean ("/Import//Notion/" -> "Import/Notion",
/// "" = the notebook's top level); std::nullopt when unusable (".." segments, a protected
/// folder such as .trash or .brulion, characters not allowed in file names).
std::optional<QString> cleanTargetFolder(const QString &folder);

/// How-to texts and pick modes of the importers (translated; importtexts.cpp).
struct ImporterInfo {
    QString howTo;
    QString pick; ///< "files" | "folder" | "both"
    QString icon;
    /// On a phone (Android): where that app's notes or export are and what to pick in the
    /// system's picker.
    QString phoneHowTo;
};
ImporterInfo importerInfo(const QString &id);

class ImportController : public QObject {
    Q_OBJECT

    Q_PROPERTY(brulion::shell::Workspace *workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged FINAL)
    /// Translated: changes with the UI language (importersChanged).
    Q_PROPERTY(QVariantList importers READ importers NOTIFY importersChanged FINAL)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    /// 0…1 while reading / importing, -1 while analysing (indeterminate).
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString importerId READ importerId WRITE setImporterId NOTIFY importerChanged FINAL)
    Q_PROPERTY(QString detectedId READ detectedId NOTIFY importerChanged FINAL)
    Q_PROPERTY(QString effectiveId READ effectiveId NOTIFY importerChanged FINAL)
    Q_PROPERTY(QString effectiveName READ effectiveName NOTIFY importerChanged FINAL)
    /// File name filters for a file dialog for the chosen importer ("Notion (*.zip)", "All files (*)").
    Q_PROPERTY(QStringList nameFilters READ nameFilters NOTIFY importerChanged FINAL)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY inputChanged FINAL)
    Q_PROPERTY(int inputCount READ inputCount NOTIFY inputChanged FINAL)
    Q_PROPERTY(qint64 inputBytes READ inputBytes NOTIFY inputChanged FINAL)
    Q_PROPERTY(QString inputSizeText READ inputSizeText NOTIFY inputChanged FINAL)
    Q_PROPERTY(QString targetFolder READ targetFolder WRITE setTargetFolder NOTIFY targetFolderChanged FINAL)
    Q_PROPERTY(QString targetFolderError READ targetFolderError NOTIFY targetFolderChanged FINAL)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged FINAL)
    Q_PROPERTY(QVariantMap report READ report NOTIFY reportChanged FINAL)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged FINAL)
    /// Warnings of reading the picked files (too large, unreadable).
    Q_PROPERTY(QStringList readWarnings READ readWarnings NOTIFY inputChanged FINAL)

public:
    explicit ImportController(QObject *parent = nullptr);
    ~ImportController() override;

    shell::Workspace *workspace() const { return m_workspace; }
    void setWorkspace(shell::Workspace *workspace);

    QVariantList importers() const;
    QString state() const { return m_state; }
    bool busy() const;
    qreal progress() const { return m_progress; }
    QString progressText() const { return m_progressText; }
    QString importerId() const { return m_importerId; }
    void setImporterId(const QString &id);
    QString detectedId() const { return m_detectedId; }
    QString effectiveId() const;
    QString effectiveName() const;
    QStringList nameFilters() const;
    QString sourceName() const { return m_sourceName; }
    int inputCount() const { return int(m_input.size()); }
    qint64 inputBytes() const { return m_inputBytes; }
    QString inputSizeText() const;
    QString targetFolder() const { return m_targetFolder; }
    void setTargetFolder(const QString &folder);
    QString targetFolderError() const { return m_targetFolderError; }
    QVariantMap preview() const { return m_preview; }
    QVariantMap report() const { return m_report; }
    QString errorText() const { return m_errorText; }
    QStringList readWarnings() const { return m_readWarnings; }

    /// Read the picked files (QML FilePicker.selectedFiles; local or content:// URLs).
    Q_INVOKABLE void addUrls(const QList<QUrl> &urls);
    /// Read a picked folder (QML FilePicker.selectedFolder) recursively.
    Q_INVOKABLE void addFolder(const QUrl &url);
    /// Typed paths or URLs (route parameter, the "path on this device" field, drag and drop,
    /// tests): local files or folders ("/storage/emulated/0/Download/Obsidian"), file:// or
    /// content:// URLs.
    Q_INVOKABLE void addPaths(const QStringList &paths);
    /// Browser (WebAssembly): the browser's file picker, several files at once
    /// (platform/wasm/wasmfiles.h; files above GatherLimits::maxFileBytes are skipped with a
    /// warning). Elsewhere: nothing (QML uses FilePicker).
    Q_INVOKABLE void pickInBrowser();
    /// Use these files as the input (tests, the browser picker). Analyses them.
    void setInput(const QList<io::InputFile> &files, const QString &sourceName, const QStringList &warnings = {});
    /// Back to "empty" (also cancels reading).
    Q_INVOKABLE void clear();
    /// Stop reading or writing (what was written stays; the report says so).
    Q_INVOKABLE void cancel();
    /// Write the previewed import ("ready" only).
    Q_INVOKABLE void startImport();
    /// Show the target folder (All pages filtered to it), or the first imported page.
    Q_INVOKABLE void openTarget();
    Q_INVOKABLE void openPage(const QString &path);

    /// How-to text, pick mode and icon of an importer (translated; importtexts.cpp).
    static ImporterInfo importerInfo(const QString &id);

    /// Files written per event loop turn while importing (default 20; tests).
    void setWriteChunk(int files) { m_writeChunk = qMax(1, files); }
    FileGatherer *gatherer() { return &m_gatherer; }

Q_SIGNALS:
    void workspaceChanged();
    /// The UI language changed: `importers` (names, how-to steps) and `nameFilters` read again.
    void importersChanged();
    void stateChanged();
    void progressChanged();
    void importerChanged();
    void inputChanged();
    void targetFolderChanged();
    void previewChanged();
    void reportChanged();
    /// An import finished (also partly); `message` is a translated one-line summary.
    void imported(const QString &message);

protected:
    /// QEvent::LanguageChange of the application (a translator was installed or removed).
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setState(const QString &state, const QString &error = {});
    void setProgress(qreal value, const QString &text = {});
    void analyze();
    void scheduleAnalyze();
    std::function<bool(const VaultPath &)> existsSnapshot() const;
    void applyPlan(int generation, const io::ImportResult &result, bool forImport);
    void writeStep();
    void finishImport(bool cancelled);
    const io::Importer *effectiveImporter() const;
    void updateDefaultFolder();

    QPointer<shell::Workspace> m_workspace;
    FileGatherer m_gatherer;
    QString m_state = QStringLiteral("empty");
    qreal m_progress = 0;
    QString m_progressText;
    QString m_importerId;
    QString m_detectedId;
    QString m_sourceName;
    QList<io::InputFile> m_input;
    qint64 m_inputBytes = 0;
    QStringList m_readWarnings;
    QString m_targetFolder;
    bool m_targetFolderEdited = false;
    QString m_targetFolderError;
    QVariantMap m_preview;
    QVariantMap m_report;
    QString m_errorText;
    int m_generation = 0;
    QTimer m_analyzeTimer;
    QTimer m_writeTimer;
    int m_writeChunk = 20;
    // The import being written.
    QList<io::ImportedFile> m_writeQueue;
    qsizetype m_writeIndex = 0;
    QString m_writeSource;
    QString m_writeFolder;
    QVariantMap m_writeCounts;
    QVariantList m_writeFailed;
    QStringList m_writeWarnings;
    QString m_firstPage;
    bool m_cancelRequested = false;
};

} // namespace brulion::ioviews
