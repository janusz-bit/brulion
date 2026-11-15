// LocalAi — the model on the device (`AppController.localAi`; Settings → Extensions → AI, the
// provider "On this device"; docs/EXTENSIONS.md "AI"): which of the models the app knows are
// downloaded, downloading and removing them (ai::LocalModelFiles: <app data>/models), and
// answering b.ai's requests with one (ai::LocalBackend: WorkspaceExtensions' ai::Client hands them
// here).
//
// A request is answered on a worker thread by the model runner (ai::LocalRunner): llama.cpp in a
// plugin loaded the first time a model is asked — an app that never asks loads none of it, and a
// build without the plugin (`available` false) offers no model at all. Requests are answered one
// after another; the model stays in memory between them (the next question of a conversation
// reads only what is new) and is let go after ten minutes without a request — what it read of a
// request's system text and tools is kept on disk (`stateFolder`), so the first request after it
// was opened again does not read them anew. How much it holds at once follows the device's memory
// (contextFor()).
#pragma once

#include "ai/local.h"
#include "ai/localfiles.h"
#include "ai/localrunner.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QVariantList>

#include <atomic>
#include <functional>
#include <memory>

namespace brulion::shell {

class LocalAiWorker;

class LocalAi : public QObject, public ai::LocalBackend {
    Q_OBJECT

    /// This build can run a model on the device (it has the model runner's plugin).
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    /// The models to choose from, for a segmented control: {value: id, text: name}.
    Q_PROPERTY(QVariantList choices READ choices CONSTANT FINAL)
    /// The model chosen (its id; AppSettings' aiLocalModel, "" being the default one) — what the
    /// properties below and download() / cancelDownload() / remove() are about.
    Q_PROPERTY(QString model READ chosen WRITE setChosen NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelName READ modelName NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelMaker READ modelMaker NOTIFY modelChanged FINAL)
    /// Its files' size, as a text ("1.2 GB").
    Q_PROPERTY(QString modelSize READ modelSize NOTIFY modelChanged FINAL)
    /// It sees pictures (b.ai's `images`).
    Q_PROPERTY(bool modelSees READ modelSees NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelLicense READ modelLicense NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelLicenseUrl READ modelLicenseUrl NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelPageUrl READ modelPageUrl NOTIFY modelChanged FINAL)
    /// Its files are on the device, whole.
    Q_PROPERTY(bool modelHere READ modelHere NOTIFY modelsChanged FINAL)
    /// It is being downloaded: how far (0–1; `progressText`: "312 MB of 1.2 GB"), whether the
    /// files are being checked. `downloadError`: why its last download failed ("": it did not).
    Q_PROPERTY(bool downloading READ downloadingChosen NOTIFY downloadChanged FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString progressText READ progressText NOTIFY downloadChanged FINAL)
    Q_PROPERTY(bool checking READ checking NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString downloadError READ downloadError NOTIFY downloadChanged FINAL)
    /// Another model is being downloaded (one at a time).
    Q_PROPERTY(bool downloadingOther READ downloadingOther NOTIFY downloadChanged FINAL)
    /// A model is answering (or being loaded for an answer).
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)

public:
    /// Opens a model's runner (the worker thread calls it); nullptr with `error`.
    using Opener = std::function<std::unique_ptr<ai::LocalRunner>(const ai::LocalRunner::Options &, QString *error)>;

    struct Options {
        QString folder;               ///< where the models' files are kept
        /// Where a model's state after a request's system text and tools is kept between runs (a
        /// cache: "" keeps none).
        QString stateFolder;
        /// The model's threads spin between two steps instead of sleeping at once
        /// (ai::LocalRunner::Options::spinWait: Android, where background work is kept off the
        /// fast cores).
        bool spinWait = false;
        QStringList pluginDirs;       ///< where the model runner's plugin may be ("": by its name)
        qint64 memoryBytes = 0;       ///< the device's memory (0: unknown)
        QList<ai::LocalModel> models; ///< empty: ai::localModels() (tests: small ones)
        Opener open;                  ///< empty: the plugin (tests: a fake runner)
        int idleMs = 10 * 60 * 1000;  ///< the model leaves memory after this long without a request
    };

    explicit LocalAi(const Options &options, QObject *parent = nullptr);
    ~LocalAi() override;

    bool available() const { return m_available; }
    QVariantList choices() const;
    QString chosen() const { return m_chosen; }
    void setChosen(const QString &id);
    QString modelName() const;
    QString modelMaker() const;
    QString modelSize() const;
    bool modelSees() const;
    QString modelLicense() const;
    QString modelLicenseUrl() const;
    QString modelPageUrl() const;
    bool modelHere() const { return has(m_chosen); }
    bool downloadingChosen() const;
    bool downloadingOther() const;
    double progress() const;
    QString progressText() const;
    bool checking() const { return m_files.checking(); }
    QString downloadError() const;
    bool busy() const { return m_pending > 0; }
    /// A model is being downloaded or answers — what a phone does only while the app is on
    /// screen, so its screen stays on meanwhile (AppController: platform::keepScreenOn()).
    bool working() const { return m_working; }

    /// Downloads the chosen model (what is missing of it), stops its download, deletes its files
    /// (a request it is answering ends: "stopped").
    Q_INVOKABLE void download() { download(m_chosen); }
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void remove() { remove(m_chosen); }

    /// The model `id` is downloaded ("": the default model).
    bool has(const QString &id) const;
    /// Downloads the model `id`; one download at a time.
    void download(const QString &id);
    void remove(const QString &id);
    /// The id of the model being downloaded ("": none).
    QString downloadingId() const { return m_files.downloading(); }
    /// Where the models' files are.
    QString folder() const { return m_files.folder(); }

    /// Answers `request` with the model `settings.model` names ("": the default). `done` is called
    /// once, from the event loop: the answer, or why there is none (not downloaded, no plugin …).
    void send(const ai::Settings &settings, const ai::Request &request,
              std::function<void(const ai::Reply &)> done) override;

    /// The tokens a model may hold at once on a device with `memoryBytes` of memory, and the most
    /// a picture may take of them.
    static int contextFor(qint64 memoryBytes);
    static int imageTokensFor(int contextTokens);

Q_SIGNALS:
    /// Another model is chosen.
    void modelChanged();
    /// Which models are on the device changed.
    void modelsChanged();
    void downloadChanged();
    void busyChanged();
    void workingChanged();
    /// A download ended: `ok`, or `error` ("" when it was cancelled).
    void downloadFinished(const QString &id, bool ok, const QString &error);

private:
    const ai::LocalModel *model(const QString &id) const;
    void finished(std::function<void(const ai::Reply &)> done, const ai::Reply &reply);
    void updateWorking();

    QList<ai::LocalModel> m_models;
    ai::LocalModelFiles m_files;
    QString m_stateFolder;
    bool m_spinWait = false;
    bool m_available = false;
    int m_contextTokens = 0;
    QString m_chosen;
    QString m_downloadError;
    QString m_downloadErrorId;     ///< the model whose download failed
    QElapsedTimer m_downloadClock; ///< since the download was asked for (the log tells how it went)
    qint64 m_downloadBytes = 0;    ///< what it had to fetch
    int m_pending = 0;
    bool m_working = false;
    std::atomic<bool> m_cancel{false};
    QThread m_thread;
    LocalAiWorker *m_worker = nullptr; ///< lives in m_thread
};

} // namespace brulion::shell
