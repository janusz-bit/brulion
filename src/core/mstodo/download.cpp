#include "mstodo/download.h"

#include "mstodo/messages_p.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

#include <algorithm>

namespace brulion::mstodo {

using namespace Qt::StringLiterals;
using detail::MsTodoMessages;

namespace {

/// `id` as one percent-encoded path segment.
QString segment(const QString &id)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(id));
}

/// A Graph v1.0 address; `path` and `query` are already percent-encoded.
QUrl graph(const QString &path, const QString &query = {})
{
    QByteArray url = "https://" + GraphHost.toLatin1() + "/v1.0" + path.toUtf8();
    if (!query.isEmpty())
        url += '?' + query.toUtf8();
    return QUrl::fromEncoded(url, QUrl::StrictMode);
}

bool isGraphUrl(const QUrl &url)
{
    return url.isValid() && url.scheme() == u"https" && url.host().compare(GraphHost, Qt::CaseInsensitive) == 0
           && url.userInfo().isEmpty() && (url.port() == -1 || url.port() == 443);
}

/// Bytes a base64 text decodes to (about).
qint64 decodedSize(qsizetype base64Length)
{
    return qint64(base64Length) / 4 * 3;
}

/// The size an attachment claims: 0 for none or a negative one, at most 1e15 (casting a larger
/// double to an integer would be undefined).
qint64 declaredSize(const QJsonObject &attachment)
{
    const double size = attachment.value(u"size"_s).toDouble();
    return size > 0 ? qint64(std::min(size, 1e15)) : 0;
}

} // namespace

struct TodoDownload::Attachments {
    qsizetype list = 0;
    qsizetype task = 0;
    QString path;             ///< the task's, in Graph
    QList<QJsonObject> queue; ///< still to come
    QJsonArray kept;
    Next next;
};

TodoDownload::TodoDownload(sync::HttpTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport) { }

TodoDownload::~TodoDownload() = default;

void TodoDownload::start(const QString &accessToken)
{
    cancel(); // also drops what an earlier download left
    m_token = accessToken;
    m_secret.reset(accessToken);
    m_active = true;
    m_warnings.clear();
    m_taskCount = 0;
    m_attachmentBytes = 0;
    m_expand = true;
    collect(graph(u"/me/todo/lists"_s), {}, [this](const QJsonArray &items, bool incomplete) {
        for (const auto &v : items) {
            if (const QJsonObject list = v.toObject(); !list.value(u"id"_s).toString().isEmpty())
                m_lists.append(ListData{list, {}, false});
        }
        if (incomplete)
            warn(MsTodoMessages::tr("Not every list could be downloaded."));
        fetchList(0);
    });
}

void TodoDownload::cancel()
{
    ++m_generation;
    m_active = false;
    m_lists.clear(); // tasks and attachments (up to the size budget): not kept after the end
    m_jobs.clear();
    m_token.clear();
    m_secret.reset();
}

void TodoDownload::warn(const QString &message)
{
    if (!m_warnings.contains(message))
        m_warnings.append(message);
}

void TodoDownload::fail(const QString &code, const QString &message)
{
    // Redacted while the token is still registered: cancel() forgets it.
    const QString text = logging::redact(message);
    cancel();
    Q_EMIT failed(code, text);
}

TodoDownload::Failure TodoDownload::skipping(Next next, bool timeoutFatal)
{
    return [this, next = std::move(next), timeoutFatal](const sync::HttpResponse &response) {
        // No network (after the retries), a sign-in that expired, no permission: the next request
        // would fail the same way, so the download stops instead of going on job by job.
        const bool lost = response.status == 0 && (timeoutFatal || !response.timedOut);
        if (lost || response.status == 401 || response.status == 403) {
            failWith(response);
            return;
        }
        warn(MsTodoMessages::tr("Some steps, links or attachments could not be downloaded."));
        next();
    };
}

void TodoDownload::failWith(const sync::HttpResponse &response)
{
    // Graph's error answer {"error": {"code", "message"}}; any other body gives the general texts.
    const QJsonObject error = QJsonDocument::fromJson(response.body).object().value(u"error"_s).toObject();
    const QString code = error.value(u"code"_s).toString();
    if (response.status == 0) {
        fail(u"network"_s, response.timedOut ? MsTodoMessages::tr("Microsoft Graph did not answer in time. Try again.")
                                             : MsTodoMessages::tr("Microsoft Graph cannot be reached. Check the "
                                                                  "internet connection."));
    } else if (response.status == 401) {
        fail(u"unauthorized"_s, MsTodoMessages::tr("Microsoft did not accept the sign-in (it may have expired). "
                                                   "Sign in again."));
    } else if (response.status == 403) {
        fail(u"forbidden"_s, MsTodoMessages::tr("Brulion may not read your tasks. Add the delegated permission "
                                                "Tasks.Read to the app registration and sign in again."));
    } else if (code.contains(u"Mailbox"_s, Qt::CaseInsensitive)) {
        fail(u"no-mailbox"_s, MsTodoMessages::tr("Microsoft To Do is not available for this account (it has no "
                                                 "Exchange Online mailbox)."));
    } else if (response.status == 429 || response.status == 503 || response.status == 504) {
        fail(u"throttled"_s, MsTodoMessages::tr("Microsoft is limiting requests right now. Try again in a few "
                                                "minutes."));
    } else {
        QString detail = error.value(u"message"_s).toString().simplified().left(300);
        if (detail.isEmpty())
            detail = code;
        fail(u"server"_s, detail.isEmpty()
                                  ? MsTodoMessages::tr("Microsoft Graph answered with an error (HTTP %1).")
                                            .arg(response.status)
                                  : MsTodoMessages::tr("Microsoft Graph answered with an error (HTTP %1): %2")
                                            .arg(response.status)
                                            .arg(detail));
    }
}

