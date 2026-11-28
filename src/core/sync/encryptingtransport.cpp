#include "sync/encryptingtransport.h"

#include "sync/davxml.h"
#include "sync/url.h"

#include <QIODevice>
#include <QUrl>

#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

/// The layer's answer in the server's place: a status of one request's failure (4xx: the engine
/// reports that file and goes on with the others), the reason in the error's message.
HttpResponse refused(int status, const QString &why)
{
    HttpResponse r;
    r.status = status;
    r.reason = why;
    r.body = why.toUtf8();
    r.headers.insert("content-type", "text/plain; charset=utf-8");
    return r;
}

/// A file the server gave back changed, cut or not of this notebook's key.
HttpResponse undecryptable()
{
    return refused(422, u"the file on the server cannot be decrypted: changed there, or of another key"_s);
}

bool isSuccess(const HttpResponse &response)
{
    return response.status >= 200 && response.status < 300;
}

/// An upload's body encrypted as Qt reads it: the header, then the chunks of the device's
/// contents. Sequential, its size known (the Content-Length).
class EncryptingDevice : public QIODevice {
public:
    EncryptingDevice(std::shared_ptr<QIODevice> plain, const encryption::Keys &keys)
        : m_plain(std::move(plain)), m_encryptor(keys), m_size(encryption::encryptedSize(m_plain->size()))
    {
        m_buffer = m_encryptor.header();
        open(QIODevice::ReadOnly);
    }
    bool isSequential() const override { return true; }
    qint64 size() const override { return m_size; }
    qint64 bytesAvailable() const override { return m_buffer.size() - m_at + QIODevice::bytesAvailable(); }
    bool atEnd() const override { return m_finished && m_at >= m_buffer.size(); }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (m_at >= m_buffer.size() && !fill())
            return m_failed ? -1 : 0;
        const qint64 n = std::min(maxSize, m_buffer.size() - m_at);
        std::memcpy(data, m_buffer.constData() + m_at, std::size_t(n));
        m_at += n;
        return n;
    }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    /// The next encrypted bytes into the buffer; false at the end (or when the file failed).
    bool fill()
    {
        m_buffer.clear();
        m_at = 0;
        while (m_buffer.isEmpty() && !m_finished) {
            const QByteArray plain = m_plain->read(HttpRequest::StreamPieceBytes);
            if (plain.isEmpty()) {
                // (A file shorter than its size makes a body shorter than the Content-Length.)
                m_failed = m_plain->bytesAvailable() > 0 || (!m_plain->isSequential() && !m_plain->atEnd());
                m_buffer = m_encryptor.finish();
                m_finished = true;
            } else {
                m_buffer = m_encryptor.add(plain);
            }
        }
        return !m_buffer.isEmpty();
    }

    std::shared_ptr<QIODevice> m_plain;
    encryption::Encryptor m_encryptor;
    qint64 m_size;
    QByteArray m_buffer;
    qint64 m_at = 0;
    bool m_finished = false;
    bool m_failed = false;
};

/// `plain` handed to `sink` in pieces of at most StreamPieceBytes.
bool handOver(const std::function<bool(QByteArrayView)> &sink, QByteArrayView plain)
{
    for (qsizetype at = 0; at < plain.size(); at += HttpRequest::StreamPieceBytes) {
        if (!sink(plain.sliced(at, std::min(HttpRequest::StreamPieceBytes, plain.size() - at))))
            return false;
    }
    return true;
}


using Folder = EncryptingTransport::Folder;

std::optional<QString> serverUrl(const Folder &folder, const QUrl &url, HttpResponse *refusal)
{
    const QString encoded = url.toString(QUrl::FullyEncoded);
    const std::optional<QString> rel = relativeTo(urlPathname(encoded), folder.basePathname);
    if (!rel) {
        *refusal = refused(403, u"outside the encrypted folder"_s);
        return std::nullopt;
    }
    const bool dir = url.path().endsWith(u'/');
    if (rel->isEmpty())
        return folder.baseUrl + (dir ? u"/"_s : QString());
    const std::optional<QString> path = encryption::encryptPath(folder.keys, *rel);
    if (!path) {
        *refusal = refused(400, u"the name is too long to encrypt"_s);
        return std::nullopt;
    }
    return joinUrl(folder.baseUrl, *path, dir);
}

/// A listed resource as the client should see it: its href and size decrypted; nullopt for one
/// that is not the notebook's (the key file, a README, another app's file, one changed there).
std::optional<DavResource> decryptedResource(const Folder &folder, const QString &baseHref, DavResource res)
{
    const std::optional<QString> rel = relativeTo(hrefPathname(res.href, folder.baseUrl + u'/'), folder.basePathname);
    if (!rel)
        return std::nullopt;
    const std::optional<QString> path = rel->isEmpty() ? std::optional<QString>(QString()) : encryption::decryptPath(folder.keys, *rel);
    const std::optional<qint64> size = res.isCollection ? std::optional<qint64>(0) : encryption::plainSize(res.size);
    if (!path || !size)
        return std::nullopt;
    res.size = *size;
    res.href = baseHref + u'/' + encodePath(*path);
    if (res.isCollection && !res.href.endsWith(u'/'))
        res.href += u'/';
    return res;
}

