#include "sync/webdavclient.h"

#include "log/slowscope.h"
#include "sync/pool_p.h"
#include "sync/request_p.h"
#include "sync/url.h"
#include "sync/util_p.h"
#include "util/paths.h"

#include <QPointer>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

using Header = QPair<QByteArray, QByteArray>;

/// Downloads of unknown size get this many times the base timeout.
constexpr int kUnknownSizeFactor = 10;
/// Bound for creating missing ancestors of the base URL.
constexpr int kMaxBaseDepth = 32;
/// Answers slower than this get an info line in the log (normal mode).
constexpr qint64 kSlowAnswerMs = 5000;

/// "https://h/a/b/" -> "https://h/a/"; std::nullopt at the root.
std::optional<QString> parentUrl(const QString &url)
{
    static const QRegularExpression re(u"^([a-z][a-z0-9+.-]*://[^/]+)(/.*)$"_s, QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(url);
    const QString origin = m.hasMatch() ? m.captured(1) : QString();
    const QString path = trimTrailingSlashes(m.hasMatch() ? m.captured(2) : url);
    const qsizetype slash = path.lastIndexOf(u'/');
    if (slash <= 0)
        return std::nullopt;
    return origin + path.left(slash) + u'/';
}

void stopPool(const std::weak_ptr<detail::JobPool> &pool)
{
    if (const std::shared_ptr<detail::JobPool> p = pool.lock())
        p->stop();
}

RemoteEntry entryFor(const VaultPath &path, const DavResource &res)
{
    return RemoteEntry{path, res.isCollection, res.etag, res.mtime, res.isCollection ? 0 : res.size};
}

} // namespace

/// State of one list() call.
struct WebDavClient::ListRun {
    ListOptions options;
    ListCallback done;
    /// Weak: the pool's idle callback owns this run (a strong pointer would be a cycle).
    std::weak_ptr<detail::JobPool> pool;
    QList<RemoteEntry> entries;
    QSet<VaultPath> seen;
    WebDavError error;
    int foldersDone = 0;
    int foldersKnown = 1; ///< the base folder
};

WebDavClient::WebDavClient(const QString &baseUrl, const QString &username, const QString &password,
                           HttpTransport *transport, QObject *parent)
    : QObject(parent)
    , m_baseUrl(trimTrailingSlashes(baseUrl.trimmed()))
    , m_username(username)
    , m_authorization(basicAuth(username, password))
    , m_secretPassword(password)
    , m_secretToken(QString::fromLatin1(m_authorization.mid(m_authorization.indexOf(' ') + 1)))
    , m_transport(transport)
    , m_basePathname(urlPathname(m_baseUrl))
{
}

WebDavClient::~WebDavClient() = default;

QString WebDavClient::identity() const
{
    return m_username + u'@' + m_baseUrl;
}

void WebDavClient::setTimeout(int msec)
{
    m_timeoutMs = qMax(0, msec);
}

void WebDavClient::setMinBytesPerSecond(qint64 bytesPerSecond)
{
    m_minBytesPerSecond = qMax<qint64>(1, bytesPerSecond);
}

void WebDavClient::setListConcurrency(int count)
{
    m_listConcurrency = qMax(1, count);
}

void WebDavClient::setLogTag(const QString &tag)
{
    m_logTag = tag;
}

qint64 WebDavClient::idleMs() const
{
    if (m_inFlight.isEmpty() || !m_lastActivity.isValid())
        return 0;
    return m_lastActivity.elapsed();
}

QStringList WebDavClient::inFlight(int max) const
{
    QStringList out;
    for (auto it = m_inFlight.cbegin(); it != m_inFlight.cend() && out.size() < max; ++it)
        out.append(it.value());
    return out;
}

QString WebDavClient::relativePath(const QUrl &url) const
{
    const QString full = url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
    const std::optional<QString> rel = relativeTo(urlPathname(full), m_basePathname);
    if (!rel)
        return full;
    if (!rel->isEmpty() && url.path().endsWith(u'/'))
        return *rel + u'/';
    return *rel;
}

