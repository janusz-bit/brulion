// SemanticSearch — search by meaning in the open notebook (`Workspace.semantic`; Settings → Search
// → Search by meaning; docs/UI.md "Search by meaning", docs/SEMANTIC.md): the embedding model on
// this device (core semantic::semanticModels(), ai::LocalModelFiles in <app data>/models/semantic:
// downloading, removing it), the setting that turns it on (off by default), and the notebook's
// vector index (core semantic::VectorIndex) that SearchResultModel fuses with the full-text hits
// (semantic::hybridSearch) — the Search view and the quick switcher's "Search the notes".
//
//   * The model runs in a plugin (core semantic::SemanticRunner: brulion_semantic, llama.cpp) loaded
//     the first time a note or a query is embedded; a build without it (the browser, a build without
//     BRULION_LOCAL_AI) has `available` false, and search is full text, exactly as without the model.
//   * Notes are embedded in the background: on a worker thread at the lowest priority (Linux:
//     SCHED_IDLE), a few notes per call, a few seconds after the notebook's index is ready — the most
//     recent journals first. Queries have a thread (and a model context) of their own: a query is
//     never held up by the indexing.
//   * Kept on the device, never in the notebook: <app cache>/semantic/<notebook id>-<model>.vectors
//     (VectorIndex::save). Opening the notebook again reads it; only notes whose text changed since
//     (semantic::noteKey) are embedded again. Edits follow the notebook's index (VaultIndex::changed,
//     changesSince): a changed note is embedded again a moment after the typing stops, a deleted one
//     forgotten; the file is written when the queue is empty (and every minute of a long build).
//   * The calibration (core semantic::Calibration: the mean of the notebook's chunks, the
//     Polish–English direction) is estimated from the notebook itself whenever the index is up to
//     date after a change.
//   * The model leaves memory five minutes after its last use; the next note or query loads it again.
//
// QML: state ("unavailable" | "off" | "noModel" | "idle" | "loading" | "indexing" | "ready" |
// "failed"), notesDone / notesTotal ("Reading notes by meaning: 120 of 900"), the model's file
// (modelHere, downloading, progress, progressText, checking, downloadError, download(),
// cancelDownload(), remove()), its name, size, licence and page, `enabled`.
#pragma once

#include "ai/local.h"
#include "ai/localfiles.h"
#include "semantic/models.h"
#include "semantic/semanticrunner.h"
#include "semantic/vectorindex.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariant>

#include <functional>
#include <memory>
#include <optional>

namespace brulion {
class VaultIndex;
}

namespace brulion::shell {

class SemanticIndexWorker;
class SemanticQueryWorker;
class SemanticRunnerHolder;
struct SemanticBatch;

class SemanticSearch : public QObject {
    Q_OBJECT

    /// This build can search by meaning (it has the plugin).
    Q_PROPERTY(bool available READ available NOTIFY configured FINAL)
    /// Search by meaning is on (AppSettings' "search/semantic"; off by default). Turning it on
    /// downloads the model when it is not here yet.
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(QString modelName READ modelName NOTIFY configured FINAL)
    Q_PROPERTY(QString modelMaker READ modelMaker NOTIFY configured FINAL)
    /// Its file's size as a text ("126 MB").
    Q_PROPERTY(QString modelSize READ modelSize NOTIFY configured FINAL)
    Q_PROPERTY(QString modelLicense READ modelLicense NOTIFY configured FINAL)
    Q_PROPERTY(QString modelLicenseUrl READ modelLicenseUrl NOTIFY configured FINAL)
    Q_PROPERTY(QString modelPageUrl READ modelPageUrl NOTIFY configured FINAL)
    /// The model is downloaded (whole and checked).
    Q_PROPERTY(bool modelHere READ modelHere NOTIFY modelChanged FINAL)
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadChanged FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString progressText READ progressText NOTIFY downloadChanged FINAL)
    Q_PROPERTY(bool checking READ checking NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString downloadError READ downloadError NOTIFY downloadChanged FINAL)
    /// "unavailable" (no plugin) | "off" | "noModel" (on, the model not here) | "idle" (no notebook
    /// open) | "loading" (reading the kept index) | "indexing" | "ready" | "failed" (`error`).
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    /// Why the model cannot be used ("" unless `state` is "failed").
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
    /// The notebook's notes, and those found by meaning already.
    Q_PROPERTY(int notesTotal READ notesTotal NOTIFY progressChanged FINAL)
    Q_PROPERTY(int notesDone READ notesDone NOTIFY progressChanged FINAL)
    /// Search fuses meaning in now (on, the model here and working, vectors of some notes).
    Q_PROPERTY(bool active READ active NOTIFY stateChanged FINAL)

public:
    /// Opens the embedding model (a worker thread calls it); nullptr with `error`.
    using Opener = std::function<std::unique_ptr<semantic::SemanticRunner>(const semantic::SemanticRunner::Options &,
                                                                           QString *error)>;

    struct Options {
        QString folder;               ///< where the model's file is kept
        QString cacheDir;             ///< where the notebooks' vector indexes are kept ("": nowhere)
        QStringList pluginDirs;       ///< where the plugin may be ("": by its name)
        QList<ai::LocalModel> models; ///< empty: semantic::semanticModels() (tests: small ones)
        Opener open;                  ///< empty: the plugin (tests: a fake model)
        int idleMs = 5 * 60 * 1000;   ///< the model leaves memory after this long without work
        int startDelayMs = 3000;      ///< after the notebook's index is ready
        int changeDelayMs = 1500;     ///< after the last change of a note (typing)
        /// The settings (the shell: AppSettings' keys).
        std::function<QVariant(const QString &key, const QVariant &fallback)> setting;
        std::function<void(const QString &key, const QVariant &value)> setSetting;
    };

