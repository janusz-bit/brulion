#include "sync/syncthing.h"

#include "sync/request_p.h"
#include "sync/url.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <cmath>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

const QRegularExpression &windowsDrive()
{
    static const QRegularExpression re(u"^[a-z]:"_s, QRegularExpression::CaseInsensitiveOption);
    return re;
}

bool isWithinFsPath(const QString &path, const QString &dir, bool caseInsensitive)
{
    const Qt::CaseSensitivity cs = caseInsensitive ? Qt::CaseInsensitive : Qt::CaseSensitive;
    if (dir == u"/"_s)
        return path.startsWith(u'/');
    return path.compare(dir, cs) == 0 || path.startsWith(dir + u'/', cs);
}

/// A non-negative count from the REST API (bytes, errors). Garbage is clamped: negative or not a
/// number -> 0, beyond the qint64 range -> its maximum (a plain cast would be undefined).
qint64 jsonCount(const QJsonValue &value)
{
    if (!value.isDouble())
        return 0;
    const double d = value.toDouble();
    if (!(d > 0))
        return 0;
    if (d >= 9223372036854775808.0) // 2^63
        return std::numeric_limits<qint64>::max();
    return value.toInteger(static_cast<qint64>(d)); // exact for whole numbers beyond 2^53
}

/// a + b for non-negative counts, saturating instead of overflowing, as an int.
int sumCounts(qint64 a, qint64 b)
{
    const qint64 sum = a > std::numeric_limits<qint64>::max() - b ? std::numeric_limits<qint64>::max() : a + b;
    return static_cast<int>(qMin<qint64>(sum, std::numeric_limits<int>::max()));
}

} // namespace

QString syncthingDefaultUrl()
{
    return u"http://127.0.0.1:8384"_s;
}

double completionPercent(qint64 needBytes, qint64 globalBytes)
{
    if (globalBytes <= 0)
        return needBytes > 0 ? 0.0 : 100.0;
    const double pct = 100.0 * static_cast<double>(globalBytes - qMin(needBytes, globalBytes))
        / static_cast<double>(globalBytes);
    return qBound(0.0, std::floor(pct * 10.0) / 10.0, 100.0);
}

QString normalizeFsPath(const QString &path, const QString &home)
{
    static const QRegularExpression duplicateSlashes(u"/{2,}"_s);
    QString s = path.trimmed();
    s.replace(u'\\', u'/');
    if (!home.isEmpty() && (s == u"~"_s || s.startsWith(u"~/"_s))) {
        QString h = home;
        s = trimTrailingSlashes(h.replace(u'\\', u'/')) + s.mid(1);
    }
    s.replace(duplicateSlashes, u"/"_s);
    if (s.size() > 1)
        s = trimTrailingSlashes(s);
    if (windowsDrive().match(s).hasMatch())
        s[0] = s.at(0).toLower();
    return s.normalized(QString::NormalizationForm_C);
}

std::optional<QString> guessHome(const QString &absPath)
{
    static const QRegularExpression re(
        u"^(/home/[^/]+|/Users/[^/]+|/root|/data/data/[^/]+|[a-z]:/Users/[^/]+)(?:/|$)"_s,
        QRegularExpression::CaseInsensitiveOption);
    QString s = absPath;
    s.replace(u'\\', u'/');
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch())
        return std::nullopt;
    return m.captured(1);
}

std::optional<SyncthingFolder> findFolderForPath(const QList<SyncthingFolder> &folders, const QString &absPath,
                                                 const QString &home)
{
    const QString h = !home.isEmpty() ? home : guessHome(absPath).value_or(QString());
    const QString target = normalizeFsPath(absPath, h);
    const bool windows = windowsDrive().match(target).hasMatch();
    std::optional<SyncthingFolder> best;
    qsizetype bestLength = -1;
    for (const SyncthingFolder &folder : folders) {
        if (folder.path.isEmpty())
            continue;
        const QString dir = normalizeFsPath(folder.path, h);
        if (dir.startsWith(u'~'))
            continue;
        if (isWithinFsPath(target, dir, windows) && dir.size() > bestLength) {
            best = folder;
            bestLength = dir.size();
        }
    }
    return best;
}

QString stignoreTemplate()
{
    return u"// Brulion — zalecany plik .stignore dla folderu z notatkami.\n"
           "// Syncthing nie synchronizuje samego pliku .stignore: skopiuj go na każde urządzenie.\n"
           "// Prefiks (?d) pozwala Syncthingowi usunąć taki plik, gdy blokuje usunięcie folderu.\n"
           "\n"
           "// Kosz Brulionu — usunięte notatki zostają lokalnie na każdym urządzeniu.\n"
           "/.trash\n"
           "\n"
           "// Pliki systemowe macOS i Windows.\n"
           "(?d).DS_Store\n"
           "(?d)Thumbs.db\n"
           "\n"
           "// Układ okien Obsidiana — każde urządzenie ma własny.\n"
           "/.obsidian/workspace*.json\n"
           "\n"
           "// Kopie zapasowe i kosz Logseqa.\n"
           "/logseq/bak\n"
           "/logseq/.recycle\n"
           "\n"
           "// Pliki tymczasowe (także zapisy w toku Brulionu).\n"
           "*.tmp\n"
           "(?d).brulion-tmp-*\n"_s;
}

// ---------------------------------------------------------------------------------------------
// SyncthingClient
// ---------------------------------------------------------------------------------------------

SyncthingClient::SyncthingClient(const QString &apiKey, HttpTransport *transport, const QString &url, QObject *parent)
    : QObject(parent)
    , m_apiKey(apiKey)
    , m_transport(transport)
    , m_url(trimTrailingSlashes(url.trimmed()))
{
    if (m_url.isEmpty())
        m_url = syncthingDefaultUrl();
}