// ---------------------------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------------------------

void WebDavClient::test(TestCallback done)
{
    propfind(QString(), 0, [done = std::move(done)](const WebDavError &error, const QList<DavResource> &) {
        done(WebDavTestResult{!error.isError(), error});
    });
}

void WebDavClient::list(const ListOptions &options, ListCallback done)
{
    auto run = std::make_shared<ListRun>();
    run->options = options;
    run->done = std::move(done);
    run->seen.insert(QString());
    const auto pool = std::make_shared<detail::JobPool>(m_listConcurrency, this);
    run->pool = pool;
    m_knownDirs.clear();
    m_remoteNames.clear();

    // Adds the job listing one folder (recursively through the pool).
    auto listFolder = std::make_shared<std::function<void(const QString &, bool)>>();
    *listFolder = [this, run, weakList = std::weak_ptr(listFolder)](const QString &raw, bool isBase) {
        const std::shared_ptr<detail::JobPool> folderPool = run->pool.lock();
        if (!folderPool)
            return;
        folderPool->add([this, run, raw, isBase, weakList](detail::JobPool::Done jobDone) {
            if (run->error.isError()) {
                jobDone();
                return;
            }
            if (run->options.cancelled && run->options.cancelled()) {
                run->error = WebDavError::make(WebDavErrorCode::Cancelled, u"Listing cancelled"_s, m_baseUrl);
                stopPool(run->pool);
                jobDone();
                return;
            }
            propfindRaw(raw, 1, [this, run, raw, isBase, weakList, jobDone](const WebDavError &error,
                                                                             const QList<DavResource> &resources) {
                if (error.isError()) {
                    // A sub-folder deleted while we were listing: skip it.
                    if (!(!isBase && error.is(WebDavErrorCode::NotFound))) {
                        if (!run->error.isError())
                            run->error = error;
                        stopPool(run->pool);
                    }
                    jobDone();
                    return;
                }
                const VaultPath folder = paths::normalize(raw);
                m_knownDirs.insert(folder);
                ++run->foldersDone;
                const QString requestUrl = joinUrl(m_baseUrl, raw, true);
                const auto recurse = weakList.lock();
                for (const DavResource &res : resources) {
                    const std::optional<QString> rawRel = relativeRaw(res.href, requestUrl);
                    if (!rawRel)
                        continue;
                    const VaultPath path = paths::normalize(*rawRel);
                    if (path.isEmpty() || path == folder || run->seen.contains(path))
                        continue;
                    run->seen.insert(path);
                    if (*rawRel != path)
                        m_remoteNames.insert(path, *rawRel);
                    run->entries.append(entryFor(path, res));
                    if (res.isCollection) {
                        m_knownDirs.insert(path);
                        if (recurse && !(run->options.skipDir && run->options.skipDir(path))) {
                            ++run->foldersKnown;
                            (*recurse)(*rawRel, false);
                        }
                    }
                }
                if (run->options.onProgress)
                    run->options.onProgress(run->foldersDone, run->foldersKnown, int(run->entries.size()));
                jobDone();
            });
        });
    };
    (*listFolder)(QString(), true);
    pool->start([this, run, listFolder]() mutable {
        ListResult result;
        result.error = run->error;
        if (!result.error.isError()) {
            result.entries = run->entries;
            std::sort(result.entries.begin(), result.entries.end(),
                      [](const RemoteEntry &a, const RemoteEntry &b) { return a.path < b.path; });
        }
        ListCallback callback = std::move(run->done);
        listFolder.reset();
        detail::post(this, [callback = std::move(callback), result = std::move(result)] { callback(result); });
    });
}

void WebDavClient::stat(const VaultPath &path, StatCallback done)
{
    const VaultPath p = paths::normalize(path);
    propfind(p, 0, [p, done = std::move(done)](const WebDavError &error, const QList<DavResource> &resources) {
        if (error.is(WebDavErrorCode::NotFound) || (!error.isError() && resources.isEmpty())) {
            done(StatResult{});
            return;
        }
        if (error.isError()) {
            done(StatResult{std::nullopt, error});
            return;
        }
        done(StatResult{entryFor(p, resources.first()), {}});
    });
}

