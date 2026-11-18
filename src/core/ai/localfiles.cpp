#include "ai/localfiles.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStorageInfo>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ai {

namespace {

/// No bytes for this long: the download gives up (and can go on later).
constexpr int StallMs = 60 * 1000;
/// A file is checked in steps of this size, the event loop running in between.
constexpr qint64 CheckStep = 8 * 1024 * 1024;
/// What a download holds in memory before it is written; beyond it the network waits. Without
/// it everything the server sends while this thread stands still piles up in memory — Android
/// pauses the app's event loop while the app is off the screen, and a gigabyte arrives within
/// the minute a background app is left running.
constexpr qint64 ReadAheadBytes = 4 * 1024 * 1024;

QString megabytes(qint64 bytes)
{
    return QString::number((bytes + 1024 * 1024 - 1) / (1024 * 1024)) + u" MB"_s;
}

} // namespace

LocalModelFiles::LocalModelFiles(const QString &folder, QObject *parent) : QObject(parent), m_folder(folder) { }

LocalModelFiles::~LocalModelFiles()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void LocalModelFiles::setNetwork(QNetworkAccessManager *network)
{
    m_network = network;
}

QString LocalModelFiles::pathOf(const LocalFile &file) const
{
    return m_folder + u'/' + file.name;
}

QString LocalModelFiles::markerOf(const LocalModel &model) const
{
    return m_folder + u'/' + model.id + u".downloaded"_s;
}

bool LocalModelFiles::whole(const LocalModel &model) const
{
    const QList<LocalFile> files = model.files();
    for (const LocalFile &file : files) {
        const QFileInfo info(pathOf(file));
        if (!info.isFile() || info.size() != file.bytes)
            return false;
    }
    return !files.isEmpty();
}

bool LocalModelFiles::has(const LocalModel &model) const
{
    return !model.id.isEmpty() && QFileInfo::exists(markerOf(model)) && whole(model);
}

void LocalModelFiles::adopt(const QList<LocalModel> &models)
{
    // Once: later the files of a model removed (kept for another one) or of one half-downloaded
    // (a shared file only) would look the same as an older version's.
    const QString adopted = m_folder + u"/.adopted"_s;
    if (QFileInfo::exists(adopted) || !QDir().mkpath(m_folder))
        return;
    for (const LocalModel &model : models) {
        if (!model.id.isEmpty() && whole(model)) {
            QFile marker(markerOf(model));
            if (marker.open(QIODevice::WriteOnly))
                marker.close();
        }
    }
    QFile done(adopted);
    if (done.open(QIODevice::WriteOnly))
        done.close();
}

qint64 LocalModelFiles::bytesHere(const LocalModel &model) const
{
    qint64 bytes = 0;
    const QList<LocalFile> files = model.files();
    for (const LocalFile &file : files) {
        const QFileInfo whole(pathOf(file));
        if (whole.isFile() && whole.size() == file.bytes)
            bytes += file.bytes;
        else
            bytes += std::min(QFileInfo(pathOf(file) + u".part"_s).size(), file.bytes);
    }
    return bytes;
}

qint64 LocalModelFiles::wholeBytes() const
{
    qint64 bytes = 0;
    const QList<LocalFile> files = m_model.files();
    for (const LocalFile &file : files) {
        const QFileInfo whole(pathOf(file));
        if (whole.isFile() && whole.size() == file.bytes)
            bytes += file.bytes;
    }
    return bytes;
}

void LocalModelFiles::download(const LocalModel &model)
{
    if (!m_id.isEmpty())
        return;
    m_model = model;
    m_id = model.id;
    m_cancelled = false;
    m_checking = false;
    m_total = model.bytes();
    m_received = bytesHere(model);
    m_lastProgress.start();
    if (!QDir().mkpath(m_folder)) {
        end(false, u"cannot make the folder %1"_s.arg(m_folder));
        return;
    }
    // Nothing missing (files another model shares, a part whole but not yet checked): nothing is
    // written, so no free space is needed — it is only marked downloaded.
    const QStorageInfo storage(m_folder);
    const qint64 missing = m_total - m_received;
    if (missing > 0 && storage.isValid() && storage.bytesAvailable() < missing + m_spareBytes) {
        end(false, u"not enough free space: %1 needed, %2 free"_s.arg(megabytes(missing + m_spareBytes),
                                                                      megabytes(storage.bytesAvailable())));
        return;
    }
    Q_EMIT progress();
    nextFile();
}