HttpResponse decryptedListing(const Folder &folder, const HttpResponse &response)
{
    const MultistatusResult parsed = parseMultistatus(response.body);
    if (!parsed.ok())
        return response; // (the client reports it as the server's)
    const QString baseHref = QUrl(folder.baseUrl).path(QUrl::FullyEncoded);
    QList<DavResource> plain;
    for (const DavResource &res : parsed.resources) {
        if (std::optional<DavResource> decrypted = decryptedResource(folder, baseHref, res))
            plain.append(*decrypted);
    }
    HttpResponse out = response;
    out.body = writeMultistatus(plain);
    out.headers.remove("content-length");
    return out;
}

/// A whole answer's body decrypted.
HttpResponse decryptedBody(const Folder &folder, const HttpResponse &response)
{
    if (!isSuccess(response))
        return response;
    const std::optional<crypto::SecureBytes> plain = encryption::open(folder.keys, response.body);
    if (!plain)
        return undecryptable();
    HttpResponse out = response;
    out.body = plain->toByteArray();
    out.headers.insert("content-length", QByteArray::number(out.body.size()));
    return out;
}

/// A download decrypted chunk by chunk into the request's sink: each chunk checked before its
/// plaintext goes on, the last one when the answer has come.
class StreamedDownload {
public:
    StreamedDownload(const encryption::Keys &keys, std::function<bool(QByteArrayView)> sink)
        : m_decryptor(keys), m_sink(std::move(sink))
    {
    }

    /// For the inner request's bodySink.
    bool piece(QByteArrayView sealed)
    {
        const std::optional<QByteArray> plain = m_decryptor.add(sealed);
        m_failed = !plain;
        return plain && handOver(m_sink, *plain);
    }

    /// The answer as the client should see it.
    HttpResponse finish(const HttpResponse &response)
    {
        if (m_failed)
            return undecryptable();
        if (!isSuccess(response))
            return response;
        const std::optional<QByteArray> last = m_decryptor.finish();
        if (!last)
            return undecryptable();
        if (!handOver(m_sink, *last)) {
            HttpResponse stopped;
            stopped.networkError = u"The answer could not be kept"_s;
            return stopped;
        }
        return response;
    }

private:
    encryption::Decryptor m_decryptor;
    std::function<bool(QByteArrayView)> m_sink;
    bool m_failed = false;
};

} // namespace

EncryptingTransport::EncryptingTransport(HttpTransport *inner, const QString &baseUrl, encryption::Keys keys)
    : m_inner(inner)
{
    const QString base = trimTrailingSlashes(baseUrl.trimmed());
    m_folder = std::make_shared<const Folder>(Folder{base, urlPathname(base), std::move(keys)});
}

std::optional<HttpRequest> EncryptingTransport::encrypted(const HttpRequest &request, HttpResponse *refusal) const
{
    HttpRequest out = request;
    const std::optional<QString> url = serverUrl(*m_folder, request.url, refusal);
    if (!url || !encryptDestination(out, refusal))
        return std::nullopt;
    out.url = QUrl(*url, QUrl::StrictMode);
    if (request.method == "PUT")
        encryptBody(out);
    // An encrypted answer is larger: a file by its header and tags, a listing by its longer names.
    if (request.maxBodyBytes > 0)
        out.maxBodyBytes = request.method == "PROPFIND" ? request.maxBodyBytes * 3
                                                        : encryption::encryptedSize(request.maxBodyBytes);
    // (Never the ciphertext to whoever reads an answer while it comes.)
    out.onData = nullptr;
    return out;
}

bool EncryptingTransport::encryptDestination(HttpRequest &request, HttpResponse *refusal) const
{
    for (auto &[name, value] : request.headers) {
        if (name.compare("destination", Qt::CaseInsensitive) != 0)
            continue;
        const std::optional<QString> destination =
                serverUrl(*m_folder, QUrl(QString::fromUtf8(value), QUrl::TolerantMode), refusal);
        if (!destination)
            return false;
        value = destination->toUtf8();
    }
    return true;
}

void EncryptingTransport::encryptBody(HttpRequest &request) const
{
    if (request.bodyDevice)
        request.bodyDevice = std::make_shared<EncryptingDevice>(request.bodyDevice, m_folder->keys);
    else
        request.body = encryption::seal(m_folder->keys, request.body);
}

void EncryptingTransport::send(const HttpRequest &request, HttpCallback done)
{
    if (!m_inner) {
        HttpResponse none;
        none.networkError = u"No HTTP transport"_s;
        done(none);
        return;
    }
    HttpResponse refusal;
    std::optional<HttpRequest> out = encrypted(request, &refusal);
    if (!out) {
        done(refusal);
        return;
    }
    if (request.method == "GET") {
        sendGet(std::move(*out), std::move(done));
        return;
    }
    if (request.method == "PROPFIND") {
        m_inner->send(*out, [folder = m_folder, done = std::move(done)](const HttpResponse &response) {
            done(response.status == 207 ? decryptedListing(*folder, response) : response);
        });
        return;
    }
    m_inner->send(*out, std::move(done));
}

void EncryptingTransport::sendGet(HttpRequest request, HttpCallback done)
{
    if (!request.bodySink) {
        m_inner->send(request, [folder = m_folder, done = std::move(done)](const HttpResponse &response) {
            done(decryptedBody(*folder, response));
        });
        return;
    }
    auto download = std::make_shared<StreamedDownload>(m_folder->keys, std::move(request.bodySink));
    request.bodySink = [download](QByteArrayView piece) { return download->piece(piece); };
    m_inner->send(request, [download, done = std::move(done)](const HttpResponse &response) {
        done(download->finish(response));
    });
}

} // namespace brulion::sync