void WebDavClient::get(const VaultPath &path, const GetOptions &options, GetCallback done)
{
    const QString url = urlFor(path);
    const int timeoutMs = options.size ? timeoutFor(*options.size) : unknownSizeTimeout();
    request("GET", url, {}, {}, timeoutMs, [this, url, done = std::move(done)](const HttpResponse &response) {
        if (const WebDavError error = check(response, "GET", url, {200}); error.isError()) {
            done(GetResult{{}, {}, error});
            return;
        }
        QByteArray etag = response.header("etag");
        if (etag.isEmpty())
            etag = response.header("oc-etag");
        done(GetResult{response.body, normalizeEtag(QString::fromUtf8(etag)), {}});
    });
}

void WebDavClient::put(const VaultPath &path, const QByteArray &data, const PutOptions &options, PutCallback done)
{
    const VaultPath p = paths::normalize(path);
    if (p.isEmpty()) {
        fail(WebDavError::make(WebDavErrorCode::Http, u"Cannot PUT the base folder"_s, m_baseUrl),
             [done = std::move(done)](const WebDavError &error) { done(PutResult{{}, error}); });
        return;
    }
    auto callback = std::make_shared<PutCallback>(std::move(done));
    auto finish = [this, p, callback](const HttpResponse &response) {
        if (const WebDavError error = check(response, "PUT", urlFor(p), {200, 201, 204}); error.isError()) {
            (*callback)(PutResult{{}, error});
            return;
        }
        QByteArray etag = response.header("oc-etag");
        if (etag.isEmpty())
            etag = response.header("etag");
        (*callback)(PutResult{normalizeEtag(QString::fromUtf8(etag)), {}});
    };
    ensureParents(p, false, [this, p, data, options, finish, callback](const WebDavError &error) {
        if (error.isError()) {
            (*callback)(PutResult{{}, error});
            return;
        }
        sendPut(p, data, options, [this, p, data, options, finish, callback](const HttpResponse &response) {
            if (response.status != 409) {
                finish(response);
                return;
            }
            // The parent folder is missing after all (deleted by someone else): create, retry once.
            ensureParents(p, true, [this, p, data, options, finish, callback](const WebDavError &retryError) {
                if (retryError.isError()) {
                    (*callback)(PutResult{{}, retryError});
                    return;
                }
                sendPut(p, data, options, finish);
            });
        });
    });
}

void WebDavClient::remove(const VaultPath &path, const RemoveOptions &options, DoneCallback done)
{
    const VaultPath p = paths::normalize(path);
    if (p.isEmpty()) {
        fail(WebDavError::make(WebDavErrorCode::Http, u"Refusing to DELETE the base folder"_s, m_baseUrl),
             std::move(done));
        return;
    }
    const QString url = urlFor(p);
    QList<QPair<QByteArray, QByteArray>> headers;
    if (!options.ifMatch.isEmpty())
        headers.append(Header{"If-Match", quoteEtag(options.ifMatch).toUtf8()});
    request("DELETE", url, headers, {}, m_timeoutMs, [this, p, url, done = std::move(done)](const HttpResponse &response) {
        if (response.status == 404) {
            done({});
            return;
        }
        if (const WebDavError error = check(response, "DELETE", url, {200, 202, 204}); error.isError()) {
            done(error);
            return;
        }
        forgetKnown(p);
        done({});
    });
}