void LocalModelFiles::nextFile()
{
    const QList<LocalFile> files = m_model.files();
    for (const LocalFile &file : files) {
        const QFileInfo whole(pathOf(file));
        if (whole.isFile() && whole.size() == file.bytes)
            continue;
        // Not this model's file: a leftover of another size — no other model's either (a name in
        // the folder is one file: models share only the identical LocalFile, ai::LocalModel).
        if (whole.exists())
            QFile::remove(whole.filePath());
        begin(file);
        return;
    }
    end(true, QString());
}

void LocalModelFiles::begin(const LocalFile &file)
{
    m_file = file;
    m_part.setFileName(pathOf(file) + u".part"_s);
    if (m_part.size() > file.bytes)
        m_part.remove();
    m_resumedAt = m_part.size();
    m_started = false;
    if (m_resumedAt == file.bytes) {
        check(); // it was whole already (the app closed before it was checked)
        return;
    }
    if (!m_part.open(QIODevice::WriteOnly | QIODevice::Append)) {
        end(false, u"cannot write %1: %2"_s.arg(m_part.fileName(), m_part.errorString()));
        return;
    }
    if (!m_network) {
        m_ownNetwork = std::make_unique<QNetworkAccessManager>();
        m_network = m_ownNetwork.get();
    }
    QNetworkRequest request(file.url);
    request.setTransferTimeout(StallMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    // The file as it is (a checksum of its bytes follows), not a compressed transfer.
    request.setRawHeader("Accept-Encoding", "identity");
    // HTTP/1.1: only there does Qt make the server wait for a reader that stands still
    // (ReadAheadBytes) — over HTTP/2 it takes all the server sends —, and one file gains nothing
    // from HTTP/2.
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    if (m_resumedAt > 0)
        request.setRawHeader("Range", "bytes=" + QByteArray::number(m_resumedAt) + "-");
    m_reply = m_network->get(request);
    m_reply->setReadBufferSize(ReadAheadBytes);
    connect(m_reply, &QNetworkReply::readyRead, this, &LocalModelFiles::onData);
    connect(m_reply, &QNetworkReply::finished, this, &LocalModelFiles::onReplyFinished);
}

void LocalModelFiles::onData()
{
    if (!m_reply)
        return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200 && status != 206)
        return; // an error page: read by onReplyFinished
    if (!m_started) {
        m_started = true;
        // Asked for the rest, answered with the whole file (a server without ranges): from the start.
        if (status == 200 && m_resumedAt > 0) {
            m_part.resize(0);
            m_part.seek(0);
            m_resumedAt = 0;
        }
    }
    const QByteArray bytes = m_reply->readAll();
    // Never more than the file's own size, whatever the server sends: a longer answer is not the
    // model's file (and would fill the storage before any checksum is looked at).
    const bool tooLong = m_part.size() + bytes.size() > m_file.bytes;
    if (tooLong || m_part.write(bytes) != bytes.size()) {
        const QString why = tooLong ? u"the server sent more than the file's %1: not the file %2"_s.arg(
                                              megabytes(m_file.bytes), m_file.name)
                                    : u"cannot write %1: %2"_s.arg(m_part.fileName(), m_part.errorString());
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
        m_part.close();
        if (tooLong)
            m_part.remove();
        end(false, why);
        return;
    }
    m_received = wholeBytes() + m_part.size();
    if (m_lastProgress.elapsed() >= 100) {
        m_lastProgress.restart();
        Q_EMIT progress();
    }
}