    explicit SemanticSearch(QObject *parent = nullptr);
    ~SemanticSearch() override;

    /// The device's side (AppController, once): the model's folder, the plugin, the settings.
    void configure(const Options &options);

    /// Follow the notebook `index` (its Vault::id(): `notebookId`); nothing happens before start().
    void attach(VaultIndex *index, const QString &notebookId);
    /// The notebook's index is ready: begin after Options::startDelayMs.
    void start();
    /// The notebook closes: what is not saved yet is written now.
    void detach();

    bool available() const { return m_available; }
    bool enabled() const;
    void setEnabled(bool on);
    QString modelName() const;
    QString modelMaker() const;
    QString modelSize() const;
    QString modelLicense() const;
    QString modelLicenseUrl() const;
    QString modelPageUrl() const;
    bool modelHere() const;
    bool downloading() const;
    double progress() const;
    QString progressText() const;
    bool checking() const;
    QString downloadError() const { return m_downloadError; }
    QString state() const;
    QString error() const { return m_error; }
    int notesTotal() const { return m_total; }
    int notesDone() const;
    bool active() const;

    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();
    /// Deletes the model's file and every notebook's vectors kept on this device; turns it off.
    Q_INVOKABLE void remove();

    /// The vector of the query `text` when it is known already; else it is asked for (queryReady()
    /// follows, unless the model fails) and nullopt comes back — search shows the full-text
    /// results until then.
    std::optional<semantic::Vector> queryVector(const QString &text);
    /// The notebook's vectors (search reads them on this thread).
    const semantic::VectorIndex &index() const { return m_index; }
    /// Nothing waits to be embedded and nothing is being embedded (tests wait for it).
    bool idle() const;
    /// Notes embedded since the app started (tests: what an edit embeds again).
    int embeddedNotes() const { return m_embeddedNotes; }
    /// Where the open notebook's vectors are kept ("": nowhere).
    QString indexFile() const;

Q_SIGNALS:
    void configured();
    void enabledChanged();
    void modelChanged();
    void downloadChanged();
    void stateChanged();
    void progressChanged();
    /// What search by meaning finds changed (notes embedded, removed; at most every few seconds
    /// while a notebook is being indexed): results on screen follow.
    void contentChanged();
    /// The vector of `text` arrived (queryVector()).
    void queryReady(const QString &text);

private:
    const ai::LocalModel *model() const;
    QString modelPath() const;
    /// Can it work now: on, available, the model here, a notebook attached and started.
    bool canRun() const;
    void begin();
    void stop();
    void onLoaded(quint64 generation, std::shared_ptr<std::optional<semantic::VectorIndex>> loaded);
    /// Every note against the index (`full`), or only those changed since the last look.
    void reconcile(bool full);
    void enqueue(const QString &id, bool first);
    void dispatch();
    void onEmbedded(quint64 generation, std::shared_ptr<SemanticBatch> batch);
    void onDrained();
    void save(bool now);
    void fail(const QString &error);
    /// stateChanged() when state() or active() changed.
    void refreshState();
    void touchRunner();
    void notifyContent(bool now);

    Options m_options;
    bool m_available = false;
    QList<ai::LocalModel> m_models;
    std::unique_ptr<ai::LocalModelFiles> m_files;
    QString m_downloadError;

    QPointer<VaultIndex> m_vaultIndex;
    QString m_notebookId;
    bool m_started = false;
    int m_version = -1; ///< the VaultIndex::version() looked at last
    quint64 m_generation = 0; ///< grows when what runs is stopped: older results are dropped
    QString m_state = QStringLiteral("unavailable");
    bool m_wasActive = false;
    QString m_error;
    bool m_enabled = false; ///< (without Options::setting: tests)
    bool m_loading = false; ///< the kept index is being read

    semantic::VectorIndex m_index;
    bool m_loaded = false;      ///< the kept index was read (or there was none)
    QStringList m_queue;        ///< notes to embed, the next first
    QSet<QString> m_queued;
    QStringList m_inFlight;     ///< the notes being embedded now
    int m_total = 0;
    int m_embeddedNotes = 0;
    bool m_dirty = false;       ///< not saved since a change
    bool m_calibrate = false;   ///< chunks changed since the calibration was estimated
    QElapsedTimer m_buildClock; ///< since the queue was last empty (the log)
    int m_builtNotes = 0;       ///< notes embedded since then

    QHash<QString, semantic::Vector> m_queries; ///< query vectors, the most recent kept
    QStringList m_queryOrder;
    QSet<QString> m_asked;

    QTimer m_startTimer;
    QTimer m_changeTimer;
    QTimer m_saveTimer;
    QTimer m_idleTimer;
    QTimer m_contentTimer;

    std::shared_ptr<SemanticRunnerHolder> m_runner;
    QThread m_indexThread;
    QThread m_queryThread;
    SemanticIndexWorker *m_indexWorker = nullptr; ///< lives in m_indexThread
    SemanticQueryWorker *m_queryWorker = nullptr; ///< lives in m_queryThread
};

} // namespace brulion::shell