SyncthingClient::~SyncthingClient() = default;

void SyncthingClient::getJson(const QString &path, JsonCallback done)
{
    HttpRequest request;
    request.method = "GET";
    request.url = QUrl(m_url + path, QUrl::TolerantMode);
    request.headers = {{"X-API-Key", m_apiKey.toUtf8()}, {"Accept", "application/json"}};
    request.timeoutMs = m_timeoutMs;
    detail::sendRequest(m_transport, this, request, [this, path, done = std::move(done)](const HttpResponse &response) {
        if (response.status == 0) {
            done({}, SyncthingError{0, u"Syncthing is not reachable at %1: %2"_s.arg(m_url, response.networkError)});
            return;
        }
        if (response.status < 200 || response.status >= 300) {
            const QString hint = response.status == 401 || response.status == 403 ? u" (wrong API key?)"_s : QString();
            done({}, SyncthingError{response.status,
                                    u"GET %1 failed: HTTP %2%3"_s.arg(path, QString::number(response.status), hint)});
            return;
        }
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(response.body, &error);
        if (error.error != QJsonParseError::NoError) {
            done({}, SyncthingError{response.status, u"GET %1: invalid JSON"_s.arg(path)});
            return;
        }
        done(doc, {});
    });
}

void SyncthingClient::ping(std::function<void(bool)> done)
{
    getJson(u"/rest/system/ping"_s, [done = std::move(done)](const QJsonDocument &doc, const SyncthingError &error) {
        done(!error.isError() && doc.object().value(u"ping"_s).toString() == u"pong"_s);
    });
}

void SyncthingClient::folders(std::function<void(const FoldersResult &)> done)
{
    getJson(u"/rest/config/folders"_s, [done = std::move(done)](const QJsonDocument &doc, const SyncthingError &error) {
        FoldersResult result;
        if (error.isError()) {
            result.error = error;
        } else if (!doc.isArray()) {
            result.error = SyncthingError{200, u"Unexpected /rest/config/folders response"_s};
        } else {
            const QJsonArray array = doc.array();
            for (const QJsonValue &value : array) {
                const QJsonObject o = value.toObject();
                const QString id = o.value(u"id"_s).toString();
                if (id.isEmpty())
                    continue;
                SyncthingFolder folder;
                folder.id = id;
                folder.label = o.value(u"label"_s).toString();
                if (folder.label.isEmpty())
                    folder.label = id;
                folder.path = o.value(u"path"_s).toString();
                if (const QJsonValue paused = o.value(u"paused"_s); paused.isBool())
                    folder.paused = paused.toBool();
                result.folders.append(folder);
            }
        }
        done(result);
    });
}

void SyncthingClient::connections(std::function<void(const ConnectionsResult &)> done)
{
    getJson(u"/rest/system/connections"_s,
            [done = std::move(done)](const QJsonDocument &doc, const SyncthingError &error) {
                ConnectionsResult result;
                result.error = error;
                const QJsonObject connections = doc.object().value(u"connections"_s).toObject();
                for (auto it = connections.constBegin(); it != connections.constEnd(); ++it) {
                    if (it.value().toObject().value(u"connected"_s).toBool())
                        ++result.connected;
                }
                done(result);
            });
}

void SyncthingClient::status(const QString &folderId, std::function<void(const SyncthingStatus &)> done)
{
    // The folder (or daemon) status and the connection count are fetched in parallel.
    struct Join {
        int pending = 2;
        bool withConnections = false;
        std::optional<int> connected;
        SyncthingStatus status;
        std::function<void(const SyncthingStatus &)> done;

        void arrive()
        {
            if (--pending > 0)
                return;
            if (withConnections)
                status.connected = connected;
            done(status);
        }
    };
    auto join = std::make_shared<Join>();
    join->done = std::move(done);
    connections([join](const ConnectionsResult &result) {
        if (!result.error.isError())
            join->connected = result.connected;
        join->arrive();
    });
    const QString id = folderId.isEmpty() ? m_folderId : folderId;
    if (id.isEmpty()) {
        ping([join](bool ok) {
            join->status.state = ok ? u"idle"_s : u"offline"_s;
            join->status.completion = ok ? 100 : 0;
            if (!ok)
                join->status.error = u"unreachable"_s;
            join->withConnections = true;
            join->arrive();
        });
        return;
    }
    const QString path = u"/rest/db/status?folder="_s + QString::fromLatin1(QUrl::toPercentEncoding(id));
    getJson(path, [join, id](const QJsonDocument &doc, const SyncthingError &error) {
        SyncthingStatus &status = join->status;
        status.folderId = id;
        if (error.isError()) {
            status.state = u"offline"_s;
            status.error = error.message;
        } else {
            const QJsonObject o = doc.object();
            status.state = o.value(u"state"_s).toString();
            if (status.state.isEmpty())
                status.state = u"unknown"_s;
            status.needBytes = jsonCount(o.value(u"needBytes"_s));
            status.globalBytes = jsonCount(o.value(u"globalBytes"_s));
            status.completion = completionPercent(status.needBytes, status.globalBytes);
            status.errors = sumCounts(jsonCount(o.value(u"errors"_s)), jsonCount(o.value(u"pullErrors"_s)));
            join->withConnections = true;
        }
        join->arrive();
    });
}

void SyncthingClient::findFolderForPath(const QString &absPath,
                                        std::function<void(const std::optional<SyncthingFolder> &, const SyncthingError &)> done)
{
    folders([this, absPath, done = std::move(done)](const FoldersResult &result) {
        if (result.error.isError()) {
            done(std::nullopt, result.error);
            return;
        }
        done(sync::findFolderForPath(result.folders, absPath, m_home), {});
    });
}

} // namespace brulion::sync