void TodoDownload::get(const QUrl &url, Json ok, Failure failure, int attempt)
{
    sync::HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.headers = {{"Authorization", "Bearer " + m_token.toUtf8()}, {"Accept", "application/json"}};
    request.timeoutMs = m_limits.timeoutMs;
    const int generation = m_generation;
    QPointer<TodoDownload> self(this);
    m_transport->send(request, [self, generation, url, ok = std::move(ok), failure = std::move(failure),
                                attempt](const sync::HttpResponse &response) {
        if (!self || self->m_generation != generation || !self->m_active)
            return;
        TodoDownload *d = self.data();
        const int status = response.status;
        if (status >= 200 && status < 300) {
            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(response.body, &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                d->fail(u"server"_s, MsTodoMessages::tr("Microsoft Graph sent an answer Brulion does not understand."));
                return;
            }
            ok(doc.object());
            return;
        }
        const bool retry = status == 429 || status == 503 || status == 504 || (status == 0 && !response.timedOut);
        if (retry && attempt < d->m_limits.maxRetries) {
            bool numeric = false;
            int seconds = response.header("retry-after").trimmed().toInt(&numeric);
            if (!numeric || seconds < 0)
                seconds = 1 << std::min(attempt, 30); // 1, 2, 4, 8 s
            seconds = std::clamp(seconds, 1, d->m_limits.maxRetryAfterSeconds);
            // `d` is the context object: the retry never runs after the download is gone.
            QTimer::singleShot(seconds * d->m_unitMs, d, [d, generation, url, ok, failure, attempt] {
                if (d->m_generation == generation && d->m_active)
                    d->get(url, ok, failure, attempt + 1);
            });
            return;
        }
        if (failure)
            failure(response);
        else
            d->failWith(response);
    });
}

void TodoDownload::collect(const QUrl &url, QJsonArray items, std::function<void(QJsonArray, bool)> done,
                           Failure failure)
{
    // Called once (the transport's contract): the pages so far move on to the next request.
    get(
            url,
            [this, items = std::move(items), done = std::move(done), failure](const QJsonObject &page) mutable {
                const QJsonArray values = page.value(u"value"_s).toArray();
                for (const auto &v : values)
                    items.append(v);
                const QString next = page.value(u"@odata.nextLink"_s).toString();
                if (next.isEmpty()) {
                    done(std::move(items), false);
                    return;
                }
                const QUrl nextUrl(next);
                if (!isGraphUrl(nextUrl) || items.size() >= m_limits.maxTasks) {
                    done(std::move(items), true); // never sent anywhere else
                    return;
                }
                collect(nextUrl, std::move(items), std::move(done), std::move(failure));
            },
            failure);
}

bool TodoDownload::reportProgress(qsizetype listsDone)
{
    const int generation = m_generation;
    Q_EMIT progress(int(listsDone), int(m_lists.size()), m_taskCount);
    return m_generation == generation;
}

void TodoDownload::fetchList(qsizetype index)
{
    if (index >= m_lists.size())
        finish();
    else if (reportProgress(index))
        fetchTasks(index, m_expand);
}

void TodoDownload::fetchTasks(qsizetype index, bool expand)
{
    const QString path = u"/me/todo/lists/"_s + segment(m_lists.at(index).list.value(u"id"_s).toString()) + u"/tasks"_s;
    const QString query = expand ? u"%24top=100&%24expand=checklistItems,linkedResources"_s : u"%24top=100"_s;
    collect(
            graph(path, query), {},
            [this, index](const QJsonArray &items, bool incomplete) {
                ListData &list = m_lists[index];
                for (const auto &v : items) {
                    if (!v.isObject())
                        continue;
                    if (m_taskCount >= m_limits.maxTasks) {
                        incomplete = true;
                        break;
                    }
                    list.tasks.append(v.toObject());
                    ++m_taskCount;
                }
                list.incomplete = incomplete;
                queueTaskDetails(index);
                runJobs([this, index] {
                    if (reportProgress(index + 1))
                        fetchList(index + 1);
                });
            },
            [this, index, expand](const sync::HttpResponse &response) {
                // Graph may refuse $expand on tasks: then steps and links come task by task.
                if (expand && response.status == 400) {
                    m_expand = false;
                    fetchTasks(index, false);
                    return;
                }
                // A list deleted meanwhile: the others still come.
                if (response.status == 404) {
                    warn(MsTodoMessages::tr("Not every list could be downloaded."));
                    fetchList(index + 1);
                    return;
                }
                failWith(response);
            });
}

