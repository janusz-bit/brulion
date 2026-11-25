#pragma once
// The files of the models on the device (ai/local.h) in the app's models folder: which models are
// there, downloading one, removing it. A download can stop anywhere — no network, the app closed,
// cancel() — and goes on from where it stopped: each file is written as "<name>.part", asked for
// from its size on (an HTTP range), checked against its SHA-256 when whole and only then given its
// name. So a file with a model's name is a whole, checked file. A download holds little in memory:
// over HTTP/1.1 with a few megabytes read ahead, the server waits while this thread does not read
// (an app off the screen on Android).
//
// Models may share a file (ai::LocalModel: an adapted model and its maker's own — the identical
// LocalFile), so a model is on the device when its download finished — "<id>.downloaded" says so —
// and its files are there. A file another model on the device (or being downloaded) needs stays
// when one of them is removed, and goes with the last.
//
// Older versions marked nothing: adopt() marks the models whole in such a folder, once — it then
// writes ".adopted" there (making the folder), also when nothing was adopted, and never looks
// again. So a model removed stays removed although its file stayed for another one, and a shared
// file this version downloaded for one model never makes another one downloaded.
//
//   ai::LocalModelFiles files(folder);
//   files.adopt(ai::localModels());                         // at the start: an older version's folder
//   connect(&files, &ai::LocalModelFiles::finished, …);   // (id, ok, error)
//   files.download(*ai::localModel(u"lfm3-vl-2b"_s));       // one model at a time
//   files.has(model);  files.remove(model, ai::localModels());

#include "ai/local.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QPointer>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace brulion::ai {

class LocalModelFiles : public QObject {
    Q_OBJECT

public:
    explicit LocalModelFiles(const QString &folder, QObject *parent = nullptr);
    ~LocalModelFiles() override;

    /// Downloads go through `network` (not owned) instead of a manager of its own.
    void setNetwork(QNetworkAccessManager *network);

    QString folder() const { return m_folder; }
    /// Where a model's file is (or will be): in the folder by its name, a file of the user's own
    /// where it is (LocalFile::path).
    QString pathOf(const LocalFile &file) const;
    /// `model` was downloaded, and every file of it is there, whole and checked.
    bool has(const LocalModel &model) const;
    /// The first time for the folder (no ".adopted" in it): every model of `models` whose files
    /// are all there is marked downloaded — a folder of an older version, which marked nothing —,
    /// then ".adopted" is written (the folder made first). Afterwards nothing.
    void adopt(const QList<LocalModel> &models);
    /// The bytes of `model` on the device: its whole files and the part of one downloaded so far.
    qint64 bytesHere(const LocalModel &model) const;

    /// Downloads what `model` lacks, file after file; finished() says how it ended. Nothing while
    /// another download runs (one at a time). Its files all there already: only marked downloaded
    /// (no free space needed).
    void download(const LocalModel &model);
    /// The free space a download leaves beyond its own bytes (64 MB; tests: more than any disk).
    void setSpareBytes(qint64 bytes) { m_spareBytes = bytes; }
    /// Stops the download; what it has stays for the next one. finished(id, false, "").
    void cancel();
    /// Deletes `model`'s files and partial downloads (its running download stops first) — but
    /// not a file another model of `models` needs too (the identical LocalFile), one on the
    /// device or being downloaded.
    bool remove(const LocalModel &model, const QList<LocalModel> &models = {});

    /// The id of the model being downloaded ("": none).
    QString downloading() const { return m_id; }
    /// Its bytes here so far and in all.
    qint64 received() const { return m_received; }
    qint64 total() const { return m_total; }
    /// The download is whole and being checked (a second or a few).
    bool checking() const { return m_checking; }

Q_SIGNALS:
    /// received() or checking() changed (at most ten times a second while bytes arrive).
    void progress();
    /// The download of the model `id` ended: `ok`, or `error` says why not ("": it was cancelled).
    void finished(const QString &id, bool ok, const QString &error);
    /// Which models are on the device changed (a download finished, files were removed).
    void changed();

private:
    void nextFile();
    void begin(const LocalFile &file);
    void onData();
    void onReplyFinished();
    void check();
    void checkSome();
    void end(bool ok, QString error);
    qint64 wholeBytes() const;
    /// Every file of `model` is there, whole.
    bool whole(const LocalModel &model) const;
    QString markerOf(const LocalModel &model) const;

    QString m_folder;
    qint64 m_spareBytes = 64 * 1024 * 1024;
    QPointer<QNetworkAccessManager> m_network;
    std::unique_ptr<QNetworkAccessManager> m_ownNetwork;
    QString m_id; ///< the model being downloaded
    LocalModel m_model;
    LocalFile m_file; ///< its file being downloaded or checked
    QFile m_part;
    QPointer<QNetworkReply> m_reply;
    qint64 m_resumedAt = 0; ///< the size the part had when this request started
    bool m_started = false; ///< the first bytes of the answer were seen
    bool m_cancelled = false;
    bool m_checking = false;
    std::unique_ptr<QCryptographicHash> m_hash;
    qint64 m_received = 0;
    qint64 m_total = 0;
    QElapsedTimer m_lastProgress;
};

} // namespace brulion::ai