void WebDavClient::move(const VaultPath &from, const VaultPath &to, const MoveOptions &options, DoneCallback done)
{
    const VaultPath p = paths::normalize(from);
    const VaultPath q = paths::normalize(to);
    if (p.isEmpty() || q.isEmpty()) {
        fail(WebDavError::make(WebDavErrorCode::Http, u"Refusing to MOVE the base folder"_s, m_baseUrl),
             std::move(done));
        return;
    }
    if (p == q || q.startsWith(p + u'/')) {
        fail(WebDavError::make(WebDavErrorCode::Http, u"Refusing to MOVE %1 into itself"_s.arg(p), m_baseUrl),
             std::move(done));
        return;
    }
    ensureParents(q, false, [this, p, q, options, done = std::move(done)](const WebDavError &parentError) {
        if (parentError.isError()) {
            done(parentError);
            return;
        }
        const QString url = urlFor(p, options.folder);
        const QString spelledTo = serverName(q);
        const QString destination = joinUrl(m_baseUrl, spelledTo, options.folder);
        const QList<QPair<QByteArray, QByteArray>> headers = {
                {"Destination", destination.toUtf8()},
                {"Overwrite", options.overwrite ? "T" : "F"},
        };
        request("MOVE", url, headers, {}, m_timeoutMs,
                [this, p, q, spelledTo, url, done](const HttpResponse &response) {
                    if (const WebDavError error = check(response, "MOVE", url, {201, 204}); error.isError()) {
                        done(error);
                        return;
                    }
                    rebaseKnown(p, q, spelledTo);
                    done({});
                });
    });
}

void WebDavClient::listFolder(const VaultPath &path, ListCallback done)
{
    const VaultPath folder = paths::normalize(path);
    const QString raw = serverName(folder);
    propfindRaw(
            raw, 1,
            [this, folder, raw, done = std::move(done)](const WebDavError &error, const QList<DavResource> &resources) {
                ListResult result;
                result.error = error;
                if (!error.isError()) {
                    // Callers decide from this answer that a folder is empty (and DELETE it with
                    // everything in it): an entry that cannot be named as a child of the folder
                    // (an href elsewhere, a name such as "\" that collapses onto the folder) is not
                    // skipped but fails the listing, and so does a second entry for the folder.
                    const QString requestUrl = joinUrl(m_baseUrl, raw, true);
                    bool self = false;
                    for (const DavResource &res : resources) {
                        const std::optional<QString> rawRel = relativeRaw(res.href, requestUrl);
                        const VaultPath child = rawRel ? paths::normalize(*rawRel) : QString();
                        if (rawRel && child == folder && !std::exchange(self, true))
                            continue; // the folder itself
                        const bool isChild =
                                rawRel && child != folder && (folder.isEmpty() || child.startsWith(folder + u'/'));
                        if (!isChild) {
                            const QString message = u"PROPFIND %1: an entry that is not a child of the folder (%2)"_s;
                            result.error = WebDavError::make(WebDavErrorCode::Parse, message.arg(folder, res.href),
                                                             requestUrl);
                            result.entries.clear();
                            break;
                        }
                        result.entries.append(entryFor(child, res));
                    }
                    std::sort(result.entries.begin(), result.entries.end(),
                              [](const RemoteEntry &a, const RemoteEntry &b) { return a.path < b.path; });
                }
                done(result);
            });
}

void WebDavClient::mkcol(const VaultPath &path, DoneCallback done)
{
    const VaultPath p = paths::normalize(path);
    const QString url = urlFor(p, true);
    request("MKCOL", url, {}, {}, m_timeoutMs, [this, p, url, done = std::move(done)](const HttpResponse &response) {
        if (response.status != 405) {
            if (const WebDavError error = check(response, "MKCOL", url, {200, 201}); error.isError()) {
                done(error);
                return;
            }
        }
        m_knownDirs.insert(p);
        done({});
    });
}

void WebDavClient::mkdirs(const VaultPath &path, DoneCallback done)
{
    const VaultPath p = paths::normalize(path);
    if (p.isEmpty()) {
        ensureBase(std::move(done));
        return;
    }
    // ensureParents() creates the folders of a file path: "<p>/" names a file inside <p>.
    ensureParents(p + u'/', false, std::move(done));
}

void WebDavClient::ensureBase(DoneCallback done)
{
    mkcolUrl(m_baseUrl + u'/', 0, [this, done = std::move(done)](const WebDavError &error) {
        if (!error.isError())
            m_knownDirs.insert(QString());
        done(error);
    });
}