void LocalModelFiles::onReplyFinished()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    m_reply = nullptr;
    reply->deleteLater();
    if (reply->bytesAvailable() > 0 && reply->error() == QNetworkReply::NoError) {
        m_reply = reply; // the last bytes
        onData();
        m_reply = nullptr;
        if (m_id.isEmpty())
            return; // (writing them failed)
    }
    m_part.close();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (m_cancelled) {
        end(false, QString());
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        // What arrived stays: the next download goes on from there.
        end(false, status >= 400 ? u"the server answered with an error (HTTP %1)"_s.arg(status) : reply->errorString());
        return;
    }
    if (status != 200 && status != 206) {
        end(false, u"the server answered with HTTP %1"_s.arg(status));
        return;
    }
    const qint64 size = QFileInfo(m_part.fileName()).size();
    if (size != m_file.bytes) {
        if (size > m_file.bytes)
            m_part.remove();
        end(false, u"the download ended early (%1 of %2)"_s.arg(megabytes(size), megabytes(m_file.bytes)));
        return;
    }
    check();
}

void LocalModelFiles::check()
{
    m_checking = true;
    m_received = wholeBytes() + m_file.bytes;
    Q_EMIT progress();
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    if (!m_part.open(QIODevice::ReadOnly)) {
        end(false, u"cannot read %1: %2"_s.arg(m_part.fileName(), m_part.errorString()));
        return;
    }
    QTimer::singleShot(0, this, &LocalModelFiles::checkSome);
}

void LocalModelFiles::checkSome()
{
    if (m_id.isEmpty() || !m_hash)
        return; // cancelled meanwhile
    const QByteArray bytes = m_part.read(CheckStep);
    m_hash->addData(bytes);
    if (!m_part.atEnd() && !bytes.isEmpty()) {
        QTimer::singleShot(0, this, &LocalModelFiles::checkSome);
        return;
    }
    m_part.close();
    const QByteArray sum = m_hash->result().toHex();
    m_hash.reset();
    m_checking = false;
    if (sum != m_file.sha256.toLower()) {
        m_part.remove();
        end(false, u"the file %1 arrived damaged (its checksum differs): download it again"_s.arg(m_file.name));
        return;
    }
    QFile::remove(pathOf(m_file));
    if (!m_part.rename(pathOf(m_file))) {
        end(false, u"cannot name %1: %2"_s.arg(pathOf(m_file), m_part.errorString()));
        return;
    }
    nextFile();
}

void LocalModelFiles::cancel()
{
    if (m_id.isEmpty())
        return;
    m_cancelled = true;
    if (m_reply) {
        m_reply->abort(); // → onReplyFinished
        return;
    }
    m_hash.reset();
    m_part.close();
    end(false, QString());
}

void LocalModelFiles::end(bool ok, QString error)
{
    const QString id = m_id;
    m_id.clear();
    m_checking = false;
    m_hash.reset();
    if (m_part.isOpen())
        m_part.close();
    m_received = 0;
    m_total = 0;
    if (ok) {
        QFile marker(markerOf(m_model));
        if (!marker.open(QIODevice::WriteOnly)) {
            ok = false;
            error = u"cannot write %1: %2"_s.arg(marker.fileName(), marker.errorString());
        }
    }
    Q_EMIT progress();
    if (ok)
        Q_EMIT changed();
    Q_EMIT finished(id, ok, error);
}

bool LocalModelFiles::remove(const LocalModel &model, const QList<LocalModel> &models)
{
    if (m_id == model.id)
        cancel();
    bool ok = !QFileInfo::exists(markerOf(model)) || QFile::remove(markerOf(model));
    // What another model needs too stays (the same file, not just its name): one on the device, or
    // one being downloaded.
    QList<LocalFile> kept;
    for (const LocalModel &other : models) {
        const bool downloading = !m_id.isEmpty() && m_id == other.id;
        if (other.id == model.id || (!downloading && !has(other)))
            continue;
        kept.append(other.files());
    }
    const QList<LocalFile> files = model.files();
    for (const LocalFile &file : files) {
        if (kept.contains(file))
            continue;
        for (const QString &path : {pathOf(file), QString(pathOf(file) + u".part"_s)}) {
            if (QFileInfo::exists(path) && !QFile::remove(path))
                ok = false;
        }
    }
    Q_EMIT changed();
    return ok;
}

} // namespace brulion::ai
