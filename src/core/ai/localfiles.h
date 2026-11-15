#pragma once
// The files of the models on the device (ai/local.h) in the app's models folder: which models are
// there, downloading one, removing it. A download can stop anywhere — no network, the app closed,
// cancel() — and goes on from where it stopped: each file is written as "<name>.part", asked for
// from its size on (an HTTP range), checked against its SHA-256 when whole and only then given its
// name. So a file with a model's name is a whole, checked file. A download holds little in memory:
// over HTTP/1.1 with a few megabytes read ahead, the server waits while this thread does not read
// (an app off the screen on Android).
//
//   ai::LocalModelFiles files(folder);
//   connect(&files, &ai::LocalModelFiles::finished, …);   // (id, ok, error)
//   files.download(*ai::localModel(u"lfm3-vl-2b"_s));       // one model at a time
//   files.has(model);  files.remove(model);

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
    /// Where a model's file is (or will be).
    QString pathOf(const LocalFile &file) const;
    /// Every file of `model` is there, whole and checked.
    bool has(const LocalModel &model) const;
    /// The bytes of `model` on the device: its whole files and the part of one downloaded so far.
    qint64 bytesHere(const LocalModel &model) const;

    /// Downloads what `model` lacks, file after file; finished() says how it ended. Nothing while
    /// another download runs (one at a time).
    void download(const LocalModel &model);
    /// Stops the download; what it has stays for the next one. finished(id, false, "").
    void cancel();
    /// Deletes `model`'s files and partial downloads (its running download stops first).
    bool remove(const LocalModel &model);

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
    void end(bool ok, const QString &error);
    qint64 wholeBytes() const;

    QString m_folder;
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