// ---------------------------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------------------------

QString WebDavClient::serverName(const VaultPath &path) const
{
    if (const auto it = m_remoteNames.constFind(path); it != m_remoteNames.cend())
        return *it;
    // A new file or folder inside a folder the server spells differently (NFD): keep that
    // folder's spelling, else a normalisation-sensitive server sees a missing parent (409).
    for (qsizetype slash = path.lastIndexOf(u'/'); slash > 0; slash = path.lastIndexOf(u'/', slash - 1)) {
        if (const auto it = m_remoteNames.constFind(path.left(slash)); it != m_remoteNames.cend())
            return *it + path.mid(slash);
    }
    return path;
}

QString WebDavClient::urlFor(const VaultPath &path, bool dir) const
{
    return joinUrl(m_baseUrl, serverName(paths::normalize(path)), dir);
}

std::optional<QString> WebDavClient::relativeRaw(const QString &href, const QString &requestUrl) const
{
    return relativeTo(hrefPathname(href, requestUrl), m_basePathname);
}

int WebDavClient::unknownSizeTimeout() const
{
    const qint64 ms = qint64(m_timeoutMs) * kUnknownSizeFactor;
    return static_cast<int>(qMin<qint64>(ms, std::numeric_limits<int>::max()));
}

int WebDavClient::timeoutFor(qint64 bytes) const
{
    if (m_timeoutMs <= 0)
        return 0;
    const double extra = std::ceil(static_cast<double>(qMax<qint64>(0, bytes)) * 1000.0
                                   / static_cast<double>(m_minBytesPerSecond));
    const double total = static_cast<double>(m_timeoutMs) + extra;
    return total >= std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : static_cast<int>(total);
}

void WebDavClient::request(const QByteArray &method, const QString &url, QList<QPair<QByteArray, QByteArray>> headers,
                           const QByteArray &body, int timeoutMs, ResponseCallback done)
{
    HttpRequest req;
    req.method = method;
    req.url = QUrl(url, QUrl::TolerantMode);
    req.headers = {{"Authorization", m_authorization}, {"X-Requested-With", "XMLHttpRequest"}};
    req.headers += headers;
    req.body = body;
    req.timeoutMs = timeoutMs;
    const quint64 id = ++m_nextRequestId;
    if (m_inFlight.isEmpty())
        m_lastActivity.start(); // idle time counts from the first open request
    const QString shown = relativePath(req.url);
    m_inFlight.insert(id, QString::fromLatin1(method) + u' ' + (shown.isEmpty() ? u"(base)"_s : shown));
    req.onProgress = [self = QPointer<WebDavClient>(this)](qint64, qint64) {
        if (self)
            self->m_lastActivity.start();
    };
    detail::sendRequest(m_transport, this, req, std::move(done), [this, id](const HttpExchange &exchange) {
        m_inFlight.remove(id);
        m_lastActivity.start();
        logExchange(exchange);
        Q_EMIT exchanged(exchange);
    });
}

void WebDavClient::logExchange(const HttpExchange &exchange) const
{
    const QString prefix = m_logTag.isEmpty() ? QString() : m_logTag + u' ';
    const QString path = relativePath(exchange.url);
    const QString what = QString::fromLatin1(exchange.method) + u' ' + (path.isEmpty() ? u"(base)"_s : path);
    if (exchange.status == 0) {
        if (exchange.timedOut) {
            qCWarning(detail::lcNet).noquote()
                    << u"%1%2 timed out after %3"_s.arg(prefix, what, logging::formatMs(exchange.elapsedMs));
        } else {
            qCWarning(detail::lcNet).noquote() << u"%1%2 failed after %3: %4"_s.arg(
                    prefix, what, logging::formatMs(exchange.elapsedMs),
                    exchange.networkError.isEmpty() ? u"no response"_s : exchange.networkError);
        }
        return;
    }
    if (exchange.elapsedMs >= kSlowAnswerMs) {
        qCInfo(detail::lcNet).noquote() << u"%1slow answer: %2 -> %3 in %4 (%5)"_s.arg(
                prefix, what, QString::number(exchange.status), logging::formatMs(exchange.elapsedMs),
                logging::formatBytes(exchange.bytesSent + exchange.bytesReceived));
        return;
    }
    if (!detail::lcNet().isDebugEnabled())
        return;
    QString details = u"sent %1, received %2"_s.arg(logging::formatBytes(exchange.bytesSent),
                                                    logging::formatBytes(exchange.bytesReceived));
    if (!exchange.protocol.isEmpty())
        details += u", "_s + QString::fromLatin1(exchange.protocol);
    qCDebug(detail::lcNet).noquote() << u"%1%2 -> %3 in %4 (%5)"_s.arg(prefix, what, QString::number(exchange.status),
                                                                       logging::formatMs(exchange.elapsedMs), details);
}

