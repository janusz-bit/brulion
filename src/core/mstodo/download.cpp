#include "mstodo/download.h"

#include "mstodo/messages_p.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>

namespace brulion::mstodo {

using namespace Qt::StringLiterals;
using detail::MsTodoMessages;

namespace {

QByteArray segment(const QString &id)
{
    return QUrl::toPercentEncoding(id);
}

/// Bytes a base64 text decodes to (about).
qint64 decodedSize(qsizetype base64Length)
{
    return qint64(base64Length) / 4 * 3;
}

} // namespace

TodoDownload::TodoDownload(sync::HttpTransport *transport, QObject *parent) : QObject(parent), m_transport(transport)
{
}

TodoDownload::~TodoDownload() = default;

QUrl TodoDownload::graph(const QString &path, const QString &query) const
{
    QByteArray url = "https://" + GraphHost.toLatin1() + "/v1.0" + path.toUtf8();
    if (!query.isEmpty())
        url += '?' + query.toUtf8();
    return QUrl::fromEncoded(url, QUrl::StrictMode);
}

bool TodoDownload::isGraphUrl(const QUrl &url) const
{
    return url.isValid() && url.scheme() == u"https" && url.host().compare(GraphHost, Qt::CaseInsensitive) == 0
           && url.userInfo().isEmpty() && (url.port() == -1 || url.port() == 443);
}

void TodoDownload::start(const QString &accessToken)
{
    cancel();
    m_token = accessToken;
    m_secret.reset(accessToken);
    m_active = true;
    m_lists.clear();
    m_jobs.clear();
    m_warnings.clear();
    m_taskCount = 0;
    m_attachmentBytes = 0;
    m_expand = true;
    collect(graph(u"/me/todo/lists"_s), {}, [this](const QJsonArray &items, bool incomplete) {
        for (const QJsonValue &v : items) {
            if (v.isObject() && !v.toObject().value(u"id"_s).toString().isEmpty())
                m_lists.append(ListData{v.toObject(), {}, false});
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
    cancel();
    Q_EMIT failed(code, logging::redact(message));
}

void TodoDownload::failWith(const sync::HttpResponse &response)
{
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
                seconds = int(std::pow(2, attempt)); // 1, 2, 4, 8 s
            seconds = std::clamp(seconds, 1, d->m_limits.maxRetryAfterSeconds);
            QTimer::singleShot(seconds * d->m_unitMs, d, [self, generation, url, ok, failure, attempt] {
                if (self && self->m_generation == generation && self->m_active)
                    self->get(url, ok, failure, attempt + 1);
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
    get(
            url,
            [this, items, done, failure](const QJsonObject &page) mutable {
                for (const QJsonValue &v : page.value(u"value"_s).toArray())
                    items.append(v);
                const QString next = page.value(u"@odata.nextLink"_s).toString();
                if (next.isEmpty()) {
                    done(items, false);
                    return;
                }
                const QUrl nextUrl(next);
                if (!isGraphUrl(nextUrl) || items.size() >= m_limits.maxTasks) {
                    done(items, true); // never sent anywhere else
                    return;
                }
                collect(nextUrl, items, done, failure);
            },
            failure);
}

void TodoDownload::fetchList(qsizetype index)
{
    if (index >= m_lists.size()) {
        finish();
        return;
    }
    Q_EMIT progress(int(index), int(m_lists.size()), m_taskCount);
    if (!m_active)
        return;
    fetchTasks(index, m_expand);
}

void TodoDownload::fetchTasks(qsizetype index, bool expand)
{
    const QString id = m_lists[index].list.value(u"id"_s).toString();
    const QString path = u"/me/todo/lists/"_s + QString::fromLatin1(segment(id)) + u"/tasks"_s;
    const QString query = expand ? u"%24top=100&%24expand=checklistItems,linkedResources"_s : u"%24top=100"_s;
    collect(
            graph(path, query), {},
            [this, index](const QJsonArray &items, bool incomplete) {
                ListData &list = m_lists[index];
                for (const QJsonValue &v : items) {
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
                    Q_EMIT progress(int(index) + 1, int(m_lists.size()), m_taskCount);
                    if (m_active)
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
    const QString listPath =
            u"/me/todo/lists/"_s + QString::fromLatin1(segment(m_lists[index].list.value(u"id"_s).toString()));
    for (qsizetype t = 0; t < m_lists[index].tasks.size(); ++t) {
        const QJsonObject &task = m_lists[index].tasks[t];
        const QString taskPath =
                listPath + u"/tasks/"_s + QString::fromLatin1(segment(task.value(u"id"_s).toString()));
        if (task.value(u"id"_s).toString().isEmpty())
            continue;
        const auto detail = [this, index, t, taskPath](const QString &relation) {
            m_jobs.append([this, index, t, taskPath, relation](std::function<void()> next) {
                collect(
                        graph(taskPath + u'/' + relation), {},
                        [this, index, t, relation, next](const QJsonArray &items, bool) {
                            m_lists[index].tasks[t].insert(relation, items);
                            next();
                        },
                        [this, next](const sync::HttpResponse &response) {
                            if (response.status == 401 || response.status == 403) {
                                failWith(response);
                                return;
                            }
                            warn(MsTodoMessages::tr("Some steps, links or attachments could not be downloaded."));
                            next();
                        });
            });
        };
        if (!m_expand && !task.contains(u"checklistItems"_s)) {
            detail(u"checklistItems"_s);
            detail(u"linkedResources"_s);
        }
        if (task.value(u"hasAttachments"_s).toBool()) {
            m_jobs.append([this, index, t, taskPath](std::function<void()> next) {
                collect(
                        graph(taskPath + u"/attachments"_s), {},
                        [this, index, t, taskPath, next](const QJsonArray &items, bool) {
                            // The attachments one by one: each within the size budget.
                            auto kept = std::make_shared<QJsonArray>();
                            auto queue = std::make_shared<QList<QJsonObject>>();
                            for (const QJsonValue &v : items) {
                                if (v.isObject())
                                    queue->append(v.toObject());
                            }
                            // The step holds itself only weakly; whoever calls it (this function, a
                            // request's callback) keeps it alive.
                            auto step = std::make_shared<std::function<void()>>();
                            std::weak_ptr<std::function<void()>> weak = step;
                            *step = [this, index, t, taskPath, next, kept, queue, weak] {
                                const auto again = [weak] {
                                    if (const auto s = weak.lock())
                                        (*s)();
                                };
                                if (queue->isEmpty()) {
                                    m_lists[index].tasks[t].insert(u"attachments"_s, *kept);
                                    next();
                                    return;
                                }
                                const QJsonObject a = queue->takeFirst();
                                const qint64 size = qint64(a.value(u"size"_s).toDouble());
                                if (size > m_limits.maxAttachmentBytes) {
                                    warn(MsTodoMessages::tr("Attachments larger than 25 MB were left out."));
                                    again();
                                    return;
                                }
                                if (m_attachmentBytes + size > m_limits.maxTotalAttachmentBytes) {
                                    warn(MsTodoMessages::tr("Some attachments were left out: together they are too "
                                                            "large to import at once."));
                                    again();
                                    return;
                                }
                                const auto accept = [this, kept](const QJsonObject &full) {
                                    const qint64 bytes = decodedSize(full.value(u"contentBytes"_s).toString().size());
                                    if (bytes > m_limits.maxAttachmentBytes) {
                                        warn(MsTodoMessages::tr("Attachments larger than 25 MB were left out."));
                                        return;
                                    }
                                    if (m_attachmentBytes + bytes > m_limits.maxTotalAttachmentBytes) {
                                        warn(MsTodoMessages::tr("Some attachments were left out: together they "
                                                                "are too large to import at once."));
                                        return;
                                    }
                                    m_attachmentBytes += bytes;
                                    kept->append(full);
                                };
                                if (a.contains(u"contentBytes"_s)) {
                                    accept(a);
                                    again();
                                    return;
                                }
                                const QString id = a.value(u"id"_s).toString();
                                if (id.isEmpty()) {
                                    again();
                                    return;
                                }
                                get(
                                        graph(taskPath + u"/attachments/"_s + QString::fromLatin1(segment(id))),
                                        [accept, s = weak.lock()](const QJsonObject &full) {
                                            accept(full);
                                            (*s)();
                                        },
                                        [this, s = weak.lock()](const sync::HttpResponse &response) {
                                            if (response.status == 401 || response.status == 403) {
                                                failWith(response);
                                                return;
                                            }
                                            warn(MsTodoMessages::tr(
                                                    "Some steps, links or attachments could not be downloaded."));
                                            (*s)();
                                        });
                            };
                            (*step)();
                        },
                        [this, next](const sync::HttpResponse &response) {
                            if (response.status == 401 || response.status == 403) {
                                failWith(response);
                                return;
                            }
                            warn(MsTodoMessages::tr("Some steps, links or attachments could not be downloaded."));
                            next();
                        });
            });
        }
    }
}

void TodoDownload::runJobs(std::function<void()> then)
{
    if (!m_active)
        return;
    if (m_jobs.isEmpty()) {
        then();
        return;
    }
    const auto job = m_jobs.takeFirst();
    job([this, then] { runJobs(then); });
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
    cancel(); // forgets the token
    m_lists.clear();
    Q_EMIT finished(QJsonDocument(bundle).toJson(QJsonDocument::Compact), warnings);
}

} // namespace brulion::mstodo