void TodoDownload::queueTaskDetails(qsizetype index)
{
    const ListData &list = m_lists.at(index);
    const QString listPath = u"/me/todo/lists/"_s + segment(list.list.value(u"id"_s).toString());
    for (qsizetype t = 0; t < list.tasks.size(); ++t) {
        const QJsonObject &task = list.tasks.at(t);
        const QString id = task.value(u"id"_s).toString();
        if (id.isEmpty())
            continue;
        const QString taskPath = listPath + u"/tasks/"_s + segment(id);
        // Without $expand (refused), or when Graph answered but left the expansion out (an
        // expanded collection is always there, if empty): task by task.
        for (const QString &relation : {u"checklistItems"_s, u"linkedResources"_s}) {
            if (task.contains(relation))
                continue;
            m_jobs.append([this, index, t, taskPath, relation](const Next &next) {
                collect(
                        graph(taskPath + u'/' + relation), {},
                        [this, index, t, relation, next](const QJsonArray &items, bool) {
                            m_lists[index].tasks[t].insert(relation, items);
                            next();
                        },
                        skipping(next));
            });
        }
        if (task.value(u"hasAttachments"_s).toBool()) {
            m_jobs.append([this, index, t, taskPath](const Next &next) {
                collect(
                        graph(taskPath + u"/attachments"_s), {},
                        [this, index, t, taskPath, next](const QJsonArray &items, bool) {
                            QList<QJsonObject> queue;
                            for (const auto &v : items) {
                                if (v.isObject())
                                    queue.append(v.toObject());
                            }
                            fetchAttachments(std::make_shared<Attachments>(
                                    Attachments{index, t, taskPath, std::move(queue), {}, next}));
                        },
                        skipping(next));
            });
        }
    }
}

bool TodoDownload::fitsBudget(qint64 bytes)
{
    if (bytes > m_limits.maxAttachmentBytes) {
        warn(MsTodoMessages::tr("Attachments larger than 25 MB were left out."));
        return false;
    }
    if (m_attachmentBytes + bytes > m_limits.maxTotalAttachmentBytes) {
        warn(MsTodoMessages::tr("Some attachments were left out: together they are too large to import at once."));
        return false;
    }
    return true;
}

void TodoDownload::fetchAttachments(const std::shared_ptr<Attachments> &a)
{
    const auto keep = [this, a](const QJsonObject &full) {
        const qint64 bytes = decodedSize(full.value(u"contentBytes"_s).toString().size());
        if (fitsBudget(bytes)) {
            m_attachmentBytes += bytes;
            a->kept.append(full);
        }
    };
    // One by one, each within the size budget. A loop (no recursion) over those that need no
    // request: the stack stays flat however many a task has.
    while (!a->queue.isEmpty()) {
        const QJsonObject attachment = a->queue.takeFirst();
        if (!fitsBudget(declaredSize(attachment)))
            continue;
        if (attachment.contains(u"contentBytes"_s)) {
            keep(attachment);
            continue;
        }
        const QString id = attachment.value(u"id"_s).toString();
        if (id.isEmpty())
            continue;
        // A large file may take longer than the time limit on a slow line: only that file is left
        // out.
        get(
                graph(a->path + u"/attachments/"_s + segment(id)),
                [this, a, keep](const QJsonObject &full) {
                    keep(full);
                    fetchAttachments(a);
                },
                skipping([this, a] { fetchAttachments(a); }, false));
        return;
    }
    m_lists[a->list].tasks[a->task].insert(u"attachments"_s, a->kept);
    a->next();
}

void TodoDownload::runJobs(Next then)
{
    if (!m_active)
        return;
    if (m_jobs.isEmpty()) {
        then();
        return;
    }
    const Job job = m_jobs.takeFirst();
    job([this, then = std::move(then)] { runJobs(then); });
}

void TodoDownload::finish()
{
    QJsonArray lists;
    for (const ListData &data : std::as_const(m_lists)) {
        QJsonObject list = data.list;
        QJsonArray tasks;
        for (const QJsonObject &task : data.tasks)
            tasks.append(task);
        list.insert(u"tasks"_s, tasks);
        if (data.incomplete)
            list.insert(u"tasks@odata.nextLink"_s, u"(not downloaded)"_s);
        lists.append(list);
    }
    const QJsonObject bundle{{u"brulion"_s, u"mstodo-export"_s},
                             {u"version"_s, 1},
                             {u"exported"_s, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                             {u"lists"_s, lists}};
    const QStringList warnings = m_warnings;
    cancel(); // forgets the token and the lists
    Q_EMIT finished(QJsonDocument(bundle).toJson(QJsonDocument::Compact), warnings);
}

} // namespace brulion::mstodo