WebDavError WebDavClient::check(const HttpResponse &response, const QByteArray &method, const QString &url,
                                std::initializer_list<int> ok) const
{
    if (response.status == 0) {
        if (response.timedOut)
            return WebDavError{WebDavErrorCode::Timeout, 0, response.networkError, url};
        const QString reason = response.networkError.isEmpty() ? u"no response"_s : response.networkError;
        return WebDavError{WebDavErrorCode::Network, 0,
                           u"%1 %2 failed: %3"_s.arg(QString::fromLatin1(method), url, reason), url};
    }
    if (std::find(ok.begin(), ok.end(), response.status) != ok.end())
        return {};
    return WebDavError::fromStatus(response.status,
                                   u"%1 %2 failed: HTTP %3"_s.arg(QString::fromLatin1(method), url, QString::number(response.status)),
                                   url);
}

void WebDavClient::propfindRaw(const QString &rawPath, int depth, PropfindCallback done)
{
    const QString url = joinUrl(m_baseUrl, rawPath, true);
    const QList<QPair<QByteArray, QByteArray>> headers = {
        {"Depth", QByteArray::number(depth)},
        {"Content-Type", "application/xml; charset=utf-8"},
    };
    request("PROPFIND", url, headers, propfindBody(), m_timeoutMs,
            [this, url, done = std::move(done)](const HttpResponse &response) {
                if (const WebDavError error = check(response, "PROPFIND", url, {200, 207}); error.isError()) {
                    done(error, {});
                    return;
                }
                const MultistatusResult parsed = parseMultistatus(response.body);
                if (!parsed.ok()) {
                    const QString path = relativePath(QUrl(url, QUrl::TolerantMode));
                    qCInfo(detail::lcNet).noquote()
                            << u"%1PROPFIND %2 -> %3 %4: not a WebDAV answer (a login page? check the server address)"_s
                                       .arg(m_logTag.isEmpty() ? QString() : m_logTag + u' ',
                                            path.isEmpty() ? u"(base)"_s : path, QString::number(response.status),
                                            QString::fromLatin1(response.header("content-type").left(60)));
                    done(WebDavError{WebDavErrorCode::Parse, response.status, parsed.error, url}, {});
                    return;
                }
                done({}, parsed.resources);
            });
}

void WebDavClient::propfind(const VaultPath &path, int depth, PropfindCallback done)
{
    propfindRaw(serverName(paths::normalize(path)), depth, std::move(done));
}

void WebDavClient::sendPut(const VaultPath &path, const QByteArray &data, const PutOptions &options,
                           ResponseCallback done)
{
    QList<QPair<QByteArray, QByteArray>> headers = {{"Content-Type", "application/octet-stream"}};
    if (!options.ifMatch.isEmpty())
        headers.append(Header{"If-Match", quoteEtag(options.ifMatch).toUtf8()});
    if (options.ifNoneMatch)
        headers.append(Header{"If-None-Match", "*"});
    if (options.mtime)
        headers.append(Header{"X-OC-MTime", QByteArray::number(static_cast<qint64>(std::floor(*options.mtime / 1000.0)))});
    request("PUT", urlFor(path), headers, data, timeoutFor(data.size()), std::move(done));
}

