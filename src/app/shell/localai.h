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
// was opened again does not read them anew. It holds the same on every device: 16 384 tokens at
// once (ContextTokens).
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
    /// The models to choose from, for a segmented control: {value: id, text: its label, else its
    /// name}.
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
    /// Of a model that does not see: the model that looks at pictures in its place, by name
    /// ("LFM3-VL 2B"; "": the chosen one sees, or no model does) — a request with a picture goes to
    /// that one when it is downloaded too (`pictureModelHere`), loaded for it in the chosen one's
    /// place (send()).
    Q_PROPERTY(QString pictureModel READ pictureModel NOTIFY modelChanged FINAL)
    Q_PROPERTY(bool pictureModelHere READ pictureModelHere NOTIFY modelsChanged FINAL)
    /// It is another model adapted to the app (ai::LocalModel::adapter): `modelBase` by
    /// `modelBaseMaker` ("LFM3 1.7B", "Liquid AI"; "" for a model as its maker gives it), trained
    /// further by `modelMaker`.
    Q_PROPERTY(bool modelAdapted READ modelAdapted NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelBase READ modelBase NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelBaseMaker READ modelBaseMaker NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelLicense READ modelLicense NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelLicenseUrl READ modelLicenseUrl NOTIFY modelChanged FINAL)
    Q_PROPERTY(QString modelPageUrl READ modelPageUrl NOTIFY modelChanged FINAL)
    /// It is downloaded: its download finished (ai::LocalModelFiles::has: marked so) and its files
    /// are on the device, whole — a file it shares with another model alone is not enough.
    Q_PROPERTY(bool modelHere READ modelHere NOTIFY modelsChanged FINAL)
    /// What is still to download of it, as a text: less than `modelSize` when part of it is on the
    /// device already (a file it shares with another model, a download that stopped), the same
    /// when none of it is (or too little to show), "" when all its files are.
    Q_PROPERTY(QString modelLeft READ modelLeft NOTIFY modelsChanged FINAL)
    /// The other models it shares a file with, by name ("LFM3 1.7B"; "": none): that file is
    /// downloaded once and stays while one of them is on the device.
    Q_PROPERTY(QString modelSharesWith READ modelSharesWith NOTIFY modelChanged FINAL)
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
        QList<ai::LocalModel> models; ///< empty: ai::localModels() (tests: small ones)
        Opener open;                  ///< empty: the plugin (tests: a fake runner)
        int idleMs = 10 * 60 * 1000;  ///< the model leaves memory after this long without a request
        /// The default model before the assistant's own came first (keptChoice()).
        QString formerDefault = QStringLiteral("lfm3-vl-2b");
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
    QString pictureModel() const;
    bool pictureModelHere() const;
    bool modelAdapted() const;
    QString modelBase() const;
    QString modelBaseMaker() const;
    QString modelLicense() const;
    QString modelLicenseUrl() const;
    QString modelPageUrl() const;
    bool modelHere() const { return has(m_chosen); }
    QString modelLeft() const;
    QString modelSharesWith() const;
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
    /// What a device that never chose a model keeps: the former default (Options::formerDefault)
    /// when its files are here and today's default's are not — so that an update does not swap the
    /// model it uses for one still to download (AppController stores it as chosen); "" otherwise.
    QString keptChoice() const;
    /// Downloads the model `id`; one download at a time.
    void download(const QString &id);
    void remove(const QString &id);
    /// The id of the model being downloaded ("": none).
    QString downloadingId() const { return m_files.downloading(); }
    /// Where the models' files are.
    QString folder() const { return m_files.folder(); }

    /// Answers `request` with the model `settings.model` names ("": the default). `done` is called
    /// once, from the event loop: the answer, or why there is none (not downloaded, no plugin,
    /// "stopped" …); `partial` gets the answer while it is written (from the event loop, the
    /// latest only when they come faster than it is read); `stopped` set stops it. A request with
    /// a picture for a model that does not see goes, whole, to the first model that does and is
    /// downloaded (seer()): it takes the other's place in memory for it — one model is in memory
    /// at a time — and the next request without a picture opens the chosen one again. None
    /// downloaded: refused, with the name of the one to download.
    void send(const ai::Settings &settings, const ai::Request &request, std::function<void(const ai::Reply &)> done,
              ai::PartialCallback partial = {}, std::shared_ptr<std::atomic<bool>> stopped = {}) override;

    /// The tokens a model holds at once, the request and the answer together, on every device (a
    /// token takes some 12 KB of memory: 200 MB), and the most a picture may take of them.
    static constexpr int ContextTokens = 16384;
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
    /// The first model that sees pictures (`here`: of those downloaded); nullptr: none.
    const ai::LocalModel *seer(bool here) const;
    void finished(std::function<void(const ai::Reply &)> done, const ai::Reply &reply);
    /// Stops every request not answered yet.
    void stopAll();
    void updateWorking();

    QList<ai::LocalModel> m_models;
    ai::LocalModelFiles m_files;
    QString m_stateFolder;
    bool m_spinWait = false;
    bool m_available = false;
    int m_contextTokens = 0;
    QString m_chosen;
    QString m_formerDefault; ///< Options::formerDefault
    QString m_downloadError;
    QString m_downloadErrorId;     ///< the model whose download failed
    QElapsedTimer m_downloadClock; ///< since the download was asked for (the log tells how it went)
    qint64 m_downloadBytes = 0;    ///< what it had to fetch
    int m_pending = 0;
    bool m_working = false;
    /// The requests not answered yet (set: they stop — the app ends, their model is removed).
    QList<std::weak_ptr<std::atomic<bool>>> m_stops;
    QThread m_thread;
    LocalAiWorker *m_worker = nullptr; ///< lives in m_thread
};

} // namespace brulion::shell