void WebDavClient::ensureParents(const VaultPath &path, bool force, DoneCallback done)
{
    QStringList segments = path.split(u'/');
    segments.removeLast();
    ensureParentsFrom(segments, 0, QString(), force, path, std::move(done));
}

void WebDavClient::ensureParentsFrom(const QStringList &segments, int index, QString dir, bool force,
                                     const VaultPath &path, DoneCallback done)
{
    for (; index < segments.size(); ++index) {
        dir = dir.isEmpty() ? segments.at(index) : dir + u'/' + segments.at(index);
        if (force || !m_knownDirs.contains(dir))
            break;
    }
    if (index >= segments.size()) {
        done({});
        return;
    }
    const QString url = urlFor(dir, true);
    request("MKCOL", url, {}, {}, m_timeoutMs,
            [this, segments, index, dir, force, path, url, done = std::move(done)](const HttpResponse &response) {
                if (response.status == 409 && !force) {
                    // A folder above is missing although we thought it existed: start over.
                    ensureParents(path, true, done);
                    return;
                }
                if (response.status != 405) { // 405: exists already
                    if (const WebDavError error = check(response, "MKCOL", url, {200, 201}); error.isError()) {
                        done(error);
                        return;
                    }
                }
                m_knownDirs.insert(dir);
                ensureParentsFrom(segments, index + 1, dir, force, path, done);
            });
}

void WebDavClient::mkcolUrl(const QString &url, int depth, DoneCallback done)
{
    request("MKCOL", url, {}, {}, m_timeoutMs, [this, url, depth, done = std::move(done)](const HttpResponse &response) {
        if (response.status == 405 || response.status == 200 || response.status == 201) {
            done({});
            return;
        }
        const std::optional<QString> parent = response.status == 409 && depth < kMaxBaseDepth ? parentUrl(url) : std::nullopt;
        if (!parent) {
            done(check(response, "MKCOL", url, {200, 201}));
            return;
        }
        mkcolUrl(*parent, depth + 1, [this, url, done](const WebDavError &error) {
            if (error.isError()) {
                done(error);
                return;
            }
            request("MKCOL", url, {}, {}, m_timeoutMs, [this, url, done](const HttpResponse &retry) {
                done(retry.status == 405 ? WebDavError() : check(retry, "MKCOL", url, {200, 201}));
            });
        });
    });
}

void WebDavClient::rebaseKnown(const VaultPath &from, const VaultPath &to, const QString &spelledTo)
{
    const QString spelledFrom = serverName(from);
    const auto within = [&from](const QString &path) { return path == from || path.startsWith(from + u'/'); };
    QSet<QString> dirs;
    for (const QString &dir : std::as_const(m_knownDirs))
        dirs.insert(within(dir) ? to + dir.mid(from.size()) : dir);
    m_knownDirs = dirs;
    QHash<VaultPath, QString> names;
    for (auto it = m_remoteNames.cbegin(); it != m_remoteNames.cend(); ++it) {
        if (!within(it.key())) {
            names.insert(it.key(), it.value());
            continue;
        }
        const VaultPath key = to + it.key().mid(from.size());
        const QString spelled = spelledTo + it.value().mid(spelledFrom.size());
        if (spelled != key)
            names.insert(key, spelled);
    }
    if (spelledTo != to)
        names.insert(to, spelledTo);
    m_remoteNames = names;
}

void WebDavClient::forgetKnown(const VaultPath &path)
{
    const QString prefix = path + u'/';
    m_knownDirs.removeIf([&](const QString &dir) { return dir == path || dir.startsWith(prefix); });
    m_remoteNames.removeIf([&](const QHash<VaultPath, QString>::iterator it) {
        return it.key() == path || it.key().startsWith(prefix);
    });
}

void WebDavClient::fail(const WebDavError &error, DoneCallback done)
{
    detail::post(this, [error, done = std::move(done)] { done(error); });
}

} // namespace brulion::sync
